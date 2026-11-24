/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    aio_internal.h
 * @Brief     :    Helpers shared by the AI and AO implementations.
 *------------------------------------------------------------------------------
 */

#ifndef AIO_INTERNAL_H
#define AIO_INTERNAL_H

#include <pthread.h>
#include <time.h>

#include "type.h"
#include "sys_type.h"
#include "aio_type.h"
#include "aio_al_ops.h"
#include "log.h"

#define AIO_NS_PER_SEC 1000000000ULL
#define AIO_NS_PER_MS 1000000ULL

#define AIO_FRM_NUM_MIN 2U
#define AIO_FRM_NUM_MAX 10U

/* Channel defaults used until the channel parameter APIs are implemented. */
#define AIO_AI_USR_FRM_DEPTH 4U
#define AIO_AI_QUEUE_FRAMES 4U
#define AIO_AO_QUEUE_FACTOR 4U

/* Device threads re-check their run flag at least this often. */
#define AIO_WAIT_SLICE_MS 1000

/* ======================== Backend ======================== */

/* Implemented in aio_backend.c (plugin loader); tests link their own. */
S32 aio_backend_load(const AioAlOps **ppstOps);
VOID aio_backend_unload(VOID);

/* Reference-counted access shared by AI and AO. Return SYS_ERR_* codes. */
S32 aio_backend_acquire(const AioAlOps **ppstOps);
VOID aio_backend_release(VOID);
S32 aio_backend_err_to_sys(S32 s32AlErr);

/* ======================== Time ======================== */

U64 aio_mono_ns(VOID);
/* Overflow-safe frames -> nanoseconds. */
U64 aio_frames_to_ns(U64 u64Frames, U32 u32Rate);
/* Internal nanoseconds -> the unit of the public u64PTS fields. */
U64 aio_ns_to_pts(U64 u64Ns);
/* Condition variable bound to CLOCK_MONOTONIC, for aio_deadline(). */
S32 aio_cond_init(pthread_cond_t *pstCond);
VOID aio_deadline(struct timespec *pstTs, S32 s32MilliSec);
/* At most one message per second; *pu32Suppressed counts the skipped ones. */
BOOL aio_log_allow(U64 *pu64LastNs, U32 *pu32Suppressed);

/* ======================== Format ======================== */

BOOL aio_rate_valid(AudioSampleRate eRate);
AudioSoundMode aio_sound_mode(U32 u32Channels);
U32 aio_format_bytes(AudioPcmFormat eFormat);
/* Resolve AUDIO_PCM_DEFAULT and check the format against the bit width. */
S32 aio_resolve_format(AudioBitWidth eBitWidth, AudioPcmFormat eFormat, AudioPcmFormat *peResolved);
S32 aio_check_dev_attr(const char *pszTag, const AioDevAttrS *pstAttr, AudioPcmFormat *peResolved);
/* Frame size of a channel that converts u32Period frames between rates. */
U32 aio_scale_frames(U32 u32Period, U32 u32FromRate, U32 u32ToRate);

VOID aio_fill_silence(VOID *pBuf, U32 u32Samples, AudioPcmFormat eFormat);
/* Formats the resampler can process: S16_LE, S24_4LE, S32_LE, F32_LE. */
BOOL aio_format_resamplable(AudioPcmFormat eFormat);
VOID aio_pcm_to_float(const VOID *pSrc, float *pfDst, U32 u32Samples, AudioPcmFormat eFormat);
VOID aio_float_to_pcm(const float *pfSrc, VOID *pDst, U32 u32Samples, AudioPcmFormat eFormat);

#endif /* AIO_INTERNAL_H */
