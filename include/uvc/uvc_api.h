/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    uvc_api.h
 * @Date      :    2026-3-16
 * @Author    :    SPACEMIT
 * @Brief     :    Media Interface for MPP.
 *------------------------------------------------------------------------------
 */

#ifndef UVC_API_H
#define UVC_API_H

#include "type.h"
#include "uvc_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== Module Init / Exit ======================== */

/**
 * @brief  Initialize the UVC module and allocate global resources.
 *         Must be called before any other UVC API.
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_Init(VOID);

/**
 * @brief  De-initialize the UVC module and release all global resources.
 *         No other UVC API shall be called after this.
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_Exit(VOID);

/* ======================== Device Operations ======================== */

/**
 * @brief  Create a UVC device with the given attributes (device node, buffer count, etc.).
 * @param  dev        [in] Device index, range [0, UVC_MAX_DEV_NUM).
 * @param  pstDevAttr [in] Pointer to device attributes. @see UvcDevAttr
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_CreateDev(UVC_DEV dev, const UvcDevAttr *pstDevAttr);

/**
 * @brief  Destroy a UVC device and release its resources.
 *         All channels must be disabled before calling this.
 * @param  dev [in] Device index.
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_DestroyDev(UVC_DEV dev);

/**
 * @brief  Enable a UVC device and start the underlying V4L2 stream.
 *         The device must be created and at least one channel configured before enabling.
 * @param  dev [in] Device index.
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_EnableDev(UVC_DEV dev);

/**
 * @brief  Disable a UVC device and stop the underlying V4L2 stream.
 * @param  dev [in] Device index.
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_DisableDev(UVC_DEV dev);

/* ======================== Channel Operations ======================== */

/**
 * @brief  Set the attributes of a UVC channel (resolution, pixel format, frame rate, queue depth, etc.).
 * @param  dev        [in] Device index.
 * @param  chn        [in] Channel index, range [0, UVC_MAX_CHN_NUM).
 * @param  pstChnAttr [in] Pointer to channel attributes. @see UvcChnAttr
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_SetChnAttr(UVC_DEV dev, UVC_CHN chn, const UvcChnAttr *pstChnAttr);

/**
 * @brief  Query the current attributes of a UVC channel.
 * @param  dev        [in]  Device index.
 * @param  chn        [in]  Channel index.
 * @param  pstChnAttr [out] Pointer to receive the channel attributes.
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_GetChnAttr(UVC_DEV dev, UVC_CHN chn, UvcChnAttr *pstChnAttr);

/**
 * @brief  Enable a UVC channel. Once enabled, frames can be obtained via UVC_GetFrame().
 * @param  dev [in] Device index.
 * @param  chn [in] Channel index.
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_EnableChn(UVC_DEV dev, UVC_CHN chn);

/**
 * @brief  Disable a UVC channel.
 * @param  dev [in] Device index.
 * @param  chn [in] Channel index.
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_DisableChn(UVC_DEV dev, UVC_CHN chn);

/* ======================== Effect (Image Control) ======================== */

/**
 * @brief  Set image effect attributes for a UVC device (brightness, contrast, saturation, hue, etc.).
 * @param  dev       [in] Device index.
 * @param  pstEffect [in] Pointer to effect attributes. @see UvcEffectAttr
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_SetEffectAttr(UVC_DEV dev, const UvcEffectAttr *pstEffect);

/**
 * @brief  Query the current image effect attributes of a UVC device.
 * @param  dev       [in]  Device index.
 * @param  pstEffect [out] Pointer to receive the effect attributes.
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_GetEffectAttr(UVC_DEV dev, UvcEffectAttr *pstEffect);

/* ======================== Frame Operations ======================== */

/**
 * @brief  Get one video frame from the specified channel. Supports blocking and non-blocking modes.
 * @param  dev          [in]  Device index.
 * @param  chn          [in]  Channel index.
 * @param  pstFrameInfo [out] Pointer to receive the frame info. @see VideoFrameInfo
 * @param  s32MilliSec  [in]  Timeout in milliseconds:
 *                             -1 = block until a frame is available,
 *                              0 = non-blocking (return immediately),
 *                             >0 = wait up to the specified time.
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_GetFrame(UVC_DEV dev, UVC_CHN chn, VideoFrameInfo *pstFrameInfo, S32 s32MilliSec);

/**
 * @brief  Release a video frame previously obtained by UVC_GetFrame().
 *         Frames must be released promptly to avoid buffer exhaustion.
 * @param  dev          [in] Device index.
 * @param  chn          [in] Channel index.
 * @param  pstFrameInfo [in] Pointer to the frame info to release.
 * @return 0 on success, non-zero error code on failure.
 */
S32 UVC_ReleaseFrame(UVC_DEV dev, UVC_CHN chn, const VideoFrameInfo *pstFrameInfo);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /*UVC_API_H */
