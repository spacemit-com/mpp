/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    ai.c
 * @Date      :    2026-11-24
 * @Author    :    SPACEMIT
 * @Brief     :    AI (Audio Input) module: capture and per-channel
 *                 resampling on top of the pcm backend.
 *
 * One capture thread per enabled device reads a period from the backend,
 * timestamps it and hands it to every enabled channel. A channel copies (or
 * resamples) the period into a slot of its own pool; AI_GetFrame lends the
 * slot to the user until AI_ReleaseFrame.
 *
 * Locking: g_stAi.stCtlLock serializes every configuration call. Each
 * channel lock is a leaf that protects the pool, the queue and the channel
 * state against the capture thread and the data path calls.
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "ai_api.h"
#include "aio_internal.h"
#include "aio_resample.h"

#define AI_LOG_ERR(fmt, ...) MPP_LOG(MPP_LOG_ERROR, "ai", fmt, ##__VA_ARGS__)
#define AI_LOG_WARN(fmt, ...) MPP_LOG(MPP_LOG_WARN, "ai", fmt, ##__VA_ARGS__)
#define AI_LOG_INFO(fmt, ...) MPP_LOG(MPP_LOG_INFO, "ai", fmt, ##__VA_ARGS__)

/* A capture clock error beyond this restarts the timeline. */
#define AI_RESYNC_NS (200 * AIO_NS_PER_MS)
/* Share of the measured clock error applied per period. */
#define AI_DRIFT_DIV 32
#define AI_SLOT_INVALID ((U32)-1)

/* ======================== Internal State ======================== */

typedef enum _AiSlotState {
    AI_SLOT_FREE = 0,
    AI_SLOT_QUEUED,
    AI_SLOT_HELD,
} AiSlotState;

typedef struct _AiSlot {
    AiSlotState eState;
    U32 u32Seq;
    U32 u32Flags;
    U64 u64PtsNs;
} AiSlot;

/* Frame memory of one channel generation. It outlives the channel while the
 * user still holds frames, so that their pointers stay valid. */
typedef struct _AiPool {
    struct _AiPool *pstNext; /* list of pools orphaned by DisableChn */
    U64 u64Epoch;
    U32 u32SlotCnt;
    U32 u32SlotBytes;
    U32 u32HeldCnt;
    U32 u32QueueCap;
    U32 u32QueueHead;
    U32 u32QueueCnt;
    AiSlot *pstSlots;
    U32 *pu32Queue; /* slot ids in capture order */
    U8 *pu8Mem;
} AiPool;

typedef struct _AiChnCtx {
    pthread_mutex_t stLock;
    pthread_cond_t stCond;
    BOOL bEnabled;
    BOOL bLost; /* the device disappeared under the channel */
    U32 u32Waiters;
    U64 u64Epoch; /* bumped by every EnableChn */
    AiPool *pstPool;
    AiPool *pstOrphans;
    U32 u32UsrDepth;

    /* Resampling request, set while the channel is disabled. */
    BOOL bReSmp;
    AudioSampleRate eReSmpRate;

    /* Output format, fixed at EnableChn. */
    AudioBitWidth eBitWidth;
    AudioPcmFormat eFormat;
    U32 u32Channels;
    U32 u32DevRate;
    U32 u32OutRate;
    U32 u32OutFrames; /* sample points per delivered frame */

    AioResampler *pstRs;
    float *pfFifo; /* resampled frames not yet cut into a slot */
    U32 u32FifoCap;
    U32 u32FifoLen;

    /* Position on the device timeline, capture thread only. */
    U64 u64TlGen;
    U64 u64InBase;
    U64 u64OutCnt;
    BOOL bDiscont;
    U32 u32Seq;

    U64 u64Dropped;
    U32 u32PeakQueued;
} AiChnCtx;

typedef struct _AiDevCtx {
    BOOL bAttrSet;
    AioDevAttrS stAttr;
    AudioPcmFormat eFormat;
    BOOL bMicSet;
    MicAttrS stMic;

    BOOL bEnabled;
    AioAlPcm *pstPcm;
    pthread_t stThread;
    atomic_bool bRun;
    atomic_bool bLost;
    S32 s32WakeFd;
    U32 u32FrameBytes;
    U8 *pu8Raw;
    float *pfRaw;
    BOOL bRawFloat;

    /* Timeline, capture thread only. Frame 0 was captured at u64AnchorNs. */
    BOOL bAnchored;
    U64 u64AnchorNs;
    U64 u64Frames;
    U64 u64TlGen;

    atomic_ullong u64Xruns;
    U64 u64LogNs;
    U32 u32LogSkip;

    AiChnCtx astChn[AI_MAX_CHN_NUM];
} AiDevCtx;

typedef struct _AiModCtx {
    pthread_mutex_t stCtlLock;
    atomic_bool bInited;
    const AioAlOps *pstOps;
    AiDevCtx astDev[AI_MAX_DEV_NUM];
} AiModCtx;

static AiModCtx g_stAi = {.stCtlLock = PTHREAD_MUTEX_INITIALIZER};
static pthread_once_t g_stAiOnce = PTHREAD_ONCE_INIT;

/* ======================== Validation Helpers ======================== */

#define AI_CHECK_DEV(dev)                              \
    do {                                               \
        if (((dev) < 0) || ((dev) >= AI_MAX_DEV_NUM)) { \
            AI_LOG_ERR("invalid dev %d", (dev));       \
            return SYS_ERR_INVAL;                      \
        }                                              \
    } while (0)

#define AI_CHECK_CHN(chn)                              \
    do {                                               \
        if (((chn) < 0) || ((chn) >= AI_MAX_CHN_NUM)) { \
            AI_LOG_ERR("invalid chn %d", (chn));       \
            return SYS_ERR_INVAL;                      \
        }                                              \
    } while (0)

#define AI_CHECK_POINTER(ptr)           \
    do {                                \
        if (NULL == (ptr)) {            \
            AI_LOG_ERR("null pointer"); \
            return SYS_ERR_INVAL;       \
        }                               \
    } while (0)

#define AI_CHECK_INIT()                         \
    do {                                        \
        if (!atomic_load(&g_stAi.bInited)) {    \
            AI_LOG_ERR("not initialized");      \
            return SYS_ERR_NOT_INIT;            \
        }                                       \
    } while (0)

static VOID ai_init_once(VOID) {
    S32 d, c;

    for (d = 0; d < AI_MAX_DEV_NUM; d++) {
        g_stAi.astDev[d].s32WakeFd = -1;
        for (c = 0; c < AI_MAX_CHN_NUM; c++) {
            pthread_mutex_init(&g_stAi.astDev[d].astChn[c].stLock, NULL);
            aio_cond_init(&g_stAi.astDev[d].astChn[c].stCond);
        }
    }
}

/* Take the control lock; fails when the module is not initialized. */
static S32 ai_ctl_enter(VOID) {
    pthread_once(&g_stAiOnce, ai_init_once);
    pthread_mutex_lock(&g_stAi.stCtlLock);
    if (!atomic_load(&g_stAi.bInited)) {
        pthread_mutex_unlock(&g_stAi.stCtlLock);
        AI_LOG_ERR("not initialized");
        return SYS_ERR_NOT_INIT;
    }

    return SYS_ERR_OK;
}

#define AI_CTL_LEAVE(ret)                          \
    do {                                           \
        pthread_mutex_unlock(&g_stAi.stCtlLock);   \
        return (ret);                              \
    } while (0)

/* ======================== Pool ======================== */

static AiPool *ai_pool_create(U32 u32SlotCnt, U32 u32QueueCap, U32 u32SlotBytes, U64 u64Epoch) {
    size_t szSlots = (size_t)u32SlotCnt * sizeof(AiSlot);
    size_t szQueue = (size_t)u32QueueCap * sizeof(U32);
    AiPool *pstPool = (AiPool *)calloc(1, sizeof(AiPool) + szSlots + szQueue + (size_t)u32SlotCnt * u32SlotBytes);

    if (pstPool == NULL)
        return NULL;
    pstPool->u64Epoch = u64Epoch;
    pstPool->u32SlotCnt = u32SlotCnt;
    pstPool->u32SlotBytes = u32SlotBytes;
    pstPool->u32QueueCap = u32QueueCap;
    pstPool->pstSlots = (AiSlot *)(pstPool + 1);
    pstPool->pu32Queue = (U32 *)((U8 *)pstPool->pstSlots + szSlots);
    pstPool->pu8Mem = (U8 *)pstPool->pu32Queue + szQueue;

    return pstPool;
}

/* A free slot for a new frame. A full queue first gives up its oldest
 * pending frame; frames the user holds are never touched. */
static U32 ai_pool_get_slot(AiChnCtx *pstChn) {
    AiPool *pstPool = pstChn->pstPool;
    U32 i;

    if (pstPool->u32QueueCnt == pstPool->u32QueueCap) {
        U32 u32Old = pstPool->pu32Queue[pstPool->u32QueueHead];

        pstPool->u32QueueHead = (pstPool->u32QueueHead + 1) % pstPool->u32QueueCap;
        pstPool->u32QueueCnt--;
        pstPool->pstSlots[u32Old].eState = AI_SLOT_FREE;
        pstChn->u64Dropped++;
    }
    for (i = 0; i < pstPool->u32SlotCnt; i++) {
        if (pstPool->pstSlots[i].eState == AI_SLOT_FREE)
            return i;
    }

    return AI_SLOT_INVALID;
}

/* ======================== Capture Path ======================== */

/* Queue one frame, taken from device PCM or from resampled float samples. */
static VOID ai_chn_emit(AiChnCtx *pstChn, const U8 *pu8Pcm, const float *pfSamples, U64 u64PtsNs) {
    AiPool *pstPool = pstChn->pstPool;
    U32 u32Slot = ai_pool_get_slot(pstChn);
    U32 u32Seq = pstChn->u32Seq++;
    AiSlot *pstSlot;
    U8 *pu8Dst;

    if (u32Slot == AI_SLOT_INVALID) {
        pstChn->u64Dropped++;
        return;
    }
    pu8Dst = pstPool->pu8Mem + (size_t)u32Slot * pstPool->u32SlotBytes;
    if (pu8Pcm != NULL)
        memcpy(pu8Dst, pu8Pcm, pstPool->u32SlotBytes);
    else
        aio_float_to_pcm(pfSamples, pu8Dst, pstChn->u32OutFrames * pstChn->u32Channels, pstChn->eFormat);

    pstSlot = &pstPool->pstSlots[u32Slot];
    pstSlot->eState = AI_SLOT_QUEUED;
    pstSlot->u32Seq = u32Seq;
    pstSlot->u64PtsNs = u64PtsNs;
    pstSlot->u32Flags = pstChn->bDiscont ? AUDIO_FRAME_DISCONTINUITY : 0;
    pstChn->bDiscont = MPP_FALSE;

    pstPool->pu32Queue[(pstPool->u32QueueHead + pstPool->u32QueueCnt) % pstPool->u32QueueCap] = u32Slot;
    pstPool->u32QueueCnt++;
    if (pstPool->u32QueueCnt > pstChn->u32PeakQueued)
        pstChn->u32PeakQueued = pstPool->u32QueueCnt;
    pthread_cond_broadcast(&pstChn->stCond);
}

/* Feed one device period to a channel. Called with the channel locked. */
static VOID ai_chn_push(AiDevCtx *pstDev, AiChnCtx *pstChn) {
    const U32 u32Period = pstDev->stAttr.u32PtNumPerFrm;
    const U32 u32Ch = pstChn->u32Channels;

    if (pstChn->u64TlGen != pstDev->u64TlGen) {
        /* The device timeline restarted (or the channel just joined it). */
        if (pstChn->u64TlGen != 0)
            pstChn->bDiscont = MPP_TRUE;
        pstChn->u64TlGen = pstDev->u64TlGen;
        pstChn->u64InBase = pstDev->u64Frames;
        pstChn->u64OutCnt = 0;
        if (pstChn->pstRs != NULL) {
            aio_resampler_reset(pstChn->pstRs);
            pstChn->u32FifoLen = 0;
        }
    }

    if (pstChn->pstRs == NULL) {
        ai_chn_emit(pstChn, pstDev->pu8Raw, NULL,
            pstDev->u64AnchorNs + aio_frames_to_ns(pstDev->u64Frames, pstChn->u32DevRate));
        return;
    }

    if (!pstDev->bRawFloat) {
        aio_pcm_to_float(pstDev->pu8Raw, pstDev->pfRaw, u32Period * u32Ch, pstDev->eFormat);
        pstDev->bRawFloat = MPP_TRUE;
    }
    pstChn->u32FifoLen += aio_resampler_process(pstChn->pstRs, pstDev->pfRaw, u32Period,
        pstChn->pfFifo + (size_t)pstChn->u32FifoLen * u32Ch, pstChn->u32FifoCap - pstChn->u32FifoLen);

    while (pstChn->u32FifoLen >= pstChn->u32OutFrames) {
        /* Output frame k is the signal at input time k * in / out. */
        U64 u64PtsNs = pstDev->u64AnchorNs + aio_frames_to_ns(pstChn->u64InBase, pstChn->u32DevRate) +
            aio_frames_to_ns(pstChn->u64OutCnt, pstChn->u32OutRate);

        ai_chn_emit(pstChn, NULL, pstChn->pfFifo, u64PtsNs);
        pstChn->u64OutCnt += pstChn->u32OutFrames;
        pstChn->u32FifoLen -= pstChn->u32OutFrames;
        memmove(pstChn->pfFifo, pstChn->pfFifo + (size_t)pstChn->u32OutFrames * u32Ch,
            (size_t)pstChn->u32FifoLen * u32Ch * sizeof(float));
    }
}

/* Relate the period about to be read to CLOCK_MONOTONIC. */
static VOID ai_dev_stamp(AiDevCtx *pstDev) {
    const U32 u32Rate = (U32)pstDev->stAttr.eSampleRate;
    const U64 u64PeriodNs = aio_frames_to_ns(pstDev->stAttr.u32PtNumPerFrm, u32Rate);
    AioAlPcmStatus stStatus;
    U64 u64Measured;
    S64 s64Err, s64Step, s64Max;

    memset(&stStatus, 0, sizeof(stStatus));
    if (g_stAi.pstOps->pcm_get_status(pstDev->pstPcm, &stStatus) == AIO_AL_OK && stStatus.bTimestampValid)
        /* The oldest unread frame was captured avail frames before the stamp. */
        u64Measured = stStatus.u64TimestampNs - aio_frames_to_ns(stStatus.u32AvailFrames, u32Rate);
    else
        u64Measured = aio_mono_ns() - u64PeriodNs;

    if (pstDev->bAnchored) {
        s64Err = (S64)(u64Measured - (pstDev->u64AnchorNs + aio_frames_to_ns(pstDev->u64Frames, u32Rate)));
        if (s64Err <= (S64)AI_RESYNC_NS && s64Err >= -(S64)AI_RESYNC_NS) {
            /* Follow the device clock slowly; never by enough to reorder PTS. */
            s64Step = s64Err / AI_DRIFT_DIV;
            s64Max = (S64)(u64PeriodNs / 8);
            if (s64Step > s64Max)
                s64Step = s64Max;
            else if (s64Step < -s64Max)
                s64Step = -s64Max;
            pstDev->u64AnchorNs = (U64)((S64)pstDev->u64AnchorNs + s64Step);
            return;
        }
        AI_LOG_WARN("capture clock jumped by %lld ms, timeline restarted", (long long)(s64Err / (S64)AIO_NS_PER_MS));
    }
    pstDev->bAnchored = MPP_TRUE;
    pstDev->u64AnchorNs = u64Measured;
    pstDev->u64Frames = 0;
    pstDev->u64TlGen++;
}

/* Returns MPP_TRUE when capture can go on. */
static BOOL ai_dev_recover(AiDevCtx *pstDev, S32 s32Err) {
    const AioAlOps *pstOps = g_stAi.pstOps;
    S32 c;

    if (s32Err == AIO_AL_ERR_XRUN || s32Err == AIO_AL_ERR_SUSPEND) {
        S32 s32Ret;

        atomic_fetch_add(&pstDev->u64Xruns, 1);
        if (aio_log_allow(&pstDev->u64LogNs, &pstDev->u32LogSkip)) {
            AI_LOG_WARN("capture %s, recovering (%u more not logged)",
                s32Err == AIO_AL_ERR_XRUN ? "overrun" : "suspended", pstDev->u32LogSkip);
            pstDev->u32LogSkip = 0;
        }
        s32Ret = pstOps->pcm_recover(pstDev->pstPcm, s32Err);
        if (s32Ret == AIO_AL_OK)
            s32Ret = pstOps->pcm_start(pstDev->pstPcm);
        if (s32Ret == AIO_AL_OK) {
            pstDev->bAnchored = MPP_FALSE;
            return MPP_TRUE;
        }
        s32Err = s32Ret;
    }

    AI_LOG_ERR("capture device lost (backend error %d)", s32Err);
    atomic_store(&pstDev->bLost, MPP_TRUE);
    for (c = 0; c < AI_MAX_CHN_NUM; c++) {
        AiChnCtx *pstChn = &pstDev->astChn[c];

        pthread_mutex_lock(&pstChn->stLock);
        if (pstChn->bEnabled) {
            pstChn->bLost = MPP_TRUE;
            pthread_cond_broadcast(&pstChn->stCond);
        }
        pthread_mutex_unlock(&pstChn->stLock);
    }

    return MPP_FALSE;
}

static VOID *ai_capture_thread(VOID *pArg) {
    AiDevCtx *pstDev = (AiDevCtx *)pArg;
    const AioAlOps *pstOps = g_stAi.pstOps;
    const U32 u32Period = pstDev->stAttr.u32PtNumPerFrm;
    char szName[16];
    S32 c;

    snprintf(szName, sizeof(szName), "mpp_ai%d", (S32)(pstDev - g_stAi.astDev));
    pthread_setname_np(pthread_self(), szName);

    while (atomic_load(&pstDev->bRun)) {
        BOOL bStamped = MPP_FALSE;
        S32 s32Ret = AIO_AL_OK;
        U32 u32Got = 0;

        while (u32Got < u32Period && atomic_load(&pstDev->bRun)) {
            s32Ret = pstOps->pcm_wait(pstDev->pstPcm, u32Period - u32Got, pstDev->s32WakeFd, AIO_WAIT_SLICE_MS);
            if (s32Ret == AIO_AL_ERR_TIMEOUT) {
                s32Ret = AIO_AL_OK;
                continue;
            }
            if (s32Ret != AIO_AL_OK)
                break;
            if (!bStamped) {
                ai_dev_stamp(pstDev);
                bStamped = MPP_TRUE;
            }
            s32Ret = pstOps->pcm_read(pstDev->pstPcm, pstDev->pu8Raw + (size_t)u32Got * pstDev->u32FrameBytes,
                u32Period - u32Got);
            if (s32Ret == AIO_AL_ERR_AGAIN) {
                s32Ret = AIO_AL_OK;
                continue;
            }
            if (s32Ret < 0)
                break;
            u32Got += (U32)s32Ret;
            s32Ret = AIO_AL_OK;
        }
        if (!atomic_load(&pstDev->bRun) || s32Ret == AIO_AL_ERR_WAKE)
            continue;
        if (s32Ret != AIO_AL_OK) {
            if (!ai_dev_recover(pstDev, s32Ret))
                break;
            continue;
        }

        pstDev->bRawFloat = MPP_FALSE;
        for (c = 0; c < AI_MAX_CHN_NUM; c++) {
            AiChnCtx *pstChn = &pstDev->astChn[c];

            pthread_mutex_lock(&pstChn->stLock);
            if (pstChn->bEnabled)
                ai_chn_push(pstDev, pstChn);
            pthread_mutex_unlock(&pstChn->stLock);
        }
        pstDev->u64Frames += u32Period;
    }

    return NULL;
}

/* ======================== Channel / Device Teardown ======================== */

static VOID ai_chn_release_resources(AiChnCtx *pstChn) {
    AiPool *pstPool = pstChn->pstPool;

    if (pstPool != NULL) {
        if (pstPool->u32HeldCnt > 0) {
            /* Keep the memory until the user returns the frames. */
            pstPool->pstNext = pstChn->pstOrphans;
            pstChn->pstOrphans = pstPool;
        } else {
            free(pstPool);
        }
        pstChn->pstPool = NULL;
    }
    aio_resampler_destroy(pstChn->pstRs);
    pstChn->pstRs = NULL;
    free(pstChn->pfFifo);
    pstChn->pfFifo = NULL;
    pstChn->u32FifoLen = 0;
}

static VOID ai_chn_disable(AiChnCtx *pstChn) {
    pthread_mutex_lock(&pstChn->stLock);
    if (pstChn->bEnabled) {
        pstChn->bEnabled = MPP_FALSE;
        pthread_cond_broadcast(&pstChn->stCond);
        while (pstChn->u32Waiters > 0)
            pthread_cond_wait(&pstChn->stCond, &pstChn->stLock);
        ai_chn_release_resources(pstChn);
    }
    pthread_mutex_unlock(&pstChn->stLock);
}

static VOID ai_dev_close(AiDevCtx *pstDev) {
    if (pstDev->pstPcm != NULL) {
        g_stAi.pstOps->pcm_stop(pstDev->pstPcm);
        g_stAi.pstOps->pcm_close(pstDev->pstPcm);
        pstDev->pstPcm = NULL;
    }
    if (pstDev->s32WakeFd >= 0) {
        close(pstDev->s32WakeFd);
        pstDev->s32WakeFd = -1;
    }
    free(pstDev->pu8Raw);
    pstDev->pu8Raw = NULL;
    free(pstDev->pfRaw);
    pstDev->pfRaw = NULL;
}

static VOID ai_dev_disable(AiDevCtx *pstDev) {
    uint64_t u64One = 1;
    S32 c;

    atomic_store(&pstDev->bRun, MPP_FALSE);
    if (write(pstDev->s32WakeFd, &u64One, sizeof(u64One)) < 0)
        AI_LOG_WARN("wake fd write failed: %s", strerror(errno));
    pthread_join(pstDev->stThread, NULL);

    for (c = 0; c < AI_MAX_CHN_NUM; c++)
        ai_chn_disable(&pstDev->astChn[c]);
    ai_dev_close(pstDev);
    pstDev->bEnabled = MPP_FALSE;
}

static VOID ai_dev_reset_cfg(AiDevCtx *pstDev) {
    S32 c;

    pstDev->bAttrSet = MPP_FALSE;
    pstDev->bMicSet = MPP_FALSE;
    memset(&pstDev->stAttr, 0, sizeof(pstDev->stAttr));
    memset(&pstDev->stMic, 0, sizeof(pstDev->stMic));
    for (c = 0; c < AI_MAX_CHN_NUM; c++) {
        AiChnCtx *pstChn = &pstDev->astChn[c];

        pthread_mutex_lock(&pstChn->stLock);
        pstChn->bReSmp = MPP_FALSE;
        while (pstChn->pstOrphans != NULL) {
            AiPool *pstPool = pstChn->pstOrphans;

            AI_LOG_WARN("%u frame(s) never released, their memory is freed now", pstPool->u32HeldCnt);
            pstChn->pstOrphans = pstPool->pstNext;
            free(pstPool);
        }
        pthread_mutex_unlock(&pstChn->stLock);
    }
}

/* ======================== Module / Device ======================== */

S32 AI_Init(VOID) {
    S32 ret, d;

    pthread_once(&g_stAiOnce, ai_init_once);
    pthread_mutex_lock(&g_stAi.stCtlLock);
    if (atomic_load(&g_stAi.bInited))
        AI_CTL_LEAVE(SYS_ERR_OK);

    ret = aio_backend_acquire(&g_stAi.pstOps);
    if (ret != SYS_ERR_OK)
        AI_CTL_LEAVE(ret);
    for (d = 0; d < AI_MAX_DEV_NUM; d++)
        ai_dev_reset_cfg(&g_stAi.astDev[d]);
    atomic_store(&g_stAi.bInited, MPP_TRUE);

    AI_CTL_LEAVE(SYS_ERR_OK);
}

S32 AI_DeInit(VOID) {
    S32 d;

    pthread_once(&g_stAiOnce, ai_init_once);
    pthread_mutex_lock(&g_stAi.stCtlLock);
    if (!atomic_load(&g_stAi.bInited))
        AI_CTL_LEAVE(SYS_ERR_OK);

    for (d = 0; d < AI_MAX_DEV_NUM; d++) {
        if (g_stAi.astDev[d].bEnabled) {
            AI_LOG_ERR("dev %d is still enabled", d);
            AI_CTL_LEAVE(SYS_ERR_BUSY);
        }
    }
    atomic_store(&g_stAi.bInited, MPP_FALSE);
    for (d = 0; d < AI_MAX_DEV_NUM; d++)
        ai_dev_reset_cfg(&g_stAi.astDev[d]);
    aio_backend_release();
    g_stAi.pstOps = NULL;

    AI_CTL_LEAVE(SYS_ERR_OK);
}

S32 AI_SetDevAttr(AI_DEV AiDev, const AioDevAttrS *pstDevAttr) {
    AudioPcmFormat eFormat;
    AiDevCtx *pstDev;
    S32 ret;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    AI_CHECK_POINTER(pstDevAttr);
    ret = ai_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAi.astDev[AiDev];
    if (pstDev->bEnabled) {
        AI_LOG_ERR("dev %d is enabled, attribute is read-only", AiDev);
        AI_CTL_LEAVE(SYS_ERR_BUSY);
    }
    ret = aio_check_dev_attr("ai", pstDevAttr, &eFormat);
    if (ret != SYS_ERR_OK)
        AI_CTL_LEAVE(ret);
    pstDev->stAttr = *pstDevAttr;
    pstDev->eFormat = eFormat;
    pstDev->bAttrSet = MPP_TRUE;

    AI_CTL_LEAVE(SYS_ERR_OK);
}

S32 AI_GetDevAttr(AI_DEV AiDev, AioDevAttrS *pstDevAttr) {
    AiDevCtx *pstDev;
    S32 ret;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    AI_CHECK_POINTER(pstDevAttr);
    ret = ai_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAi.astDev[AiDev];
    if (!pstDev->bAttrSet) {
        AI_LOG_ERR("dev %d has no attribute yet", AiDev);
        AI_CTL_LEAVE(SYS_ERR_INVAL);
    }
    *pstDevAttr = pstDev->stAttr;

    AI_CTL_LEAVE(SYS_ERR_OK);
}

S32 AI_SetMicAttr(AI_DEV AiDev, const MicAttrS *pstMicAttr) {
    AiDevCtx *pstDev;
    S32 ret;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    AI_CHECK_POINTER(pstMicAttr);
    ret = ai_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAi.astDev[AiDev];
    if (pstDev->bEnabled) {
        AI_LOG_ERR("dev %d is enabled, MIC routing cannot change", AiDev);
        AI_CTL_LEAVE(SYS_ERR_INVAL);
    }
    if (pstMicAttr->stMicAttr.s32Dev < 0 || pstMicAttr->stMicAttr.s32Chns < 1 ||
        pstMicAttr->stMicAttr.s32Chns > (S32)AUDIO_MAX_PCM_CHANNELS) {
        AI_LOG_ERR("invalid MIC routing: card %d, %d channel(s)", pstMicAttr->stMicAttr.s32Dev,
            pstMicAttr->stMicAttr.s32Chns);
        AI_CTL_LEAVE(SYS_ERR_INVAL);
    }
    pstDev->stMic = *pstMicAttr;
    pstDev->bMicSet = MPP_TRUE;

    AI_CTL_LEAVE(SYS_ERR_OK);
}

S32 AI_GetMicAttr(AI_DEV AiDev, MicAttrS *pstMicAttr) {
    AiDevCtx *pstDev;
    S32 ret;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    AI_CHECK_POINTER(pstMicAttr);
    ret = ai_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAi.astDev[AiDev];
    if (pstDev->bMicSet) {
        *pstMicAttr = pstDev->stMic;
    } else {
        memset(pstMicAttr, 0, sizeof(*pstMicAttr));
        pstMicAttr->stMicAttr.s32Chns = 1;
    }

    AI_CTL_LEAVE(SYS_ERR_OK);
}

S32 AI_EnableDev(AI_DEV AiDev) {
    AioAlPcmCfg stCfg, stActual;
    AiDevCtx *pstDev;
    S32 ret, s32AlRet;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    ret = ai_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAi.astDev[AiDev];
    if (pstDev->bEnabled)
        AI_CTL_LEAVE(SYS_ERR_OK);
    if (!pstDev->bAttrSet) {
        AI_LOG_ERR("dev %d: call AI_SetDevAttr first", AiDev);
        AI_CTL_LEAVE(SYS_ERR_INVAL);
    }
    if (pstDev->bMicSet && (U32)pstDev->stMic.stMicAttr.s32Chns != pstDev->stAttr.u32ChnCnt) {
        AI_LOG_ERR("dev %d: MIC routing has %d channel(s) but u32ChnCnt is %u", AiDev,
            pstDev->stMic.stMicAttr.s32Chns, pstDev->stAttr.u32ChnCnt);
        AI_CTL_LEAVE(SYS_ERR_INVAL);
    }

    memset(&stCfg, 0, sizeof(stCfg));
    stCfg.eDir = AIO_AL_DIR_CAPTURE;
    stCfg.s32Card = pstDev->bMicSet ? pstDev->stMic.stMicAttr.s32Dev : 0;
    stCfg.u32Rate = (U32)pstDev->stAttr.eSampleRate;
    stCfg.u32Channels = pstDev->stAttr.u32ChnCnt;
    stCfg.ePcmFormat = pstDev->eFormat;
    stCfg.u32PeriodFrames = pstDev->stAttr.u32PtNumPerFrm;
    stCfg.u32BufferFrames = pstDev->stAttr.u32FrmNum * pstDev->stAttr.u32PtNumPerFrm;
    stActual = stCfg;

    s32AlRet = g_stAi.pstOps->pcm_open(&stCfg, &pstDev->pstPcm, &stActual);
    if (s32AlRet != AIO_AL_OK) {
        pstDev->pstPcm = NULL;
        AI_LOG_ERR("dev %d: cannot open capture card %d (%u Hz, %u ch, format %d): backend error %d", AiDev,
            stCfg.s32Card, stCfg.u32Rate, stCfg.u32Channels, (S32)stCfg.ePcmFormat, s32AlRet);
        AI_CTL_LEAVE(aio_backend_err_to_sys(s32AlRet));
    }

    pstDev->u32FrameBytes = stCfg.u32Channels * aio_format_bytes(stCfg.ePcmFormat);
    pstDev->pu8Raw = (U8 *)calloc(stCfg.u32PeriodFrames, pstDev->u32FrameBytes);
    pstDev->pfRaw = (float *)calloc((size_t)stCfg.u32PeriodFrames * stCfg.u32Channels, sizeof(float));
    pstDev->s32WakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (pstDev->pu8Raw == NULL || pstDev->pfRaw == NULL || pstDev->s32WakeFd < 0) {
        AI_LOG_ERR("dev %d: out of memory", AiDev);
        ai_dev_close(pstDev);
        AI_CTL_LEAVE(SYS_ERR_NOMEM);
    }

    pstDev->bAnchored = MPP_FALSE;
    pstDev->u64Frames = 0;
    pstDev->u64TlGen = 0;
    atomic_store(&pstDev->u64Xruns, 0);
    atomic_store(&pstDev->bLost, MPP_FALSE);
    atomic_store(&pstDev->bRun, MPP_TRUE);

    s32AlRet = g_stAi.pstOps->pcm_start(pstDev->pstPcm);
    if (s32AlRet != AIO_AL_OK) {
        AI_LOG_ERR("dev %d: cannot start capture: backend error %d", AiDev, s32AlRet);
        ai_dev_close(pstDev);
        AI_CTL_LEAVE(aio_backend_err_to_sys(s32AlRet));
    }
    if (pthread_create(&pstDev->stThread, NULL, ai_capture_thread, pstDev) != 0) {
        AI_LOG_ERR("dev %d: cannot create capture thread", AiDev);
        ai_dev_close(pstDev);
        AI_CTL_LEAVE(SYS_ERR_NOMEM);
    }
    pstDev->bEnabled = MPP_TRUE;
    AI_LOG_INFO("dev %d enabled: card %d, %u Hz, %u ch, format %d, frame %u, device period %u, buffer %u", AiDev,
        stCfg.s32Card, stCfg.u32Rate, stCfg.u32Channels, (S32)stCfg.ePcmFormat, stCfg.u32PeriodFrames,
        stActual.u32PeriodFrames, stActual.u32BufferFrames);

    AI_CTL_LEAVE(SYS_ERR_OK);
}

S32 AI_DisableDev(AI_DEV AiDev) {
    AiDevCtx *pstDev;
    S32 ret;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    ret = ai_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAi.astDev[AiDev];
    if (pstDev->bEnabled) {
        ai_dev_disable(pstDev);
        AI_LOG_INFO("dev %d disabled", AiDev);
    }

    AI_CTL_LEAVE(SYS_ERR_OK);
}

/* ======================== Channel ======================== */

S32 AI_EnableChn(AI_DEV AiDev, AI_CHN AiChn) {
    U32 u32DevRate, u32OutRate, u32OutFrames, u32Period, u32SlotBytes, u32Ch;
    AiDevCtx *pstDev;
    AiChnCtx *pstChn;
    S32 ret;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    AI_CHECK_CHN(AiChn);
    ret = ai_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAi.astDev[AiDev];
    pstChn = &pstDev->astChn[AiChn];
    if (!pstDev->bEnabled) {
        AI_LOG_ERR("dev %d is not enabled", AiDev);
        AI_CTL_LEAVE(SYS_ERR_NOT_INIT);
    }
    if (atomic_load(&pstDev->bLost)) {
        AI_LOG_ERR("dev %d was lost, disable and enable it again", AiDev);
        AI_CTL_LEAVE(SYS_ERR_NOT_FOUND);
    }

    u32Period = pstDev->stAttr.u32PtNumPerFrm;
    u32Ch = pstDev->stAttr.u32ChnCnt;
    u32DevRate = (U32)pstDev->stAttr.eSampleRate;

    pthread_mutex_lock(&pstChn->stLock);
    if (pstChn->bEnabled) {
        pthread_mutex_unlock(&pstChn->stLock);
        AI_CTL_LEAVE(SYS_ERR_OK);
    }

    u32OutRate = pstChn->bReSmp ? (U32)pstChn->eReSmpRate : u32DevRate;
    u32OutFrames = u32Period;
    ret = SYS_ERR_OK;
    if (u32OutRate != u32DevRate) {
        u32OutFrames = aio_scale_frames(u32Period, u32DevRate, u32OutRate);
        if (!aio_format_resamplable(pstDev->eFormat)) {
            AI_LOG_ERR("chn %d: pcm format %d cannot be resampled", AiChn, (S32)pstDev->eFormat);
            ret = SYS_ERR_INVAL;
        } else if (u32OutFrames < 1 || u32OutFrames > MAX_AIO_POINT_NUM ||
            (U64)u32OutFrames * pstDev->u32FrameBytes > MAX_AUDIO_FRAME_LEN) {
            AI_LOG_ERR("chn %d: %u Hz -> %u Hz gives a frame of %u points, out of range", AiChn, u32DevRate,
                u32OutRate, u32OutFrames);
            ret = SYS_ERR_INVAL;
        }
    }
    if (ret != SYS_ERR_OK) {
        pthread_mutex_unlock(&pstChn->stLock);
        AI_CTL_LEAVE(ret);
    }

    u32SlotBytes = u32OutFrames * pstDev->u32FrameBytes;
    pstChn->u32UsrDepth = AIO_AI_USR_FRM_DEPTH;
    pstChn->pstPool = ai_pool_create(AIO_AI_QUEUE_FRAMES + AIO_AI_USR_FRM_DEPTH, AIO_AI_QUEUE_FRAMES, u32SlotBytes,
        pstChn->u64Epoch + 1);
    if (pstChn->pstPool != NULL && u32OutRate != u32DevRate) {
        pstChn->pstRs = aio_resampler_create(u32DevRate, u32OutRate, u32Ch, u32Period);
        if (pstChn->pstRs != NULL) {
            pstChn->u32FifoCap = u32OutFrames + aio_resampler_max_out(pstChn->pstRs, u32Period);
            pstChn->pfFifo = (float *)calloc((size_t)pstChn->u32FifoCap * u32Ch, sizeof(float));
        }
    }
    if (pstChn->pstPool == NULL || (u32OutRate != u32DevRate && (pstChn->pstRs == NULL || pstChn->pfFifo == NULL))) {
        AI_LOG_ERR("chn %d: out of memory", AiChn);
        ai_chn_release_resources(pstChn);
        pthread_mutex_unlock(&pstChn->stLock);
        AI_CTL_LEAVE(SYS_ERR_NOMEM);
    }

    pstChn->u64Epoch++;
    pstChn->eBitWidth = pstDev->stAttr.eBitWidth;
    pstChn->eFormat = pstDev->eFormat;
    pstChn->u32Channels = u32Ch;
    pstChn->u32DevRate = u32DevRate;
    pstChn->u32OutRate = u32OutRate;
    pstChn->u32OutFrames = u32OutFrames;
    pstChn->u32FifoLen = 0;
    pstChn->u64TlGen = 0;
    pstChn->bDiscont = MPP_FALSE;
    pstChn->u32Seq = 0;
    pstChn->u64Dropped = 0;
    pstChn->u32PeakQueued = 0;
    pstChn->bLost = MPP_FALSE;
    pstChn->bEnabled = MPP_TRUE;
    pthread_mutex_unlock(&pstChn->stLock);
    AI_LOG_INFO("dev %d chn %d enabled: %u Hz, %u points per frame", AiDev, AiChn, u32OutRate, u32OutFrames);

    AI_CTL_LEAVE(SYS_ERR_OK);
}

S32 AI_DisableChn(AI_DEV AiDev, AI_CHN AiChn) {
    S32 ret;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    AI_CHECK_CHN(AiChn);
    ret = ai_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    ai_chn_disable(&g_stAi.astDev[AiDev].astChn[AiChn]);

    AI_CTL_LEAVE(SYS_ERR_OK);
}

S32 AI_QueryChnStat(AI_DEV AiDev, AI_CHN AiChn, AiChnStateS *pstStatus) {
    AiChnCtx *pstChn;
    U32 i;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    AI_CHECK_CHN(AiChn);
    AI_CHECK_POINTER(pstStatus);
    pthread_once(&g_stAiOnce, ai_init_once);

    pstChn = &g_stAi.astDev[AiDev].astChn[AiChn];
    pthread_mutex_lock(&pstChn->stLock);
    if (!pstChn->bEnabled) {
        pthread_mutex_unlock(&pstChn->stLock);
        AI_LOG_ERR("dev %d chn %d is not enabled", AiDev, AiChn);
        return SYS_ERR_NOT_INIT;
    }
    memset(pstStatus, 0, sizeof(*pstStatus));
    pstStatus->u32ChnTotalNum = pstChn->pstPool->u32SlotCnt;
    for (i = 0; i < pstChn->pstPool->u32SlotCnt; i++) {
        if (pstChn->pstPool->pstSlots[i].eState == AI_SLOT_FREE)
            pstStatus->u32ChnFreeNum++;
    }
    pstStatus->u32ChnBusyNum = pstChn->pstPool->u32HeldCnt;
    pstStatus->u32QueuedFrames = pstChn->pstPool->u32QueueCnt;
    pstStatus->u32PeakQueuedFrames = pstChn->u32PeakQueued;
    pstStatus->u32ValidMask = AUDIO_STATS_QUEUE | AUDIO_STATS_DROP;
    pstStatus->u64Epoch = pstChn->u64Epoch;
    pstStatus->u64DroppedFrames = pstChn->u64Dropped;
    pthread_mutex_unlock(&pstChn->stLock);

    return SYS_ERR_OK;
}

/* ======================== Frame capture ======================== */

S32 AI_GetFrame(AI_DEV AiDev, AI_CHN AiChn, AudioFrameS *pstFrame, AecFrameS *pstAecFrame, S32 s32MilliSec) {
    struct timespec stDeadline;
    BOOL bTimedOut = MPP_FALSE;
    AiChnCtx *pstChn;
    U64 u64Epoch;
    S32 ret;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    AI_CHECK_CHN(AiChn);
    AI_CHECK_POINTER(pstFrame);
    if (s32MilliSec < AIO_BLOCK_INFINITE) {
        AI_LOG_ERR("invalid timeout %d", s32MilliSec);
        return SYS_ERR_INVAL;
    }
    pthread_once(&g_stAiOnce, ai_init_once);
    if (s32MilliSec > 0)
        aio_deadline(&stDeadline, s32MilliSec);

    pstChn = &g_stAi.astDev[AiDev].astChn[AiChn];
    pthread_mutex_lock(&pstChn->stLock);
    if (!pstChn->bEnabled) {
        pthread_mutex_unlock(&pstChn->stLock);
        AI_LOG_ERR("dev %d chn %d is not enabled", AiDev, AiChn);
        return SYS_ERR_NOT_INIT;
    }

    u64Epoch = pstChn->u64Epoch;
    pstChn->u32Waiters++;
    for (;;) {
        AiPool *pstPool = pstChn->pstPool;

        if (!pstChn->bEnabled || pstChn->u64Epoch != u64Epoch) {
            ret = AI_ERR_ABORTED;
            break;
        }
        if (pstPool->u32QueueCnt > 0 && pstPool->u32HeldCnt < pstChn->u32UsrDepth) {
            U32 u32Slot = pstPool->pu32Queue[pstPool->u32QueueHead];
            AiSlot *pstSlot = &pstPool->pstSlots[u32Slot];

            pstPool->u32QueueHead = (pstPool->u32QueueHead + 1) % pstPool->u32QueueCap;
            pstPool->u32QueueCnt--;
            pstPool->u32HeldCnt++;
            pstSlot->eState = AI_SLOT_HELD;

            memset(pstFrame, 0, sizeof(*pstFrame));
            pstFrame->eBitWidth = pstChn->eBitWidth;
            pstFrame->eSoundMode = aio_sound_mode(pstChn->u32Channels);
            pstFrame->eSampleRate = (AudioSampleRate)pstChn->u32OutRate;
            pstFrame->s32Channels = (S32)pstChn->u32Channels;
            pstFrame->pu8Data = pstPool->pu8Mem + (size_t)u32Slot * pstPool->u32SlotBytes;
            pstFrame->u32Len = pstPool->u32SlotBytes;
            pstFrame->u64PTS = aio_ns_to_pts(pstSlot->u64PtsNs);
            pstFrame->u32Seq = pstSlot->u32Seq;
            pstFrame->u32SlotId = u32Slot;
            pstFrame->ePcmFormat = pstChn->eFormat;
            pstFrame->u32Flags = pstSlot->u32Flags;
            pstFrame->u64Epoch = pstPool->u64Epoch;
            ret = SYS_ERR_OK;
            break;
        }
        if (pstChn->bLost) {
            ret = AI_ERR_ABORTED;
            break;
        }
        if (bTimedOut) {
            ret = SYS_ERR_TIMEOUT;
            break;
        }
        if (s32MilliSec == AIO_NO_BLOCK) {
            ret = AI_ERR_BUF_EMPTY;
            break;
        }
        /* After a timeout the loop looks at the queue once more. */
        if (s32MilliSec == AIO_BLOCK_INFINITE)
            pthread_cond_wait(&pstChn->stCond, &pstChn->stLock);
        else if (pthread_cond_timedwait(&pstChn->stCond, &pstChn->stLock, &stDeadline) == ETIMEDOUT)
            bTimedOut = MPP_TRUE;
    }
    pstChn->u32Waiters--;
    if (!pstChn->bEnabled)
        pthread_cond_broadcast(&pstChn->stCond);
    pthread_mutex_unlock(&pstChn->stLock);

    /* No AEC reference path yet: report "no valid reference". */
    if (ret == SYS_ERR_OK && pstAecFrame != NULL)
        memset(pstAecFrame, 0, sizeof(*pstAecFrame));

    return ret;
}

S32 AI_ReleaseFrame(AI_DEV AiDev, AI_CHN AiChn, const AudioFrameS *pstFrame, const AecFrameS *pstAecFrame) {
    AiPool *pstPool, **ppstLink;
    AiChnCtx *pstChn;
    AiSlot *pstSlot;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    AI_CHECK_CHN(AiChn);
    AI_CHECK_POINTER(pstFrame);
    pthread_once(&g_stAiOnce, ai_init_once);

    pstChn = &g_stAi.astDev[AiDev].astChn[AiChn];
    pthread_mutex_lock(&pstChn->stLock);
    ppstLink = NULL;
    pstPool = pstChn->pstPool;
    if (pstPool == NULL || pstPool->u64Epoch != pstFrame->u64Epoch) {
        for (ppstLink = &pstChn->pstOrphans; *ppstLink != NULL; ppstLink = &(*ppstLink)->pstNext) {
            if ((*ppstLink)->u64Epoch == pstFrame->u64Epoch)
                break;
        }
        pstPool = *ppstLink;
    }

    /* The whole descriptor has to match what AI_GetFrame handed out. */
    pstSlot = (pstPool != NULL && pstFrame->u32SlotId < pstPool->u32SlotCnt) ? &pstPool->pstSlots[pstFrame->u32SlotId]
                                                                             : NULL;
    if (pstSlot == NULL || pstSlot->eState != AI_SLOT_HELD || pstSlot->u32Seq != pstFrame->u32Seq ||
        pstFrame->pu8Data != pstPool->pu8Mem + (size_t)pstFrame->u32SlotId * pstPool->u32SlotBytes) {
        pthread_mutex_unlock(&pstChn->stLock);
        AI_LOG_ERR("dev %d chn %d: frame (slot %u, seq %u) is not on loan", AiDev, AiChn, pstFrame->u32SlotId,
            pstFrame->u32Seq);
        return SYS_ERR_INVAL;
    }

    pstSlot->eState = AI_SLOT_FREE;
    pstPool->u32HeldCnt--;
    if (ppstLink != NULL) {
        if (pstPool->u32HeldCnt == 0) {
            *ppstLink = pstPool->pstNext;
            free(pstPool);
        }
    } else {
        /* A reader may be waiting for the held-frame limit to drop. */
        pthread_cond_broadcast(&pstChn->stCond);
    }
    pthread_mutex_unlock(&pstChn->stLock);

    return SYS_ERR_OK;
}

/* ======================== Resample ======================== */

S32 AI_EnableReSmp(AI_DEV AiDev, AI_CHN AiChn, AudioSampleRate eOutSampleRate) {
    AiChnCtx *pstChn;
    S32 ret;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    AI_CHECK_CHN(AiChn);
    if (!aio_rate_valid(eOutSampleRate)) {
        AI_LOG_ERR("unsupported sample rate %d", (S32)eOutSampleRate);
        return SYS_ERR_INVAL;
    }
    ret = ai_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstChn = &g_stAi.astDev[AiDev].astChn[AiChn];
    pthread_mutex_lock(&pstChn->stLock);
    if (pstChn->bEnabled) {
        AI_LOG_ERR("dev %d chn %d is enabled, call before AI_EnableChn", AiDev, AiChn);
        ret = SYS_ERR_INVAL;
    } else {
        pstChn->bReSmp = MPP_TRUE;
        pstChn->eReSmpRate = eOutSampleRate;
    }
    pthread_mutex_unlock(&pstChn->stLock);

    AI_CTL_LEAVE(ret);
}

S32 AI_DisableReSmp(AI_DEV AiDev, AI_CHN AiChn) {
    AiChnCtx *pstChn;
    S32 ret;

    AI_CHECK_INIT();
    AI_CHECK_DEV(AiDev);
    AI_CHECK_CHN(AiChn);
    ret = ai_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstChn = &g_stAi.astDev[AiDev].astChn[AiChn];
    pthread_mutex_lock(&pstChn->stLock);
    if (pstChn->bEnabled) {
        AI_LOG_ERR("dev %d chn %d is enabled, disable the channel first", AiDev, AiChn);
        ret = SYS_ERR_INVAL;
    } else {
        pstChn->bReSmp = MPP_FALSE;
    }
    pthread_mutex_unlock(&pstChn->stLock);

    AI_CTL_LEAVE(ret);
}
