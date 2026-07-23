/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vdec_api.h
 * @Date      :    2026-04-18
 * @Brief     :    VDEC module public API for MPP.
 *                 Decode compressed stream (H.264/H.265/MJPEG) to video frames.
 *                 Input uses StreamBufferInfo, output uses VB-backed VideoFrameInfo.
 *                 Zero-copy: output dma-buf fds are imported into VB pool directly.
 *------------------------------------------------------------------------------
 */

#ifndef VDEC_API_H
#define VDEC_API_H

#include "sys/type.h"
#include "sys/sys_type.h"
#include "sys/vb_type.h"
#include "vdec_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/**
 * @brief  Initialize VDEC module global resources.
 *         Must be called once before any other VDEC API.
 * @return 0 on success, error code on failure
 */
S32 VDEC_Init(VOID);

/**
 * @brief  Deinitialize VDEC module, release global resources.
 *         All channels must be destroyed first.
 * @return 0 on success, error code on failure
 */
S32 VDEC_Exit(VOID);

/**
 * @brief  Create a VDEC channel.
 * @param  s32ChnId  Channel ID [0, VDEC_MAX_CHN)
 * @param  pstAttr   Channel attributes (codec type, resolution, etc.)
 * @return 0 on success, error code on failure
 */
S32 VDEC_CreateChn(S32 s32ChnId, const VdecChnAttr *pstAttr);

/**
 * @brief  Destroy a VDEC channel and release all resources.
 *         Channel must be stopped first.
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 VDEC_DestroyChn(S32 s32ChnId);

/**
 * @brief  Enable decoding on the channel.
 *         AL decoder is initialized and ready to accept streams.
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 VDEC_EnableChn(S32 s32ChnId);

/**
 * @brief  Disable decoding on the channel.
 *         Flushes remaining frames and stops the decoder.
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 VDEC_DisableChn(S32 s32ChnId);

/**
 * @brief  Send a compressed stream packet to the decoder (zero-copy).
 *         The caller's buffer pointer in pstStream is passed directly to the
 *         AL layer without memcpy at the MPI level.
 *         Caller must keep the buffer valid until this call returns.
 * @param  s32ChnId   Channel ID
 * @param  pstStream  Stream packet (pu8Addr, u32Size, u64PTS, bKeyFrame, etc.)
 * @param  u32TimeoutMs  0 = non-blocking, finite value = wait up to that many
 *                       milliseconds, (U32)-1 = wait until accepted or stopped
 * @return 0 on success, ERR_VDEC_BUSY for non-blocking backpressure,
 *         ERR_VDEC_TIMEOUT when a finite wait expires, or another error code
 */
S32 VDEC_SendStream(S32 s32ChnId, const StreamBufferInfo *pstStream, U32 u32TimeoutMs);

/**
 * @brief  Receive a decoded video frame (zero-copy).
 *         The returned VB buffer handle wraps the decoder's dma-buf fd directly.
 *         Caller MUST call VDEC_ReleaseFrame (or VB_ReleaseBuffer) after use.
 * @param  s32ChnId       Channel ID
 * @param  pstFrameInfo   Output: frame metadata; pstFrameInfo->ulBufferId is
 *                        the VB buffer handle to pass to VDEC_ReleaseFrame.
 * @param  u32TimeoutMs   Timeout in ms (0 = non-blocking, -1 = infinite)
 * @return 0 on success, ERR_VDEC_NO_FRAME if no frame available,
 *         ERR_VDEC_EOS on end-of-stream, error code on failure
 */
S32 VDEC_GetFrame(S32 s32ChnId, VideoFrameInfo *pstFrameInfo, U32 u32TimeoutMs);

/**
 * @brief  Release a decoded frame back to the decoder.
 *         Decrements VB reference count and returns the buffer to the decoder
 *         for reuse. Must be paired with each successful VDEC_GetFrame.
 * @param  s32ChnId   Channel ID
 * @param  ulVbBuff   pstFrameInfo->ulBufferId from VDEC_GetFrame
 * @return 0 on success, error code on failure
 */
S32 VDEC_ReleaseFrame(S32 s32ChnId, UL ulVbBuff);

/**
 * @brief  Get the dma-buf fd for an enabled channel's output buffer.
 * @note   The fd remains owned by MPP. Callers that retain it must dup it.
 */
S32 VDEC_GetOutputBufferFd(S32 s32ChnId, U32 u32Idx, S32 *ps32Fd);

/** @brief Get the number of output buffers allocated for an enabled channel. */
S32 VDEC_GetOutputBufferCount(S32 s32ChnId, U32 *pu32Cnt);

/**
 * @brief  Query channel status.
 * @param  s32ChnId   Channel ID
 * @param  pstStatus  Output: channel status
 * @return 0 on success, error code on failure
 */
S32 VDEC_QueryStatus(S32 s32ChnId, VdecChnStatus *pstStatus);

/**
 * @brief  Flush the decoder: drain all pending frames.
 *         After flush, the decoder is ready for new stream input.
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 VDEC_Flush(S32 s32ChnId);

/**
 * @brief  Reset the decoder: discard all pending data and reset state.
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 VDEC_Reset(S32 s32ChnId);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /*VDEC_API_H */
