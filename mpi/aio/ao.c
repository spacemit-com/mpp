/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    ao.c
 * @Date      :    2026-11-24
 * @Author    :    SPACEMIT
 * @Brief     :    AO (Audio Output) module: playback and per-channel
 *                 resampling on top of the pcm backend.
 *
 * AO_SendFrame copies a frame into the send queue of its channel. One
 * playback thread per enabled device writes a period to the backend every
 * time the device has room for it: the queued data when there is some,
 * silence otherwise, so the device clock keeps running while the
 * application is late.
 *
 * Locking: g_stAo.stCtlLock serializes every configuration call. Each
 * channel lock is a leaf that protects the send queue and the channel state
 * against the playback thread and AO_SendFrame.
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

#include "ao_api.h"
#include "aio_internal.h"
#include "aio_resample.h"

#define AO_LOG_ERR(fmt, ...) MPP_LOG(MPP_LOG_ERROR, "ao", fmt, ##__VA_ARGS__)
#define AO_LOG_WARN(fmt, ...) MPP_LOG(MPP_LOG_WARN, "ao", fmt, ##__VA_ARGS__)
#define AO_LOG_INFO(fmt, ...) MPP_LOG(MPP_LOG_INFO, "ao", fmt, ##__VA_ARGS__)

/* ======================== Internal State ======================== */

typedef struct _AoChnCtx {
    pthread_mutex_t stLock;
    pthread_cond_t stCond;
    BOOL bEnabled;
    BOOL bLost; /* the device disappeared under the channel */
    U32 u32Waiters;
    U64 u64Epoch; /* bumped by every EnableChn */

    /* Resampling request, set while the channel is disabled. */
    BOOL bReSmp;
    AudioSampleRate eReSmpRate;

    /* Format AO_SendFrame accepts, fixed at EnableChn. */
    AudioPcmFormat eFormat;
    U32 u32Channels;
    U32 u32InRate;
    U32 u32InFrames;   /* sample points per submitted frame */
    U32 u32FrameBytes; /* bytes per submitted frame */

    U8 *pu8Queue;
    U32 u32QueueCap;
    U32 u32QueueHead;
    U32 u32QueueCnt;

    AioResampler *pstRs;
    float *pfIn;
    float *pfFifo; /* frames at the device rate, not yet played */
    U32 u32FifoCap;
    U32 u32FifoLen;
    BOOL bRsDirty; /* the converter holds the tail of real input */

    BOOL bPlaying; /* the last period was fully fed by this channel */
    U64 u64Underruns;
    U32 u32PeakQueued;
} AoChnCtx;

typedef struct _AoDevCtx {
    BOOL bAttrSet;
    AioDevAttrS stAttr;
    AudioPcmFormat eFormat;
    BOOL bSpkSet;
    SpkAttrS stSpk;

    BOOL bEnabled;
    AioAlPcm *pstPcm;
    pthread_t stThread;
    atomic_bool bRun;
    atomic_bool bLost;
    S32 s32WakeFd;
    U32 u32FrameBytes;
    U32 u32Prefill; /* frames of silence that start the stream */
    U8 *pu8Out;
    U8 *pu8Silence; /* one frame of silence, to start the stream with */

    atomic_ullong u64Xruns;
    U64 u64LogNs;
    U32 u32LogSkip;

    AoChnCtx astChn[AO_MAX_CHN_NUM];
} AoDevCtx;

typedef struct _AoModCtx {
    pthread_mutex_t stCtlLock;
    atomic_bool bInited;
    const AioAlOps *pstOps;
    AoDevCtx astDev[AO_MAX_DEV_NUM];
} AoModCtx;

static AoModCtx g_stAo = {.stCtlLock = PTHREAD_MUTEX_INITIALIZER};
static pthread_once_t g_stAoOnce = PTHREAD_ONCE_INIT;

/* ======================== Validation Helpers ======================== */

#define AO_CHECK_DEV(dev)                              \
    do {                                               \
        if (((dev) < 0) || ((dev) >= AO_MAX_DEV_NUM)) { \
            AO_LOG_ERR("invalid dev %d", (dev));       \
            return SYS_ERR_INVAL;                      \
        }                                              \
    } while (0)

#define AO_CHECK_CHN(chn)                              \
    do {                                               \
        if (((chn) < 0) || ((chn) >= AO_MAX_CHN_NUM)) { \
            AO_LOG_ERR("invalid chn %d", (chn));       \
            return SYS_ERR_INVAL;                      \
        }                                              \
    } while (0)

#define AO_CHECK_POINTER(ptr)           \
    do {                                \
        if (NULL == (ptr)) {            \
            AO_LOG_ERR("null pointer"); \
            return SYS_ERR_INVAL;       \
        }                               \
    } while (0)

#define AO_CHECK_INIT()                      \
    do {                                     \
        if (!atomic_load(&g_stAo.bInited)) { \
            AO_LOG_ERR("not initialized");   \
            return SYS_ERR_NOT_INIT;         \
        }                                    \
    } while (0)

static VOID ao_init_once(VOID) {
    S32 d, c;

    for (d = 0; d < AO_MAX_DEV_NUM; d++) {
        g_stAo.astDev[d].s32WakeFd = -1;
        for (c = 0; c < AO_MAX_CHN_NUM; c++) {
            pthread_mutex_init(&g_stAo.astDev[d].astChn[c].stLock, NULL);
            aio_cond_init(&g_stAo.astDev[d].astChn[c].stCond);
        }
    }
}

/* Take the control lock; fails when the module is not initialized. */
static S32 ao_ctl_enter(VOID) {
    pthread_once(&g_stAoOnce, ao_init_once);
    pthread_mutex_lock(&g_stAo.stCtlLock);
    if (!atomic_load(&g_stAo.bInited)) {
        pthread_mutex_unlock(&g_stAo.stCtlLock);
        AO_LOG_ERR("not initialized");
        return SYS_ERR_NOT_INIT;
    }

    return SYS_ERR_OK;
}

#define AO_CTL_LEAVE(ret)                        \
    do {                                         \
        pthread_mutex_unlock(&g_stAo.stCtlLock); \
        return (ret);                            \
    } while (0)

/* ======================== Playback Path ======================== */

static VOID ao_chn_pop(AoChnCtx *pstChn) {
    pstChn->u32QueueHead = (pstChn->u32QueueHead + 1) % pstChn->u32QueueCap;
    pstChn->u32QueueCnt--;
    pthread_cond_broadcast(&pstChn->stCond);
}

static VOID ao_chn_feed(AoChnCtx *pstChn, const float *pfIn, U32 u32Frames) {
    pstChn->u32FifoLen += aio_resampler_process(pstChn->pstRs, pfIn, u32Frames,
        pstChn->pfFifo + (size_t)pstChn->u32FifoLen * pstChn->u32Channels, pstChn->u32FifoCap - pstChn->u32FifoLen);
}

/*
 * Put one device period of this channel into pu8Out, which already holds
 * silence. Called with the channel locked.
 */
static VOID ao_chn_pull(AoChnCtx *pstChn, U8 *pu8Out, U32 u32Period) {
    const U32 u32Ch = pstChn->u32Channels;
    BOOL bFed = MPP_FALSE;
    U32 u32Take;

    if (pstChn->pstRs == NULL) {
        if (pstChn->u32QueueCnt > 0) {
            memcpy(pu8Out, pstChn->pu8Queue + (size_t)pstChn->u32QueueHead * pstChn->u32FrameBytes,
                pstChn->u32FrameBytes);
            ao_chn_pop(pstChn);
            pstChn->bPlaying = MPP_TRUE;
        } else if (pstChn->bPlaying) {
            pstChn->u64Underruns++;
            pstChn->bPlaying = MPP_FALSE;
        }
        return;
    }

    while (pstChn->u32FifoLen < u32Period && pstChn->u32QueueCnt > 0) {
        aio_pcm_to_float(pstChn->pu8Queue + (size_t)pstChn->u32QueueHead * pstChn->u32FrameBytes, pstChn->pfIn,
            pstChn->u32InFrames * u32Ch, pstChn->eFormat);
        ao_chn_feed(pstChn, pstChn->pfIn, pstChn->u32InFrames);
        ao_chn_pop(pstChn);
        pstChn->bRsDirty = MPP_TRUE;
        bFed = MPP_TRUE;
    }

    if (pstChn->u32FifoLen < u32Period) {
        if (pstChn->bPlaying) {
            pstChn->u64Underruns++;
            pstChn->bPlaying = MPP_FALSE;
        }
        /*
         * A period without any new input means the stream stopped (or the
         * sender is late): push the tail out of the converter with silence
         * and play what is left. A period that did get input but not enough
         * yet only waits, so that a just-in-time sender is not cut up.
         */
        if (bFed)
            return;
        if (pstChn->bRsDirty) {
            u32Take = aio_resampler_delay(pstChn->pstRs);
            memset(pstChn->pfIn, 0, (size_t)pstChn->u32InFrames * u32Ch * sizeof(float));
            while (u32Take > 0) {
                U32 u32Chunk = u32Take < pstChn->u32InFrames ? u32Take : pstChn->u32InFrames;

                ao_chn_feed(pstChn, pstChn->pfIn, u32Chunk);
                u32Take -= u32Chunk;
            }
            pstChn->bRsDirty = MPP_FALSE;
        }
    } else {
        pstChn->bPlaying = MPP_TRUE;
    }

    u32Take = pstChn->u32FifoLen < u32Period ? pstChn->u32FifoLen : u32Period;
    aio_float_to_pcm(pstChn->pfFifo, pu8Out, u32Take * u32Ch, pstChn->eFormat);
    pstChn->u32FifoLen -= u32Take;
    memmove(pstChn->pfFifo, pstChn->pfFifo + (size_t)u32Take * u32Ch,
        (size_t)pstChn->u32FifoLen * u32Ch * sizeof(float));
}

/* Fill the device with silence up to its start level and start it. */
static S32 ao_dev_prefill(AoDevCtx *pstDev) {
    const AioAlOps *pstOps = g_stAo.pstOps;
    const U32 u32Period = pstDev->stAttr.u32PtNumPerFrm;
    U32 u32Left = pstDev->u32Prefill;

    while (u32Left > 0) {
        S32 s32Ret = pstOps->pcm_write(pstDev->pstPcm, pstDev->pu8Silence,
            u32Left < u32Period ? u32Left : u32Period);

        if (s32Ret == AIO_AL_ERR_AGAIN || s32Ret == 0)
            break;
        if (s32Ret < 0)
            return s32Ret;
        u32Left -= (U32)s32Ret;
    }

    return pstOps->pcm_start(pstDev->pstPcm);
}

/* Returns MPP_TRUE when playback can go on. */
static BOOL ao_dev_recover(AoDevCtx *pstDev, S32 s32Err) {
    S32 c;

    if (s32Err == AIO_AL_ERR_XRUN || s32Err == AIO_AL_ERR_SUSPEND) {
        S32 s32Ret;

        atomic_fetch_add(&pstDev->u64Xruns, 1);
        if (aio_log_allow(&pstDev->u64LogNs, &pstDev->u32LogSkip)) {
            AO_LOG_WARN("playback %s, recovering (%u more not logged)",
                s32Err == AIO_AL_ERR_XRUN ? "underrun" : "suspended", pstDev->u32LogSkip);
            pstDev->u32LogSkip = 0;
        }
        s32Ret = g_stAo.pstOps->pcm_recover(pstDev->pstPcm, s32Err);
        if (s32Ret == AIO_AL_OK)
            s32Ret = ao_dev_prefill(pstDev);
        if (s32Ret == AIO_AL_OK)
            return MPP_TRUE;
        s32Err = s32Ret;
    }

    AO_LOG_ERR("playback device lost (backend error %d)", s32Err);
    atomic_store(&pstDev->bLost, MPP_TRUE);
    for (c = 0; c < AO_MAX_CHN_NUM; c++) {
        AoChnCtx *pstChn = &pstDev->astChn[c];

        pthread_mutex_lock(&pstChn->stLock);
        if (pstChn->bEnabled) {
            pstChn->bLost = MPP_TRUE;
            pthread_cond_broadcast(&pstChn->stCond);
        }
        pthread_mutex_unlock(&pstChn->stLock);
    }

    return MPP_FALSE;
}

static VOID *ao_playback_thread(VOID *pArg) {
    AoDevCtx *pstDev = (AoDevCtx *)pArg;
    const AioAlOps *pstOps = g_stAo.pstOps;
    const U32 u32Period = pstDev->stAttr.u32PtNumPerFrm;
    char szName[16];
    S32 c;

    snprintf(szName, sizeof(szName), "mpp_ao%d", (S32)(pstDev - g_stAo.astDev));
    pthread_setname_np(pthread_self(), szName);

    while (atomic_load(&pstDev->bRun)) {
        BOOL bLost = MPP_FALSE;
        U32 u32Written = 0;
        S32 s32Ret;

        s32Ret = pstOps->pcm_wait(pstDev->pstPcm, u32Period, pstDev->s32WakeFd, AIO_WAIT_SLICE_MS);
        if (s32Ret == AIO_AL_ERR_TIMEOUT || s32Ret == AIO_AL_ERR_WAKE)
            continue;
        if (s32Ret != AIO_AL_OK) {
            if (!ao_dev_recover(pstDev, s32Ret))
                break;
            continue;
        }

        aio_fill_silence(pstDev->pu8Out, u32Period * pstDev->stAttr.u32ChnCnt, pstDev->eFormat);
        for (c = 0; c < AO_MAX_CHN_NUM; c++) {
            AoChnCtx *pstChn = &pstDev->astChn[c];

            pthread_mutex_lock(&pstChn->stLock);
            if (pstChn->bEnabled)
                ao_chn_pull(pstChn, pstDev->pu8Out, u32Period);
            pthread_mutex_unlock(&pstChn->stLock);
        }

        /* The frame has left the channel queue: all of it gets written, across
         * a recovery if need be, unless the device is being disabled or is lost. */
        while (u32Written < u32Period && atomic_load(&pstDev->bRun)) {
            s32Ret = pstOps->pcm_write(pstDev->pstPcm, pstDev->pu8Out + (size_t)u32Written * pstDev->u32FrameBytes,
                u32Period - u32Written);
            if (s32Ret > 0) {
                u32Written += (U32)s32Ret;
                continue;
            }
            if (s32Ret == AIO_AL_ERR_AGAIN || s32Ret == 0)
                s32Ret = pstOps->pcm_wait(pstDev->pstPcm, u32Period - u32Written, pstDev->s32WakeFd,
                    AIO_WAIT_SLICE_MS);
            if (s32Ret == AIO_AL_OK || s32Ret == AIO_AL_ERR_TIMEOUT || s32Ret == AIO_AL_ERR_WAKE)
                continue;
            if (!ao_dev_recover(pstDev, s32Ret)) {
                bLost = MPP_TRUE;
                break;
            }
        }
        if (bLost)
            break;
    }

    return NULL;
}

/* ======================== Channel / Device Teardown ======================== */

static VOID ao_chn_release_resources(AoChnCtx *pstChn) {
    free(pstChn->pu8Queue);
    pstChn->pu8Queue = NULL;
    pstChn->u32QueueCnt = 0;
    pstChn->u32QueueHead = 0;
    aio_resampler_destroy(pstChn->pstRs);
    pstChn->pstRs = NULL;
    free(pstChn->pfIn);
    pstChn->pfIn = NULL;
    free(pstChn->pfFifo);
    pstChn->pfFifo = NULL;
    pstChn->u32FifoLen = 0;
}

static VOID ao_chn_disable(AoChnCtx *pstChn) {
    pthread_mutex_lock(&pstChn->stLock);
    if (pstChn->bEnabled) {
        pstChn->bEnabled = MPP_FALSE;
        pthread_cond_broadcast(&pstChn->stCond);
        while (pstChn->u32Waiters > 0)
            pthread_cond_wait(&pstChn->stCond, &pstChn->stLock);
        ao_chn_release_resources(pstChn);
    }
    pthread_mutex_unlock(&pstChn->stLock);
}

static VOID ao_dev_close(AoDevCtx *pstDev) {
    if (pstDev->pstPcm != NULL) {
        g_stAo.pstOps->pcm_stop(pstDev->pstPcm);
        g_stAo.pstOps->pcm_close(pstDev->pstPcm);
        pstDev->pstPcm = NULL;
    }
    if (pstDev->s32WakeFd >= 0) {
        close(pstDev->s32WakeFd);
        pstDev->s32WakeFd = -1;
    }
    free(pstDev->pu8Out);
    pstDev->pu8Out = NULL;
    free(pstDev->pu8Silence);
    pstDev->pu8Silence = NULL;
}

static VOID ao_dev_disable(AoDevCtx *pstDev) {
    uint64_t u64One = 1;
    S32 c;

    atomic_store(&pstDev->bRun, MPP_FALSE);
    if (write(pstDev->s32WakeFd, &u64One, sizeof(u64One)) < 0)
        AO_LOG_WARN("wake fd write failed: %s", strerror(errno));
    pthread_join(pstDev->stThread, NULL);

    for (c = 0; c < AO_MAX_CHN_NUM; c++)
        ao_chn_disable(&pstDev->astChn[c]);
    ao_dev_close(pstDev);
    pstDev->bEnabled = MPP_FALSE;
}

static VOID ao_dev_reset_cfg(AoDevCtx *pstDev) {
    S32 c;

    pstDev->bAttrSet = MPP_FALSE;
    pstDev->bSpkSet = MPP_FALSE;
    memset(&pstDev->stAttr, 0, sizeof(pstDev->stAttr));
    memset(&pstDev->stSpk, 0, sizeof(pstDev->stSpk));
    for (c = 0; c < AO_MAX_CHN_NUM; c++) {
        pthread_mutex_lock(&pstDev->astChn[c].stLock);
        pstDev->astChn[c].bReSmp = MPP_FALSE;
        pthread_mutex_unlock(&pstDev->astChn[c].stLock);
    }
}

/* ======================== Module / Device ======================== */

S32 AO_Init(VOID) {
    S32 ret, d;

    pthread_once(&g_stAoOnce, ao_init_once);
    pthread_mutex_lock(&g_stAo.stCtlLock);
    if (atomic_load(&g_stAo.bInited))
        AO_CTL_LEAVE(SYS_ERR_OK);

    ret = aio_backend_acquire(&g_stAo.pstOps);
    if (ret != SYS_ERR_OK)
        AO_CTL_LEAVE(ret);
    for (d = 0; d < AO_MAX_DEV_NUM; d++)
        ao_dev_reset_cfg(&g_stAo.astDev[d]);
    atomic_store(&g_stAo.bInited, MPP_TRUE);

    AO_CTL_LEAVE(SYS_ERR_OK);
}

S32 AO_DeInit(VOID) {
    S32 d;

    pthread_once(&g_stAoOnce, ao_init_once);
    pthread_mutex_lock(&g_stAo.stCtlLock);
    if (!atomic_load(&g_stAo.bInited))
        AO_CTL_LEAVE(SYS_ERR_OK);

    for (d = 0; d < AO_MAX_DEV_NUM; d++) {
        if (g_stAo.astDev[d].bEnabled) {
            AO_LOG_ERR("dev %d is still enabled", d);
            AO_CTL_LEAVE(SYS_ERR_BUSY);
        }
    }
    atomic_store(&g_stAo.bInited, MPP_FALSE);
    for (d = 0; d < AO_MAX_DEV_NUM; d++)
        ao_dev_reset_cfg(&g_stAo.astDev[d]);
    aio_backend_release();
    g_stAo.pstOps = NULL;

    AO_CTL_LEAVE(SYS_ERR_OK);
}

S32 AO_SetDevAttr(AO_DEV AoDev, const AioDevAttrS *pstDevAttr) {
    AudioPcmFormat eFormat;
    AoDevCtx *pstDev;
    S32 ret;

    AO_CHECK_INIT();
    AO_CHECK_DEV(AoDev);
    AO_CHECK_POINTER(pstDevAttr);
    ret = ao_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAo.astDev[AoDev];
    if (pstDev->bEnabled) {
        AO_LOG_ERR("dev %d is enabled, attribute is read-only", AoDev);
        AO_CTL_LEAVE(SYS_ERR_BUSY);
    }
    ret = aio_check_dev_attr("ao", pstDevAttr, &eFormat);
    if (ret != SYS_ERR_OK)
        AO_CTL_LEAVE(ret);
    pstDev->stAttr = *pstDevAttr;
    pstDev->eFormat = eFormat;
    pstDev->bAttrSet = MPP_TRUE;

    AO_CTL_LEAVE(SYS_ERR_OK);
}

S32 AO_GetDevAttr(AO_DEV AoDev, AioDevAttrS *pstDevAttr) {
    AoDevCtx *pstDev;
    S32 ret;

    AO_CHECK_INIT();
    AO_CHECK_DEV(AoDev);
    AO_CHECK_POINTER(pstDevAttr);
    ret = ao_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAo.astDev[AoDev];
    if (!pstDev->bAttrSet) {
        AO_LOG_ERR("dev %d has no attribute yet", AoDev);
        AO_CTL_LEAVE(SYS_ERR_INVAL);
    }
    *pstDevAttr = pstDev->stAttr;

    AO_CTL_LEAVE(SYS_ERR_OK);
}

S32 AO_SetSpkAttr(AO_DEV AoDev, const SpkAttrS *pstSpkAttr) {
    AoDevCtx *pstDev;
    S32 ret;

    AO_CHECK_INIT();
    AO_CHECK_DEV(AoDev);
    AO_CHECK_POINTER(pstSpkAttr);
    ret = ao_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAo.astDev[AoDev];
    if (pstDev->bEnabled) {
        AO_LOG_ERR("dev %d is enabled, SPK routing cannot change", AoDev);
        AO_CTL_LEAVE(SYS_ERR_INVAL);
    }
    if (pstSpkAttr->stSpkAttr.s32Dev < 0 || pstSpkAttr->stSpkAttr.s32Chns < 1 ||
        pstSpkAttr->stSpkAttr.s32Chns > (S32)AUDIO_MAX_PCM_CHANNELS) {
        AO_LOG_ERR("invalid SPK routing: card %d, %d channel(s)", pstSpkAttr->stSpkAttr.s32Dev,
            pstSpkAttr->stSpkAttr.s32Chns);
        AO_CTL_LEAVE(SYS_ERR_INVAL);
    }
    pstDev->stSpk = *pstSpkAttr;
    pstDev->bSpkSet = MPP_TRUE;

    AO_CTL_LEAVE(SYS_ERR_OK);
}

S32 AO_GetSpkAttr(AO_DEV AoDev, SpkAttrS *pstSpkAttr) {
    AoDevCtx *pstDev;
    S32 ret;

    AO_CHECK_INIT();
    AO_CHECK_DEV(AoDev);
    AO_CHECK_POINTER(pstSpkAttr);
    ret = ao_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAo.astDev[AoDev];
    if (pstDev->bSpkSet) {
        *pstSpkAttr = pstDev->stSpk;
    } else {
        memset(pstSpkAttr, 0, sizeof(*pstSpkAttr));
        pstSpkAttr->stSpkAttr.s32Chns = 1;
    }

    AO_CTL_LEAVE(SYS_ERR_OK);
}

S32 AO_EnableDev(AO_DEV AoDev) {
    AioAlPcmCfg stCfg, stActual;
    AoDevCtx *pstDev;
    S32 ret, s32AlRet;

    AO_CHECK_INIT();
    AO_CHECK_DEV(AoDev);
    ret = ao_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAo.astDev[AoDev];
    if (pstDev->bEnabled)
        AO_CTL_LEAVE(SYS_ERR_OK);
    if (!pstDev->bAttrSet) {
        AO_LOG_ERR("dev %d: call AO_SetDevAttr first", AoDev);
        AO_CTL_LEAVE(SYS_ERR_INVAL);
    }
    if (pstDev->bSpkSet && (U32)pstDev->stSpk.stSpkAttr.s32Chns != pstDev->stAttr.u32ChnCnt) {
        AO_LOG_ERR("dev %d: SPK routing has %d channel(s) but u32ChnCnt is %u", AoDev,
            pstDev->stSpk.stSpkAttr.s32Chns, pstDev->stAttr.u32ChnCnt);
        AO_CTL_LEAVE(SYS_ERR_INVAL);
    }

    memset(&stCfg, 0, sizeof(stCfg));
    stCfg.eDir = AIO_AL_DIR_PLAYBACK;
    stCfg.s32Card = pstDev->bSpkSet ? pstDev->stSpk.stSpkAttr.s32Dev : 0;
    stCfg.u32Rate = (U32)pstDev->stAttr.eSampleRate;
    stCfg.u32Channels = pstDev->stAttr.u32ChnCnt;
    stCfg.ePcmFormat = pstDev->eFormat;
    stCfg.u32PeriodFrames = pstDev->stAttr.u32PtNumPerFrm;
    stCfg.u32BufferFrames = pstDev->stAttr.u32FrmNum * pstDev->stAttr.u32PtNumPerFrm;
    stActual = stCfg;

    s32AlRet = g_stAo.pstOps->pcm_open(&stCfg, &pstDev->pstPcm, &stActual);
    if (s32AlRet != AIO_AL_OK) {
        pstDev->pstPcm = NULL;
        AO_LOG_ERR("dev %d: cannot open playback card %d (%u Hz, %u ch, format %d): backend error %d", AoDev,
            stCfg.s32Card, stCfg.u32Rate, stCfg.u32Channels, (S32)stCfg.ePcmFormat, s32AlRet);
        AO_CTL_LEAVE(aio_backend_err_to_sys(s32AlRet));
    }
    /* The card takes a hardware period at a time: between two of its
     * wake-ups it plays that much of what is queued, and the queue may
     * be up to one frame short of its fill level when that starts. */
    if (stActual.u32BufferFrames < stActual.u32PeriodFrames + stCfg.u32PeriodFrames) {
        AO_LOG_ERR("dev %d: the card plays in periods of %u frames, which needs %u frames queued but u32FrmNum x "
                   "u32PtNumPerFrm allows %u; raise u32FrmNum or u32PtNumPerFrm",
            AoDev, stActual.u32PeriodFrames, stActual.u32PeriodFrames + stCfg.u32PeriodFrames,
            stActual.u32BufferFrames);
        ao_dev_close(pstDev);
        AO_CTL_LEAVE(SYS_ERR_INVAL);
    }

    pstDev->u32FrameBytes = stCfg.u32Channels * aio_format_bytes(stCfg.ePcmFormat);
    pstDev->u32Prefill = stActual.u32BufferFrames;
    pstDev->pu8Out = (U8 *)calloc(stCfg.u32PeriodFrames, pstDev->u32FrameBytes);
    pstDev->pu8Silence = (U8 *)calloc(stCfg.u32PeriodFrames, pstDev->u32FrameBytes);
    pstDev->s32WakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (pstDev->pu8Out == NULL || pstDev->pu8Silence == NULL || pstDev->s32WakeFd < 0) {
        AO_LOG_ERR("dev %d: out of memory", AoDev);
        ao_dev_close(pstDev);
        AO_CTL_LEAVE(SYS_ERR_NOMEM);
    }
    aio_fill_silence(pstDev->pu8Silence, stCfg.u32PeriodFrames * stCfg.u32Channels, stCfg.ePcmFormat);

    atomic_store(&pstDev->u64Xruns, 0);
    atomic_store(&pstDev->bLost, MPP_FALSE);
    atomic_store(&pstDev->bRun, MPP_TRUE);

    s32AlRet = ao_dev_prefill(pstDev);
    if (s32AlRet != AIO_AL_OK) {
        AO_LOG_ERR("dev %d: cannot start playback: backend error %d", AoDev, s32AlRet);
        ao_dev_close(pstDev);
        AO_CTL_LEAVE(aio_backend_err_to_sys(s32AlRet));
    }
    if (pthread_create(&pstDev->stThread, NULL, ao_playback_thread, pstDev) != 0) {
        AO_LOG_ERR("dev %d: cannot create playback thread", AoDev);
        ao_dev_close(pstDev);
        AO_CTL_LEAVE(SYS_ERR_NOMEM);
    }
    pstDev->bEnabled = MPP_TRUE;
    AO_LOG_INFO("dev %d enabled: card %d, %u Hz, %u ch, format %d, frame %u, device period %u, start level %u", AoDev,
        stCfg.s32Card, stCfg.u32Rate, stCfg.u32Channels, (S32)stCfg.ePcmFormat, stCfg.u32PeriodFrames,
        stActual.u32PeriodFrames, stActual.u32BufferFrames);

    AO_CTL_LEAVE(SYS_ERR_OK);
}

S32 AO_DisableDev(AO_DEV AoDev) {
    AoDevCtx *pstDev;
    S32 ret;

    AO_CHECK_INIT();
    AO_CHECK_DEV(AoDev);
    ret = ao_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAo.astDev[AoDev];
    if (pstDev->bEnabled) {
        ao_dev_disable(pstDev);
        AO_LOG_INFO("dev %d disabled", AoDev);
    }

    AO_CTL_LEAVE(SYS_ERR_OK);
}

/* ======================== Channel ======================== */

S32 AO_EnableChn(AO_DEV AoDev, AO_CHN AoChn) {
    U32 u32DevRate, u32InRate, u32InFrames, u32Period, u32Ch;
    AoDevCtx *pstDev;
    AoChnCtx *pstChn;
    S32 ret, c;

    AO_CHECK_INIT();
    AO_CHECK_DEV(AoDev);
    AO_CHECK_CHN(AoChn);
    ret = ao_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstDev = &g_stAo.astDev[AoDev];
    pstChn = &pstDev->astChn[AoChn];
    if (!pstDev->bEnabled) {
        AO_LOG_ERR("dev %d is not enabled", AoDev);
        AO_CTL_LEAVE(SYS_ERR_NOT_INIT);
    }
    if (atomic_load(&pstDev->bLost)) {
        AO_LOG_ERR("dev %d was lost, disable and enable it again", AoDev);
        AO_CTL_LEAVE(SYS_ERR_NOT_FOUND);
    }
    /* bEnabled only changes under the control lock, which is held here. */
    if (pstChn->bEnabled)
        AO_CTL_LEAVE(SYS_ERR_OK);
    for (c = 0; c < AO_MAX_CHN_NUM; c++) {
        if (pstDev->astChn[c].bEnabled) {
            /* Playing several channels at once needs the mixer, which is not implemented yet. */
            AO_LOG_ERR("dev %d: chn %d is enabled, only one channel per device is supported", AoDev, c);
            AO_CTL_LEAVE(SYS_ERR_BUSY);
        }
    }

    u32Period = pstDev->stAttr.u32PtNumPerFrm;
    u32Ch = pstDev->stAttr.u32ChnCnt;
    u32DevRate = (U32)pstDev->stAttr.eSampleRate;

    pthread_mutex_lock(&pstChn->stLock);
    /* Looked at again under the channel lock: the playback thread raises the
     * flag before it tells the enabled channels, each under this lock, so a
     * channel that is enabled from here on cannot miss the loss. */
    if (atomic_load(&pstDev->bLost)) {
        pthread_mutex_unlock(&pstChn->stLock);
        AO_LOG_ERR("dev %d was lost, disable and enable it again", AoDev);
        AO_CTL_LEAVE(SYS_ERR_NOT_FOUND);
    }
    u32InRate = pstChn->bReSmp ? (U32)pstChn->eReSmpRate : u32DevRate;
    u32InFrames = u32Period;
    if (u32InRate != u32DevRate) {
        u32InFrames = aio_scale_frames(u32Period, u32DevRate, u32InRate);
        if (!aio_format_resamplable(pstDev->eFormat)) {
            AO_LOG_ERR("chn %d: pcm format %d cannot be resampled", AoChn, (S32)pstDev->eFormat);
            ret = SYS_ERR_INVAL;
        } else if (u32InFrames < 1 || u32InFrames > MAX_AIO_POINT_NUM ||
            (U64)u32InFrames * pstDev->u32FrameBytes > MAX_AUDIO_FRAME_LEN) {
            AO_LOG_ERR("chn %d: %u Hz -> %u Hz needs a frame of %u points, out of range", AoChn, u32InRate,
                u32DevRate, u32InFrames);
            ret = SYS_ERR_INVAL;
        }
    }
    if (ret != SYS_ERR_OK) {
        pthread_mutex_unlock(&pstChn->stLock);
        AO_CTL_LEAVE(ret);
    }

    pstChn->u32FrameBytes = u32InFrames * pstDev->u32FrameBytes;
    pstChn->u32QueueCap = AIO_AO_QUEUE_FACTOR * pstDev->stAttr.u32FrmNum;
    pstChn->pu8Queue = (U8 *)calloc(pstChn->u32QueueCap, pstChn->u32FrameBytes);
    if (pstChn->pu8Queue != NULL && u32InRate != u32DevRate) {
        pstChn->pstRs = aio_resampler_create(u32InRate, u32DevRate, u32Ch, u32InFrames);
        pstChn->pfIn = (float *)calloc((size_t)u32InFrames * u32Ch, sizeof(float));
        if (pstChn->pstRs != NULL) {
            /* Room for a period, the frame that completes it and a flushed tail. */
            pstChn->u32FifoCap = u32Period + 2 * aio_resampler_max_out(pstChn->pstRs, u32InFrames) +
                2 * aio_resampler_max_out(pstChn->pstRs, aio_resampler_delay(pstChn->pstRs)) + 16;
            pstChn->pfFifo = (float *)calloc((size_t)pstChn->u32FifoCap * u32Ch, sizeof(float));
        }
    }
    if (pstChn->pu8Queue == NULL ||
        (u32InRate != u32DevRate && (pstChn->pstRs == NULL || pstChn->pfIn == NULL || pstChn->pfFifo == NULL))) {
        AO_LOG_ERR("chn %d: out of memory", AoChn);
        ao_chn_release_resources(pstChn);
        pthread_mutex_unlock(&pstChn->stLock);
        AO_CTL_LEAVE(SYS_ERR_NOMEM);
    }

    pstChn->u64Epoch++;
    pstChn->eFormat = pstDev->eFormat;
    pstChn->u32Channels = u32Ch;
    pstChn->u32InRate = u32InRate;
    pstChn->u32InFrames = u32InFrames;
    pstChn->u32QueueHead = 0;
    pstChn->u32QueueCnt = 0;
    pstChn->u32FifoLen = 0;
    pstChn->bRsDirty = MPP_FALSE;
    pstChn->bPlaying = MPP_FALSE;
    pstChn->u64Underruns = 0;
    pstChn->u32PeakQueued = 0;
    pstChn->bLost = MPP_FALSE;
    pstChn->bEnabled = MPP_TRUE;
    pthread_mutex_unlock(&pstChn->stLock);
    AO_LOG_INFO("dev %d chn %d enabled: %u Hz, %u points per frame, queue of %u frames", AoDev, AoChn, u32InRate,
        u32InFrames, pstChn->u32QueueCap);

    AO_CTL_LEAVE(SYS_ERR_OK);
}

S32 AO_DisableChn(AO_DEV AoDev, AO_CHN AoChn) {
    S32 ret;

    AO_CHECK_INIT();
    AO_CHECK_DEV(AoDev);
    AO_CHECK_CHN(AoChn);
    ret = ao_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    ao_chn_disable(&g_stAo.astDev[AoDev].astChn[AoChn]);

    AO_CTL_LEAVE(SYS_ERR_OK);
}

S32 AO_QueryChnStat(AO_DEV AoDev, AO_CHN AoChn, AoChnStateS *pstStatus) {
    AoChnCtx *pstChn;

    AO_CHECK_INIT();
    AO_CHECK_DEV(AoDev);
    AO_CHECK_CHN(AoChn);
    AO_CHECK_POINTER(pstStatus);
    pthread_once(&g_stAoOnce, ao_init_once);

    pstChn = &g_stAo.astDev[AoDev].astChn[AoChn];
    pthread_mutex_lock(&pstChn->stLock);
    if (!pstChn->bEnabled) {
        pthread_mutex_unlock(&pstChn->stLock);
        AO_LOG_ERR("dev %d chn %d is not enabled", AoDev, AoChn);
        return SYS_ERR_NOT_INIT;
    }
    memset(pstStatus, 0, sizeof(*pstStatus));
    pstStatus->u32ChnTotalNum = pstChn->u32QueueCap;
    pstStatus->u32ChnFreeNum = pstChn->u32QueueCap - pstChn->u32QueueCnt;
    pstStatus->u32ChnBusyNum = pstChn->u32QueueCnt;
    pstStatus->u32PeakQueuedFrames = pstChn->u32PeakQueued;
    pstStatus->u32ValidMask = AUDIO_STATS_QUEUE | AUDIO_STATS_UNDERRUN;
    pstStatus->u64Epoch = pstChn->u64Epoch;
    pstStatus->u64UnderrunPeriods = pstChn->u64Underruns;
    pthread_mutex_unlock(&pstChn->stLock);

    return SYS_ERR_OK;
}

/* ======================== Playback ======================== */

static BOOL ao_frame_matches(const AoChnCtx *pstChn, const AudioFrameS *pstData) {
    AudioPcmFormat eFormat;

    if (aio_resolve_format(pstData->eBitWidth, pstData->ePcmFormat, &eFormat) != SYS_ERR_OK ||
        eFormat != pstChn->eFormat)
        return MPP_FALSE;
    if ((U32)pstData->eSampleRate != pstChn->u32InRate || pstData->s32Channels != (S32)pstChn->u32Channels ||
        pstData->eSoundMode != aio_sound_mode(pstChn->u32Channels))
        return MPP_FALSE;
    if (pstData->u32Len != pstChn->u32FrameBytes || (pstData->u32Flags & ~(U32)AUDIO_FRAME_DISCONTINUITY) != 0)
        return MPP_FALSE;

    return MPP_TRUE;
}

S32 AO_SendFrame(AO_DEV AoDev, AO_CHN AoChn, const AudioFrameS *pstData, S32 s32MilliSec) {
    struct timespec stDeadline;
    BOOL bTimedOut = MPP_FALSE;
    AoChnCtx *pstChn;
    S32 ret;

    AO_CHECK_INIT();
    AO_CHECK_DEV(AoDev);
    AO_CHECK_CHN(AoChn);
    AO_CHECK_POINTER(pstData);
    AO_CHECK_POINTER(pstData->pu8Data);
    if (s32MilliSec < AIO_BLOCK_INFINITE) {
        AO_LOG_ERR("invalid timeout %d", s32MilliSec);
        return SYS_ERR_INVAL;
    }
    pthread_once(&g_stAoOnce, ao_init_once);
    if (s32MilliSec > 0)
        aio_deadline(&stDeadline, s32MilliSec);

    pstChn = &g_stAo.astDev[AoDev].astChn[AoChn];
    pthread_mutex_lock(&pstChn->stLock);
    if (!pstChn->bEnabled) {
        pthread_mutex_unlock(&pstChn->stLock);
        AO_LOG_ERR("dev %d chn %d is not enabled", AoDev, AoChn);
        return SYS_ERR_NOT_INIT;
    }
    if (!ao_frame_matches(pstChn, pstData)) {
        AO_LOG_ERR("dev %d chn %d: frame (%d Hz, %d ch, format %d/%d, %u bytes) does not match the channel "
                   "(%u Hz, %u ch, format %d, %u bytes)",
            AoDev, AoChn, (S32)pstData->eSampleRate, pstData->s32Channels, (S32)pstData->eBitWidth,
            (S32)pstData->ePcmFormat, pstData->u32Len, pstChn->u32InRate, pstChn->u32Channels, (S32)pstChn->eFormat,
            pstChn->u32FrameBytes);
        pthread_mutex_unlock(&pstChn->stLock);
        return SYS_ERR_INVAL;
    }

    pstChn->u32Waiters++;
    for (;;) {
        if (!pstChn->bEnabled || pstChn->bLost) {
            ret = AO_ERR_ABORTED;
            break;
        }
        if (pstChn->u32QueueCnt < pstChn->u32QueueCap) {
            /* The payload is copied: the caller may reuse its buffer at once. */
            memcpy(pstChn->pu8Queue +
                    (size_t)((pstChn->u32QueueHead + pstChn->u32QueueCnt) % pstChn->u32QueueCap) * pstChn->u32FrameBytes,
                pstData->pu8Data, pstChn->u32FrameBytes);
            pstChn->u32QueueCnt++;
            if (pstChn->u32QueueCnt > pstChn->u32PeakQueued)
                pstChn->u32PeakQueued = pstChn->u32QueueCnt;
            ret = SYS_ERR_OK;
            break;
        }
        if (bTimedOut) {
            ret = SYS_ERR_TIMEOUT;
            break;
        }
        if (s32MilliSec == AIO_NO_BLOCK) {
            ret = SYS_ERR_FULL;
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

    return ret;
}

/* ======================== Resample ======================== */

S32 AO_EnableReSmp(AO_DEV AoDev, AO_CHN AoChn, AudioSampleRate eInSampleRate) {
    AoChnCtx *pstChn;
    S32 ret;

    AO_CHECK_INIT();
    AO_CHECK_DEV(AoDev);
    AO_CHECK_CHN(AoChn);
    if (!aio_rate_valid(eInSampleRate)) {
        AO_LOG_ERR("unsupported sample rate %d", (S32)eInSampleRate);
        return SYS_ERR_INVAL;
    }
    ret = ao_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstChn = &g_stAo.astDev[AoDev].astChn[AoChn];
    pthread_mutex_lock(&pstChn->stLock);
    if (pstChn->bEnabled) {
        AO_LOG_ERR("dev %d chn %d is enabled, call before AO_EnableChn", AoDev, AoChn);
        ret = SYS_ERR_INVAL;
    } else {
        pstChn->bReSmp = MPP_TRUE;
        pstChn->eReSmpRate = eInSampleRate;
    }
    pthread_mutex_unlock(&pstChn->stLock);

    AO_CTL_LEAVE(ret);
}

S32 AO_DisableReSmp(AO_DEV AoDev, AO_CHN AoChn) {
    AoChnCtx *pstChn;
    S32 ret;

    AO_CHECK_INIT();
    AO_CHECK_DEV(AoDev);
    AO_CHECK_CHN(AoChn);
    ret = ao_ctl_enter();
    if (ret != SYS_ERR_OK)
        return ret;

    pstChn = &g_stAo.astDev[AoDev].astChn[AoChn];
    pthread_mutex_lock(&pstChn->stLock);
    if (pstChn->bEnabled) {
        AO_LOG_ERR("dev %d chn %d is enabled, disable the channel first", AoDev, AoChn);
        ret = SYS_ERR_INVAL;
    } else {
        pstChn->bReSmp = MPP_FALSE;
    }
    pthread_mutex_unlock(&pstChn->stLock);

    AO_CTL_LEAVE(ret);
}
