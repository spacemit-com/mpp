/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    ao_stub.c
 * @Date      :    2026-11-24
 * @Author    :    SPACEMIT
 * @Brief     :    AO interfaces that are declared in ao_api.h but not
 *                 implemented yet. They fail with SYS_ERR_INVAL, the code
 *                 the audio headers use for unsupported capabilities, so
 *                 applications written against the full header still link.
 *                 Move a function to ao.c when it gets implemented.
 *------------------------------------------------------------------------------
 */

#include "ao_api.h"
#include "log.h"

#define AO_NOT_IMPLEMENTED()                                                            \
    do {                                                                                \
        MPP_LOG(MPP_LOG_ERROR, "ao", "%s is not implemented in this version", __func__); \
        return SYS_ERR_INVAL;                                                           \
    } while (0)

/* ======================== Discovery / device diagnostics ======================== */

S32 AO_EnumDevices(AioDeviceInfoS *pstDevices, U32 u32Capacity, U32 *pu32Count) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_GetEventFd(AO_DEV AoDev) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_GetEvent(AO_DEV AoDev, AudioEventS *pstEvent, S32 s32MilliSec) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_QueryControlCaps(AO_DEV AoDev, AudioControlCapsS *pstCaps, U32 u32Capacity, U32 *pu32Count) {
    AO_NOT_IMPLEMENTED();
}

/* ======================== Channel ======================== */

S32 AO_GetFd(AO_DEV AoDev, AO_CHN AoChn) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_SetChnParam(AO_DEV AoDev, AO_CHN AoChn, const AoChnParamS *pstParam) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_GetChnParam(AO_DEV AoDev, AO_CHN AoChn, AoChnParamS *pstParam) {
    AO_NOT_IMPLEMENTED();
}

/* ======================== Playback control ======================== */

S32 AO_PauseChn(AO_DEV AoDev, AO_CHN AoChn) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_ResumeChn(AO_DEV AoDev, AO_CHN AoChn) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_ClearChnBuf(AO_DEV AoDev, AO_CHN AoChn) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_DrainChn(AO_DEV AoDev, AO_CHN AoChn, S32 s32MilliSec) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_CancelDrainChn(AO_DEV AoDev, AO_CHN AoChn) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_QueryDelay(AO_DEV AoDev, AO_CHN AoChn, AoDelayS *pstDelay) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_QueryPlaybackPosition(AO_DEV AoDev, AO_CHN AoChn, AoPlaybackPositionS *pstPosition) {
    AO_NOT_IMPLEMENTED();
}

/* ======================== Decoder attachment ======================== */

S32 AO_RegisterDecoder(S32 *ps32Handle, const AoDecoderS *pstDecoder) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_UnRegisterDecoder(S32 s32Handle) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_EnableDecChn(AO_DEV AoDev, AO_CHN AoChn, const AoDecChnAttrS *pstDecAttr) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_DisableDecChn(AO_DEV AoDev, AO_CHN AoChn) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_SendStream(AO_DEV AoDev, AO_CHN AoChn, const AudioStreamS *pstStream, BOOL bBlock) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_SendEndOfStream(AO_DEV AoDev, AO_CHN AoChn, BOOL bInstant) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_SendStreamEx(AO_DEV AoDev, AO_CHN AoChn, const AudioStreamS *pstStream, S32 s32MilliSec,
    U32 *pu32ConsumedBytes) {
    AO_NOT_IMPLEMENTED();
}

/* ======================== Volume / gain / mute / track ======================== */

S32 AO_SetChnVolume(AO_DEV AoDev, AO_CHN AoChn, S32 s32VolumeDb, U32 u32FadeMs) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_GetChnVolume(AO_DEV AoDev, AO_CHN AoChn, AudioChnVolumeS *pstVolume) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_SetChnMute(AO_DEV AoDev, AO_CHN AoChn, BOOL bMute, U32 u32FadeMs) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_GetChnMute(AO_DEV AoDev, AO_CHN AoChn, AudioChnMuteS *pstMute) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_SetVolume(AO_DEV AoDev, S32 s32VolumeDb) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_GetVolume(AO_DEV AoDev, S32 *ps32VolumeDb) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_SetAnalogGain(AO_DEV AoDev, const AudioAnalogGainS *pstSetGain) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_GetAnalogGain(AO_DEV AoDev, AudioAnalogGainS *pstGain) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_SetMute(AO_DEV AoDev, BOOL bEnable, const AudioFadeS *pstFade) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_GetMute(AO_DEV AoDev, BOOL *pbEnable, AudioFadeS *pstFade) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_SetTrackMode(AO_DEV AoDev, AudioTrackMode eTrackMode) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_GetTrackMode(AO_DEV AoDev, AudioTrackMode *peTrackMode) {
    AO_NOT_IMPLEMENTED();
}

/* ======================== VQE ======================== */

S32 AO_SetVqeAttr(AO_DEV AoDev, AO_CHN AoChn, const AoVqeConfigS *pstVqeConfig) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_GetVqeAttr(AO_DEV AoDev, AO_CHN AoChn, AoVqeConfigS *pstVqeConfig) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_EnableVqe(AO_DEV AoDev, AO_CHN AoChn) {
    AO_NOT_IMPLEMENTED();
}

S32 AO_DisableVqe(AO_DEV AoDev, AO_CHN AoChn) {
    AO_NOT_IMPLEMENTED();
}
