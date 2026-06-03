/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    venc_api.h
 * @Date      :    2026-04-19
 * @Brief     :    VENC module public API for MPP.
 *                 Encode video frames (YUV420) to compressed stream (H.264/H.265/MJPEG).
 *                 Input uses VideoFrameInfo, output uses StreamBufferInfo.
 *------------------------------------------------------------------------------
 */

#ifndef VENC_API_H
#define VENC_API_H

#include "sys/type.h"
#include "sys/sys_type.h"
#include "sys/vb_type.h"
#include "venc_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/**
 * @brief  Initialize VENC module global resources.
 *         Must be called once before any other VENC API.
 * @return 0 on success, error code on failure
 */
S32 VENC_Init(VOID);

/**
 * @brief  Deinitialize VENC module, release global resources.
 *         All channels must be destroyed first.
 * @return 0 on success, error code on failure
 */
S32 VENC_Exit(VOID);

/**
 * @brief  Create a VENC channel.
 * @param  s32ChnId  Channel ID [0, VENC_MAX_CHN)
 * @param  pstAttr   Channel attributes (codec type, resolution, bitrate, etc.)
 * @return 0 on success, error code on failure
 */
S32 VENC_CreateChn(S32 s32ChnId, const VencChnAttr *pstAttr);

/**
 * @brief  Destroy a VENC channel and release all resources.
 *         Channel must be stopped first.
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 VENC_DestroyChn(S32 s32ChnId);

/**
 * @brief  Enable encoding on the channel.
 *         AL encoder is initialized and ready to accept frames.
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 VENC_EnableChn(S32 s32ChnId);

/**
 * @brief  Disable encoding on the channel.
 *         Flushes remaining streams and stops the encoder.
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 VENC_DisableChn(S32 s32ChnId);

/**
 * @brief  Send a video frame to the encoder (zero-copy).
 *         The caller's dma-buf fd in pstFrame is passed directly to the
 *         AL layer without memcpy at the MPI level.
 *         Caller must keep the buffer valid until this call returns.
 * @param  s32ChnId   Channel ID
 * @param  pstFrame   Input frame info (width, height, pixel format, dma-buf fd, etc.)
 * @return 0 on success, error code on failure
 */
S32 VENC_SendFrame(S32 s32ChnId, const VideoFrameInfo *pstFrame, U32 u32TimeoutMs);

/**
 * @brief  Receive an encoded stream packet (zero-copy).
 *         Caller MUST call VENC_ReleaseStream after use.
 * @param  s32ChnId       Channel ID
 * @param  pstStream      Output: stream packet (address, size, PTS, key frame flag)
 * @param  u32TimeoutMs   Timeout in ms (0 = non-blocking, -1 = infinite)
 * @return 0 on success, ERR_VENC_NO_STREAM if no stream available,
 *         ERR_VENC_EOS on end-of-stream, error code on failure
 */
S32 VENC_GetStream(S32 s32ChnId, StreamBufferInfo *pstStream, U32 u32TimeoutMs);

/**
 * @brief  Release an encoded stream packet back to the encoder.
 *         Must be paired with each successful VENC_RecvStream.
 * @param  s32ChnId   Channel ID
 * @param  pstStream  Stream packet from VENC_RecvStream
 * @return 0 on success, error code on failure
 */
S32 VENC_ReleaseStream(S32 s32ChnId, const StreamBufferInfo *pstStream);

/**
 * @brief  Query channel status.
 * @param  s32ChnId   Channel ID
 * @param  pstStatus  Output: channel status
 * @return 0 on success, error code on failure
 */
S32 VENC_QueryStatus(S32 s32ChnId, VencChnStatus *pstStatus);

/**
 * @brief  Flush the encoder: drain all pending encoded streams.
 *         After flush, the encoder is ready for new frame input.
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 VENC_Flush(S32 s32ChnId);

/**
 * @brief  Reset the encoder: discard all pending data and reset state.
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 VENC_Reset(S32 s32ChnId);

/**
 * @brief  Set encoder frame rate dynamically.
 * @param  s32ChnId    Channel ID
 * @param  s32FrameRate  Target frame rate (fps)
 * @return 0 on success, error code on failure
 */
S32 VENC_SetFrameRate(S32 s32ChnId, S32 s32FrameRate);

/**
 * @brief  Set encoder rate control attributes dynamically.
 * @param  s32ChnId   Channel ID
 * @param  pstRcAttr  Pointer to rate control attributes
 * @return 0 on success, error code on failure
 */
S32 VENC_SetRateControl(S32 s32ChnId, VencRcAttr *pstRcAttr);

/**
 * @brief  Set encoder crop attributes dynamically.
 * @param  s32ChnId     Channel ID
 * @param  pstCropAttr  Pointer to crop attributes
 * @return 0 on success, error code on failure
 */
S32 VENC_SetCropAttr(S32 s32ChnId, VencCropAttr *pstCropAttr);

/**
 * @brief  Force an IDR frame on the encoder.
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 VENC_SetForceIDR(S32 s32ChnId);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /*VENC_API_H */
