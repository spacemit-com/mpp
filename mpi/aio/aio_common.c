/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    aio_common.c
 * @Date      :    2026-11-24
 * @Author    :    SPACEMIT
 * @Brief     :    Backend reference, time and PCM format helpers shared by
 *                 the AI and AO modules.
 *------------------------------------------------------------------------------
 */

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "aio_internal.h"

#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__)
#error "aio PCM conversion assumes a little-endian host"
#endif

#define AIO_LOG_ERR(fmt, ...) MPP_LOG(MPP_LOG_ERROR, "aio", fmt, ##__VA_ARGS__)

/* ======================== Backend ======================== */

static pthread_mutex_t g_stAioBackendLock = PTHREAD_MUTEX_INITIALIZER;
static const AioAlOps *g_pstAioBackendOps;
static U32 g_u32AioBackendRef;

S32 aio_backend_err_to_sys(S32 s32AlErr) {
    switch (s32AlErr) {
        case AIO_AL_OK:
            return SYS_ERR_OK;
        case AIO_AL_ERR_NOENT:
        case AIO_AL_ERR_NODEV:
            return SYS_ERR_NOT_FOUND;
        case AIO_AL_ERR_BUSY:
            return SYS_ERR_BUSY;
        case AIO_AL_ERR_NOMEM:
            return SYS_ERR_NOMEM;
        case AIO_AL_ERR_TIMEOUT:
            return SYS_ERR_TIMEOUT;
        default:
            return SYS_ERR_INVAL;
    }
}

S32 aio_backend_acquire(const AioAlOps **ppstOps) {
    S32 ret = SYS_ERR_OK;

    pthread_mutex_lock(&g_stAioBackendLock);
    if (g_u32AioBackendRef == 0) {
        const AioAlOps *pstOps = NULL;

        ret = aio_backend_load(&pstOps);
        if (ret == SYS_ERR_OK) {
            S32 s32AlRet = pstOps->init();

            if (s32AlRet != AIO_AL_OK) {
                AIO_LOG_ERR("pcm backend init failed: %d", s32AlRet);
                aio_backend_unload();
                ret = aio_backend_err_to_sys(s32AlRet);
            } else {
                g_pstAioBackendOps = pstOps;
            }
        }
    }
    if (ret == SYS_ERR_OK) {
        g_u32AioBackendRef++;
        *ppstOps = g_pstAioBackendOps;
    }
    pthread_mutex_unlock(&g_stAioBackendLock);

    return ret;
}

VOID aio_backend_release(VOID) {
    pthread_mutex_lock(&g_stAioBackendLock);
    if (g_u32AioBackendRef > 0 && --g_u32AioBackendRef == 0) {
        g_pstAioBackendOps->deinit();
        g_pstAioBackendOps = NULL;
        aio_backend_unload();
    }
    pthread_mutex_unlock(&g_stAioBackendLock);
}

/* ======================== Time ======================== */

U64 aio_mono_ns(VOID) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (U64)ts.tv_sec * AIO_NS_PER_SEC + (U64)ts.tv_nsec;
}

U64 aio_frames_to_ns(U64 u64Frames, U32 u32Rate) {
    return (u64Frames / u32Rate) * AIO_NS_PER_SEC + (u64Frames % u32Rate) * AIO_NS_PER_SEC / u32Rate;
}

U64 aio_ns_to_pts(U64 u64Ns) {
    /* aio_type.h defines the public PTS fields in milliseconds. */
    return u64Ns / AIO_NS_PER_MS;
}

S32 aio_cond_init(pthread_cond_t *pstCond) {
    pthread_condattr_t stAttr;
    S32 ret;

    if (pthread_condattr_init(&stAttr) != 0)
        return SYS_ERR_NOMEM;
    pthread_condattr_setclock(&stAttr, CLOCK_MONOTONIC);
    ret = pthread_cond_init(pstCond, &stAttr) == 0 ? SYS_ERR_OK : SYS_ERR_NOMEM;
    pthread_condattr_destroy(&stAttr);

    return ret;
}

VOID aio_deadline(struct timespec *pstTs, S32 s32MilliSec) {
    clock_gettime(CLOCK_MONOTONIC, pstTs);
    pstTs->tv_sec += s32MilliSec / 1000;
    pstTs->tv_nsec += (long)(s32MilliSec % 1000) * 1000000L;
    if (pstTs->tv_nsec >= 1000000000L) {
        pstTs->tv_sec++;
        pstTs->tv_nsec -= 1000000000L;
    }
}

BOOL aio_log_allow(U64 *pu64LastNs, U32 *pu32Suppressed) {
    U64 u64Now = aio_mono_ns();

    if (*pu64LastNs != 0 && u64Now - *pu64LastNs < AIO_NS_PER_SEC) {
        (*pu32Suppressed)++;
        return MPP_FALSE;
    }
    *pu64LastNs = u64Now;

    return MPP_TRUE;
}

/* ======================== Format ======================== */

BOOL aio_rate_valid(AudioSampleRate eRate) {
    switch (eRate) {
        case AUDIO_SAMPLE_RATE_8000:
        case AUDIO_SAMPLE_RATE_11025:
        case AUDIO_SAMPLE_RATE_12000:
        case AUDIO_SAMPLE_RATE_16000:
        case AUDIO_SAMPLE_RATE_22050:
        case AUDIO_SAMPLE_RATE_24000:
        case AUDIO_SAMPLE_RATE_32000:
        case AUDIO_SAMPLE_RATE_44100:
        case AUDIO_SAMPLE_RATE_48000:
        case AUDIO_SAMPLE_RATE_64000:
        case AUDIO_SAMPLE_RATE_96000:
            return MPP_TRUE;
        default:
            return MPP_FALSE;
    }
}

AudioSoundMode aio_sound_mode(U32 u32Channels) {
    if (u32Channels == 1)
        return AUDIO_SOUND_MODE_MONO;
    if (u32Channels == 2)
        return AUDIO_SOUND_MODE_STEREO;
    return AUDIO_SOUND_MODE_RAW;
}

U32 aio_format_bytes(AudioPcmFormat eFormat) {
    switch (eFormat) {
        case AUDIO_PCM_U8:
        case AUDIO_PCM_S8:
            return 1;
        case AUDIO_PCM_S16_LE:
            return 2;
        case AUDIO_PCM_S24_3LE:
            return 3;
        case AUDIO_PCM_S24_4LE:
        case AUDIO_PCM_S32_LE:
        case AUDIO_PCM_F32_LE:
            return 4;
        default:
            return 0;
    }
}

S32 aio_resolve_format(AudioBitWidth eBitWidth, AudioPcmFormat eFormat, AudioPcmFormat *peResolved) {
    AudioBitWidth eExpect;

    if (eFormat == AUDIO_PCM_DEFAULT) {
        switch (eBitWidth) {
            case AUDIO_BIT_WIDTH_8:
                *peResolved = AUDIO_PCM_U8;
                return SYS_ERR_OK;
            case AUDIO_BIT_WIDTH_16:
                *peResolved = AUDIO_PCM_S16_LE;
                return SYS_ERR_OK;
            case AUDIO_BIT_WIDTH_24:
                *peResolved = AUDIO_PCM_S24_4LE;
                return SYS_ERR_OK;
            case AUDIO_BIT_WIDTH_32:
                *peResolved = AUDIO_PCM_S32_LE;
                return SYS_ERR_OK;
            default:
                return SYS_ERR_INVAL;
        }
    }

    switch (eFormat) {
        case AUDIO_PCM_U8:
        case AUDIO_PCM_S8:
            eExpect = AUDIO_BIT_WIDTH_8;
            break;
        case AUDIO_PCM_S16_LE:
            eExpect = AUDIO_BIT_WIDTH_16;
            break;
        case AUDIO_PCM_S24_3LE:
        case AUDIO_PCM_S24_4LE:
            eExpect = AUDIO_BIT_WIDTH_24;
            break;
        case AUDIO_PCM_S32_LE:
        case AUDIO_PCM_F32_LE:
            eExpect = AUDIO_BIT_WIDTH_32;
            break;
        default:
            return SYS_ERR_INVAL;
    }
    if (eBitWidth != eExpect)
        return SYS_ERR_INVAL;
    *peResolved = eFormat;

    return SYS_ERR_OK;
}

S32 aio_check_dev_attr(const char *pszTag, const AioDevAttrS *pstAttr, AudioPcmFormat *peResolved) {
    AudioPcmFormat eFormat;
    U64 u64FrameBytes;

    if (!aio_rate_valid(pstAttr->eSampleRate)) {
        MPP_LOG(MPP_LOG_ERROR, pszTag, "unsupported sample rate %d", (S32)pstAttr->eSampleRate);
        return SYS_ERR_INVAL;
    }
    if (aio_resolve_format(pstAttr->eBitWidth, pstAttr->ePcmFormat, &eFormat) != SYS_ERR_OK) {
        MPP_LOG(MPP_LOG_ERROR, pszTag, "pcm format %d does not match bit width %d", (S32)pstAttr->ePcmFormat,
            (S32)pstAttr->eBitWidth);
        return SYS_ERR_INVAL;
    }
    if (pstAttr->u32ChnCnt < 1 || pstAttr->u32ChnCnt > AUDIO_MAX_PCM_CHANNELS) {
        MPP_LOG(MPP_LOG_ERROR, pszTag, "u32ChnCnt %u out of range [1, %u]", pstAttr->u32ChnCnt,
            AUDIO_MAX_PCM_CHANNELS);
        return SYS_ERR_INVAL;
    }
    if (pstAttr->eSoundMode != aio_sound_mode(pstAttr->u32ChnCnt)) {
        MPP_LOG(MPP_LOG_ERROR, pszTag, "sound mode %d does not match %u channels", (S32)pstAttr->eSoundMode,
            pstAttr->u32ChnCnt);
        return SYS_ERR_INVAL;
    }
    if (pstAttr->u32FrmNum < AIO_FRM_NUM_MIN || pstAttr->u32FrmNum > AIO_FRM_NUM_MAX) {
        MPP_LOG(MPP_LOG_ERROR, pszTag, "u32FrmNum %u out of range [%u, %u]", pstAttr->u32FrmNum, AIO_FRM_NUM_MIN,
            AIO_FRM_NUM_MAX);
        return SYS_ERR_INVAL;
    }
    if (pstAttr->u32PtNumPerFrm < 1 || pstAttr->u32PtNumPerFrm > MAX_AIO_POINT_NUM) {
        MPP_LOG(MPP_LOG_ERROR, pszTag, "u32PtNumPerFrm %u out of range [1, %u]", pstAttr->u32PtNumPerFrm,
            MAX_AIO_POINT_NUM);
        return SYS_ERR_INVAL;
    }
    u64FrameBytes = (U64)pstAttr->u32PtNumPerFrm * pstAttr->u32ChnCnt * aio_format_bytes(eFormat);
    if (u64FrameBytes > MAX_AUDIO_FRAME_LEN) {
        MPP_LOG(MPP_LOG_ERROR, pszTag, "frame of %llu bytes exceeds MAX_AUDIO_FRAME_LEN %u",
            (unsigned long long)u64FrameBytes, MAX_AUDIO_FRAME_LEN);
        return SYS_ERR_INVAL;
    }
    *peResolved = eFormat;

    return SYS_ERR_OK;
}

U32 aio_scale_frames(U32 u32Period, U32 u32FromRate, U32 u32ToRate) {
    return (U32)(((U64)u32Period * u32ToRate + u32FromRate / 2) / u32FromRate);
}

VOID aio_fill_silence(VOID *pBuf, U32 u32Samples, AudioPcmFormat eFormat) {
    memset(pBuf, eFormat == AUDIO_PCM_U8 ? 0x80 : 0, (size_t)u32Samples * aio_format_bytes(eFormat));
}

BOOL aio_format_resamplable(AudioPcmFormat eFormat) {
    return eFormat == AUDIO_PCM_S16_LE || eFormat == AUDIO_PCM_S24_4LE || eFormat == AUDIO_PCM_S32_LE ||
        eFormat == AUDIO_PCM_F32_LE;
}

VOID aio_pcm_to_float(const VOID *pSrc, float *pfDst, U32 u32Samples, AudioPcmFormat eFormat) {
    const U8 *pu8Src = (const U8 *)pSrc;
    U32 i;

    switch (eFormat) {
        case AUDIO_PCM_S16_LE:
            for (i = 0; i < u32Samples; i++) {
                int16_t s16;

                memcpy(&s16, pu8Src + (size_t)i * 2, 2);
                pfDst[i] = (float)s16 * (1.0f / 32768.0f);
            }
            break;
        case AUDIO_PCM_S24_4LE:
            for (i = 0; i < u32Samples; i++) {
                uint32_t u32;

                memcpy(&u32, pu8Src + (size_t)i * 4, 4);
                /* Only the low 24 bits are significant; sign-extend them. */
                pfDst[i] = (float)((int32_t)((u32 & 0x00FFFFFFU) ^ 0x00800000U) - 0x00800000) * (1.0f / 8388608.0f);
            }
            break;
        case AUDIO_PCM_S32_LE:
            for (i = 0; i < u32Samples; i++) {
                int32_t s32;

                memcpy(&s32, pu8Src + (size_t)i * 4, 4);
                pfDst[i] = (float)((double)s32 * (1.0 / 2147483648.0));
            }
            break;
        case AUDIO_PCM_F32_LE:
            memcpy(pfDst, pSrc, (size_t)u32Samples * 4);
            break;
        default:
            memset(pfDst, 0, (size_t)u32Samples * sizeof(float));
            break;
    }
}

VOID aio_float_to_pcm(const float *pfSrc, VOID *pDst, U32 u32Samples, AudioPcmFormat eFormat) {
    U8 *pu8Dst = (U8 *)pDst;
    U32 i;

    switch (eFormat) {
        case AUDIO_PCM_S16_LE:
            for (i = 0; i < u32Samples; i++) {
                float f = pfSrc[i] * 32768.0f;
                int16_t s16;

                /* The negated comparison also sends NaN to zero. */
                if (!(f > -32768.0f))
                    s16 = (f < 0.0f) ? INT16_MIN : 0;
                else if (f >= 32767.0f)
                    s16 = INT16_MAX;
                else
                    s16 = (int16_t)lrintf(f);
                memcpy(pu8Dst + (size_t)i * 2, &s16, 2);
            }
            break;
        case AUDIO_PCM_S24_4LE:
            for (i = 0; i < u32Samples; i++) {
                float f = pfSrc[i] * 8388608.0f;
                int32_t s32;

                if (!(f > -8388608.0f))
                    s32 = (f < 0.0f) ? -8388608 : 0;
                else if (f >= 8388607.0f)
                    s32 = 8388607;
                else
                    s32 = (int32_t)lrintf(f);
                memcpy(pu8Dst + (size_t)i * 4, &s32, 4);
            }
            break;
        case AUDIO_PCM_S32_LE:
            for (i = 0; i < u32Samples; i++) {
                double d = (double)pfSrc[i] * 2147483648.0;
                int32_t s32;

                if (!(d > -2147483648.0))
                    s32 = (d < 0.0) ? INT32_MIN : 0;
                else if (d >= 2147483647.0)
                    s32 = INT32_MAX;
                else
                    s32 = (int32_t)lrint(d);
                memcpy(pu8Dst + (size_t)i * 4, &s32, 4);
            }
            break;
        case AUDIO_PCM_F32_LE:
            memcpy(pDst, pfSrc, (size_t)u32Samples * 4);
            break;
        default:
            break;
    }
}
