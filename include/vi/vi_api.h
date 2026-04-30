/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_api.h
 * @Date      :    2026-3-24
 * @Author    :    SPACEMIT
 * @Brief     :    VI public API for MPP.
 *------------------------------------------------------------------------------
 */

#ifndef __VI_API_H__
#define __VI_API_H__

#include "type.h"
#include "vb_type.h"
#include "vi_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */


/**
 * @description: Initialize VI module, allocate VI-related resources, and prepare device/channel context.
 *               Must be called before any other VI interface, and should only be called once.
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_Init(VOID);

/**
 * @description: Deinitialize VI module, release all VI-related resources, paired with VI_Init.
 *               Should be called after all VI devices and channels are disabled.
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_DeInit(VOID);

/**
 * @description: Set VI device attributes, including input resolution, lane count, raw type, and bit depth.
 *               Must be called before VI_EnableDev.
 * @param {VI_DEV} ViDev VI device ID
 * @param {ViDevAttrS *} pstDevAttr Pointer to VI device attribute structure
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_SetDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr);

/**
 * @description: Get current VI device attributes.
 * @param {VI_DEV} ViDev VI device ID
 * @param {ViDevAttrS *} pstDevAttr Output parameter to receive VI device attributes
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_GetDevAttr(VI_DEV ViDev, ViDevAttrS *pstDevAttr);

/**
 * @description: Enable VI device, allowing the input device to start serving channel acquisition flow.
 *               Must be called after VI_SetDevAttr.
 * @param {VI_DEV} ViDev VI device ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_EnableDev(VI_DEV ViDev);

/**
 * @description: Disable VI device and stop related input activity, paired with VI_EnableDev.
 * @param {VI_DEV} ViDev VI device ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_DisableDev(VI_DEV ViDev);

/**
 * @description: Set VI channel attributes, including output resolution, buffer count, line stride, and pixel format.
 *               Must be called before VI_EnableChn.
 * @param {VI_DEV} ViDev VI device ID to which the channel belongs
 * @param {VI_CHN} ViChn VI channel ID
 * @param {ViChnAttrS *} pstChnAttr Pointer to VI channel attribute structure
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_SetChnAttr(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr);

/**
 * @description: Get current VI channel attributes.
 * @param {VI_DEV} ViDev VI device ID to which the channel belongs
 * @param {VI_CHN} ViChn VI channel ID
 * @param {ViChnAttrS *} pstChnAttr Output parameter to receive VI channel attributes
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_GetChnAttr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstChnAttr);

/**
 * @description: Set VI channel frame-rate control ratio. Both physical and virtual channels are supported.
 *               The channel keeps `u32OutputFrameStep` frames for every `u32InputFrameStep` input frames.
 *               For example, input=2 output=1 means keep one frame and drop one frame every two frames.
 * @param {VI_DEV} ViDev VI device ID to which the channel belongs
 * @param {VI_CHN} ViChn VI channel ID
 * @param {ViFrameRateCtrlS *} pstFrameRateCtrl Pointer to frame-rate control configuration
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_SetChnFrameRate(VI_DEV ViDev, VI_CHN ViChn, const ViFrameRateCtrlS *pstFrameRateCtrl);

/**
 * @description: Get current VI channel frame-rate control ratio.
 * @param {VI_DEV} ViDev VI device ID to which the channel belongs
 * @param {VI_CHN} ViChn VI channel ID
 * @param {ViFrameRateCtrlS *} pstFrameRateCtrl Output parameter to receive frame-rate control configuration
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_GetChnFrameRate(VI_DEV ViDev, VI_CHN ViChn, ViFrameRateCtrlS *pstFrameRateCtrl);

/**
 * @description: Enable VI channel so that frames can be obtained from this channel.
 *               Must be called after VI_SetChnAttr.
 * @param {VI_DEV} ViDev VI device ID to which the channel belongs
 * @param {VI_CHN} ViChn VI channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_EnableChn(VI_DEV ViDev, VI_CHN ViChn);

/**
 * @description: Disable VI channel and stop frame output from this channel, paired with VI_EnableChn.
 * @param {VI_DEV} ViDev VI device ID to which the channel belongs
 * @param {VI_CHN} ViChn VI channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_DisableChn(VI_DEV ViDev, VI_CHN ViChn);

/**
 * @description: Get one frame from the specified VI channel.
 *               This is the standard frame acquisition interface for applications using VI channel output.
 * @param {VI_DEV} ViDev VI device ID to which the channel belongs
 * @param {VI_CHN} ViChn VI channel ID
 * @param {VideoFrameInfo *} pstVideoFrame Output parameter to receive frame information
 * @param {S32} s32MilliSec Timeout in milliseconds, 0 for non-blocking, negative for blocking behavior defined by implementation
 * @return {S32} Returns 0 on success, error code on failure or timeout
 */
S32 VI_GetChnFrame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec);

/**
 * @description: Release one frame previously acquired from the specified VI channel, paired with VI_GetChnFrame.
 * @param {VI_DEV} ViDev VI device ID to which the channel belongs
 * @param {VI_CHN} ViChn VI channel ID
 * @param {VideoFrameInfo *} pstVideoFrame Pointer to frame information returned by VI_GetChnFrame
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_ReleaseChnFrame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame);

/**
 * @description: Trigger one-shot rawdump on an enabled online VI physical channel.
 *               User does not need to create an extra rawdump channel.
 *               After triggering, call VI_GetRawDumpFrame to obtain the captured raw frame.
 * @param {VI_DEV} ViDev VI device ID to which the channel belongs
 * @param {VI_CHN} ViChn Enabled online physical VI channel ID
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_TriggerRawDump(VI_DEV ViDev, VI_CHN ViChn);

/**
 * @description: Get one rawdump frame produced by VI_TriggerRawDump on the specified VI physical channel.
 *               The returned frame remains valid until the next VI_TriggerRawDump on the same channel.
 * @param {VI_DEV} ViDev VI device ID to which the channel belongs
 * @param {VI_CHN} ViChn Online physical VI channel ID
 * @param {VideoFrameInfo *} pstVideoFrame Output parameter to receive frame information
 * @param {S32} s32MilliSec Timeout in milliseconds, 0 for non-blocking, negative for blocking behavior defined by implementation
 * @return {S32} Returns 0 on success, error code on failure or timeout
 */
S32 VI_GetRawDumpFrame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec);

/**
 * @description: Release one rawdump frame previously acquired from VI_GetRawDumpFrame.
 * @param {VI_DEV} ViDev VI device ID to which the channel belongs
 * @param {VI_CHN} ViChn Online physical VI channel ID
 * @param {const VideoFrameInfo *} pstVideoFrame Pointer to rawdump frame information returned by VI_GetRawDumpFrame
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_ReleaseRawDumpFrame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame);

/**
 * @description: Configure offline VI input source with a user-provided raw DDR virtual address.
 *               This API is used only when ViDev is configured in VI_WORK_MODE_OFFLINE.
 *               The implementation will apply default offline ISP configuration and start processing internally.
 * @param {VI_DEV} ViDev VI device ID
 * @param {VI_CHN} ViChn Physical preview/output channel ID associated with the offline ISP path
 * @param {U8 *} pu8RawVirAddr User-provided raw buffer virtual address
 * @param {U32} u32RawSize Raw buffer size in bytes
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VI_OfflineSetInputAddr(VI_DEV ViDev,
                           VI_CHN ViChn,
                           const U8 *pu8RawVirAddr,
                           U32 u32RawSize);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __VI_API_H__ */
