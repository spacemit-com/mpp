/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    mpp_shm.h
* @Date      :    2026-3-26
* @Author    :    rmwei(rongmin.wei@spacemit.com)
* @Brief     :    POSIX shared memory control plane for multi-process MPP.
*                 All SYS/VB metadata lives here so multiple processes
*                 can operate on the same pools, blocks, binds, and PTS.
*------------------------------------------------------------------------------
*/

#ifndef MPP_SHM_H
#define MPP_SHM_H

#include <pthread.h>
#include <stdatomic.h>
#include <sys/types.h>

#include "type.h"
#include "sys_type.h"
#include "vb_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== Constants ======================== */

#define MPP_SHM_NAME        "/mpp_ctrl"
#define MPP_SHM_MAGIC       0x4D505053  /* "MPPS" */
#define MPP_SHM_VERSION     2

#define MPP_MAX_POOL        16
#define MPP_MAX_BLK         256     /* per pool */
#define MPP_MAX_BIND        128
#define MPP_MAX_MAP         256
#define MPP_CHAN_DEPTH       64      /* ring buffer depth per bind */
#define MPP_STREAM_CHAN_DEPTH 16      /* stream queue depth per bind (lightweight) */
#define MPP_STREAM_MAX_PAYLOAD (1024 * 1024)  /* max single stream packet size */

/* ======================== VB Block (shared) ======================== */

typedef enum _VbBlkState {
    VB_BLK_FREE = 0,
    VB_BLK_USED
} VbBlkState;

typedef struct _VbBlockShm {
    UL handle;
    U32 pool_id;
    U32 blk_idx;
    atomic_int ref_cnt;
    U32 state;                  /* VbBlkState */
    U64 phy_addr;               /* real CMA physical address */
    U32 size;
    U64 pts;
    U32 next_free;              /* free-list linkage, 0xFFFFFFFF = end */
    U32 exported;               /* export flag */
    pid_t owner_pid;            /* PID of allocating process */
    int owner_fd;               /* dma-buf fd in owner's fd table */
    U32 frame_info_set;         /* per-buffer metadata snapshot valid */
    VideoFrameInfo frame_info;  /* dynamic metadata: PTS/size/stride/fd/idx/etc. */
} VbBlockShm;

/* ======================== VB Pool (shared) ======================== */

typedef enum _VbPoolState {
    VB_POOL_FREE = 0,
    VB_POOL_ACTIVE,
    VB_POOL_DESTROYING
} VbPoolState;

typedef struct _VbPoolShm {
    U32 id;
    U32 state;                      /* VbPoolState */
    VbPoolCfg cfg;
    pthread_mutex_t lock;           /* PTHREAD_PROCESS_SHARED */
    pthread_cond_t cond;            /* PTHREAD_PROCESS_SHARED */
    U32 blk_cnt;
    U32 free_head;
    U32 free_cnt;
    U32 used_cnt;
    U32 min_free;
    U32 frame_info_set;
    VideoFrameInfo frame_info;
    VbBlockShm blocks[MPP_MAX_BLK];
} VbPoolShm;

/* ======================== Bind Entry ======================== */

typedef enum _SysBindState {
    SYS_BIND_FREE = 0,
    SYS_BIND_ACTIVE
} SysBindState;

typedef struct _SysBindEntry {
    U32 state;                  /* SysBindState */
    MppNode src;
    MppNode sink;
} SysBindEntry;

/* ======================== Channel Queue (per bind) ======================== */

typedef struct _MppChanQueue {
    pthread_mutex_t lock;           /* PTHREAD_PROCESS_SHARED */
    pthread_cond_t not_empty;       /* PTHREAD_PROCESS_SHARED */
    pthread_cond_t not_full;        /* PTHREAD_PROCESS_SHARED */
    U32 head;
    U32 tail;
    U32 count;
    struct {
        VideoFrameInfo frame_info;
    } entries[MPP_CHAN_DEPTH];
} MppChanQueue;

typedef struct _MppStreamQueueEntry {
    U32 used;
    StreamBufferInfo info;       /* pu8Addr is meaningless across processes */
    U64 dma_phy;                 /* DMA buffer physical address */
    U32 dma_size;                /* allocated DMA buffer size (page-aligned) */
    int dma_fd;                  /* dma-buf fd in producer's fd table */
    pid_t owner_pid;             /* PID that allocated the DMA buffer */
} MppStreamQueueEntry;

typedef struct _MppStreamQueue {
    pthread_mutex_t lock;               /* PTHREAD_PROCESS_SHARED */
    pthread_cond_t not_empty;           /* PTHREAD_PROCESS_SHARED */
    pthread_cond_t not_full;            /* PTHREAD_PROCESS_SHARED */
    U32 head;
    U32 tail;
    U32 count;
    MppStreamQueueEntry entries[MPP_STREAM_CHAN_DEPTH];
} MppStreamQueue;

/* ======================== Map Record ======================== */

typedef enum _SysMapType {
    SYS_MAP_MMAP = 0,
    SYS_MAP_MMAP_CACHE,
    SYS_MAP_MMZ,
    SYS_MAP_MMZ_CACHED
} SysMapType;

typedef struct _SysMapRecord {
    BOOL used;
    SysMapType type;
    U64 phy_addr;
    void        *vir_addr;
    U32 size;
    int dma_fd;                 /* dma-buf fd for this mapping */
    pid_t owner_pid;            /* PID that allocated */
} SysMapRecord;

/* ======================== Shared Memory Root ======================== */

typedef struct _MppSharedMem {
    /* Header */
    U32 magic;
    U32 version;
    atomic_int proc_ref;            /* attached process count */

    /* SYS inited flag */
    U32 sys_inited;

    /* PTS */
    pthread_mutex_t pts_lock;       /* PTHREAD_PROCESS_SHARED */
    U64 base_pts_us;
    U64 base_mono_ns;

    /* Bind table */
    pthread_rwlock_t bind_lock;     /* PTHREAD_PROCESS_SHARED */
    U32 bind_cnt;
    SysBindEntry binds[MPP_MAX_BIND];

    /* VB */
    pthread_rwlock_t vb_lock;       /* PTHREAD_PROCESS_SHARED */
    U32 pool_cnt;
    U32 vb_inited;
    VbPoolShm pools[MPP_MAX_POOL];

    /* Channel queues (one per bind slot) */
    MppChanQueue queues[MPP_MAX_BIND];
    MppStreamQueue stream_queues[MPP_MAX_BIND];

    /* Map records (per-process, but we keep a global table for dump) */
    pthread_mutex_t map_lock;       /* PTHREAD_PROCESS_SHARED */
    SysMapRecord maps[MPP_MAX_MAP];
} MppSharedMem;

/* ======================== API ======================== */

/**
* @brief Initialize or attach to MPP shared memory.
*        First caller creates + initializes; subsequent callers attach.
* @return 0 on success, negative on failure
*/
S32 mpp_shm_init(void);

/**
* @brief Detach from shared memory. Last process also unlinks it.
* @return 0 on success, negative on failure
*/
S32 mpp_shm_detach(void);

/**
* @brief Get pointer to the shared memory structure.
*        Must call mpp_shm_init() first.
* @return Pointer to MppSharedMem, or NULL if not attached
*/
MppSharedMem *mpp_shm_get(void);

#ifdef __cplusplus
}
#endif

#endif /* MPP_SHM_H */
