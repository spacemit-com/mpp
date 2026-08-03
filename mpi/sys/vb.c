/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    vb.c
 * @Date      :    2026-3-26
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Video Buffer module — multi-process, DMA heap backed.
 *                 Pool/block metadata lives in POSIX shared memory.
 *                 Each block is a CMA dma-buf allocated via /dev/dma_heap.
 *                 Cross-process access via pidfd_getfd().
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <inttypes.h>

#include "sys/vb_api.h"
#include "sys/mpp_shm.h"
#include "sys/dma_alloc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== Error Codes ======================== */

#define VB_ERR_OK 0
#define VB_ERR_INVAL (-2)
#define VB_ERR_NOMEM (-3)
#define VB_ERR_NOT_INIT (-4)
#define VB_ERR_BUSY (-5)
#define VB_ERR_NOT_FOUND (-6)
#define VB_ERR_TIMEOUT (-7)
#define VB_ERR_STATE (-8)
#define VB_ERR_DOUBLE_INIT (-9)

/* ======================== Constants ======================== */

#define VB_INVALID_HANDLE ((UL)0)
#define VB_FREE_LIST_END ((U32)0xFFFFFFFF)

/* Handle encoding: [31:16] = pool_id (1-based), [15:0] = blk_idx (0-based) */
#define VB_HANDLE_ENCODE(pool_id, blk_idx) (((UL)(pool_id) << 16) | ((UL)(blk_idx) & 0xFFFF))
#define VB_HANDLE_TO_POOL(h) ((U32)((h) >> 16))
#define VB_HANDLE_TO_BLK(h) ((U32)((h) & 0xFFFF))

/* Opaque token: [63:13] = export generation, [12:8] = pool, [7:0] = block. */
#define VB_TOKEN_BLOCK_BITS 8
#define VB_TOKEN_POOL_BITS 5
#define VB_TOKEN_LOCATION_BITS (VB_TOKEN_BLOCK_BITS + VB_TOKEN_POOL_BITS)
#define VB_TOKEN_BLOCK_MASK ((U64)((1U << VB_TOKEN_BLOCK_BITS) - 1))
#define VB_TOKEN_POOL_MASK ((U64)((1U << VB_TOKEN_POOL_BITS) - 1))
#define VB_TOKEN_GENERATION_MASK (UINT64_MAX >> VB_TOKEN_LOCATION_BITS)
#define VB_TOKEN_ENCODE(generation, pool_id, blk_idx)                                                  \
    ((((U64)(generation) & VB_TOKEN_GENERATION_MASK) << VB_TOKEN_LOCATION_BITS) |                      \
        (((U64)(pool_id) & VB_TOKEN_POOL_MASK) << VB_TOKEN_BLOCK_BITS) |                              \
        ((U64)(blk_idx) & VB_TOKEN_BLOCK_MASK))
#define VB_TOKEN_TO_POOL(token) ((U32)(((token) >> VB_TOKEN_BLOCK_BITS) & VB_TOKEN_POOL_MASK))
#define VB_TOKEN_TO_BLK(token) ((U32)((token) & VB_TOKEN_BLOCK_MASK))

#ifndef PAGE_SIZE
#define PAGE_SIZE (4096)
#endif

/* ======================== Log Macros ======================== */

#define VB_LOG_ERR(fmt, ...) fprintf(stderr, "[VB][ERR] %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define VB_LOG_WARN(fmt, ...) fprintf(stderr, "[VB][WARN] %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define VB_LOG_INFO(fmt, ...) fprintf(stdout, "[VB][INFO] " fmt "\n", ##__VA_ARGS__)

typedef struct _VideoFrameFmt {
    MppPixelFormat ePixelFormat;
    CHAR *cName;
    U8 u8DataPlanes;
    U8 u8WidthDepth[FRAME_MAX_PLANE];
    U8 u8HeightDepth[FRAME_MAX_PLANE];
} VideoFrameFmt;

/* ======================== Process-Local Mapping ======================== */
/*
 * Each process needs its own fd and virtual address for each block.
 * The owner process gets these during dma_alloc_buf().
 * Other processes obtain them via pidfd_getfd().
 */

#define VB_LOCAL_MAP_MAX 4096

typedef struct _VbLocalEntry {
    UL handle;       /* VB handle (0 = unused) */
    int local_fd;    /* dma-buf fd in this process */
    void *local_vir; /* mmap'd virtual address in this process */
    U32 size;        /* mapped size (page-aligned) */
    BOOL is_owner;   /* did this process allocate it? */
} VbLocalEntry;

static VbLocalEntry g_local_map[VB_LOCAL_MAP_MAX];
static pthread_mutex_t g_local_lock = PTHREAD_MUTEX_INITIALIZER;

static VideoFrameFmt g_stVideoFrameFmt[] = {
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_YV12,
        .cName = "YV12",
        .u8DataPlanes = 3,
        .u8WidthDepth = {8, 4, 4},
        .u8HeightDepth = {8, 4, 4},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_I420,
        .cName = "I420",
        .u8DataPlanes = 3,
        .u8WidthDepth = {8, 4, 4},
        .u8HeightDepth = {8, 4, 4},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_NV21,
        .cName = "NV21",
        .u8DataPlanes = 2,
        .u8WidthDepth = {8, 8},
        .u8HeightDepth = {8, 4},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_NV12,
        .cName = "NV12",
        .u8DataPlanes = 2,
        .u8WidthDepth = {8, 8},
        .u8HeightDepth = {8, 4},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_YUV422P,
        .cName = "YUV422P",
        .u8DataPlanes = 3,
        .u8WidthDepth = {8, 4, 4},
        .u8HeightDepth = {8, 8, 8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_NV61,
        .cName = "NV61",
        .u8DataPlanes = 2,
        .u8WidthDepth = {8, 8},
        .u8HeightDepth = {8, 8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_YUV444P,
        .cName = "YUV444P",
        .u8DataPlanes = 3,
        .u8WidthDepth = {8, 8, 8},
        .u8HeightDepth = {8, 8, 8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_YUV444SP,
        .cName = "YUV444SP",
        .u8DataPlanes = 2,
        .u8WidthDepth = {8, 16},
        .u8HeightDepth = {8, 8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_YUYV,
        .cName = "YUYV",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_UYVY,
        .cName = "UYVY",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_YVYU,
        .cName = "YVYU",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGBA,
        .cName = "RGBA",
        .u8DataPlanes = 1,
        .u8WidthDepth = {32},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_ARGB,
        .cName = "ARGB",
        .u8DataPlanes = 1,
        .u8WidthDepth = {32},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_ABGR,
        .cName = "ABGR",
        .u8DataPlanes = 1,
        .u8WidthDepth = {32},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_BGRA,
        .cName = "BGRA",
        .u8DataPlanes = 1,
        .u8WidthDepth = {32},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGBA_5551,
        .cName = "BGRA_5551",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_ARGB_1555,
        .cName = "ARGB_1555",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_ABGR_1555,
        .cName = "ABGR_1555",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_BGRA_5551,
        .cName = "BGRA_5551",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGBA_4444,
        .cName = "RGBA_4444",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_ARGB_4444,
        .cName = "ARGB_4444",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_ABGR_4444,
        .cName = "ABGR_4444",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_BGRA_4444,
        .cName = "BGRA_4444",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGB_888,
        .cName = "RGB_888",
        .u8DataPlanes = 1,
        .u8WidthDepth = {24},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_BGR_888,
        .cName = "BGR_888",
        .u8DataPlanes = 1,
        .u8WidthDepth = {24},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGB_565,
        .cName = "RGB_565",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_BGR_565,
        .cName = "BGR_565",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGB_555,
        .cName = "RGB_555",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_BGR_555,
        .cName = "BGR_555",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGB_444,
        .cName = "RGB_444",
        .u8DataPlanes = 1,
        .u8WidthDepth = {12},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_BGR_444,
        .cName = "BGR_444",
        .u8DataPlanes = 1,
        .u8WidthDepth = {12},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGB_BAYER_8BITS,
        .cName = "RAW8",
        .u8DataPlanes = 1,
        .u8WidthDepth = {8},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGB_BAYER_10BITS,
        .cName = "RAW10",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGB_BAYER_12BITS,
        .cName = "RAW12",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGB_BAYER_14BITS,
        .cName = "RAW14",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGB_BAYER_16BITS,
        .cName = "RAW16",
        .u8DataPlanes = 1,
        .u8WidthDepth = {16},
        .u8HeightDepth = {8},
    },
    {
        .ePixelFormat = MPP_PIXEL_FORMAT_RGB_BAYER_20BITS,
        .cName = "RAW20",
        .u8DataPlanes = 1,
        .u8WidthDepth = {32},
        .u8HeightDepth = {8},
    },
};

static VbLocalEntry *vb_local_find(UL handle) {
    for (U32 i = 0; i < VB_LOCAL_MAP_MAX; i++) {
        if (g_local_map[i].handle == handle)
            return &g_local_map[i];
    }
    return NULL;
}

static VbLocalEntry *vb_local_alloc(void) {
    for (U32 i = 0; i < VB_LOCAL_MAP_MAX; i++) {
        if (g_local_map[i].handle == 0)
            return &g_local_map[i];
    }
    return NULL;
}

static void vb_local_free_entry(VbLocalEntry *ent) {
    if (!ent)
        return;
    if (ent->local_vir) {
        U32 alloc_size = (ent->size + 4095) & ~4095u;
        munmap(ent->local_vir, alloc_size);
    }
    if (ent->local_fd >= 0)
        close(ent->local_fd);
    memset(ent, 0, sizeof(*ent));
}

/* pidfd_open / pidfd_getfd wrappers */
static int vb_pidfd_open(pid_t pid) {
    return (int)syscall(SYS_pidfd_open, pid, 0);
}

static int vb_pidfd_getfd(int pidfd, int target_fd) {
    return (int)syscall(SYS_pidfd_getfd, pidfd, target_fd, 0);
}

/*
 * Get a local fd+vir for a block, creating one if needed.
 * If this process is the owner, the entry already exists.
 * Otherwise, use pidfd_getfd to get the dma-buf fd from the owner.
 */
static S32 vb_ensure_local_mapping(VbBlockShm *blk, VbLocalEntry **out) {
    pthread_mutex_lock(&g_local_lock);

    VbLocalEntry *ent = vb_local_find(blk->handle);
    if (ent) {
        *out = ent;
        pthread_mutex_unlock(&g_local_lock);
        return 0;
    }

    ent = vb_local_alloc();
    if (!ent) {
        VB_LOG_ERR("local map full");
        pthread_mutex_unlock(&g_local_lock);
        return VB_ERR_NOMEM;
    }

    pid_t my_pid = getpid();
    int local_fd;
    void *local_vir;
    U32 alloc_size = (blk->size + 4095) & ~4095u;

    if (blk->owner_pid == my_pid) {
        /* owner — fd is already in our fd table */
        local_fd = blk->owner_fd;
    } else {
        /* cross-process: pidfd_getfd */
        int pidfd = vb_pidfd_open(blk->owner_pid);
        if (pidfd < 0) {
            VB_LOG_ERR("pidfd_open(%d): %s", blk->owner_pid, strerror(errno));
            pthread_mutex_unlock(&g_local_lock);
            return VB_ERR_NOT_FOUND;
        }
        local_fd = vb_pidfd_getfd(pidfd, blk->owner_fd);
        close(pidfd);
        if (local_fd < 0) {
            VB_LOG_ERR("pidfd_getfd(pid=%d, fd=%d): %s", blk->owner_pid, blk->owner_fd, strerror(errno));
            pthread_mutex_unlock(&g_local_lock);
            return VB_ERR_NOT_FOUND;
        }
    }

    /* mmap the dma-buf fd */
    local_vir = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_SHARED, local_fd, 0);
    if (local_vir == MAP_FAILED) {
        VB_LOG_ERR("mmap dma-buf fd=%d size=%u: %s", local_fd, alloc_size, strerror(errno));
        if (blk->owner_pid != my_pid)
            close(local_fd);
        pthread_mutex_unlock(&g_local_lock);
        return VB_ERR_NOMEM;
    }

    ent->handle = blk->handle;
    ent->local_fd = local_fd;
    ent->local_vir = local_vir;
    ent->size = blk->size;
    ent->is_owner = (blk->owner_pid == my_pid);

    *out = ent;
    pthread_mutex_unlock(&g_local_lock);
    return 0;
}

/* ======================== Robust Mutex Helper ======================== */

static int vb_mutex_lock(pthread_mutex_t *mtx) {
    int r = pthread_mutex_lock(mtx);
    if (r == EOWNERDEAD) {
        pthread_mutex_consistent(mtx);
        return 0;
    }
    return r;
}

/* ======================== Internal Helpers ======================== */

static VbPoolShm *vb_get_pool(U32 pool_id) {
    MppSharedMem *shm = mpp_shm_get();
    if (!shm)
        return NULL;
    if (pool_id == 0 || pool_id > MPP_MAX_POOL)
        return NULL;
    VbPoolShm *pool = &shm->pools[pool_id - 1];
    if (pool->state == VB_POOL_FREE)
        return NULL;
    return pool;
}

static VbBlockShm *vb_get_block(UL handle, VbPoolShm **out_pool) {
    U32 pool_id = VB_HANDLE_TO_POOL(handle);
    U32 blk_idx = VB_HANDLE_TO_BLK(handle);

    VbPoolShm *pool = vb_get_pool(pool_id);
    if (!pool)
        return NULL;
    if (blk_idx >= pool->blk_cnt)
        return NULL;

    if (out_pool)
        *out_pool = pool;
    return &pool->blocks[blk_idx];
}

/* Add a reference only while the block is live. This is safe across practical
 * slot reuse: VB_Import validates the token again after taking the reference,
 * and a false ABA would require wrapping the 51-bit generation counter within
 * one shared-memory lifetime. */
static BOOL vb_try_ref_block(VbBlockShm *blk) {
    int ref_count = atomic_load_explicit(&blk->ref_cnt, memory_order_acquire);
    while (ref_count > 0) {
        if (atomic_compare_exchange_weak_explicit(
                &blk->ref_cnt, &ref_count, ref_count + 1,
                memory_order_acquire, memory_order_relaxed))
            return MPP_TRUE;
    }
    return MPP_FALSE;
}

/* Return block to free-list; caller must hold pool->lock */
static void vb_return_block(VbPoolShm *pool, VbBlockShm *blk) {
    blk->state = VB_BLK_FREE;
    blk->pts = 0;
    atomic_store_explicit(&blk->exported, 0, memory_order_relaxed);
    atomic_store_explicit(&blk->export_token, 0, memory_order_release);
    blk->next_free = pool->free_head;
    pool->free_head = blk->blk_idx;
    pool->free_cnt++;
    pool->used_cnt--;
    pthread_cond_signal(&pool->cond);
}

/* Allocate DMA buffers for all blocks in a pool */
static S32 vb_pool_alloc_blocks(VbPoolShm *pool) {
    U32 buf_size = pool->cfg.u32BufSize;
    U32 buf_cnt = pool->cfg.u32BufCnt;
    pid_t my_pid = getpid();

    for (U32 i = 0; i < buf_cnt; i++) {
        VbBlockShm *blk = &pool->blocks[i];
        int fd = -1;
        U64 phy = 0;
        void *vir = NULL;

        if (dma_alloc_buf(buf_size, &fd, &phy, &vir) != 0) {
            VB_LOG_ERR("dma_alloc_buf failed for pool %u blk %u", pool->id, i);
            /* free already allocated blocks */
            for (U32 j = 0; j < i; j++) {
                VbBlockShm *prev = &pool->blocks[j];
                pthread_mutex_lock(&g_local_lock);
                VbLocalEntry *ent = vb_local_find(prev->handle);
                if (ent)
                    vb_local_free_entry(ent);
                pthread_mutex_unlock(&g_local_lock);
                prev->state = VB_BLK_FREE;
            }
            return VB_ERR_NOMEM;
        }

        blk->handle = VB_HANDLE_ENCODE(pool->id, i);
        blk->pool_id = pool->id;
        blk->blk_idx = i;
        atomic_init(&blk->ref_cnt, 0);
        blk->state = VB_BLK_FREE;
        blk->phy_addr = phy;
        blk->size = buf_size;
        blk->pts = 0;
        atomic_init(&blk->exported, 0);
        atomic_init(&blk->export_token, 0);
        blk->frame_info_set = 0;
        memset(&blk->frame_info, 0, sizeof(blk->frame_info));
        blk->owner_pid = my_pid;
        blk->owner_fd = fd;
        blk->next_free = (i + 1 < buf_cnt) ? (i + 1) : VB_FREE_LIST_END;

        /* register in local map */
        pthread_mutex_lock(&g_local_lock);
        VbLocalEntry *ent = vb_local_alloc();
        if (ent) {
            ent->handle = blk->handle;
            ent->local_fd = fd;
            ent->local_vir = vir;
            ent->size = buf_size;
            ent->is_owner = MPP_TRUE;
        }
        pthread_mutex_unlock(&g_local_lock);
    }

    pool->free_head = 0;
    pool->free_cnt = buf_cnt;
    pool->used_cnt = 0;
    pool->min_free = buf_cnt;
    pool->blk_cnt = buf_cnt;

    return VB_ERR_OK;
}

/* Free all DMA buffers owned by this process in a pool */
static void vb_pool_free_blocks(VbPoolShm *pool) {
    pid_t my_pid = getpid();

    for (U32 i = 0; i < pool->blk_cnt; i++) {
        VbBlockShm *blk = &pool->blocks[i];

        pthread_mutex_lock(&g_local_lock);
        VbLocalEntry *ent = vb_local_find(blk->handle);
        if (ent) {
            if (ent->is_owner) {
                /* owner frees the DMA buffer via dma_free_buf */
                dma_free_buf(ent->local_fd, ent->local_vir, ent->size);
                /* clear without close/munmap — dma_free_buf did it */
                memset(ent, 0, sizeof(*ent));
            } else {
                vb_local_free_entry(ent);
            }
        } else if (blk->owner_pid == my_pid && blk->owner_fd >= 0) {
            /* fallback: if local map entry was lost */
            dma_free_buf(blk->owner_fd, NULL, blk->size);
        }
        pthread_mutex_unlock(&g_local_lock);

        blk->state = VB_BLK_FREE;
        blk->owner_pid = 0;
        blk->owner_fd = -1;
        blk->frame_info_set = 0;
        memset(&blk->frame_info, 0, sizeof(blk->frame_info));
    }
}

VideoFrameFmt *vb_get_pic_fmt(MppPixelFormat ePixelFormat) {
    VideoFrameFmt *pstVideoFmt = NULL;
    U32 i;

    for (i = 0; i < sizeof(g_stVideoFrameFmt) / sizeof(VideoFrameFmt); ++i) {
        pstVideoFmt = &g_stVideoFrameFmt[i];
        if (pstVideoFmt->ePixelFormat == ePixelFormat) {
            return pstVideoFmt;
        }
    }

    return NULL;
}

/* ======================== VB API Implementation ======================== */

/* Per-process VB init refcount — mirrors SYS_Init pattern */
static int g_vb_init_ref = 0;

S32 VB_Init(VOID) {
    MppSharedMem *shm = mpp_shm_get();
    if (!shm) {
        VB_LOG_ERR("shared memory not initialized, call SYS_Init first");
        return VB_ERR_NOT_INIT;
    }

    if (g_vb_init_ref > 0) {
        g_vb_init_ref++;
        VB_LOG_INFO("VB_Init: per-process ref=%d", g_vb_init_ref);
        return VB_ERR_OK;
    }

    pthread_rwlock_wrlock(&shm->vb_lock);
    if (shm->vb_inited) {
        /* another process already inited — just attach */
        pthread_rwlock_unlock(&shm->vb_lock);
        g_vb_init_ref = 1;
        VB_LOG_INFO("VB already initialized, attached");
        return VB_ERR_OK;
    }

    shm->pool_cnt = 0;
    shm->vb_inited = 1;
    pthread_rwlock_unlock(&shm->vb_lock);

    g_vb_init_ref = 1;

    /* init process-local map */
    pthread_mutex_lock(&g_local_lock);
    memset(g_local_map, 0, sizeof(g_local_map));
    pthread_mutex_unlock(&g_local_lock);

    VB_LOG_INFO("VB_Init done");
    return VB_ERR_OK;
}

S32 VB_Exit(VOID) {
    MppSharedMem *shm = mpp_shm_get();
    if (!shm || !shm->vb_inited || g_vb_init_ref <= 0) {
        VB_LOG_ERR("VB not initialized");
        return VB_ERR_NOT_INIT;
    }

    g_vb_init_ref--;
    if (g_vb_init_ref > 0) {
        VB_LOG_INFO("VB_Exit: per-process ref=%d, not final", g_vb_init_ref);
        return VB_ERR_OK;
    }

    /* Only clean up process-local state. Do NOT destroy shared pools —
     * other processes may still be using them. */

    /* clean up local map */
    pthread_mutex_lock(&g_local_lock);
    for (U32 i = 0; i < VB_LOCAL_MAP_MAX; i++) {
        if (g_local_map[i].handle != 0 && !g_local_map[i].is_owner) {
            vb_local_free_entry(&g_local_map[i]);
        }
    }
    memset(g_local_map, 0, sizeof(g_local_map));
    pthread_mutex_unlock(&g_local_lock);

    VB_LOG_INFO("VB_Exit done (process-local cleanup only)");
    return VB_ERR_OK;
}

UL VB_CreatePool(VbPoolCfg *pstVbPoolCfg) {
    MppSharedMem *shm = mpp_shm_get();

    if (!pstVbPoolCfg) {
        VB_LOG_ERR("null config");
        return VB_INVALID_HANDLE;
    }
    if (!shm || !shm->vb_inited) {
        VB_LOG_ERR("VB not initialized");
        return VB_INVALID_HANDLE;
    }
    if (pstVbPoolCfg->u32BufSize == 0 || pstVbPoolCfg->u32BufCnt == 0) {
        VB_LOG_ERR("invalid config: size=%u cnt=%u", pstVbPoolCfg->u32BufSize, pstVbPoolCfg->u32BufCnt);
        return VB_INVALID_HANDLE;
    }
    if (pstVbPoolCfg->u32BufCnt > MPP_MAX_BLK) {
        VB_LOG_ERR("buf_cnt %u exceeds max %u", pstVbPoolCfg->u32BufCnt, MPP_MAX_BLK);
        return VB_INVALID_HANDLE;
    }

    pthread_rwlock_wrlock(&shm->vb_lock);

    /* find a free pool slot */
    VbPoolShm *pool = NULL;
    U32 slot;
    for (slot = 0; slot < MPP_MAX_POOL; slot++) {
        if (shm->pools[slot].state == VB_POOL_FREE) {
            pool = &shm->pools[slot];
            break;
        }
    }
    if (!pool) {
        VB_LOG_ERR("no free pool slot, max=%u", MPP_MAX_POOL);
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_INVALID_HANDLE;
    }

    /* pool->lock and pool->cond already initialized in mpp_shm_init */
    pool->id = slot + 1; /* 1-based */
    pool->cfg = *pstVbPoolCfg;
    pool->frame_info_set = 0;
    memset(&pool->frame_info, 0, sizeof(pool->frame_info));

    S32 ret = vb_pool_alloc_blocks(pool);
    if (ret != VB_ERR_OK) {
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_INVALID_HANDLE;
    }

    pool->state = VB_POOL_ACTIVE;
    shm->pool_cnt++;

    pthread_rwlock_unlock(&shm->vb_lock);

    VB_LOG_INFO(
        "VB_CreatePool: id=%u blk_size=%u blk_cnt=%u phy[0]=0x%" PRIx64,
        pool->id,
        pool->cfg.u32BufSize,
        pool->cfg.u32BufCnt,
        (uint64_t)pool->blocks[0].phy_addr);
    return (UL)pool->id;
}

S32 VB_DestroyPool(UL ulPool) {
    MppSharedMem *shm = mpp_shm_get();
    U32 pool_id = (U32)ulPool;

    if (!shm || !shm->vb_inited) {
        VB_LOG_ERR("VB not initialized");
        return VB_ERR_NOT_INIT;
    }

    pthread_rwlock_wrlock(&shm->vb_lock);

    VbPoolShm *pool = vb_get_pool(pool_id);
    if (!pool) {
        VB_LOG_ERR("pool %u not found", pool_id);
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }

    vb_mutex_lock(&pool->lock);

    if (pool->state != VB_POOL_ACTIVE) {
        VB_LOG_ERR("pool %u state %d, not active", pool_id, pool->state);
        pthread_mutex_unlock(&pool->lock);
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_STATE;
    }

    /* check for outstanding references */
    for (U32 i = 0; i < pool->blk_cnt; i++) {
        int rc = atomic_load(&pool->blocks[i].ref_cnt);
        if (rc > 0) {
            VB_LOG_ERR("pool %u blk %u ref_cnt=%d, cannot destroy", pool_id, i, rc);
            pthread_mutex_unlock(&pool->lock);
            pthread_rwlock_unlock(&shm->vb_lock);
            return VB_ERR_BUSY;
        }
    }

    pool->state = VB_POOL_DESTROYING;
    vb_pool_free_blocks(pool);
    pool->state = VB_POOL_FREE;

    pthread_mutex_unlock(&pool->lock);
    /* NOTE: do NOT destroy the mutex/cond — they live in shared memory
     * and are pre-initialized. They will be reused when the slot is
     * allocated again. */

    shm->pool_cnt--;
    pthread_rwlock_unlock(&shm->vb_lock);

    VB_LOG_INFO("VB_DestroyPool: id=%u destroyed", pool_id);
    return VB_ERR_OK;
}

UL VB_GetBuffer(UL ulPool, U32 u32TimeoutMs) {
    MppSharedMem *shm = mpp_shm_get();
    U32 pool_id = (U32)ulPool;

    if (!shm || !shm->vb_inited) {
        VB_LOG_ERR("VB not initialized");
        return VB_INVALID_HANDLE;
    }

    pthread_rwlock_rdlock(&shm->vb_lock);

    VbPoolShm *pool = vb_get_pool(pool_id);
    if (!pool) {
        VB_LOG_ERR("pool %u not found", pool_id);
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_INVALID_HANDLE;
    }

    vb_mutex_lock(&pool->lock);
    pthread_rwlock_unlock(&shm->vb_lock);

    if (pool->state != VB_POOL_ACTIVE) {
        VB_LOG_ERR("pool %u not active", pool_id);
        pthread_mutex_unlock(&pool->lock);
        return VB_INVALID_HANDLE;
    }

    /* wait for free block */
    if (pool->free_head == VB_FREE_LIST_END) {
        if (u32TimeoutMs == 0) {
            pthread_mutex_unlock(&pool->lock);
            return VB_INVALID_HANDLE;
        }

        if (u32TimeoutMs == (U32)-1) {
            while (pool->free_head == VB_FREE_LIST_END && pool->state == VB_POOL_ACTIVE) {
                pthread_cond_wait(&pool->cond, &pool->lock);
            }
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += u32TimeoutMs / 1000;
            ts.tv_nsec += (u32TimeoutMs % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000L;
            }
            while (pool->free_head == VB_FREE_LIST_END && pool->state == VB_POOL_ACTIVE) {
                int rc = pthread_cond_timedwait(&pool->cond, &pool->lock, &ts);
                if (rc == ETIMEDOUT)
                    break;
            }
        }

        if (pool->free_head == VB_FREE_LIST_END || pool->state != VB_POOL_ACTIVE) {
            pthread_mutex_unlock(&pool->lock);
            return VB_INVALID_HANDLE;
        }
    }

    /* pop from free-list */
    U32 idx = pool->free_head;
    VbBlockShm *blk = &pool->blocks[idx];
    pool->free_head = blk->next_free;
    blk->next_free = VB_FREE_LIST_END;

    blk->state = VB_BLK_USED;
    atomic_store(&blk->ref_cnt, 1);
    blk->pts = 0;

    pool->free_cnt--;
    pool->used_cnt++;
    if (pool->free_cnt < pool->min_free)
        pool->min_free = pool->free_cnt;

    pthread_mutex_unlock(&pool->lock);

    /* ensure this process has a local mapping */
    VbLocalEntry *ent = NULL;
    vb_ensure_local_mapping(blk, &ent);

    return blk->handle;
}

/* Drop one reference already owned by the caller. The caller must hold
 * shm->vb_lock for reading; this helper always releases that lock. Keeping the
 * resolved pool/block pair makes an import rollback unambiguously release the
 * exact reference acquired by vb_try_ref_block(). */
static S32 vb_release_ref_locked(MppSharedMem *shm, VbPoolShm *pool, VbBlockShm *blk, UL handle) {
    int old_ref = atomic_fetch_sub(&blk->ref_cnt, 1);
    if (old_ref <= 0) {
        atomic_store(&blk->ref_cnt, 0);
        VB_LOG_ERR("double release on handle 0x%lx", handle);
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_STATE;
    }

    if (old_ref == 1) {
        /* ref dropped to 0 — return block to pool */
        vb_mutex_lock(&pool->lock);
        pthread_rwlock_unlock(&shm->vb_lock);

        /* free non-owner local mappings */
        pthread_mutex_lock(&g_local_lock);
        VbLocalEntry *ent = vb_local_find(handle);
        if (ent && !ent->is_owner)
            vb_local_free_entry(ent);
        pthread_mutex_unlock(&g_local_lock);

        vb_return_block(pool, blk);
        pthread_mutex_unlock(&pool->lock);
    } else {
        pthread_rwlock_unlock(&shm->vb_lock);
    }

    return VB_ERR_OK;
}

S32 VB_ReleaseBuffer(UL ulBuff) {
    MppSharedMem *shm = mpp_shm_get();
    VbPoolShm *pool = NULL;

    if (ulBuff == VB_INVALID_HANDLE)
        return VB_ERR_INVAL;
    if (!shm || !shm->vb_inited)
        return VB_ERR_NOT_INIT;

    pthread_rwlock_rdlock(&shm->vb_lock);

    VbBlockShm *blk = vb_get_block(ulBuff, &pool);
    if (!blk || !pool) {
        VB_LOG_ERR("handle 0x%lx not found", ulBuff);
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }

    return vb_release_ref_locked(shm, pool, blk, ulBuff);
}

S32 VB_RefAdd(UL ulBuff) {
    MppSharedMem *shm = mpp_shm_get();

    if (ulBuff == VB_INVALID_HANDLE)
        return VB_ERR_INVAL;
    if (!shm || !shm->vb_inited)
        return VB_ERR_NOT_INIT;

    pthread_rwlock_rdlock(&shm->vb_lock);

    VbBlockShm *blk = vb_get_block(ulBuff, NULL);
    if (!blk) {
        VB_LOG_ERR("handle 0x%lx not found", ulBuff);
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }

    int old = atomic_fetch_add(&blk->ref_cnt, 1);
    if (old <= 0) {
        atomic_fetch_sub(&blk->ref_cnt, 1);
        VB_LOG_ERR("RefAdd on free block handle 0x%lx", ulBuff);
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_STATE;
    }

    pthread_rwlock_unlock(&shm->vb_lock);
    return VB_ERR_OK;
}

S32 VB_RefSub(UL ulBuff) {
    return VB_ReleaseBuffer(ulBuff);
}

S32 VB_SetBufferPTS(UL ulBuff, U64 u64PTS) {
    MppSharedMem *shm = mpp_shm_get();
    if (ulBuff == VB_INVALID_HANDLE)
        return VB_ERR_INVAL;
    if (!shm || !shm->vb_inited)
        return VB_ERR_NOT_INIT;

    pthread_rwlock_rdlock(&shm->vb_lock);
    VbBlockShm *blk = vb_get_block(ulBuff, NULL);
    if (!blk || atomic_load(&blk->ref_cnt) <= 0) {
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }
    blk->pts = u64PTS;
    if (blk->frame_info_set)
        blk->frame_info.stVFrame.u64PTS = u64PTS;
    pthread_rwlock_unlock(&shm->vb_lock);
    return VB_ERR_OK;
}

S32 VB_UpdateBufferFrameInfo(UL ulBuff, const VideoFrameInfo *pstFrameInfo) {
    MppSharedMem *shm = mpp_shm_get();
    VbPoolShm *pool = NULL;

    if (ulBuff == VB_INVALID_HANDLE || !pstFrameInfo)
        return VB_ERR_INVAL;
    if (!shm || !shm->vb_inited)
        return VB_ERR_NOT_INIT;

    pthread_rwlock_rdlock(&shm->vb_lock);
    VbBlockShm *blk = vb_get_block(ulBuff, &pool);
    if (!blk || !pool || atomic_load(&blk->ref_cnt) <= 0) {
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }

    blk->frame_info = *pstFrameInfo;
    blk->frame_info.ulPoolId = (UL)pool->id;
    blk->frame_info.ulBufferId = blk->handle;
    if (blk->frame_info.u32Idx == 0)
        blk->frame_info.u32Idx = blk->blk_idx;
    blk->pts = blk->frame_info.stVFrame.u64PTS;
    blk->frame_info_set = 1;

    pthread_rwlock_unlock(&shm->vb_lock);
    return VB_ERR_OK;
}

S32 VB_SetFrameInfo(UL ulPool, VideoFrameInfo *pstFrameInfo) {
    MppSharedMem *shm = mpp_shm_get();
    if (!pstFrameInfo)
        return VB_ERR_INVAL;
    if (!shm || !shm->vb_inited)
        return VB_ERR_NOT_INIT;

    pthread_rwlock_rdlock(&shm->vb_lock);
    VbPoolShm *pool = vb_get_pool((U32)ulPool);
    if (!pool) {
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }
    vb_mutex_lock(&pool->lock);
    pthread_rwlock_unlock(&shm->vb_lock);

    pool->frame_info = *pstFrameInfo;
    pool->frame_info_set = 1;

    pthread_mutex_unlock(&pool->lock);
    return VB_ERR_OK;
}

S32 VB_GetFrameInfo(UL ulBuff, VideoFrameInfo *pstFrameInfo) {
    MppSharedMem *shm = mpp_shm_get();
    VbPoolShm *pool = NULL;
    VbBlockShm *blk = NULL;
    U64 u64PhyAddr = 0;
    U32 u32BlkSize = 0;
    U32 u32Offset = 0;
    U32 i = 0;

    if (!pstFrameInfo)
        return VB_ERR_INVAL;
    if (!shm || !shm->vb_inited)
        return VB_ERR_NOT_INIT;

    pthread_rwlock_rdlock(&shm->vb_lock);
    blk = vb_get_block(ulBuff, &pool);
    if (!blk || !pool || atomic_load(&blk->ref_cnt) <= 0) {
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }

    vb_mutex_lock(&pool->lock);
    pthread_rwlock_unlock(&shm->vb_lock);

    if (blk->frame_info_set) {
        *pstFrameInfo = blk->frame_info;
    } else if (pool->frame_info_set) {
        *pstFrameInfo = pool->frame_info;
    } else {
        memset(pstFrameInfo, 0, sizeof(VideoFrameInfo));
    }
    pstFrameInfo->ulPoolId = (UL)pool->id;
    pstFrameInfo->ulBufferId = blk->handle;
    if (pstFrameInfo->u32Idx == 0)
        pstFrameInfo->u32Idx = blk->blk_idx;
    pstFrameInfo->stVFrame.u64PTS = blk->pts;
    if (pstFrameInfo->stVFrame.u32TotalSize == 0)
        pstFrameInfo->stVFrame.u32TotalSize = blk->size;
    if (pstFrameInfo->stVFrame.u32PlaneNum == 0)
        pstFrameInfo->stVFrame.u32PlaneNum = 1;
    if (pstFrameInfo->stVFrame.u32PlaneSize[0] == 0)
        pstFrameInfo->stVFrame.u32PlaneSize[0] = pstFrameInfo->stVFrame.u32TotalSize;
    if (pstFrameInfo->stVFrame.u32PlaneSizeValid[0] == 0)
        pstFrameInfo->stVFrame.u32PlaneSizeValid[0] = pstFrameInfo->stVFrame.u32PlaneSize[0];
    u64PhyAddr = blk->phy_addr;
    u32BlkSize = blk->size;

    pthread_mutex_unlock(&pool->lock);

    VbLocalEntry *ent = NULL;
    if (vb_ensure_local_mapping(blk, &ent) == VB_ERR_OK && ent != NULL) {
        u32Offset = 0;
        for (i = 0; i < pstFrameInfo->stVFrame.u32PlaneNum && i < FRAME_MAX_PLANE; i++) {
            if (u32Offset >= u32BlkSize)
                break;
            if (pstFrameInfo->stVFrame.u64PlanePhyAddr[i] == 0)
                pstFrameInfo->stVFrame.u64PlanePhyAddr[i] = u64PhyAddr + u32Offset;
            if (ent->local_vir)
                pstFrameInfo->stVFrame.ulPlaneVirAddr[i] = (UL)((uintptr_t)ent->local_vir + u32Offset);
            if (ent->local_fd >= 0)
                pstFrameInfo->stVFrame.u32Fd[i] = (UL)ent->local_fd;
            if (pstFrameInfo->stVFrame.u32PlaneSizeValid[i] == 0)
                pstFrameInfo->stVFrame.u32PlaneSizeValid[i] = pstFrameInfo->stVFrame.u32PlaneSize[i];
            u32Offset += pstFrameInfo->stVFrame.u32PlaneSize[i];
        }
    }

    return VB_ERR_OK;
}

/* ======================== Cross-Process Export/Import ======================== */
/*
 * With shared memory, all processes see the same pool/block metadata.
 * Export marks the block as exported (adds a ref).
 * Import in another process decodes the block location and validates the
 * export generation before taking a reference. A delayed token can therefore
 * never alias a newer frame after the block slot is reused.
 */

S32 VB_Export(UL ulBuff, U64 *pu64Token) {
    MppSharedMem *shm = mpp_shm_get();

    if (ulBuff == VB_INVALID_HANDLE || !pu64Token)
        return VB_ERR_INVAL;
    if (!shm || !shm->vb_inited)
        return VB_ERR_NOT_INIT;

    pthread_rwlock_rdlock(&shm->vb_lock);

    VbPoolShm *pool = NULL;
    VbBlockShm *blk = vb_get_block(ulBuff, &pool);
    if (!blk || !pool) {
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }

    vb_mutex_lock(&pool->lock);
    if (pool->state != VB_POOL_ACTIVE || blk->state != VB_BLK_USED || atomic_load(&blk->ref_cnt) <= 0) {
        pthread_mutex_unlock(&pool->lock);
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_STATE;
    }
    if (atomic_load_explicit(&blk->exported, memory_order_acquire)) {
        VB_LOG_ERR("block 0x%lx already exported", ulBuff);
        pthread_mutex_unlock(&pool->lock);
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_STATE;
    }

    U64 generation;
    do {
        generation = pool->next_export_generation++ & VB_TOKEN_GENERATION_MASK;
    } while (generation == 0);

    atomic_fetch_add(&blk->ref_cnt, 1);
    const U64 token = VB_TOKEN_ENCODE(generation, blk->pool_id, blk->blk_idx);
    atomic_store_explicit(&blk->export_token, token, memory_order_release);
    atomic_store_explicit(&blk->exported, 1, memory_order_release);
    *pu64Token = token;

    pthread_mutex_unlock(&pool->lock);
    pthread_rwlock_unlock(&shm->vb_lock);

    VB_LOG_INFO("VB_Export: handle=0x%" PRIx64 " token=0x%" PRIx64, (uint64_t)ulBuff, (uint64_t)*pu64Token);
    return VB_ERR_OK;
}

S32 VB_Import(U64 u64Token, UL *pulBuff) {
    MppSharedMem *shm = mpp_shm_get();
    U32 pool_id = VB_TOKEN_TO_POOL(u64Token);
    U32 blk_idx = VB_TOKEN_TO_BLK(u64Token);
    UL handle = VB_HANDLE_ENCODE(pool_id, blk_idx);

    if (!pulBuff)
        return VB_ERR_INVAL;
    if (!shm || !shm->vb_inited)
        return VB_ERR_NOT_INIT;
    if (u64Token == 0 || pool_id == 0 || pool_id > MPP_MAX_POOL || blk_idx >= MPP_MAX_BLK)
        return VB_ERR_INVAL;

    pthread_rwlock_rdlock(&shm->vb_lock);

    VbPoolShm *pool = NULL;
    VbBlockShm *blk = vb_get_block(handle, &pool);
    if (!blk || !pool) {
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }

    const U64 observed_token = atomic_load_explicit(&blk->export_token, memory_order_acquire);
    if (!atomic_load_explicit(&blk->exported, memory_order_acquire) || observed_token != u64Token ||
        !vb_try_ref_block(blk)) {
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_STALE_TOKEN;
    }

    /* Unexport/reuse may race the first check. The successful CAS owns one
     * reference, so the current allocation cannot reach zero or be reused
     * until that reference is returned here or handed to the caller. */
    if (!atomic_load_explicit(&blk->exported, memory_order_acquire) ||
        atomic_load_explicit(&blk->export_token, memory_order_acquire) != u64Token) {
        (void)vb_release_ref_locked(shm, pool, blk, handle);
        return VB_ERR_STALE_TOKEN;
    }
    *pulBuff = handle;

    pthread_rwlock_unlock(&shm->vb_lock);

    /* ensure local mapping (will pidfd_getfd if cross-process) */
    VbLocalEntry *ent = NULL;
    vb_ensure_local_mapping(blk, &ent);

    VB_LOG_INFO("VB_Import: token=0x%" PRIx64 " handle=0x%" PRIx64, (uint64_t)u64Token, (uint64_t)handle);
    return VB_ERR_OK;
}

S32 VB_Unexport(UL ulBuff) {
    MppSharedMem *shm = mpp_shm_get();

    if (ulBuff == VB_INVALID_HANDLE)
        return VB_ERR_INVAL;
    if (!shm || !shm->vb_inited)
        return VB_ERR_NOT_INIT;

    pthread_rwlock_rdlock(&shm->vb_lock);

    VbPoolShm *pool = NULL;
    VbBlockShm *blk = vb_get_block(ulBuff, &pool);
    if (!blk || !pool) {
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }

    if (!atomic_exchange_explicit(&blk->exported, 0, memory_order_acq_rel)) {
        VB_LOG_ERR("block 0x%lx not exported", ulBuff);
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_STATE;
    }

    atomic_store_explicit(&blk->export_token, 0, memory_order_release);
    pthread_rwlock_unlock(&shm->vb_lock);

    return VB_ReleaseBuffer(ulBuff);
}

/* ======================== Query Helpers ======================== */

S32 VB_GetPhyAddr(UL ulBuff, U64 *pu64PhyAddr) {
    MppSharedMem *shm = mpp_shm_get();
    if (!pu64PhyAddr || ulBuff == VB_INVALID_HANDLE)
        return VB_ERR_INVAL;
    if (!shm || !shm->vb_inited)
        return VB_ERR_NOT_INIT;

    pthread_rwlock_rdlock(&shm->vb_lock);
    VbBlockShm *blk = vb_get_block(ulBuff, NULL);
    if (!blk || atomic_load(&blk->ref_cnt) <= 0) {
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }
    *pu64PhyAddr = blk->phy_addr;
    pthread_rwlock_unlock(&shm->vb_lock);
    return VB_ERR_OK;
}

S32 VB_GetVirAddr(UL ulBuff, void **ppVirAddr) {
    MppSharedMem *shm = mpp_shm_get();
    if (!ppVirAddr || ulBuff == VB_INVALID_HANDLE)
        return VB_ERR_INVAL;
    if (!shm || !shm->vb_inited)
        return VB_ERR_NOT_INIT;

    pthread_rwlock_rdlock(&shm->vb_lock);
    VbBlockShm *blk = vb_get_block(ulBuff, NULL);
    if (!blk || atomic_load(&blk->ref_cnt) <= 0) {
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }
    pthread_rwlock_unlock(&shm->vb_lock);

    VbLocalEntry *ent = NULL;
    S32 ret = vb_ensure_local_mapping(blk, &ent);
    if (ret != 0)
        return ret;
    *ppVirAddr = ent->local_vir;
    return VB_ERR_OK;
}

S32 VB_GetDmaBufFd(UL ulBuff, S32 *ps32Fd) {
    MppSharedMem *shm = mpp_shm_get();
    if (!ps32Fd || ulBuff == VB_INVALID_HANDLE)
        return VB_ERR_INVAL;
    if (!shm || !shm->vb_inited)
        return VB_ERR_NOT_INIT;

    pthread_rwlock_rdlock(&shm->vb_lock);
    VbBlockShm *blk = vb_get_block(ulBuff, NULL);
    if (!blk || atomic_load(&blk->ref_cnt) <= 0) {
        pthread_rwlock_unlock(&shm->vb_lock);
        return VB_ERR_NOT_FOUND;
    }
    pthread_rwlock_unlock(&shm->vb_lock);

    VbLocalEntry *ent = NULL;
    S32 ret = vb_ensure_local_mapping(blk, &ent);
    if (ret != 0)
        return ret;
    *ps32Fd = ent->local_fd;
    return VB_ERR_OK;
}

S32 VB_GetPicBufferSize(VideoFrameInfo *pstFrameInfo) {
    VideoFrameFmt *pstVideoFmt = NULL;
    VideoFrame *pstVFrame = NULL;
    CommonFrameInfo *pstCommFrameInfo = NULL;
    U32 i = 0, u32TotalSize = 0;

    if (!pstFrameInfo)
        return VB_ERR_INVAL;

    pstCommFrameInfo = &pstFrameInfo->stCommFrameInfo;

    pstVideoFmt = vb_get_pic_fmt(pstCommFrameInfo->ePixelFormat);
    if (!pstVideoFmt) {
        VB_LOG_ERR("get picure format error pixfmt:%d!", pstCommFrameInfo->ePixelFormat);
        return VB_ERR_INVAL;
    }
    if (0 == pstCommFrameInfo->u32Width || 0 == pstCommFrameInfo->u32Height) {
        VB_LOG_ERR("error picture size w:%d h:%d!\n", pstCommFrameInfo->u32Width, pstCommFrameInfo->u32Height);
        return VB_ERR_INVAL;
    }
    if (pstCommFrameInfo->u32Align == 0) {
        pstCommFrameInfo->u32Align = 16; /* default align to 16 */
    }

    pstVFrame = &pstFrameInfo->stVFrame;
    for (i = 0; i < pstVideoFmt->u8DataPlanes; ++i) {
        pstVFrame->u32PlaneStride[i] =
            ALIGNUP((pstCommFrameInfo->u32Width * pstVideoFmt->u8WidthDepth[i] + 7) >> 3, pstCommFrameInfo->u32Align);
        pstVFrame->u32PlaneSizeValid[i] = pstVFrame->u32PlaneStride[i] *
            ALIGNUP(pstCommFrameInfo->u32Height, pstCommFrameInfo->u32Align) * pstVideoFmt->u8HeightDepth[i] / 8;
        pstVFrame->u32PlaneSize[i] = ALIGNUP(pstVFrame->u32PlaneSizeValid[i], PAGE_SIZE);
        u32TotalSize += pstVFrame->u32PlaneSize[i];
    }

    pstVFrame->u32TotalSize = u32TotalSize;
    VB_LOG_INFO("u32TotalSize size %d\n", pstVFrame->u32TotalSize);

    return pstVFrame->u32TotalSize;
}

/* ======================== Debug Dump ======================== */

VOID VB_DumpPools(VOID) {
    MppSharedMem *shm = mpp_shm_get();
    static const char *pool_state_str[] = {"FREE", "ACTIVE", "DESTROYING"};
    static const char *blk_state_str[] = {"FREE", "USED"};

    printf("\n========== VB Pool Dump ==========\n");

    if (!shm || !shm->vb_inited) {
        printf("  VB not initialized\n");
        printf("==================================\n\n");
        return;
    }

    printf("  pool_cnt  : %u\n", shm->pool_cnt);

    pthread_rwlock_rdlock(&shm->vb_lock);

    for (U32 i = 0; i < MPP_MAX_POOL; i++) {
        VbPoolShm *pool = &shm->pools[i];
        if (pool->state == VB_POOL_FREE)
            continue;

        printf("\n  Pool[%u]  id=%u  state=%s\n", i, pool->id, pool_state_str[pool->state]);
        printf("    buf_size=%u  buf_cnt=%u  mod=%d\n", pool->cfg.u32BufSize, pool->cfg.u32BufCnt, pool->cfg.eModId);
        printf("    free=%u  used=%u  min_free=%u\n", pool->free_cnt, pool->used_cnt, pool->min_free);

        printf("    Blocks:\n");
        printf(
            "      %5s %10s %5s %5s %4s %18s %8s %6s\n",
            "idx", "handle", "state", "ref", "exp", "phy_addr", "pid", "fd");
        for (U32 j = 0; j < pool->blk_cnt; j++) {
            VbBlockShm *blk = &pool->blocks[j];
            int rc = atomic_load(&blk->ref_cnt);
            printf(
                "      %5u 0x%08" PRIx64 " %5s %5d %4u 0x%016" PRIx64 " %8d %6d\n",
                blk->blk_idx,
                (uint64_t)blk->handle,
                blk_state_str[blk->state],
                rc,
                atomic_load_explicit(&blk->exported, memory_order_relaxed),
                (uint64_t)blk->phy_addr,
                blk->owner_pid,
                blk->owner_fd);
        }
    }

    pthread_rwlock_unlock(&shm->vb_lock);
    printf("==================================\n\n");
}

#ifdef __cplusplus
}
#endif
