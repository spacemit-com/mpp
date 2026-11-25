/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    ai_type.h
 * @Date      :    2026-10-27
 * @Author    :    SPACEMIT
 * @Brief     :    AI (Audio Input) type definitions for MPP.
 *------------------------------------------------------------------------------
 */

#ifndef AI_TYPE_H
#define AI_TYPE_H

#include "type.h"
#include "sys_type.h"
#include "aio_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== Limits ======================== */

#define AI_INVALID_DEV_ID (-1)
#define AI_INVALID_CHN_ID (-1)

#define AI_MAX_DEV_NUM AIO_MAX_DEV_NUM
#define AI_MAX_CHN_NUM AIO_MAX_CHN_NUM

typedef S32 AI_DEV;
typedef S32 AI_CHN;

/* ======================== Module private error codes ========================
 * Generic errors reuse SYS_ERR_* from sys_type.h; only audio-specific
 * conditions get private negative codes (starting from -100).
 */

#define AI_ERR_ENCODER (-100)  /* encoder plugin internal failure */
#define AI_ERR_VQE (-101)      /* VQE algorithm instance failure */
#define AI_ERR_ABORTED (-102)  /* blocking call interrupted by DisableChn/DeInit */
#define AI_ERR_BUF_EMPTY (-103) /* non-blocking get with empty queue */

/* ======================== Device / channel ======================== */

/*
 * MIC (capture sound-card) routing. When bSwAecEnable is MPP_TRUE the card
 * described by stRefMicAttr feeds the software AEC far-end reference.
 */
typedef struct _MicAttrS {
    AioDevRouteS stMicAttr;
    BOOL bSwAecEnable;
    AioDevRouteS stRefMicAttr;
} MicAttrS;

typedef enum { AUDIO_DROP_OLDEST_QUEUED = 0, AUDIO_DROP_NEWEST = 1 } AudioDropPolicy;

/* Configure before EnableChn. Dropping never invalidates user-held frames.
 * The pool is managed internally from queue depth, held limit and processing
 * needs. A zero map count preserves existing track handling; a nonzero map
 * requires AUDIO_TRACK_NORMAL. Output channel i selects physical input
 * as32ChannelMap[i]; -1 produces silence, duplicates are allowed. Other
 * indices must be less than device u32ChnCnt; map count must be <=
 * AUDIO_MAX_PCM_CHANNELS. Unsupported maps return SYS_ERR_INVAL.
 */
typedef struct _AiChnParamS {
    U32 u32UsrFrmDepth; /* 0 = default 4; max frames the user may hold */
    U32 u32QueueFrames; /* pending frames, 0 = effective u32UsrFrmDepth */
    AudioDropPolicy eDropPolicy; /* default: drop oldest queued frame */
    U32 u32MapCount; /* 0 = default; otherwise output PCM channel count */
    S32 as32ChannelMap[AUDIO_MAX_PCM_CHANNELS];
} AiChnParamS;

typedef struct _AiChnStateS {
    U32 u32ChnTotalNum; /* total pool slots, including processing slots */
    U32 u32ChnFreeNum;  /* free slots */
    U32 u32ChnBusyNum;  /* slots held by the user (not released) */
    U32 u32QueuedFrames; /* pending frames, excludes held/processing slots */
    U32 u32PeakQueuedFrames;
    U32 u32ValidMask; /* AUDIO_STATS_QUEUE / DROP / PROCESS */
    U64 u64Epoch; /* channel generation; changes on re-enable/recovery */
    U64 u64DroppedFrames;
    U64 u64ProcessCalls;
    U64 u64ProcessTotalUs;
    U64 u64ProcessMaxUs;
} AiChnStateS;

/* ======================== VQE ======================== */

typedef struct _AiRnrConfigS {
    BOOL bUsrMode;

    S32 s32NrMode;
    S32 s32MaxNrLevel;
    S32 s32NoiseThresh;
} AiRnrConfigS;

typedef S32 (*PFuncGainCallBack)(S32 s32SetGain);

typedef struct _AiHdrConfigS {
    BOOL bUsrMode;

    S32 s32MinGainDb;
    S32 s32MaxGainDb;

    S32 s32MicGainDb;
    S32 s32MicGainStepDb;
    PFuncGainCallBack pCallBack;
} AiHdrConfigS;

typedef struct _AiDrcConfigS {
    BOOL bUsrMode;

    S16 s16AttackTime;
    S16 s16ReleaseTime;

    S16 s16LevDb[VQE_DRC_SEC_NUM];
} AiDrcConfigS;

/*
 * AI VQE configuration. The processing chain is fixed as
 * HPF -> ANR -> AEC -> AGC -> EQ -> DRC; u32OpenMask selects the active
 * subset. When AIO_VQE_MASK_AEC is set, AoDev/AoChn passed to
 * AI_SetVqeAttr / AI_EnableAecRef must reference an enabled AO channel.
 */
typedef struct _AiVqeConfigS {
    U32 u32OpenMask; /* AIO_VQE_MASK_xxx bitwise OR */
    S32 s32Channels;
    S32 s32WorkSampleRate;
    S32 s32FrameSample;
    VqeWorkState eWorkState;

    AudioHpfConfigS stHpfCfg;
    AudioAnrConfigS stAnrCfg;
    AudioAgcConfigS stAgcCfg;
    AudioEqConfigS stEqCfg;
    AudioVadConfigS stVadCfg;
    AiDrcConfigS stDrcCfg;
} AiVqeConfigS;

/* ======================== Encoder attachment ======================== */

/* Per-codec encoder attributes, pointed to by AiEncChnAttrS.pValue */

typedef struct _AiEncAttrG711S {
    U32 u32Reserved;
} AiEncAttrG711S;

typedef struct _AiEncAttrG726S {
    G726Bps eG726Bps;
} AiEncAttrG726S;

typedef struct _AiEncAttrAdpcmS {
    AdpcmType eAdpcmType;
} AiEncAttrAdpcmS;

typedef struct _AiEncAttrLpcmS {
    U32 u32Reserved;
} AiEncAttrLpcmS;

typedef struct _AiEncAttrAacS {
    S32 s32BitRate;        /* bitrate in bps */
    AudioSampleRate eSampleRate;
    AudioBitWidth eBitWidth;
    AudioSoundMode eSoundMode;
    S16 s16BandWidth;
} AiEncAttrAacS;

typedef struct _AiEncChnAttrS {
    AudioCodecType eCodecType;  /* AUDIO_CODEC_xxx */
    U32 u32PtNumPerFrm;         /* must equal AioDevAttrS.u32PtNumPerFrm */
    U32 u32BufSize;             /* stream ring buffer bytes, 0 = default */
    VOID *pValue;               /* AiEncAttrXxxS, NULL for built-in codecs */
} AiEncChnAttrS;

/*
 * External encoder plugin (function table). Built-in codecs
 * (G711A/U, G726, ADPCM, LPCM) need no registration.
 */
typedef struct _AiEncoderS {
    AudioCodecType eCodecType;
    U32 u32MaxFrmLen; /* max encoded bytes per frame */
    CHAR szName[17];
    S32 (*pfnOpenEncoder)(VOID *pEncAttr, VOID **ppEncoder);
    S32 (*pfnEncodeFrm)(VOID *pEncoder, AudioFrameS *pstFrame,
        U8 *pu8OutBuf, U32 *pu32OutLen);
    S32 (*pfnCloseEncoder)(VOID *pEncoder);
} AiEncoderS;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* AI_TYPE_H */
