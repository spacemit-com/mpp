/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    ai_api.h
 * @Date      :    2026-10-27
 * @Author    :    SPACEMIT
 * @Brief     :    AI (Audio Input) public API for MPP.
 *------------------------------------------------------------------------------
 */

#ifndef AI_API_H
#define AI_API_H

#include "type.h"
#include "aio_type.h"
#include "ai_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== Module / device ======================== */

/**
 * @description: Initialize the AI module, allocate module-level resources and
 *               load the pcm backend. Must be called before any other AI
 *               interface, and should only be called once.
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_Init(VOID);

/**
 * @description: Deinitialize the AI module and release all resources. All
 *               devices must be disabled (AI_DisableDev) beforehand, otherwise
 *               SYS_ERR_BUSY is returned. Paired with AI_Init.
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_DeInit(VOID);

/**
 * @description: Set AI device attributes (sample rate, bit width, sound mode,
 *               frame depth, frame size, channel count). Must be called before
 *               AI_EnableDev; the attribute is read-only once the device is
 *               enabled.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AioDevAttrS *} pstDevAttr Pointer to the device attribute
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_SetDevAttr(AI_DEV AiDev, const AioDevAttrS *pstDevAttr);

/**
 * @description: Get current AI device attributes.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AioDevAttrS *} pstDevAttr Output parameter to receive the attribute
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_GetDevAttr(AI_DEV AiDev, AioDevAttrS *pstDevAttr);

/**
 * @description: Enable the AI device: open the pcm capture device and start
 *               the internal capture thread. Must be called after AI_SetDevAttr.
 * @param {AI_DEV} AiDev AI device ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_EnableDev(AI_DEV AiDev);

/**
 * @description: Disable the AI device, stop the capture thread and close the
 *               pcm device. Paired with AI_EnableDev. All channels should be
 *               disabled first; enabled channels are disabled implicitly.
 * @param {AI_DEV} AiDev AI device ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_DisableDev(AI_DEV AiDev);

/**
 * @description: Set MIC routing (pcm capture card selection and MIC count).
 *               When bSwAecEnable is MPP_TRUE, stRefMicAttr selects the
 *               reference card for software AEC. Must be called before
 *               AI_EnableDev. Defaults: s32Dev = 0, s32Chns = 1.
 * @param {AI_DEV} AiDev AI device ID
 * @param {MicAttrS *} pstMicAttr Pointer to the MIC routing attribute
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_SetMicAttr(AI_DEV AiDev, const MicAttrS *pstMicAttr);

/**
 * @description: Get current MIC routing attribute.
 * @param {AI_DEV} AiDev AI device ID
 * @param {MicAttrS *} pstMicAttr Output parameter to receive the attribute
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_GetMicAttr(AI_DEV AiDev, MicAttrS *pstMicAttr);

/* ======================== Discovery / device diagnostics ======================== */

/* Call after AI_Init. NULL array with capacity 0 queries required count.
 * pu32Count is required and always receives the total available count on
 * success or SYS_ERR_FULL. An undersized array receives a prefix and FULL;
 * enumeration may change between calls. IDs are logical AI/AO device IDs,
 * not the backend indices in MicAttrS/SpkAttrS. Empty capacity probes succeed.
 */
S32 AI_EnumDevices(AioDeviceInfoS *pstDevices, U32 u32Capacity, U32 *pu32Count);

/* Device event queue, distinct from the channel data-ready/writable GetFd.
 * Available after EnableDev. Returns fd >= 0 or a negative error code.
 * Framework owns the fd: do not close/read it; consume with GetEvent.
 * Invalid after DisableDev. Readability means at least one queued event.
 */
S32 AI_GetEventFd(AI_DEV AiDev);

/* Pop one event after EnableDev. Timeout uses AIO_BLOCK_INFINITE /
 * AIO_NO_BLOCK / ms. Device loss leaves the event queue accessible until
 * explicit DisableDev, so DEVICE_LOST can be consumed.
 * Empty nonblocking queue returns SYS_ERR_TIMEOUT. DisableDev wakes waiters
 * with AI_ERR_ABORTED. Queue overflow drops oldest events; sequence
 * gaps within the same device epoch expose lost events.
 */
S32 AI_GetEvent(AI_DEV AiDev, AudioEventS *pstEvent, S32 s32MilliSec);

/* Same count/capacity convention as EnumDevices. Query supported controls;
 * does not change gain, mute or routing. Call after Init.
 */
S32 AI_QueryControlCaps(AI_DEV AiDev, AudioControlCapsS *pstCaps,
    U32 u32Capacity, U32 *pu32Count);

/* ======================== Channel ======================== */

/**
 * @description: Enable one AI channel: allocate its frame ring queue (depth
 *               from AiChnParamS.u32QueueFrames, default 4) and start
 *               receiving device data. Must be called after AI_EnableDev.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_EnableChn(AI_DEV AiDev, AI_CHN AiChn);

/**
 * @description: Disable one AI channel, release its frame queue and reset any
 *               attached encoder/VQE. Attached encoder must be disabled first,
 *               otherwise SYS_ERR_BUSY is returned. Paired with AI_EnableChn.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_DisableChn(AI_DEV AiDev, AI_CHN AiChn);

/**
 * @description: Set held-frame limit, pending queue depth, drop policy and
 *               optional physical input channel map. Must be called
 *               before AI_EnableChn.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AiChnParamS *} pstChnParam Pointer to the channel parameter
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_SetChnParam(AI_DEV AiDev, AI_CHN AiChn, const AiChnParamS *pstChnParam);

/**
 * @description: Get effective channel parameters, resolving zero defaults.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AiChnParamS *} pstChnParam Output parameter
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_GetChnParam(AI_DEV AiDev, AI_CHN AiChn, AiChnParamS *pstChnParam);

/**
 * @description: Query pool occupancy, pending/held frames and channel counters.
 *               Counters/high-water marks reset on a new channel epoch.
 *               Legacy pool counts are always valid; u32ValidMask selects
 *               the supported extended fields. Unsupported fields are zero.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AiChnStateS *} pstStatus Output parameter
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_QueryChnStat(AI_DEV AiDev, AI_CHN AiChn, AiChnStateS *pstStatus);

/**
 * @description: Get the data-ready eventfd of the channel for
 *               select/poll/epoll integration. Readable when a frame is ready;
 *               then call AI_GetFrame with AIO_NO_BLOCK. The fd is owned by
 *               the framework, do not close it; it becomes invalid after
 *               AI_DisableChn.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @return {S32} Nonnegative fd on success, negative error code on failure
 */
S32 AI_GetFd(AI_DEV AiDev, AI_CHN AiChn);

/* ======================== Frame capture ======================== */

/**
 * @description: Get one PCM frame (after VQE processing). Zero-copy:
 *               pstFrame->pu8Data points into an internal queue slot and must
 *               be returned with AI_ReleaseFrame. pstAecFrame may be NULL;
 *               when non-NULL and no valid reference exists, its bValid is
 *               MPP_FALSE (AEC degenerates to pass-through).
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AudioFrameS *} pstFrame Output frame
 * @param {AecFrameS *} pstAecFrame Output AEC reference frame, NULL to skip
 * @param {S32} s32MilliSec AIO_BLOCK_INFINITE / AIO_NO_BLOCK / timeout in ms
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_GetFrame(AI_DEV AiDev, AI_CHN AiChn, AudioFrameS *pstFrame,
    AecFrameS *pstAecFrame, S32 s32MilliSec);

/**
 * @description: Release a frame obtained by AI_GetFrame. Each frame must be
 *               released exactly once; the pu8Data pointer becomes invalid
 *               after this call.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AudioFrameS *} pstFrame Frame to release
 * @param {AecFrameS *} pstAecFrame AEC reference frame to release (same one
 *                                  passed to AI_GetFrame, NULL if not taken)
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_ReleaseFrame(AI_DEV AiDev, AI_CHN AiChn,
    const AudioFrameS *pstFrame, const AecFrameS *pstAecFrame);

/* ======================== Encoder attachment ======================== */

/**
 * @description: Register an external encoder plugin. Built-in codecs
 *               (G711A/U, G726, ADPCM, LPCM) need no registration. After
 *               registration the eCodecType can be used in AiEncChnAttrS.
 * @param {S32 *} ps32Handle Output plugin handle
 * @param {AiEncoderS *} pstEncoder Encoder function table
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_RegisterEncoder(S32 *ps32Handle, const AiEncoderS *pstEncoder);

/**
 * @description: Unregister an encoder plugin. Fails with SYS_ERR_BUSY while
 *               any channel still uses the plugin.
 * @param {S32} s32Handle Plugin handle from AI_RegisterEncoder
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_UnRegisterEncoder(S32 s32Handle);

/**
 * @description: Attach an encoder to an enabled channel. The capture thread
 *               encodes inline after VQE and pushes into the stream ring
 *               queue; read it with AI_GetStream. AI_GetFrame keeps working
 *               (PCM and stream outputs are parallel).
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AiEncChnAttrS *} pstEncAttr Encoder attribute
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_EnableEncChn(AI_DEV AiDev, AI_CHN AiChn, const AiEncChnAttrS *pstEncAttr);

/**
 * @description: Detach the encoder from the channel. Must be called before
 *               AI_DisableChn, otherwise SYS_ERR_BUSY is returned.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_DisableEncChn(AI_DEV AiDev, AI_CHN AiChn);

/**
 * @description: Get one encoded stream frame. Zero-copy semantics and slot
 *               lifetime rules are the same as AI_GetFrame.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AudioStreamS *} pstStream Output stream frame
 * @param {S32} s32MilliSec AIO_BLOCK_INFINITE / AIO_NO_BLOCK / timeout in ms
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_GetStream(AI_DEV AiDev, AI_CHN AiChn, AudioStreamS *pstStream,
    S32 s32MilliSec);

/**
 * @description: Release a stream frame obtained by AI_GetStream.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AudioStreamS *} pstStream Stream frame to release
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_ReleaseStream(AI_DEV AiDev, AI_CHN AiChn, const AudioStreamS *pstStream);

/* ======================== Volume / gain / mute / track ======================== */

/**
 * @description: Set digital volume of the capture device, in dB.
 * @param {AI_DEV} AiDev AI device ID
 * @param {S32} s32VolumeDb Volume in dB
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_SetVolume(AI_DEV AiDev, S32 s32VolumeDb);

/**
 * @description: Get current digital volume, in dB.
 * @param {AI_DEV} AiDev AI device ID
 * @param {S32 *} ps32VolumeDb Output volume
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_GetVolume(AI_DEV AiDev, S32 *ps32VolumeDb);

/**
 * @description: Set analog gain of the sound-card codec, takes effect
 *               immediately. Valid range is reported by AI_GetAnalogGain.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AudioAnalogGainS *} pstSetGain Gain to set
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_SetAnalogGain(AI_DEV AiDev, const AudioAnalogGainS *pstSetGain);

/**
 * @description: Get analog gain and its valid range.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AudioAnalogGainS *} pstGetGain Output gain
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_GetAnalogGain(AI_DEV AiDev, AudioAnalogGainS *pstGetGain);

/**
 * @description: Mute the capture device. Frames keep flowing with zero
 *               content while PTS/sequence stay continuous. pstFade may be
 *               NULL for an immediate switch.
 * @param {AI_DEV} AiDev AI device ID
 * @param {BOOL} bEnable MPP_TRUE to mute
 * @param {AudioFadeS *} pstFade Fade rates, NULL for immediate
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_SetMute(AI_DEV AiDev, BOOL bEnable, const AudioFadeS *pstFade);

/**
 * @description: Get current mute state and fade configuration.
 * @param {AI_DEV} AiDev AI device ID
 * @param {BOOL *} pbEnable Output mute state
 * @param {AudioFadeS *} pstFade Output fade configuration
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_GetMute(AI_DEV AiDev, BOOL *pbEnable, AudioFadeS *pstFade);

/**
 * @description: Set multi-MIC handling mode (select left/right, mix, exchange
 *               and so on). Can be switched at runtime.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AudioTrackMode} eTrackMode Track mode
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_SetTrackMode(AI_DEV AiDev, AudioTrackMode eTrackMode);

/**
 * @description: Get current track mode.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AudioTrackMode *} peTrackMode Output track mode
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_GetTrackMode(AI_DEV AiDev, AudioTrackMode *peTrackMode);

/* ======================== VQE / AEC / resample ======================== */

/**
 * @description: Set VQE attributes of the channel. Must be called before
 *               AI_EnableChn. When u32OpenMask contains AIO_VQE_MASK_AEC,
 *               AoDev/AoChn must reference the AO channel used as far-end
 *               reference; pass AI_INVALID_DEV_ID otherwise.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AI_DEV} AoDev AO device ID of the AEC reference
 * @param {S32} AoChn AO channel ID of the AEC reference
 * @param {AiVqeConfigS *} pstVqeConfig VQE configuration
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_SetVqeAttr(AI_DEV AiDev, AI_CHN AiChn, AI_DEV AoDev, S32 AoChn,
    const AiVqeConfigS *pstVqeConfig);

/**
 * @description: Get VQE attributes of the channel.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AiVqeConfigS *} pstVqeConfig Output VQE configuration
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_GetVqeAttr(AI_DEV AiDev, AI_CHN AiChn, AiVqeConfigS *pstVqeConfig);

/**
 * @description: Enable VQE processing on the channel. Must be called after
 *               AI_SetVqeAttr and AI_EnableChn.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_EnableVqe(AI_DEV AiDev, AI_CHN AiChn);

/**
 * @description: Disable VQE processing, raw frames pass through.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_DisableVqe(AI_DEV AiDev, AI_CHN AiChn);

/**
 * @description: Enable software resampling on the channel: output frames use
 *               eOutSampleRate and proportionally scaled frame size. Used
 *               when the sound card rate differs from the application rate.
 *               Must be called before AI_EnableChn.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AudioSampleRate} eOutSampleRate Output sample rate
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_EnableReSmp(AI_DEV AiDev, AI_CHN AiChn, AudioSampleRate eOutSampleRate);

/**
 * @description: Disable software resampling.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_DisableReSmp(AI_DEV AiDev, AI_CHN AiChn);

/**
 * @description: Bypass what the AO channel is playing as the far-end AEC
 *               reference of this capture channel, through an in-process
 *               shared ring (no kernel involvement). Must be called before
 *               AI_EnableChn; the target AO channel must already be enabled
 *               (AO_EnableChn). When the AO channel is disabled the reference
 *               becomes invalid (bValid = MPP_FALSE) and recovers
 *               automatically once it is enabled again.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AI_DEV} AoDev AO device ID of the reference
 * @param {S32} AoChn AO channel ID of the reference
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_EnableAecRef(AI_DEV AiDev, AI_CHN AiChn, AI_DEV AoDev, S32 AoChn);

/**
 * @description: Stop using the AO channel as AEC reference.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_DisableAecRef(AI_DEV AiDev, AI_CHN AiChn);

/* ======================== Debug ======================== */

/**
 * @description: Save the channel PCM to a file (debug helper).
 *               pstSaveFileInfo->bCfg = MPP_TRUE starts saving, MPP_FALSE
 *               stops and closes the file; saving stops automatically when
 *               u32FileSize is reached. Only one channel may save at a time.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AudioSaveFileInfoS *} pstSaveFileInfo Save configuration
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_SaveFile(AI_DEV AiDev, AI_CHN AiChn, const AudioSaveFileInfoS *pstSaveFileInfo);

/**
 * @description: Query whether the channel is currently saving a file.
 * @param {AI_DEV} AiDev AI device ID
 * @param {AI_CHN} AiChn AI channel ID
 * @param {AudioFileStatusS *} pstFileStatus Output status
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AI_QueryFileStatus(AI_DEV AiDev, AI_CHN AiChn, AudioFileStatusS *pstFileStatus);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* AI_API_H */
