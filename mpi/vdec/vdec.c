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
 *                 Per-channel locks coordinate input, control and output.
 *------------------------------------------------------------------------------
 */

#define ENABLE_DEBUG 0

#include "vdec/vdec_api.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "al_interface_dec.h"
#include "log.h"
#include "module.h"
#include "sys/dma_alloc.h"
#include "sys/mpp_shm.h"
#include "sys/sys_api.h"
#include "sys/vb_api.h"
#include "vdec_bufpool.h"
#include "vdec_input_retry.h"

#define MODULE_TAG "mpp_vdec"

#define VDEC_DEFAULT_BUF_CNT 12

/* ======================== Internal Channel Context ======================== */

typedef enum _VdecChnState {
    VDEC_CHN_STATE_IDLE = 0,
    VDEC_CHN_STATE_STARTED,
    VDEC_CHN_STATE_STOPPING,
} VdecChnState;

/** al_dec_* entry points resolved from the codec plugin, per channel. */
typedef struct _VdecAlOps {
    ALBaseContext *(*create)(void);
    S32 (*init)(ALBaseContext *ctx, const VdecChnAttr *pstAttr, AlDecBufferRequirement *pstReq);
    S32 (*get_status)(ALBaseContext *ctx, VdecChnStatus *pstStatus);
    S32 (*get_output_requirement)(ALBaseContext *ctx, AlDecOutputRequirement *pstReq);
    S32 (*decode)(ALBaseContext *ctx, const StreamBufferInfo *pstStream);
    S32 (*request_output_frame)(ALBaseContext *ctx, VideoFrameInfo *pstFrame, U32 u32TimeoutMs);
    S32 (*queue_output_buffer)(ALBaseContext *ctx, const VideoFrameInfo *pstFrame);
    S32 (*flush)(ALBaseContext *ctx);
    S32 (*reset)(ALBaseContext *ctx);
    void (*destory)(ALBaseContext *ctx);
} VdecAlOps;

typedef struct _VdecChnCtx {
    atomic_bool bUsed;
    atomic_int eState;
    VdecChnAttr stAttr;
    pthread_mutex_t lock;
    pthread_mutex_t inputLock; /**< serializes packets across backpressure waits */
    pthread_mutex_t outputLock; /**< serializes all CAPTURE operations and base refs */
    S32 s32ChnId;               /**< channel ID for SYS_SendFrame */

    /* codec plugin binding (valid while pModule != NULL) */
    MppModule *pModule;
    ALBaseContext *pAlCtx;
    VdecAlOps stOps;
    AlDecBufferRequirement stBufReq;

    VdecBufPool stPool;
    S32 s32OutputResult; /**< latched EOS/error; cleared by Flush/Reset */
    pthread_t taskTid;   /**< only VDEC_OUTPUT_BIND creates an output worker */

    /* stream input thread: receives bound stream via SYS_RecvStream */
    pthread_t streamInputTid;
    atomic_bool bStreamInputRun;
    BOOL bInputBound; /**< TRUE if SYS_RecvStream got data */
} VdecChnCtx;

typedef struct _VdecInputSubmitCtx {
    VdecChnCtx *pChn;
    const StreamBufferInfo *pstStream;
    BOOL rejectBound;
} VdecInputSubmitCtx;

/* ======================== Global State ======================== */

static BOOL g_bVdecInited = MPP_FALSE;
static VdecChnCtx g_stChn[VDEC_MAX_CHN];
static pthread_mutex_t g_stGlobalLock = PTHREAD_MUTEX_INITIALIZER;

/* ======================== Helpers ======================== */

static inline BOOL vdec_chn_valid(S32 s32ChnId) { return (s32ChnId >= 0 && s32ChnId < VDEC_MAX_CHN); }

static U64 vdec_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (U64)ts.tv_sec * 1000U + (U64)ts.tv_nsec / 1000000U;
}

static VOID vdec_sleep_us(U32 delayUs, VOID *opaque) {
    (void)opaque;
    usleep(delayUs);
}

static S32 vdec_try_submit_input(VOID *opaque) {
    VdecInputSubmitCtx *submit = (VdecInputSubmitCtx *)opaque;
    VdecChnCtx *pChn = submit->pChn;
    S32 ret;

    pthread_mutex_lock(&pChn->lock);
    if (!pChn->bUsed || pChn->eState != VDEC_CHN_STATE_STARTED ||
        !atomic_load_explicit(&pChn->bStreamInputRun, memory_order_acquire)) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_NOT_STARTED;
    }
    if (submit->rejectBound && pChn->bInputBound) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_BUSY;
    }
    ret = pChn->stOps.decode(pChn->pAlCtx, submit->pstStream);
    pthread_mutex_unlock(&pChn->lock);
    return ret;
}

static U64 vdec_submit_now_ms(VOID *opaque) {
    (void)opaque;
    return vdec_monotonic_ms();
}

static S32 vdec_submit_stream(VdecChnCtx *pChn, const StreamBufferInfo *pstStream,
    U32 u32TimeoutMs, BOOL bRejectBound) {
    VdecInputSubmitCtx submit = {
        .pChn = pChn,
        .pstStream = pstStream,
        .rejectBound = bRejectBound,
    };
    VdecInputRetryOps retry = {
        .trySubmit = vdec_try_submit_input,
        .nowMs = vdec_submit_now_ms,
        .sleepUs = vdec_sleep_us,
        .opaque = &submit,
    };

    pthread_mutex_lock(&pChn->inputLock);
    S32 ret = vdec_input_submit_with_timeout(&retry, u32TimeoutMs);
    pthread_mutex_unlock(&pChn->inputLock);

    return ret;
}

static BOOL vdec_stream_input_active(VdecChnCtx *pChn) {
    return atomic_load_explicit(&pChn->bStreamInputRun, memory_order_acquire);
}

/* ======================== Plugin Binding ======================== */

static void vdec_plugin_close(VdecChnCtx *pChn);

/**
 * @brief  Load the codec plugin, resolve the al_dec_* symbols into
 *         pChn->stOps, create the AL context and init the decoder with
 *         pChn->stAttr / pChn->stBufReq.  Called from VDEC_EnableChn.
 */
static S32 vdec_plugin_open(VdecChnCtx *pChn) {
    S32 ret = MPP_INIT_FAILED;
    void *handle;
    VdecAlOps *ops = &pChn->stOps;

    pChn->pModule = module_init(CODEC_V4L2_LINLONV5V7);
    if (!pChn->pModule) {
        error("module_init failed for codec type %d", (int)CODEC_V4L2_LINLONV5V7);
        goto fail;
    }

    handle = module_get_so_path(pChn->pModule);
    if (!handle) {
        error("module handle is NULL after module_init");
        goto fail;
    }

    ops->create = (ALBaseContext * (*)(void)) dlsym(handle, "al_dec_create");
    ops->init = (S32 (*)(ALBaseContext *, const VdecChnAttr *, AlDecBufferRequirement *))dlsym(handle, "al_dec_init");
    ops->get_status = (S32 (*)(ALBaseContext *, VdecChnStatus *))dlsym(handle, "al_dec_get_status");
    ops->get_output_requirement =
        (S32 (*)(ALBaseContext *, AlDecOutputRequirement *))dlsym(handle, "al_dec_get_output_requirement");
    ops->decode = (S32 (*)(ALBaseContext *, const StreamBufferInfo *))dlsym(handle, "al_dec_decode");
    ops->request_output_frame =
        (S32 (*)(ALBaseContext *, VideoFrameInfo *, U32))dlsym(handle, "al_dec_request_output_frame");
    ops->queue_output_buffer =
        (S32 (*)(ALBaseContext *, const VideoFrameInfo *))dlsym(handle, "al_dec_queue_output_buffer");
    ops->flush = (S32 (*)(ALBaseContext *))dlsym(handle, "al_dec_flush");
    ops->reset = (S32 (*)(ALBaseContext *))dlsym(handle, "al_dec_reset");
    ops->destory = (void (*)(ALBaseContext *))dlsym(handle, "al_dec_destory");

    if (!ops->create || !ops->init || !ops->get_status || !ops->get_output_requirement || !ops->decode ||
        !ops->request_output_frame || !ops->queue_output_buffer || !ops->flush || !ops->reset || !ops->destory) {
        error("required decoder symbols missing from plugin (create/init/"
                "get_status/get_output_requirement/decode/request/queue/flush/reset/"
                "destory)");
        goto fail;
    }

    pChn->pAlCtx = ops->create();
    if (!pChn->pAlCtx) {
        error("al_dec_create returned NULL");
        goto fail;
    }

    ret = ops->init(pChn->pAlCtx, &pChn->stAttr, &pChn->stBufReq);
    debug("init VDEC Channel, ret = %d", ret);

    if (ret != MPP_OK)
        goto fail;

    return ret;

fail:
    vdec_plugin_close(pChn);
    return ret;
}

/**
 * @brief  Destroy the AL decoder context and unload the plugin module.
 *         Safe to call when the plugin was never opened.
 */
static void vdec_plugin_close(VdecChnCtx *pChn) {
    if (pChn->pAlCtx && pChn->stOps.destory)
        pChn->stOps.destory(pChn->pAlCtx);
    if (pChn->pModule)
        module_destory(pChn->pModule);

    pChn->pModule = NULL;
    pChn->pAlCtx = NULL;
    memset(&pChn->stOps, 0, sizeof(pChn->stOps));
}

/* ======================== Shared output path ======================== */

/* Lock order: outputLock -> lock. Input retries release lock before
 * sleeping, so Flush/Reset can pause submission without deadlock. */
static S32 vdec_create_ext_pool(VdecChnCtx *pChn) {
    AlDecOutputRequirement req = {0};
    S32 ret = pChn->stOps.get_output_requirement(pChn->pAlCtx, &req);
    if (ret != MPP_OK)
        return ret;
    ret = vdec_bufpool_create(&pChn->stPool, &req);
    if (ret == ERR_VDEC_OK)
        ret = vdec_bufpool_queue_available_buffers(&pChn->stPool, pChn->pAlCtx, pChn->stOps.queue_output_buffer, 0);
    return ret;
}

static S32 vdec_destroy_ext_pool(VdecChnCtx *pChn) {
    S32 ret = vdec_bufpool_retire_active_pool(&pChn->stPool);
    if (ret != ERR_VDEC_OK)
        return ret;
    vdec_bufpool_try_destroy_retired_pools(&pChn->stPool);
    return pChn->stPool.u32RetiredCount ? ERR_VDEC_BUSY : ERR_VDEC_OK;
}

static U32 vdec_remaining_ms(U64 start, U32 budget) {
    U64 elapsed = vdec_monotonic_ms() - start;
    return elapsed >= budget ? 0 : budget - (U32)elapsed;
}

/* Caller must hold outputLock; this function does not lock internally.
 * Requeue available buffers, request one decoder output and handle pool
 * changes. NO_FRAME also covers skipped output or a completed pool replacement.
 * On success, the frame has a VDEC reference but no application reference.
 * Hand off or release that reference before reading again or unlocking. */
static S32 vdec_read_decoded_frame_locked(VdecChnCtx *pChn, VideoFrameInfo *frame, U32 waitMs) {
    U64 start = vdec_monotonic_ms();
    S32 ret;
    if (!pChn->bUsed || pChn->eState != VDEC_CHN_STATE_STARTED)
        return ERR_VDEC_NOT_STARTED;
    if (pChn->s32OutputResult)
        return pChn->s32OutputResult;

    vdec_bufpool_try_destroy_retired_pools(&pChn->stPool);
    ret = vdec_bufpool_queue_available_buffers(&pChn->stPool, pChn->pAlCtx, pChn->stOps.queue_output_buffer,
                                                vdec_bufpool_get_queued_count(&pChn->stPool) ? 0 : waitMs);
    if (ret != MPP_OK)
        goto failed;
    memset(frame, 0, sizeof(*frame));
    ret = pChn->stOps.request_output_frame(pChn->pAlCtx, frame, vdec_remaining_ms(start, waitMs));
    if (ret == MPP_RESOLUTION_CHANGED) {
        /* AL has stopped/reallocated CAPTURE. Consumers retain the old VB pool;
         * no concurrent QBUF can still use the old plugin slot metadata. */
        ret = vdec_bufpool_retire_active_pool(&pChn->stPool);
        if (ret == MPP_OK)
            ret = vdec_create_ext_pool(pChn);
        if (ret != MPP_OK)
            goto failed;
        return ERR_VDEC_NO_FRAME;
    }
    if (ret == MPP_CODER_EOS) {
        pChn->s32OutputResult = ERR_VDEC_EOS;
        return ERR_VDEC_EOS;
    }
    if (ret == MPP_CODER_NO_DATA) {
        /* POLLERR/EPIPE can be immediately ready despite a positive timeout. */
        U32 remaining = vdec_remaining_ms(start, waitMs);
        if (remaining)
            usleep(remaining * 1000U);
        return ERR_VDEC_NO_FRAME;
    }
    BOOL discard = ret == MPP_CODER_NULL_DATA || (ret == MPP_ERROR_FRAME && !pChn->stAttr.bDispErrorFrame);
    if (ret != MPP_OK && ret != MPP_ERROR_FRAME && ret != MPP_CODER_NULL_DATA)
        goto failed;
    ret = vdec_bufpool_on_buffer_dequeued(&pChn->stPool, frame);
    if (ret != MPP_OK)
        goto failed;
    if (discard)
        return ERR_VDEC_NO_FRAME; /* OWNED: next bounded pass queues it once. */
    ret = VB_UpdateBufferFrameInfo(frame->ulBufferId, frame);
    if (ret != MPP_OK)
        goto failed;
    if (frame->stVdecFrameInfo.bEndOfStream)
        pChn->s32OutputResult = ERR_VDEC_EOS;
    return ERR_VDEC_OK;
failed:
    pChn->s32OutputResult = ret;
    error("output failed: chn=%d ret=%d", pChn->s32ChnId, ret);
    return ret;
}

/* Caller must hold outputLock and supply a frame whose VDEC reference is OWNED.
 * Add an application reference before releasing VDEC's reference. No pixels are
 * copied and no frame is sent. On success, the caller must release the frame
 * with VDEC_ReleaseFrame (or VB_ReleaseBuffer) when it is no longer needed. */
static S32 vdec_transfer_frame_to_caller_locked(VdecChnCtx *pChn, VideoFrameInfo *frame) {
    S32 ret = VB_RefAdd(frame->ulBufferId);
    if (ret != MPP_OK) {
        vdec_bufpool_release_vdec_ref(&pChn->stPool, frame->u32Idx);
        return ret;
    }
    ret = vdec_bufpool_release_vdec_ref(&pChn->stPool, frame->u32Idx);
    if (ret != MPP_OK)
        VB_ReleaseBuffer(frame->ulBufferId);
    return ret;
}

static void *vdec_output_task(void *arg) {
    VdecChnCtx *pChn = (VdecChnCtx *)arg;
    MppNode src = {.eModId = MPP_ID_VDEC, .s32DevId = 0, .s32ChnId = pChn->s32ChnId};
    while (pChn->eState == VDEC_CHN_STATE_STARTED) {
        VideoFrameInfo frame;
        pthread_mutex_lock(&pChn->outputLock);
        S32 ret = vdec_read_decoded_frame_locked(pChn, &frame, 10);
        if (ret == ERR_VDEC_OK) {
            S32 sendRet = SYS_SendFrame(&src, frame.ulBufferId);
            if (sendRet != SYS_ERR_OK && sendRet != SYS_ERR_NOT_FOUND)
                error("bound frame delivery failed: chn=%d ret=%d", pChn->s32ChnId, sendRet);
            ret = vdec_bufpool_release_vdec_ref(&pChn->stPool, frame.u32Idx);
        }
        pthread_mutex_unlock(&pChn->outputLock);
        /* Keep the worker available for explicit Flush/Reset recovery. */
        if (ret != ERR_VDEC_OK && ret != ERR_VDEC_NO_FRAME)
            usleep(10000);
    }
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

    info("stream input task started: chn %d", s32ChnId);

    while (vdec_stream_input_active(pChn)) {
        StreamBufferInfo stStream;
        memset(&stStream, 0, sizeof(stStream));

        ret = SYS_RecvStream(&stSink, &stStream, 100);
        if (ret != 0) {
            /* timeout or no bind — just retry */
            if (SYS_ERR_NOT_FOUND == ret) {
                pthread_mutex_lock(&pChn->lock);
                pChn->bInputBound = MPP_FALSE;
                pthread_mutex_unlock(&pChn->lock);
                usleep(20000); // Sleep 20ms before retrying to avoid busy loop when no
                               // stream is bound
            }
            continue;
        }

        /* Mark channel as bound on first successful receive */
        pthread_mutex_lock(&pChn->lock);
        if (!pChn->bInputBound) {
            pChn->bInputBound = MPP_TRUE;
            info("stream input task: chn %d bound, stream input active", s32ChnId);
        }
        pthread_mutex_unlock(&pChn->lock);

        S32 fd = -1;
        BOOL read_started = MPP_FALSE;
        ret = MPP_OK;
        if (stStream.ulVbHandle) {
            if (VB_GetDmaBufFd(stStream.ulVbHandle, &fd) != 0 ||
                dma_sync_buf(fd, DMA_SYNC_READ | DMA_SYNC_START) != 0)
                ret = MPP_POLL_FAILED;
            else
                read_started = MPP_TRUE;
        }
        if (ret == MPP_OK) {
            /* Keep the source VB alive throughout the existing input retry. */
            ret = vdec_submit_stream(pChn, &stStream, (U32)-1, MPP_FALSE);
        }
        if (read_started && dma_sync_buf(fd, DMA_SYNC_READ | DMA_SYNC_END) != 0)
            error("stream input task: VB read end failed, chn %d", s32ChnId);
        if (stStream.ulVbHandle && VB_ReleaseBuffer(stStream.ulVbHandle) != 0)
            error("stream input task: VB release failed, chn %d", s32ChnId);
        if (ret != MPP_OK && ret != 0 && ret != MPP_CODER_EOS && ret != ERR_VDEC_NOT_STARTED)
            error("stream input task: decode failed %d, chn %d", ret, s32ChnId);

        if (stStream.bEndOfStream) {
            info("stream input task: EOS received, chn %d", s32ChnId);
            break;
        }
    }

    info("stream input task exiting: chn %d", s32ChnId);
    return NULL;
}

static S32 vdec_get_frame(S32 chn, VideoFrameInfo *frame, U32 timeoutMs, BOOL latest) {
    if (!frame)
        return ERR_VDEC_NULL_PTR;
    if (!vdec_chn_valid(chn))
        return ERR_VDEC_INVALID_CHN;
    VdecChnCtx *pChn = &g_stChn[chn];
    if (!pChn->bUsed || pChn->eState != VDEC_CHN_STATE_STARTED)
        return ERR_VDEC_NOT_STARTED;
    U64 start = vdec_monotonic_ms();
    for (;;) {
        pthread_mutex_lock(&pChn->outputLock);
        U32 waitMs = timeoutMs == (U32)-1 ? 10 : vdec_remaining_ms(start, timeoutMs);
        if (waitMs > 10)
            waitMs = 10;
        S32 ret;
        if (pChn->stAttr.eOutputMode != VDEC_OUTPUT_PULL) {
            ret = ERR_VDEC_NOT_SUPPORT;
        } else {
            ret = vdec_read_decoded_frame_locked(pChn, frame, waitMs);
            if (ret == ERR_VDEC_OK)
                ret = vdec_transfer_frame_to_caller_locked(pChn, frame);
            if (ret == ERR_VDEC_OK && latest) {
                /* The candidate has an application reference: a subsequent
                 * resolution change may safely retire its whole pool. */
                for (U32 i = 1; i < VDEC_MAX_EXT_BUF; ++i) {
                    VideoFrameInfo next;
                    S32 nextRet = vdec_read_decoded_frame_locked(pChn, &next, 0);
                    if (nextRet == ERR_VDEC_NO_FRAME || nextRet == ERR_VDEC_EOS)
                        break;
                    if (nextRet == ERR_VDEC_OK)
                        nextRet = vdec_transfer_frame_to_caller_locked(pChn, &next);
                    VB_ReleaseBuffer(frame->ulBufferId);
                    if (nextRet != ERR_VDEC_OK) {
                        ret = nextRet;
                        break;
                    }
                    *frame = next;
                }
            }
        }
        pthread_mutex_unlock(&pChn->outputLock);
        if (ret != ERR_VDEC_NO_FRAME || timeoutMs == 0)
            return ret;
        if (timeoutMs != (U32)-1 && !vdec_remaining_ms(start, timeoutMs))
            return ERR_VDEC_TIMEOUT;
    }
}

static S32 vdec_control(S32 chn, BOOL reset) {
    if (!vdec_chn_valid(chn))
        return ERR_VDEC_INVALID_CHN;
    VdecChnCtx *pChn = &g_stChn[chn];
    pthread_mutex_lock(&pChn->outputLock);
    pthread_mutex_lock(&pChn->lock);
    S32 ret = ERR_VDEC_NOT_STARTED;
    if (pChn->bUsed && pChn->eState == VDEC_CHN_STATE_STARTED) {
        ret = reset ? pChn->stOps.reset(pChn->pAlCtx) : pChn->stOps.flush(pChn->pAlCtx);
        if (ret == MPP_OK) {
            vdec_bufpool_on_stream_stopped(&pChn->stPool);
            ret = vdec_bufpool_queue_available_buffers(&pChn->stPool, pChn->pAlCtx, pChn->stOps.queue_output_buffer, 0);
        }
        pChn->s32OutputResult = ret;
    }
    pthread_mutex_unlock(&pChn->lock);
    pthread_mutex_unlock(&pChn->outputLock);
    return ret;
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
        pthread_mutex_init(&g_stChn[i].inputLock, NULL);
        pthread_mutex_init(&g_stChn[i].outputLock, NULL);
        atomic_init(&g_stChn[i].bStreamInputRun, MPP_FALSE);
        atomic_init(&g_stChn[i].bUsed, MPP_FALSE);
        atomic_init(&g_stChn[i].eState, VDEC_CHN_STATE_IDLE);
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
        pthread_mutex_destroy(&g_stChn[i].outputLock);
        pthread_mutex_destroy(&g_stChn[i].inputLock);
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

    if (pstAttr->eOutputMode != VDEC_OUTPUT_PULL && pstAttr->eOutputMode != VDEC_OUTPUT_BIND) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VDEC_NOT_SUPPORT;
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
    S32 ret = ERR_VDEC_OK;
    pthread_mutex_lock(&pChn->outputLock);
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed) {
        ret = ERR_VDEC_INVALID_CHN;
        goto unlock;
    }
    if (pChn->eState != VDEC_CHN_STATE_IDLE) {
        error("channel %d still started, stop it first", s32ChnId);
        ret = ERR_VDEC_BUSY;
        goto unlock;
    }

    vdec_plugin_close(pChn);

    /* Safety: destroy VB pool if still alive */
    if (vdec_destroy_ext_pool(pChn) != ERR_VDEC_OK) {
        ret = ERR_VDEC_BUSY;
        goto unlock;
    }

    pChn->bUsed = MPP_FALSE;
unlock:
    pthread_mutex_unlock(&pChn->lock);
    pthread_mutex_unlock(&pChn->outputLock);
    return ret;
}

S32 VDEC_EnableChn(S32 chn) {
    if (!vdec_chn_valid(chn))
        return ERR_VDEC_INVALID_CHN;
    VdecChnCtx *pChn = &g_stChn[chn];
    S32 ret;
    BOOL taskCreated = MPP_FALSE;
    pthread_mutex_lock(&pChn->outputLock);
    pthread_mutex_lock(&pChn->lock);
    if (!pChn->bUsed) {
        ret = ERR_VDEC_INVALID_CHN;
        goto unlock;
    }
    if (pChn->eState != VDEC_CHN_STATE_IDLE) {
        ret = ERR_VDEC_ALREADY_INIT;
        goto unlock;
    }
    ret = vdec_destroy_ext_pool(pChn);
    if (ret != ERR_VDEC_OK)
        goto unlock;
    pChn->stBufReq.u32InputBufNum = 0;
    pChn->stBufReq.u32OutputBufNum = pChn->stAttr.u32BufCnt ? pChn->stAttr.u32BufCnt : VDEC_DEFAULT_BUF_CNT;
    if (pChn->stBufReq.u32OutputBufNum > VDEC_MAX_EXT_BUF)
        pChn->stBufReq.u32OutputBufNum = VDEC_MAX_EXT_BUF;
    ret = vdec_plugin_open(pChn);
    if (ret != MPP_OK)
        goto fail;
    ret = vdec_create_ext_pool(pChn);
    if (ret != ERR_VDEC_OK)
        goto fail;
    pChn->s32OutputResult = 0;
    pChn->bInputBound = MPP_FALSE;
    pChn->eState = VDEC_CHN_STATE_STARTED;
    if (pChn->stAttr.eOutputMode == VDEC_OUTPUT_BIND) {
        ret = pthread_create(&pChn->taskTid, NULL, vdec_output_task, pChn);
        if (ret != 0) {
            ret = ERR_VDEC_NOMEM;
            goto fail;
        }
        taskCreated = MPP_TRUE;
    }
    atomic_store_explicit(&pChn->bStreamInputRun, MPP_TRUE, memory_order_release);
    ret = pthread_create(&pChn->streamInputTid, NULL, vdec_stream_input_task, pChn);
    if (ret != 0) {
        atomic_store_explicit(&pChn->bStreamInputRun, MPP_FALSE, memory_order_release);
        pChn->eState = VDEC_CHN_STATE_STOPPING;
        pthread_mutex_unlock(&pChn->lock);
        pthread_mutex_unlock(&pChn->outputLock);
        if (taskCreated)
            pthread_join(pChn->taskTid, NULL);
        pthread_mutex_lock(&pChn->outputLock);
        pthread_mutex_lock(&pChn->lock);
        ret = ERR_VDEC_NOMEM;
        goto fail;
    }
    ret = ERR_VDEC_OK;
    goto unlock;
fail:
    pChn->eState = VDEC_CHN_STATE_IDLE;
    /* Stop CAPTURE before releasing references in a partially queued pool. */
    vdec_plugin_close(pChn);
    vdec_destroy_ext_pool(pChn);
unlock:
    pthread_mutex_unlock(&pChn->lock);
    pthread_mutex_unlock(&pChn->outputLock);
    return ret;
}

S32 VDEC_DisableChn(S32 chn) {
    if (!vdec_chn_valid(chn))
        return ERR_VDEC_INVALID_CHN;
    VdecChnCtx *pChn = &g_stChn[chn];
    pthread_mutex_lock(&pChn->lock);
    if (!pChn->bUsed || pChn->eState != VDEC_CHN_STATE_STARTED) {
        S32 ret = pChn->bUsed ? ERR_VDEC_NOT_STARTED : ERR_VDEC_INVALID_CHN;
        pthread_mutex_unlock(&pChn->lock);
        return ret;
    }
    pChn->eState = VDEC_CHN_STATE_STOPPING;
    atomic_store_explicit(&pChn->bStreamInputRun, MPP_FALSE, memory_order_release);
    pthread_mutex_unlock(&pChn->lock);
    pthread_join(pChn->streamInputTid, NULL);
    if (pChn->stAttr.eOutputMode == VDEC_OUTPUT_BIND)
        pthread_join(pChn->taskTid, NULL);
    pthread_mutex_lock(&pChn->outputLock);
    pthread_mutex_lock(&pChn->lock);
    vdec_plugin_close(pChn);
    S32 ret = vdec_bufpool_retire_active_pool(&pChn->stPool);
    pChn->eState = VDEC_CHN_STATE_IDLE;
    pthread_mutex_unlock(&pChn->lock);
    pthread_mutex_unlock(&pChn->outputLock);
    return ret;
}

S32 VDEC_SendStream(S32 s32ChnId, const StreamBufferInfo *pstStream, U32 u32TimeoutMs) {
    if (!pstStream)
        return ERR_VDEC_NULL_PTR;
    if (!vdec_chn_valid(s32ChnId))
        return ERR_VDEC_INVALID_CHN;

    VdecChnCtx *pChn = &g_stChn[s32ChnId];
    S32 ret = vdec_submit_stream(pChn, pstStream, u32TimeoutMs, MPP_TRUE);

    return ret == MPP_CODER_EOS ? ERR_VDEC_EOS : ret;
}

S32 VDEC_GetFrame(S32 s32ChnId, VideoFrameInfo *pstFrameInfo, U32 u32TimeoutMs) {
    return vdec_get_frame(s32ChnId, pstFrameInfo, u32TimeoutMs, MPP_FALSE);
}

S32 VDEC_GetLatestFrame(S32 s32ChnId, VideoFrameInfo *pstFrameInfo, U32 u32TimeoutMs) {
    return vdec_get_frame(s32ChnId, pstFrameInfo, u32TimeoutMs, MPP_TRUE);
}

/* Drop the application reference. PULL recycles on the next read; BIND uses
 * its worker. Outstanding frames remain releasable after Disable. */
S32 VDEC_ReleaseFrame(S32 s32ChnId, UL ulVbBuff) {
    if (!vdec_chn_valid(s32ChnId))
        return ERR_VDEC_INVALID_CHN;
    if (!ulVbBuff)
        return ERR_VDEC_NULL_PTR;

    S32 ret = VB_ReleaseBuffer(ulVbBuff);
    return ret == 0 ? ERR_VDEC_OK : ret;
}

S32 VDEC_QueryStatus(S32 chn, VdecChnStatus *status) {
    if (!status)
        return ERR_VDEC_NULL_PTR;
    if (!vdec_chn_valid(chn))
        return ERR_VDEC_INVALID_CHN;
    VdecChnCtx *pChn = &g_stChn[chn];
    pthread_mutex_lock(&pChn->outputLock);
    pthread_mutex_lock(&pChn->lock);
    S32 ret = ERR_VDEC_OK;
    memset(status, 0, sizeof(*status));
    if (!pChn->bUsed)
        ret = ERR_VDEC_INVALID_CHN;
    else if (pChn->eState == VDEC_CHN_STATE_STARTED && pChn->pAlCtx)
        ret = pChn->stOps.get_status(pChn->pAlCtx, status);
    pthread_mutex_unlock(&pChn->lock);
    pthread_mutex_unlock(&pChn->outputLock);
    return ret;
}

S32 VDEC_Flush(S32 chn) { return vdec_control(chn, MPP_FALSE); }

S32 VDEC_Reset(S32 chn) { return vdec_control(chn, MPP_TRUE); }
