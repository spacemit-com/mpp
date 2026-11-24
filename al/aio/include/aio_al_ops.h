/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    aio_al_ops.h
 * @Brief     :    AI/AO AL plugin operations table (vtable).
 *
 * Every audio backend plugin must export one symbol:
 *
 *   const AioAlOps *al_aio_get_ops(void);
 *
 * The backend only knows about PCM devices: open/close, period I/O, status
 * and recovery. Channels, queues, resampling and timestamps live in the MPI
 * layer (mpi/aio). All ops are required; MPI checks them at load time.
 *
 * A PCM handle is used by one thread at a time (the device thread, or the
 * control path while that thread is not running), so backends need no
 * locking of their own.
 *------------------------------------------------------------------------------
 */

#ifndef AIO_AL_OPS_H
#define AIO_AL_OPS_H

#include "type.h"
#include "aio_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Backend result codes. MPI translates them; they never reach the user. */
#define AIO_AL_OK 0
#define AIO_AL_ERR_AGAIN (-1)   /* nothing to read / no room to write right now */
#define AIO_AL_ERR_XRUN (-2)    /* capture overrun / playback underrun */
#define AIO_AL_ERR_SUSPEND (-3) /* device suspended by the system */
#define AIO_AL_ERR_NODEV (-4)   /* device disappeared while open */
#define AIO_AL_ERR_BUSY (-5)    /* device is held by someone else */
#define AIO_AL_ERR_NOENT (-6)   /* no such device */
#define AIO_AL_ERR_UNSUP (-7)   /* hardware rejects the requested parameters */
#define AIO_AL_ERR_NOMEM (-8)
#define AIO_AL_ERR_IO (-9)      /* any other failure */
#define AIO_AL_ERR_WAKE (-10)   /* pcm_wait interrupted through the wake fd */
#define AIO_AL_ERR_TIMEOUT (-11)

typedef enum _AioAlDir {
    AIO_AL_DIR_CAPTURE = 0,
    AIO_AL_DIR_PLAYBACK = 1,
} AioAlDir;

typedef struct _AioAlPcm AioAlPcm;

typedef struct _AioAlPcmCfg {
    AioAlDir eDir;
    S32 s32Card;               /* sound card index (AioDevRouteS.s32Dev) */
    U32 u32Rate;               /* Hz, must be matched exactly */
    U32 u32Channels;           /* must be matched exactly */
    AudioPcmFormat ePcmFormat; /* resolved, never AUDIO_PCM_DEFAULT */
    U32 u32PeriodFrames;       /* MPI frame size; the unit of pcm_wait/read/write */
    U32 u32BufferFrames;       /* requested device buffer, u32FrmNum periods */
} AioAlPcmCfg;

typedef struct _AioAlPcmStatus {
    U32 u32AvailFrames;   /* readable (capture) / writable (playback) frames */
    S64 s64DelayFrames;   /* playback: frames queued in the device */
    U64 u64TimestampNs;   /* CLOCK_MONOTONIC time the two values were sampled */
    BOOL bTimestampValid; /* MPP_FALSE: the driver gave no usable timestamp */
} AioAlPcmStatus;

typedef struct _AioAlOps {
    S32 (*init)(VOID);
    S32 (*deinit)(VOID);

    /*
     * Open and prepare a PCM. Rate, channels and format are exact; period
     * and buffer size may be adjusted by the hardware and are reported in
     * pstActual. For playback pstActual->u32BufferFrames is the fill level
     * the backend keeps (min of requested and real buffer): the stream
     * starts by itself once that many frames were written.
     */
    S32 (*pcm_open)(const AioAlPcmCfg *pstCfg, AioAlPcm **ppstPcm, AioAlPcmCfg *pstActual);
    S32 (*pcm_close)(AioAlPcm *pstPcm);

    /* Start a prepared stream; a no-op when it is already running. */
    S32 (*pcm_start)(AioAlPcm *pstPcm);
    /* Stop immediately and drop pending frames. */
    S32 (*pcm_stop)(AioAlPcm *pstPcm);

    /*
     * Block until u32Frames can be read (capture) or written without
     * exceeding the kept fill level (playback). Returns AIO_AL_ERR_WAKE as
     * soon as s32WakeFd becomes readable (the fd is not drained) and
     * AIO_AL_ERR_TIMEOUT after s32TimeoutMs.
     */
    S32 (*pcm_wait)(AioAlPcm *pstPcm, U32 u32Frames, S32 s32WakeFd, S32 s32TimeoutMs);

    /* Non-blocking; return the number of frames transferred or an error. */
    S32 (*pcm_read)(AioAlPcm *pstPcm, VOID *pBuf, U32 u32Frames);
    S32 (*pcm_write)(AioAlPcm *pstPcm, const VOID *pBuf, U32 u32Frames);

    /* Bring the stream back to the prepared state after XRUN / SUSPEND. */
    S32 (*pcm_recover)(AioAlPcm *pstPcm, S32 s32Err);

    S32 (*pcm_get_status)(AioAlPcm *pstPcm, AioAlPcmStatus *pstStatus);
} AioAlOps;

/* The returned pointer must remain valid for the lifetime of the plugin. */
typedef const AioAlOps *(*PFN_al_aio_get_ops)(void);

#ifdef __cplusplus
}
#endif

#endif /* AIO_AL_OPS_H */
