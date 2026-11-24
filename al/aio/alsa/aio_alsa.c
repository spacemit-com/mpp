/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    aio_alsa.c
 * @Date      :    2026-11-24
 * @Author    :    SPACEMIT
 * @Brief     :    ALSA pcm backend of the AI/AO modules (AL plugin).
 *
 * Devices are opened as "hw:<card>,0": the hardware either takes the
 * requested rate, channel count and format or the open fails, nothing is
 * converted behind the caller's back. MPP_AI_PCM / MPP_AO_PCM replace the
 * device name (for example "plughw:1,0") and are ignored for setuid
 * programs.
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <alsa/asoundlib.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "aio_al_ops.h"
#include "log.h"

#define ALSA_LOG_ERR(fmt, ...) MPP_LOG(MPP_LOG_ERROR, "aio_alsa", fmt, ##__VA_ARGS__)
#define ALSA_LOG_WARN(fmt, ...) MPP_LOG(MPP_LOG_WARN, "aio_alsa", fmt, ##__VA_ARGS__)
#define ALSA_LOG_INFO(fmt, ...) MPP_LOG(MPP_LOG_INFO, "aio_alsa", fmt, ##__VA_ARGS__)

#define ALSA_MAX_POLL_FDS 4
#define ALSA_RESUME_RETRIES 50
#define ALSA_RESUME_SLEEP_US 20000
/* A status timestamp further than this from "now" is not on our clock. */
#define ALSA_TSTAMP_MAX_AGE_NS 10000000000ULL

struct _AioAlPcm {
    snd_pcm_t *pstPcm;
    AioAlDir eDir;
    snd_pcm_uframes_t ulHeadroom; /* playback: real buffer minus the kept fill level */
    struct pollfd astFds[ALSA_MAX_POLL_FDS + 1];
    S32 s32FdCnt;
    char szName[64];
};

static U64 alsa_mono_ns(VOID) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (U64)ts.tv_sec * 1000000000ULL + (U64)ts.tv_nsec;
}

static S32 alsa_map_err(S32 s32Err) {
    switch (s32Err) {
        case -EAGAIN:
            return AIO_AL_ERR_AGAIN;
        case -EPIPE:
            return AIO_AL_ERR_XRUN;
        case -ESTRPIPE:
            return AIO_AL_ERR_SUSPEND;
        case -ENODEV:
        case -ENOENT:
        case -ENXIO:
            return AIO_AL_ERR_NODEV;
        case -EBUSY:
            return AIO_AL_ERR_BUSY;
        case -ENOMEM:
            return AIO_AL_ERR_NOMEM;
        default:
            return AIO_AL_ERR_IO;
    }
}

static S32 alsa_map_state(snd_pcm_t *pstPcm) {
    switch (snd_pcm_state(pstPcm)) {
        case SND_PCM_STATE_XRUN:
            return AIO_AL_ERR_XRUN;
        case SND_PCM_STATE_SUSPENDED:
            return AIO_AL_ERR_SUSPEND;
        case SND_PCM_STATE_DISCONNECTED:
            return AIO_AL_ERR_NODEV;
        default:
            return AIO_AL_OK;
    }
}

static snd_pcm_format_t alsa_format(AudioPcmFormat eFormat) {
    switch (eFormat) {
        case AUDIO_PCM_U8:
            return SND_PCM_FORMAT_U8;
        case AUDIO_PCM_S8:
            return SND_PCM_FORMAT_S8;
        case AUDIO_PCM_S16_LE:
            return SND_PCM_FORMAT_S16_LE;
        case AUDIO_PCM_S24_3LE:
            return SND_PCM_FORMAT_S24_3LE;
        case AUDIO_PCM_S24_4LE:
            /* ALSA S24_LE: 4-byte container, low 24 bits significant. */
            return SND_PCM_FORMAT_S24_LE;
        case AUDIO_PCM_S32_LE:
            return SND_PCM_FORMAT_S32_LE;
        case AUDIO_PCM_F32_LE:
            return SND_PCM_FORMAT_FLOAT_LE;
        default:
            return SND_PCM_FORMAT_UNKNOWN;
    }
}

/* alsa-lib prints its diagnostics to stderr unless they are taken over. */
static void alsa_lib_error(const char *pszFile, int line, const char *pszFunc, int err, const char *pszFmt, ...) {
    char szMsg[256];
    va_list ap;

    va_start(ap, pszFmt);
    vsnprintf(szMsg, sizeof(szMsg), pszFmt, ap);
    va_end(ap);
    if (err != 0)
        ALSA_LOG_WARN("alsa-lib %s:%d %s: %s (%s)", pszFile, line, pszFunc, szMsg, snd_strerror(err));
    else
        ALSA_LOG_WARN("alsa-lib %s:%d %s: %s", pszFile, line, pszFunc, szMsg);
}

static S32 alsa_init(VOID) {
    snd_lib_error_set_handler(alsa_lib_error);
    return AIO_AL_OK;
}

static S32 alsa_deinit(VOID) {
    snd_lib_error_set_handler(NULL);
    /* Drop the configuration tree alsa-lib caches for the whole process. */
    snd_config_update_free_global();
    return AIO_AL_OK;
}

/* Tell what the hardware does accept after it refused a parameter. */
static VOID alsa_log_caps(AioAlPcm *pstPcm, const AioAlPcmCfg *pstCfg, const char *pszWhat) {
    unsigned int uRateMin = 0, uRateMax = 0, uChMin = 0, uChMax = 0;
    snd_pcm_hw_params_t *pstHw;
    char szFormats[160] = "";
    S32 f;

    snd_pcm_hw_params_alloca(&pstHw);
    if (snd_pcm_hw_params_any(pstPcm->pstPcm, pstHw) < 0)
        return;
    snd_pcm_hw_params_get_rate_min(pstHw, &uRateMin, NULL);
    snd_pcm_hw_params_get_rate_max(pstHw, &uRateMax, NULL);
    snd_pcm_hw_params_get_channels_min(pstHw, &uChMin);
    snd_pcm_hw_params_get_channels_max(pstHw, &uChMax);
    for (f = 0; f <= (S32)SND_PCM_FORMAT_LAST; f++) {
        if (snd_pcm_hw_params_test_format(pstPcm->pstPcm, pstHw, (snd_pcm_format_t)f) == 0) {
            size_t szLen = strlen(szFormats);

            snprintf(szFormats + szLen, sizeof(szFormats) - szLen, " %s", snd_pcm_format_name((snd_pcm_format_t)f));
        }
    }
    ALSA_LOG_ERR("%s does not support %s (asked %u Hz, %u ch, %s); it supports %u..%u Hz, %u..%u ch, formats:%s",
        pstPcm->szName, pszWhat, pstCfg->u32Rate, pstCfg->u32Channels,
        snd_pcm_format_name(alsa_format(pstCfg->ePcmFormat)), uRateMin, uRateMax, uChMin, uChMax, szFormats);
}

static S32 alsa_pcm_close(AioAlPcm *pstPcm) {
    if (pstPcm == NULL)
        return AIO_AL_OK;
    if (pstPcm->pstPcm != NULL)
        snd_pcm_close(pstPcm->pstPcm);
    free(pstPcm);

    return AIO_AL_OK;
}

static S32 alsa_pcm_open(const AioAlPcmCfg *pstCfg, AioAlPcm **ppstPcm, AioAlPcmCfg *pstActual) {
    const BOOL bCapture = (pstCfg->eDir == AIO_AL_DIR_CAPTURE);
    snd_pcm_format_t eFormat = alsa_format(pstCfg->ePcmFormat);
    snd_pcm_uframes_t ulPeriod = pstCfg->u32PeriodFrames;
    snd_pcm_uframes_t ulBuffer = pstCfg->u32BufferFrames;
    snd_pcm_uframes_t ulKept, ulAvailMin;
    snd_pcm_hw_params_t *pstHw;
    snd_pcm_sw_params_t *pstSw;
    const char *pszEnv;
    AioAlPcm *pstPcm;
    S32 err;

    if (eFormat == SND_PCM_FORMAT_UNKNOWN)
        return AIO_AL_ERR_UNSUP;
    pstPcm = (AioAlPcm *)calloc(1, sizeof(*pstPcm));
    if (pstPcm == NULL)
        return AIO_AL_ERR_NOMEM;
    pstPcm->eDir = pstCfg->eDir;

    pszEnv = secure_getenv(bCapture ? "MPP_AI_PCM" : "MPP_AO_PCM");
    if (pszEnv != NULL && pszEnv[0] != '\0')
        snprintf(pstPcm->szName, sizeof(pstPcm->szName), "%s", pszEnv);
    else
        snprintf(pstPcm->szName, sizeof(pstPcm->szName), "hw:%d,0", pstCfg->s32Card);

    err = snd_pcm_open(&pstPcm->pstPcm, pstPcm->szName, bCapture ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK,
        SND_PCM_NONBLOCK);
    if (err < 0) {
        ALSA_LOG_ERR("cannot open %s for %s: %s", pstPcm->szName, bCapture ? "capture" : "playback",
            snd_strerror(err));
        pstPcm->pstPcm = NULL;
        alsa_pcm_close(pstPcm);
        return (err == -ENOENT || err == -ENODEV || err == -ENXIO) ? AIO_AL_ERR_NOENT : alsa_map_err(err);
    }

    snd_pcm_hw_params_alloca(&pstHw);
    err = snd_pcm_hw_params_any(pstPcm->pstPcm, pstHw);
    if (err < 0) {
        ALSA_LOG_ERR("%s: no hardware configuration: %s", pstPcm->szName, snd_strerror(err));
        alsa_pcm_close(pstPcm);
        return AIO_AL_ERR_IO;
    }
    /* Only matters for plug devices: keep the rate the caller asked for. */
    snd_pcm_hw_params_set_rate_resample(pstPcm->pstPcm, pstHw, 0);

    if (snd_pcm_hw_params_set_access(pstPcm->pstPcm, pstHw, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
        alsa_log_caps(pstPcm, pstCfg, "interleaved access");
        alsa_pcm_close(pstPcm);
        return AIO_AL_ERR_UNSUP;
    }
    if (snd_pcm_hw_params_set_format(pstPcm->pstPcm, pstHw, eFormat) < 0) {
        alsa_log_caps(pstPcm, pstCfg, "the sample format");
        alsa_pcm_close(pstPcm);
        return AIO_AL_ERR_UNSUP;
    }
    if (snd_pcm_hw_params_set_channels(pstPcm->pstPcm, pstHw, pstCfg->u32Channels) < 0) {
        alsa_log_caps(pstPcm, pstCfg, "the channel count");
        alsa_pcm_close(pstPcm);
        return AIO_AL_ERR_UNSUP;
    }
    if (snd_pcm_hw_params_set_rate(pstPcm->pstPcm, pstHw, pstCfg->u32Rate, 0) < 0) {
        alsa_log_caps(pstPcm, pstCfg, "the sample rate");
        alsa_pcm_close(pstPcm);
        return AIO_AL_ERR_UNSUP;
    }
    /* Any period will do: I/O is done in MPI frames, whatever the hardware period. */
    if (snd_pcm_hw_params_set_period_size_near(pstPcm->pstPcm, pstHw, &ulPeriod, NULL) < 0 ||
        snd_pcm_hw_params_set_buffer_size_near(pstPcm->pstPcm, pstHw, &ulBuffer) < 0 ||
        (err = snd_pcm_hw_params(pstPcm->pstPcm, pstHw)) < 0) {
        ALSA_LOG_ERR("%s: cannot set period %u / buffer %u frames: %s", pstPcm->szName, pstCfg->u32PeriodFrames,
            pstCfg->u32BufferFrames, snd_strerror(err));
        alsa_pcm_close(pstPcm);
        return AIO_AL_ERR_UNSUP;
    }
    snd_pcm_hw_params_get_period_size(pstHw, &ulPeriod, NULL);
    snd_pcm_hw_params_get_buffer_size(pstHw, &ulBuffer);
    if (ulBuffer < 2 * (snd_pcm_uframes_t)pstCfg->u32PeriodFrames) {
        ALSA_LOG_ERR("%s: device buffer of %lu frames cannot hold two frames of %u", pstPcm->szName,
            (unsigned long)ulBuffer, pstCfg->u32PeriodFrames);
        alsa_pcm_close(pstPcm);
        return AIO_AL_ERR_UNSUP;
    }

    /* Playback never queues more than the requested buffer, even when the real one is larger. */
    ulKept = ulBuffer < pstCfg->u32BufferFrames ? ulBuffer : pstCfg->u32BufferFrames;
    pstPcm->ulHeadroom = bCapture ? 0 : ulBuffer - ulKept;
    ulAvailMin = pstPcm->ulHeadroom + pstCfg->u32PeriodFrames;

    snd_pcm_sw_params_alloca(&pstSw);
    err = snd_pcm_sw_params_current(pstPcm->pstPcm, pstSw);
    if (err >= 0) {
        snd_pcm_sw_params_set_tstamp_mode(pstPcm->pstPcm, pstSw, SND_PCM_TSTAMP_ENABLE);
        snd_pcm_sw_params_set_tstamp_type(pstPcm->pstPcm, pstSw, SND_PCM_TSTAMP_TYPE_MONOTONIC);
        snd_pcm_sw_params_set_avail_min(pstPcm->pstPcm, pstSw, ulAvailMin);
        if (!bCapture)
            snd_pcm_sw_params_set_start_threshold(pstPcm->pstPcm, pstSw, ulKept);
        err = snd_pcm_sw_params(pstPcm->pstPcm, pstSw);
    }
    if (err >= 0)
        err = snd_pcm_prepare(pstPcm->pstPcm);
    if (err < 0) {
        ALSA_LOG_ERR("%s: cannot prepare the stream: %s", pstPcm->szName, snd_strerror(err));
        alsa_pcm_close(pstPcm);
        return alsa_map_err(err);
    }

    pstPcm->s32FdCnt = snd_pcm_poll_descriptors_count(pstPcm->pstPcm);
    if (pstPcm->s32FdCnt < 1 || pstPcm->s32FdCnt > ALSA_MAX_POLL_FDS ||
        snd_pcm_poll_descriptors(pstPcm->pstPcm, pstPcm->astFds, (unsigned int)pstPcm->s32FdCnt) != pstPcm->s32FdCnt) {
        ALSA_LOG_ERR("%s: unusable poll descriptors (%d)", pstPcm->szName, pstPcm->s32FdCnt);
        alsa_pcm_close(pstPcm);
        return AIO_AL_ERR_IO;
    }

    *pstActual = *pstCfg;
    pstActual->u32PeriodFrames = (U32)ulPeriod;
    pstActual->u32BufferFrames = (U32)(bCapture ? ulBuffer : ulKept);
    *ppstPcm = pstPcm;
    ALSA_LOG_INFO("%s opened for %s: %u Hz, %u ch, %s, period %lu, buffer %lu", pstPcm->szName,
        bCapture ? "capture" : "playback", pstCfg->u32Rate, pstCfg->u32Channels, snd_pcm_format_name(eFormat),
        (unsigned long)ulPeriod, (unsigned long)ulBuffer);

    return AIO_AL_OK;
}

static S32 alsa_pcm_start(AioAlPcm *pstPcm) {
    S32 err;

    if (snd_pcm_state(pstPcm->pstPcm) != SND_PCM_STATE_PREPARED)
        return alsa_map_state(pstPcm->pstPcm);
    err = snd_pcm_start(pstPcm->pstPcm);

    return err < 0 ? alsa_map_err(err) : AIO_AL_OK;
}

static S32 alsa_pcm_stop(AioAlPcm *pstPcm) {
    snd_pcm_drop(pstPcm->pstPcm);
    return AIO_AL_OK;
}

static S32 alsa_pcm_wait(AioAlPcm *pstPcm, U32 u32Frames, S32 s32WakeFd, S32 s32TimeoutMs) {
    const snd_pcm_uframes_t ulNeed = pstPcm->ulHeadroom + u32Frames;
    const U64 u64Deadline = alsa_mono_ns() + (U64)(s32TimeoutMs > 0 ? s32TimeoutMs : 0) * 1000000ULL;
    const S32 s32FdCnt = pstPcm->s32FdCnt;

    for (;;) {
        snd_pcm_sframes_t lAvail = snd_pcm_avail_update(pstPcm->pstPcm);
        unsigned short u16Revents = 0;
        U64 u64Now;
        S32 ret;

        if (lAvail < 0)
            return alsa_map_err((S32)lAvail);
        if ((snd_pcm_uframes_t)lAvail >= ulNeed)
            return AIO_AL_OK;
        ret = alsa_map_state(pstPcm->pstPcm);
        if (ret != AIO_AL_OK)
            return ret;

        u64Now = alsa_mono_ns();
        if (u64Now >= u64Deadline)
            return AIO_AL_ERR_TIMEOUT;
        pstPcm->astFds[s32FdCnt].fd = s32WakeFd;
        pstPcm->astFds[s32FdCnt].events = POLLIN;
        pstPcm->astFds[s32FdCnt].revents = 0;
        ret = poll(pstPcm->astFds, (nfds_t)(s32FdCnt + (s32WakeFd >= 0 ? 1 : 0)),
            (int)((u64Deadline - u64Now + 999999ULL) / 1000000ULL));
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            return AIO_AL_ERR_IO;
        }
        if (ret == 0)
            return AIO_AL_ERR_TIMEOUT;
        if (s32WakeFd >= 0 && pstPcm->astFds[s32FdCnt].revents != 0)
            return AIO_AL_ERR_WAKE;
        if (snd_pcm_poll_descriptors_revents(pstPcm->pstPcm, pstPcm->astFds, (unsigned int)s32FdCnt, &u16Revents) < 0)
            return AIO_AL_ERR_IO;
        if (u16Revents & (POLLERR | POLLNVAL | POLLHUP)) {
            /* The stream left the running state; find out why. */
            lAvail = snd_pcm_avail_update(pstPcm->pstPcm);
            if (lAvail < 0)
                return alsa_map_err((S32)lAvail);
            ret = alsa_map_state(pstPcm->pstPcm);
            return ret != AIO_AL_OK ? ret : AIO_AL_ERR_IO;
        }
    }
}

static S32 alsa_pcm_read(AioAlPcm *pstPcm, VOID *pBuf, U32 u32Frames) {
    snd_pcm_sframes_t lRet = snd_pcm_readi(pstPcm->pstPcm, pBuf, u32Frames);

    return lRet < 0 ? alsa_map_err((S32)lRet) : (S32)lRet;
}

static S32 alsa_pcm_write(AioAlPcm *pstPcm, const VOID *pBuf, U32 u32Frames) {
    snd_pcm_sframes_t lRet = snd_pcm_writei(pstPcm->pstPcm, pBuf, u32Frames);

    return lRet < 0 ? alsa_map_err((S32)lRet) : (S32)lRet;
}

static S32 alsa_pcm_recover(AioAlPcm *pstPcm, S32 s32Err) {
    S32 err;

    if (s32Err == AIO_AL_ERR_SUSPEND) {
        S32 s32Try = 0;

        /* -EAGAIN: the system is still resuming. */
        while ((err = snd_pcm_resume(pstPcm->pstPcm)) == -EAGAIN && s32Try++ < ALSA_RESUME_RETRIES)
            usleep(ALSA_RESUME_SLEEP_US);
        if (err == -ENODEV)
            return AIO_AL_ERR_NODEV;
    }
    /* A resumed stream is prepared again too, so both cases restart alike. */
    err = snd_pcm_prepare(pstPcm->pstPcm);

    return err < 0 ? alsa_map_err(err) : AIO_AL_OK;
}

static S32 alsa_pcm_get_status(AioAlPcm *pstPcm, AioAlPcmStatus *pstStatus) {
    snd_pcm_status_t *pstSt;
    snd_htimestamp_t stTs;
    U64 u64Now, u64Ts;
    S32 err;

    snd_pcm_status_alloca(&pstSt);
    err = snd_pcm_status(pstPcm->pstPcm, pstSt);
    if (err < 0)
        return alsa_map_err(err);

    snd_pcm_status_get_htstamp(pstSt, &stTs);
    u64Now = alsa_mono_ns();
    u64Ts = (U64)stTs.tv_sec * 1000000000ULL + (U64)stTs.tv_nsec;
    pstStatus->u32AvailFrames = (U32)snd_pcm_status_get_avail(pstSt);
    pstStatus->s64DelayFrames = (S64)snd_pcm_status_get_delay(pstSt);
    pstStatus->u64TimestampNs = u64Ts;
    pstStatus->bTimestampValid = (u64Ts != 0 && u64Ts <= u64Now && u64Now - u64Ts < ALSA_TSTAMP_MAX_AGE_NS);

    return AIO_AL_OK;
}

static const AioAlOps g_stAlsaOps = {
    .init = alsa_init,
    .deinit = alsa_deinit,
    .pcm_open = alsa_pcm_open,
    .pcm_close = alsa_pcm_close,
    .pcm_start = alsa_pcm_start,
    .pcm_stop = alsa_pcm_stop,
    .pcm_wait = alsa_pcm_wait,
    .pcm_read = alsa_pcm_read,
    .pcm_write = alsa_pcm_write,
    .pcm_recover = alsa_pcm_recover,
    .pcm_get_status = alsa_pcm_get_status,
};

const AioAlOps *al_aio_get_ops(void) {
    return &g_stAlsaOps;
}
