/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    sys_api.h
* @Date      :    2026-3-16
* @Author    :    rmwei(rongmin.wei@spacemit.com)
* @Brief     :    Media Interface for MPP.
*------------------------------------------------------------------------------
*/

#ifndef SYS_API_H
#define SYS_API_H

#include "type.h"
#include "sys_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/**
* @description: Initialize MPP system module, allocate system resources, and start internal threads and drivers.
*               Must be called before any other MPP module interfaces, and should only be called once.
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_Init(VOID);

/**
* @description: Deinitialize MPP system module, release all system resources, and stop internal threads.
*               Should be called after all modules have stopped, paired with SYS_Init.
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_Exit(VOID);

/**
* @description: Bind source node to destination node, establishing data path between modules.
*               After binding, data produced by source node will be automatically passed to destination node.
*               Example: Bind VDEC channel to VO channel for direct decode-to-display.
* @param {MppNode *} pstSrcNode  Source node pointer (contains module ID, device ID, channel ID)
* @param {MppNode *} pstSinkNode Destination node pointer (contains module ID, device ID, channel ID)
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_Bind(const MppNode *pstSrcNode, const MppNode *pstSinkNode);

/**
* @description: Unbind source node from destination node, disconnecting data path between modules.
*               After unbinding, data will no longer be automatically passed, paired with SYS_Bind.
* @param {MppNode *} pstSrcNode  Source node pointer (contains module ID, device ID, channel ID)
* @param {MppNode *} pstSinkNode Destination node pointer (contains module ID, device ID, channel ID)
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_UnBind(const MppNode *pstSrcNode, const MppNode *pstSinkNode);

/**
* @description: Get current system PTS (Presentation Time Stamp).
*               Used for audio-video synchronization, unit is microseconds (us).
* @param {U64 *} pu64CurPTS Output parameter to receive current PTS value
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_GetCurPTS(U64 *pu64CurPTS);

/**
* @description: Initialize PTS base value, setting the starting reference point for system timestamp.
*               Typically called at system startup or reset, unit is microseconds (us).
* @param {U64} u64PTSBase PTS base value
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_InitPTSBase(U64 u64PTSBase);

/**
* @description: Synchronize system PTS timestamp to specified base value.
*               Used for multi-stream or external clock synchronization scenarios, unit is microseconds (us).
* @param {U64} u64PTSBase Target PTS base value to synchronize to
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_SyncPTS(U64 u64PTSBase);

/**
* @description: Map physical address to user-space virtual address (non-cached mode).
*               Suitable for real-time scenarios that don't need cache (e.g., DMA direct access).
* @param {U64} u64PhyAddr Physical address to map
* @param {U32} u32Size    Size of memory to map (bytes)
* @return {S32} Returns mapped virtual address on success, error code on failure
*/
S32 SYS_Mmap(U64 u64PhyAddr, U32 u32Size);

/**
* @description: Map physical address to user-space virtual address (cached mode).
*               Suitable for CPU frequent read/write scenarios that need cache acceleration.
*               Must call SYS_MflushCache to sync cache to memory after writing.
* @param {U64} u64PhyAddr Physical address to map
* @param {U32} u32Size    Size of memory to map (bytes)
* @return {S32} Returns mapped virtual address on success, error code on failure
*/
S32 SYS_MmapCache(U64 u64PhyAddr, U32 u32Size);

/**
* @description: Unmap user-space virtual address, paired with SYS_Mmap/SYS_MmapCache.
* @param {VOID *} pVirAddr Virtual address to unmap
* @param {U32}    u32Size  Size of mapped memory (bytes)
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_Munmap(VOID *pVirAddr, U32 u32Size);

/**
* @description: Flush cache for specified memory region, syncing cache data back to physical memory.
*               Must be called after CPU writes to cached memory and before hardware (e.g., DMA) reads.
* @param {U64}    u64PhyAddr Physical address
* @param {VOID *} pVirAddr   Corresponding virtual address
* @param {U32}    u32Size    Size of memory to flush (bytes)
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_MflushCache(U64 u64PhyAddr, VOID *pVirAddr, U32 u32Size);

/**
* @description: Allocate non-cached contiguous physical memory in MMZ (Multi-Media Zone).
*               Returns both physical address and mapped virtual address, suitable for hardware direct access.
* @param {U64 *}   pu64PhyAddr Output parameter to receive allocated physical address
* @param {VOID **} ppVirAddr   Output parameter to receive mapped virtual address
* @param {CHAR *}  sMb         Memory block name (for debugging, can be NULL)
* @param {CHAR *}  sZone       MMZ zone name (specify which zone to allocate from, NULL for default)
* @param {U32}     u32Len      Size of memory to allocate (bytes)
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_MmzAlloc(U64 *pu64PhyAddr, VOID **ppVirAddr, const CHAR *sMb, const CHAR *sZone, U32 u32Len);

/**
* @description: Allocate cached contiguous physical memory in MMZ.
*               Suitable for CPU frequent read/write scenarios, must call SYS_MmzFlushCache to sync after writing.
* @param {U64 *}   pu64PhyAddr Output parameter to receive allocated physical address
* @param {VOID **} ppVirAddr   Output parameter to receive mapped virtual address
* @param {CHAR *}  sMb         Memory block name (for debugging, can be NULL)
* @param {CHAR *}  sZone       MMZ zone name (specify which zone to allocate from, NULL for default)
* @param {U32}     u32Len      Size of memory to allocate (bytes)
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_MmzAlloc_Cached(U64 *pu64PhyAddr, VOID **ppVirAddr, const CHAR *sMb, const CHAR *sZone, U32 u32Len);

/**
* @description: Flush cache for MMZ cached memory, syncing data back to physical memory.
*               Must be called after CPU writes to cached MMZ memory and before hardware reads.
* @param {U64}    u64PhyAddr Physical address
* @param {VOID *} pVirAddr   Corresponding virtual address
* @param {U32}    u32Size    Size of memory to flush (bytes)
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_MmzFlushCache(U64 u64PhyAddr, VOID *pVirAddr, U32 u32Size);

/**
* @description: Free MMZ memory allocated by SYS_MmzAlloc or SYS_MmzAlloc_Cached.
*               Unmaps virtual address and releases physical memory, paired with SYS_MmzAlloc series.
* @param {U64}    u64PhyAddr Physical address to free
* @param {VOID *} pVirAddr   Corresponding virtual address
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_MmzFree(U64 u64PhyAddr, VOID *pVirAddr);

/**
* @description: Send a video frame buffer to all sink nodes bound to the source node.
*               Used for zero-copy frame flow in module binding (e.g., VI->VPSS->VENC).
*               Automatically snapshots VB frame metadata (PTS, size, stride, valid size,
*               plane addresses/fd and module info) and increments buffer reference count
*               for each sink. If producer did not stamp PTS, SYS stamps current PTS.
* @param {MppNode *} pstSrc  Source node pointer
* @param {UL}        ulBuff  VB buffer handle to send
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_SendFrame(const MppNode *pstSrc, UL ulBuff);

/**
* @description: Receive a video frame buffer from the channel queue bound to the sink node.
*               Blocks until a frame is available or timeout expires.
*               The received frame metadata is restored to the VB buffer automatically;
*               caller can query complete metadata with VB_GetFrameInfo(pulBuff, ...).
*               fd/virtual addresses are always refilled for the current process.
*               Caller must call VB_ReleaseBuffer after processing the frame.
* @param {MppNode *} pstSink       Sink node pointer
* @param {UL *}      pulBuff       Output: received VB buffer handle
* @param {U32}       u32TimeoutMs  Timeout in milliseconds (0=non-blocking, -1=infinite)
* @return {S32} Returns 0 on success, error code on failure/timeout
*/
S32 SYS_RecvFrame(const MppNode *pstSink, UL *pulBuff, U32 u32TimeoutMs);

/**
* @description: Send a compressed stream packet to all sink nodes bound to the source node.
*               Used for compressed-domain binding such as DEMUX->VDEC or VENC->MUX.
* @param {MppNode *} pstSrc    Source node pointer
* @param {StreamBufferInfo *} pstStream Stream metadata and payload pointer
* @return {S32} Returns 0 on success, error code on failure
*/
S32 SYS_SendStream(const MppNode *pstSrc, const StreamBufferInfo *pstStream);

/**
* @description: Receive a compressed stream packet from the channel queue bound to the sink node.
*               Caller must provide a writable payload buffer via pu8Addr/u32Size.
* @param {MppNode *} pstSink       Sink node pointer
* @param {StreamBufferInfo *} pstStream In/out stream metadata and payload buffer
* @param {U32}       u32TimeoutMs  Timeout in milliseconds (0=non-blocking)
* @return {S32} Returns 0 on success, error code on failure/timeout
*/
S32 SYS_RecvStream(const MppNode *pstSink, StreamBufferInfo *pstStream, U32 u32TimeoutMs);

/**
* @description: Dump MPP system status to stdout for debugging.
*               Shows PTS, bind table, memory mappings, and process info.
*/
VOID SYS_DumpStatus(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /*__SYS_API_H__ */
