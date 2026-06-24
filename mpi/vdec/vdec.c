/*
 *------------------------------------------------------------------------------
 * Copyright 2022-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vdec.c
 * @Brief     :    VDEC public API implementation (vdec_api.h / vdec_type.h).
 *                 Loads the codec plugin via the AL module loader, binds the
 *                 al_dec_* symbols per channel, and manages channel state,
 *                 the DMABUF_EXTERNAL VB pool and the worker threads.
 *                 Thread-safe via per-channel mutex.
 *------------------------------------------------------------------------------
 */

#define ENABLE_DEBUG 0

#include "vdec/vdec_api.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "al_interface_dec.h"
#include "log.h"
#include "module.h"
#include "sys/mpp_shm.h"
#include "sys/sys_api.h"
#include "sys/vb_api.h"

#define MODULE_TAG "mpp_vdec"

#define VDEC_MAX_EXT_BUF 64
#define VDEC_DEFAULT_BUF_CNT 12

/* ======================== Internal Channel Context ======================== */

typedef enum _VdecChnState {
    VDEC_CHN_STATE_IDLE = 0,
    VDEC_CHN_STATE_STARTED,
} VdecChnState;

/** al_dec_* entry points resolved from the codec plugin, per channel. */
typedef struct _VdecAlOps {
    ALBaseContext *(*create)(void);
    S32 (*init)(ALBaseContext *ctx, const VdecChnAttr *pstAttr, AlDecBufferRequirement *pstReq);
    S32 (*get_status)(ALBaseContext *ctx, VdecChnStatus *pstStatus);
    S32 (*decode)(ALBaseContext *ctx, const StreamBufferInfo *pstStream);
    S32 (*request_output_frame)(ALBaseContext *ctx, VideoFrameInfo *pstFrame, U32 u32TimeoutMs);
    S32 (*return_output_frame)(ALBaseContext *ctx, const VideoFrameInfo *pstFrame);
    S32 (*queue_output_buffer)(ALBaseContext *ctx, const VideoFrameInfo *pstFrame);
    S32 (*flush)(ALBaseContext *ctx);
    S32 (*reset)(ALBaseContext *ctx);
    void (*destory)(ALBaseContext *ctx);
} VdecAlOps;

/**
 * @brief Per-buffer tracking for DMABUF_EXTERNAL mode.
 *        Maps VB buffer handle ↔ dma-buf fd ↔ decoder slot.
 */
typedef struct _VdecExtBuf {
    UL ulVbBuff;       /**< VB buffer handle from VB_GetBuffer */
    S32 s32DmaBufFd;   /**< dma-buf fd from VB_GetDmaBufFd */
    VOID *pVirAddr;    /**< virtual address from VB_GetVirAddr */
    BOOL bInDecoder;   /**< buffer currently queued in decoder */
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
    pthread_mutex_t lock;
    S32 s32ChnId; /**< channel ID for SYS_SendFrame */

    /* codec plugin binding (valid while pModule != NULL) */
    MppModule *pModule;
    ALBaseContext *pAlCtx;
    VdecAlOps stOps;
    AlDecBufferRequirement stBufReq;

    /* VB pool for DMABUF_EXTERNAL mode */
    UL ulPoolId;       /**< VB pool handle, 0 = not created */
    U32 u32ExtBufCnt;  /**< number of external buffers */
    U32 u32PoolBufSize; /**< per-buffer size of the current pool (bytes) */
    U32 u32PoolWidth;  /**< width the current pool was allocated for */
    U32 u32PoolHeight; /**< height the current pool was allocated for */
    VdecExtBuf stExtBuf[VDEC_MAX_EXT_BUF];

    /* depth queue (ring buffer, protected by depthLock) */
    VdecDepthEntry *pstDepth;   /**< dynamically allocated depth ring buffer */
    U32 u32DepthMax;   /**< capacity of the depth ring buffer */
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
    pthread_mutex_t poolLock;
    pthread_cond_t poolCond;
    BOOL bPoolReconfig;
    BOOL bRecycleIdle;

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

/* ======================== Plugin Binding ======================== */

/**
 * @brief  Load the codec plugin, resolve the al_dec_* symbols into
 *         pChn->stOps, create the AL context and init the decoder with
 *         pChn->stAttr / pChn->stBufReq.  Called from VDEC_EnableChn.
 */
static S32 vdec_plugin_open(VdecChnCtx *pChn) {
    S32 ret = 0;
    void *handle;
    VdecAlOps *ops = &pChn->stOps;

    pChn->pModule = module_init(CODEC_V4L2_LINLONV5V7);
    if (!pChn->pModule) {
        error("module_init failed for codec type %d", (int)CODEC_V4L2_LINLONV5V7);
        return MPP_INIT_FAILED;
    }

    handle = module_get_so_path(pChn->pModule);
    if (!handle) {
        error("module handle is NULL after module_init");
        module_destory(pChn->pModule);
        pChn->pModule = NULL;
        return MPP_INIT_FAILED;
    }

    dlerror();

    /*
     * Stale-plugin guard: the AL ABI was changed in place (same al_dec_*
     * symbol names, new struct-based signatures). A plugin built against the
     * legacy interface still exports al_dec_getparam /
     * al_dec_request_output_frame_2, which no longer exist in the new
     * interface — loading such a plugin would crash at runtime with mismatched
     * struct layouts, so reject it here with a clear error instead.
     */
    if (dlsym(handle, "al_dec_getparam") || dlsym(handle, "al_dec_request_output_frame_2")) {
        error(
            "legacy codec plugin detected (exports al_dec_getparam / "
            "al_dec_request_output_frame_2); rebuild and redeploy the plugin "
            "to match this libmpp");
        module_destory(pChn->pModule);
        pChn->pModule = NULL;
        return MPP_INIT_FAILED;
    }

    ops->create = (ALBaseContext * (*)(void)) dlsym(handle, "al_dec_create");
    ops->init = (S32 (*)(ALBaseContext *, const VdecChnAttr *, AlDecBufferRequirement *))dlsym(handle, "al_dec_init");
    ops->get_status = (S32 (*)(ALBaseContext *, VdecChnStatus *))dlsym(handle, "al_dec_get_status");
    ops->decode = (S32 (*)(ALBaseContext *, const StreamBufferInfo *))dlsym(handle, "al_dec_decode");
    ops->request_output_frame =
        (S32 (*)(ALBaseContext *, VideoFrameInfo *, U32))dlsym(handle, "al_dec_request_output_frame");
    ops->return_output_frame =
        (S32 (*)(ALBaseContext *, const VideoFrameInfo *))dlsym(handle, "al_dec_return_output_frame");
    ops->queue_output_buffer =
        (S32 (*)(ALBaseContext *, const VideoFrameInfo *))dlsym(handle, "al_dec_queue_output_buffer");
    ops->flush = (S32 (*)(ALBaseContext *))dlsym(handle, "al_dec_flush");
    ops->reset = (S32 (*)(ALBaseContext *))dlsym(handle, "al_dec_reset");
    ops->destory = (void (*)(ALBaseContext *))dlsym(handle, "al_dec_destory");

    if (!ops->create || !ops->init || !ops->get_status || !ops->decode || !ops->request_output_frame ||
        !ops->return_output_frame || !ops->queue_output_buffer || !ops->flush || !ops->reset || !ops->destory) {
        error(
            "required decoder symbols missing from plugin (create/init/"
            "get_status/decode/request/return/queue/flush/reset/destory)");
        module_destory(pChn->pModule);
        pChn->pModule = NULL;
        return MPP_INIT_FAILED;
    }

    pChn->pAlCtx = ops->create();
    if (!pChn->pAlCtx) {
        error("al_dec_create returned NULL");
        module_destory(pChn->pModule);
        pChn->pModule = NULL;
        return MPP_INIT_FAILED;
    }

    ret = ops->init(pChn->pAlCtx, &pChn->stAttr, &pChn->stBufReq);
    debug("init VDEC Channel, ret = %d", ret);

    return ret;
}

/**
 * @brief  Destroy the AL decoder context and unload the plugin module.
 *         Safe to call when the plugin was never opened.
 */
static void vdec_plugin_close(VdecChnCtx *pChn) {
    if (pChn->pModule == NULL) {
        info("module not init!");
        return;
    }

    if (pChn->stOps.destory)
        pChn->stOps.destory(pChn->pAlCtx);
    debug("finish destory decoder");

    module_destory(pChn->pModule);
    debug("finish destory module");

    pChn->pModule = NULL;
    pChn->pAlCtx = NULL;
    memset(&pChn->stOps, 0, sizeof(pChn->stOps));
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
 *         Called from VDEC_EnableChn and resolution-change rebuild.
 */
static S32 vdec_create_ext_pool_size(VdecChnCtx *pChn, U32 u32Width, U32 u32Height) {
    U32 bufCnt = pChn->stBufReq.u32OutputBufNum;
    U32 bufSize = 0;

    /* the plugin returned the capture buffer count actually allocated by the
     * V4L2 driver — we must supply exactly that many external buffers */
    if (bufCnt == 0)
        bufCnt = VDEC_DEFAULT_BUF_CNT;
    if (bufCnt > VDEC_MAX_EXT_BUF)
        bufCnt = VDEC_MAX_EXT_BUF;

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

        /* From here on the buffer is tracked in stExtBuf[i] and released by
         * the fail loop — do not release it inline (double release). */
        pChn->stExtBuf[i].ulVbBuff = ulBuff;

        S32 fd = -1;
        if (VB_GetDmaBufFd(ulBuff, &fd) != 0 || fd < 0) {
            error("VB_GetDmaBufFd failed for buf %u", i);
            goto fail;
        }

        VOID *pVirAddr = NULL;
        VB_GetVirAddr(ulBuff, &pVirAddr);

        pChn->stExtBuf[i].s32DmaBufFd = fd;
        pChn->stExtBuf[i].pVirAddr = pVirAddr;
        pChn->stExtBuf[i].bInDecoder = MPP_FALSE;

        /* Queue the external dma-buf to the decoder (slot index = i) */
        VideoFrameInfo stQueueFrame;
        memset(&stQueueFrame, 0, sizeof(stQueueFrame));
        stQueueFrame.u32Idx = i;
        stQueueFrame.stVFrame.u32Fd[0] = (UL)fd;
        stQueueFrame.stVFrame.ulPlaneVirAddr[0] = (UL)pVirAddr;

        S32 ret = pChn->stOps.queue_output_buffer(pChn->pAlCtx, &stQueueFrame);
        if (ret != MPP_OK) {
            error("al_dec_queue_output_buffer failed for buf %u, ret=%d", i, ret);
            goto fail;
        }

        pChn->stExtBuf[i].bInDecoder = MPP_TRUE;
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

        VideoFrameInfo stQueueFrame;
        memset(&stQueueFrame, 0, sizeof(stQueueFrame));
        stQueueFrame.u32Idx = i;
        stQueueFrame.stVFrame.u32Fd[0] = (UL)pChn->stExtBuf[i].s32DmaBufFd;
        stQueueFrame.stVFrame.ulPlaneVirAddr[0] = (UL)pChn->stExtBuf[i].pVirAddr;

        S32 ret = pChn->stOps.queue_output_buffer(pChn->pAlCtx, &stQueueFrame);
        if (ret == MPP_OK) {
            pChn->stExtBuf[i].bInDecoder = MPP_TRUE;
            queued++;
        } else {
            error("requeue: queue buf %u failed, ret=%d", i, ret);
        }
    }

    info("re-queued %d/%u ext buffers after resolution change", queued, pChn->u32ExtBufCnt);
    return (queued > 0) ? ERR_VDEC_OK : ERR_VDEC_NOMEM;
}

static void vdec_drain_depth_queue_locked(VdecChnCtx *pChn) {
    while (pChn->u32DepthCount > 0) {
        VdecDepthEntry *pEntry = &pChn->pstDepth[pChn->u32DepthHead];
        if (pEntry->ulBufferId != 0)
            VB_ReleaseBuffer(pEntry->ulBufferId);
        pChn->u32DepthHead = (pChn->u32DepthHead + 1) % pChn->u32DepthMax;
        pChn->u32DepthCount--;
    }
    pChn->u32DepthHead = 0;
    pChn->u32DepthTail = 0;
    pChn->u32DepthCount = 0;
}

static S32 vdec_handle_resolution_change(VdecChnCtx *pChn) {
    VdecChnStatus stStatus;
    U32 u32NewW = 0;
    U32 u32NewH = 0;
    U32 u32NeedSize = 0;

    memset(&stStatus, 0, sizeof(stStatus));
    if (pChn->stOps.get_status(pChn->pAlCtx, &stStatus) == MPP_OK) {
        u32NewW = stStatus.u32Width;
        u32NewH = stStatus.u32Height;
    }

    if (u32NewW == 0 || u32NewH == 0)
        return vdec_requeue_ext_buffers(pChn);

    u32NeedSize = vdec_calc_default_buf_size(u32NewW, u32NewH, pChn->stAttr.eOutputPixelFormat, pChn->stAttr.u32Align);
    if (u32NeedSize > 0 && u32NeedSize <= pChn->u32PoolBufSize) {
        info("resolution change %ux%u fits current pool (need=%u have=%u), re-queue", u32NewW, u32NewH, u32NeedSize,
            pChn->u32PoolBufSize);
        return vdec_requeue_ext_buffers(pChn);
    }

    info("resolution change %ux%u needs larger buffers (need=%u have=%u), rebuilding ext pool", u32NewW, u32NewH,
        u32NeedSize, pChn->u32PoolBufSize);

    pthread_mutex_lock(&pChn->poolLock);
    pChn->bPoolReconfig = MPP_TRUE;
    while (!pChn->bRecycleIdle && pChn->bRecycleRun) {
        pthread_cond_wait(&pChn->poolCond, &pChn->poolLock);
    }

    pthread_mutex_lock(&pChn->depthLock);
    vdec_drain_depth_queue_locked(pChn);
    pthread_mutex_unlock(&pChn->depthLock);

    for (U32 i = 0; i < pChn->u32ExtBufCnt; i++) {
        pChn->stExtBuf[i].bInDecoder = MPP_FALSE;
    }

    vdec_destroy_ext_pool(pChn);

    S32 ret = vdec_create_ext_pool_size(pChn, u32NewW, u32NewH);
    if (ret != ERR_VDEC_OK) {
        error("failed to rebuild ext pool at %ux%u, ret=%d - stopping channel", u32NewW, u32NewH, ret);
        pChn->bRecycleRun = MPP_FALSE;
        pChn->bPoolReconfig = MPP_FALSE;
        pthread_cond_broadcast(&pChn->poolCond);
        pthread_mutex_unlock(&pChn->poolLock);
        return ret;
    }

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
        pthread_mutex_lock(&pChn->poolLock);
        if (pChn->bPoolReconfig) {
            pChn->bRecycleIdle = MPP_TRUE;
            pthread_cond_broadcast(&pChn->poolCond);
            while (pChn->bPoolReconfig && pChn->bRecycleRun) {
                pthread_cond_wait(&pChn->poolCond, &pChn->poolLock);
            }
            if (pChn->bRecycleRun)
                pChn->bRecycleIdle = MPP_FALSE;
        }
        if (!pChn->bRecycleRun) {
            pthread_mutex_unlock(&pChn->poolLock);
            break;
        }
        pthread_mutex_unlock(&pChn->poolLock);

        /* Block up to 100ms waiting for a free buffer in the pool. */
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

        /*
         * If the buffer is already queued in the decoder (e.g. the
         * error-frame path in the output task already did VIDIOC_QBUF
         * internally), we must NOT queue it again — the V4L2 driver
         * rejects a duplicate QBUF with EINVAL.  Just skip re-queuing.
         */
        if (pChn->stExtBuf[idx].bInDecoder) {
            debug("recycle: buf %d already in decoder, skip re-queue", idx);
            continue;
        }

        /* Re-queue the external dma-buf to the decoder */
        VideoFrameInfo stQueueFrame;
        memset(&stQueueFrame, 0, sizeof(stQueueFrame));
        stQueueFrame.u32Idx = (U32)idx;
        stQueueFrame.stVFrame.u32Fd[0] = (UL)pChn->stExtBuf[idx].s32DmaBufFd;
        stQueueFrame.stVFrame.ulPlaneVirAddr[0] = (UL)pChn->stExtBuf[idx].pVirAddr;

        S32 ret = pChn->stOps.queue_output_buffer(pChn->pAlCtx, &stQueueFrame);
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
        VideoFrameInfo stFrame;
        S32 ret = pChn->stOps.request_output_frame(pChn->pAlCtx, &stFrame, 100);
        if (ret == MPP_CODER_EOS) {
            /* Push an EOS entry into depth queue */
            VideoFrameInfo stEosFrame;
            memset(&stEosFrame, 0, sizeof(stEosFrame));
            stEosFrame.eFrameType = FRAME_TYPE_VDEC;
            stEosFrame.eModId = MPP_ID_VDEC;
            stEosFrame.stVdecFrameInfo.bEndOfStream = MPP_TRUE;

            pthread_mutex_lock(&pChn->depthLock);
            if (pChn->u32DepthCount < pChn->u32DepthMax) {
                VdecDepthEntry *pNew = &pChn->pstDepth[pChn->u32DepthTail];
                pNew->ulBufferId = 0;
                memcpy(&pNew->stFrameInfo, &stEosFrame, sizeof(VideoFrameInfo));
                pChn->u32DepthTail = (pChn->u32DepthTail + 1) % pChn->u32DepthMax;
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
            if (vdec_handle_resolution_change(pChn) != ERR_VDEC_OK)
                break;
            continue;
        }
        if (ret == MPP_ERROR_FRAME || ret == MPP_CODER_NULL_DATA) {
            /*
             * The buffer has been dequeued from V4L2 but carries an error
             * flag or zero payload.  We must return it to the decoder and
             * re-queue it, otherwise the buffer is leaked and the decoder
             * eventually runs out of output buffers.
             *
             * al_dec_return_output_frame internally does clearBytesUsed +
             * setExternalDmaBuf + queueBuffer, so we must NOT call
             * queue_output_buffer again — that would attempt a second
             * VIDIOC_QBUF on the same buffer index, which the V4L2 driver
             * rejects with EINVAL.
             */
            U32 errIdx = stFrame.u32Idx;
            if (errIdx < pChn->u32ExtBufCnt) {
                pChn->stExtBuf[errIdx].bInDecoder = MPP_FALSE;
                pChn->stOps.return_output_frame(pChn->pAlCtx, &stFrame);
                pChn->stExtBuf[errIdx].bInDecoder = MPP_TRUE;
            } else {
                error("output task: error frame idx=%u out of range", errIdx);
            }
            continue;
        }
        /*
         * After capture EOS, poll may still report POLLIN while DQBUF fails;
         * plugin returns MPP_CODER_NO_DATA — not an error, avoid log spam.
         */
        if (ret == MPP_CODER_NO_DATA)
            continue;
        if (ret != MPP_OK) {
            error("output task: unexpected ret=%d", ret);
            continue; /* timeout or transient error */
        }

        /* The plugin filled stFrame (planes/fds/strides/sizes/PTS/geometry)
         * and set u32Idx to the V4L2 buffer index == our ext buf slot. */
        U32 idx = stFrame.u32Idx;
        if (idx >= pChn->u32ExtBufCnt) {
            error("output task: decoded frame idx=%u out of range", idx);
            continue;
        }

        pChn->stExtBuf[idx].bInDecoder = MPP_FALSE;

        UL ulBuf = pChn->stExtBuf[idx].ulVbBuff;

        /* Attach VB ownership info (the plugin never touches these) */
        stFrame.ulPoolId = pChn->ulPoolId;
        stFrame.ulBufferId = ulBuf;
        VB_SetBufferPTS(ulBuf, stFrame.stVFrame.u64PTS);

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

            if (pChn->u32DepthCount >= pChn->u32DepthMax) {
                /* queue full — drop oldest, release its ref */
                VdecDepthEntry *pOld = &pChn->pstDepth[pChn->u32DepthHead];
                if (pOld->ulBufferId != 0)
                    VB_ReleaseBuffer(pOld->ulBufferId);
                pChn->u32DepthHead = (pChn->u32DepthHead + 1) % pChn->u32DepthMax;
                pChn->u32DepthCount--;
            }

            VdecDepthEntry *pNew = &pChn->pstDepth[pChn->u32DepthTail];
            pNew->ulBufferId = ulBuf;
            memcpy(&pNew->stFrameInfo, &stFrame, sizeof(VideoFrameInfo));
            pChn->u32DepthTail = (pChn->u32DepthTail + 1) % pChn->u32DepthMax;
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

        pthread_mutex_lock(&pChn->lock);
        if (pChn->eState == VDEC_CHN_STATE_STARTED) {
            ret = pChn->stOps.decode(pChn->pAlCtx, &stStream);
            if (ret != MPP_OK && ret != 0 && ret != MPP_CODER_EOS)
                error("stream input task: decode failed %d, chn %d", ret, s32ChnId);
        }
        pthread_mutex_unlock(&pChn->lock);

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

    vdec_plugin_close(pChn);

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

    /* desired capture buffer count; the plugin overwrites stBufReq with the
     * counts the V4L2 driver actually granted */
    U32 u32BufCnt = pChn->stAttr.u32BufCnt;
    if (u32BufCnt == 0)
        u32BufCnt = VDEC_DEFAULT_BUF_CNT;
    if (u32BufCnt > VDEC_MAX_EXT_BUF)
        u32BufCnt = VDEC_MAX_EXT_BUF;

    pChn->stBufReq.u32InputBufNum = 0; /* plugin default */
    pChn->stBufReq.u32OutputBufNum = u32BufCnt;

    S32 ret = vdec_plugin_open(pChn);
    if (ret != MPP_OK) {
        error("vdec_plugin_open failed for chn %d, ret=%d", s32ChnId, ret);
        pthread_mutex_unlock(&pChn->lock);
        return ret;
    }

    /* Create VB pool and queue buffers to decoder */
    ret = vdec_create_ext_pool(pChn);
    if (ret != ERR_VDEC_OK) {
        error("vdec_create_ext_pool failed for chn %d, ret=%d", s32ChnId, ret);
        pthread_mutex_unlock(&pChn->lock);
        return ret;
    }

    /* Initialize depth queue */
    pChn->u32DepthMax = u32BufCnt / 2;
    if (pChn->u32DepthMax < 2)
        pChn->u32DepthMax = 2;
    pChn->pstDepth = (VdecDepthEntry *)calloc(pChn->u32DepthMax, sizeof(VdecDepthEntry));
    if (!pChn->pstDepth) {
        error("depth queue alloc failed for chn %d, cnt=%u", s32ChnId, pChn->u32DepthMax);
        vdec_destroy_ext_pool(pChn);
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_NOMEM;
    }
    pChn->u32DepthHead = 0;
    pChn->u32DepthTail = 0;
    pChn->u32DepthCount = 0;
    pthread_mutex_init(&pChn->depthLock, NULL);
    pthread_cond_init(&pChn->depthNotEmpty, NULL);

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
        pthread_mutex_lock(&pChn->poolLock);
        pChn->bRecycleRun = MPP_FALSE;
        pthread_cond_broadcast(&pChn->poolCond);
        pthread_mutex_unlock(&pChn->poolLock);
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
        pthread_mutex_lock(&pChn->poolLock);
        pChn->bRecycleRun = MPP_FALSE;
        pthread_cond_broadcast(&pChn->poolCond);
        pthread_mutex_unlock(&pChn->poolLock);
        pthread_mutex_unlock(&pChn->lock);
        pthread_join(pChn->taskTid, NULL);
        pthread_join(pChn->recycleTid, NULL);
        pthread_mutex_lock(&pChn->lock);
        goto err_cleanup;
    }

    pthread_mutex_unlock(&pChn->lock);
    info("VDEC_EnableChn: chn %d enabled, bufCnt=%u, depthMax=%u", s32ChnId, u32BufCnt, pChn->u32DepthMax);
    return ERR_VDEC_OK;

err_cleanup:
    pChn->eState = VDEC_CHN_STATE_IDLE;
    vdec_destroy_ext_pool(pChn);
    pthread_mutex_destroy(&pChn->depthLock);
    pthread_cond_destroy(&pChn->depthNotEmpty);
    free(pChn->pstDepth);
    pChn->pstDepth = NULL;
    pChn->u32DepthMax = 0;
    pthread_mutex_destroy(&pChn->poolLock);
    pthread_cond_destroy(&pChn->poolCond);
    pthread_mutex_unlock(&pChn->lock);
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

    /* Signal output task thread to stop */
    pChn->bTaskRun = MPP_FALSE;
    pthread_mutex_unlock(&pChn->lock);
    pthread_join(pChn->taskTid, NULL);
    pthread_mutex_lock(&pChn->lock);

    /* Flush depth queue — release VB refs */
    pthread_mutex_lock(&pChn->depthLock);
    vdec_drain_depth_queue_locked(pChn);
    pthread_mutex_unlock(&pChn->depthLock);
    pthread_mutex_destroy(&pChn->depthLock);
    pthread_cond_destroy(&pChn->depthNotEmpty);
    free(pChn->pstDepth);
    pChn->pstDepth = NULL;
    pChn->u32DepthMax = 0;

    /* Flush decoder */
    pChn->stOps.flush(pChn->pAlCtx);

    /* Stop recycle thread */
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

    /* zero-copy: the stream buffer belongs to the caller */
    S32 ret = pChn->stOps.decode(pChn->pAlCtx, pstStream);
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

    VdecDepthEntry *pEntry = &pChn->pstDepth[pChn->u32DepthHead];
    memcpy(pstFrameInfo, &pEntry->stFrameInfo, sizeof(VideoFrameInfo));
    pChn->u32DepthHead = (pChn->u32DepthHead + 1) % pChn->u32DepthMax;
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

    if (pChn->eState == VDEC_CHN_STATE_STARTED && pChn->pAlCtx) {
        pChn->stOps.get_status(pChn->pAlCtx, pstStatus);
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

    S32 ret = pChn->stOps.flush(pChn->pAlCtx);
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

    S32 ret = pChn->stOps.reset(pChn->pAlCtx);
    pthread_mutex_unlock(&pChn->lock);
    return (ret == MPP_OK) ? ERR_VDEC_OK : ret;
}
