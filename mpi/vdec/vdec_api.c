/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vdec_api.c
 * @Date      :    2026-04-18
 * @Brief     :    VDEC new public API implementation.
 *                 Wraps the old context-based vdec.c (vdec_ctx_*) into
 *                 channel-ID-based API defined in vdec_api.h / vdec_type.h.
 *                 Each channel owns one MppVdecCtx internally.
 *                 Thread-safe via per-channel mutex.
 *------------------------------------------------------------------------------
 */

#define ENABLE_DEBUG 0

#include "vdec/vdec_api.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "vdec.h"
#include "log.h"
#include "sys/sys_api.h"
#include "sys/mpp_shm.h"
#include "sys/vb_api.h"

#define MODULE_TAG "mpp_vdec_api"

#define VDEC_MAX_EXT_BUF 64
#define VDEC_DEFAULT_BUF_CNT 12                     /* default output buffer count */
#define VDEC_DEPTH_DEFAULT VDEC_DEFAULT_BUF_CNT / 2 /* depth = half of buf cnt */
#define VDEC_DEPTH_MAX VDEC_DEPTH_DEFAULT

/* ======================== Internal Channel Context ======================== */

typedef enum _VdecChnState {
    VDEC_CHN_STATE_IDLE = 0,
    VDEC_CHN_STATE_STARTED,
} VdecChnState;

/**
 * @brief Per-buffer tracking for DMABUF_EXTERNAL mode.
 *        Maps VB buffer handle ↔ dma-buf fd ↔ decoder slot.
 */
typedef struct _VdecExtBuf {
    UL ulVbBuff;       /**< VB buffer handle from VB_GetBuffer */
    S32 s32DmaBufFd;   /**< dma-buf fd from VB_GetDmaBufFd */
    VOID *pVirAddr;    /**< virtual address from VB_GetVirAddr */
    BOOL bInDecoder;   /**< buffer currently queued in decoder */
    MppData *pSrcData; /**< MppData from decoder output (for return) */
} VdecExtBuf;

/* Depth queue entry: holds a VB buffer handle + frame metadata */
typedef struct _VdecDepthEntry {
    UL ulBufferId;              /**< VB buffer handle (ref-counted) */
    VideoFrameInfo stFrameInfo; /**< snapshot of frame info at decode time */
} VdecDepthEntry;

typedef struct _VdecChnCtx {
    BOOL bUsed;
    VdecChnState eState;
    VdecChnAttr stAttr;
    MppVdecCtx *pOldCtx;
    pthread_mutex_t lock;
    S32 s32ChnId; /**< channel ID for SYS_SendFrame */

    /* VB pool for DMABUF_EXTERNAL mode */
    UL ulPoolId;       /**< VB pool handle, 0 = not created */
    U32 u32ExtBufCnt;  /**< number of external buffers */
    U32 u32PoolBufSize; /**< per-buffer size of the current pool (bytes) */
    U32 u32PoolWidth;  /**< width the current pool was allocated for */
    U32 u32PoolHeight; /**< height the current pool was allocated for */
    VdecExtBuf stExtBuf[VDEC_MAX_EXT_BUF];

    /* depth queue (ring buffer, protected by depthLock) */
    VdecDepthEntry astDepth[VDEC_DEPTH_MAX];
    U32 u32DepthHead;  /**< read index  */
    U32 u32DepthTail;  /**< write index */
    U32 u32DepthCount; /**< current entries */
    pthread_mutex_t depthLock;
    pthread_cond_t depthNotEmpty;

    /* output task thread: request decoded frames, SYS_SendFrame + depth queue */
    pthread_t taskTid;
    BOOL bTaskRun;

    /* recycle thread: picks up free VB buffers and re-queues to decoder */
    pthread_t recycleTid;
    BOOL bRecycleRun;
    /* Resolution-change stop-the-recycle-thread handshake.
     *
     * The output thread (which rebuilds the external pool on a resolution
     * change) and the recycle thread (which calls VB_GetBuffer on that
     * pool) must never touch the pool at the same time.  Plain/volatile
     * flags give no acquire/release ordering on weakly-ordered targets
     * (RISC-V/ARM), so we use an explicit mutex + condition variable to
     * get both visibility and a true stop-and-wait rendezvous:
     *   - bPoolReconfig: set by the output thread while it is destroying
     *     and recreating the pool.
     *   - bRecycleIdle: set by the recycle thread once it has parked on
     *     poolCond and is provably NOT inside VB_GetBuffer.
     * Both flags are only ever read/written while poolLock is held, and
     * the recycle thread blocks on poolCond (it does NOT busy-spin) for
     * the whole rebuild, so the steady-state VB_GetBuffer path — which
     * runs with poolLock released — sees zero added contention. */
    pthread_mutex_t poolLock;
    pthread_cond_t poolCond;
    BOOL bPoolReconfig; /**< TRUE while the ext pool is being rebuilt (poolLock) */
    BOOL bRecycleIdle;  /**< recycle thread parked, not touching pool (poolLock) */

    /* stream input thread: receives bound stream via SYS_RecvStream */
    pthread_t streamInputTid;
    BOOL bStreamInputRun;
    BOOL bBound; /**< TRUE if SYS_RecvStream got data */
} VdecChnCtx;

/* ======================== Global State ======================== */

static BOOL g_bVdecInited = MPP_FALSE;
static VdecChnCtx g_stChn[VDEC_MAX_CHN];
static pthread_mutex_t g_stGlobalLock = PTHREAD_MUTEX_INITIALIZER;

/* ======================== Helpers ======================== */

static inline BOOL vdec_chn_valid(S32 s32ChnId) {
    return (s32ChnId >= 0 && s32ChnId < VDEC_MAX_CHN);
}

static void vdec_attr_to_old_para(const VdecChnAttr *pstAttr, MppVdecCtx *pOldCtx) {
    pOldCtx->eCodecType = CODEC_V4L2_LINLONV5V7;

    switch (pstAttr->eCodecType) {
        case MPP_STREAM_CODEC_H263:
            pOldCtx->stVdecPara.eCodingType = CODING_H263;
            break;
        case MPP_STREAM_CODEC_H264:
            pOldCtx->stVdecPara.eCodingType = CODING_H264;
            break;
        case MPP_STREAM_CODEC_H264_MVC:
            pOldCtx->stVdecPara.eCodingType = CODING_H264_MVC;
            break;
        case MPP_STREAM_CODEC_H264_NO_SC:
            pOldCtx->stVdecPara.eCodingType = CODING_H264_NO_SC;
            break;
        case MPP_STREAM_CODEC_H265:
            pOldCtx->stVdecPara.eCodingType = CODING_H265;
            break;
        case MPP_STREAM_CODEC_MJPEG:
            pOldCtx->stVdecPara.eCodingType = CODING_MJPEG;
            break;
        case MPP_STREAM_CODEC_JPEG:
            pOldCtx->stVdecPara.eCodingType = CODING_JPEG;
            break;
        case MPP_STREAM_CODEC_VP8:
            pOldCtx->stVdecPara.eCodingType = CODING_VP8;
            break;
        case MPP_STREAM_CODEC_VP9:
            pOldCtx->stVdecPara.eCodingType = CODING_VP9;
            break;
        case MPP_STREAM_CODEC_AV1:
            pOldCtx->stVdecPara.eCodingType = CODING_AV1;
            break;
        case MPP_STREAM_CODEC_AVS:
            pOldCtx->stVdecPara.eCodingType = CODING_AVS;
            break;
        case MPP_STREAM_CODEC_AVS2:
            pOldCtx->stVdecPara.eCodingType = CODING_AVS2;
            break;
        case MPP_STREAM_CODEC_MPEG1:
            pOldCtx->stVdecPara.eCodingType = CODING_MPEG1;
            break;
        case MPP_STREAM_CODEC_MPEG2:
            pOldCtx->stVdecPara.eCodingType = CODING_MPEG2;
            break;
        case MPP_STREAM_CODEC_MPEG4:
            pOldCtx->stVdecPara.eCodingType = CODING_MPEG4;
            break;
        case MPP_STREAM_CODEC_RV:
            pOldCtx->stVdecPara.eCodingType = CODING_RV;
            break;
        case MPP_STREAM_CODEC_VC1:
            pOldCtx->stVdecPara.eCodingType = CODING_VC1;
            break;
        case MPP_STREAM_CODEC_VC1_ANNEX_L:
            pOldCtx->stVdecPara.eCodingType = CODING_VC1_ANNEX_L;
            break;
        case MPP_STREAM_CODEC_FWHT:
            pOldCtx->stVdecPara.eCodingType = CODING_FWHT;
            break;
        default:
            pOldCtx->stVdecPara.eCodingType = CODING_H264;
            break;
    }

    pOldCtx->stVdecPara.nWidth = (S32)pstAttr->u32Width;
    pOldCtx->stVdecPara.nHeight = (S32)pstAttr->u32Height;
    pOldCtx->stVdecPara.eOutputPixelFormat = pstAttr->eOutputPixelFormat;
    pOldCtx->stVdecPara.bIsInterlaced = pstAttr->bIsInterlaced;
    pOldCtx->stVdecPara.bIsFrameReordering = pstAttr->bIsFrameReordering;
    pOldCtx->stVdecPara.nRotateDegree = (S32)pstAttr->u32RotateDegree;
    pOldCtx->stVdecPara.nHorizonScaleDownRatio = 1;
    pOldCtx->stVdecPara.nVerticalScaleDownRatio = 1;
    pOldCtx->stVdecPara.bDispErrorFrame = pstAttr->bDispErrorFrame;
    pOldCtx->stVdecPara.nHorizonScaleDownFrameWidth = (S32)pstAttr->stScale.u32Width;
    pOldCtx->stVdecPara.nVerticalScaleDownFrameHeight = (S32)pstAttr->stScale.u32Height;

    /* Always use DMABUF_EXTERNAL — internally managed VB pool */
    pOldCtx->stVdecPara.eFrameBufferType = MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL;
    pOldCtx->stVdecPara.nOutputBufferNum = VDEC_DEFAULT_BUF_CNT;
}

/**
 * Derive per-plane byte sizes for VideoFrameInfo when the codec plugin only
 * fills MppFrame pointers/fds (common for V4L2 / dma-buf paths).
 */
static void vdec_fill_plane_sizes(VideoFrameInfo *pstOut) {
    U32 w = pstOut->stVdecFrameInfo.stCommFrameInfo.u32Width;
    U32 h = pstOut->stVdecFrameInfo.stCommFrameInfo.u32Height;
    if (w == 0 || h == 0)
        return;

    U32 stride = pstOut->stVFrame.u32PlaneStride[0];
    if (stride == 0)
        stride = (w + 63u) & ~63u;

    MppPixelFormat fmt = pstOut->stVdecFrameInfo.stCommFrameInfo.ePixelFormat;
    U32 n = pstOut->stVFrame.u32PlaneNum;
    if (n == 0)
        n = 1;
    if (n > FRAME_MAX_PLANE)
        n = FRAME_MAX_PLANE;

    switch (fmt) {
        case MPP_PIXEL_FORMAT_NV12:
        case MPP_PIXEL_FORMAT_NV21:
            if (n >= 2) {
                pstOut->stVFrame.u32PlaneSize[0] = stride * h;
                pstOut->stVFrame.u32PlaneSize[1] = stride * h / 2;
                pstOut->stVFrame.u32PlaneSizeValid[0] = pstOut->stVFrame.u32PlaneSize[0];
                pstOut->stVFrame.u32PlaneSizeValid[1] = pstOut->stVFrame.u32PlaneSize[1];
            } else {
                pstOut->stVFrame.u32PlaneSize[0] = stride * h * 3 / 2;
                pstOut->stVFrame.u32PlaneSizeValid[0] = pstOut->stVFrame.u32PlaneSize[0];
            }
            break;
        case MPP_PIXEL_FORMAT_I420:
        case MPP_PIXEL_FORMAT_YV12:
            if (n >= 3) {
                U32 chroma_w = (w + 1) / 2;
                U32 chroma_h = (h + 1) / 2;
                pstOut->stVFrame.u32PlaneSize[0] = stride * h;
                pstOut->stVFrame.u32PlaneSize[1] = chroma_w * chroma_h;
                pstOut->stVFrame.u32PlaneSize[2] = chroma_w * chroma_h;
                pstOut->stVFrame.u32PlaneSizeValid[0] = pstOut->stVFrame.u32PlaneSize[0];
                pstOut->stVFrame.u32PlaneSizeValid[1] = pstOut->stVFrame.u32PlaneSize[1];
                pstOut->stVFrame.u32PlaneSizeValid[2] = pstOut->stVFrame.u32PlaneSize[2];
            }
            break;
        default:
            break;
    }
}

static void vdec_fill_frame_info(MppFrame *pFrame, VideoFrameInfo *pstOut, UL ulPoolId, UL ulBufferId) {
    S32 i;

    memset(pstOut, 0, sizeof(*pstOut));
    pstOut->eFrameType = FRAME_TYPE_VDEC;
    pstOut->eModId = MPP_ID_VDEC;
    pstOut->ulPoolId = ulPoolId;
    pstOut->ulBufferId = ulBufferId;

    pstOut->stVdecFrameInfo.stCommFrameInfo.u32Width = (U32)FRAME_GetWidth(pFrame);
    pstOut->stVdecFrameInfo.stCommFrameInfo.u32Height = (U32)FRAME_GetHeight(pFrame);
    pstOut->stVdecFrameInfo.stCommFrameInfo.ePixelFormat = (MppPixelFormat)FRAME_GetPixelFormat(pFrame);

    pstOut->stVFrame.u64PTS = (U64)FRAME_GetPts(pFrame);

    S32 nPlanes = FRAME_GetDataUsedNum(pFrame);
    if (nPlanes <= 0)
        nPlanes = 1;
    if (nPlanes > FRAME_MAX_PLANE)
        nPlanes = FRAME_MAX_PLANE;
    pstOut->stVFrame.u32PlaneNum = (U32)nPlanes;

    for (i = 0; i < nPlanes; i++) {
        pstOut->stVFrame.u32Fd[i] = (UL)FRAME_GetFD(pFrame, i);
        pstOut->stVFrame.ulPlaneVirAddr[i] = (UL)FRAME_GetDataPointer(pFrame, i);
    }

    pstOut->stVFrame.u32PlaneStride[0] = (U32)FRAME_GetLineStride(pFrame);

    MppFrameEos eos = FRAME_GetEos(pFrame);
    pstOut->stVdecFrameInfo.bEndOfStream = (eos != FRAME_NO_EOS) ? MPP_TRUE : MPP_FALSE;

    vdec_fill_plane_sizes(pstOut);
}

/* ======================== VB Pool Helpers (DMABUF_EXTERNAL) ======================== */

/**
 * @brief  Calculate buffer size.
 */
static U32 vdec_calc_default_buf_size(U32 u32Width, U32 u32Height, MppPixelFormat ePixelFormat, U32 u32Align) {
    VideoFrameInfo stTmp = {0};
    U32 u32TotalSize = 0;

    stTmp.stVdecFrameInfo.stCommFrameInfo.u32Width = u32Width;
    stTmp.stVdecFrameInfo.stCommFrameInfo.u32Height = u32Height;
    stTmp.stVdecFrameInfo.stCommFrameInfo.ePixelFormat = ePixelFormat;
    stTmp.stVdecFrameInfo.stCommFrameInfo.u32Align = u32Align;
    u32TotalSize = VB_GetPicBufferSize(&stTmp);

    return u32TotalSize;
}

/**
 * @brief  Create VB pool and queue all external dma-buf fds to decoder.
 *         Called from VDEC_EnableChn (initial) and on resolution change
 *         (rebuild).  The buffer size is computed from the supplied
 *         width/height so it can grow when the stream resolution differs
 *         from the channel's initial attributes.
 */
static S32 vdec_create_ext_pool_size(VdecChnCtx *pChn, U32 u32Width, U32 u32Height) {
    U32 bufCnt = VDEC_DEFAULT_BUF_CNT;
    U32 bufSize = 0;

    bufSize = vdec_calc_default_buf_size(u32Width, u32Height, pChn->stAttr.eOutputPixelFormat, pChn->stAttr.u32Align);
    if (bufSize == 0) {
        error("cannot determine buffer size for DMABUF_EXTERNAL");
        return ERR_VDEC_NOMEM;
    }

    /* Create VB pool */
    VbPoolCfg stPoolCfg;
    memset(&stPoolCfg, 0, sizeof(stPoolCfg));
    stPoolCfg.u32BufSize = bufSize;
    stPoolCfg.u32BufCnt = bufCnt;
    stPoolCfg.eModId = MPP_ID_VDEC;
    stPoolCfg.eRemapMode = VBUF_REMAP_MODE_NOCACHE;

    UL ulPool = VB_CreatePool(&stPoolCfg);
    if (ulPool == 0) {
        error("VB_CreatePool failed (size=%u cnt=%u)", bufSize, bufCnt);
        return ERR_VDEC_NOMEM;
    }

    pChn->ulPoolId = ulPool;
    pChn->u32ExtBufCnt = bufCnt;
    pChn->u32PoolBufSize = bufSize;
    pChn->u32PoolWidth = u32Width;
    pChn->u32PoolHeight = u32Height;
    memset(pChn->stExtBuf, 0, sizeof(pChn->stExtBuf));

    /* Get each buffer, extract dma-buf fd, queue to decoder */
    for (U32 i = 0; i < bufCnt; i++) {
        UL ulBuff = VB_GetBuffer(ulPool, 0);
        if (ulBuff == 0) {
            error("VB_GetBuffer failed for buf %u", i);
            goto fail;
        }

        S32 fd = -1;
        if (VB_GetDmaBufFd(ulBuff, &fd) != 0 || fd < 0) {
            error("VB_GetDmaBufFd failed for buf %u", i);
            VB_ReleaseBuffer(ulBuff);
            goto fail;
        }

        VOID *pVirAddr = NULL;
        VB_GetVirAddr(ulBuff, &pVirAddr);

        pChn->stExtBuf[i].ulVbBuff = ulBuff;
        pChn->stExtBuf[i].s32DmaBufFd = fd;
        pChn->stExtBuf[i].pVirAddr = pVirAddr;
        pChn->stExtBuf[i].bInDecoder = MPP_FALSE;

        /* Build MppFrame with external fd and queue to decoder */
        MppFrame *pFrame = FRAME_Create();
        if (!pFrame) {
            error("FRAME_Create failed for buf %u", i);
            VB_ReleaseBuffer(ulBuff);
            goto fail;
        }

        FRAME_SetFD(pFrame, fd, 0);
        if (pVirAddr) {
            FRAME_SetDataPointer(pFrame, 0, (U8 *)pVirAddr);
        }
        FRAME_SetID(pFrame, (S32)i);
        FRAME_SetBufferType(pFrame, MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL);

        MppData *src_data = FRAME_GetBaseData(pFrame);
        S32 ret = vdec_ctx_queue_output_buffer(pChn->pOldCtx, src_data);
        if (ret != MPP_OK) {
            error("vdec_ctx_queue_output_buffer failed for buf %u, ret=%d", i, ret);
            FRAME_Destory(pFrame);
            VB_ReleaseBuffer(ulBuff);
            goto fail;
        }

        pChn->stExtBuf[i].bInDecoder = MPP_TRUE;
        /* Frame wrapper ownership transferred to decoder, do not free here */
    }

    info("DMABUF_EXTERNAL pool created: pool=%lu, cnt=%u, size=%u (%ux%u)", ulPool, bufCnt, bufSize, u32Width,
        u32Height);
    return ERR_VDEC_OK;

fail:
    /* Release already-allocated buffers */
    for (U32 j = 0; j < bufCnt; j++) {
        if (pChn->stExtBuf[j].ulVbBuff) {
            VB_ReleaseBuffer(pChn->stExtBuf[j].ulVbBuff);
            pChn->stExtBuf[j].ulVbBuff = 0;
        }
    }
    VB_DestroyPool(ulPool);
    pChn->ulPoolId = 0;
    pChn->u32ExtBufCnt = 0;
    pChn->u32PoolBufSize = 0;
    pChn->u32PoolWidth = 0;
    pChn->u32PoolHeight = 0;
    return ERR_VDEC_NOMEM;
}

/**
 * @brief  Create VB pool using the channel's initial attributes.
 *         Called from VDEC_EnableChn.
 */
static S32 vdec_create_ext_pool(VdecChnCtx *pChn) {
    return vdec_create_ext_pool_size(pChn, pChn->stAttr.u32Width, pChn->stAttr.u32Height);
}

/**
 * @brief  Destroy VB pool and release all external buffers.
 */
static void vdec_destroy_ext_pool(VdecChnCtx *pChn) {
    if (pChn->ulPoolId == 0)
        return;

    for (U32 i = 0; i < pChn->u32ExtBufCnt; i++) {
        if (pChn->stExtBuf[i].ulVbBuff) {
            VB_ReleaseBuffer(pChn->stExtBuf[i].ulVbBuff);
            pChn->stExtBuf[i].ulVbBuff = 0;
        }
    }

    VB_DestroyPool(pChn->ulPoolId);
    pChn->ulPoolId = 0;
    pChn->u32ExtBufCnt = 0;
    pChn->u32PoolBufSize = 0;
    pChn->u32PoolWidth = 0;
    pChn->u32PoolHeight = 0;
    memset(pChn->stExtBuf, 0, sizeof(pChn->stExtBuf));
}

/**
 * @brief  Find VdecExtBuf entry by dma-buf fd.
 * @return index in stExtBuf[], or -1 if not found.
 */
static S32 vdec_find_ext_buf_by_fd(VdecChnCtx *pChn, S32 s32Fd) {
    for (U32 i = 0; i < pChn->u32ExtBufCnt; i++) {
        if (pChn->stExtBuf[i].s32DmaBufFd == s32Fd)
            return (S32)i;
    }
    return -1;
}

/**
 * @brief  Re-queue all external dma-buf buffers to the decoder.
 *         Called after a resolution change event causes the V4L2 output port
 *         to be reallocated.  The old QBUF entries are lost after
 *         streamoff/streamon, so we must re-queue every buffer.
 */
static S32 vdec_requeue_ext_buffers(VdecChnCtx *pChn) {
    S32 queued = 0;

    for (U32 i = 0; i < pChn->u32ExtBufCnt; i++) {
        if (pChn->stExtBuf[i].ulVbBuff == 0)
            continue;

        /* Return any held MppData to decoder first */
        if (pChn->stExtBuf[i].pSrcData) {
            vdec_ctx_return_output_frame(pChn->pOldCtx, pChn->stExtBuf[i].pSrcData);
            pChn->stExtBuf[i].pSrcData = NULL;
        }

        MppFrame *pFrame = FRAME_Create();
        if (!pFrame) {
            error("requeue: FRAME_Create failed for buf %u", i);
            continue;
        }

        FRAME_SetFD(pFrame, pChn->stExtBuf[i].s32DmaBufFd, 0);
        if (pChn->stExtBuf[i].pVirAddr) {
            FRAME_SetDataPointer(pFrame, 0, (U8 *)pChn->stExtBuf[i].pVirAddr);
        }
        FRAME_SetID(pFrame, (S32)i);
        FRAME_SetBufferType(pFrame, MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL);

        MppData *queue_data = FRAME_GetBaseData(pFrame);
        S32 ret = vdec_ctx_queue_output_buffer(pChn->pOldCtx, queue_data);
        if (ret == MPP_OK) {
            pChn->stExtBuf[i].bInDecoder = MPP_TRUE;
            queued++;
        } else {
            error("requeue: queue buf %u failed, ret=%d", i, ret);
            FRAME_Destory(pFrame);
        }
    }

    info("re-queued %d/%u ext buffers after resolution change", queued, pChn->u32ExtBufCnt);
    return (queued > 0) ? ERR_VDEC_OK : ERR_VDEC_NOMEM;
}

/**
 * @brief  Release every VB reference held by the depth queue and reset it.
 *
 *         LOCK PRECONDITION: the caller MUST already hold pChn->depthLock.
 *         This helper does not acquire depthLock itself precisely so that the
 *         locking is explicit at every call site, and so that no caller can
 *         accidentally take depthLock in the wrong order relative to poolLock.
 *
 *         GLOBAL LOCK ORDER (project-wide): poolLock -> depthLock.  The only
 *         site that holds both at once (vdec_handle_resolution_change) takes
 *         poolLock first and then calls this helper under depthLock; the
 *         VDEC_DisableChn caller holds depthLock alone.  By funnelling both
 *         drains through this single helper — whose contract documents the
 *         required lock — any future edit that needs to release the queue is
 *         forced to reason about the lock it must already hold, rather than
 *         open-coding a third release loop that might nest the locks the wrong
 *         way.
 *
 *         Each queued entry holds one VB ref (added via VB_RefAdd in
 *         vdec_output_task) on a pool buffer.  EOS markers carry
 *         ulBufferId == 0 and hold no ref, so they are simply dropped.
 */
static void vdec_drain_depth_queue_locked(VdecChnCtx *pChn) {
    while (pChn->u32DepthCount > 0) {
        VdecDepthEntry *pEntry = &pChn->astDepth[pChn->u32DepthHead];
        if (pEntry->ulBufferId != 0)
            VB_ReleaseBuffer(pEntry->ulBufferId);
        pChn->u32DepthHead = (pChn->u32DepthHead + 1) % VDEC_DEPTH_MAX;
        pChn->u32DepthCount--;
    }
    pChn->u32DepthHead = 0;
    pChn->u32DepthTail = 0;
    pChn->u32DepthCount = 0;
}

/**
 * @brief  Handle a decoder source/resolution-change event.
 *
 *         The channel is created with the caller-supplied initial
 *         resolution (e.g. 640x480), but the real stream resolution is
 *         only known after the decoder parses the sequence header and
 *         raises V4L2_EVENT_SOURCE_CHANGE.  When the real resolution is
 *         larger than what the external dma-buf pool was sized for, the
 *         old buffers are too small and VIDIOC_QBUF rejects them with
 *         EFAULT ("Bad address"), leaving the decoder with no capture
 *         buffers and stalling the whole pipeline.
 *
 *         We query the decoder for the new geometry and, if the existing
 *         buffers cannot hold a frame of the new size, destroy the pool
 *         and rebuild it at the new resolution.  Otherwise the old
 *         buffers are simply re-queued.
 *
 *         This runs on the output-task thread.  For the common case the
 *         source-change event is delivered before the first decoded frame is
 *         produced, so the depth queue is empty.  However an in-stream dynamic
 *         resolution change (DRC) can arrive after frames have already been
 *         pushed into the depth queue, and those entries hold a VB ref on the
 *         OLD pool.  Before destroying the old pool we therefore drain the
 *         depth queue and release each held ref, otherwise VDEC_GetFrame /
 *         VDEC_ReleaseFrame would later operate on a dangling VB handle whose
 *         pool has been torn down.  The al-layer plugin has already streamed
 *         the capture port off (releasing the driver's dma-buf references)
 *         before returning MPP_RESOLUTION_CHANGED, and we hold poolLock across
 *         the whole destroy/rebuild so the recycle thread cannot touch the
 *         pool mid-flight.
 */
static S32 vdec_handle_resolution_change(VdecChnCtx *pChn) {
    MppVdecPara *pPara = NULL;
    U32 u32NewW = 0;
    U32 u32NewH = 0;
    U32 u32NeedSize = 0;

    /* vdec_ctx_get_param() -> al_dec_getparam() returns an *internal* pointer
     * (*para = context->pVdecPara), i.e. it aliases the decoder context's own
     * MppVdecPara member.  It is NOT a heap allocation handed to the caller, so
     * there is nothing to free here; the lifetime is owned by pOldCtx. */
    if (vdec_ctx_get_param(pChn->pOldCtx, &pPara) == MPP_OK && pPara) {
        if (pPara->nWidth > 0 && pPara->nHeight > 0) {
            u32NewW = (U32)pPara->nWidth;
            u32NewH = (U32)pPara->nHeight;
        }
    }

    if (u32NewW == 0 || u32NewH == 0) {
        /* Decoder did not report a usable resolution; fall back to the
         * legacy behaviour of re-queuing the existing buffers. */
        return vdec_requeue_ext_buffers(pChn);
    }

    u32NeedSize = vdec_calc_default_buf_size(u32NewW, u32NewH, pChn->stAttr.eOutputPixelFormat, pChn->stAttr.u32Align);

    if (u32NeedSize > 0 && u32NeedSize <= pChn->u32PoolBufSize) {
        /* Existing buffers are large enough; just re-queue them. */
        info("resolution change %ux%u fits current pool (need=%u have=%u), re-queue", u32NewW, u32NewH, u32NeedSize,
            pChn->u32PoolBufSize);
        return vdec_requeue_ext_buffers(pChn);
    }

    info("resolution change %ux%u needs larger buffers (need=%u have=%u), rebuilding ext pool", u32NewW, u32NewH,
        u32NeedSize, pChn->u32PoolBufSize);

    /* Pause the recycle thread so it does not race with us on the pool
     * handle / buffer table while we destroy and recreate the pool.
     * Raise bPoolReconfig under poolLock, then block on poolCond until
     * the recycle thread acknowledges (bRecycleIdle) that it has parked
     * and is provably not inside VB_GetBuffer.
     *
     * We hold poolLock across the ENTIRE destroy+rebuild sequence.  The
     * recycle thread stays blocked on poolCond for the whole window
     * (it only wakes when bPoolReconfig is cleared at the very end), so
     * there is no point at which it can re-enter VB_GetBuffer against a
     * pool that is being torn down.  Keeping the lock held is free in
     * steady state: the recycle thread only ever contends for poolLock
     * during a reconfig, never on the hot path.
     *
     * Holding poolLock across the (potentially slow) VB_DestroyPool /
     * VB_GetBuffer / VIDIOC_QBUF calls does NOT delay channel teardown.
     * This function runs on the output task thread (taskTid), and
     * VDEC_DisableChn stops the channel in this order: it clears bTaskRun
     * and pthread_join()s taskTid FIRST, and only AFTER that join returns
     * does it take poolLock to clear bRecycleRun.  So DisableChn never
     * contends for poolLock while a reconfig is in flight — by the time it
     * touches poolLock, this function has already finished and the output
     * thread has exited.  The reconfig duration is bounded by the join on
     * taskTid regardless of where the lock is held, so moving destroy/rebuild
     * outside the lock would not shorten teardown latency; it would only
     * reopen the VB_GetBuffer-vs-pool-teardown race the lock exists to close.
     * The only consumer that ever blocks on poolLock during a reconfig is the
     * recycle thread, which is exactly the thread we are deliberately parking. */
    pthread_mutex_lock(&pChn->poolLock);
    pChn->bPoolReconfig = MPP_TRUE;
    /* Wait until the recycle thread has parked (bRecycleIdle).  Also bail out
     * if the recycle thread is shutting down (bRecycleRun cleared by
     * VDEC_DisableChn): a stopped recycle thread can no longer touch the pool,
     * so it is safe to proceed, and waiting on bRecycleIdle that will never be
     * set again would deadlock the join in VDEC_DisableChn.
     *
     * This wait is precisely what closes the "VB_GetBuffer in flight while the
     * pool is being destroyed" window.  Walk the worst case: the recycle
     * thread has just passed its own under-lock bPoolReconfig check (FALSE at
     * the time), released poolLock, and is now blocked inside
     * VB_GetBuffer(ulPoolId, 100) on the lock-free hot path.  We then set
     * bPoolReconfig and acquire poolLock (uncontended, since recycle dropped
     * it before the blocking call).  At this instant bRecycleIdle is still
     * FALSE, so we do NOT fall through — we block here on poolCond.
     * bRecycleIdle can transition to TRUE only inside the recycle thread's
     * under-lock block, which it can reach only AFTER its current VB_GetBuffer
     * call returns and it loops back to re-acquire poolLock.  Therefore, by
     * the time we are released from this wait (bRecycleIdle == TRUE), the
     * recycle thread is provably parked in pthread_cond_wait() and is NOT
     * inside VB_GetBuffer, so the destroy/rebuild below cannot race a live
     * VB_GetBuffer against the pool.  Guarding the VB_GetBuffer call itself
     * with poolLock (as one might suggest) would force this thread to wait out
     * the full 100ms timeout and, worse, hold poolLock across a blocking call
     * on the recycle hot path — a regression we already measured (steady-state
     * recycling stalls at 25fps), which is exactly why the 100ms VB_GetBuffer
     * deliberately stays lock-free. */
    while (!pChn->bRecycleIdle && pChn->bRecycleRun) {
        pthread_cond_wait(&pChn->poolCond, &pChn->poolLock);
    }

    /*
     * It is safe to free the external dma-buf pool here without an
     * explicit decoder flush: the plugin's handleResolutionChange (run
     * on the al layer before it returned MPP_RESOLUTION_CHANGED to us)
     * already issued VIDIOC_STREAMOFF on the capture port.  Per the V4L2
     * spec, STREAMOFF removes every buffer from both the incoming and
     * outgoing driver queues and drops the driver's references to their
     * dma-bufs, so by the time we reach this point the kernel holds no
     * reference to any buffer in the pool.  (We must NOT call
     * vdec_ctx_flush here — handleFlush does streamoff+streamon and
     * re-arms the capture port, which would fight with the destroy /
     * rebuild we are about to perform and leaves the decoder producing
     * no frames.)
     */

    /* Drain the depth queue before tearing the old pool down.
     *
     * For an initial sequence-header source-change the queue is empty, but an
     * in-stream dynamic resolution change can arrive after frames have been
     * pushed here.  Each queued entry holds one VB ref (added via VB_RefAdd in
     * vdec_output_task) on a buffer of the OLD pool.  If we destroyed the pool
     * without releasing these refs, the handles in the queue would dangle and a
     * later VDEC_GetFrame/VDEC_ReleaseFrame would release an already-freed VB
     * buffer.  Release each held ref and reset the queue (mirroring the flush
     * in VDEC_DisableChn).  EOS markers carry ulBufferId == 0 and hold no ref,
     * so they are simply dropped.  We are on the output-task thread (the only
     * producer), so no new entries can be appended while we drain; we still
     * take depthLock to stay consistent with VDEC_GetFrame, which pops from
     * another thread.  poolLock is held here and depthLock is acquired nested
     * inside it; this is the only site that nests the two, so no lock-order
     * cycle is possible.
     *
     * GLOBAL LOCK ORDER (must hold project-wide): poolLock -> depthLock.  This
     * is the ONLY site that ever holds both at once, and it acquires them in
     * that order.  VDEC_GetFrame takes depthLock alone and never reaches for
     * poolLock while holding it; the recycle/output tasks take poolLock and
     * depthLock only in separate, non-overlapping critical sections elsewhere.
     * Any future code that needs both locks MUST take poolLock first. */
    pthread_mutex_lock(&pChn->depthLock);
    vdec_drain_depth_queue_locked(pChn);
    pthread_mutex_unlock(&pChn->depthLock);

    /* Release any decoder-held MppData wrappers before tearing the pool
     * down so they are not leaked.  We cannot re-queue them with
     * vdec_ctx_return_output_frame here (the pool is about to be destroyed
     * and rebuilt), so destroy the MppFrame wrapper directly.  For
     * DMABUF_EXTERNAL frames FRAME_Destory only frees the malloc'd MppFrame
     * struct and NULLs the plane pointers — it never frees the underlying
     * dma-buf, which is owned by the ext pool — so this is safe.
     *
     * Use the framework's FRAME_GetFrame() accessor to obtain the MppFrame*
     * from the MppData* rather than casting the pointer ourselves: the
     * MppData/MppFrame memory layout lives in utils/frame.c and is not
     * visible here, so an ad-hoc cast would silently break if that layout
     * ever changed.  FRAME_Destory() is NULL-safe (it returns immediately on
     * a NULL argument), so even in the impossible case that FRAME_GetFrame()
     * returned NULL for a non-NULL pSrcData, this would not misbehave. */
    for (U32 i = 0; i < pChn->u32ExtBufCnt; i++) {
        if (pChn->stExtBuf[i].pSrcData) {
            FRAME_Destory(FRAME_GetFrame(pChn->stExtBuf[i].pSrcData));
            pChn->stExtBuf[i].pSrcData = NULL;
        }
        pChn->stExtBuf[i].bInDecoder = MPP_FALSE;
    }

    vdec_destroy_ext_pool(pChn);

    S32 ret = vdec_create_ext_pool_size(pChn, u32NewW, u32NewH);

    if (ret != ERR_VDEC_OK) {
        /*
         * Pool rebuild failed and vdec_destroy_ext_pool has already zeroed
         * ulPoolId, so there is no valid pool for the recycle thread to
         * operate on.  We do NOT forcibly stop that thread here; instead we
         * clear bRecycleRun (and bPoolReconfig) under poolLock and wake it.
         * The recycle thread then leaves its loop on its own the next time it
         * re-checks its condition (it waits while bPoolReconfig && bRecycleRun
         * and breaks out as soon as bRecycleRun == FALSE), so it can never
         * reach VB_GetBuffer against the destroyed pool.  bRecycleRun and
         * bPoolReconfig are part of the poolLock-protected handshake state,
         * which is why both are cleared here under poolLock.
         *
         * We do NOT touch bTaskRun here.  bTaskRun is owned by pChn->lock
         * (VDEC_EnableChn/VDEC_DisableChn read and write it only under that
         * lock), and we hold poolLock — not pChn->lock — on this path, so
         * writing bTaskRun here would be a cross-lock data race with a
         * concurrent VDEC_DisableChn.  Instead we propagate the failure to the
         * caller (vdec_output_task) via the return value; it breaks out of its
         * loop on a non-OK return, which stops the output thread without ever
         * writing bTaskRun outside pChn->lock.
         *
         * We only unlock poolLock here; we deliberately do NOT destroy
         * poolLock/poolCond (or the ext pool) on this path.  Both threads
         * exit on their own (the output thread by breaking on our return, the
         * recycle thread by observing bRecycleRun == FALSE), and the single
         * owner of teardown is VDEC_DisableChn, which joins both threads and
         * destroys the sync primitives exactly once.  vdec_destroy_ext_pool is
         * idempotent (it no-ops when ulPoolId == 0, which is already the case
         * here), so the later DisableChn call is safe even though the pool was
         * already torn down above.
         *
         * Note we leave bRecycleIdle as-is (it may still be TRUE here): there
         * is no destroy-during-use race on poolLock because VDEC_DisableChn
         * pthread_join()s recycleTid BEFORE it calls
         * pthread_mutex_destroy(&poolLock).  The join is a full barrier — the
         * recycle thread has provably returned (after observing bRecycleRun ==
         * FALSE and breaking out of its loop) before poolLock is destroyed, so
         * resetting bRecycleIdle here would have no observable effect. */
        error("failed to rebuild ext pool at %ux%u, ret=%d - stopping channel", u32NewW, u32NewH, ret);
        pChn->bRecycleRun = MPP_FALSE;
        pChn->bPoolReconfig = MPP_FALSE;
        pthread_cond_broadcast(&pChn->poolCond);
        pthread_mutex_unlock(&pChn->poolLock);
        return ret;
    }

    /* Resume the recycle thread now that the new pool is fully in place. */
    pChn->bPoolReconfig = MPP_FALSE;
    pthread_cond_broadcast(&pChn->poolCond);
    pthread_mutex_unlock(&pChn->poolLock);

    return ERR_VDEC_OK;
}

/* ======================== Recycle / Output Task Threads ======================== */

/**
 * @brief Recycle thread.
 *
 * Waits for VB buffers whose refcount has dropped to 0 (returned to
 * the pool by all consumers).  When VB_GetBuffer succeeds, the buffer
 * is free — we re-queue it to the decoder so it can be filled again.
 * The VB_GetBuffer call gives us ref=1, which represents "decoder owns
 * this buffer".
 */
static void *vdec_recycle_task(void *arg) {
    VdecChnCtx *pChn = (VdecChnCtx *)arg;

    info("vdec recycle task started: chn %d pool=%lu", pChn->s32ChnId, pChn->ulPoolId);

    while (pChn->bRecycleRun) {
        /* While the output task is rebuilding the external pool (resolution
         * change), the pool handle and buffer table are in flux.  Stay out
         * of the way: under poolLock, advertise that we are idle and block
         * on poolCond until the rebuild completes.  Blocking (rather than
         * spinning) means we are provably NOT inside VB_GetBuffer while the
         * output thread tears the pool down.  The lock is released the
         * instant we leave this block, so the steady-state path below runs
         * lock-free. */
        pthread_mutex_lock(&pChn->poolLock);
        if (pChn->bPoolReconfig) {
            pChn->bRecycleIdle = MPP_TRUE;
            pthread_cond_broadcast(&pChn->poolCond);
            while (pChn->bPoolReconfig && pChn->bRecycleRun) {
                pthread_cond_wait(&pChn->poolCond, &pChn->poolLock);
            }
            /* Leave bRecycleIdle = TRUE when we are exiting because the channel
             * is being torn down (bRecycleRun cleared).  An output thread that
             * is still blocked in vdec_handle_resolution_change waits on
             * bRecycleIdle; if we cleared it here and then exited the thread,
             * that wait could never be satisfied.  Only clear it when we are
             * resuming normal recycling (rebuild finished, bRecycleRun set). */
            if (pChn->bRecycleRun)
                pChn->bRecycleIdle = MPP_FALSE;
        }
        if (!pChn->bRecycleRun) {
            pthread_mutex_unlock(&pChn->poolLock);
            break;
        }
        pthread_mutex_unlock(&pChn->poolLock);

        /* Block up to 100ms waiting for a free buffer in the pool.
         *
         * There is no window in which this can be reached with a destroyed
         * pool (ulPoolId == 0).  The pool-rebuild failure path clears
         * bRecycleRun, bTaskRun and bPoolReconfig together *while holding
         * poolLock* and only then broadcasts poolCond.  A recycle thread
         * parked in pthread_cond_wait() above can only return after it
         * re-acquires poolLock, i.e. after the failure path has unlocked, by
         * which point bRecycleRun is already FALSE.  The `if (!bRecycleRun)`
         * check sits between the cond-wait loop and this VB_GetBuffer call and
         * runs under the same lock, so the thread always breaks out before
         * reaching here.  On the success path ulPoolId is repopulated by
         * vdec_create_ext_pool_size() before bPoolReconfig is cleared, so the
         * thread never resumes onto a zeroed ulPoolId either. */
        UL ulBuf = VB_GetBuffer(pChn->ulPoolId, 100);
        if (ulBuf == 0)
            continue; /* timeout or shutting down */

        /* Find which ext buf slot this VB handle belongs to */
        S32 idx = -1;
        for (U32 i = 0; i < pChn->u32ExtBufCnt; i++) {
            if (pChn->stExtBuf[i].ulVbBuff == ulBuf) {
                idx = (S32)i;
                break;
            }
        }
        if (idx < 0) {
            error("recycle: unknown VB handle %lu", ulBuf);
            VB_ReleaseBuffer(ulBuf);
            continue;
        }

        /* Return the MppData to decoder if still held.
         * NOTE: vdec_ctx_return_output_frame internally calls queueBuffer
         * (VIDIOC_QBUF), so the buffer is already back in the decoder
         * after this call.  Mark bInDecoder = TRUE immediately. */
        if (pChn->stExtBuf[idx].pSrcData) {
            vdec_ctx_return_output_frame(pChn->pOldCtx, pChn->stExtBuf[idx].pSrcData);
            pChn->stExtBuf[idx].pSrcData = NULL;
            pChn->stExtBuf[idx].bInDecoder = MPP_TRUE;
        }

        /*
         * If the buffer is already queued in the decoder (e.g.
         * vdec_ctx_return_output_frame above, or the error-frame
         * path in the output task, already did VIDIOC_QBUF internally),
         * we must NOT queue it again — the V4L2 driver rejects a
         * duplicate QBUF with EINVAL.  Just skip re-queuing.
         */
        if (pChn->stExtBuf[idx].bInDecoder) {
            debug("recycle: buf %d already in decoder, skip re-queue", idx);
            continue;
        }

        /* Build MppFrame and re-queue to decoder */
        MppFrame *pFrame = FRAME_Create();
        if (!pFrame) {
            error("recycle: FRAME_Create failed for buf %d", idx);
            VB_ReleaseBuffer(ulBuf);
            continue;
        }

        FRAME_SetFD(pFrame, pChn->stExtBuf[idx].s32DmaBufFd, 0);
        if (pChn->stExtBuf[idx].pVirAddr) {
            FRAME_SetDataPointer(pFrame, 0, (U8 *)pChn->stExtBuf[idx].pVirAddr);
        }
        FRAME_SetID(pFrame, idx);
        FRAME_SetBufferType(pFrame, MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL);

        MppData *queue_data = FRAME_GetBaseData(pFrame);
        S32 ret = vdec_ctx_queue_output_buffer(pChn->pOldCtx, queue_data);
        if (ret == MPP_OK) {
            pChn->stExtBuf[idx].bInDecoder = MPP_TRUE;
        } else {
            error("recycle: re-queue buf %d failed, ret=%d", idx, ret);
            VB_ReleaseBuffer(ulBuf);
        }
    }

    info("vdec recycle task exiting: chn %d", pChn->s32ChnId);
    return NULL;
}

/**
 * @brief Output task thread.
 *
 * Continuously requests decoded frames from the decoder, then:
 *   1. SYS_SendFrame to all bound sinks (internally VB_RefAdd per sink).
 *   2. If depth > 0, VB_RefAdd and push into the depth queue.
 *   3. Release the "decoder base ref" via VB_ReleaseBuffer.
 *
 * The buffer is NOT directly re-queued to the decoder here.  When ALL
 * consumers (SYS sinks + depth queue user) have called VB_ReleaseBuffer,
 * the VB refcount drops to 0, the buffer returns to the pool, and the
 * recycle thread picks it up and re-queues it to the decoder.
 */
static void *vdec_output_task(void *arg) {
    VdecChnCtx *pChn = (VdecChnCtx *)arg;
    S32 s32ChnId = pChn->s32ChnId;

    MppNode stSrcNode;
    stSrcNode.eModId = MPP_ID_VDEC;
    stSrcNode.s32DevId = 0;
    stSrcNode.s32ChnId = s32ChnId;

    info("vdec output task started: chn %d", s32ChnId);

    while (pChn->bTaskRun) {
        MppData *src_data = NULL;
        S32 ret = vdec_ctx_request_output_frame_2(pChn->pOldCtx, &src_data, 100);
        if (ret == MPP_CODER_EOS) {
            /* Push an EOS entry into depth queue */
            VideoFrameInfo stEosFrame;
            memset(&stEosFrame, 0, sizeof(stEosFrame));
            stEosFrame.eFrameType = FRAME_TYPE_VDEC;
            stEosFrame.eModId = MPP_ID_VDEC;
            stEosFrame.stVdecFrameInfo.bEndOfStream = MPP_TRUE;

            pthread_mutex_lock(&pChn->depthLock);
            if (pChn->u32DepthCount < VDEC_DEPTH_MAX) {
                VdecDepthEntry *pNew = &pChn->astDepth[pChn->u32DepthTail];
                pNew->ulBufferId = 0;
                memcpy(&pNew->stFrameInfo, &stEosFrame, sizeof(VideoFrameInfo));
                pChn->u32DepthTail = (pChn->u32DepthTail + 1) % VDEC_DEPTH_MAX;
                pChn->u32DepthCount++;
                pthread_cond_signal(&pChn->depthNotEmpty);
            }
            pthread_mutex_unlock(&pChn->depthLock);
            continue;
        }
        if (ret == MPP_RESOLUTION_CHANGED) {
            /*
             * V4L2 driver triggered a source-change event (common on the
             * first frame for MJPEG / H.264 etc.).  handleResolutionChange
             * inside the plugin did streamoff → realloc → streamon on the
             * capture port, which invalidated all previously queued
             * DMABUF_EXTERNAL buffers.  Re-queue them now so the decoder
             * can continue producing output.
             */
            info("output task: resolution changed on chn %d, re-queuing ext buffers", s32ChnId);
            /* The MppData returned by the plugin is not a valid frame;
             * destroy it to avoid leaking the MppFrame wrapper. */
            if (src_data) {
                MppFrame *pTmp = FRAME_GetFrame(src_data);
                if (pTmp)
                    FRAME_Destory(pTmp);
            }
            if (vdec_handle_resolution_change(pChn) != ERR_VDEC_OK) {
                /* Pool rebuild failed irrecoverably; the handler has already
                 * stopped the recycle thread (bRecycleRun cleared under
                 * poolLock).  Just break out so this output thread returns.
                 *
                 * We deliberately do NOT write bTaskRun here.  bTaskRun is
                 * owned by pChn->lock and is only meaningful as a "please
                 * stop" request to THIS thread; since we are already exiting,
                 * setting it is pointless.  More importantly, writing it here
                 * (outside pChn->lock) would race with VDEC_DisableChn, which
                 * reads/writes bTaskRun under pChn->lock, and on a weakly
                 * ordered target (RISC-V/ARM) the store might not even be
                 * visible there.  VDEC_DisableChn's pthread_join(taskTid) is a
                 * full barrier: once this thread returns, the join completes
                 * and the value of bTaskRun is irrelevant. */
                break;
            }
            continue;
        }
        if (ret == MPP_ERROR_FRAME || ret == MPP_CODER_NULL_DATA) {
            /*
             * The buffer has been dequeued from V4L2 but carries an error
             * flag or zero payload.  We must return it to the decoder and
             * re-queue it, otherwise the buffer is leaked and the decoder
             * eventually runs out of output buffers.
             *
             * al_dec_return_output_frame already does clearBytesUsed +
             * setExternalDmaBuf + queueBuffer internally for
             * DMABUF_EXTERNAL buffers, so we must NOT call
             * vdec_ctx_queue_output_buffer again — that would attempt a
             * second VIDIOC_QBUF on the same buffer index, which the
             * V4L2 driver rejects with EINVAL.
             */
            if (src_data) {
                MppFrame *pErrFrame = FRAME_GetFrame(src_data);
                if (pErrFrame) {
                    S32 errFd = FRAME_GetFD(pErrFrame, 0);
                    S32 errIdx = vdec_find_ext_buf_by_fd(pChn, errFd);
                    if (errIdx >= 0) {
                        pChn->stExtBuf[errIdx].bInDecoder = MPP_FALSE;
                        pChn->stExtBuf[errIdx].pSrcData = NULL;
                        /*
                         * Return the buffer to the decoder plugin.
                         * This internally re-queues the V4L2 buffer
                         * (VIDIOC_QBUF) with the correct external
                         * dma-buf fd, so the decoder can reuse it.
                         */
                        vdec_ctx_return_output_frame(pChn->pOldCtx, src_data);
                        pChn->stExtBuf[errIdx].bInDecoder = MPP_TRUE;
                    } else {
                        FRAME_Destory(pErrFrame);
                    }
                }
            }
            continue;
        }
        /*
         * After capture EOS, poll may still report POLLIN while DQBUF fails;
         * plugin returns MPP_CODER_NO_DATA — not an error, avoid log spam.
         */
        if (ret == MPP_CODER_NO_DATA)
            continue;
        if (ret != MPP_OK || !src_data) {
            error("output task: unexpected ret=%d", ret);
            continue; /* timeout or transient error */
        }

        MppFrame *pFrame = FRAME_GetFrame(src_data);
        if (!pFrame)
            continue;

        /* Match decoded frame fd to our VB buffer */
        S32 frameFd = FRAME_GetFD(pFrame, 0);
        S32 idx = vdec_find_ext_buf_by_fd(pChn, frameFd);
        if (idx < 0) {
            error("output task: decoded frame fd=%d not found in ext buf table", frameFd);
            continue;
        }

        pChn->stExtBuf[idx].bInDecoder = MPP_FALSE;
        pChn->stExtBuf[idx].pSrcData = src_data;

        UL ulBuf = pChn->stExtBuf[idx].ulVbBuff;

        /* Build frame info */
        VideoFrameInfo stFrame;
        vdec_fill_frame_info(pFrame, &stFrame, pChn->ulPoolId, ulBuf);
        VB_SetBufferPTS(ulBuf, stFrame.stVFrame.u64PTS);
        stFrame.u32Idx = (U32)FRAME_GetID(pFrame);

        /*
         * At this point ref=1 (the "decoder base ref" from the initial
         * VB_GetBuffer or the recycle thread's VB_GetBuffer).
         *
         * SYS_SendFrame internally does VB_RefAdd for each bound sink,
         * and each sink will eventually VB_ReleaseBuffer.
         *
         * We VB_RefAdd once for the depth queue consumer.
         *
         * Finally we VB_ReleaseBuffer to drop the base ref.  When all
         * consumers are done, refcount reaches 0, buffer goes back to
         * the pool, and the recycle thread re-queues it to the decoder.
         */

        /* --- 1. SYS_SendFrame: internally VB_RefAdd per bound sink --- */
        SYS_SendFrame(&stSrcNode, ulBuf);

        /* --- 2. Push into depth queue --- */
        if (ulBuf != 0) {
            /* add a ref for the depth queue consumer */
            VB_RefAdd(ulBuf);

            pthread_mutex_lock(&pChn->depthLock);

            if (pChn->u32DepthCount >= VDEC_DEPTH_MAX) {
                /* queue full — drop oldest, release its ref */
                VdecDepthEntry *pOld = &pChn->astDepth[pChn->u32DepthHead];
                if (pOld->ulBufferId != 0)
                    VB_ReleaseBuffer(pOld->ulBufferId);
                pChn->u32DepthHead = (pChn->u32DepthHead + 1) % VDEC_DEPTH_MAX;
                pChn->u32DepthCount--;
            }

            VdecDepthEntry *pNew = &pChn->astDepth[pChn->u32DepthTail];
            pNew->ulBufferId = ulBuf;
            memcpy(&pNew->stFrameInfo, &stFrame, sizeof(VideoFrameInfo));
            pChn->u32DepthTail = (pChn->u32DepthTail + 1) % VDEC_DEPTH_MAX;
            pChn->u32DepthCount++;

            pthread_cond_signal(&pChn->depthNotEmpty);
            pthread_mutex_unlock(&pChn->depthLock);
        }

        /* --- 3. Release the decoder base ref --- */
        VB_ReleaseBuffer(ulBuf);
    }

    info("vdec output task exiting: chn %d", s32ChnId);
    return NULL;
}

/* ---------- stream input thread: receive bound stream data ---------- */
static void *vdec_stream_input_task(void *arg) {
    VdecChnCtx *pChn = (VdecChnCtx *)arg;
    S32 s32ChnId = pChn->s32ChnId;
    S32 ret = 0;

    MppNode stSink = {
        .eModId = MPP_ID_VDEC,
        .s32DevId = 0,
        .s32ChnId = s32ChnId,
    };

    U8 *pRecvBuf = (U8 *)malloc(MPP_STREAM_MAX_PAYLOAD);
    if (!pRecvBuf) {
        error("stream input task: malloc %d failed, chn %d", MPP_STREAM_MAX_PAYLOAD, s32ChnId);
        return NULL;
    }

    info("stream input task started: chn %d", s32ChnId);

    while (pChn->bStreamInputRun) {
        StreamBufferInfo stStream;
        memset(&stStream, 0, sizeof(stStream));
        stStream.pu8Addr = pRecvBuf;
        stStream.u32Size = MPP_STREAM_MAX_PAYLOAD;

        ret = SYS_RecvStream(&stSink, &stStream, 100);
        if (ret != 0) {
            /* timeout or no bind — just retry */
            if (SYS_ERR_NOT_FOUND == ret) {
                pChn->bBound = MPP_FALSE;
                usleep(20000);  // Sleep 20ms before retrying to avoid busy loop when no stream is bound
            }
            continue;
        }

        /* Mark channel as bound on first successful receive */
        if (!pChn->bBound) {
            pChn->bBound = MPP_TRUE;
            info("stream input task: chn %d bound, stream input active", s32ChnId);
        }

        /* Build MppPacket and feed to decoder */
        MppPacket *pkt = PACKET_Create();
        if (!pkt) {
            error("stream input task: PACKET_Create failed, chn %d", s32ChnId);
            continue;
        }

        PACKET_SetDataPointer(pkt, (U8 *)stStream.pu8Addr);
        PACKET_SetLength(pkt, (S32)stStream.u32Size);
        PACKET_SetPts(pkt, (S64)stStream.u64PTS);
        PACKET_SetEos(pkt, stStream.bEndOfStream);

        MppData *sink_data = PACKET_GetBaseData(pkt);

        pthread_mutex_lock(&pChn->lock);
        if (pChn->eState == VDEC_CHN_STATE_STARTED) {
            ret = vdec_ctx_decode(pChn->pOldCtx, sink_data);
            if (ret != MPP_OK && ret != 0 && ret != MPP_CODER_EOS)
                error("stream input task: decode failed %d, chn %d", ret, s32ChnId);
        }
        pthread_mutex_unlock(&pChn->lock);

        PACKET_Destory(pkt);

        if (stStream.bEndOfStream) {
            info("stream input task: EOS received, chn %d", s32ChnId);
            break;
        }
    }

    free(pRecvBuf);
    info("stream input task exiting: chn %d", s32ChnId);
    return NULL;
}

/* ======================== API Implementation ======================== */

S32 VDEC_Init(VOID) {
    pthread_mutex_lock(&g_stGlobalLock);
    if (g_bVdecInited) {
        pthread_mutex_unlock(&g_stGlobalLock);
        return ERR_VDEC_ALREADY_INIT;
    }

    memset(g_stChn, 0, sizeof(g_stChn));
    for (S32 i = 0; i < VDEC_MAX_CHN; i++) {
        pthread_mutex_init(&g_stChn[i].lock, NULL);
    }

    g_bVdecInited = MPP_TRUE;
    pthread_mutex_unlock(&g_stGlobalLock);
    return ERR_VDEC_OK;
}

S32 VDEC_Exit(VOID) {
    pthread_mutex_lock(&g_stGlobalLock);
    if (!g_bVdecInited) {
        pthread_mutex_unlock(&g_stGlobalLock);
        return ERR_VDEC_NOT_INIT;
    }

    for (S32 i = 0; i < VDEC_MAX_CHN; i++) {
        if (g_stChn[i].bUsed) {
            error("channel %d still in use, destroy it first", i);
            pthread_mutex_unlock(&g_stGlobalLock);
            return ERR_VDEC_BUSY;
        }
    }

    for (S32 i = 0; i < VDEC_MAX_CHN; i++) {
        pthread_mutex_destroy(&g_stChn[i].lock);
    }
    memset(g_stChn, 0, sizeof(g_stChn));

    g_bVdecInited = MPP_FALSE;
    pthread_mutex_unlock(&g_stGlobalLock);
    return ERR_VDEC_OK;
}

S32 VDEC_CreateChn(S32 s32ChnId, const VdecChnAttr *pstAttr) {
    if (!pstAttr)
        return ERR_VDEC_NULL_PTR;
    if (!vdec_chn_valid(s32ChnId))
        return ERR_VDEC_INVALID_CHN;

    pthread_mutex_lock(&g_stGlobalLock);
    if (!g_bVdecInited) {
        pthread_mutex_unlock(&g_stGlobalLock);
        return ERR_VDEC_NOT_INIT;
    }
    pthread_mutex_unlock(&g_stGlobalLock);

    VdecChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (pChn->bUsed) {
        error("channel %d already created", s32ChnId);
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_BUSY;
    }

    MppVdecCtx *pOldCtx = vdec_ctx_create();
    if (!pOldCtx) {
        error("failed to create vdec context for chn %d", s32ChnId);
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_NOMEM;
    }

    vdec_attr_to_old_para(pstAttr, pOldCtx);

    pChn->pOldCtx = pOldCtx;
    pChn->stAttr = *pstAttr;
    pChn->s32ChnId = s32ChnId;
    pChn->eState = VDEC_CHN_STATE_IDLE;
    pChn->bUsed = MPP_TRUE;

    pthread_mutex_unlock(&pChn->lock);
    return ERR_VDEC_OK;
}

S32 VDEC_DestroyChn(S32 s32ChnId) {
    if (!vdec_chn_valid(s32ChnId))
        return ERR_VDEC_INVALID_CHN;

    VdecChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_INVALID_CHN;
    }
    if (pChn->eState == VDEC_CHN_STATE_STARTED) {
        error("channel %d still started, stop it first", s32ChnId);
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_BUSY;
    }

    if (pChn->pOldCtx) {
        vdec_ctx_destroy(pChn->pOldCtx);
        pChn->pOldCtx = NULL;
    }

    /* Safety: destroy VB pool if still alive */
    vdec_destroy_ext_pool(pChn);

    pChn->bUsed = MPP_FALSE;
    pthread_mutex_unlock(&pChn->lock);
    return ERR_VDEC_OK;
}

S32 VDEC_EnableChn(S32 s32ChnId) {
    if (!vdec_chn_valid(s32ChnId))
        return ERR_VDEC_INVALID_CHN;

    VdecChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_INVALID_CHN;
    }
    if (pChn->eState == VDEC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_ALREADY_INIT;
    }

    S32 ret = vdec_ctx_init(pChn->pOldCtx);
    if (ret != MPP_OK) {
        error("vdec_ctx_init failed for chn %d, ret=%d", s32ChnId, ret);
        /* Early failure: none of the depth/pool sync primitives have been
         * initialised yet, so we must NOT goto err_cleanup (which destroys
         * them).  Return the real error code directly. */
        pthread_mutex_unlock(&pChn->lock);
        return ret;
    }

    /* Create VB pool and queue buffers to decoder */
    ret = vdec_create_ext_pool(pChn);
    if (ret != ERR_VDEC_OK) {
        error("vdec_create_ext_pool failed for chn %d, ret=%d", s32ChnId, ret);
        /* Same as above: sync primitives are still uninitialised here, so
         * return directly instead of going through err_cleanup. */
        pthread_mutex_unlock(&pChn->lock);
        return ret;
    }

    /* Initialize depth queue.
     *
     * NOTE: every sync primitive destroyed at err_cleanup (depthLock,
     * depthNotEmpty, poolLock, poolCond) is initialised in this block, which
     * runs BEFORE the first `goto err_cleanup`.  All `goto err_cleanup` sites
     * are in the thread-creation section below, i.e. strictly after this
     * point, so err_cleanup can never destroy an uninitialised primitive.
     * The only failures that precede this block (vdec_ctx_init,
     * vdec_create_ext_pool) return directly and never reach err_cleanup.
     * If you add a new failure path before this block, return directly too;
     * do NOT goto err_cleanup. */
    pChn->u32DepthHead = 0;
    pChn->u32DepthTail = 0;
    pChn->u32DepthCount = 0;
    pthread_mutex_init(&pChn->depthLock, NULL);
    pthread_cond_init(&pChn->depthNotEmpty, NULL);

    /* Resolution-change handshake primitives */
    pChn->bPoolReconfig = MPP_FALSE;
    pChn->bRecycleIdle = MPP_FALSE;
    pthread_mutex_init(&pChn->poolLock, NULL);
    pthread_cond_init(&pChn->poolCond, NULL);

    pChn->eState = VDEC_CHN_STATE_STARTED;

    /* Start recycle thread */
    pChn->bRecycleRun = MPP_TRUE;
    ret = pthread_create(&pChn->recycleTid, NULL, vdec_recycle_task, pChn);
    if (ret != 0) {
        error("recycle thread create failed for chn %d: %s", s32ChnId, strerror(ret));
        pChn->bRecycleRun = MPP_FALSE;
        goto err_cleanup;
    }

    /* Start output task thread */
    pChn->bTaskRun = MPP_TRUE;
    ret = pthread_create(&pChn->taskTid, NULL, vdec_output_task, pChn);
    if (ret != 0) {
        error("output task thread create failed for chn %d: %s", s32ChnId, strerror(ret));
        pChn->bTaskRun = MPP_FALSE;
        /* stop recycle thread */
        pChn->bRecycleRun = MPP_FALSE;
        pthread_mutex_unlock(&pChn->lock);
        pthread_join(pChn->recycleTid, NULL);
        pthread_mutex_lock(&pChn->lock);
        goto err_cleanup;
    }

    /* Start stream input thread (receives bound stream via SYS_RecvStream) */
    pChn->bBound = MPP_FALSE;
    pChn->bStreamInputRun = MPP_TRUE;
    ret = pthread_create(&pChn->streamInputTid, NULL, vdec_stream_input_task, pChn);
    if (ret != 0) {
        error("stream input thread create failed for chn %d: %s", s32ChnId, strerror(ret));
        pChn->bStreamInputRun = MPP_FALSE;
        /* stop output + recycle threads */
        pChn->bTaskRun = MPP_FALSE;
        pChn->bRecycleRun = MPP_FALSE;
        pthread_mutex_unlock(&pChn->lock);
        pthread_join(pChn->taskTid, NULL);
        pthread_join(pChn->recycleTid, NULL);
        pthread_mutex_lock(&pChn->lock);
        goto err_cleanup;
    }

    pthread_mutex_unlock(&pChn->lock);
    info("VDEC_EnableChn: chn %d enabled", s32ChnId);
    return ERR_VDEC_OK;

err_cleanup:
    /* No thread join happens at this label, and none is needed: every `goto
     * err_cleanup` site joins the threads IT started, before jumping here.
     *   - recycle-create failure: no thread is running yet (recycleTid was
     *     never created), so there is nothing to join.
     *   - output-create failure: recycleTid IS running, so that site clears
     *     bRecycleRun and pthread_join(recycleTid) inline before the goto.
     *   - stream-create failure: both taskTid and recycleTid are running, so
     *     that site joins BOTH inline before the goto.
     * Thus recycleTid can never leak: it is either un-created or already
     * joined by the time we arrive here.  Adding a join at this label would be
     * a double-join on an already-joined (and possibly reused) pthread_t,
     * which is undefined behaviour — so we deliberately do NOT join here. */
    pChn->eState = VDEC_CHN_STATE_IDLE;
    vdec_destroy_ext_pool(pChn);
    pthread_mutex_destroy(&pChn->depthLock);
    pthread_cond_destroy(&pChn->depthNotEmpty);
    /* poolLock is NOT held here: it is only ever locked transiently by the
     * recycle/output tasks and vdec_handle_resolution_change, all of which
     * unlock before returning, and no thread is running on any error path
     * that reaches this label (threads are joined before goto err_cleanup,
     * or were never started).  Destroying an unlocked mutex is well-defined.
     * This mirrors VDEC_DisableChn, which also unlocks poolLock before
     * destroying it. */
    pthread_mutex_destroy(&pChn->poolLock);
    pthread_cond_destroy(&pChn->poolCond);
    pthread_mutex_unlock(&pChn->lock);
    /* All three `goto err_cleanup` sites are pthread_create() failures, where
     * `ret` holds a libc errno (e.g. EAGAIN), NOT an ERR_VDEC_* code.  We
     * deliberately do NOT `return ret` here: that would leak a raw errno
     * through an API whose contract is to return ERR_VDEC_* values.  Thread
     * creation failure is a resource-exhaustion condition, so we map it to the
     * closest VDEC code, ERR_VDEC_NOMEM.  (This differs from
     * vdec_handle_resolution_change, whose `ret` is the genuine ERR_VDEC_*
     * return of vdec_create_ext_pool_size and is correctly propagated there.) */
    return ERR_VDEC_NOMEM;
}

S32 VDEC_DisableChn(S32 s32ChnId) {
    if (!vdec_chn_valid(s32ChnId))
        return ERR_VDEC_INVALID_CHN;

    VdecChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_INVALID_CHN;
    }
    if (pChn->eState != VDEC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_NOT_STARTED;
    }

    /* Stop stream input thread first */
    pChn->bStreamInputRun = MPP_FALSE;
    pthread_mutex_unlock(&pChn->lock);
    pthread_join(pChn->streamInputTid, NULL);
    pthread_mutex_lock(&pChn->lock);

    /* Signal output task thread to stop.
     *
     * Ordering is load-bearing: we clear bTaskRun and pthread_join(taskTid)
     * HERE, before we touch poolLock further below.  vdec_handle_resolution_change
     * runs on taskTid and holds poolLock across the whole VB_DestroyPool +
     * pool-rebuild + QBUF chain; by joining taskTid first we guarantee any
     * in-flight reconfig has fully completed and released poolLock before this
     * function ever competes for it.  DisableChn therefore never blocks on
     * poolLock waiting for a reconfig, and the teardown latency is bounded by
     * the taskTid join alone, independent of how long a reconfig holds the
     * lock.  Do NOT reorder the poolLock acquisition below ahead of this join. */
    pChn->bTaskRun = MPP_FALSE;
    pthread_mutex_unlock(&pChn->lock);
    pthread_join(pChn->taskTid, NULL);
    pthread_mutex_lock(&pChn->lock);

    /* Flush depth queue — release VB refs (depthLock held; see
     * vdec_drain_depth_queue_locked for the lock contract). */
    pthread_mutex_lock(&pChn->depthLock);
    vdec_drain_depth_queue_locked(pChn);
    pthread_mutex_unlock(&pChn->depthLock);
    pthread_mutex_destroy(&pChn->depthLock);
    pthread_cond_destroy(&pChn->depthNotEmpty);

    /* Flush decoder */
    vdec_ctx_flush(pChn->pOldCtx);

    /* Stop recycle thread.  Take poolLock so the flag store is ordered
     * and wake it in case it is parked on poolCond mid-reconfig.
     *
     * This is reached only AFTER pthread_join(taskTid) above, so the output
     * thread (the only one that runs vdec_handle_resolution_change, which holds
     * poolLock across a slow VB_DestroyPool/rebuild/QBUF chain) has already
     * exited and released poolLock.  This acquisition is therefore always
     * uncontended by a reconfig and cannot stall.
     *
     * Idempotency: if the channel is being torn down because a pool rebuild
     * failed, vdec_handle_resolution_change already cleared bRecycleRun under
     * poolLock (on taskTid, before that thread exited).  In that case the
     * store below is a harmless no-op and the broadcast wakes no one (the
     * recycle thread has already observed bRecycleRun == FALSE and left its
     * loop).  We still perform both unconditionally so this teardown path is
     * correct for the normal case too, where bRecycleRun is still TRUE here.
     * The recycle thread is only joined below, so poolCond is never destroyed
     * before that join — broadcasting to an already-exited thread is defined
     * and safe. */
    pthread_mutex_lock(&pChn->poolLock);
    pChn->bRecycleRun = MPP_FALSE;
    pthread_cond_broadcast(&pChn->poolCond);
    pthread_mutex_unlock(&pChn->poolLock);
    pthread_mutex_unlock(&pChn->lock);
    pthread_join(pChn->recycleTid, NULL);
    pthread_mutex_lock(&pChn->lock);

    pthread_mutex_destroy(&pChn->poolLock);
    pthread_cond_destroy(&pChn->poolCond);

    /* Destroy VB pool */
    vdec_destroy_ext_pool(pChn);

    pChn->eState = VDEC_CHN_STATE_IDLE;
    pthread_mutex_unlock(&pChn->lock);
    info("VDEC_DisableChn: chn %d disabled", s32ChnId);
    return ERR_VDEC_OK;
}

S32 VDEC_SendStream(S32 s32ChnId, const StreamBufferInfo *pstStream, U32 u32TimeoutMs) {
    if (!pstStream)
        return ERR_VDEC_NULL_PTR;
    if (!vdec_chn_valid(s32ChnId))
        return ERR_VDEC_INVALID_CHN;

    VdecChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VDEC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_NOT_STARTED;
    }

    if (pChn->bBound) {
        error("VDEC_SendStream: chn %d has active bind, reject manual send", s32ChnId);
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_BUSY;
    }

    /* Build MppPacket from StreamBufferInfo (zero-copy: just set pointer) */
    MppPacket *pkt = PACKET_Create();
    if (!pkt) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_NOMEM;
    }

    PACKET_SetDataPointer(pkt, (U8 *)pstStream->pu8Addr);
    PACKET_SetLength(pkt, (S32)pstStream->u32Size);
    PACKET_SetPts(pkt, (S64)pstStream->u64PTS);
    PACKET_SetEos(pkt, pstStream->bEndOfStream);

    MppData *sink_data = PACKET_GetBaseData(pkt);
    S32 ret = vdec_ctx_decode(pChn->pOldCtx, sink_data);

    /* Do NOT free the packet buffer — it belongs to the caller.
     * Only destroy the MppPacket wrapper. */
    PACKET_Destory(pkt);
    pthread_mutex_unlock(&pChn->lock);

    if (ret == MPP_CODER_EOS)
        return ERR_VDEC_EOS;
    if (ret != MPP_OK && ret != 0)
        return ret;
    return ERR_VDEC_OK;
}

S32 VDEC_GetFrame(S32 s32ChnId, VideoFrameInfo *pstFrameInfo, U32 u32TimeoutMs) {
    if (!pstFrameInfo)
        return ERR_VDEC_NULL_PTR;
    if (!vdec_chn_valid(s32ChnId))
        return ERR_VDEC_INVALID_CHN;

    VdecChnCtx *pChn = &g_stChn[s32ChnId];

    if (!pChn->bUsed || pChn->eState != VDEC_CHN_STATE_STARTED)
        return ERR_VDEC_NOT_STARTED;

    /* Pop from depth queue with optional timeout */
    pthread_mutex_lock(&pChn->depthLock);

    while (pChn->u32DepthCount == 0) {
        if (u32TimeoutMs == 0) {
            pthread_mutex_unlock(&pChn->depthLock);
            return ERR_VDEC_NO_FRAME;
        }
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += u32TimeoutMs / 1000;
        ts.tv_nsec += (u32TimeoutMs % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        S32 waitRet = pthread_cond_timedwait(&pChn->depthNotEmpty, &pChn->depthLock, &ts);
        if (waitRet != 0) {
            pthread_mutex_unlock(&pChn->depthLock);
            return ERR_VDEC_TIMEOUT;
        }
    }

    VdecDepthEntry *pEntry = &pChn->astDepth[pChn->u32DepthHead];
    memcpy(pstFrameInfo, &pEntry->stFrameInfo, sizeof(VideoFrameInfo));
    pChn->u32DepthHead = (pChn->u32DepthHead + 1) % VDEC_DEPTH_MAX;
    pChn->u32DepthCount--;

    pthread_mutex_unlock(&pChn->depthLock);

    /* Check for EOS marker */
    if (pstFrameInfo->ulBufferId == 0 && pstFrameInfo->stVdecFrameInfo.bEndOfStream)
        return ERR_VDEC_EOS;

    return ERR_VDEC_OK;
}

/**
 * @brief  Release a decoded frame back.
 *         Simply drops the VB ref. When refcount reaches 0, the buffer
 *         returns to the pool and the recycle thread re-queues it to
 *         the decoder.
 */
S32 VDEC_ReleaseFrame(S32 s32ChnId, UL ulVbBuff) {
    if (!vdec_chn_valid(s32ChnId))
        return ERR_VDEC_INVALID_CHN;
    if (!ulVbBuff)
        return ERR_VDEC_NULL_PTR;

    VB_ReleaseBuffer(ulVbBuff);
    return ERR_VDEC_OK;
}

S32 VDEC_QueryStatus(S32 s32ChnId, VdecChnStatus *pstStatus) {
    if (!pstStatus)
        return ERR_VDEC_NULL_PTR;
    if (!vdec_chn_valid(s32ChnId))
        return ERR_VDEC_INVALID_CHN;

    VdecChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_INVALID_CHN;
    }

    memset(pstStatus, 0, sizeof(*pstStatus));

    if (pChn->eState == VDEC_CHN_STATE_STARTED && pChn->pOldCtx) {
        MppVdecPara *pPara = NULL;
        S32 ret = vdec_ctx_get_param(pChn->pOldCtx, &pPara);
        if (ret == MPP_OK && pPara) {
            pstStatus->u32LeftStreamFrames = (U32)pPara->nInputQueueLeftNum;
            pstStatus->u32LeftDecodedFrames = (U32)pPara->nOutputQueueLeftNum;
            pstStatus->u32Width = (U32)pPara->nWidth;
            pstStatus->u32Height = (U32)pPara->nHeight;
        }
    }

    pthread_mutex_unlock(&pChn->lock);
    return ERR_VDEC_OK;
}

S32 VDEC_Flush(S32 s32ChnId) {
    if (!vdec_chn_valid(s32ChnId))
        return ERR_VDEC_INVALID_CHN;

    VdecChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VDEC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_NOT_STARTED;
    }

    S32 ret = vdec_ctx_flush(pChn->pOldCtx);
    pthread_mutex_unlock(&pChn->lock);
    return (ret == MPP_OK) ? ERR_VDEC_OK : ret;
}

S32 VDEC_Reset(S32 s32ChnId) {
    if (!vdec_chn_valid(s32ChnId))
        return ERR_VDEC_INVALID_CHN;

    VdecChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VDEC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_NOT_STARTED;
    }

    S32 ret = vdec_ctx_reset(pChn->pOldCtx);
    pthread_mutex_unlock(&pChn->lock);
    return (ret == MPP_OK) ? ERR_VDEC_OK : ret;
}
