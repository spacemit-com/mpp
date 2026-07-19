/*
 *------------------------------------------------------------------------------
 * Copyright 2022-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    venc.c
 * @Brief     :    VENC public API implementation (venc_api.h / venc_type.h).
 *                 Loads the codec plugin via the AL module loader, binds the
 *                 al_enc_* symbols per channel, and manages channel state,
 *                 the stream output queue and the worker threads.
 *                 Thread-safe via per-channel mutex.
 *------------------------------------------------------------------------------
 */

#define ENABLE_DEBUG 0

#include "venc/venc_api.h"

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "al_interface_enc.h"
#include "log.h"
#include "module.h"
#include "sys/sys_api.h"
#include "sys/vb_api.h"

#define MODULE_TAG "mpp_venc"

/* ======================== Internal Channel Context ======================== */

typedef enum _VencChnState {
    VENC_CHN_STATE_IDLE = 0,
    VENC_CHN_STATE_STARTED,
} VencChnState;

/** al_enc_* entry points resolved from the codec plugin, per channel. */
typedef struct _VencAlOps {
    ALBaseContext *(*create)(void);
    S32 (*init)(ALBaseContext *ctx, const VencChnAttr *pstAttr);
    S32 (*set_para)(ALBaseContext *ctx, VencCmd cmd, const void *pParam);
    S32 (*send_input_frame)(ALBaseContext *ctx, const VideoFrameInfo *pstFrame);
    S32 (*return_input_frame)(ALBaseContext *ctx);
    S32 (*request_output_stream)(ALBaseContext *ctx, StreamBufferInfo *pstStream, U32 u32TimeoutMs);
    S32 (*get_status)(ALBaseContext *ctx, VencChnStatus *pstStatus); /* optional */
    S32 (*flush)(ALBaseContext *ctx);
    void (*destory)(ALBaseContext *ctx);
} VencAlOps;

typedef struct _VencChnCtx {
    BOOL bUsed;
    VencChnState eState;
    VencChnAttr stAttr;
    pthread_mutex_t lock;
    S32 s32ChnId;

    /* codec plugin binding (valid while pModule != NULL) */
    MppModule *pModule;
    ALBaseContext *pAlCtx;
    VencAlOps stOps;

    /* frame input thread: receives bound frames via SYS_RecvFrame */
    pthread_t frameInputTid;
    BOOL bFrameInputRun;
    BOOL bBound;     /**< TRUE if SYS_RecvFrame got data */
    BOOL bBoundSink; /**< TRUE if SYS_SendStream got data */

    /* Task thread: sole consumer of al_enc_request_output_stream; pushes
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

/* The linlonv5v7 encoder consumes input frames zero-copy (USERPTR/DMABUF)
 * and only releases them asynchronously via al_enc_return_input_frame().
 * Both the bound path (venc_frame_input_task) and the manual path
 * (VENC_SendFrame) must keep one VB reference per in-flight input frame
 * until the encoder returns the matching input buffer, otherwise the VB
 * buffer can be recycled while the hardware is still reading it (UAF).
 * The slot index is passed to the plugin as VideoFrameInfo.u32Idx and
 * comes back from al_enc_return_input_frame(), identifying which ref to
 * release. */
#define VENC_INPUT_REF_SLOTS 64
    UL aulInputRefs[VENC_INPUT_REF_SLOTS];
    pthread_mutex_t inputRefLock;
} VencChnCtx;

/* ======================== Global State ======================== */

static BOOL g_bVencInited = MPP_FALSE;
static VencChnCtx g_stChn[VENC_MAX_CHN];
static pthread_mutex_t g_stGlobalLock = PTHREAD_MUTEX_INITIALIZER;

/* ======================== Helpers ======================== */

static inline BOOL venc_chn_valid(S32 s32ChnId) {
    return (s32ChnId >= 0 && s32ChnId < VENC_MAX_CHN);
}

/* ======================== Input Frame Ref Tracking ======================== */

/**
 * @brief  Reserve a slot for an in-flight input frame and return the slot
 *         index as the frame id to hand to the encoder plugin.
 *         Caller must hold pChn->lock.
 *
 * @param  ulBuff     VB buffer handle backing the input frame (non-zero).
 * @param  bAddRef    MPP_TRUE  : take a new VB ref (manual VENC_SendFrame,
 *                                where the caller keeps ownership of ulBuff).
 *                    MPP_FALSE : transfer an already-owned ref into the slot
 *                                (bound path, where SYS_RecvFrame added a ref
 *                                that the consumer must release).
 * @param  pFrameId   Output: slot index to store in VideoFrameInfo.u32Idx.
 * @return ERR_VENC_OK on success; ERR_VENC_BUSY if no free slot.
 */
static S32 venc_hold_input_ref_locked(VencChnCtx *pChn, UL ulBuff, BOOL bAddRef, S32 *pFrameId) {
    if (!pChn || !pFrameId || ulBuff == 0)
        return ERR_VENC_NULL_PTR;

    S32 slot = -1;
    for (S32 i = 0; i < VENC_INPUT_REF_SLOTS; ++i) {
        if (pChn->aulInputRefs[i] == 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return ERR_VENC_BUSY;

    if (bAddRef) {
        S32 ret = VB_RefAdd(ulBuff);
        if (ret != 0)
            return ret;
    }

    pChn->aulInputRefs[slot] = ulBuff;
    *pFrameId = slot;
    return ERR_VENC_OK;
}

/**
 * @brief  Release the VB reference held in the given slot.
 *         Caller must hold pChn->lock.
 */
static void venc_release_input_ref_locked(VencChnCtx *pChn, S32 frameId) {
    if (!pChn || frameId < 0 || frameId >= VENC_INPUT_REF_SLOTS)
        return;

    UL ulBuff = pChn->aulInputRefs[frameId];
    if (ulBuff != 0) {
        pChn->aulInputRefs[frameId] = 0;
        VB_ReleaseBuffer(ulBuff);
    }
}

/**
 * @brief  Ask the encoder for one recycled input buffer; if it returns a
 *         valid slot id, release the VB reference held for that input frame.
 *         Must be called WITHOUT holding pChn->lock (the plugin call may poll).
 */
static void venc_return_one_input_ref(VencChnCtx *pChn) {
    if (!pChn)
        return;

    pthread_mutex_lock(&pChn->inputRefLock);
    S32 frameId = pChn->stOps.return_input_frame(pChn->pAlCtx);
    if (frameId < 0) {
        pthread_mutex_unlock(&pChn->inputRefLock);
        return;
    }

    pthread_mutex_lock(&pChn->lock);
    venc_release_input_ref_locked(pChn, frameId);
    pthread_mutex_unlock(&pChn->lock);
    pthread_mutex_unlock(&pChn->inputRefLock);
}

/**
 * @brief  Release every VB reference still held by the channel.
 *         Caller must hold pChn->lock.
 */
static void venc_release_all_input_refs_locked(VencChnCtx *pChn) {
    if (!pChn)
        return;

    for (S32 i = 0; i < VENC_INPUT_REF_SLOTS; ++i) {
        venc_release_input_ref_locked(pChn, i);
    }
}

/* ======================== Plugin Binding ======================== */

/**
 * @brief  Load the codec plugin, resolve the al_enc_* symbols into
 *         pChn->stOps, create the AL context and init the encoder with
 *         pChn->stAttr.  Called from VENC_EnableChn.
 */
static S32 venc_plugin_open(VencChnCtx *pChn) {
    S32 ret = 0;
    void *handle;
    VencAlOps *ops = &pChn->stOps;

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
     * Stale-plugin guard: the AL ABI was changed in place (same al_enc_*
     * symbol names, new struct-based signatures). A plugin built against the
     * legacy interface still exports al_enc_encode / al_enc_get_output_stream,
     * which no longer exist in the new interface — loading such a plugin would
     * crash at runtime with mismatched struct layouts, so reject it here.
     */
    if (dlsym(handle, "al_enc_encode") || dlsym(handle, "al_enc_get_output_stream")) {
        error(
            "legacy codec plugin detected (exports al_enc_encode / "
            "al_enc_get_output_stream); rebuild and redeploy the plugin to "
            "match this libmpp");
        module_destory(pChn->pModule);
        pChn->pModule = NULL;
        return MPP_INIT_FAILED;
    }

    ops->create = (ALBaseContext * (*)(void)) dlsym(handle, "al_enc_create");
    ops->init = (S32 (*)(ALBaseContext *, const VencChnAttr *))dlsym(handle, "al_enc_init");
    ops->set_para = (S32 (*)(ALBaseContext *, VencCmd, const void *))dlsym(handle, "al_enc_set_para");
    ops->send_input_frame = (S32 (*)(ALBaseContext *, const VideoFrameInfo *))dlsym(handle, "al_enc_send_input_frame");
    ops->return_input_frame = (S32 (*)(ALBaseContext *))dlsym(handle, "al_enc_return_input_frame");
    ops->request_output_stream =
        (S32 (*)(ALBaseContext *, StreamBufferInfo *, U32))dlsym(handle, "al_enc_request_output_stream");
    ops->get_status = (S32 (*)(ALBaseContext *, VencChnStatus *))dlsym(handle, "al_enc_get_status");
    ops->flush = (S32 (*)(ALBaseContext *))dlsym(handle, "al_enc_flush");
    ops->destory = (void (*)(ALBaseContext *))dlsym(handle, "al_enc_destory");

    if (!ops->create || !ops->init || !ops->set_para || !ops->send_input_frame || !ops->return_input_frame ||
        !ops->request_output_stream || !ops->flush || !ops->destory) {
        error(
            "required encoder symbols missing from plugin (create/init/"
            "set_para/send/return/request/flush/destory)");
        module_destory(pChn->pModule);
        pChn->pModule = NULL;
        return MPP_INIT_FAILED;
    }

    pChn->pAlCtx = ops->create();
    if (!pChn->pAlCtx) {
        error("al_enc_create returned NULL");
        module_destory(pChn->pModule);
        pChn->pModule = NULL;
        return MPP_INIT_FAILED;
    }

    ret = ops->init(pChn->pAlCtx, &pChn->stAttr);
    if (ret != MPP_OK) {
        error("al_enc_init failed, ret = %d", ret);
        /* init failed (e.g. CMA exhausted): destroy the AL context and unload
         * the plugin so the channel is left clean and a later retry (chaos
         * restart once CMA frees up) can re-open from scratch without leaking
         * the plugin handle or a half-built encoder context. */
        if (ops->destory) {
            ops->destory(pChn->pAlCtx);
        }
        pChn->pAlCtx = NULL;
        module_destory(pChn->pModule);
        pChn->pModule = NULL;
        return ret;
    }
    debug("init VENC Channel success");

    return MPP_OK;
}

/**
 * @brief  Destroy the AL encoder context and unload the plugin module.
 *         Safe to call when the plugin was never opened.
 */
static void venc_plugin_close(VencChnCtx *pChn) {
    if (pChn->pModule == NULL) {
        info("module not init!");
        return;
    }

    if (pChn->stOps.destory)
        pChn->stOps.destory(pChn->pAlCtx);
    debug("finish destory encoder");

    module_destory(pChn->pModule);
    debug("finish destory module");

    pChn->pModule = NULL;
    pChn->pAlCtx = NULL;
    memset(&pChn->stOps, 0, sizeof(pChn->stOps));
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
        /* Allocate buffer for encoded output */
        U32 allocSize = pChn->stAttr.u32Width * pChn->stAttr.u32Height;
        if (allocSize == 0)
            allocSize = 1280 * 720;
        U8 *pOutBuf = (U8 *)malloc(allocSize);
        if (!pOutBuf) {
            usleep(1000);
            continue;
        }

        /* Request encoded stream from encoder (timeout-based poll).
         * The plugin copies the payload into pOutBuf and overwrites
         * u32Size with the payload length. */
        StreamBufferInfo stStream;
        memset(&stStream, 0, sizeof(stStream));
        stStream.pu8Addr = pOutBuf;
        stStream.u32Size = allocSize;

        S32 ret = pChn->stOps.request_output_stream(pChn->pAlCtx, &stStream, 100);
        if (ret != MPP_OK && ret != MPP_CODER_EOS) {
            free(pOutBuf);
            usleep(5000); /* no stream available */
            continue;
        }

        /* Successfully got encoded output; recycle one input buffer so the
         * encoder can accept more frames when the pool is full.  This also
         * releases the VB reference held for that input frame (the slot id
         * comes back from al_enc_return_input_frame). */
        venc_return_one_input_ref(pChn);

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
             * The CAPTURE buffer was already re-queued inside the plugin,
             * so just free the local output buffer. */
            free(pOutBuf);
            continue;
        }

        /* Non-bind mode: queue for VENC_GetStream;
         * VENC_ReleaseStream frees the buffer. */
        stStream.ulPrivate = (UL)pOutBuf;

        pthread_mutex_lock(&pChn->queueLock);
        while (pChn->u32QueueCount >= VENC_STREAM_QUEUE_SIZE && pChn->bTaskRun) {
            pthread_cond_wait(&pChn->queueNotFull, &pChn->queueLock);
        }
        if (!pChn->bTaskRun) {
            pthread_mutex_unlock(&pChn->queueLock);
            free(pOutBuf);
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
        if (st.ulPrivate) {
            free((void *)st.ulPrivate);
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
    S32 ret = pthread_mutex_init(&pChn->queueLock, NULL);
    if (ret != 0) {
        error("failed to init venc queue lock for chn %d: %s", pChn->s32ChnId, strerror(ret));
        return ERR_VENC_NOMEM;
    }

    ret = pthread_cond_init(&pChn->queueNotEmpty, NULL);
    if (ret != 0) {
        error("failed to init venc queue-not-empty condition for chn %d: %s", pChn->s32ChnId, strerror(ret));
        pthread_mutex_destroy(&pChn->queueLock);
        return ERR_VENC_NOMEM;
    }

    ret = pthread_cond_init(&pChn->queueNotFull, NULL);
    if (ret != 0) {
        error("failed to init venc queue-not-full condition for chn %d: %s", pChn->s32ChnId, strerror(ret));
        pthread_cond_destroy(&pChn->queueNotEmpty);
        pthread_mutex_destroy(&pChn->queueLock);
        return ERR_VENC_NOMEM;
    }

    pChn->bTaskRun = MPP_TRUE;
    ret = pthread_create(&pChn->taskTid, NULL, venc_task_thread, pChn);
    if (ret != 0) {
        error("failed to create venc task thread for chn %d: %s", pChn->s32ChnId, strerror(ret));
        pChn->bTaskRun = MPP_FALSE;
        pthread_cond_destroy(&pChn->queueNotFull);
        pthread_cond_destroy(&pChn->queueNotEmpty);
        pthread_mutex_destroy(&pChn->queueLock);
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

        venc_return_one_input_ref(pChn);

        pthread_mutex_lock(&pChn->lock);
        BOOL bRefHeld = MPP_FALSE;
        if (pChn->eState == VENC_CHN_STATE_STARTED) {
            /* The encoder consumes the frame zero-copy and only releases it
             * later via al_enc_return_input_frame(). Hand the VB ref that
             * SYS_RecvFrame added over to a slot and tag the frame with the
             * slot id (u32Idx) so venc_return_one_input_ref() can release it
             * once the encoder is done. */
            S32 frameId = -1;
            ret = venc_hold_input_ref_locked(pChn, ulBuff, MPP_FALSE, &frameId);
            if (ret == ERR_VENC_BUSY) {
                pthread_mutex_unlock(&pChn->lock);
                venc_return_one_input_ref(pChn);
                pthread_mutex_lock(&pChn->lock);
                ret = venc_hold_input_ref_locked(pChn, ulBuff, MPP_FALSE, &frameId);
            }
            if (ret == ERR_VENC_OK) {
                bRefHeld = MPP_TRUE;
                stFrame.u32Idx = (U32)frameId;
                ret = pChn->stOps.send_input_frame(pChn->pAlCtx, &stFrame);
                if (ret != MPP_OK) {
                    error("frame input task: send_input_frame failed %d, chn %d", ret, s32ChnId);
                    /* Encoder never took the frame; drop the held ref now. */
                    venc_release_input_ref_locked(pChn, frameId);
                    bRefHeld = MPP_FALSE;
                }
            } else {
                error("frame input task: no free input ref slot, chn %d", s32ChnId);
            }
        }
        pthread_mutex_unlock(&pChn->lock);

        /* If the ref was handed to a slot, the encoder owns it now and it is
         * released via al_enc_return_input_frame(); otherwise drop the ref
         * that SYS_RecvFrame added. */
        if (!bRefHeld) {
            VB_ReleaseBuffer(ulBuff);
        }
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
        pthread_mutex_init(&g_stChn[i].inputRefLock, NULL);
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
        pthread_mutex_destroy(&g_stChn[i].inputRefLock);
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

    pChn->stAttr = *pstAttr;
    pChn->eState = VENC_CHN_STATE_IDLE;
    memset(pChn->aulInputRefs, 0, sizeof(pChn->aulInputRefs));
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

    venc_plugin_close(pChn);

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

    S32 ret = venc_plugin_open(pChn);
    if (ret != MPP_OK) {
        error("venc_plugin_open failed for chn %d, ret=%d", s32ChnId, ret);
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
        pthread_mutex_lock(&pChn->inputRefLock);
        pthread_mutex_lock(&pChn->lock);
        pChn->bFrameInputRun = MPP_FALSE;
        pChn->stOps.flush(pChn->pAlCtx);
        venc_release_all_input_refs_locked(pChn);
        pChn->eState = VENC_CHN_STATE_IDLE;
        venc_plugin_close(pChn);
        pthread_mutex_unlock(&pChn->lock);
        pthread_mutex_unlock(&pChn->inputRefLock);
        return ERR_VENC_NOMEM;
    }

    /* Start stream output task thread (pulls encoded data, SYS_SendStream, queue) */
    S32 ret2 = venc_start_threads(pChn);
    if (ret2 != ERR_VENC_OK) {
        error("venc_start_threads failed for chn %d, ret=%d", s32ChnId, ret2);
        pChn->bFrameInputRun = MPP_FALSE;
        pthread_join(pChn->frameInputTid, NULL);
        pthread_mutex_lock(&pChn->inputRefLock);
        pthread_mutex_lock(&pChn->lock);
        pChn->stOps.flush(pChn->pAlCtx);
        venc_release_all_input_refs_locked(pChn);
        pChn->eState = VENC_CHN_STATE_IDLE;
        venc_plugin_close(pChn);
        pthread_mutex_unlock(&pChn->lock);
        pthread_mutex_unlock(&pChn->inputRefLock);
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

    pthread_mutex_lock(&pChn->inputRefLock);
    pthread_mutex_lock(&pChn->lock);
    pChn->stOps.flush(pChn->pAlCtx);

    /* Both worker threads are stopped; release any VB refs still held for
     * input frames the encoder never returned. */
    venc_release_all_input_refs_locked(pChn);

    pChn->bBound = MPP_FALSE;
    pChn->eState = VENC_CHN_STATE_IDLE;
    pthread_mutex_unlock(&pChn->lock);
    pthread_mutex_unlock(&pChn->inputRefLock);
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

    pthread_mutex_unlock(&pChn->lock);
    venc_return_one_input_ref(pChn);
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOT_STARTED;
    }

    if (pChn->bBound) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_BUSY;
    }

    /* Send frame to encoder (V4L2 QBUF on OUTPUT port). The linlonv5v7
     * encoder is fully async: hardware encodes the frame after QBUF, and the
     * encoded result appears on the CAPTURE port. Recycle one completed input
     * before queuing a new frame so a plugin-side input-buffer reuse cannot
     * overwrite an old extra_id before its MPI ref slot is released. */
    /* The encoder consumes the frame zero-copy and releases it later via
     * al_enc_return_input_frame(). Hold a VB ref until then so the buffer is
     * not recycled while the hardware is still reading it. u32Idx carries the
     * slot id back through the plugin. */
    S32 ret = ERR_VENC_OK;
    S32 frameId = -1;
    VideoFrameInfo stLocalFrame = *pstFrame;
    if (pstFrame->ulBufferId != 0) {
        ret = venc_hold_input_ref_locked(pChn, pstFrame->ulBufferId, MPP_TRUE, &frameId);
        if (ret == ERR_VENC_BUSY) {
            pthread_mutex_unlock(&pChn->lock);
            venc_return_one_input_ref(pChn);
            pthread_mutex_lock(&pChn->lock);
            ret = venc_hold_input_ref_locked(pChn, pstFrame->ulBufferId, MPP_TRUE, &frameId);
        }
        if (ret != ERR_VENC_OK) {
            pthread_mutex_unlock(&pChn->lock);
            return ret;
        }
        stLocalFrame.u32Idx = (U32)frameId;
    } else {
        /* No VB ref to track; use an out-of-range slot id so the value
         * returned by al_enc_return_input_frame() never matches a live slot. */
        stLocalFrame.u32Idx = VENC_INPUT_REF_SLOTS;
    }

    ret = pChn->stOps.send_input_frame(pChn->pAlCtx, &stLocalFrame);
    if (ret != MPP_OK && frameId >= 0) {
        /* Encoder never took the frame; drop the ref we just held. */
        venc_release_input_ref_locked(pChn, frameId);
    }

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
     * The task thread is the only path that calls al_enc_request_output_stream.
     * The heap output buffer is queued here zero-copy; VENC_ReleaseStream
     * frees it after SYS_SendStream and the app are done.
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
            ts.tv_nsec += (int64_t)(u32TimeoutMs % 1000) * 1000000L;
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
        if (pstStream->ulPrivate) {
            free((void *)pstStream->ulPrivate);
            pstStream->ulPrivate = 0;
        }
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

    /* The CAPTURE buffer was already re-queued inside the plugin;
     * just free the heap output copy carried in ulPrivate. */
    if (pstStream->ulPrivate) {
        free((void *)pstStream->ulPrivate);
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

    /* get_status is optional in the AL interface */
    if (pChn->eState == VENC_CHN_STATE_STARTED && pChn->pAlCtx && pChn->stOps.get_status) {
        pChn->stOps.get_status(pChn->pAlCtx, pstStatus);
    }

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
    pthread_mutex_unlock(&pChn->lock);

    pthread_mutex_lock(&pChn->inputRefLock);
    pthread_mutex_lock(&pChn->lock);
    if (!pChn->bUsed || pChn->eState != VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        pthread_mutex_unlock(&pChn->inputRefLock);
        return ERR_VENC_NOT_STARTED;
    }
    S32 ret = pChn->stOps.flush(pChn->pAlCtx);
    if (ret == MPP_OK) {
        venc_release_all_input_refs_locked(pChn);
    }

    pthread_mutex_unlock(&pChn->lock);
    pthread_mutex_unlock(&pChn->inputRefLock);
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

    /* TODO: implement encoder reset when the AL interface supports it */
    pthread_mutex_unlock(&pChn->lock);
    return ERR_VENC_OK;
}

S32 VENC_SetFrameRate(S32 s32ChnId, S32 s32FrameRate) {
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOT_STARTED;
    }

    VencFrameRateAttr stFrAttr = {.u32FrameRate = (U32)s32FrameRate};
    pChn->stAttr.u32FrameRate = (U32)s32FrameRate;
    S32 ret = pChn->stOps.set_para(pChn->pAlCtx, VENC_CMD_SET_FRAME_RATE, &stFrAttr);
    if (ret != MPP_OK) {
        error("VENC_SetFrameRate failed: chn %d, frameRate %d, ret %d", s32ChnId, s32FrameRate, ret);
    }

    pthread_mutex_unlock(&pChn->lock);
    return (ret == MPP_OK) ? ERR_VENC_OK : ret;
}

S32 VENC_SetRateControl(S32 s32ChnId, VencRcAttr *pstRcAttr) {
    if (!pstRcAttr)
        return ERR_VENC_NULL_PTR;
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOT_STARTED;
    }

    S32 ret = pChn->stOps.set_para(pChn->pAlCtx, VENC_CMD_SET_RATE_CONTROL, pstRcAttr);
    if (ret != MPP_OK) {
        error("VENC_SetRateControl failed: chn %d, ret %d", s32ChnId, ret);
    }

    pthread_mutex_unlock(&pChn->lock);
    return (ret == MPP_OK) ? ERR_VENC_OK : ret;
}

S32 VENC_SetCropAttr(S32 s32ChnId, VencCropAttr *pstCropAttr) {
    if (!pstCropAttr)
        return ERR_VENC_NULL_PTR;
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOT_STARTED;
    }

    S32 ret = pChn->stOps.set_para(pChn->pAlCtx, VENC_CMD_SET_CROP, pstCropAttr);
    if (ret != MPP_OK) {
        error("VENC_SetCropAttr failed: chn %d, crop(%u,%u,%u,%u), ret %d",
            s32ChnId, pstCropAttr->s32Left, pstCropAttr->s32Right,
            pstCropAttr->s32Top, pstCropAttr->s32Bottom, ret);
    }

    pthread_mutex_unlock(&pChn->lock);
    return (ret == MPP_OK) ? ERR_VENC_OK : ret;
}

S32 VENC_SetForceIDR(S32 s32ChnId) {
    if (!venc_chn_valid(s32ChnId))
        return ERR_VENC_INVALID_CHN;

    VencChnCtx *pChn = &g_stChn[s32ChnId];
    pthread_mutex_lock(&pChn->lock);

    if (!pChn->bUsed || pChn->eState != VENC_CHN_STATE_STARTED) {
        pthread_mutex_unlock(&pChn->lock);
        return ERR_VENC_NOT_STARTED;
    }

    S32 ret = pChn->stOps.set_para(pChn->pAlCtx, VENC_CMD_SET_FORCE_IDR, NULL);
    if (ret != MPP_OK) {
        error("VENC_SetForceIDR failed: chn %d, ret %d", s32ChnId, ret);
    }

    pthread_mutex_unlock(&pChn->lock);
    return (ret == MPP_OK) ? ERR_VENC_OK : ret;
}
