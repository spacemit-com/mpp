/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    ao_api.h
 * @Date      :    2026-10-27
 * @Author    :    SPACEMIT
 * @Brief     :    AO (Audio Output) public API for MPP.
 *------------------------------------------------------------------------------
 */

#ifndef AO_API_H
#define AO_API_H

#include "type.h"
#include "aio_type.h"
#include "ao_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== Module / device ======================== */

/**
 * @description: Initialize the AO module, allocate module-level resources and
 *               load the pcm backend. Must be called before any other AO
 *               interface, and should only be called once.
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_Init(VOID);

/**
 * @description: Deinitialize the AO module and release all resources. All
 *               devices must be disabled (AO_DisableDev) beforehand, otherwise
 *               SYS_ERR_BUSY is returned. Paired with AO_Init.
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_DeInit(VOID);

/**
 * @description: Set AO device attributes (sample rate, bit width, sound mode,
 *               frame depth, frame size, channel count). u32PtNumPerFrm is the
 *               playback frame granularity; the send queue depth in frames is
 *               4 * u32FrmNum unless overridden by AO_SetChnParam.
 *               Must be called before AO_EnableDev.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AioDevAttrS *} pstDevAttr Pointer to the device attribute
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_SetDevAttr(AO_DEV AoDev, const AioDevAttrS *pstDevAttr);

/**
 * @description: Get current AO device attributes.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AioDevAttrS *} pstDevAttr Output parameter to receive the attribute
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_GetDevAttr(AO_DEV AoDev, AioDevAttrS *pstDevAttr);

/**
 * @description: Enable the AO device: open the pcm playback device, start the
 *               internal playback thread and prefill u32FrmNum silence frames
 *               to establish the playback clock. Must be called after
 *               AO_SetDevAttr.
 * @param {AO_DEV} AoDev AO device ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_EnableDev(AO_DEV AoDev);

/**
 * @description: Disable the AO device, stop the playback thread and close the
 *               pcm device. Paired with AO_EnableDev. Attached decoders must
 *               be disabled first.
 * @param {AO_DEV} AoDev AO device ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_DisableDev(AO_DEV AoDev);

/**
 * @description: Set SPK routing (pcm playback card selection and SPK count).
 *               Must be called before AO_EnableDev.
 *               Defaults: s32Dev = 0, s32Chns = 1.
 * @param {AO_DEV} AoDev AO device ID
 * @param {SpkAttrS *} pstSpkAttr Pointer to the SPK routing attribute
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_SetSpkAttr(AO_DEV AoDev, const SpkAttrS *pstSpkAttr);

/**
 * @description: Get current SPK routing attribute.
 * @param {AO_DEV} AoDev AO device ID
 * @param {SpkAttrS *} pstSpkAttr Output parameter to receive the attribute
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_GetSpkAttr(AO_DEV AoDev, SpkAttrS *pstSpkAttr);

/* ======================== Discovery / device diagnostics ======================== */

/* Call after AO_Init. NULL array with capacity 0 queries required count.
 * pu32Count is required and always receives the total available count on
 * success or SYS_ERR_FULL. An undersized array receives a prefix and FULL;
 * enumeration may change between calls. IDs are logical AI/AO device IDs,
 * not the backend indices in MicAttrS/SpkAttrS. Empty capacity probes succeed.
 */
S32 AO_EnumDevices(AioDeviceInfoS *pstDevices, U32 u32Capacity, U32 *pu32Count);

/* Device event queue, distinct from the channel data-ready/writable GetFd.
 * Available after EnableDev. Returns fd >= 0 or a negative error code.
 * Framework owns the fd: do not close/read it; consume with GetEvent.
 * Invalid after DisableDev. Readability means at least one queued event.
 */
S32 AO_GetEventFd(AO_DEV AoDev);

/* Pop one event after EnableDev. Timeout uses AIO_BLOCK_INFINITE /
 * AIO_NO_BLOCK / ms. Device loss leaves the event queue accessible until
 * explicit DisableDev, so DEVICE_LOST can be consumed.
 * Empty nonblocking queue returns SYS_ERR_TIMEOUT. DisableDev wakes waiters
 * with AO_ERR_ABORTED. Queue overflow drops oldest events; sequence
 * gaps within the same device epoch expose lost events.
 */
S32 AO_GetEvent(AO_DEV AoDev, AudioEventS *pstEvent, S32 s32MilliSec);

/* Same count/capacity convention as EnumDevices. Query supported controls;
 * does not change gain, mute or routing. Call after Init.
 */
S32 AO_QueryControlCaps(AO_DEV AoDev, AudioControlCapsS *pstCaps,
    U32 u32Capacity, U32 *pu32Count);

/* ======================== Channel ======================== */

/**
 * @description: Enable one AO channel and allocate its send queue.
 *               Must be called after AO_EnableDev.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_EnableChn(AO_DEV AoDev, AO_CHN AoChn);

/**
 * @description: Disable one AO channel, drop queued data and wake up threads
 *               blocked in AO_SendFrame / AO_SendStream (they return
 *               AO_ERR_ABORTED). Attached decoder must be disabled first,
 *               otherwise SYS_ERR_BUSY is returned.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_DisableChn(AO_DEV AoDev, AO_CHN AoChn);

/**
 * @description: Query send-queue occupancy, high-water mark and channel
 *               processing/underrun counters. Counters reset on a new channel
 *               epoch; legacy queue counts are always valid. u32ValidMask
 *               selects extended fields; unsupported fields are zero.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @param {AoChnStateS *} pstStatus Output parameter
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_QueryChnStat(AO_DEV AoDev, AO_CHN AoChn, AoChnStateS *pstStatus);

/**
 * @description: Get the queue-writable eventfd of the channel for
 *               select/poll/epoll integration. Readable when the send queue
 *               has free space; then call AO_SendFrame with AIO_NO_BLOCK.
 *               The fd is owned by the framework, do not close it.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @return {S32} Nonnegative fd on success, negative error code on failure
 */
S32 AO_GetFd(AO_DEV AoDev, AO_CHN AoChn);

/* Configure/query queue limits and optional channel mapping. Set only while
 * the channel is disabled. Get returns effective values including defaults.
 */
S32 AO_SetChnParam(AO_DEV AoDev, AO_CHN AoChn, const AoChnParamS *pstParam);
S32 AO_GetChnParam(AO_DEV AoDev, AO_CHN AoChn, AoChnParamS *pstParam);

/* ======================== Playback ======================== */

/**
 * @description: Send one PCM frame to the playback queue. The payload is
 *               copied before returning, so pstData can be reused right after
 *               the call (note the asymmetry with the zero-copy AI_GetFrame).
 *               PCM format must match the device; sample rate must match
 *               the device or AO_EnableReSmp input rate. Channel count must
 *               match the device unless a valid channel map is configured.
 *               Frame duration must equal one device frame. Sends during
 *               drain or pending EOS return SYS_ERR_BUSY.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @param {AudioFrameS *} pstData Frame to send
 * @param {S32} s32MilliSec AIO_BLOCK_INFINITE / AIO_NO_BLOCK / timeout in ms
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_SendFrame(AO_DEV AoDev, AO_CHN AoChn,
    const AudioFrameS *pstData, S32 s32MilliSec);

/**
 * @description: Pause the channel: output goes silent while queue data is
 *               kept and AO_SendFrame keeps enqueueing.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_PauseChn(AO_DEV AoDev, AO_CHN AoChn);

/**
 * @description: Resume a paused channel. Paired with AO_PauseChn.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_ResumeChn(AO_DEV AoDev, AO_CHN AoChn);

/**
 * @description: Drop all not-yet-played frames of the channel. The standard
 *               call to remove accumulated latency when switching talkback
 *               direction.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_ClearChnBuf(AO_DEV AoDev, AO_CHN AoChn);

/* Start or wait for drain of this channel, including decoder/converter tail
 * and audio already submitted to hardware. New sends fail with SYS_ERR_BUSY
 * during drain. Success means the channel's last sample has played, not just
 * left the software queue. No-block starts drain and returns TIMEOUT while
 * pending; a timed-out drain remains active. Completion emits DRAIN_DONE.
 * Pause can delay completion. Disable/Clear/Cancel wakes waiters with
 * AO_ERR_ABORTED. Other channels continue playing. After completion, the next
 * send starts a new playback epoch. Unsupported completion tracking returns
 * SYS_ERR_INVAL rather than claiming successful hardware completion.
 */
S32 AO_DrainChn(AO_DEV AoDev, AO_CHN AoChn, S32 s32MilliSec);

/* Cancel the pending drain and reopen input; do not discard queued audio.
 * A finalized decoder may require its normal reset before new coded input.
 * No pending drain is a successful no-op. Does not cancel a separate EOS.
 * Cancel does not increment the playback epoch or rewind position counters.
 */
S32 AO_CancelDrainChn(AO_DEV AoDev, AO_CHN AoChn);

/* Atomic per-call snapshots; valid bits identify supported measurements.
 * Available on enabled channels, including paused/draining channels.
 */
S32 AO_QueryDelay(AO_DEV AoDev, AO_CHN AoChn, AoDelayS *pstDelay);
S32 AO_QueryPlaybackPosition(AO_DEV AoDev, AO_CHN AoChn,
    AoPlaybackPositionS *pstPosition);

/* ======================== Decoder attachment ======================== */

/**
 * @description: Register an external decoder plugin. Built-in codecs
 *               (G711A/U, G726, ADPCM, LPCM) need no registration.
 * @param {S32 *} ps32Handle Output plugin handle
 * @param {AoDecoderS *} pstDecoder Decoder function table
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_RegisterDecoder(S32 *ps32Handle, const AoDecoderS *pstDecoder);

/**
 * @description: Unregister a decoder plugin. Fails with SYS_ERR_BUSY while
 *               any channel still uses the plugin.
 * @param {S32} s32Handle Plugin handle from AO_RegisterDecoder
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_UnRegisterDecoder(S32 s32Handle);

/**
 * @description: Attach a decoder to an enabled channel. Afterwards
 *               AO_SendFrame is rejected (SYS_ERR_INVAL) and coded data is
 *               fed with AO_SendStream; decoding runs inline in the caller
 *               thread and decoded frames enter the send queue. Decoded frame
 *               format must match the device attribute, otherwise normalize
 *               with AO_EnableReSmp.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @param {AoDecChnAttrS *} pstDecAttr Decoder attribute
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_EnableDecChn(AO_DEV AoDev, AO_CHN AoChn, const AoDecChnAttrS *pstDecAttr);

/**
 * @description: Detach the decoder from the channel. Must be called before
 *               AO_DisableChn, otherwise SYS_ERR_BUSY is returned.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_DisableDecChn(AO_DEV AoDev, AO_CHN AoChn);

/**
 * @description: Send coded stream to the attached decoder. In PACK mode one
 *               call must carry whole coded frames; in STREAM mode any length
 *               is accepted. The payload is consumed before returning.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @param {AudioStreamS *} pstStream Stream frame to send
 * @param {BOOL} bBlock MPP_TRUE to block while the buffer is full
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_SendStream(AO_DEV AoDev, AO_CHN AoChn,
    const AudioStreamS *pstStream, BOOL bBlock);

/**
 * @description: Notify end of stream. With bInstant = MPP_TRUE undecoded data
 *               is dropped and the decoder resets immediately; with
 *               MPP_FALSE the decoder resets after the queue drains (useful
 *               for gapless continuation). This notification does not wait
 *               for hardware playback; use AO_DrainChn to wait. Pending EOS
 *               rejects new input; after completion, input starts a new epoch.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @param {BOOL} bInstant MPP_TRUE for immediate reset
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_SendEndOfStream(AO_DEV AoDev, AO_CHN AoChn, BOOL bInstant);

/* Timed counterpart of SendStream using its existing stream structure.
 * pu32ConsumedBytes is required, initialized to zero before processing, and
 * valid on success, timeout or abort. It counts the accepted prefix; retry
 * only the unconsumed suffix. Consumed bytes remain valid on decoder errors
 * and must not be resubmitted. Success means all bytes accepted. PACK accepts
 * the complete packet or none; STREAM may accept a prefix. Accepted bytes
 * are copied/consumed before return. Timeout follows AIO blocking conventions.
 * Decoder must be attached; sends during drain/EOS return SYS_ERR_BUSY.
 */
S32 AO_SendStreamEx(AO_DEV AoDev, AO_CHN AoChn, const AudioStreamS *pstStream,
    S32 s32MilliSec, U32 *pu32ConsumedBytes);

/* Per-channel software controls, applied before mixing and device volume.
 * Valid on enabled channels. Fade 0 applies immediately; otherwise duration
 * is in ms, advances with channel playback and freezes while paused. Setting
 * again starts at the current gain. Query returns target/current/remaining.
 * Default volume is 0 dB, mute is false. QueryControlCaps reports limits.
 */
S32 AO_SetChnVolume(AO_DEV AoDev, AO_CHN AoChn, S32 s32VolumeDb, U32 u32FadeMs);
S32 AO_GetChnVolume(AO_DEV AoDev, AO_CHN AoChn, AudioChnVolumeS *pstVolume);
S32 AO_SetChnMute(AO_DEV AoDev, AO_CHN AoChn, BOOL bMute, U32 u32FadeMs);
S32 AO_GetChnMute(AO_DEV AoDev, AO_CHN AoChn, AudioChnMuteS *pstMute);

/* ======================== Volume / mute / track ======================== */

/**
 * @description: Set digital volume of the playback device, in dB.
 * @param {AO_DEV} AoDev AO device ID
 * @param {S32} s32VolumeDb Volume in dB
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_SetVolume(AO_DEV AoDev, S32 s32VolumeDb);

/**
 * @description: Get current digital volume, in dB.
 * @param {AO_DEV} AoDev AO device ID
 * @param {S32 *} ps32VolumeDb Output volume
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_GetVolume(AO_DEV AoDev, S32 *ps32VolumeDb);

S32 AO_SetAnalogGain(AO_DEV AoDev, const AudioAnalogGainS *pstSetGain);
S32 AO_GetAnalogGain(AO_DEV AoDev, AudioAnalogGainS *pstGain);

/**
 * @description: Mute the playback device with optional fade in/out.
 *               pstFade may be NULL for an immediate switch.
 * @param {AO_DEV} AoDev AO device ID
 * @param {BOOL} bEnable MPP_TRUE to mute
 * @param {AudioFadeS *} pstFade Fade rates, NULL for immediate
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_SetMute(AO_DEV AoDev, BOOL bEnable, const AudioFadeS *pstFade);

/**
 * @description: Get current mute state and fade configuration.
 * @param {AO_DEV} AoDev AO device ID
 * @param {BOOL *} pbEnable Output mute state
 * @param {AudioFadeS *} pstFade Output fade configuration
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_GetMute(AO_DEV AoDev, BOOL *pbEnable, AudioFadeS *pstFade);

/**
 * @description: Set stereo output handling (copy/exchange/mix, single-side
 *               mute and so on). MONO streams are expanded to both channels
 *               according to the mode (BOTH_LEFT / BOTH_RIGHT / MIX are the
 *               common choices for mono prompts). Runtime switchable.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AudioTrackMode} eTrackMode Track mode
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_SetTrackMode(AO_DEV AoDev, AudioTrackMode eTrackMode);

/**
 * @description: Get current track mode.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AudioTrackMode *} peTrackMode Output track mode
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_GetTrackMode(AO_DEV AoDev, AudioTrackMode *peTrackMode);

/* ======================== VQE / resample ======================== */

/**
 * @description: Set VQE attributes of the channel. Must be called before
 *               AO_EnableChn. Only HPF/ANR/AGC/EQ are supported on AO.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @param {AoVqeConfigS *} pstVqeConfig VQE configuration
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_SetVqeAttr(AO_DEV AoDev, AO_CHN AoChn, const AoVqeConfigS *pstVqeConfig);

/**
 * @description: Get VQE attributes of the channel.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @param {AoVqeConfigS *} pstVqeConfig Output VQE configuration
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_GetVqeAttr(AO_DEV AoDev, AO_CHN AoChn, AoVqeConfigS *pstVqeConfig);

/**
 * @description: Enable VQE processing on the channel. Must be called after
 *               AO_SetVqeAttr and AO_EnableChn.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_EnableVqe(AO_DEV AoDev, AO_CHN AoChn);

/**
 * @description: Disable VQE processing.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_DisableVqe(AO_DEV AoDev, AO_CHN AoChn);

/**
 * @description: Declare the sample rate of frames sent to this channel; the
 *               playback thread resamples them to the device sample rate.
 *               Must be called before AO_EnableChn.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @param {AudioSampleRate} eInSampleRate Input sample rate
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_EnableReSmp(AO_DEV AoDev, AO_CHN AoChn, AudioSampleRate eInSampleRate);

/**
 * @description: Disable input resampling.
 * @param {AO_DEV} AoDev AO device ID
 * @param {AO_CHN} AoChn AO channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 AO_DisableReSmp(AO_DEV AoDev, AO_CHN AoChn);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* AO_API_H */
