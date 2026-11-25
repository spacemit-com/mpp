/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    aio_resample.h
 * @Brief     :    Streaming band-limited sample rate converter.
 *------------------------------------------------------------------------------
 */

#ifndef AIO_RESAMPLE_H
#define AIO_RESAMPLE_H

#include "type.h"

typedef struct _AioResampler AioResampler;

/*
 * u32MaxInFrames is the largest block one aio_resampler_process() call will
 * be given. Samples are interleaved float; channels are filtered separately.
 */
AioResampler *aio_resampler_create(U32 u32InRate, U32 u32OutRate, U32 u32Channels, U32 u32MaxInFrames);
VOID aio_resampler_destroy(AioResampler *pstRs);

/* Forget all history, as if freshly created. */
VOID aio_resampler_reset(AioResampler *pstRs);

/* Upper bound of the frames one process() call returns for u32InFrames. */
U32 aio_resampler_max_out(const AioResampler *pstRs, U32 u32InFrames);

/*
 * Input frames the converter holds back: output frame k is the signal at
 * input time k * in / out, but it can only be produced once this many later
 * input frames have arrived. Feeding that many zero frames flushes the tail.
 */
U32 aio_resampler_delay(const AioResampler *pstRs);

/*
 * Consume u32InFrames and write the output frames that became available.
 * Block boundaries do not affect the result. u32OutCap must be at least
 * aio_resampler_max_out(u32InFrames). Returns the frames written.
 */
U32 aio_resampler_process(AioResampler *pstRs, const float *pfIn, U32 u32InFrames, float *pfOut, U32 u32OutCap);

#endif /* AIO_RESAMPLE_H */
