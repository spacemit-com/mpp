/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * Scripted pcm backend for the AI/AO tests. It takes the place of
 * mpi/aio/aio_backend.c at link time. The "sound card" has no clock of its
 * own: it captures and plays exactly as many periods as the test tells it
 * to, so every result is deterministic. One capture and one playback PCM
 * can be open at a time, like an exclusive hw: device.
 */

#ifndef AIO_FAKE_BACKEND_H
#define AIO_FAKE_BACKEND_H

#include "aio_al_ops.h"

typedef struct _FakeStats {
    U32 u32Loads;
    U32 u32Unloads;
    U32 u32Inits;
    U32 u32Deinits;
    U32 u32Opens; /* successful opens */
    U32 u32Closes;
    U32 u32Starts;
    U32 u32Stops;
    U32 u32Recovers;
    AioAlPcmCfg astLastCfg[2]; /* last requested configuration, by AioAlDir */
} FakeStats;

/* Sample of capture frame u64Frame / channel u32Ch, in [-1, 1). */
typedef float (*FakeSourceFn)(U64 u64Frame, U32 u32Ch);

/* Forget every setting and counter. No PCM may be open. */
VOID fake_reset(VOID);
const FakeStats *fake_stats(VOID);

/* aio_backend_load fails with this SYS_ERR_* code, as if the plugin were missing. */
VOID fake_set_load_error(S32 s32SysErr);
/* The next pcm_open fails with this AIO_AL_ERR_* code. */
VOID fake_set_open_error(S32 s32Err);
/* What the hardware accepts; 0 / AUDIO_PCM_DEFAULT accept anything. */
VOID fake_set_support(U32 u32Rate, U32 u32Channels, AudioPcmFormat eFormat);
/* Period / buffer the hardware really gives; 0 keeps the request. */
VOID fake_set_geometry(U32 u32Period, U32 u32Buffer);
/* The next pcm_wait/read/write of that direction fails once with s32Err. */
VOID fake_inject(AioAlDir eDir, S32 s32Err);
/* pcm_recover returns this from now on. */
VOID fake_set_recover_error(S32 s32Err);

/* Default source: frame index + 1000 * channel, as a 16-bit ramp. */
VOID fake_capture_set_source(FakeSourceFn pfnSource);
float fake_ramp_source(U64 u64Frame, U32 u32Ch);
/* PTS in the public unit (ms) the framework gives capture frame u64Frame. */
U64 fake_capture_pts_ms(U64 u64Frame, U32 u32Rate);

/*
 * Let the device capture / play u32Periods more periods, then wait until
 * the device thread has dealt with them and is blocked again. Returns
 * MPP_FALSE when the thread did not settle within 5 seconds.
 */
BOOL fake_capture_advance(U32 u32Periods);
BOOL fake_playback_advance(U32 u32Periods);
/* The same without waiting, for tests that run threads against the clock. */
VOID fake_capture_tick(U32 u32Periods);
VOID fake_playback_tick(U32 u32Periods);
/* Wait until the device thread of that direction is blocked in pcm_wait. */
BOOL fake_wait_idle(AioAlDir eDir);

/* Everything written to the playback device so far. */
U64 fake_playback_frames(VOID);
const U8 *fake_playback_data(VOID);

#endif /* AIO_FAKE_BACKEND_H */
