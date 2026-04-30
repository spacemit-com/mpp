/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux.c
 * @Date      :    2026-04-15
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    MUX module implementation for MPP.
 *------------------------------------------------------------------------------
 */

#include "mux/mux_api.h"
#include "mux_rtsp_internal.h"
#include "sys/mpp_shm.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sys/sys_api.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>

#define MUX_LOGE(fmt, ...) fprintf(stderr, "[MUX][ERR] " fmt "\n", ##__VA_ARGS__)
#define MUX_LOGI(fmt, ...) fprintf(stdout, "[MUX][INF] " fmt "\n", ##__VA_ARGS__)

#define MUX_STATE_IDLE      0
#define MUX_STATE_CREATED   1
#define MUX_STATE_RUNNING   2

typedef struct _MuxContext {
    S32             s32Init;
    pthread_mutex_t lock;
    MuxChannel      astChn[MUX_MAX_CHN];
} MuxContext;

static MuxContext g_stMuxCtx = {0};

static S32 mux_check_chn(S32 s32ChnId)
{
    if (s32ChnId < 0 || s32ChnId >= MUX_MAX_CHN) {
        return ERR_MUX_INVALID_CHN;
    }
    return ERR_MUX_OK;
}

static enum AVCodecID mux_codec_to_ffmpeg(MuxCodecType eCodecType)
{
    switch (eCodecType) {
        case MUX_CODEC_H264:
            return AV_CODEC_ID_H264;
        case MUX_CODEC_H265:
            return AV_CODEC_ID_HEVC;
        case MUX_CODEC_MJPEG:
            return AV_CODEC_ID_MJPEG;
        default:
            return AV_CODEC_ID_NONE;
    }
}

static MuxCodecType mux_codec_from_stream(MppStreamCodecType eCodecType)
{
    switch (eCodecType) {
        case MPP_STREAM_CODEC_H264:
            return MUX_CODEC_H264;
        case MPP_STREAM_CODEC_H265:
            return MUX_CODEC_H265;
        case MPP_STREAM_CODEC_MJPEG:
            return MUX_CODEC_MJPEG;
        default:
            return MUX_CODEC_UNKNOWN;
    }
}

static VOID *mux_bind_worker(VOID *arg)
{
    MuxChannel *pstChn = (MuxChannel *)arg;
    U8 *pu8Buf;
    S32 ret = 0;

    if (!pstChn) {
        return NULL;
    }

    pu8Buf = (U8 *)malloc(MPP_STREAM_MAX_PAYLOAD);
    if (!pu8Buf) {
        return NULL;
    }

    pstChn->s32WorkerAlive = 1;
    while (!pstChn->s32StopWorker) {
        StreamBufferInfo stStream;
        MuxPacket stPkt;

        memset(&stStream, 0, sizeof(stStream));
        stStream.pu8Addr = pu8Buf;
        stStream.u32Size = MPP_STREAM_MAX_PAYLOAD;
        ret = SYS_RecvStream(&pstChn->stSinkNode, &stStream, 100);
        if (ret != 0) {
            if (SYS_ERR_NOT_FOUND == ret) {
                usleep(20000); // Sleep 20ms before retrying to avoid busy loop when no stream is bound
            }
            continue;
        }

        memset(&stPkt, 0, sizeof(stPkt));
        stPkt.pu8Data = (U8 *)stStream.pu8Addr;
        stPkt.u32Size = stStream.u32Size;
        stPkt.bKeyFrame = stStream.bKeyFrame;
        stPkt.eCodecType = mux_codec_from_stream(stStream.eCodecType);
        stPkt.u64PTS = stStream.u64PTS;
        (VOID)MUX_SendPacket(pstChn->s32ChnId, &stPkt);
    }

    pstChn->s32WorkerAlive = 0;
    free(pu8Buf);
    return NULL;
}

static S32 mux_open_output(MuxChannel *pstChn)
{
    enum AVCodecID eCodecId;
    S32 ret;

    if (!pstChn) {
        return ERR_MUX_NULL_PTR;
    }

    if (pstChn->stAttr.eOutputType == MUX_OUTPUT_RTSP) {
        return mux_rtsp_server_start(pstChn);
    }

    eCodecId = mux_codec_to_ffmpeg(pstChn->stAttr.stStreamAttr.eCodecType);
    if (eCodecId == AV_CODEC_ID_NONE) {
        return ERR_MUX_OPEN_FAIL;
    }

    ret = avformat_alloc_output_context2(&pstChn->pstFmt, NULL, NULL, pstChn->stAttr.szUrl);
    if (ret < 0 || !pstChn->pstFmt) {
        MUX_LOGE("avformat_alloc_output_context2 failed, ret=%d, url=%s", ret, pstChn->stAttr.szUrl);
        return ERR_MUX_OPEN_FAIL;
    }

    pstChn->pstStream = avformat_new_stream(pstChn->pstFmt, NULL);
    if (!pstChn->pstStream) {
        MUX_LOGE("avformat_new_stream failed, chn=%d", pstChn->s32ChnId);
        avformat_free_context(pstChn->pstFmt);
        pstChn->pstFmt = NULL;
        return ERR_MUX_NOMEM;
    }

    pstChn->pstStream->time_base = (AVRational){1, 1000000};
    pstChn->pstStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    pstChn->pstStream->codecpar->codec_id = eCodecId;
    pstChn->pstStream->codecpar->width = (int)pstChn->stAttr.stStreamAttr.u32Width;
    pstChn->pstStream->codecpar->height = (int)pstChn->stAttr.stStreamAttr.u32Height;
    pstChn->pstStream->codecpar->bit_rate = (int64_t)pstChn->stAttr.stStreamAttr.u32BitrateKbps * 1000LL;

    if (!(pstChn->pstFmt->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open2(&pstChn->pstFmt->pb, pstChn->stAttr.szUrl, AVIO_FLAG_WRITE, NULL, NULL);
        if (ret < 0) {
            MUX_LOGE("avio_open2 failed, ret=%d, url=%s", ret, pstChn->stAttr.szUrl);
            avformat_free_context(pstChn->pstFmt);
            pstChn->pstFmt = NULL;
            pstChn->pstStream = NULL;
            return ERR_MUX_OPEN_FAIL;
        }
    }

    ret = avformat_write_header(pstChn->pstFmt, NULL);
    if (ret < 0) {
        MUX_LOGE("avformat_write_header failed, ret=%d", ret);
        if (!(pstChn->pstFmt->oformat->flags & AVFMT_NOFILE) && pstChn->pstFmt->pb) {
            avio_closep(&pstChn->pstFmt->pb);
        }
        avformat_free_context(pstChn->pstFmt);
        pstChn->pstFmt = NULL;
        pstChn->pstStream = NULL;
        return ERR_MUX_OPEN_FAIL;
    }

    pstChn->s32HeaderWritten = 1;
    return ERR_MUX_OK;
}

static VOID mux_close_output(MuxChannel *pstChn)
{
    if (!pstChn) {
        return;
    }

    if (pstChn->stAttr.eOutputType == MUX_OUTPUT_RTSP) {
        mux_rtsp_server_stop(pstChn);
        return;
    }

    if (!pstChn->pstFmt) {
        return;
    }

    if (pstChn->s32HeaderWritten) {
        av_write_trailer(pstChn->pstFmt);
    }
    if (!(pstChn->pstFmt->oformat->flags & AVFMT_NOFILE) && pstChn->pstFmt->pb) {
        avio_closep(&pstChn->pstFmt->pb);
    }
    avformat_free_context(pstChn->pstFmt);
    pstChn->pstFmt = NULL;
    pstChn->pstStream = NULL;
    pstChn->s32HeaderWritten = 0;
}

S32 MUX_Init(VOID)
{
    S32 i;

    if (g_stMuxCtx.s32Init) {
        return ERR_MUX_ALREADY_INIT;
    }

    if (pthread_mutex_init(&g_stMuxCtx.lock, NULL) != 0) {
        return ERR_MUX_BUSY;
    }

    for (i = 0; i < MUX_MAX_CHN; ++i) {
        MuxChannel *pstChn = &g_stMuxCtx.astChn[i];
        memset(pstChn, 0, sizeof(*pstChn));
        pstChn->s32ChnId = i;
        pstChn->stSinkNode.eModId = MPP_ID_MUX;
        pstChn->stSinkNode.s32DevId = 0;
        pstChn->stSinkNode.s32ChnId = i;
        pthread_mutex_init(&pstChn->lock, NULL);
    }

    avformat_network_init();
    g_stMuxCtx.s32Init = 1;
    return ERR_MUX_OK;
}

S32 MUX_Exit(VOID)
{
    S32 i;

    if (!g_stMuxCtx.s32Init) {
        return ERR_MUX_NOT_INIT;
    }

    for (i = 0; i < MUX_MAX_CHN; ++i) {
        if (g_stMuxCtx.astChn[i].s32Created) {
            MUX_DestroyChn(i);
        }
        pthread_mutex_destroy(&g_stMuxCtx.astChn[i].lock);
    }

    avformat_network_deinit();
    pthread_mutex_destroy(&g_stMuxCtx.lock);
    memset(&g_stMuxCtx, 0, sizeof(g_stMuxCtx));
    return ERR_MUX_OK;
}

S32 MUX_CreateChn(S32 s32ChnId, const MuxChnAttr *pstAttr)
{
    MuxChannel *pstChn;
    S32 ret;

    if (!pstAttr) {
        return ERR_MUX_NULL_PTR;
    }
    if (!g_stMuxCtx.s32Init) {
        return ERR_MUX_NOT_INIT;
    }

    ret = mux_check_chn(s32ChnId);
    if (ret != ERR_MUX_OK) {
        return ret;
    }

    pstChn = &g_stMuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_BUSY;
    }

    memcpy(&pstChn->stAttr, pstAttr, sizeof(*pstAttr));
    pstChn->s32State = MUX_STATE_CREATED;
    pstChn->s32Created = 1;
    pstChn->s32StopWorker = 0;
    pstChn->s32WorkerAlive = 0;
    pthread_mutex_unlock(&pstChn->lock);
    return ERR_MUX_OK;
}

S32 MUX_DestroyChn(S32 s32ChnId)
{
    MuxChannel *pstChn;
    S32 ret;

    if (!g_stMuxCtx.s32Init) {
        return ERR_MUX_NOT_INIT;
    }

    ret = mux_check_chn(s32ChnId);
    if (ret != ERR_MUX_OK) {
        return ret;
    }

    pstChn = &g_stMuxCtx.astChn[s32ChnId];
    MUX_StopChn(s32ChnId);

    pthread_mutex_lock(&pstChn->lock);
    pstChn->s32Created = 0;
    pstChn->s32State = MUX_STATE_IDLE;
    memset(&pstChn->stAttr, 0, sizeof(pstChn->stAttr));
    pthread_mutex_unlock(&pstChn->lock);
    return ERR_MUX_OK;
}

S32 MUX_StartChn(S32 s32ChnId)
{
    MuxChannel *pstChn;
    S32 ret;

    if (!g_stMuxCtx.s32Init) {
        return ERR_MUX_NOT_INIT;
    }

    ret = mux_check_chn(s32ChnId);
    if (ret != ERR_MUX_OK) {
        return ret;
    }

    pstChn = &g_stMuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (!pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_INVALID_CHN;
    }
    if (pstChn->s32State == MUX_STATE_RUNNING) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_OK;
    }

    ret = mux_open_output(pstChn);
    if (ret != ERR_MUX_OK) {
        pthread_mutex_unlock(&pstChn->lock);
        return ret;
    }

    pstChn->s32State = MUX_STATE_RUNNING;
    pstChn->s32StopWorker = 0;
    pthread_mutex_unlock(&pstChn->lock);

    if (pthread_create(&pstChn->tidWorker, NULL, mux_bind_worker, pstChn) != 0) {
        pthread_mutex_lock(&pstChn->lock);
        mux_close_output(pstChn);
        pstChn->s32State = MUX_STATE_CREATED;
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_BUSY;
    }

    MUX_LOGI("channel %d started, url=%s", s32ChnId, pstChn->stAttr.szUrl);
    return ERR_MUX_OK;
}

S32 MUX_StopChn(S32 s32ChnId)
{
    MuxChannel *pstChn;
    S32 ret;

    if (!g_stMuxCtx.s32Init) {
        return ERR_MUX_NOT_INIT;
    }

    ret = mux_check_chn(s32ChnId);
    if (ret != ERR_MUX_OK) {
        return ret;
    }

    pstChn = &g_stMuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (!pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_INVALID_CHN;
    }
    if (pstChn->s32State != MUX_STATE_RUNNING) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_OK;
    }

    pstChn->s32StopWorker = 1;
    pstChn->s32State = MUX_STATE_CREATED;
    pthread_mutex_unlock(&pstChn->lock);

    if (pstChn->s32WorkerAlive) {
        pthread_join(pstChn->tidWorker, NULL);
    }

    pthread_mutex_lock(&pstChn->lock);
    mux_close_output(pstChn);
    pthread_mutex_unlock(&pstChn->lock);
    return ERR_MUX_OK;
}

S32 MUX_SendPacket(S32 s32ChnId, const MuxPacket *pstPkt)
{
    MuxChannel *pstChn;
    AVPacket stPkt = {0};
    S32 ret;

    if (!pstPkt) {
        return ERR_MUX_NULL_PTR;
    }
    if (!g_stMuxCtx.s32Init) {
        return ERR_MUX_NOT_INIT;
    }

    ret = mux_check_chn(s32ChnId);
    if (ret != ERR_MUX_OK) {
        return ret;
    }

    pstChn = &g_stMuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (!pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_INVALID_CHN;
    }
    if (pstChn->s32State != MUX_STATE_RUNNING) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_NOT_STARTED;
    }

    if (pstChn->stAttr.eOutputType == MUX_OUTPUT_RTSP) {
        ret = mux_rtsp_server_send_packet(pstChn, pstPkt);
        pthread_mutex_unlock(&pstChn->lock);
        return ret;
    }

    if (!pstChn->pstFmt || !pstChn->pstStream) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_NOT_STARTED;
    }

    // av_init_packet(&stPkt);
    stPkt.data = (U8 *)pstPkt->pu8Data;
    stPkt.size = (S32)pstPkt->u32Size;
    stPkt.stream_index = pstChn->pstStream->index;
    stPkt.pts = (S64)pstPkt->u64PTS;
    stPkt.dts = (S64)pstPkt->u64PTS;
    stPkt.duration = 0;
    if (pstPkt->bKeyFrame) {
        stPkt.flags |= AV_PKT_FLAG_KEY;
    }

    ret = av_interleaved_write_frame(pstChn->pstFmt, &stPkt);
    pthread_mutex_unlock(&pstChn->lock);
    if (ret < 0) {
        MUX_LOGE("av_interleaved_write_frame failed, chn=%d, ret=%d", s32ChnId, ret);
        return ERR_MUX_OPEN_FAIL;
    }
    return ERR_MUX_OK;
}

S32 MUX_GetChnStat(S32 s32ChnId, MuxChnStat *pstStat)
{
    MuxChannel *pstChn;
    S32 ret;

    if (!pstStat) {
        return ERR_MUX_NULL_PTR;
    }
    if (!g_stMuxCtx.s32Init) {
        return ERR_MUX_NOT_INIT;
    }

    ret = mux_check_chn(s32ChnId);
    if (ret != ERR_MUX_OK) {
        return ret;
    }

    pstChn = &g_stMuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (!pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_INVALID_CHN;
    }

    memset(pstStat, 0, sizeof(*pstStat));
    pstStat->s32State = pstChn->s32State;

    if (pstChn->stAttr.eOutputType == MUX_OUTPUT_RTSP) {
        pstStat->u32ActiveClients = pstChn->stRtspServer.u32ActiveClients;
        pstStat->u64TotalPkts = pstChn->stRtspServer.u64TotalPkts;
        pstStat->u64TotalBytes = pstChn->stRtspServer.u64TotalBytes;
    }

    pthread_mutex_unlock(&pstChn->lock);
    return ERR_MUX_OK;
}

S32 MUX_GetChnAttr(S32 s32ChnId, MuxChnAttr *pstAttr)
{
    MuxChannel *pstChn;
    S32 ret;

    if (!pstAttr) {
        return ERR_MUX_NULL_PTR;
    }
    if (!g_stMuxCtx.s32Init) {
        return ERR_MUX_NOT_INIT;
    }

    ret = mux_check_chn(s32ChnId);
    if (ret != ERR_MUX_OK) {
        return ret;
    }

    pstChn = &g_stMuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (!pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_INVALID_CHN;
    }

    memcpy(pstAttr, &pstChn->stAttr, sizeof(*pstAttr));
    pthread_mutex_unlock(&pstChn->lock);
    return ERR_MUX_OK;
}

S32 MUX_GetSinkNode(S32 s32ChnId, MppNode *pstNode)
{
    MuxChannel *pstChn;
    S32 ret;

    if (!pstNode) {
        return ERR_MUX_NULL_PTR;
    }
    if (!g_stMuxCtx.s32Init) {
        return ERR_MUX_NOT_INIT;
    }

    ret = mux_check_chn(s32ChnId);
    if (ret != ERR_MUX_OK) {
        return ret;
    }

    pstChn = &g_stMuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (!pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_INVALID_CHN;
    }
    *pstNode = pstChn->stSinkNode;
    pthread_mutex_unlock(&pstChn->lock);
    return ERR_MUX_OK;
}
