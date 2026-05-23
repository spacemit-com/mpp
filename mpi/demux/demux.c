/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    demux.c
 * @Date      :    2026-04-15
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    DEMUX module implementation for MPP.
 *------------------------------------------------------------------------------
 */

#include "demux/demux_api.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sys/sys_api.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#ifdef __cplusplus
}
#endif

#define DEMUX_LOGE(fmt, ...) fprintf(stderr, "[DEMUX][ERR] " fmt "\n", ##__VA_ARGS__)
#define DEMUX_LOGI(fmt, ...) fprintf(stdout, "[DEMUX][INF] " fmt "\n", ##__VA_ARGS__)

#define DEMUX_STATE_IDLE 0
#define DEMUX_STATE_CREATED 1
#define DEMUX_STATE_RUNNING 2
#define DEMUX_STATE_STOPPING 3

#define DEMUX_TIME_BASE_US 1000000ULL

typedef struct _DemuxPsCache {
    U8 *pu8Vps;
    U32 u32VpsLen;
    U8 *pu8Sps;
    U32 u32SpsLen;
    U8 *pu8Pps;
    U32 u32PpsLen;
} DemuxPsCache;

typedef struct _DemuxChannel {
    S32 s32Created;
    S32 s32State;
    S32 s32Stop;
    S32 s32ThreadAlive;
    S32 s32ChnId;
    pthread_t thread;
    pthread_mutex_t lock;
    DemuxChnAttr stAttr;
    DemuxStreamInfo stStreamInfo;
    DemuxPacketCallback pfnCb;
    VOID *pCbPriv;
    MppNode stSrcNode;
    AVFormatContext *pstFmt;
    AVBSFContext *pstBsf;
    S32 s32VideoIndex;
    S64 s64IoDeadlineMs;
    DemuxPsCache stPs;
} DemuxChannel;

typedef struct _DemuxContext {
    S32 s32Init;
    pthread_mutex_t lock;
    DemuxChannel astChn[DEMUX_MAX_CHN];
} DemuxContext;

static DemuxContext g_stDemuxCtx = {0};

static int64_t demux_now_ms(VOID) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000LL + (int64_t)ts.tv_nsec / 1000000LL;
}

static S32 demux_check_chn(S32 s32ChnId) {
    if (s32ChnId < 0 || s32ChnId >= DEMUX_MAX_CHN) {
        return ERR_DEMUX_INVALID_CHN;
    }
    return ERR_DEMUX_OK;
}

static VOID demux_free_ps_cache(DemuxPsCache *pstPs) {
    if (!pstPs) {
        return;
    }

    free(pstPs->pu8Vps);
    free(pstPs->pu8Sps);
    free(pstPs->pu8Pps);
    memset(pstPs, 0, sizeof(*pstPs));
}

static VOID demux_close_bsf(DemuxChannel *pstChn) {
    if (!pstChn) {
        return;
    }

    if (pstChn->pstBsf) {
        av_bsf_free(&pstChn->pstBsf);
        pstChn->pstBsf = NULL;
    }
}

static VOID demux_close_input(DemuxChannel *pstChn) {
    if (!pstChn) {
        return;
    }

    demux_close_bsf(pstChn);
    if (pstChn->pstFmt) {
        avformat_close_input(&pstChn->pstFmt);
        pstChn->pstFmt = NULL;
    }
    pstChn->s32VideoIndex = -1;
}

static int demux_interrupt_cb(VOID *opaque) {
    DemuxChannel *pstChn = (DemuxChannel *)opaque;

    if (!pstChn) {
        return 1;
    }
    if (pstChn->s32Stop) {
        return 1;
    }
    if (pstChn->s64IoDeadlineMs > 0 && demux_now_ms() > pstChn->s64IoDeadlineMs) {
        return 1;
    }
    return 0;
}

static VOID demux_set_opt_us(AVDictionary **ppDict, const CHAR *pszKey, U32 u32Ms) {
    char szVal[32];
    int64_t us;

    if (!ppDict || !pszKey || u32Ms == 0) {
        return;
    }

    us = (int64_t)u32Ms * 1000LL;
    snprintf(szVal, sizeof(szVal), "%" PRId64, us);
    av_dict_set(ppDict, pszKey, szVal, 0);
}

static S32 demux_codec_from_ffmpeg(enum AVCodecID eCodecId) {
    switch (eCodecId) {
        case AV_CODEC_ID_H264:
            return DEMUX_CODEC_H264;
        case AV_CODEC_ID_HEVC:
            return DEMUX_CODEC_H265;
        case AV_CODEC_ID_MJPEG:
            return DEMUX_CODEC_MJPEG;
        default:
            return DEMUX_CODEC_UNKNOWN;
    }
}

static S32 demux_annexb_has_complete_ps(const U8 *pu8Data, S32 s32Size, S32 s32CodecType) {
    S32 i;
    S32 bHasVps = 0;
    S32 bHasSps = 0;
    S32 bHasPps = 0;

    if (!pu8Data || s32Size <= 4) {
        return 0;
    }

    for (i = 0; i + 4 <= s32Size; ++i) {
        S32 prefix = 0;
        S32 nstart;
        U8 type;

        if (i + 3 < s32Size && pu8Data[i] == 0 && pu8Data[i + 1] == 0 && pu8Data[i + 2] == 1) {
            prefix = 3;
        } else if (
            i + 4 < s32Size && pu8Data[i] == 0 && pu8Data[i + 1] == 0 && pu8Data[i + 2] == 0 && pu8Data[i + 3] == 1
        ) {
            prefix = 4;
        }

        if (prefix == 0) {
            continue;
        }

        nstart = i + prefix;
        if (nstart >= s32Size) {
            break;
        }

        if (s32CodecType == DEMUX_CODEC_H265) {
            type = (pu8Data[nstart] >> 1) & 0x3f;
            if (type == 32) {
                bHasVps = 1;
            } else if (type == 33) {
                bHasSps = 1;
            } else if (type == 34) {
                bHasPps = 1;
            }
            if (bHasVps && bHasSps && bHasPps) {
                return 1;
            }
        } else if (s32CodecType == DEMUX_CODEC_H264) {
            type = pu8Data[nstart] & 0x1f;
            if (type == 7) {
                bHasSps = 1;
            } else if (type == 8) {
                bHasPps = 1;
            }
            if (bHasSps && bHasPps) {
                return 1;
            }
        }
    }

    return 0;
}

static VOID demux_copy_nalu_with_sc(U8 **ppu8Dst, U32 *pu32DstLen, const U8 *pu8Src, U32 u32Len) {
    static const U8 au8Sc[4] = {0, 0, 0, 1};
    U8 *pu8Buf;

    if (!ppu8Dst || !pu32DstLen || !pu8Src || u32Len == 0) {
        return;
    }

    pu8Buf = (U8 *)malloc(u32Len + 4);
    if (!pu8Buf) {
        return;
    }

    memcpy(pu8Buf, au8Sc, 4);
    memcpy(pu8Buf + 4, pu8Src, u32Len);

    free(*ppu8Dst);
    *ppu8Dst = pu8Buf;
    *pu32DstLen = u32Len + 4;
}

static VOID demux_cache_h264_extradata(const AVCodecParameters *pstPar, DemuxPsCache *pstPs) {
    const U8 *pu8Data;
    S32 s32Size;
    S32 off;
    U8 num;

    if (!pstPar || !pstPs || !pstPar->extradata || pstPar->extradata_size <= 0) {
        return;
    }

    pu8Data = pstPar->extradata;
    s32Size = pstPar->extradata_size;

    if (demux_annexb_has_complete_ps(pu8Data, s32Size, DEMUX_CODEC_H264)) {
        return;
    }

    if (s32Size < 7 || pu8Data[0] != 1) {
        return;
    }

    off = 5;
    num = pu8Data[off++] & 0x1f;
    while (num > 0 && off + 2 <= s32Size) {
        U32 len = ((U32)pu8Data[off] << 8) | pu8Data[off + 1];
        off += 2;
        if (off + (S32)len > s32Size) {
            return;
        }
        demux_copy_nalu_with_sc(&pstPs->pu8Sps, &pstPs->u32SpsLen, pu8Data + off, len);
        off += (S32)len;
        num--;
    }

    if (off + 1 > s32Size) {
        return;
    }

    num = pu8Data[off++];
    while (num > 0 && off + 2 <= s32Size) {
        U32 len = ((U32)pu8Data[off] << 8) | pu8Data[off + 1];
        off += 2;
        if (off + (S32)len > s32Size) {
            return;
        }
        demux_copy_nalu_with_sc(&pstPs->pu8Pps, &pstPs->u32PpsLen, pu8Data + off, len);
        off += (S32)len;
        num--;
    }
}

static VOID demux_make_packet_with_ps(
    DemuxChannel *pstChn, const U8 *pu8Data, U32 u32Size, DemuxPacket *pstPkt, U8 **ppu8Out, U32 *pu32OutLen
) {
    U32 total = 0;
    U8 *pu8Buf;
    U8 *p;

    if (!pstChn || !pstPkt || !ppu8Out || !pu32OutLen || !pu8Data || u32Size == 0) {
        return;
    }

    if (pstPkt->eCodecType == DEMUX_CODEC_H265) {
        total += pstChn->stPs.u32VpsLen + pstChn->stPs.u32SpsLen + pstChn->stPs.u32PpsLen;
    } else if (pstPkt->eCodecType == DEMUX_CODEC_H264) {
        total += pstChn->stPs.u32SpsLen + pstChn->stPs.u32PpsLen;
    }
    total += u32Size;

    if (total == u32Size) {
        return;
    }

    pu8Buf = (U8 *)malloc(total);
    if (!pu8Buf) {
        return;
    }

    p = pu8Buf;
    if (pstPkt->eCodecType == DEMUX_CODEC_H265) {
        if (pstChn->stPs.u32VpsLen) {
            memcpy(p, pstChn->stPs.pu8Vps, pstChn->stPs.u32VpsLen);
            p += pstChn->stPs.u32VpsLen;
        }
        if (pstChn->stPs.u32SpsLen) {
            memcpy(p, pstChn->stPs.pu8Sps, pstChn->stPs.u32SpsLen);
            p += pstChn->stPs.u32SpsLen;
        }
        if (pstChn->stPs.u32PpsLen) {
            memcpy(p, pstChn->stPs.pu8Pps, pstChn->stPs.u32PpsLen);
            p += pstChn->stPs.u32PpsLen;
        }
    } else {
        if (pstChn->stPs.u32SpsLen) {
            memcpy(p, pstChn->stPs.pu8Sps, pstChn->stPs.u32SpsLen);
            p += pstChn->stPs.u32SpsLen;
        }
        if (pstChn->stPs.u32PpsLen) {
            memcpy(p, pstChn->stPs.pu8Pps, pstChn->stPs.u32PpsLen);
            p += pstChn->stPs.u32PpsLen;
        }
    }

    memcpy(p, pu8Data, u32Size);
    *ppu8Out = pu8Buf;
    *pu32OutLen = total;
}

static S32 demux_open_bsf(DemuxChannel *pstChn) {
    const AVBitStreamFilter *pstFilter;
    const AVCodecParameters *pstPar;
    const CHAR *pszBsfName = NULL;
    S32 ret;

    if (!pstChn || !pstChn->pstFmt || pstChn->s32VideoIndex < 0) {
        return ERR_DEMUX_OK;
    }

    demux_close_bsf(pstChn);

    pstPar = pstChn->pstFmt->streams[pstChn->s32VideoIndex]->codecpar;
    if (pstPar->codec_id == AV_CODEC_ID_H264) {
        pszBsfName = "h264_mp4toannexb";
    } else if (pstPar->codec_id == AV_CODEC_ID_HEVC) {
        pszBsfName = "hevc_mp4toannexb";
    } else {
        return ERR_DEMUX_OK;
    }

    pstFilter = av_bsf_get_by_name(pszBsfName);
    if (!pstFilter) {
        return ERR_DEMUX_OK;
    }

    ret = av_bsf_alloc(pstFilter, &pstChn->pstBsf);
    if (ret < 0) {
        DEMUX_LOGE("av_bsf_alloc failed, ret=%d", ret);
        return ERR_DEMUX_OPEN_FAIL;
    }

    ret = avcodec_parameters_copy(pstChn->pstBsf->par_in, pstPar);
    if (ret < 0) {
        DEMUX_LOGE("avcodec_parameters_copy failed, ret=%d", ret);
        demux_close_bsf(pstChn);
        return ERR_DEMUX_OPEN_FAIL;
    }

    ret = av_bsf_init(pstChn->pstBsf);
    if (ret < 0) {
        DEMUX_LOGE("av_bsf_init failed, ret=%d", ret);
        demux_close_bsf(pstChn);
        return ERR_DEMUX_OPEN_FAIL;
    }

    return ERR_DEMUX_OK;
}

static S32 demux_deliver_packet(DemuxChannel *pstChn, const U8 *pu8Data, U32 u32Size, S64 s64Pts) {
    DemuxPacket stPkt;
    U8 *pu8Tmp = NULL;
    U32 u32TmpLen = 0;
    S32 ret = ERR_DEMUX_OK;

    if (!pstChn || !pu8Data || u32Size == 0) {
        return ERR_DEMUX_NULL_PTR;
    }

    memset(&stPkt, 0, sizeof(stPkt));
    stPkt.pu8Data = pu8Data;
    stPkt.u32Size = u32Size;
    stPkt.eCodecType = pstChn->stStreamInfo.eCodecType;
    stPkt.u32Width = pstChn->stStreamInfo.u32Width;
    stPkt.u32Height = pstChn->stStreamInfo.u32Height;
    stPkt.u64PTS = (s64Pts == AV_NOPTS_VALUE) ? 0
                                                : (U64)av_rescale_q(
                                                    s64Pts,
                                                    pstChn->pstFmt->streams[pstChn->s32VideoIndex]->time_base,
                                                    (AVRational){1, (int)DEMUX_TIME_BASE_US});

    if (stPkt.eCodecType == DEMUX_CODEC_H264 && u32Size > 4) {
        U8 type = pu8Data[4] & 0x1f;
        stPkt.bKeyFrame = (type == 5) ? MPP_TRUE : MPP_FALSE;
    } else if (stPkt.eCodecType == DEMUX_CODEC_H265 && u32Size > 5) {
        U8 type = (pu8Data[4] >> 1) & 0x3f;
        stPkt.bKeyFrame = (type == 19 || type == 20) ? MPP_TRUE : MPP_FALSE;
    }

    if (pstChn->stAttr.bInjectPS && stPkt.bKeyFrame &&
        !demux_annexb_has_complete_ps(pu8Data, (S32)u32Size, stPkt.eCodecType)) {
        demux_make_packet_with_ps(pstChn, pu8Data, u32Size, &stPkt, &pu8Tmp, &u32TmpLen);
        if (pu8Tmp && u32TmpLen > 0) {
            stPkt.pu8Data = pu8Tmp;
            stPkt.u32Size = u32TmpLen;
        }
    }

    if (pstChn->pfnCb) {
        ret = pstChn->pfnCb(pstChn->s32ChnId, &stPkt, pstChn->pCbPriv);
    }

    if (ret == ERR_DEMUX_OK) {
        StreamBufferInfo stStream;

        memset(&stStream, 0, sizeof(stStream));
        stStream.pu8Addr = stPkt.pu8Data;
        stStream.u32Size = stPkt.u32Size;
        stStream.bKeyFrame = stPkt.bKeyFrame;
        stStream.bEndOfStream = MPP_FALSE;
        stStream.eCodecType = MPP_STREAM_CODEC_UNKNOWN;
        stStream.u64PTS = stPkt.u64PTS;
        stStream.u32Width = stPkt.u32Width;
        stStream.u32Height = stPkt.u32Height;

        if (stPkt.eCodecType == DEMUX_CODEC_H264) {
            stStream.eCodecType = MPP_STREAM_CODEC_H264;
        } else if (stPkt.eCodecType == DEMUX_CODEC_H265) {
            stStream.eCodecType = MPP_STREAM_CODEC_H265;
        } else if (stPkt.eCodecType == DEMUX_CODEC_MJPEG) {
            stStream.eCodecType = MPP_STREAM_CODEC_MJPEG;
        }

        (VOID) SYS_SendStream(&pstChn->stSrcNode, &stStream);
    }

    free(pu8Tmp);
    return ret;
}

static VOID *demux_thread_proc(VOID *arg) {
    DemuxChannel *pstChn = (DemuxChannel *)arg;
    AVPacket *pstPkt = NULL;
    AVPacket *pstOut = NULL;

    if (!pstChn) {
        return NULL;
    }

    pstPkt = av_packet_alloc();
    pstOut = av_packet_alloc();
    if (!pstPkt || !pstOut) {
        DEMUX_LOGE("av_packet_alloc failed");
        goto exit;
    }

    pstChn->s32ThreadAlive = 1;

    while (!pstChn->s32Stop) {
        AVDictionary *pstOpts = NULL;
        const AVCodecParameters *pstPar;
        S32 ret;
        U32 fps = 0;

        demux_close_input(pstChn);
        demux_free_ps_cache(&pstChn->stPs);

        pstChn->pstFmt = avformat_alloc_context();
        if (!pstChn->pstFmt) {
            DEMUX_LOGE("avformat_alloc_context failed");
            usleep((useconds_t)pstChn->stAttr.u32ReconnectMs * 1000);
            continue;
        }

        pstChn->pstFmt->interrupt_callback.callback = demux_interrupt_cb;
        pstChn->pstFmt->interrupt_callback.opaque = pstChn;

        if (pstChn->stAttr.eInputType == DEMUX_INPUT_RTSP && pstChn->stAttr.bPreferTcp) {
            av_dict_set(&pstOpts, "rtsp_transport", "tcp", 0);
        }
        if (pstChn->stAttr.bLowLatency) {
            av_dict_set(&pstOpts, "fflags", "nobuffer", 0);
            av_dict_set(&pstOpts, "reorder_queue_size", "0", 0);
            av_dict_set(&pstOpts, "avioflags", "direct", 0);
        }
        demux_set_opt_us(&pstOpts, "stimeout", pstChn->stAttr.u32OpenTimeoutMs);
        demux_set_opt_us(&pstOpts, "rw_timeout", pstChn->stAttr.u32RwTimeoutMs);
        if (pstChn->stAttr.u32AnalyzeDurationMs) {
            char szVal[32];
            snprintf(szVal, sizeof(szVal), "%u", pstChn->stAttr.u32AnalyzeDurationMs * 1000U);
            av_dict_set(&pstOpts, "analyzeduration", szVal, 0);
        }
        if (pstChn->stAttr.u32ProbeSizeBytes) {
            char szVal[32];
            snprintf(szVal, sizeof(szVal), "%u", pstChn->stAttr.u32ProbeSizeBytes);
            av_dict_set(&pstOpts, "probesize", szVal, 0);
        }

        pstChn->s64IoDeadlineMs = demux_now_ms() + (int64_t)pstChn->stAttr.u32OpenTimeoutMs + 500LL;
        ret = avformat_open_input(&pstChn->pstFmt, pstChn->stAttr.szUrl, NULL, &pstOpts);
        av_dict_free(&pstOpts);
        if (ret < 0) {
            DEMUX_LOGE(
                "avformat_open_input failed, chn=%d, ret=%d, url=%s", pstChn->s32ChnId, ret, pstChn->stAttr.szUrl);
            demux_close_input(pstChn);
            usleep((useconds_t)pstChn->stAttr.u32ReconnectMs * 1000);
            continue;
        }

        pstChn->s64IoDeadlineMs = demux_now_ms() + (int64_t)pstChn->stAttr.u32OpenTimeoutMs + 500LL;
        ret = avformat_find_stream_info(pstChn->pstFmt, NULL);
        if (ret < 0) {
            DEMUX_LOGE("avformat_find_stream_info failed, chn=%d, ret=%d", pstChn->s32ChnId, ret);
            demux_close_input(pstChn);
            usleep((useconds_t)pstChn->stAttr.u32ReconnectMs * 1000);
            continue;
        }

        pstChn->s32VideoIndex = -1;
        for (U32 i = 0; i < pstChn->pstFmt->nb_streams; ++i) {
            if (pstChn->pstFmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                pstChn->s32VideoIndex = (S32)i;
                break;
            }
        }
        if (pstChn->s32VideoIndex < 0) {
            DEMUX_LOGE("no video stream found, chn=%d", pstChn->s32ChnId);
            demux_close_input(pstChn);
            usleep((useconds_t)pstChn->stAttr.u32ReconnectMs * 1000);
            continue;
        }

        pstPar = pstChn->pstFmt->streams[pstChn->s32VideoIndex]->codecpar;
        pstChn->stStreamInfo.eCodecType = demux_codec_from_ffmpeg(pstPar->codec_id);
        pstChn->stStreamInfo.u32Width = (U32)pstPar->width;
        pstChn->stStreamInfo.u32Height = (U32)pstPar->height;
        if (pstChn->pstFmt->streams[pstChn->s32VideoIndex]->avg_frame_rate.num > 0 &&
            pstChn->pstFmt->streams[pstChn->s32VideoIndex]->avg_frame_rate.den > 0) {
            fps = (U32)av_q2d(pstChn->pstFmt->streams[pstChn->s32VideoIndex]->avg_frame_rate);
        }
        pstChn->stStreamInfo.u32Fps = fps;

        if (pstPar->codec_id == AV_CODEC_ID_H264) {
            demux_cache_h264_extradata(pstPar, &pstChn->stPs);
        }

        demux_open_bsf(pstChn);
        DEMUX_LOGI(
            "channel %d connected: %ux%u fps=%u codec=%d",
            pstChn->s32ChnId,
            pstChn->stStreamInfo.u32Width,
            pstChn->stStreamInfo.u32Height,
            pstChn->stStreamInfo.u32Fps,
            pstChn->stStreamInfo.eCodecType);

        while (!pstChn->s32Stop) {
            pstChn->s64IoDeadlineMs = demux_now_ms() + (int64_t)pstChn->stAttr.u32RwTimeoutMs + 500LL;
            ret = av_read_frame(pstChn->pstFmt, pstPkt);
            if (ret < 0) {
                break;
            }

            if (pstPkt->stream_index != pstChn->s32VideoIndex) {
                av_packet_unref(pstPkt);
                continue;
            }

            if (pstChn->pstBsf) {
                if (av_bsf_send_packet(pstChn->pstBsf, pstPkt) == 0) {
                    while (av_bsf_receive_packet(pstChn->pstBsf, pstOut) == 0) {
                        demux_deliver_packet(pstChn, pstOut->data, (U32)pstOut->size, pstOut->pts);
                        av_packet_unref(pstOut);
                    }
                }
            } else {
                demux_deliver_packet(pstChn, pstPkt->data, (U32)pstPkt->size, pstPkt->pts);
            }

            av_packet_unref(pstPkt);
        }

        demux_close_input(pstChn);
        if (!pstChn->s32Stop) {
            usleep((useconds_t)pstChn->stAttr.u32ReconnectMs * 1000);
        }
    }

exit:
    if (pstOut) {
        av_packet_free(&pstOut);
    }
    if (pstPkt) {
        av_packet_free(&pstPkt);
    }
    pstChn->s32ThreadAlive = 0;
    return NULL;
}

S32 DEMUX_Init(VOID) {
    S32 i;

    if (g_stDemuxCtx.s32Init) {
        return ERR_DEMUX_ALREADY_INIT;
    }

    if (pthread_mutex_init(&g_stDemuxCtx.lock, NULL) != 0) {
        DEMUX_LOGE("pthread_mutex_init failed");
        return ERR_DEMUX_BUSY;
    }

    for (i = 0; i < DEMUX_MAX_CHN; ++i) {
        DemuxChannel *pstChn = &g_stDemuxCtx.astChn[i];
        memset(pstChn, 0, sizeof(*pstChn));
        pstChn->s32ChnId = i;
        pstChn->s32VideoIndex = -1;
        pstChn->stSrcNode.eModId = MPP_ID_DEMUX;
        pstChn->stSrcNode.s32DevId = 0;
        pstChn->stSrcNode.s32ChnId = i;
        pthread_mutex_init(&pstChn->lock, NULL);
    }

    avformat_network_init();
    g_stDemuxCtx.s32Init = 1;
    return ERR_DEMUX_OK;
}

S32 DEMUX_Exit(VOID) {
    S32 i;

    if (!g_stDemuxCtx.s32Init) {
        return ERR_DEMUX_NOT_INIT;
    }

    for (i = 0; i < DEMUX_MAX_CHN; ++i) {
        if (g_stDemuxCtx.astChn[i].s32Created) {
            DEMUX_DestroyChn(i);
        }
        pthread_mutex_destroy(&g_stDemuxCtx.astChn[i].lock);
    }

    avformat_network_deinit();
    pthread_mutex_destroy(&g_stDemuxCtx.lock);
    memset(&g_stDemuxCtx, 0, sizeof(g_stDemuxCtx));
    return ERR_DEMUX_OK;
}

S32 DEMUX_CreateChn(S32 s32ChnId, const DemuxChnAttr *pstAttr) {
    DemuxChannel *pstChn;
    S32 ret;

    if (!pstAttr) {
        return ERR_DEMUX_NULL_PTR;
    }
    if (!g_stDemuxCtx.s32Init) {
        return ERR_DEMUX_NOT_INIT;
    }

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK) {
        return ret;
    }

    pstChn = &g_stDemuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_DEMUX_BUSY;
    }

    memcpy(&pstChn->stAttr, pstAttr, sizeof(*pstAttr));
    if (pstChn->stAttr.u32OpenTimeoutMs == 0) {
        pstChn->stAttr.u32OpenTimeoutMs = 3000;
    }
    if (pstChn->stAttr.u32RwTimeoutMs == 0) {
        pstChn->stAttr.u32RwTimeoutMs = 3000;
    }
    if (pstChn->stAttr.u32ReconnectMs == 0) {
        pstChn->stAttr.u32ReconnectMs = 1500;
    }
    pstChn->s32State = DEMUX_STATE_CREATED;
    pstChn->s32Created = 1;
    pthread_mutex_unlock(&pstChn->lock);
    return ERR_DEMUX_OK;
}

S32 DEMUX_DestroyChn(S32 s32ChnId) {
    DemuxChannel *pstChn;
    S32 ret;

    if (!g_stDemuxCtx.s32Init) {
        return ERR_DEMUX_NOT_INIT;
    }

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK) {
        return ret;
    }

    pstChn = &g_stDemuxCtx.astChn[s32ChnId];
    DEMUX_StopChn(s32ChnId);

    pthread_mutex_lock(&pstChn->lock);
    demux_close_input(pstChn);
    demux_free_ps_cache(&pstChn->stPs);
    memset(&pstChn->stStreamInfo, 0, sizeof(pstChn->stStreamInfo));
    pstChn->pfnCb = NULL;
    pstChn->pCbPriv = NULL;
    pstChn->s32Created = 0;
    pstChn->s32State = DEMUX_STATE_IDLE;
    pthread_mutex_unlock(&pstChn->lock);
    return ERR_DEMUX_OK;
}

S32 DEMUX_StartChn(S32 s32ChnId) {
    DemuxChannel *pstChn;
    S32 ret;

    if (!g_stDemuxCtx.s32Init) {
        return ERR_DEMUX_NOT_INIT;
    }

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK) {
        return ret;
    }

    pstChn = &g_stDemuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (!pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_DEMUX_INVALID_CHN;
    }
    if (pstChn->s32State == DEMUX_STATE_RUNNING) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_DEMUX_OK;
    }

    pstChn->s32Stop = 0;
    pstChn->s32State = DEMUX_STATE_RUNNING;
    if (pthread_create(&pstChn->thread, NULL, demux_thread_proc, pstChn) != 0) {
        pstChn->s32State = DEMUX_STATE_CREATED;
        pthread_mutex_unlock(&pstChn->lock);
        DEMUX_LOGE("pthread_create failed, chn=%d", s32ChnId);
        return ERR_DEMUX_BUSY;
    }
    pthread_mutex_unlock(&pstChn->lock);
    return ERR_DEMUX_OK;
}

S32 DEMUX_StopChn(S32 s32ChnId) {
    DemuxChannel *pstChn;
    S32 ret;

    if (!g_stDemuxCtx.s32Init) {
        return ERR_DEMUX_NOT_INIT;
    }

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK) {
        return ret;
    }

    pstChn = &g_stDemuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (!pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_DEMUX_INVALID_CHN;
    }
    if (pstChn->s32State != DEMUX_STATE_RUNNING) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_DEMUX_OK;
    }

    pstChn->s32Stop = 1;
    pstChn->s32State = DEMUX_STATE_STOPPING;
    pthread_mutex_unlock(&pstChn->lock);

    pthread_join(pstChn->thread, NULL);

    pthread_mutex_lock(&pstChn->lock);
    pstChn->s32State = DEMUX_STATE_CREATED;
    pthread_mutex_unlock(&pstChn->lock);
    return ERR_DEMUX_OK;
}

S32 DEMUX_GetStreamInfo(S32 s32ChnId, DemuxStreamInfo *pstInfo) {
    DemuxChannel *pstChn;
    S32 ret;

    if (!pstInfo) {
        return ERR_DEMUX_NULL_PTR;
    }
    if (!g_stDemuxCtx.s32Init) {
        return ERR_DEMUX_NOT_INIT;
    }

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK) {
        return ret;
    }

    pstChn = &g_stDemuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (!pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_DEMUX_INVALID_CHN;
    }
    memcpy(pstInfo, &pstChn->stStreamInfo, sizeof(*pstInfo));
    pthread_mutex_unlock(&pstChn->lock);
    return ERR_DEMUX_OK;
}

S32 DEMUX_SetPacketCallback(S32 s32ChnId, DemuxPacketCallback pfnCb, VOID *pPriv) {
    DemuxChannel *pstChn;
    S32 ret;

    if (!g_stDemuxCtx.s32Init) {
        return ERR_DEMUX_NOT_INIT;
    }

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK) {
        return ret;
    }

    pstChn = &g_stDemuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (!pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_DEMUX_INVALID_CHN;
    }
    pstChn->pfnCb = pfnCb;
    pstChn->pCbPriv = pPriv;
    pthread_mutex_unlock(&pstChn->lock);
    return ERR_DEMUX_OK;
}

S32 DEMUX_GetSrcNode(S32 s32ChnId, MppNode *pstNode) {
    DemuxChannel *pstChn;
    S32 ret;

    if (!pstNode) {
        return ERR_DEMUX_NULL_PTR;
    }
    if (!g_stDemuxCtx.s32Init) {
        return ERR_DEMUX_NOT_INIT;
    }

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK) {
        return ret;
    }

    pstChn = &g_stDemuxCtx.astChn[s32ChnId];
    pthread_mutex_lock(&pstChn->lock);
    if (!pstChn->s32Created) {
        pthread_mutex_unlock(&pstChn->lock);
        return ERR_DEMUX_INVALID_CHN;
    }
    *pstNode = pstChn->stSrcNode;
    pthread_mutex_unlock(&pstChn->lock);
    return ERR_DEMUX_OK;
}
