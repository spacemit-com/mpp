/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    aio_type.h
 * @Date      :    2026-10-27
 * @Author    :    SPACEMIT
 * @Brief     :    Common type definitions for AI (Audio Input) / AO (Audio
 *                 Output) modules of MPP.
 *------------------------------------------------------------------------------
 */

#ifndef AIO_TYPE_H
#define AIO_TYPE_H

#include "type.h"
#include "sys_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== Limits ======================== */

#define AIO_MAX_DEV_NUM 2
#define AIO_MAX_CHN_NUM 8

/* Max bytes per audio sample point (24-bit aligned to 4 bytes) */
#define MAX_AUDIO_POINT_BYTES 4
/* Max sample points per frame (single channel) */
#define MAX_AIO_POINT_NUM 4096
/* Max PCM bytes per frame */
#define MAX_AUDIO_FRAME_LEN (MAX_AUDIO_POINT_BYTES * MAX_AIO_POINT_NUM)
/* Max encoded stream bytes per frame */
#define MAX_AUDIO_STREAM_LEN MAX_AUDIO_FRAME_LEN

#define MAX_AUDIO_FILE_PATH_LEN 256
#define MAX_AUDIO_FILE_NAME_LEN 256

#define AUDIO_MAX_PCM_CHANNELS 8U
#define AUDIO_DEVICE_ID_BYTES 128U
#define AUDIO_DEVICE_NAME_BYTES 128U
#define AUDIO_ALL_CHANNELS (-1)
#define AUDIO_STATS_DROP    (1U << 0)
#define AUDIO_STATS_UNDERRUN (1U << 1)
#define AUDIO_STATS_PROCESS (1U << 3)
#define AUDIO_DELAY_QUEUE   (1U << 0)
#define AUDIO_DELAY_DEVICE  (1U << 1)
#define AUDIO_DELAY_TOTAL   (1U << 2)
#define AUDIO_POS_ACCEPTED  (1U << 0)
#define AUDIO_POS_MIXED     (1U << 1)
#define AUDIO_POS_SUBMITTED (1U << 2)
#define AUDIO_POS_PLAYED    (1U << 3)
#define AUDIO_POS_PTS       (1U << 4)
#define AUDIO_FRAME_DISCONTINUITY (1U << 0)
#define AUDIO_STATS_QUEUE (1U << 5) /* queue occupancy and high-water mark */

/* ======================== Blocking convention ======================== */

/*
 * Blocking time parameter used by AI_GetFrame / AI_GetStream / AO_SendFrame:
 *   AIO_BLOCK_INFINITE : block until data/space is available or the channel
 *                        is disabled (returns AI_ERR_ABORTED / AO_ERR_ABORTED)
 *   AIO_NO_BLOCK       : return immediately
 *   > 0                : timeout in milliseconds, returns SYS_ERR_TIMEOUT
 */
#define AIO_BLOCK_INFINITE (-1)
#define AIO_NO_BLOCK 0

/* ======================== Basic enums ======================== */

typedef enum _AudioSampleRate {
    AUDIO_SAMPLE_RATE_8000 = 8000,
    AUDIO_SAMPLE_RATE_11025 = 11025,
    AUDIO_SAMPLE_RATE_12000 = 12000,
    AUDIO_SAMPLE_RATE_16000 = 16000,
    AUDIO_SAMPLE_RATE_22050 = 22050,
    AUDIO_SAMPLE_RATE_24000 = 24000,
    AUDIO_SAMPLE_RATE_32000 = 32000,
    AUDIO_SAMPLE_RATE_44100 = 44100,
    AUDIO_SAMPLE_RATE_48000 = 48000,
    AUDIO_SAMPLE_RATE_64000 = 64000,
    AUDIO_SAMPLE_RATE_96000 = 96000,
    AUDIO_SAMPLE_RATE_MAX
} AudioSampleRate;

typedef enum _AudioBitWidth {
    AUDIO_BIT_WIDTH_8 = 0,
    AUDIO_BIT_WIDTH_16 = 1,
    AUDIO_BIT_WIDTH_24 = 2,
    AUDIO_BIT_WIDTH_32 = 3,
    AUDIO_BIT_WIDTH_MAX
} AudioBitWidth;

typedef enum _AudioSoundMode {
    AUDIO_SOUND_MODE_MONO = 0,
    AUDIO_SOUND_MODE_STEREO = 1,
    AUDIO_SOUND_MODE_RAW = 2, /* more than two PCM channels; count is explicit */
    AUDIO_SOUND_MODE_MAX
} AudioSoundMode;

/*
 * Channel handling mode.
 * AI: select/mix captured channels (multi-MIC routing).
 * AO: transform left/right of the playback output.
 */
typedef enum _AudioTrackMode {
    AUDIO_TRACK_NORMAL = 0,
    AUDIO_TRACK_BOTH_LEFT = 1,
    AUDIO_TRACK_BOTH_RIGHT = 2,
    AUDIO_TRACK_EXCHANGE = 3,
    AUDIO_TRACK_MIX = 4,
    AUDIO_TRACK_LEFT_MUTE = 5,
    AUDIO_TRACK_RIGHT_MUTE = 6,
    AUDIO_TRACK_BOTH_MUTE = 7,
    AUDIO_TRACK_MAX
} AudioTrackMode;

typedef enum _AudioFadeRate {
    AUDIO_FADE_RATE_1 = 0,
    AUDIO_FADE_RATE_2 = 1,
    AUDIO_FADE_RATE_4 = 2,
    AUDIO_FADE_RATE_8 = 3,
    AUDIO_FADE_RATE_16 = 4,
    AUDIO_FADE_RATE_32 = 5,
    AUDIO_FADE_RATE_64 = 6,
    AUDIO_FADE_RATE_128 = 7,
    AUDIO_FADE_RATE_MAX
} AudioFadeRate;

/* Codec types shared by AI encoders and AO decoders */
typedef enum _AudioCodecType {
    AUDIO_CODEC_G711A = 0,
    AUDIO_CODEC_G711U,
    AUDIO_CODEC_G726,
    AUDIO_CODEC_ADPCM,
    AUDIO_CODEC_LPCM,
    AUDIO_CODEC_AAC,
    AUDIO_CODEC_MP3,
    AUDIO_CODEC_MAX
} AudioCodecType;

typedef enum _G726Bps {
    G726_16K = 0,
    G726_24K,
    G726_32K,
    G726_40K,
    G726_BPS_MAX
} G726Bps;

typedef enum _AdpcmType {
    ADPCM_TYPE_DVI4 = 0,
    ADPCM_TYPE_IMA,
    ADPCM_TYPE_ORG_DVI4,
    ADPCM_TYPE_MAX
} AdpcmType;

/* Zero-initialize input structures. DEFAULT preserves legacy eBitWidth:
 * DEFAULT follows the backend legacy convention for the selected bit width;
 * use an explicit format whenever signedness or storage packing matters.
 * An explicit format must agree with eBitWidth (F32 uses BIT_WIDTH_32).
 * PCM is interleaved, little-endian, with no padding between PCM frames.
 * S24_3LE uses 3 bytes; S24_4LE uses 4 bytes, signed low 24 bits.
 * These additions change structure sizes: rebuild applications and the SDK
 * together. They do not provide binary compatibility with old headers.
 */
typedef enum _AudioPcmFormat {
    AUDIO_PCM_DEFAULT = 0,
    AUDIO_PCM_U8 = 1, AUDIO_PCM_S8 = 2, AUDIO_PCM_S16_LE = 3,
    AUDIO_PCM_S24_3LE = 4, AUDIO_PCM_S24_4LE = 5,
    AUDIO_PCM_S32_LE = 6, AUDIO_PCM_F32_LE = 7
} AudioPcmFormat;

typedef enum {
    AUDIO_DEV_UNKNOWN = 0, AUDIO_DEV_PRESENT = 1, AUDIO_DEV_ENABLED = 2,
    AUDIO_DEV_BUSY = 3, AUDIO_DEV_DISCONNECTED = 4
} AudioDeviceState;
typedef enum {
    AUDIO_CTRL_MASTER_VOLUME_DB = 0, AUDIO_CTRL_CHN_VOLUME_DB = 1,
    AUDIO_CTRL_HW_GAIN_DB = 2, AUDIO_CTRL_HW_MUTE = 3,
    AUDIO_CTRL_INPUT_ROUTE = 4, AUDIO_CTRL_OUTPUT_ROUTE = 5,
    AUDIO_CTRL_MASTER_MUTE = 6, AUDIO_CTRL_CHN_MUTE = 7
} AudioControlId;
typedef enum { AUDIO_UNIT_DB = 0, AUDIO_UNIT_BOOL = 1, AUDIO_UNIT_INDEX = 2 } AudioControlUnit;
typedef enum {
    AUDIO_EVENT_XRUN = 0, AUDIO_EVENT_DROP = 1, AUDIO_EVENT_UNDERRUN = 2,
    AUDIO_EVENT_DEVICE_LOST = 3, AUDIO_EVENT_DEVICE_READY = 4,
    AUDIO_EVENT_DISCONTINUITY = 5, AUDIO_EVENT_DRAIN_DONE = 6,
    AUDIO_EVENT_OVERFLOW = 7, AUDIO_EVENT_STOPPED = 8, AUDIO_EVENT_ERROR = 9
} AudioEventType;

/* ======================== Device attribute ======================== */

/*
 * Device public attribute, shared by AI and AO devices.
 * Must be set (AI/AO_SetDevAttr) before AI/AO_EnableDev.
 */
typedef struct _AioDevAttrS {
    AudioSampleRate eSampleRate; /* sample rate */
    AudioBitWidth eBitWidth;     /* bit width, built-in codecs support 16-bit */
    AudioSoundMode eSoundMode;   /* MONO for 1, STEREO for 2, RAW for >2 channels */
    U32 u32FrmNum;               /* pcm device buffer depth in frames, [2, 10] */
    U32 u32PtNumPerFrm;          /* sample points per frame (single channel) */
    U32 u32ChnCnt;               /* physical PCM channels, 1..AUDIO_MAX_PCM_CHANNELS; */
                                 /* must agree with MIC/SPK route s32Chns */
    AudioPcmFormat ePcmFormat;   /* DEFAULT: derive from eBitWidth */
} AioDevAttrS;

/* Sound-card routing entry shared by MicAttrS / SpkAttrS */
typedef struct _AioDevRouteS {
    S32 s32Dev;  /* pcm device index in the HAL layer */
    S32 s32Chns; /* physical channels: MIC count for AI, SPK count for AO */
} AioDevRouteS;

/* ======================== Frame / stream ======================== */

/*
 * Zero-copy audio frame. pu8Data points into an internal queue slot owned by
 * the framework; it stays valid until AI_ReleaseFrame / until AO_SendFrame
 * returns (AO copies on send). Never cache the pointer across Release.
 * For AI, return the complete descriptor unchanged, exactly once. Epoch is
 * not a per-slot allocation token and does not authorize releasing stale copies.
 * AO ignores u32SlotId/u64Epoch. u32Len must be divisible by
 * s32Channels * storage_bytes(ePcmFormat); the quotient is samples/channel.
 * s32Channels is the PCM channel count; eSoundMode must agree with it.
 * Total u32Len must not exceed MAX_AUDIO_FRAME_LEN; samples/channel must
 * also fit MAX_AIO_POINT_NUM. Resolve DEFAULT before calculating storage
 * size; unsupported formats or frame sizes are rejected by the backend.
 */
typedef struct _AudioFrameS {
    AudioBitWidth eBitWidth;
    AudioSoundMode eSoundMode;
    AudioSampleRate eSampleRate;
    S32 s32Channels;
    U8 *pu8Data;   /* frame payload, in-process virtual address */
    U32 u32Len;    /* payload length in bytes */
    U64 u64PTS;    /* PTS of the first sample, in milliseconds */
    U32 u32Seq;    /* frame sequence number, gaps mean frame drops */
    U32 u32SlotId; /* internal queue slot id, checked on release */
    AudioPcmFormat ePcmFormat; /* DEFAULT: derive from eBitWidth */
    U32 u32Flags;  /* AUDIO_FRAME_*; unknown bits must be zero */
    U64 u64Epoch;  /* capture generation; changes on restart/reconnect */
} AudioFrameS;

/* Encoded stream frame, same zero-copy lifetime rule as AudioFrameS */
typedef struct _AudioStreamS {
    U8 *pu8Data;
    U32 u32Len;
    U64 u64PTS;
    U32 u32Seq;
    U32 u32SlotId;
} AudioStreamS;

/* AEC reference frame delivered together with the captured frame */
typedef struct _AecFrameS {
    AudioFrameS stRefFrame; /* bypassed AO playback frame */
    BOOL bValid;            /* MPP_FALSE when no valid reference exists */
} AecFrameS;

typedef struct _AudioFadeS {
    BOOL bFade;
    AudioFadeRate eFadeInRate;
    AudioFadeRate eFadeOutRate;
} AudioFadeS;

typedef struct _AudioAnalogGainS {
    S32 s32LeftValue;  /* current left gain, dB */
    S32 s32RightValue; /* current right gain, dB */
    S32 s32MaxGain;    /* maximum gain, dB */
    S32 s32MinGain;    /* minimum gain, dB */
} AudioAnalogGainS;

/* ======================== Device discovery / diagnostics ======================== */
typedef struct _AioDeviceInfo {
    S32 s32DevId;
    BOOL bStableIdValid;
    U8 au8StableId[AUDIO_DEVICE_ID_BYTES]; /* NUL-terminated if valid */
    U8 au8PcmName[AUDIO_DEVICE_NAME_BYTES]; /* NUL-terminated backend name */
    AudioDeviceState eState;
} AioDeviceInfoS;

typedef struct _AudioEvent {
    AudioEventType eType;
    S32 s32DevId;
    S32 s32ChnId; /* AUDIO_ALL_CHANNELS for device-wide events */
    U64 u64Seq; /* device event sequence, monotonic within device epoch */
    U64 u64PTS; /* monotonic milliseconds at event generation */
    U64 u64Epoch; /* device generation; changes on reopen/recovery */
    S32 s32Reason; /* error code, or 0 */
    U64 u64Value; /* DROP: lost frames; UNDERRUN: starved periods; */
                  /* XRUN: occurrences; OVERFLOW: lost events; otherwise 0 */
} AudioEventS;

/* dB values are integer dB; BOOL values are 0/1; INDEX values are route IDs.
 * Query advertises only controls supported by the device and direction.
 */
typedef struct _AudioControlCaps {
    AudioControlId eControlId;
    AudioControlUnit eUnit;
    U32 u32ChannelMask; /* CHN_*: bit n is logical channel n; hardware */
                        /* controls: physical channel n; 0: device-wide */
    S32 s32MinValue;
    S32 s32MaxValue;
    S32 s32Step;
    S32 s32DefaultValue;
    BOOL bDefaultValid;
    BOOL bReadable;
    BOOL bWritable;
    BOOL bRuntimeWritable;
} AudioControlCapsS;

/* ======================== VQE (Voice Quality Enhancement) ======================== */

#define AIO_VQE_MASK_AEC 0x01 /* AI only */
#define AIO_VQE_MASK_ANR 0x02
#define AIO_VQE_MASK_AGC 0x04
#define AIO_VQE_MASK_EQ 0x08
#define AIO_VQE_MASK_HPF 0x10
#define AIO_VQE_MASK_HDR 0x20 /* AI only */
#define AIO_VQE_MASK_DRC 0x40 /* AI only */
#define AIO_VQE_MASK_RNR 0x80 /* AI only */
#define AIO_VQE_MASK_VAD 0x0100 /* AI only */
#define AIO_VQE_MASK_RES 0x1000 /* resample hint, reserved */

#define VQE_EQ_BAND_NUM 10
#define VQE_DRC_SEC_NUM 5

typedef enum _VqeWorkState {
    VQE_WORK_STATE_COMMON = 0,
    VQE_WORK_STATE_MUSIC = 1,
    VQE_WORK_STATE_NOISY = 2,
} VqeWorkState;

typedef enum _AudioHpfFreq {
    AUDIO_HPF_FREQ_80 = 80,
    AUDIO_HPF_FREQ_100 = 100,
    AUDIO_HPF_FREQ_120 = 120,
    AUDIO_HPF_FREQ_150 = 150,
    AUDIO_HPF_FREQ_MAX
} AudioHpfFreq;

typedef struct _AudioHpfConfigS {
    BOOL bUsrMode; /* MPP_FALSE: auto, MPP_TRUE: manual */
    AudioHpfFreq eHpfFreq;
} AudioHpfConfigS;

typedef struct _AudioAnrConfigS {
    BOOL bUsrMode;
    S32 s32Reserved;
} AudioAnrConfigS;

typedef struct _AudioAgcConfigS {
    BOOL bUsrMode; /* MPP_FALSE: auto, MPP_TRUE: manual */

    S16 s16TargetLevel; /* target level in -dBfs, default -3, range [0, 31] dB */
    S16 s16MaxGain;     /* max gain, default 20, range [0, 30] dB */

    S32 s32Reserved;
} AudioAgcConfigS;

typedef struct _AudioVadConfigS {
    BOOL bUsrMode;

    S32 s32NoiseFloor; /* noise floor, default 8000, range [0, 2^18] */
    S32 s32DelayMs;    /* delay in ms, default 1000, range [0, 5000] ms */

    S32 s32Reserved;
} AudioVadConfigS;

typedef struct _AudioEqConfigS {
    S8 s8GainDb[VQE_EQ_BAND_NUM]; /* gain per band, dB */
    S32 s32Reserved;
} AudioEqConfigS;

/* ======================== Debug helpers ======================== */

typedef struct _AudioSaveFileInfoS {
    BOOL bCfg; /* MPP_TRUE: start/continue saving, MPP_FALSE: stop and close */
    CHAR szFilePath[MAX_AUDIO_FILE_PATH_LEN];
    CHAR szFileName[MAX_AUDIO_FILE_NAME_LEN];
    U32 u32FileSize; /* max file size in bytes, 0 = unlimited */
} AudioSaveFileInfoS;

typedef struct _AudioFileStatusS {
    BOOL bSaving;
} AudioFileStatusS;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* AIO_TYPE_H */
