/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vb_api.h
 * @Date      :    2026-3-16
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Media Interface for MPP.
 *------------------------------------------------------------------------------
 */

#ifndef __VB_API_H__
#define __VB_API_H__

#include "type.h"
#include "vb_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/**
 * @description: Initialize the Video Buffer (VB) module, allocate system resources.
 *               Must be called before any other VB module interfaces, and only once.
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_Init(VOID);

/**
 * @description: Deinitialize the VB module, release all system resources.
 *               Should be called after all buffer pools are destroyed, paired with VB_Init.
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_Exit(VOID);

/**
 * @description: Create a video buffer pool with specified configuration.
 *               The pool manages a set of buffers with the same size for efficient allocation.
 * @param {VbPoolCfg *} pstVbPoolCfg Pointer to pool configuration (buffer size, count, module ID, remap mode)
 * @return {UL} Returns pool ID on success, 0 on failure
 */
UL VB_CreatePool(VbPoolCfg *pstVbPoolCfg);

/**
 * @description: Destroy a video buffer pool and release all associated resources.
 *               All buffers in the pool must be released before destroying, paired with VB_CreatePool.
 * @param {UL} ulPool Pool ID returned by VB_CreatePool
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_DestroyPool(UL ulPool);

/**
 * @description: Get a buffer from the specified pool with timeout.
 *               If no buffer is available, waits up to the specified timeout.
 * @param {UL} ulPool Pool ID from which to get the buffer
 * @param {U32} u32TimeoutMs Timeout in milliseconds (0 = non-blocking, -1 = infinite)
 * @return {UL} Returns buffer ID on success, 0 on failure or timeout
 */
UL VB_GetBuffer(UL ulPool, U32 u32TimeoutMs);

/**
 * @description: Release a buffer back to its pool for reuse.
 *               Decrements reference count; buffer is returned to pool when count reaches 0.
 * @param {UL} ulBuff Buffer ID to release
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_ReleaseBuffer(UL ulBuff);

/**
 * @description: Increment the reference count of a buffer.
 *               Used when multiple modules need to access the same buffer simultaneously.
 * @param {UL} ulBuff Buffer ID whose reference count to increment
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_RefAdd(UL ulBuff);

/**
 * @description: Decrement the reference count of a buffer.
 *               Buffer is returned to pool when reference count reaches 0, paired with VB_RefAdd.
 * @param {UL} ulBuff Buffer ID whose reference count to decrement
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_RefSub(UL ulBuff);

/**
 * @description: Set the PTS (Presentation Time Stamp) for a buffer.
 *               Used for audio/video synchronization, timestamp in microseconds (us).
 * @param {UL} ulBuff Buffer ID to set PTS for
 * @param {U64} u64PTS PTS value in microseconds
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_SetBufferPTS(UL ulBuff, U64 u64PTS);

/**
 * @description: Set frame information (width, height, format, etc.) for a buffer pool.
 *               Configures metadata that will be associated with buffers from this pool.
 * @param {UL} ulPool Pool ID to set frame information for
 * @param {VideoFrameInfo *} pstFrameInfo Pointer to frame information structure
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_SetFrameInfo(UL ulPool, VideoFrameInfo *pstFrameInfo);

/**
 * @description: Get frame information (width, height, format, etc.) from a buffer.
 *               Retrieves metadata associated with the buffer.
 * @param {UL} ulBuff Buffer ID to get frame information from
 * @param {VideoFrameInfo *} pstFrameInfo Output parameter to receive frame information
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_GetFrameInfo(UL ulBuff, VideoFrameInfo *pstFrameInfo);

/**
 * @description: Update per-buffer dynamic frame metadata.
 *               Optional for producers that already have a full VideoFrameInfo.
 *               SYS_SendFrame/VB_GetFrameInfo can still auto-fill common fields
 *               from VB pool/block metadata when callers only pass a buffer.
 * @param {UL} ulBuff Buffer ID to update
 * @param {const VideoFrameInfo *} pstFrameInfo Pointer to frame metadata
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_UpdateBufferFrameInfo(UL ulBuff, const VideoFrameInfo *pstFrameInfo);

/**
 * @description: Export a buffer for cross-process sharing.
 *               Marks the buffer as exported and returns a share token that can be
 *               passed to another process for import. Adds a reference to prevent
 *               premature release.
 *               NOTE: In user-space simulation, the token is the buffer handle itself.
 *               Real cross-process sharing requires kernel dma-buf support.
 * @param {UL} ulBuff Buffer ID to export
 * @param {U64 *} pu64Token Output parameter to receive the share token
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_Export(UL ulBuff, U64 *pu64Token);

/**
 * @description: Import a buffer from a share token obtained via VB_Export.
 *               Adds a reference and returns the buffer handle for local use.
 *               Caller must call VB_ReleaseBuffer when done with the imported buffer.
 *               NOTE: In user-space simulation within the same process only.
 *               Real cross-process import requires kernel dma-buf support.
 * @param {U64} u64Token Share token from VB_Export
 * @param {UL *} pulBuff Output parameter to receive the buffer handle
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_Import(U64 u64Token, UL *pulBuff);

/**
 * @description: Revoke a previously exported buffer.
 *               Decrements the export reference. The buffer can still be used by
 *               importers until they release their own references.
 * @param {UL} ulBuff Buffer ID to unexport
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_Unexport(UL ulBuff);

/**
 * @description: Get the physical address of a VB buffer.
 *               Returns the CMA physical address for DMA/hardware access.
 * @param {UL} ulBuff Buffer handle
 * @param {U64 *} pu64PhyAddr Output parameter to receive physical address
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_GetPhyAddr(UL ulBuff, U64 *pu64PhyAddr);

/**
 * @description: Get the virtual address of a VB buffer in the calling process.
 *               Returns the process-local mmap'd virtual address.
 *               May trigger lazy mapping via pidfd_getfd for cross-process buffers.
 * @param {UL} ulBuff Buffer handle
 * @param {VOID **} ppVirAddr Output parameter to receive virtual address
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_GetVirAddr(UL ulBuff, VOID **ppVirAddr);

/**
 * @description: Get the dma-buf file descriptor of a VB buffer in the calling process.
 *               Returns the process-local dma-buf fd that can be used with V4L2
 *               DMABUF mode or other DMA-capable subsystems for zero-copy I/O.
 *               May trigger lazy mapping via pidfd_getfd for cross-process buffers.
 * @param {UL} ulBuff Buffer handle
 * @param {S32 *} ps32Fd Output parameter to receive the dma-buf fd
 * @return {S32} Returns 0 on success, error code on failure
 */
S32 VB_GetDmaBufFd(UL ulBuff, S32 *ps32Fd);

/**
 * @description: Calculate the buffer size required for a video frame described by pstFrameInfo.
 *               The size is computed based on the frame's width, height, and pixel format,
 *               including any necessary stride alignment and multi-plane padding.
 *               Typical use: determine the buffer size before calling VB_CreatePool.
 * @param {VideoFrameInfo *} pstFrameInfo Pointer to the frame information structure
 *                           (width, height, pixel format, stride, etc.)
 * @return {S32} Required buffer size in bytes; returns 0 on invalid input
 */
S32 VB_GetPicBufferSize(VideoFrameInfo *pstFrameInfo);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /*__VB_API_H__ */
