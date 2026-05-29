/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    demux.c
 * @Brief     :    demux implementation.
 *                 - Context API: Demux_Create/Open/ReadPacket/...
 *                 - Channel API: DEMUX_Init/CreateChn/StartChn/...
 *                 Features: Auto-reconnect, callback/bind modes, thread-safe.
 *------------------------------------------------------------------------------
 */

#include "demux.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/url_parser.h"
#include "sys/sys_api.h"

#ifdef DEMUX_RTSP
#include "protocol/rtsp/rtsp_client.h"
#endif

#ifdef DEMUX_MP4
#include "container/mp4/mp4_demuxer.h"
#endif

#ifdef DEMUX_FLV
#include "container/flv/flv_demuxer.h"
#endif

#ifdef DEMUX_TS
#include "container/ts/ts_demuxer.h"
#endif

struct _DemuxCtx {
    DemuxProtocol eProto;
    CHAR szUrl[DEMUX_URL_MAX_LEN];

    union {
#ifdef DEMUX_RTSP
        RtspClient *pRtsp;
#endif
#ifdef DEMUX_MP4
        Mp4Demuxer *pMp4;
#endif
#ifdef DEMUX_FLV
        FlvDemuxer *pFlv;
#endif
#ifdef DEMUX_TS
        TsDemuxer *pTs;
#endif
        void *pGeneric;
    } impl;
};

DemuxProtocol Demux_DetectProtocol(const CHAR *pszUrl) {
    if (!pszUrl)
        return DEMUX_PROTO_UNKNOWN;

    /* Check scheme */
    if (strncasecmp(pszUrl, "rtsp://", 7) == 0) {
        return DEMUX_PROTO_RTSP;
    }
    if (strncasecmp(pszUrl, "rtmp://", 7) == 0) {
        return DEMUX_PROTO_RTMP;
    }
    if (strncasecmp(pszUrl, "http://", 7) == 0 || strncasecmp(pszUrl, "https://", 8) == 0) {
        const CHAR *pExt = Url_GetExtension(pszUrl);
        if (strcasecmp(pExt, "m3u8") == 0)
            return DEMUX_PROTO_HLS;
        if (strcasecmp(pExt, "flv") == 0)
            return DEMUX_PROTO_HTTP_FLV;
    }

    /* Check file extension */
    if (Url_IsFile(pszUrl)) {
        const CHAR *pExt = Url_GetExtension(pszUrl);
        if (strcasecmp(pExt, "mp4") == 0)
            return DEMUX_PROTO_FILE_MP4;
        if (strcasecmp(pExt, "ts") == 0)
            return DEMUX_PROTO_FILE_TS;
        if (strcasecmp(pExt, "flv") == 0)
            return DEMUX_PROTO_FILE_FLV;
    }

    return DEMUX_PROTO_UNKNOWN;
}

BOOL Demux_IsSupported(DemuxProtocol eProto) {
    switch (eProto) {
#ifdef DEMUX_RTSP
    case DEMUX_PROTO_RTSP:
        return MPP_TRUE;
#endif
#ifdef DEMUX_RTMP
    case DEMUX_PROTO_RTMP:
        return MPP_TRUE;
#endif
#ifdef DEMUX_MP4
    case DEMUX_PROTO_FILE_MP4:
        return MPP_TRUE;
#endif
#ifdef DEMUX_FLV
    case DEMUX_PROTO_FILE_FLV:
        return MPP_TRUE;
    case DEMUX_PROTO_HTTP_FLV:
        return MPP_TRUE;
#endif
#ifdef DEMUX_TS
    case DEMUX_PROTO_FILE_TS:
        return MPP_TRUE;
#endif
#ifdef DEMUX_HLS
    case DEMUX_PROTO_HLS:
        return MPP_TRUE;
#endif
    default:
        return MPP_FALSE;
    }
}

DemuxCtx *Demux_Create(const CHAR *pszUrl) {
    DemuxCtx *pCtx;

    if (!pszUrl)
        return NULL;

    pCtx = (DemuxCtx *)calloc(1, sizeof(DemuxCtx));
    if (!pCtx)
        return NULL;

    pCtx->eProto = Demux_DetectProtocol(pszUrl);
    strncpy(pCtx->szUrl, pszUrl, DEMUX_URL_MAX_LEN - 1);

    if (!Demux_IsSupported(pCtx->eProto)) {
        free(pCtx);
        return NULL;
    }

    /* Create protocol-specific context */
    switch (pCtx->eProto) {
#ifdef DEMUX_RTSP
    case DEMUX_PROTO_RTSP:
        pCtx->impl.pRtsp = RtspClient_Create();
        if (!pCtx->impl.pRtsp) {
            free(pCtx);
            return NULL;
        }
        break;
#endif
#ifdef DEMUX_MP4
    case DEMUX_PROTO_FILE_MP4:
        pCtx->impl.pMp4 = Mp4Demuxer_Create();
        if (!pCtx->impl.pMp4) {
            free(pCtx);
            return NULL;
        }
        break;
#endif
#ifdef DEMUX_TS
    case DEMUX_PROTO_FILE_TS:
        pCtx->impl.pTs = TsDemuxer_Create();
        if (!pCtx->impl.pTs) {
            free(pCtx);
            return NULL;
        }
        break;
#endif
#ifdef DEMUX_FLV
    case DEMUX_PROTO_FILE_FLV:
        pCtx->impl.pFlv = FlvDemuxer_Create();
        if (!pCtx->impl.pFlv) {
            free(pCtx);
            return NULL;
        }
        break;
#endif
    default:
        break;
    }

    return pCtx;
}

VOID Demux_Destroy(DemuxCtx *pCtx) {
    if (!pCtx)
        return;

    switch (pCtx->eProto) {
#ifdef DEMUX_RTSP
    case DEMUX_PROTO_RTSP:
        if (pCtx->impl.pRtsp) {
            RtspClient_Destroy(pCtx->impl.pRtsp);
        }
        break;
#endif
#ifdef DEMUX_MP4
    case DEMUX_PROTO_FILE_MP4:
        if (pCtx->impl.pMp4) {
            Mp4Demuxer_Destroy(pCtx->impl.pMp4);
        }
        break;
#endif
#ifdef DEMUX_TS
    case DEMUX_PROTO_FILE_TS:
        if (pCtx->impl.pTs) {
            TsDemuxer_Destroy(pCtx->impl.pTs);
        }
        break;
#endif
#ifdef DEMUX_FLV
    case DEMUX_PROTO_FILE_FLV:
        if (pCtx->impl.pFlv) {
            FlvDemuxer_Destroy(pCtx->impl.pFlv);
        }
        break;
#endif
    default:
        break;
    }

    free(pCtx);
}

S32 Demux_Open(DemuxCtx *pCtx, BOOL bPreferTcp, U32 u32TimeoutMs) {
    if (!pCtx)
        return ERR_DEMUX_NULL_PTR;

    switch (pCtx->eProto) {
#ifdef DEMUX_RTSP
    case DEMUX_PROTO_RTSP:
        return RtspClient_Connect(pCtx->impl.pRtsp, pCtx->szUrl, bPreferTcp, u32TimeoutMs);
#endif
#ifdef DEMUX_MP4
    case DEMUX_PROTO_FILE_MP4:
        return Mp4Demuxer_Open(pCtx->impl.pMp4, pCtx->szUrl);
#endif
#ifdef DEMUX_TS
    case DEMUX_PROTO_FILE_TS:
        return TsDemuxer_Open(pCtx->impl.pTs, pCtx->szUrl);
#endif
#ifdef DEMUX_FLV
    case DEMUX_PROTO_FILE_FLV:
        return FlvDemuxer_Open(pCtx->impl.pFlv, pCtx->szUrl);
#endif
    default:
        return ERR_DEMUX_OPEN_FAIL;
    }
}

VOID Demux_Close(DemuxCtx *pCtx) {
    if (!pCtx)
        return;

    switch (pCtx->eProto) {
#ifdef DEMUX_RTSP
    case DEMUX_PROTO_RTSP:
        RtspClient_Disconnect(pCtx->impl.pRtsp);
        break;
#endif
#ifdef DEMUX_MP4
    case DEMUX_PROTO_FILE_MP4:
        Mp4Demuxer_Close(pCtx->impl.pMp4);
        break;
#endif
#ifdef DEMUX_TS
    case DEMUX_PROTO_FILE_TS:
        TsDemuxer_Close(pCtx->impl.pTs);
        break;
#endif
#ifdef DEMUX_FLV
    case DEMUX_PROTO_FILE_FLV:
        FlvDemuxer_Close(pCtx->impl.pFlv);
        break;
#endif
    default:
        break;
    }
}

S32 Demux_GetStreamInfoCtx(DemuxCtx *pCtx, DemuxStreamInfo *pstInfo) {
    if (!pCtx || !pstInfo)
        return ERR_DEMUX_NULL_PTR;

    switch (pCtx->eProto) {
#ifdef DEMUX_RTSP
    case DEMUX_PROTO_RTSP:
        return RtspClient_GetStreamInfo(pCtx->impl.pRtsp, pstInfo);
#endif
#ifdef DEMUX_MP4
    case DEMUX_PROTO_FILE_MP4:
        return Mp4Demuxer_GetStreamInfo(pCtx->impl.pMp4, pstInfo);
#endif
#ifdef DEMUX_TS
    case DEMUX_PROTO_FILE_TS:
        return TsDemuxer_GetStreamInfo(pCtx->impl.pTs, pstInfo);
#endif
#ifdef DEMUX_FLV
    case DEMUX_PROTO_FILE_FLV:
        return FlvDemuxer_GetStreamInfo(pCtx->impl.pFlv, pstInfo);
#endif
    default:
        return ERR_DEMUX_NOT_STARTED;
    }
}

S32 Demux_ReadPacket(DemuxCtx *pCtx, DemuxPacket *pstPkt) {
    if (!pCtx || !pstPkt)
        return ERR_DEMUX_NULL_PTR;

    switch (pCtx->eProto) {
#ifdef DEMUX_RTSP
    case DEMUX_PROTO_RTSP:
        return RtspClient_ReadPacket(pCtx->impl.pRtsp, pstPkt);
#endif
#ifdef DEMUX_MP4
    case DEMUX_PROTO_FILE_MP4:
        return Mp4Demuxer_ReadPacket(pCtx->impl.pMp4, pstPkt);
#endif
#ifdef DEMUX_TS
    case DEMUX_PROTO_FILE_TS:
        return TsDemuxer_ReadPacket(pCtx->impl.pTs, pstPkt);
#endif
#ifdef DEMUX_FLV
    case DEMUX_PROTO_FILE_FLV:
        return FlvDemuxer_ReadPacket(pCtx->impl.pFlv, pstPkt);
#endif
    default:
        return ERR_DEMUX_NO_STREAM;
    }
}

S32 Demux_Seek(DemuxCtx *pCtx, S64 s64PtsUs) {
    if (!pCtx)
        return ERR_DEMUX_NULL_PTR;

    switch (pCtx->eProto) {
#ifdef DEMUX_MP4
    case DEMUX_PROTO_FILE_MP4:
        return Mp4Demuxer_Seek(pCtx->impl.pMp4, s64PtsUs);
#endif
#ifdef DEMUX_TS
    case DEMUX_PROTO_FILE_TS:
        return TsDemuxer_Seek(pCtx->impl.pTs, s64PtsUs);
#endif
    default:
        return ERR_DEMUX_NOT_STARTED; /* Live streams don't support seek */
    }
}

S64 Demux_GetDuration(DemuxCtx *pCtx) {
    if (!pCtx)
        return 0;

    switch (pCtx->eProto) {
#ifdef DEMUX_MP4
    case DEMUX_PROTO_FILE_MP4:
        return Mp4Demuxer_GetDuration(pCtx->impl.pMp4);
#endif
    default:
        return 0; /* Live streams have no duration */
    }
}

/* ============================================================================
 * Channel-based API Implementation
 * Compatible with mpp_demux, with auto-reconnect and thread management.
 * ============================================================================ */

#define DEMUX_LOGE(fmt, ...) fprintf(stderr, "[DEMUX][ERR] " fmt "\n", ##__VA_ARGS__)
#define DEMUX_LOGI(fmt, ...) fprintf(stdout, "[DEMUX][INF] " fmt "\n", ##__VA_ARGS__)

#define CHN_STATE_IDLE 0
#define CHN_STATE_CREATED 1
#define CHN_STATE_RUNNING 2
#define CHN_STATE_STOPPING 3

typedef struct _DemuxChn {
    S32 s32Created;
    S32 s32State;
    volatile S32 s32Stop;
    S32 s32ThreadAlive;
    S32 s32ChnId;
    pthread_t thread;
    pthread_mutex_t lock;

    DemuxChnAttr stAttr;
    DemuxStreamInfo stStreamInfo;
    DemuxPacketCallback pfnCb;
    VOID *pCbPriv;
    MppNode stSrcNode;

    /* Native demux context */
    DemuxCtx *pCtx;

    /* Statistics */
    U64 u64PacketCount;
    U64 u64ReconnectCount;

    /* Reconnect state: wait for keyframe after reconnect */
    BOOL bWaitKeyFrame;
} DemuxChn;

typedef struct _DemuxGlobal {
    S32 s32Init;
    pthread_mutex_t lock;
    DemuxChn astChn[DEMUX_MAX_CHN];
} DemuxGlobal;

static DemuxGlobal g_stDemuxCtx = {0};

static S32 demux_check_chn(S32 s32ChnId) {
    if (s32ChnId < 0 || s32ChnId >= DEMUX_MAX_CHN) {
        return ERR_DEMUX_INVALID_CHN;
    }
    return ERR_DEMUX_OK;
}

static S32 demux_deliver_packet(DemuxChn *pChn, const DemuxPacket *pPkt) {
    S32 ret = ERR_DEMUX_OK;

    /* After reconnect, wait for keyframe to avoid decoder errors */
    if (pChn->bWaitKeyFrame) {
        if (!pPkt->bKeyFrame) {
            /* Skip non-keyframe until we get an IDR */
            return ERR_DEMUX_OK;
        }
        /* Got keyframe, clear the flag */
        pChn->bWaitKeyFrame = MPP_FALSE;
        DEMUX_LOGI("Channel %d: Got keyframe after reconnect, resuming", pChn->s32ChnId);
    }

    /* User callback */
    if (pChn->pfnCb) {
        ret = pChn->pfnCb(pChn->s32ChnId, pPkt, pChn->pCbPriv);
    }

    /* SYS_SendStream for bind mode */
    if (ret == ERR_DEMUX_OK) {
        StreamBufferInfo stStream;
        memset(&stStream, 0, sizeof(stStream));

        stStream.pu8Addr = pPkt->pu8Data;
        stStream.u32Size = pPkt->u32Size;
        stStream.bKeyFrame = pPkt->bKeyFrame;
        stStream.bEndOfStream = MPP_FALSE;
        stStream.u64PTS = pPkt->u64PTS;
        stStream.u32Width = pPkt->u32Width;
        stStream.u32Height = pPkt->u32Height;

        switch (pPkt->eCodecType) {
        case DEMUX_CODEC_H264:
            stStream.eCodecType = MPP_STREAM_CODEC_H264;
            break;
        case DEMUX_CODEC_H265:
            stStream.eCodecType = MPP_STREAM_CODEC_H265;
            break;
        case DEMUX_CODEC_MJPEG:
            stStream.eCodecType = MPP_STREAM_CODEC_MJPEG;
            break;
        default:
            stStream.eCodecType = MPP_STREAM_CODEC_UNKNOWN;
            break;
        }

        /* For file protocols, limit send rate to avoid overwhelming decoder.
         * Network protocols (RTSP/RTMP) have natural flow control. */
        BOOL isFileProto =
            (pChn->pCtx && (pChn->pCtx->eProto == DEMUX_PROTO_FILE_MP4 || pChn->pCtx->eProto == DEMUX_PROTO_FILE_TS ||
                                pChn->pCtx->eProto == DEMUX_PROTO_FILE_FLV));

        /* Send with backpressure.
         * File demux has no natural network backpressure. Dropping one TS PES can
         * poison all following P frames in a long GOP, so file mode must wait
         * until the bound consumer has room instead of silently discarding data. */
        S32 send_ret;
        U32 retry = 0;
        U32 maxRetries = isFileProto ? 1500 : 50; /* File: up to 30s */
        U32 retryDelayUs = isFileProto ? 20000 : 10000;

        while (!pChn->s32Stop && (send_ret = SYS_SendStream(&pChn->stSrcNode, &stStream)) != 0 && retry < maxRetries) {
            usleep(retryDelayUs);
            retry++;
        }
        if (send_ret != 0) {
            DEMUX_LOGE("Channel %d: SYS_SendStream failed after %u retries, dropping packet size=%u key=%d pts=%llu",
                pChn->s32ChnId, retry, pPkt->u32Size, pPkt->bKeyFrame, (uint64_t)pPkt->u64PTS);
            return send_ret;
        }
    }

    pChn->u64PacketCount++;
    return ret;
}

static S32 demux_deliver_eos(DemuxChn *pChn) {
    StreamBufferInfo stStream;
    S32 send_ret;
    U32 retry = 0;

    if (!pChn) {
        return ERR_DEMUX_NULL_PTR;
    }

    memset(&stStream, 0, sizeof(stStream));
    stStream.bEndOfStream = MPP_TRUE;
    stStream.eCodecType = MPP_STREAM_CODEC_UNKNOWN;

    while (!pChn->s32Stop && (send_ret = SYS_SendStream(&pChn->stSrcNode, &stStream)) != 0 && retry < 1500) {
        usleep(20000);
        retry++;
    }

    if (send_ret != 0) {
        DEMUX_LOGE("Channel %d: failed to send EOS after %u retries", pChn->s32ChnId, retry);
        return send_ret;
    }

    DEMUX_LOGI("Channel %d: EOS sent", pChn->s32ChnId);
    return ERR_DEMUX_OK;
}

static void *demux_thread_proc(void *arg) {
    DemuxChn *pChn = (DemuxChn *)arg;
    DemuxPacket pkt;
    S32 ret;

    if (!pChn)
        return NULL;

    pChn->s32ThreadAlive = 1;
    DEMUX_LOGI("Channel %d thread started", pChn->s32ChnId);

    while (!pChn->s32Stop) {
        /* Create/reconnect */
        if (!pChn->pCtx) {
            pChn->pCtx = Demux_Create(pChn->stAttr.szUrl);
            if (!pChn->pCtx) {
                DEMUX_LOGE("Channel %d: Demux_Create failed", pChn->s32ChnId);
                usleep(pChn->stAttr.u32ReconnectMs * 1000);
                continue;
            }
        }

        /* Connect */
        ret = Demux_Open(pChn->pCtx, pChn->stAttr.bPreferTcp, pChn->stAttr.u32OpenTimeoutMs);
        if (ret != 0) {
            DEMUX_LOGE("Channel %d: Connect failed (ret=%d), reconnecting in %ums", pChn->s32ChnId, ret,
                pChn->stAttr.u32ReconnectMs);
            Demux_Destroy(pChn->pCtx);
            pChn->pCtx = NULL;
            pChn->u64ReconnectCount++;
            usleep(pChn->stAttr.u32ReconnectMs * 1000);
            continue;
        }

        /* Get stream info */
        Demux_GetStreamInfoCtx(pChn->pCtx, &pChn->stStreamInfo);

        BOOL bIsFileProto =
            (pChn->pCtx && (pChn->pCtx->eProto == DEMUX_PROTO_FILE_MP4 || pChn->pCtx->eProto == DEMUX_PROTO_FILE_TS ||
                                pChn->pCtx->eProto == DEMUX_PROTO_FILE_FLV));

        /* If resolution not available from SDP, probe first packets to get SPS.
         * File streams are intentionally not probed here: probing delivers up to
         * 100 packets without playback pacing, which can overrun VDEC input
         * before the normal paced read loop starts. VDEC will report resolution
         * through source-change once it receives SPS in the paced loop. */
        if (!bIsFileProto && (pChn->stStreamInfo.u32Width == 0 || pChn->stStreamInfo.u32Height == 0)) {
            DEMUX_LOGI("Channel %d: probing stream for resolution...", pChn->s32ChnId);
            DemuxPacket probePkt;
            S32 probeCount = 0;
            const S32 maxProbe = 100; /* Read up to 100 packets to find SPS */

            while (probeCount < maxProbe && !pChn->s32Stop) {
                memset(&probePkt, 0, sizeof(probePkt));
                ret = Demux_ReadPacket(pChn->pCtx, &probePkt);
                if (ret != 0)
                    break;

                probeCount++;

                /* Check if packet contains resolution info (parsed from SPS) */
                if (probePkt.u32Width > 0 && probePkt.u32Height > 0) {
                    pChn->stStreamInfo.u32Width = probePkt.u32Width;
                    pChn->stStreamInfo.u32Height = probePkt.u32Height;
                    DEMUX_LOGI("Channel %d: probed resolution %ux%u from packet %d", pChn->s32ChnId, probePkt.u32Width,
                        probePkt.u32Height, probeCount);

                    /* Deliver this packet too */
                    if (probePkt.pu8Data && probePkt.u32Size > 0) {
                        demux_deliver_packet(pChn, &probePkt);
                    }
                    break;
                }

                /* Deliver probe packets */
                if (probePkt.pu8Data && probePkt.u32Size > 0) {
                    demux_deliver_packet(pChn, &probePkt);
                }
            }
        }

        DEMUX_LOGI("Channel %d connected: %ux%u fps=%u codec=%d", pChn->s32ChnId, pChn->stStreamInfo.u32Width,
            pChn->stStreamInfo.u32Height, pChn->stStreamInfo.u32Fps, pChn->stStreamInfo.eCodecType);

        /* Read loop */
        /* For file protocols, pace the read rate to match playback fps,
         * mimicking the natural pacing of network protocols (RTSP/RTMP). */
        U32 u32FrameIntervalUs = 0;
        if (bIsFileProto) {
            U32 fps = pChn->stStreamInfo.u32Fps;
            if (fps == 0 || fps > 120)
                fps = 25; /* default 25fps */
            u32FrameIntervalUs = 1000000 / fps;
        }

        while (!pChn->s32Stop) {
            memset(&pkt, 0, sizeof(pkt));
            ret = Demux_ReadPacket(pChn->pCtx, &pkt);

            if (ret == ERR_DEMUX_NO_STREAM) {
                DEMUX_LOGI("Channel %d: End of stream", pChn->s32ChnId);
                /* For file-based protocols, exit cleanly on EOF */
                if (pChn->pCtx &&
                    (pChn->pCtx->eProto == DEMUX_PROTO_FILE_MP4 || pChn->pCtx->eProto == DEMUX_PROTO_FILE_TS ||
                        pChn->pCtx->eProto == DEMUX_PROTO_FILE_FLV)) {
                    demux_deliver_eos(pChn);
                    pChn->s32Stop = 1; /* Signal stop to exit thread */
                }
                break;
            }
            if (ret != 0) {
                if (pChn->s32Stop)
                    break;
                DEMUX_LOGE("Channel %d: Read error (ret=%d)", pChn->s32ChnId, ret);
                break;
            }

            if (pkt.pu8Data && pkt.u32Size > 0) {
                demux_deliver_packet(pChn, &pkt);
            }

            /* Pace file reading to match playback rate */
            if (u32FrameIntervalUs > 0) {
                usleep(u32FrameIntervalUs);
            }
        }

        /* Cleanup for reconnect */
        Demux_Close(pChn->pCtx);
        Demux_Destroy(pChn->pCtx);
        pChn->pCtx = NULL;

        if (!pChn->s32Stop) {
            pChn->u64ReconnectCount++;
            /* Set flag to wait for keyframe after reconnect */
            pChn->bWaitKeyFrame = MPP_TRUE;
            DEMUX_LOGI("Channel %d: Reconnecting in %ums (count=%llu)", pChn->s32ChnId, pChn->stAttr.u32ReconnectMs,
                (uint64_t)pChn->u64ReconnectCount);
            usleep(pChn->stAttr.u32ReconnectMs * 1000);
        }
    }

    DEMUX_LOGI("Channel %d thread exiting (packets=%llu, reconnects=%llu)", pChn->s32ChnId,
        (uint64_t)pChn->u64PacketCount, (uint64_t)pChn->u64ReconnectCount);

    pChn->s32ThreadAlive = 0;
    return NULL;
}

/* ======================== Channel Public API ======================== */

S32 DEMUX_Init(VOID) {
    S32 i;

    if (g_stDemuxCtx.s32Init) {
        return ERR_DEMUX_ALREADY_INIT;
    }

    if (pthread_mutex_init(&g_stDemuxCtx.lock, NULL) != 0) {
        return ERR_DEMUX_BUSY;
    }

    for (i = 0; i < DEMUX_MAX_CHN; i++) {
        DemuxChn *pChn = &g_stDemuxCtx.astChn[i];
        memset(pChn, 0, sizeof(*pChn));
        pChn->s32ChnId = i;
        pChn->stSrcNode.eModId = MPP_ID_DEMUX;
        pChn->stSrcNode.s32DevId = 0;
        pChn->stSrcNode.s32ChnId = i;
        pthread_mutex_init(&pChn->lock, NULL);
    }

    g_stDemuxCtx.s32Init = 1;
    DEMUX_LOGI("Initialized");
    return ERR_DEMUX_OK;
}

S32 DEMUX_Exit(VOID) {
    S32 i;

    if (!g_stDemuxCtx.s32Init) {
        return ERR_DEMUX_NOT_INIT;
    }

    for (i = 0; i < DEMUX_MAX_CHN; i++) {
        if (g_stDemuxCtx.astChn[i].s32Created) {
            DEMUX_DestroyChn(i);
        }
        pthread_mutex_destroy(&g_stDemuxCtx.astChn[i].lock);
    }

    pthread_mutex_destroy(&g_stDemuxCtx.lock);
    memset(&g_stDemuxCtx, 0, sizeof(g_stDemuxCtx));

    DEMUX_LOGI("Exited");
    return ERR_DEMUX_OK;
}

S32 DEMUX_CreateChn(S32 s32ChnId, const DemuxChnAttr *pstAttr) {
    DemuxChn *pChn;
    S32 ret;

    if (!pstAttr)
        return ERR_DEMUX_NULL_PTR;
    if (!g_stDemuxCtx.s32Init)
        return ERR_DEMUX_NOT_INIT;

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK)
        return ret;

    pChn = &g_stDemuxCtx.astChn[s32ChnId];

    pthread_mutex_lock(&pChn->lock);
    if (pChn->s32Created) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_DEMUX_BUSY;
    }

    memcpy(&pChn->stAttr, pstAttr, sizeof(*pstAttr));

    /* Set defaults */
    if (pChn->stAttr.u32OpenTimeoutMs == 0)
        pChn->stAttr.u32OpenTimeoutMs = 5000;
    if (pChn->stAttr.u32RwTimeoutMs == 0)
        pChn->stAttr.u32RwTimeoutMs = 5000;
    if (pChn->stAttr.u32ReconnectMs == 0)
        pChn->stAttr.u32ReconnectMs = 2000;

    pChn->s32State = CHN_STATE_CREATED;
    pChn->s32Created = 1;
    pChn->u64PacketCount = 0;
    pChn->u64ReconnectCount = 0;

    pthread_mutex_unlock(&pChn->lock);

    DEMUX_LOGI("Channel %d created: %s", s32ChnId, pstAttr->szUrl);
    return ERR_DEMUX_OK;
}

S32 DEMUX_DestroyChn(S32 s32ChnId) {
    DemuxChn *pChn;
    S32 ret;

    if (!g_stDemuxCtx.s32Init)
        return ERR_DEMUX_NOT_INIT;

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK)
        return ret;

    pChn = &g_stDemuxCtx.astChn[s32ChnId];

    DEMUX_StopChn(s32ChnId);

    pthread_mutex_lock(&pChn->lock);

    if (pChn->pCtx) {
        Demux_Close(pChn->pCtx);
        Demux_Destroy(pChn->pCtx);
        pChn->pCtx = NULL;
    }

    memset(&pChn->stStreamInfo, 0, sizeof(pChn->stStreamInfo));
    pChn->pfnCb = NULL;
    pChn->pCbPriv = NULL;
    pChn->s32Created = 0;
    pChn->s32State = CHN_STATE_IDLE;

    pthread_mutex_unlock(&pChn->lock);

    DEMUX_LOGI("Channel %d destroyed", s32ChnId);
    return ERR_DEMUX_OK;
}

S32 DEMUX_StartChn(S32 s32ChnId) {
    DemuxChn *pChn;
    S32 ret;

    if (!g_stDemuxCtx.s32Init)
        return ERR_DEMUX_NOT_INIT;

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK)
        return ret;

    pChn = &g_stDemuxCtx.astChn[s32ChnId];

    pthread_mutex_lock(&pChn->lock);

    if (!pChn->s32Created) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_DEMUX_INVALID_CHN;
    }

    if (pChn->s32State == CHN_STATE_RUNNING) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_DEMUX_OK;
    }

    pChn->s32Stop = 0;
    pChn->s32State = CHN_STATE_RUNNING;

    if (pthread_create(&pChn->thread, NULL, demux_thread_proc, pChn) != 0) {
        pChn->s32State = CHN_STATE_CREATED;
        pthread_mutex_unlock(&pChn->lock);
        DEMUX_LOGE("Channel %d: pthread_create failed", s32ChnId);
        return ERR_DEMUX_BUSY;
    }

    pthread_mutex_unlock(&pChn->lock);

    DEMUX_LOGI("Channel %d started", s32ChnId);
    return ERR_DEMUX_OK;
}

S32 DEMUX_StopChn(S32 s32ChnId) {
    DemuxChn *pChn;
    S32 ret;

    if (!g_stDemuxCtx.s32Init)
        return ERR_DEMUX_NOT_INIT;

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK)
        return ret;

    pChn = &g_stDemuxCtx.astChn[s32ChnId];

    pthread_mutex_lock(&pChn->lock);

    if (!pChn->s32Created || pChn->s32State != CHN_STATE_RUNNING) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_DEMUX_OK;
    }

    pChn->s32Stop = 1;
    pChn->s32State = CHN_STATE_STOPPING;
    pthread_mutex_unlock(&pChn->lock);

    pthread_join(pChn->thread, NULL);

    pthread_mutex_lock(&pChn->lock);
    pChn->s32State = CHN_STATE_CREATED;
    pthread_mutex_unlock(&pChn->lock);

    DEMUX_LOGI("Channel %d stopped", s32ChnId);
    return ERR_DEMUX_OK;
}

S32 DEMUX_GetStreamInfo(S32 s32ChnId, DemuxStreamInfo *pstInfo) {
    DemuxChn *pChn;
    S32 ret;

    if (!pstInfo)
        return ERR_DEMUX_NULL_PTR;
    if (!g_stDemuxCtx.s32Init)
        return ERR_DEMUX_NOT_INIT;

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK)
        return ret;

    pChn = &g_stDemuxCtx.astChn[s32ChnId];

    pthread_mutex_lock(&pChn->lock);
    if (!pChn->s32Created) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_DEMUX_INVALID_CHN;
    }
    memcpy(pstInfo, &pChn->stStreamInfo, sizeof(*pstInfo));
    pthread_mutex_unlock(&pChn->lock);

    return ERR_DEMUX_OK;
}

S32 DEMUX_SetPacketCallback(S32 s32ChnId, DemuxPacketCallback pfnCb, VOID *pPriv) {
    DemuxChn *pChn;
    S32 ret;

    if (!g_stDemuxCtx.s32Init)
        return ERR_DEMUX_NOT_INIT;

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK)
        return ret;

    pChn = &g_stDemuxCtx.astChn[s32ChnId];

    pthread_mutex_lock(&pChn->lock);
    if (!pChn->s32Created) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_DEMUX_INVALID_CHN;
    }
    pChn->pfnCb = pfnCb;
    pChn->pCbPriv = pPriv;
    pthread_mutex_unlock(&pChn->lock);

    return ERR_DEMUX_OK;
}

S32 DEMUX_GetSrcNode(S32 s32ChnId, MppNode *pstNode) {
    DemuxChn *pChn;
    S32 ret;

    if (!pstNode)
        return ERR_DEMUX_NULL_PTR;
    if (!g_stDemuxCtx.s32Init)
        return ERR_DEMUX_NOT_INIT;

    ret = demux_check_chn(s32ChnId);
    if (ret != ERR_DEMUX_OK)
        return ret;

    pChn = &g_stDemuxCtx.astChn[s32ChnId];

    pthread_mutex_lock(&pChn->lock);
    if (!pChn->s32Created) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_DEMUX_INVALID_CHN;
    }
    *pstNode = pChn->stSrcNode;
    pthread_mutex_unlock(&pChn->lock);

    return ERR_DEMUX_OK;
}
