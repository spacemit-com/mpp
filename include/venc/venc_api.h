/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
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
 *         Each successful call acquires one reference and requires a matching VENC_Exit().
 * @return 0 on success, error code on failure
 */
S32 VENC_Init(VOID);

/**
 * @brief  Release one VENC module reference.
 *         The final exit returns ERR_VENC_BUSY and retains the reference while channels exist.
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
 * @brief  Queue a video frame to the encoder without copying its pixels.
 *         DMA-BUF modes use stVFrame.u32Fd[0]. USERPTR_EXTERNAL uses
 *         stVFrame.ulPlaneVirAddr[] and does not require DMA-BUF fds.
 *         USERPTR frames must describe every plane with u32PlaneNum,
 *         u32PlaneSize[] and virtual addresses matching the configured format.
 *         Each plane must cover the driver-required size, including padding;
 *         nonzero u32PlaneStride[] must match the negotiated stride (zero means
 *         the caller already uses that stride).
 *         Submission is asynchronous: return does not mean input completion.
 *         With ulBufferId != 0, VENC retains a VB reference until input completes;
 *         the caller may release its reference but must not modify the pixels.
 *         Without a VB handle, keep all input memory valid and unchanged until
 *         the channel has been successfully disabled and destroyed.
 * @param  s32ChnId   Channel ID
 * @param  pstFrame   Input frame info (format, per-plane layout and backing memory)
 * @param  u32TimeoutMs Reserved; submission currently returns immediately if full.
 * @return 0 on success, error code on failure
 */
S32 VENC_SendFrame(S32 s32ChnId, const VideoFrameInfo *pstFrame, U32 u32TimeoutMs);

/**
 * @brief  Receive an encoded stream packet (zero-copy).
 *         Returns a read-only VB mapping with CPU read access already started.
 *         Caller MUST call VENC_ReleaseStream after use; do not free the pointer
 *         or release ulVbHandle separately. Release before destroying the channel.
 * @param  s32ChnId       Channel ID
 * @param  pstStream      Output: stream packet (address, size, PTS, key frame flag)
 * @param  u32TimeoutMs   Timeout in ms (0 = non-blocking, -1 = infinite)
 * @return 0 on success, ERR_VENC_NO_STREAM if no stream available,
 *         ERR_VENC_EOS on end-of-stream, error code on failure
 */
S32 VENC_GetStream(S32 s32ChnId, StreamBufferInfo *pstStream, U32 u32TimeoutMs);

/**
 * @brief  End CPU read access and release the encoded stream's VB reference.
 *         Must be paired with each successful VENC_GetStream.
 * @param  s32ChnId   Channel ID
 * @param  pstStream  Stream packet from VENC_GetStream
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
