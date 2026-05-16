/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    dma_alloc.h
* @Date      :    2026-3-26
* @Author    :    rmwei(rongmin.wei@spacemit.com)
* @Brief     :    DMA heap allocator for CMA physical contiguous memory.
*                 Uses /dev/dma_heap/linux,cma on the board.
*------------------------------------------------------------------------------
*/

#ifndef DMA_ALLOC_H
#define DMA_ALLOC_H

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Sync direction flags (matches DMA_BUF_SYNC_* in linux/dma-buf.h) */
#define DMA_SYNC_READ   (1 << 0)
#define DMA_SYNC_WRITE  (2 << 0)
#define DMA_SYNC_RW     (DMA_SYNC_READ | DMA_SYNC_WRITE)
#define DMA_SYNC_START  (0 << 2)
#define DMA_SYNC_END    (1 << 2)

/**
* @brief Open the DMA heap device. Call once at startup.
* @return 0 on success, negative on failure
*/
S32 dma_alloc_init(void);

/**
* @brief Close the DMA heap device.
*/
void dma_alloc_deinit(void);

/**
* @brief Allocate a CMA buffer via DMA heap.
* @param size  Buffer size in bytes
* @param p_fd  Output: dma-buf file descriptor
* @param p_phy Output: physical address (via /proc/self/pagemap)
* @param p_vir Output: mmap'd virtual address
* @return 0 on success, negative on failure
*/
S32 dma_alloc_buf(U32 size, int *p_fd, U64 *p_phy, void **p_vir);

/**
* @brief Free a CMA buffer.
* @param fd   dma-buf file descriptor
* @param vir  mmap'd virtual address
* @param size Buffer size
*/
void dma_free_buf(int fd, void *vir, U32 size);

/**
* @brief Sync (flush/invalidate) a dma-buf for CPU or device access.
* @param fd    dma-buf file descriptor
* @param flags Combination of DMA_SYNC_* flags
* @return 0 on success, negative on failure
*/
S32 dma_sync_buf(int fd, U32 flags);

/**
* @brief Get physical address of a virtual address via /proc/self/pagemap.
* @param vir  Virtual address
* @param p_phy Output: physical address
* @return 0 on success, negative on failure
*/
S32 dma_get_phy(void *vir, U64 *p_phy);

#ifdef __cplusplus
}
#endif

#endif /* DMA_ALLOC_H */
