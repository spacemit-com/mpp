/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * Scripted pcm backend for the AI/AO tests, see aio_fake_backend.h.
 */

#include <errno.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include "aio_fake_backend.h"
#include "aio_internal.h"

/* Device time of capture frame 0 / playback frame 0. */
#define FAKE_BASE_NS (1000 * AIO_NS_PER_SEC)
#define FAKE_IDLE_TIMEOUT_MS 5000
#define FAKE_CHUNK_SAMPLES 256

struct _AioAlPcm {
    BOOL bOpen;
    AioAlPcmCfg stCfg;
    U32 u32Buffer; /* real device buffer, frames */
    U32 u32Kept;   /* playback fill level the framework may reach */
    U32 u32FrameBytes;
    BOOL bRunning; /* started: the device clock only runs then */
    BOOL bWaiting; /* the device thread sits in pcm_wait with nothing to do */
    BOOL bXrun;    /* stopped by an overrun / underrun until pcm_recover */
    S32 s32Inject;
    S32 s32InjectIo;
    S32 s32EvtFd;
    U64 u64Produced; /* capture: frames the device has captured */
    U64 u64Consumed; /* capture: frames read by the framework */
    U64 u64Written;  /* playback: frames written by the framework */
    U64 u64Played;   /* playback: frames the device has played */
    U8 *pu8Rec;
    size_t szRecCap;
};

static struct _AioAlPcm g_astPcm[2];
static pthread_mutex_t g_stLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_stCond;
static pthread_once_t g_stOnce = PTHREAD_ONCE_INIT;
static FakeStats g_stStats;
static S32 g_s32LoadErr;
static S32 g_s32OpenErr;
static S32 g_s32RecoverErr;
static U32 g_u32SupRate;
static U32 g_u32SupChannels;
static AudioPcmFormat g_eSupFormat;
static U32 g_u32GeoPeriod;
static U32 g_u32GeoBuffer;
static FakeSourceFn g_pfnSource = fake_ramp_source;
static BOOL g_bCapTsValid = MPP_TRUE;
static S64 g_s64CapTsOffsetNs;

static VOID fake_once(VOID) {
    aio_cond_init(&g_stCond);
    g_astPcm[0].s32EvtFd = -1;
    g_astPcm[1].s32EvtFd = -1;
}

static VOID fake_kick(struct _AioAlPcm *pstPcm) {
    uint64_t u64One = 1;

    if (pstPcm->bOpen && write(pstPcm->s32EvtFd, &u64One, sizeof(u64One)) < 0)
        abort();
}

/* Can the device thread transfer u32Frames right now? Lock held. */
static BOOL fake_ready(const struct _AioAlPcm *pstPcm, U32 u32Frames) {
    if (pstPcm->stCfg.eDir == AIO_AL_DIR_CAPTURE)
        return pstPcm->u64Produced - pstPcm->u64Consumed >= u32Frames;
    return pstPcm->u64Written - pstPcm->u64Played + u32Frames <= pstPcm->u32Kept;
}

/* ======================== Test control ======================== */

float fake_ramp_source(U64 u64Frame, U32 u32Ch) {
    return (float)(int16_t)(u64Frame + 1000ULL * u32Ch) * (1.0f / 32768.0f);
}

VOID fake_reset(VOID) {
    S32 i;

    pthread_once(&g_stOnce, fake_once);
    pthread_mutex_lock(&g_stLock);
    for (i = 0; i < 2; i++) {
        if (g_astPcm[i].bOpen)
            abort();
        free(g_astPcm[i].pu8Rec);
        g_astPcm[i].pu8Rec = NULL;
        g_astPcm[i].szRecCap = 0;
        g_astPcm[i].u64Written = 0;
        g_astPcm[i].u64Played = 0;
        g_astPcm[i].u64Produced = 0;
        g_astPcm[i].u64Consumed = 0;
    }
    memset(&g_stStats, 0, sizeof(g_stStats));
    g_s32LoadErr = 0;
    g_s32OpenErr = 0;
    g_s32RecoverErr = 0;
    g_u32SupRate = 0;
    g_u32SupChannels = 0;
    g_eSupFormat = AUDIO_PCM_DEFAULT;
    g_u32GeoPeriod = 0;
    g_u32GeoBuffer = 0;
    g_pfnSource = fake_ramp_source;
    g_bCapTsValid = MPP_TRUE;
    g_s64CapTsOffsetNs = 0;
    pthread_mutex_unlock(&g_stLock);
}

const FakeStats *fake_stats(VOID) {
    return &g_stStats;
}

VOID fake_set_load_error(S32 s32SysErr) {
    g_s32LoadErr = s32SysErr;
}

VOID fake_set_open_error(S32 s32Err) {
    g_s32OpenErr = s32Err;
}

VOID fake_set_support(U32 u32Rate, U32 u32Channels, AudioPcmFormat eFormat) {
    g_u32SupRate = u32Rate;
    g_u32SupChannels = u32Channels;
    g_eSupFormat = eFormat;
}

VOID fake_set_geometry(U32 u32Period, U32 u32Buffer) {
    g_u32GeoPeriod = u32Period;
    g_u32GeoBuffer = u32Buffer;
}

VOID fake_set_recover_error(S32 s32Err) {
    g_s32RecoverErr = s32Err;
}

VOID fake_capture_set_source(FakeSourceFn pfnSource) {
    g_pfnSource = pfnSource ? pfnSource : fake_ramp_source;
}

VOID fake_capture_set_clock(BOOL bValid, S64 s64OffsetNs) {
    pthread_mutex_lock(&g_stLock);
    g_bCapTsValid = bValid;
    g_s64CapTsOffsetNs = s64OffsetNs;
    pthread_mutex_unlock(&g_stLock);
}

U64 fake_capture_pts_ms(U64 u64Frame, U32 u32Rate) {
    return aio_ns_to_pts(FAKE_BASE_NS + aio_frames_to_ns(u64Frame, u32Rate));
}

VOID fake_inject(AioAlDir eDir, S32 s32Err) {
    pthread_mutex_lock(&g_stLock);
    g_astPcm[eDir].s32Inject = s32Err;
    fake_kick(&g_astPcm[eDir]);
    pthread_mutex_unlock(&g_stLock);
}

VOID fake_inject_io(AioAlDir eDir, S32 s32Err) {
    pthread_mutex_lock(&g_stLock);
    g_astPcm[eDir].s32InjectIo = s32Err;
    pthread_mutex_unlock(&g_stLock);
}

BOOL fake_wait_idle(AioAlDir eDir) {
    struct _AioAlPcm *pstPcm = &g_astPcm[eDir];
    struct timespec stDeadline;
    BOOL bIdle = MPP_TRUE;

    aio_deadline(&stDeadline, FAKE_IDLE_TIMEOUT_MS);
    pthread_mutex_lock(&g_stLock);
    /* Idle: blocked in pcm_wait with no failure left to pick up and nothing to transfer. */
    while (pstPcm->bOpen && !(pstPcm->bWaiting && pstPcm->s32Inject == 0 && !pstPcm->bXrun &&
                                !fake_ready(pstPcm, pstPcm->stCfg.u32PeriodFrames))) {
        if (pthread_cond_timedwait(&g_stCond, &g_stLock, &stDeadline) == ETIMEDOUT) {
            bIdle = MPP_FALSE;
            break;
        }
    }
    pthread_mutex_unlock(&g_stLock);

    return bIdle;
}

VOID fake_capture_tick(U32 u32Periods) {
    struct _AioAlPcm *pstPcm = &g_astPcm[AIO_AL_DIR_CAPTURE];

    pthread_mutex_lock(&g_stLock);
    if (pstPcm->bOpen && pstPcm->bRunning && !pstPcm->bXrun) {
        pstPcm->u64Produced += (U64)u32Periods * pstPcm->stCfg.u32PeriodFrames;
        /* Like the real thing: an unread buffer that fills up stops the stream. */
        if (pstPcm->u64Produced - pstPcm->u64Consumed > pstPcm->u32Buffer)
            pstPcm->bXrun = MPP_TRUE;
        fake_kick(pstPcm);
    }
    pthread_mutex_unlock(&g_stLock);
}

VOID fake_playback_tick(U32 u32Periods) {
    struct _AioAlPcm *pstPcm = &g_astPcm[AIO_AL_DIR_PLAYBACK];

    pthread_mutex_lock(&g_stLock);
    if (pstPcm->bOpen && pstPcm->bRunning && !pstPcm->bXrun) {
        U64 u64Frames = (U64)u32Periods * pstPcm->stCfg.u32PeriodFrames;

        if (pstPcm->u64Written - pstPcm->u64Played < u64Frames) {
            pstPcm->u64Played = pstPcm->u64Written;
            pstPcm->bXrun = MPP_TRUE;
        } else {
            pstPcm->u64Played += u64Frames;
        }
        fake_kick(pstPcm);
    }
    pthread_mutex_unlock(&g_stLock);
}

BOOL fake_capture_advance(U32 u32Periods) {
    while (u32Periods-- > 0) {
        fake_capture_tick(1);
        if (!fake_wait_idle(AIO_AL_DIR_CAPTURE))
            return MPP_FALSE;
    }

    return MPP_TRUE;
}

BOOL fake_playback_advance(U32 u32Periods) {
    while (u32Periods-- > 0) {
        fake_playback_tick(1);
        if (!fake_wait_idle(AIO_AL_DIR_PLAYBACK))
            return MPP_FALSE;
    }

    return MPP_TRUE;
}

U64 fake_playback_frames(VOID) {
    U64 u64Frames;

    pthread_mutex_lock(&g_stLock);
    u64Frames = g_astPcm[AIO_AL_DIR_PLAYBACK].u64Written;
    pthread_mutex_unlock(&g_stLock);

    return u64Frames;
}

const U8 *fake_playback_data(VOID) {
    return g_astPcm[AIO_AL_DIR_PLAYBACK].pu8Rec;
}

/* ======================== Backend ops ======================== */

static S32 fake_init(VOID) {
    pthread_once(&g_stOnce, fake_once);
    g_stStats.u32Inits++;
    return AIO_AL_OK;
}

static S32 fake_deinit(VOID) {
    g_stStats.u32Deinits++;
    return AIO_AL_OK;
}

static S32 fake_pcm_open(const AioAlPcmCfg *pstCfg, AioAlPcm **ppstPcm, AioAlPcmCfg *pstActual) {
    struct _AioAlPcm *pstPcm = &g_astPcm[pstCfg->eDir];
    U32 u32Buffer = g_u32GeoBuffer ? g_u32GeoBuffer : pstCfg->u32BufferFrames;
    S32 ret = AIO_AL_OK;

    pthread_mutex_lock(&g_stLock);
    g_stStats.astLastCfg[pstCfg->eDir] = *pstCfg;
    if (g_s32OpenErr != 0) {
        ret = g_s32OpenErr;
        g_s32OpenErr = 0;
    } else if (pstPcm->bOpen) {
        ret = AIO_AL_ERR_BUSY;
    } else if ((g_u32SupRate != 0 && pstCfg->u32Rate != g_u32SupRate) ||
        (g_u32SupChannels != 0 && pstCfg->u32Channels != g_u32SupChannels) ||
        (g_eSupFormat != AUDIO_PCM_DEFAULT && pstCfg->ePcmFormat != g_eSupFormat) ||
        u32Buffer < 2 * pstCfg->u32PeriodFrames) {
        ret = AIO_AL_ERR_UNSUP;
    }
    if (ret == AIO_AL_OK) {
        free(pstPcm->pu8Rec);
        memset(pstPcm, 0, sizeof(*pstPcm));
        pstPcm->s32EvtFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (pstPcm->s32EvtFd < 0)
            abort();
        pstPcm->bOpen = MPP_TRUE;
        pstPcm->stCfg = *pstCfg;
        pstPcm->u32Buffer = u32Buffer;
        pstPcm->u32Kept = u32Buffer < pstCfg->u32BufferFrames ? u32Buffer : pstCfg->u32BufferFrames;
        pstPcm->u32FrameBytes = pstCfg->u32Channels * aio_format_bytes(pstCfg->ePcmFormat);
        *pstActual = *pstCfg;
        pstActual->u32PeriodFrames = g_u32GeoPeriod ? g_u32GeoPeriod : pstCfg->u32PeriodFrames;
        pstActual->u32BufferFrames = pstCfg->eDir == AIO_AL_DIR_CAPTURE ? u32Buffer : pstPcm->u32Kept;
        *ppstPcm = pstPcm;
        g_stStats.u32Opens++;
    }
    pthread_mutex_unlock(&g_stLock);

    return ret;
}

static S32 fake_pcm_close(AioAlPcm *pstPcm) {
    pthread_mutex_lock(&g_stLock);
    close(pstPcm->s32EvtFd);
    pstPcm->s32EvtFd = -1;
    pstPcm->bOpen = MPP_FALSE;
    pstPcm->bWaiting = MPP_FALSE;
    g_stStats.u32Closes++;
    pthread_cond_broadcast(&g_stCond);
    pthread_mutex_unlock(&g_stLock);

    return AIO_AL_OK;
}

static S32 fake_pcm_start(AioAlPcm *pstPcm) {
    pthread_mutex_lock(&g_stLock);
    pstPcm->bRunning = MPP_TRUE;
    g_stStats.u32Starts++;
    pthread_mutex_unlock(&g_stLock);

    return AIO_AL_OK;
}

static S32 fake_pcm_stop(AioAlPcm *pstPcm) {
    pthread_mutex_lock(&g_stLock);
    pstPcm->bRunning = MPP_FALSE;
    g_stStats.u32Stops++;
    pthread_mutex_unlock(&g_stLock);

    return AIO_AL_OK;
}

/* A pending failure, consumed. Lock held. */
static S32 fake_take_error(struct _AioAlPcm *pstPcm) {
    S32 ret = pstPcm->s32Inject;

    if (ret != 0) {
        pstPcm->s32Inject = 0;
        return ret;
    }

    return pstPcm->bXrun ? AIO_AL_ERR_XRUN : AIO_AL_OK;
}

/* The same for pcm_read / pcm_write, which can be given a failure of their own. */
static S32 fake_take_io_error(struct _AioAlPcm *pstPcm) {
    S32 ret = pstPcm->s32InjectIo;

    if (ret != 0) {
        pstPcm->s32InjectIo = 0;
        return ret;
    }

    return fake_take_error(pstPcm);
}

static S32 fake_pcm_wait(AioAlPcm *pstPcm, U32 u32Frames, S32 s32WakeFd, S32 s32TimeoutMs) {
    const U64 u64Deadline = aio_mono_ns() + (U64)(s32TimeoutMs > 0 ? s32TimeoutMs : 0) * AIO_NS_PER_MS;

    for (;;) {
        struct pollfd astFds[2];
        uint64_t u64Val;
        U64 u64Now;
        S32 ret;

        pthread_mutex_lock(&g_stLock);
        ret = fake_take_error(pstPcm);
        if (ret != AIO_AL_OK || fake_ready(pstPcm, u32Frames)) {
            pstPcm->bWaiting = MPP_FALSE;
            pthread_mutex_unlock(&g_stLock);
            return ret;
        }
        pstPcm->bWaiting = MPP_TRUE;
        pthread_cond_broadcast(&g_stCond);
        pthread_mutex_unlock(&g_stLock);

        u64Now = aio_mono_ns();
        if (u64Now >= u64Deadline)
            return AIO_AL_ERR_TIMEOUT;
        astFds[0].fd = s32WakeFd;
        astFds[0].events = POLLIN;
        astFds[0].revents = 0;
        astFds[1].fd = pstPcm->s32EvtFd;
        astFds[1].events = POLLIN;
        astFds[1].revents = 0;
        ret = poll(astFds, 2, (int)((u64Deadline - u64Now + AIO_NS_PER_MS - 1) / AIO_NS_PER_MS));
        if (ret < 0 && errno != EINTR)
            return AIO_AL_ERR_IO;
        if (ret > 0 && astFds[0].revents != 0) {
            pthread_mutex_lock(&g_stLock);
            pstPcm->bWaiting = MPP_FALSE;
            pthread_mutex_unlock(&g_stLock);
            return AIO_AL_ERR_WAKE;
        }
        if (ret > 0 && astFds[1].revents != 0 && read(pstPcm->s32EvtFd, &u64Val, sizeof(u64Val)) < 0 &&
            errno != EAGAIN)
            return AIO_AL_ERR_IO;
    }
}

static VOID fake_encode(const float *pfSrc, U8 *pu8Dst, U32 u32Samples, AudioPcmFormat eFormat) {
    U32 i;

    if (aio_format_resamplable(eFormat)) {
        aio_float_to_pcm(pfSrc, pu8Dst, u32Samples, eFormat);
        return;
    }
    for (i = 0; i < u32Samples; i++) {
        long lVal = lrintf(pfSrc[i] * 32768.0f);
        int16_t s16 = (int16_t)(lVal > 32767 ? 32767 : (lVal < -32768 ? -32768 : lVal));

        if (eFormat == AUDIO_PCM_U8) {
            pu8Dst[i] = (U8)((s16 >> 8) + 128);
        } else if (eFormat == AUDIO_PCM_S8) {
            pu8Dst[i] = (U8)(int8_t)(s16 >> 8);
        } else { /* AUDIO_PCM_S24_3LE */
            pu8Dst[i * 3] = 0;
            pu8Dst[i * 3 + 1] = (U8)(s16 & 0xFF);
            pu8Dst[i * 3 + 2] = (U8)((s16 >> 8) & 0xFF);
        }
    }
}

static S32 fake_pcm_read(AioAlPcm *pstPcm, VOID *pBuf, U32 u32Frames) {
    const U32 u32Ch = pstPcm->stCfg.u32Channels;
    const U32 u32Bytes = aio_format_bytes(pstPcm->stCfg.ePcmFormat);
    float afChunk[FAKE_CHUNK_SAMPLES];
    U8 *pu8Dst = (U8 *)pBuf;
    U32 u32Done = 0, u32Fill = 0;
    U64 u64Avail;
    S32 ret;

    pthread_mutex_lock(&g_stLock);
    ret = fake_take_io_error(pstPcm);
    if (ret != AIO_AL_OK) {
        pthread_mutex_unlock(&g_stLock);
        return ret;
    }
    u64Avail = pstPcm->u64Produced - pstPcm->u64Consumed;
    if (u32Frames > u64Avail)
        u32Frames = (U32)u64Avail;
    if (u32Frames == 0) {
        pthread_mutex_unlock(&g_stLock);
        return AIO_AL_ERR_AGAIN;
    }
    while (u32Done < u32Frames) {
        U32 c;

        for (c = 0; c < u32Ch; c++)
            afChunk[u32Fill++] = g_pfnSource(pstPcm->u64Consumed + u32Done, c);
        u32Done++;
        if (u32Fill + u32Ch > FAKE_CHUNK_SAMPLES || u32Done == u32Frames) {
            fake_encode(afChunk, pu8Dst, u32Fill, pstPcm->stCfg.ePcmFormat);
            pu8Dst += (size_t)u32Fill * u32Bytes;
            u32Fill = 0;
        }
    }
    pstPcm->u64Consumed += u32Frames;
    pthread_mutex_unlock(&g_stLock);

    return (S32)u32Frames;
}

static S32 fake_pcm_write(AioAlPcm *pstPcm, const VOID *pBuf, U32 u32Frames) {
    size_t szUsed, szAdd;
    U64 u64Space;
    S32 ret;

    pthread_mutex_lock(&g_stLock);
    ret = fake_take_io_error(pstPcm);
    if (ret != AIO_AL_OK) {
        pthread_mutex_unlock(&g_stLock);
        return ret;
    }
    u64Space = pstPcm->u32Buffer - (pstPcm->u64Written - pstPcm->u64Played);
    if (u32Frames > u64Space)
        u32Frames = (U32)u64Space;
    if (u32Frames == 0) {
        pthread_mutex_unlock(&g_stLock);
        return AIO_AL_ERR_AGAIN;
    }
    szUsed = (size_t)pstPcm->u64Written * pstPcm->u32FrameBytes;
    szAdd = (size_t)u32Frames * pstPcm->u32FrameBytes;
    if (szUsed + szAdd > pstPcm->szRecCap) {
        pstPcm->szRecCap = (szUsed + szAdd) * 2;
        pstPcm->pu8Rec = (U8 *)realloc(pstPcm->pu8Rec, pstPcm->szRecCap);
        if (pstPcm->pu8Rec == NULL)
            abort();
    }
    memcpy(pstPcm->pu8Rec + szUsed, pBuf, szAdd);
    pstPcm->u64Written += u32Frames;
    /* Playback starts by itself once the start level is reached. */
    if (pstPcm->u64Written - pstPcm->u64Played >= pstPcm->u32Kept)
        pstPcm->bRunning = MPP_TRUE;
    pthread_mutex_unlock(&g_stLock);

    return (S32)u32Frames;
}

static S32 fake_pcm_recover(AioAlPcm *pstPcm, S32 s32Err) {
    S32 ret;

    pthread_mutex_lock(&g_stLock);
    g_stStats.u32Recovers++;
    ret = g_s32RecoverErr;
    if (ret == AIO_AL_OK) {
        /* A prepared stream is stopped and empty: pending frames are gone. */
        pstPcm->bXrun = MPP_FALSE;
        pstPcm->bRunning = MPP_FALSE;
        pstPcm->u64Consumed = pstPcm->u64Produced;
        pstPcm->u64Played = pstPcm->u64Written;
    }
    pthread_mutex_unlock(&g_stLock);

    return ret;
}

static S32 fake_pcm_get_status(AioAlPcm *pstPcm, AioAlPcmStatus *pstStatus) {
    pthread_mutex_lock(&g_stLock);
    if (pstPcm->stCfg.eDir == AIO_AL_DIR_CAPTURE) {
        pstStatus->u32AvailFrames = (U32)(pstPcm->u64Produced - pstPcm->u64Consumed);
        pstStatus->s64DelayFrames = 0;
        pstStatus->u64TimestampNs = (U64)((S64)(FAKE_BASE_NS +
            aio_frames_to_ns(pstPcm->u64Produced, pstPcm->stCfg.u32Rate)) + g_s64CapTsOffsetNs);
        pstStatus->bTimestampValid = g_bCapTsValid;
    } else {
        pstStatus->s64DelayFrames = (S64)(pstPcm->u64Written - pstPcm->u64Played);
        pstStatus->u32AvailFrames = pstPcm->u32Buffer - (U32)pstStatus->s64DelayFrames;
        pstStatus->u64TimestampNs = FAKE_BASE_NS + aio_frames_to_ns(pstPcm->u64Played, pstPcm->stCfg.u32Rate);
        pstStatus->bTimestampValid = MPP_TRUE;
    }
    pthread_mutex_unlock(&g_stLock);

    return AIO_AL_OK;
}

static const AioAlOps g_stFakeOps = {
    .init = fake_init,
    .deinit = fake_deinit,
    .pcm_open = fake_pcm_open,
    .pcm_close = fake_pcm_close,
    .pcm_start = fake_pcm_start,
    .pcm_stop = fake_pcm_stop,
    .pcm_wait = fake_pcm_wait,
    .pcm_read = fake_pcm_read,
    .pcm_write = fake_pcm_write,
    .pcm_recover = fake_pcm_recover,
    .pcm_get_status = fake_pcm_get_status,
};

/* ======================== Loader replacement ======================== */

S32 aio_backend_load(const AioAlOps **ppstOps) {
    pthread_once(&g_stOnce, fake_once);
    if (g_s32LoadErr != 0)
        return g_s32LoadErr;
    g_stStats.u32Loads++;
    *ppstOps = &g_stFakeOps;

    return SYS_ERR_OK;
}

VOID aio_backend_unload(VOID) {
    g_stStats.u32Unloads++;
}
