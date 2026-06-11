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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mux/mux_api.h"
#include "mux_common.h"
#include "mux_file.h"
#include "mux_rtmp.h"
#include "mux_rtsp_server.h"
#include "sys/mpp_shm.h"
#include "sys/sys_api.h"

#define MUX_STATE_IDLE 0
#define MUX_STATE_CREATED 1
#define MUX_STATE_RUNNING 2

typedef struct _MuxContext {
    S32 s32Init;
    pthread_mutex_t lock;
    MuxChannel astChn[MUX_MAX_CHN];
} MuxContext;

static MuxContext g_stMuxCtx = {0};

static S32 mux_check_chn(S32 s32ChnId) {
    if (s32ChnId < 0 || s32ChnId >= MUX_MAX_CHN) {
        return ERR_MUX_INVALID_CHN;
    }
    return ERR_MUX_OK;
}

static MuxCodecType mux_codec_from_stream(MppStreamCodecType eCodecType) {
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

static VOID *mux_bind_worker(VOID *arg) {
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
            /* No data yet, or no producer bound (manual MUX_SendPacket mode):
             * back off to avoid a busy loop and error-log flooding. */
            usleep(20000);  // 20ms
            continue;
        }

        memset(&stPkt, 0, sizeof(stPkt));
        stPkt.pu8Data = (U8 *)stStream.pu8Addr;
        stPkt.u32Size = stStream.u32Size;
        stPkt.bKeyFrame = stStream.bKeyFrame;
        stPkt.eCodecType = mux_codec_from_stream(stStream.eCodecType);
        stPkt.u64PTS = stStream.u64PTS;
        (VOID) MUX_SendPacket(pstChn->s32ChnId, &stPkt);
    }

    pstChn->s32WorkerAlive = 0;
    free(pu8Buf);
    return NULL;
}

static S32 mux_open_output(MuxChannel *pstChn) {
    if (!pstChn) {
        return ERR_MUX_NULL_PTR;
    }

    switch (pstChn->stAttr.eOutputType) {
        case MUX_OUTPUT_RTSP:
            return mux_rtsp_server_start(pstChn);

        case MUX_OUTPUT_FILE:
            pstChn->pOutputCtx = MuxFile_Create(&pstChn->stAttr.stSegment, &pstChn->stAttr.stStreamAttr);
            if (!pstChn->pOutputCtx) {
                MUX_LOGE("create file recorder failed, url=%s", pstChn->stAttr.szUrl);
                return ERR_MUX_OPEN_FAIL;
            }
            return ERR_MUX_OK;

        case MUX_OUTPUT_RTMP:
            pstChn->pOutputCtx = MuxRtmp_Create(pstChn->stAttr.szUrl, &pstChn->stAttr.stStreamAttr);
            if (!pstChn->pOutputCtx) {
                MUX_LOGE("create rtmp publisher failed, url=%s", pstChn->stAttr.szUrl);
                return ERR_MUX_OPEN_FAIL;
            }
            return ERR_MUX_OK;

        default:
            break;
    }

    MUX_LOGE("unsupported output type %d, url=%s", pstChn->stAttr.eOutputType, pstChn->stAttr.szUrl);
    return ERR_MUX_OPEN_FAIL;
}

static VOID mux_close_output(MuxChannel *pstChn) {
    if (!pstChn) {
        return;
    }

    switch (pstChn->stAttr.eOutputType) {
        case MUX_OUTPUT_RTSP:
            mux_rtsp_server_stop(pstChn);
            break;

        case MUX_OUTPUT_FILE:
            if (pstChn->pOutputCtx) {
                MuxFile_Destroy((MuxFile *)pstChn->pOutputCtx);
                pstChn->pOutputCtx = NULL;
            }
            break;

        case MUX_OUTPUT_RTMP:
            if (pstChn->pOutputCtx) {
                MuxRtmp_Destroy((MuxRtmp *)pstChn->pOutputCtx);
                pstChn->pOutputCtx = NULL;
            }
            break;

        default:
            break;
    }
}

S32 MUX_Init(VOID) {
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

    g_stMuxCtx.s32Init = 1;
    return ERR_MUX_OK;
}

S32 MUX_Exit(VOID) {
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

    pthread_mutex_destroy(&g_stMuxCtx.lock);
    memset(&g_stMuxCtx, 0, sizeof(g_stMuxCtx));
    return ERR_MUX_OK;
}

S32 MUX_CreateChn(S32 s32ChnId, const MuxChnAttr *pstAttr) {
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

S32 MUX_DestroyChn(S32 s32ChnId) {
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

S32 MUX_StartChn(S32 s32ChnId) {
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

S32 MUX_StopChn(S32 s32ChnId) {
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

S32 MUX_SendPacket(S32 s32ChnId, const MuxPacket *pstPkt) {
    MuxChannel *pstChn;
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

    if (pstChn->stAttr.eOutputType == MUX_OUTPUT_FILE && pstChn->pOutputCtx) {
        ret = MuxFile_Write((MuxFile *)pstChn->pOutputCtx, pstPkt->pu8Data, pstPkt->u32Size, pstPkt->bKeyFrame,
                            pstPkt->u64PTS);
        pthread_mutex_unlock(&pstChn->lock);
        return ret;
    }

    if (pstChn->stAttr.eOutputType == MUX_OUTPUT_RTMP && pstChn->pOutputCtx) {
        ret = MuxRtmp_Write((MuxRtmp *)pstChn->pOutputCtx, pstPkt->pu8Data, pstPkt->u32Size, pstPkt->bKeyFrame,
                            pstPkt->u64PTS);
        pthread_mutex_unlock(&pstChn->lock);
        return ret;
    }

    pthread_mutex_unlock(&pstChn->lock);
    return ERR_MUX_NOT_STARTED;
}

S32 MUX_SplitFile(S32 s32ChnId) {
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
    if (pstChn->stAttr.eOutputType != MUX_OUTPUT_FILE || !pstChn->pOutputCtx) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_MUX_UNSUPPORTED;
    }

    ret = MuxFile_RequestSplit((MuxFile *)pstChn->pOutputCtx);
    pthread_mutex_unlock(&pstChn->lock);
    return ret;
}

S32 MUX_GetChnStat(S32 s32ChnId, MuxChnStat *pstStat) {
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
    } else if (pstChn->stAttr.eOutputType == MUX_OUTPUT_FILE && pstChn->pOutputCtx) {
        MuxFile_GetStat((MuxFile *)pstChn->pOutputCtx, &pstStat->u32FileCount, &pstStat->u64CurFileBytes,
                        pstStat->szCurFile, (U32)sizeof(pstStat->szCurFile));
    }

    pthread_mutex_unlock(&pstChn->lock);
    return ERR_MUX_OK;
}

S32 MUX_GetChnAttr(S32 s32ChnId, MuxChnAttr *pstAttr) {
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

S32 MUX_GetSinkNode(S32 s32ChnId, MppNode *pstNode) {
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
