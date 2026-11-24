/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    ai_stub.c
 * @Date      :    2026-11-24
 * @Author    :    SPACEMIT
 * @Brief     :    AI interfaces that are declared in ai_api.h but not
 *                 implemented yet. They fail with SYS_ERR_INVAL, the code
 *                 the audio headers use for unsupported capabilities, so
 *                 applications written against the full header still link.
 *                 Move a function to ai.c when it gets implemented.
 *------------------------------------------------------------------------------
 */

#include "ai_api.h"
#include "log.h"

#define AI_NOT_IMPLEMENTED()                                                            \
    do {                                                                                \
        MPP_LOG(MPP_LOG_ERROR, "ai", "%s is not implemented in this version", __func__); \
        return SYS_ERR_INVAL;                                                           \
    } while (0)

/* ======================== Discovery / device diagnostics ======================== */

S32 AI_EnumDevices(AioDeviceInfoS *pstDevices, U32 u32Capacity, U32 *pu32Count) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_GetEventFd(AI_DEV AiDev) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_GetEvent(AI_DEV AiDev, AudioEventS *pstEvent, S32 s32MilliSec) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_QueryControlCaps(AI_DEV AiDev, AudioControlCapsS *pstCaps, U32 u32Capacity, U32 *pu32Count) {
    AI_NOT_IMPLEMENTED();
}

/* ======================== Channel ======================== */

S32 AI_SetChnParam(AI_DEV AiDev, AI_CHN AiChn, const AiChnParamS *pstChnParam) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_GetChnParam(AI_DEV AiDev, AI_CHN AiChn, AiChnParamS *pstChnParam) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_GetFd(AI_DEV AiDev, AI_CHN AiChn) {
    AI_NOT_IMPLEMENTED();
}

/* ======================== Encoder attachment ======================== */

S32 AI_RegisterEncoder(S32 *ps32Handle, const AiEncoderS *pstEncoder) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_UnRegisterEncoder(S32 s32Handle) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_EnableEncChn(AI_DEV AiDev, AI_CHN AiChn, const AiEncChnAttrS *pstEncAttr) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_DisableEncChn(AI_DEV AiDev, AI_CHN AiChn) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_GetStream(AI_DEV AiDev, AI_CHN AiChn, AudioStreamS *pstStream, S32 s32MilliSec) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_ReleaseStream(AI_DEV AiDev, AI_CHN AiChn, const AudioStreamS *pstStream) {
    AI_NOT_IMPLEMENTED();
}

/* ======================== Volume / gain / mute / track ======================== */

S32 AI_SetVolume(AI_DEV AiDev, S32 s32VolumeDb) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_GetVolume(AI_DEV AiDev, S32 *ps32VolumeDb) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_SetAnalogGain(AI_DEV AiDev, const AudioAnalogGainS *pstSetGain) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_GetAnalogGain(AI_DEV AiDev, AudioAnalogGainS *pstGetGain) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_SetMute(AI_DEV AiDev, BOOL bEnable, const AudioFadeS *pstFade) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_GetMute(AI_DEV AiDev, BOOL *pbEnable, AudioFadeS *pstFade) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_SetTrackMode(AI_DEV AiDev, AudioTrackMode eTrackMode) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_GetTrackMode(AI_DEV AiDev, AudioTrackMode *peTrackMode) {
    AI_NOT_IMPLEMENTED();
}

/* ======================== VQE / AEC ======================== */

S32 AI_SetVqeAttr(AI_DEV AiDev, AI_CHN AiChn, AI_DEV AoDev, S32 AoChn, const AiVqeConfigS *pstVqeConfig) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_GetVqeAttr(AI_DEV AiDev, AI_CHN AiChn, AiVqeConfigS *pstVqeConfig) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_EnableVqe(AI_DEV AiDev, AI_CHN AiChn) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_DisableVqe(AI_DEV AiDev, AI_CHN AiChn) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_EnableAecRef(AI_DEV AiDev, AI_CHN AiChn, AI_DEV AoDev, S32 AoChn) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_DisableAecRef(AI_DEV AiDev, AI_CHN AiChn) {
    AI_NOT_IMPLEMENTED();
}

/* ======================== Debug ======================== */

S32 AI_SaveFile(AI_DEV AiDev, AI_CHN AiChn, const AudioSaveFileInfoS *pstSaveFileInfo) {
    AI_NOT_IMPLEMENTED();
}

S32 AI_QueryFileStatus(AI_DEV AiDev, AI_CHN AiChn, AudioFileStatusS *pstFileStatus) {
    AI_NOT_IMPLEMENTED();
}
