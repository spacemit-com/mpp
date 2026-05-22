/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    venc_api.c
 * @Date      :    2026-04-19
 * @Brief     :    VENC new public API implementation.
 *                 Wraps the old context-based venc.c (venc_ctx_*) into
 *                 channel-ID-based API defined in venc_api.h / venc_type.h.
 *                 Each channel owns one MppVencCtx internally.
 *                 Thread-safe via per-channel mutex.
 *------------------------------------------------------------------------------
 */

#define ENABLE_DEBUG 0

#include "venc/venc_api.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "venc.h"
#include "log.h"
#include "frame.h"
#include "packet.h"
#include "sys/sys_api.h"
#include "sys/vb_api.h"

#define MODULE_TAG "mpp_venc_api"

/* ======================== Internal Channel Context ======================== */

typedef enum _VencChnState {
    VENC_CHN_STATE_IDLE = 0,
    VENC_CHN_STATE_STARTED,
} VencChnState;

typedef struct _VencChnCtx {
    BOOL bUsed;
    VencChnState eState;
    VencChnAttr stAttr;
    MppVencCtx *pOldCtx;
    pthread_mutex_t lock;
    S32 s32ChnId;

    /* frame input thread: receives bound frames via SYS_RecvFrame */
    pthread_t frameInputTid;
    BOOL bFrameInputRun;
    BOOL bBound;     /**< TRUE if SYS_RecvFrame got data */
    BOOL bBoundSink; /**< TRUE if SYS_SendStream got data */

    /* Task thread: sole consumer of venc_ctx_request_output_stream; pushes
     *  same StreamBufferInfo to SYS_SendStream and to the queue for VENC_GetStream;
     *  buffer is returned to the encoder only in VENC_ReleaseStream. */
    pthread_t taskTid;
    BOOL bTaskRun;

/* Stream output queue: task enqueues, VENC_GetStream dequeues (shared packet) */
#define VENC_STREAM_QUEUE_SIZE 16
    StreamBufferInfo astStreamQueue[VENC_STREAM_QUEUE_SIZE];
    U32 u32QueueHead; /* GetStream reads from here */
    U32 u32QueueTail; /* task writes here */
    U32 u32QueueCount;
    pthread_mutex_t queueLock;
    pthread_cond_t queueNotEmpty;
    pthread_cond_t queueNotFull;
} VencChnCtx;

/* ======================== Global State ======================== */

static BOOL g_bVencInited = MPP_FALSE;
static VencChnCtx g_stChn[VENC_MAX_CHN];
static pthread_mutex_t g_stGlobalLock = PTHREAD_MUTEX_INITIALIZER;

/* ======================== Helpers ======================== */

static inline BOOL venc_chn_valid(S32 s32ChnId) {
    return (s32ChnId >= 0 && s32ChnId < VENC_MAX_CHN);
}

/**
 * @brief Convert public VencChnAttr to old MppVencCtx parameters.
 */
static void venc_attr_to_old_para(const VencChnAttr *pstAttr, MppVencCtx *pOldCtx) {
    pOldCtx->eCodecType = CODEC_V4L2_LINLONV5V7;

    switch (pstAttr->eCodecType) {
        case MPP_STREAM_CODEC_H263:
            pOldCtx->stVencPara.eCodingType = CODING_H263;
            break;
        case MPP_STREAM_CODEC_H264:
            pOldCtx->stVencPara.eCodingType = CODING_H264;
            break;
        case MPP_STREAM_CODEC_H264_MVC:
            pOldCtx->stVencPara.eCodingType = CODING_H264_MVC;
            break;
        case MPP_STREAM_CODEC_H264_NO_SC:
            pOldCtx->stVencPara.eCodingType = CODING_H264_NO_SC;
            break;
        case MPP_STREAM_CODEC_H265:
            pOldCtx->stVencPara.eCodingType = CODING_H265;
            break;
        case MPP_STREAM_CODEC_MJPEG:
            pOldCtx->stVencPara.eCodingType = CODING_MJPEG;
            break;
        case MPP_STREAM_CODEC_JPEG:
            pOldCtx->stVencPara.eCodingType = CODING_JPEG;
            break;
        case MPP_STREAM_CODEC_VP8:
            pOldCtx->stVencPara.eCodingType = CODING_VP8;
            break;
        case MPP_STREAM_CODEC_VP9:
            pOldCtx->stVencPara.eCodingType = CODING_VP9;
            break;
        case MPP_STREAM_CODEC_AV1:
            pOldCtx->stVencPara.eCodingType = CODING_AV1;
            break;
        case MPP_STREAM_CODEC_AVS:
            pOldCtx->stVencPara.eCodingType = CODING_AVS;
            break;
        case MPP_STREAM_CODEC_AVS2:
            pOldCtx->stVencPara.eCodingType = CODING_AVS2;
            break;
        case MPP_STREAM_CODEC_MPEG1:
            pOldCtx->stVencPara.eCodingType = CODING_MPEG1;
            break;
        case MPP_STREAM_CODEC_MPEG2:
            pOldCtx->stVencPara.eCodingType = CODING_MPEG2;
            break;
        case MPP_STREAM_CODEC_MPEG4:
            pOldCtx->stVencPara.eCodingType = CODING_MPEG4;
            break;
        case MPP_STREAM_CODEC_RV:
            pOldCtx->stVencPara.eCodingType = CODING_RV;
            break;
        case MPP_STREAM_CODEC_VC1:
            pOldCtx->stVencPara.eCodingType = CODING_VC1;
            break;
        case MPP_STREAM_CODEC_VC1_ANNEX_L:
            pOldCtx->stVencPara.eCodingType = CODING_VC1_ANNEX_L;
            break;
        case MPP_STREAM_CODEC_FWHT:
            pOldCtx->stVencPara.eCodingType = CODING_FWHT;
            break;
        default:
            pOldCtx->stVencPara.eCodingType = CODING_H264;
            break;
    }

    pOldCtx->stVencPara.nWidth = (S32)pstAttr->u32Width;
    pOldCtx->stVencPara.nHeight = (S32)pstAttr->u32Height;
    pOldCtx->stVencPara.nAlign = (S32)pstAttr->u32Align;
    pOldCtx->stVencPara.PixelFormat = pstAttr->eInputPixelFormat;
    pOldCtx->stVencPara.nBitrate = (S32)pstAttr->u32Bitrate;
    pOldCtx->stVencPara.nFrameRate = (S32)pstAttr->u32FrameRate;
    pOldCtx->stVencPara.nProfile = (S32)pstAttr->u32Profile;
    pOldCtx->stVencPara.nRotateDegree = (S32)pstAttr->u32RotateDegree;

    switch (pstAttr->eFrameBufMode) {
        case VENC_FRAME_BUF_DMABUF_INTERNAL:
            pOldCtx->stVencPara.eFrameBufferType = MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL;
            break;
        case VENC_FRAME_BUF_NORMAL_INTERNAL:
            pOldCtx->stVencPara.eFrameBufferType = MPP_FRAME_BUFFERTYPE_NORMAL_INTERNAL;
            break;
        case VENC_FRAME_BUF_DMABUF_EXTERNAL:
            pOldCtx->stVencPara.eFrameBufferType = MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL;
            break;
        default:
            pOldCtx->stVencPara.eFrameBufferType = MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL;
            break;
    }
}

/**
 * @brief Fill StreamBufferInfo from MppPacket output.
 */
static void venc_fill_stream_info(MppPacket *pPacket, StreamBufferInfo *pstOut) {
    memset(pstOut, 0, sizeof(*pstOut));
    pstOut->pu8Addr = (const U8 *)PACKET_GetDataPointer(pPacket);
    pstOut->u32Size = (U32)PACKET_GetLength(pPacket);
    pstOut->u64PTS = (U64)PACKET_GetPts(pPacket);
    pstOut->bEndOfStream = PACKET_GetEos(pPacket);
    pstOut->u32Width = (U32)PACKET_GetWidth(pPacket);
    pstOut->u32Height = (U32)PACKET_GetHeight(pPacket);
}

/* ======================== Task & Recycle Threads ======================== */

/**
 * @brief Task thread: continuously polls encoded stream from encoder, sends
 *        a copy to bound sinks via SYS_SendStream, then enqueues the same
 *        packet for VENC_GetStream; VENC_ReleaseStream returns the buffer.
 */
static void *venc_task_thread(void *arg) {
    VencChnCtx *pChn = (VencChnCtx *)arg;
    S32 s32ChnId = pChn->s32ChnId;
    S32 ret = 0;

    MppNode stSrcNode;
    stSrcNode.eModId = MPP_ID_VENC;
    stSrcNode.s32DevId = 0;
    stSrcNode.s32ChnId = s32ChnId;

    info("venc task thread started: chn %d", s32ChnId);

    while (pChn->bTaskRun) {
        /* Request encoded stream from encoder (non-blocking poll) */
        MppPacket *pPacket = PACKET_Create();
        if (!pPacket) {
            usleep(1000);
            continue;
        }

        /* Allocate buffer for encoded output */
        S32 allocSize = pChn->pOldCtx->stVencPara.nWidth * pChn->pOldCtx->stVencPara.nHeight;
        if (allocSize <= 0)
            allocSize = 1280 * 720;
        if (PACKET_Alloc(pPacket, allocSize) != MPP_OK) {
            PACKET_Free(pPacket);
            usleep(1000);
            continue;
        }

        MppData *pSrcData = PACKET_GetBaseData(pPacket);
        S32 ret = venc_ctx_request_output_stream(pChn->pOldCtx, pSrcData);
        if (ret != MPP_OK) {
            PACKET_Free(pPacket);
            usleep(5000); /* no stream available, sleep 10ms */
            continue;
        }

        /* Successfully got encoded output; recycle one input buffer so the
         * encoder can accept more frames when the pool is full. */
        venc_ctx_return_input_frame(pChn->pOldCtx, NULL);

        /* Fill StreamBufferInfo and send to bound sinks */
        StreamBufferInfo stStream;
        venc_fill_stream_info(pPacket, &stStream);

        if (stStream.pu8Addr && stStream.u32Size > 0) {
            ret = SYS_SendStream(&stSrcNode, &stStream);
            if (ret != 0) {
                if (SYS_ERR_NOT_FOUND == ret) {
                    pChn->bBoundSink = MPP_FALSE;
                }
                /* SYS_SendStream failure is non-fatal; still enqueue for
                 * VENC_GetStream below. */
            } else if (!pChn->bBoundSink && ret == 0) {
                pChn->bBoundSink = MPP_TRUE;
                info("task thread: chn %d bound sink active", s32ChnId);
            }
        }

        if (pChn->bBoundSink) {
            /* Bind mode: downstream already consumed the data via
             * SYS_SendStream (which does memcpy into DMA buf).
             * Return the CAPTURE buffer to the encoder immediately
             * so it can keep producing output. */
            venc_ctx_return_output_stream(pChn->pOldCtx, pSrcData);
            PACKET_Free(pPacket);
            continue;
        }

        /* Non-bind mode: queue for VENC_GetStream;
         * VENC_ReleaseStream returns the buffer. */
        stStream.ulPrivate = (UL)pPacket;

        pthread_mutex_lock(&pChn->queueLock);
        while (pChn->u32QueueCount >= VENC_STREAM_QUEUE_SIZE && pChn->bTaskRun) {
            pthread_cond_wait(&pChn->queueNotFull, &pChn->queueLock);
        }
        if (!pChn->bTaskRun) {
            pthread_mutex_unlock(&pChn->queueLock);
            /* Clean up packet before exit */
            venc_ctx_return_output_stream(pChn->pOldCtx, pSrcData);
            PACKET_Free(pPacket);
            break;
        }
        pChn->astStreamQueue[pChn->u32QueueTail] = stStream;
        pChn->u32QueueTail = (pChn->u32QueueTail + 1) % VENC_STREAM_QUEUE_SIZE;
        pChn->u32QueueCount++;
        pthread_cond_broadcast(&pChn->queueNotEmpty);
        pthread_mutex_unlock(&pChn->queueLock);
    }

    info("venc task thread exiting: chn %d", s32ChnId);
    return NULL;
}

/** Return all queued output packets to the encoder (used on stop / drain). */
static void venc_drain_out_stream_queue(VencChnCtx *pChn) {
    pthread_mutex_lock(&pChn->queueLock);
    while (pChn->u32QueueCount > 0) {
        StreamBufferInfo st = pChn->astStreamQueue[pChn->u32QueueHead];
        pChn->u32QueueHead = (pChn->u32QueueHead + 1) % VENC_STREAM_QUEUE_SIZE;
        pChn->u32QueueCount--;
        MppPacket *pPacket = (MppPacket *)st.ulPrivate;
        if (pPacket) {
            MppData *pData = PACKET_GetBaseData(pPacket);
            venc_ctx_return_output_stream(pChn->pOldCtx, pData);
            PACKET_Free(pPacket);
        }
    }
    pthread_cond_broadcast(&pChn->queueNotEmpty);
    pthread_mutex_unlock(&pChn->queueLock);
}

static S32 venc_start_threads(VencChnCtx *pChn) {
    /* Init queue */
    pChn->u32QueueHead = 0;
    pChn->u32QueueTail = 0;
    pChn->u32QueueCount = 0;
    pthread_mutex_init(&pChn->queueLock, NULL);
    pthread_cond_init(&pChn->queueNotEmpty, NULL);
    pthread_cond_init(&pChn->queueNotFull, NULL);

    pChn->bTaskRun = MPP_TRUE;
    if (pthread_create(&pChn->taskTid, NULL, venc_task_thread, pChn) != 0) {
        error("failed to create venc task thread for chn %d", pChn->s32ChnId);
        return ERR_VENC_NOMEM;
    }

    return ERR_VENC_OK;
}

static void venc_stop_threads(VencChnCtx *pChn) {
    pChn->bTaskRun = MPP_FALSE;
    pthread_mutex_lock(&pChn->queueLock);
    pthread_cond_broadcast(&pChn->queueNotFull);
    pthread_mutex_unlock(&pChn->queueLock);
    pthread_join(pChn->taskTid, NULL);

    venc_drain_out_stream_queue(pChn);

    /* Cleanup sync primitives */
    pthread_mutex_destroy(&pChn->queueLock);
    pthread_cond_destroy(&pChn->queueNotEmpty);
    pthread_cond_destroy(&pChn->queueNotFull);
}

/* ======================== Frame Input Thread ======================== */

/**
 * @brief  Thread: receive VB frame buffers from bound source via SYS_RecvFrame,
 *         encode each frame, and output stream via VENC_RecvStream path.
 *         Runs when VENC channel is enabled; stops on disable.
 */
static void *venc_frame_input_task(void *arg) {
    VencChnCtx *pChn = (VencChnCtx *)arg;
    S32 s32ChnId = pChn->s32ChnId;
    S32 ret = 0;

    MppNode stSink = {
        .eModId = MPP_ID_VENC,
        .s32DevId = 0,
        .s32ChnId = s32ChnId,
    };

    info("frame input task started: chn %d", s32ChnId);

    while (pChn->bFrameInputRun) {
        UL ulBuff = 0;
        ret = SYS_RecvFrame(&stSink, &ulBuff, 100);
        if (ret != 0) {
            if (SYS_ERR_NOT_FOUND == ret) {
                pChn->bBound = MPP_FALSE;
                usleep(20000);  // Sleep 20ms before retrying to avoid busy loop when no stream is bound
            }
            continue;
        }

        if (!pChn->bBound) {
            pChn->bBound = MPP_TRUE;
            info("frame input task: chn %d bound, frame input active", s32ChnId);
        }

        /* Get VideoFrameInfo from VB buffer handle */
        VideoFrameInfo stFrame;
        memset(&stFrame, 0, sizeof(stFrame));
        ret = VB_GetFrameInfo(ulBuff, &stFrame);
        if (ret != 0) {
            error("frame input task: VB_GetFrameInfo failed %d, chn %d", ret, s32ChnId);
            VB_ReleaseBuffer(ulBuff);
            continue;
        }

        /* Build MppFrame and encode */
        MppFrame *pFrame = FRAME_Create();
        if (!pFrame) {
            error("frame input task: FRAME_Create failed, chn %d", s32ChnId);
            VB_ReleaseBuffer(ulBuff);
            continue;
        }

        U32 u32Width = stFrame.stVencFrameInfo.stCommFrameInfo.u32Width;
        U32 u32Height = stFrame.stVencFrameInfo.stCommFrameInfo.u32Height;
        MppPixelFormat eFmt = stFrame.stVencFrameInfo.stCommFrameInfo.ePixelFormat;

        FRAME_SetWidth(pFrame, (S32)u32Width);
        FRAME_SetHeight(pFrame, (S32)u32Height);
        FRAME_SetPixelFormat(pFrame, (S32)eFmt);
        FRAME_SetPts(pFrame, (S64)stFrame.stVFrame.u64PTS);

        if (stFrame.stVFrame.u32PlaneStride[0] > 0) {
            FRAME_SetLineStride(pFrame, (S32)stFrame.stVFrame.u32PlaneStride[0]);
        }

        U32 nPlanes = stFrame.stVFrame.u32PlaneNum;
        if (nPlanes == 0)
            nPlanes = 1;
        if (nPlanes > FRAME_MAX_PLANE)
            nPlanes = FRAME_MAX_PLANE;
        FRAME_SetDataUsedNum(pFrame, (S32)nPlanes);

        for (U32 i = 0; i < nPlanes; i++) {
            if (stFrame.stVFrame.u32Fd[i] > 0) {
                FRAME_SetFD(pFrame, (S32)stFrame.stVFrame.u32Fd[i], (S32)i);
                FRAME_SetBufferType(pFrame, MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL);
            }
            if (stFrame.stVFrame.ulPlaneVirAddr[i]) {
                FRAME_SetDataPointer(pFrame, (S32)i, (U8 *)(stFrame.stVFrame.ulPlaneVirAddr[i]));
            }
        }

        MppData *pSinkData = FRAME_GetBaseData(pFrame);

        pthread_mutex_lock(&pChn->lock);
        if (pChn->eState == VENC_CHN_STATE_STARTED) {
            ret = venc_ctx_send_input_frame(pChn->pOldCtx, pSinkData);
            if (ret != MPP_OK) {
                error("frame input task: send_input_frame failed %d, chn %d", ret, s32ChnId);
            }
        }
        pthread_mutex_unlock(&pChn->lock);

        FRAME_Destory(pFrame);

        /* Release the VB buffer ref (SYS_RecvFrame added a ref) */
        VB_ReleaseBuffer(ulBuff);
    }

    info("frame input task exiting: chn %d", s32ChnId);
    return NULL;
}

/* ======================== Public API Implementation ======================== */

S32 VENC_Init(VOID) {
    pthread_mutex_lock(&g_stGlobalLock);

    if (g_bVencInited) {
        pthread_mutex_unlock(&g_stGlobalLock);
        return ERR_VENC_ALREADY_INIT;
    }

    memset(g_stChn, 0, sizeof(g_stChn));
    for (S32 i = 0; i < VENC_MAX_CHN; i++) {
        pthread_mutex_init(&g_stChn[i].lock, NULL);
    }

    g_bVencInited = MPP_TRUE;
    pthread_mutex_unlock(&g_stGlobalLock);
    return ERR_VENC_OK;
}

S32 VENC_Exit(VOID) {
    pthread_mutex_lock(&g_stGlobalLock);

    if (!g_bVencInited) {
        pthread_mutex_unlock(&g_stGlobalLock);
        return ERR_VENC_NOT_INIT;
    }

    /* Check all channels are destroyed */
    for (S32 i = 0; i < VENC_MAX_CHN; i++) {
        if (g_stChn[i].bUsed) {
            error("channel %d still in use, cannot exit", i);
            pthread_mutex_unlock(&g_stGlobalLock);
            return ERR_VENC_BUSY;
        }
    }

    for (S32 i = 0; i < VENC_MAX_CHN; i++) {
        pthread_mutex_destroy(&g_stChn[i].lock);
    }

    g_bVencInited = MPP_FALSE;
    pthread_mutex_unlock(&g_stGlobalLock);
    return ERR_VENC_OK;
}

S32 VENC_CreateChn(S32 s32ChnId, const VencChnAttr *pstAttr) {
    if (!pstAttr)
        return ERR_VENC_NULL_PTR;
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (pChn->bUsed) {
        error("channel %d already created", s32ChnId);
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_BUSY;
    }

    /* Create old-style context */
    MppVencCtx *pOldCtx = venc_ctx_create();
    if (!pOldCtx) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOMEM;
    }

    /* Convert attributes */
    venc_attr_to_old_para(pstAttr, pOldCtx);

    pChn->pOldCtx = pOldCtx;
    pChn->stAttr = *pstAttr;
    pChn->eState = VENC_CHN_STATE_IDLE;
    pChn->bUsed = MPP_TRUE;

    pthread_mutex_unlock(&pChn->lock);
    return ERR_VENC_OK;
}

S32 VENC_DestroyChn(S32 s32ChnId) {
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_INVALID_CHN;
    }
    if (pChn->eState == VENC_CHN_STATE_STARTED) {
        error("channel %d still started, stop it first", s32ChnId);
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_BUSY;
    }

    if (pChn->pOldCtx) {
        venc_ctx_destroy(pChn->pOldCtx);
        pChn->pOldCtx = NULL;
    }

    pChn->bUsed = MPP_FALSE;
    pthread_mutex_unlock(&pChn->lock);
    return ERR_VENC_OK;
}

S32 VENC_EnableChn(S32 s32ChnId) {
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_INVALID_CHN;
    }
    if (pChn->eState == VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_ALREADY_INIT;
    }

    S32 ret = venc_ctx_init(pChn->pOldCtx);
    if (ret != MPP_OK) {
        error("venc_ctx_init failed for chn %d, ret=%d", s32ChnId, ret);
        pthread_mutex_unlock(&pChn->lock);
        return ret;
    }

    pChn->s32ChnId = s32ChnId;
    pChn->eState = VENC_CHN_STATE_STARTED;

    /* Start frame input thread (receives bound frames via SYS_RecvFrame) */
    pChn->bFrameInputRun = MPP_TRUE;
    pChn->bBound = MPP_FALSE;
    pthread_mutex_unlock(&pChn->lock);

    if (pthread_create(&pChn->frameInputTid, NULL, venc_frame_input_task, pChn) != 0) {
        error("failed to create frame input thread for chn %d", s32ChnId);
        pthread_mutex_lock(&pChn->lock);
        pChn->bFrameInputRun = MPP_FALSE;
        venc_ctx_flush(pChn->pOldCtx);
        pChn->eState = VENC_CHN_STATE_IDLE;
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOMEM;
    }

    /* Start stream output task thread (pulls encoded data, SYS_SendStream, queue) */
    S32 ret2 = venc_start_threads(pChn);
    if (ret2 != ERR_VENC_OK) {
        error("venc_start_threads failed for chn %d, ret=%d", s32ChnId, ret2);
        pChn->bFrameInputRun = MPP_FALSE;
        pthread_join(pChn->frameInputTid, NULL);
        pthread_mutex_lock(&pChn->lock);
        venc_ctx_flush(pChn->pOldCtx);
        pChn->eState = VENC_CHN_STATE_IDLE;
        pthread_mutex_unlock(&pChn->lock);
        return ret2;
    }

    return ERR_VENC_OK;
}

S32 VENC_DisableChn(S32 s32ChnId) {
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_INVALID_CHN;
    }
    if (pChn->eState != VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOT_STARTED;
    }

    /* Stop stream output task thread and drain the stream queue */
    pthread_mutex_unlock(&pChn->lock);
    venc_stop_threads(pChn);
    pthread_mutex_lock(&pChn->lock);

    /* Stop frame input thread */
    pChn->bFrameInputRun = MPP_FALSE;
    pthread_mutex_unlock(&pChn->lock);
    pthread_join(pChn->frameInputTid, NULL);
    pthread_mutex_lock(&pChn->lock);

    venc_ctx_flush(pChn->pOldCtx);

    pChn->bBound = MPP_FALSE;
    pChn->eState = VENC_CHN_STATE_IDLE;
    pthread_mutex_unlock(&pChn->lock);
    return ERR_VENC_OK;
}

S32 VENC_SendFrame(S32 s32ChnId, const VideoFrameInfo *pstFrame, U32 u32TimeoutMs) {
    if (!pstFrame)
        return ERR_VENC_NULL_PTR;
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOT_STARTED;
    }

    if (pChn->bBound) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_BUSY;
    }

    /* Build MppFrame from VideoFrameInfo (zero-copy: set fd/pointer) */
    MppFrame *pFrame = FRAME_Create();
    if (!pFrame) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOMEM;
    }

    U32 u32Width = pstFrame->stVencFrameInfo.stCommFrameInfo.u32Width;
    U32 u32Height = pstFrame->stVencFrameInfo.stCommFrameInfo.u32Height;
    MppPixelFormat eFmt = pstFrame->stVencFrameInfo.stCommFrameInfo.ePixelFormat;

    FRAME_SetWidth(pFrame, (S32)u32Width);
    FRAME_SetHeight(pFrame, (S32)u32Height);
    FRAME_SetPixelFormat(pFrame, (S32)eFmt);
    FRAME_SetPts(pFrame, (S64)pstFrame->stVFrame.u64PTS);

    if (pstFrame->stVFrame.u32PlaneStride[0] > 0) {
        FRAME_SetLineStride(pFrame, (S32)pstFrame->stVFrame.u32PlaneStride[0]);
    }

    /* Set dma-buf fd or virtual address for each plane */
    U32 nPlanes = pstFrame->stVFrame.u32PlaneNum;
    if (nPlanes == 0)
        nPlanes = 1;
    if (nPlanes > FRAME_MAX_PLANE)
        nPlanes = FRAME_MAX_PLANE;
    FRAME_SetDataUsedNum(pFrame, (S32)nPlanes);

    for (U32 i = 0; i < nPlanes; i++) {
        if (pstFrame->stVFrame.u32Fd[i] > 0) {
            FRAME_SetFD(pFrame, (S32)pstFrame->stVFrame.u32Fd[i], (S32)i);
            FRAME_SetBufferType(pFrame, MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL);
        }
        if (pstFrame->stVFrame.ulPlaneVirAddr[i]) {
            FRAME_SetDataPointer(pFrame, (S32)i, (U8 *)(pstFrame->stVFrame.ulPlaneVirAddr[i]));
        }
    }

    MppData *pSinkData = FRAME_GetBaseData(pFrame);

    /* Send frame to encoder (V4L2 QBUF on OUTPUT port).
     * The linlonv5v7 encoder is fully async: hardware encodes the frame
     * after QBUF, and the encoded result appears on the CAPTURE port.
     * venc_task_thread polls the CAPTURE port for output.
     *
     * Do NOT call return_input_frame here — it would poll(POLLOUT,0)
     * and potentially DQBUF the buffer before hardware finishes encoding,
     * causing the CAPTURE port to never produce output.
     * Input buffers are recycled by return_input_frame only when the
     * buffer pool is exhausted (nInputQueuedNum >= ENCODER_INPUT_BUF_NUM). */
    S32 ret = venc_ctx_send_input_frame(pChn->pOldCtx, pSinkData);

    FRAME_Destory(pFrame);

    pthread_mutex_unlock(&pChn->lock);
    return (ret == MPP_OK) ? ERR_VENC_OK : ret;
}

S32 VENC_GetStream(S32 s32ChnId, StreamBufferInfo *pstStream, U32 u32TimeoutMs) {
    if (!pstStream)
        return ERR_VENC_NULL_PTR;
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOT_STARTED;
    }

    /* If a downstream module is bound to this VENC channel (via SYS_Bind),
     * the encoded stream is forwarded through SYS_SendStream automatically.
     * Manual GetStream is not allowed in this case. */
    if (pChn->bBoundSink) {
        error("chn %d has bound sink, VENC_GetStream not allowed", s32ChnId);
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_BUSY;
    }
    pthread_mutex_unlock(&pChn->lock);

    /*
     * The task thread is the only path that calls venc_ctx_request_output_stream.
     * The same MppPacket is queued here for zero-copy; VENC_ReleaseStream
     * returns the buffer to the HW after SYS_SendStream and the app are done.
     */
    pthread_mutex_lock(&pChn->queueLock);
    for (;;) {
        if (pChn->u32QueueCount > 0)
            break;
        if (!pChn->bTaskRun) {
            pthread_mutex_unlock(&pChn->queueLock);
            return ERR_VENC_NO_STREAM;
        }
        if (u32TimeoutMs == 0) {
            pthread_mutex_unlock(&pChn->queueLock);
            return ERR_VENC_NO_STREAM;
        }

        if (u32TimeoutMs == (U32)0xFFFFFFFFu) {
            pthread_cond_wait(&pChn->queueNotEmpty, &pChn->queueLock);
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += (time_t)(u32TimeoutMs / 1000);
            ts.tv_nsec += (int32_t)(u32TimeoutMs % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            if (pthread_cond_timedwait(&pChn->queueNotEmpty, &pChn->queueLock, &ts) == ETIMEDOUT) {
                pthread_mutex_unlock(&pChn->queueLock);
                return ERR_VENC_TIMEOUT;
            }
        }

        pthread_mutex_lock(&pChn->lock);
        BOOL chn_ok = pChn->bUsed && pChn->eState == VENC_CHN_STATE_STARTED;
        pthread_mutex_unlock(&pChn->lock);
        if (!chn_ok) {
            pthread_mutex_unlock(&pChn->queueLock);
            return ERR_VENC_NOT_STARTED;
        }
    }

    {
        StreamBufferInfo st;
        st = pChn->astStreamQueue[pChn->u32QueueHead];
        pChn->u32QueueHead = (pChn->u32QueueHead + 1) % VENC_STREAM_QUEUE_SIZE;
        pChn->u32QueueCount--;
        pthread_cond_signal(&pChn->queueNotFull);
        pthread_mutex_unlock(&pChn->queueLock);
        *pstStream = st;
    }

    if (pstStream->bEndOfStream) {
        pthread_mutex_lock(&pChn->lock);
        {
            MppPacket *pPacket = (MppPacket *)pstStream->ulPrivate;
            if (pChn->bUsed && pPacket) {
                venc_ctx_return_output_stream(pChn->pOldCtx, PACKET_GetBaseData(pPacket));
                PACKET_Free(pPacket);
            }
        }
        pstStream->ulPrivate = 0;
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_EOS;
    }

    return ERR_VENC_OK;
}

S32 VENC_ReleaseStream(S32 s32ChnId, const StreamBufferInfo *pstStream) {
    if (!pstStream)
        return ERR_VENC_NULL_PTR;
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_INVALID_CHN;
    }

    /* Retrieve packet pointer from ulPrivate */
    MppPacket *pPacket = (MppPacket *)pstStream->ulPrivate;
    if (pPacket) {
        MppData *pSrcData = PACKET_GetBaseData(pPacket);
        venc_ctx_return_output_stream(pChn->pOldCtx, pSrcData);
        PACKET_Free(pPacket);
    }

    pthread_mutex_unlock(&pChn->lock);
    return ERR_VENC_OK;
}

S32 VENC_QueryStatus(S32 s32ChnId, VencChnStatus *pstStatus) {
    if (!pstStatus)
        return ERR_VENC_NULL_PTR;
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_INVALID_CHN;
    }

    memset(pstStatus, 0, sizeof(*pstStatus));
    pstStatus->u32Width = pChn->stAttr.u32Width;
    pstStatus->u32Height = pChn->stAttr.u32Height;

    pthread_mutex_unlock(&pChn->lock);
    return ERR_VENC_OK;
}

S32 VENC_Flush(S32 s32ChnId) {
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOT_STARTED;
    }

    S32 ret = venc_ctx_flush(pChn->pOldCtx);

    pthread_mutex_unlock(&pChn->lock);
    return (ret == MPP_OK) ? ERR_VENC_OK : ret;
}

S32 VENC_Reset(S32 s32ChnId) {
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOT_STARTED;
    }

    S32 ret = venc_ctx_reset(pChn->pOldCtx);

    pthread_mutex_unlock(&pChn->lock);
    return (ret == MPP_OK) ? ERR_VENC_OK : ret;
}

S32 VENC_SetParam(S32 s32ChnId, MppVencCmd cmd, void *pPara) {
    if (!pPara)
        return ERR_VENC_NULL_PTR;
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOT_STARTED;
    }

    S32 ret = venc_ctx_set_param(pChn->pOldCtx, cmd, pPara);

    pthread_mutex_unlock(&pChn->lock);
    return (ret == MPP_OK) ? ERR_VENC_OK : ret;
}
