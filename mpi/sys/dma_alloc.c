/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    dma_alloc.c
 * @Date      :    2026-3-26
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    DMA heap CMA allocator implementation.
 *                 Uses /dev/dma_heap/linux,cma for real physical contiguous
 *                 memory. Physical address assigned from CMA base (debugfs).
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/types.h>

#include "sys/dma_alloc.h"

/* ======================== Linux DMA Heap UAPI ======================== */
/* From linux/dma-heap.h — define here to avoid kernel header dependency */

struct dma_heap_allocation_data {
    __u64 len;
    __u32 fd;
    __u32 fd_flags;
    __u64 heap_flags;
};

#define DMA_HEAP_IOCTL_ALLOC _IOWR('H', 0x0, struct dma_heap_allocation_data)

/* From linux/dma-buf.h */
struct dma_buf_sync {
    __u64 flags;
};

#define DMA_BUF_SYNC_READ (1 << 0)
#define DMA_BUF_SYNC_WRITE (2 << 0)
#define DMA_BUF_SYNC_RW (DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START (0 << 2)
#define DMA_BUF_SYNC_END (1 << 2)
#define DMA_BUF_BASE 'b'
#define DMA_BUF_IOCTL_SYNC _IOW(DMA_BUF_BASE, 0, struct dma_buf_sync)

/* ======================== Constants ======================== */

#define DMA_HEAP_PATH "/dev/dma_heap/linux,cma"
#define PAGE_SIZE_4K 4096

#define DMA_LOG_ERR(fmt, ...) fprintf(stderr, "[DMA][ERR] %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define DMA_LOG_INFO(fmt, ...) fprintf(stdout, "[DMA][INFO] " fmt "\n", ##__VA_ARGS__)

/* ======================== State ======================== */

static int g_heap_fd = -1;
static U64 g_cma_base = 0; /* CMA physical base from debugfs */
static U64 g_cma_next = 0; /* next assignable physical address */

/* ======================== CMA base discovery ======================== */
/*
 * DMA-buf CMA pages use remap_pfn_range() and do NOT appear in
 * /proc/self/pagemap.  Instead we read the CMA base PFN from
 * debugfs and assign monotonic physical addresses from that range.
 *
 * The assigned addresses may not match the kernel's exact CMA bitmap
 * positions, but they are:
 *   - within the real CMA physical range
 *   - unique per allocation
 *   - consistent across processes (stored in shared-memory block metadata)
 *   - different from any virtual address
 *
 * For hardware DMA programming the kernel driver obtains the real
 * physical address via dma_buf_map_attachment(); userspace physical
 * addresses are for identity/debugging only.
 */
static S32 cma_discover_base(void) {
    if (g_cma_base != 0)
        return 0;

    int fd = open("/sys/kernel/debug/cma/linux,cma/base_pfn", O_RDONLY);
    if (fd < 0) {
        /* fallback: symlink target */
        fd = open("/sys/kernel/debug/cma/linux,cma/ranges/0/base_pfn", O_RDONLY);
    }
    if (fd < 0) {
        DMA_LOG_ERR("cannot read CMA base_pfn from debugfs: %s", strerror(errno));
        return -1;
    }

    char buf[64] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        DMA_LOG_ERR("empty CMA base_pfn");
        return -1;
    }

    uint64_t base_pfn = strtoul(buf, NULL, 10);
    if (base_pfn == 0) {
        DMA_LOG_ERR("invalid CMA base_pfn: %s", buf);
        return -1;
    }

    g_cma_base = (U64)base_pfn * PAGE_SIZE_4K;
    g_cma_next = g_cma_base;
    DMA_LOG_INFO("CMA base: pfn=%" PRIu64 " phy=0x%" PRIx64, base_pfn, g_cma_base);
    return 0;
}

static U64 cma_assign_phy(U32 alloc_size) {
    U64 phy = g_cma_next;
    g_cma_next += alloc_size;
    return phy;
}

/* ======================== Public API ======================== */

S32 dma_alloc_init(void) {
    if (g_heap_fd >= 0)
        return 0; /* already open */

    g_heap_fd = open(DMA_HEAP_PATH, O_RDWR);
    if (g_heap_fd < 0) {
        DMA_LOG_ERR("open %s: %s", DMA_HEAP_PATH, strerror(errno));
        return -1;
    }

    if (cma_discover_base() != 0) {
        DMA_LOG_ERR("CMA base discovery failed — physical addresses unavailable");
        /* non-fatal: we can still allocate, just phy will be 0 */
    }

    DMA_LOG_INFO("DMA heap opened: %s (fd=%d)", DMA_HEAP_PATH, g_heap_fd);
    return 0;
}

void dma_alloc_deinit(void) {
    if (g_heap_fd >= 0) {
        close(g_heap_fd);
        g_heap_fd = -1;
    }
    g_cma_base = 0;
    g_cma_next = 0;
}

S32 dma_alloc_buf(U32 size, int *p_fd, U64 *p_phy, void **p_vir) {
    if (g_heap_fd < 0) {
        DMA_LOG_ERR("DMA heap not initialized");
        return -1;
    }
    if (!p_fd || !p_phy || !p_vir || size == 0) {
        DMA_LOG_ERR("invalid params");
        return -1;
    }

    /* round up to page size */
    U32 alloc_size = (size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);

    struct dma_heap_allocation_data alloc = {
        .len = alloc_size,
        .fd_flags = O_CLOEXEC | O_RDWR,
        .heap_flags = 0,
    };

    if (ioctl(g_heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) != 0) {
        DMA_LOG_ERR("DMA_HEAP_IOCTL_ALLOC failed (size=%u): %s", alloc_size, strerror(errno));
        return -1;
    }

    /* mmap the dma-buf fd */
    void *vir = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_SHARED, alloc.fd, 0);
    if (vir == MAP_FAILED) {
        DMA_LOG_ERR("mmap dma-buf (fd=%d, size=%u): %s", alloc.fd, alloc_size, strerror(errno));
        close(alloc.fd);
        return -1;
    }

    /* touch page to ensure mapping is established */
    *(volatile char *)vir = 0;

    /* assign physical address from CMA range */
    U64 phy = (g_cma_base != 0) ? cma_assign_phy(alloc_size) : 0;

    *p_fd = alloc.fd;
    *p_phy = phy;
    *p_vir = vir;

    return 0;
}

void dma_free_buf(int fd, void *vir, U32 size) {
    U32 alloc_size = (size + PAGE_SIZE_4K - 1) & ~(PAGE_SIZE_4K - 1);
    if (vir)
        munmap(vir, alloc_size);
    if (fd >= 0)
        close(fd);
}

S32 dma_sync_buf(int fd, U32 flags) {
    struct dma_buf_sync sync = {
        .flags = flags,
    };

    if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) != 0) {
        DMA_LOG_ERR("DMA_BUF_IOCTL_SYNC (fd=%d): %s", fd, strerror(errno));
        return -1;
    }
    return 0;
}

S32 dma_get_phy(void *vir, U64 *p_phy) {
    if (!vir || !p_phy)
        return -1;
    /* Physical address is assigned at allocation time and stored in
     * the VbBlockShm metadata. This function cannot recover the
     * physical address from an arbitrary virtual address alone.
     * Return error — callers should use the stored phy_addr. */
    DMA_LOG_ERR("dma_get_phy: not supported — use stored phy_addr from block metadata");
    return -1;
}
