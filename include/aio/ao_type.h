/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    ao_type.h
 * @Date      :    2026-10-27
 * @Author    :    SPACEMIT
 * @Brief     :    AO (Audio Output) type definitions for MPP.
 *------------------------------------------------------------------------------
 */

#ifndef AO_TYPE_H
#define AO_TYPE_H

#include "type.h"
#include "sys_type.h"
#include "aio_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== Limits ======================== */

#define AO_INVALID_DEV_ID (-1)
#define AO_INVALID_CHN_ID (-1)

#define AO_MAX_DEV_NUM AIO_MAX_DEV_NUM
#define AO_MAX_CHN_NUM AIO_MAX_CHN_NUM

typedef S32 AO_DEV;
typedef S32 AO_CHN;

/* ======================== Module private error codes ========================
 * Generic errors reuse SYS_ERR_* from sys_type.h; only audio-specific
 * conditions get private negative codes (starting from -100).
 */

#define AO_ERR_DECODER (-100) /* decoder plugin internal failure */
#define AO_ERR_BUF_LACK (-101) /* decoder input not enough to emit a frame */
#define AO_ERR_ABORTED (-102)  /* blocking call interrupted by DisableChn/DeInit */

/* ======================== Device / channel ======================== */

/*
 * SPK (playback sound-card) routing. When bSwAecEnable is MPP_TRUE the card
 * described by stDumySpkAttr acts as a virtual (dummy) output for AEC use.
 */
typedef struct _SpkAttrS {
    AioDevRouteS stSpkAttr;
    BOOL bSwAecEnable;
    AioDevRouteS stDumySpkAttr;
} SpkAttrS;

/* Set before EnableChn. Zero queue depth keeps the legacy 4*u32FrmNum.
 * If both frame and duration limits are set, both must be satisfied.
 * No input-byte limit: decoder buffering uses AoDecChnAttrS.u32BufSize.
 * Paused channels continue accepting data subject to queue limits.
 * A zero map count preserves track handling; a nonzero map requires
 * AUDIO_TRACK_NORMAL and must equal the device physical channel count.
 * Output channel i selects input channel as32ChannelMap[i]; -1 is silence.
 * Duplicates are allowed. Other indices must fit the submitted/decoded
 * frame channel count; invalid maps/frames return SYS_ERR_INVAL.
 */
typedef struct _AoChnParamS {
    U32 u32QueueFrames;
    U32 u32MaxQueueMs; /* 0 = no additional duration limit */
    U32 u32MapCount;
    S32 as32ChannelMap[AUDIO_MAX_PCM_CHANNELS];
} AoChnParamS;

typedef struct _AoChnStateS {
    U32 u32ChnTotalNum; /* total send-queue depth in frames */
    U32 u32ChnFreeNum;  /* free frames */
    U32 u32ChnBusyNum;  /* occupied frames */
    U32 u32PeakQueuedFrames;
    U32 u32ValidMask; /* AUDIO_STATS_QUEUE / UNDERRUN / PROCESS */
    U64 u64Epoch; /* channel generation, same as playback position */
    U64 u64UnderrunPeriods; /* channel starved while active, excludes pause/EOS */
    U64 u64ProcessCalls;
    U64 u64ProcessTotalUs;
    U64 u64ProcessMaxUs;
} AoChnStateS;

/* Counters/snapshots are meaningful only within the same epoch.
 * Invalid fields are zero with their validity bit clear; never present an
 * estimate as measured hardware playback. Snapshot PTS is monotonic ms.
 * Delay is in microseconds. Shared-device delay is not additive across
 * channels; total includes software queue/conversion plus device delay.
 */
typedef struct _AoDelay {
    U32 u32ValidMask; /* AUDIO_DELAY_* */
    U64 u64Epoch;
    U64 u64SnapshotPTS;
    U64 u64QueueDelayUs;
    U64 u64SharedDeviceDelayUs;
    U64 u64TotalDelayUs;
} AoDelayS;

/* Samples are per channel. Accepted uses input rate; mixed/submitted/played
 * use output rate. PlayedPTS uses the submitted stream PTS domain (ms).
 * Counters reset on a new playback epoch (enable, clear, recovery or new
 * input after completed drain/EOS). Rate fields accompany the snapshot.
 */
typedef struct _AoPlaybackPosition {
    U32 u32ValidMask; /* AUDIO_POS_*; state flags always valid */
    U64 u64Epoch;
    U64 u64SnapshotPTS;
    U32 u32InputSampleRate;
    U32 u32OutputSampleRate;
    U64 u64AcceptedInputSamples;
    U64 u64MixedOutputSamples;
    U64 u64SubmittedOutputSamples;
    U64 u64PlayedOutputSamples;
    U64 u64PlayedPTS;
    BOOL bDraining;
    BOOL bInputEnded;
} AoPlaybackPositionS;

/* Target/current software channel gain; dB and milli-dB respectively. */
typedef struct _AudioChnVolume {
    S32 s32TargetVolumeDb;
    S32 s32CurrentVolumeMilliDb;
    U32 u32RemainingFadeMs;
} AudioChnVolumeS;

/* Current mute gain: 0 = silent, 65536 = unity. */
typedef struct _AudioChnMute {
    BOOL bTargetMute;
    U32 u32CurrentMuteGainQ16;
    U32 u32RemainingFadeMs;
} AudioChnMuteS;

/* ======================== VQE ======================== */

typedef struct _AoVqeConfigS {
    U32 u32OpenMask; /* AIO_VQE_MASK_xxx bitwise OR (HPF/ANR/AGC/EQ) */

    S32 s32WorkSampleRate;
    S32 s32FrameSample;
    VqeWorkState eWorkState;

    AudioHpfConfigS stHpfCfg;
    AudioAnrConfigS stAnrCfg;
    AudioAgcConfigS stAgcCfg;
    AudioEqConfigS stEqCfg;
} AoVqeConfigS;

/* ======================== Decoder attachment ======================== */

/* Stream feeding mode of the attached decoder */
typedef enum _AoDecMode {
    AO_DEC_MODE_PACK = 0, /* one AO_SendStream call carries whole coded frames */
    AO_DEC_MODE_STREAM,   /* arbitrary length input, packed internally (AAC/MP3) */
    AO_DEC_MODE_MAX
} AoDecMode;

/* Per-codec decoder attributes, pointed to by AoDecChnAttrS.pValue */

typedef struct _AoDecAttrG711S {
    U32 u32Reserved;
} AoDecAttrG711S;

typedef struct _AoDecAttrG726S {
    G726Bps eG726Bps;
} AoDecAttrG726S;

typedef struct _AoDecAttrAdpcmS {
    AdpcmType eAdpcmType;
} AoDecAttrAdpcmS;

typedef struct _AoDecAttrLpcmS {
    U32 u32Reserved;
} AoDecAttrLpcmS;

typedef struct _AoDecAttrAacS {
    U32 u32Reserved;
} AoDecAttrAacS;

typedef struct _AoDecChnAttrS {
    AudioCodecType eCodecType; /* AUDIO_CODEC_xxx */
    U32 u32BufSize;            /* stream ring buffer bytes, 0 = default */
    AoDecMode eMode;           /* AO_DEC_MODE_PACK / AO_DEC_MODE_STREAM */
    VOID *pValue;              /* AoDecAttrXxxS, NULL for built-in codecs */
} AoDecChnAttrS;

/*
 * External decoder plugin (function table). Built-in codecs
 * (G711A/U, G726, ADPCM, LPCM) need no registration.
 */
typedef struct _AoDecoderS {
    AudioCodecType eCodecType;
    CHAR szName[17];
    S32 (*pfnOpenDecoder)(VOID *pDecAttr, VOID **ppDecoder);
    S32 (*pfnDecodeFrm)(VOID *pDecoder, U8 **ppu8InBuf, S32 *ps32LeftByte,
        U16 *pu16OutBuf, U32 *pu32OutLen, U32 *pu32Chns);
    S32 (*pfnGetFrmInfo)(VOID *pDecoder, VOID *pInfo);
    S32 (*pfnCloseDecoder)(VOID *pDecoder);
    S32 (*pfnResetDecoder)(VOID *pDecoder);
} AoDecoderS;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* AO_TYPE_H */
