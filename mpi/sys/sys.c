/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    sys.c
 * @Date      :    2026-3-26
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    System module implementation for MPP.
 *                 Multi-process safe via POSIX shared memory.
 *                 PTS (monotonic clock), Bind graph, DMA heap MmzAlloc,
 *                 SendFrame/RecvFrame zero-copy channel queue.
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

#include "sys/sys_api.h"
#include "sys/mpp_shm.h"
#include "sys/dma_alloc.h"
#include "sys/vb_api.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== Log Macros ======================== */

#define SYS_LOG_ERR(fmt, ...) \
    fprintf(stderr, "[SYS][ERR] %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define SYS_LOG_WARN(fmt, ...) \
    fprintf(stderr, "[SYS][WARN] %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define SYS_LOG_INFO(fmt, ...) \
    fprintf(stdout, "[SYS][INFO] " fmt "\n", ##__VA_ARGS__)

/* ======================== Robust mutex helper ======================== */

static inline int sys_mutex_lock(pthread_mutex_t *mtx)
{
    int r = pthread_mutex_lock(mtx);
    if (r == EOWNERDEAD) {
        pthread_mutex_consistent(mtx);
        return 0;
    }
    return r;
}

/* ======================== PTS Helper ======================== */

static U64 sys_get_mono_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (U64)ts.tv_sec * 1000000000ULL + (U64)ts.tv_nsec;
}

/* ======================== Bind Helpers ======================== */

static BOOL sys_node_equal(const MppNode *a, const MppNode *b)
{
    return (a->eModId == b->eModId &&
            a->s32DevId == b->s32DevId &&
            a->s32ChnId == b->s32ChnId);
}

static S32 sys_find_bind_idx(MppSharedMem *shm, const MppNode *src, const MppNode *sink)
{
    for (U32 i = 0; i < MPP_MAX_BIND; i++) {
        SysBindEntry *e = &shm->binds[i];
        if (e->state == SYS_BIND_ACTIVE &&
            sys_node_equal(&e->src, src) &&
            sys_node_equal(&e->sink, sink)) {
            return (S32)i;
        }
    }
    return -1;
}

static S32 sys_find_sink_bind_idx(MppSharedMem *shm, const MppNode *sink)
{
    for (U32 i = 0; i < MPP_MAX_BIND; i++) {
        SysBindEntry *e = &shm->binds[i];
        if (e->state == SYS_BIND_ACTIVE && sys_node_equal(&e->sink, sink)) {
            return (S32)i;
        }
    }
    return -1;
}

static VOID sys_reset_stream_queue(MppStreamQueue *q)
{
    if (!q) {
        return;
    }

    sys_mutex_lock(&q->lock);

    /* Free any DMA buffers still in the queue (leaked by crashed producer) */
    for (U32 i = 0; i < MPP_STREAM_CHAN_DEPTH; i++) {
        MppStreamQueueEntry *e = &q->entries[i];
        if (e->used && e->dma_fd >= 0 && e->owner_pid == getpid()) {
            dma_free_buf(e->dma_fd, NULL, e->dma_size);
        }
    }

    q->head = 0;
    q->tail = 0;
    q->count = 0;
    memset(q->entries, 0, sizeof(q->entries));
    pthread_mutex_unlock(&q->lock);
}

/* ======================== Map Helpers ======================== */

static SysMapRecord *sys_alloc_map_record(MppSharedMem *shm)
{
    for (U32 i = 0; i < MPP_MAX_MAP; i++) {
        if (!shm->maps[i].used)
            return &shm->maps[i];
    }
    return NULL;
}

static SysMapRecord *sys_find_map_by_vir(MppSharedMem *shm, void *vir_addr)
{
    for (U32 i = 0; i < MPP_MAX_MAP; i++) {
        if (shm->maps[i].used && shm->maps[i].vir_addr == vir_addr)
            return &shm->maps[i];
    }
    return NULL;
}

/* ======================== SYS API Implementation ======================== */

static int g_sys_init_ref = 0;  /* per-process init refcount */

S32 SYS_Init(VOID)
{
    S32 ret;

    /* Per-process refcount: if already inited locally, just bump */
    if (g_sys_init_ref > 0) {
        g_sys_init_ref++;
        return SYS_ERR_OK;
    }

    ret = mpp_shm_init();
    if (ret != 0) {
        SYS_LOG_ERR("shared memory init failed");
        return SYS_ERR_NOMEM;
    }

    MppSharedMem *shm = mpp_shm_get();
    if (!shm) {
        SYS_LOG_ERR("shared memory not available");
        return SYS_ERR_NOMEM;
    }

    /* Check if SYS already initialized by another process */
    sys_mutex_lock(&shm->pts_lock);
    if (shm->sys_inited) {
        /* Already inited — just attach (multi-process: second process joins) */
        pthread_mutex_unlock(&shm->pts_lock);
        SYS_LOG_INFO("SYS_Init: attached to existing session");

        /* Initialize DMA heap in this process */
        dma_alloc_init();
        g_sys_init_ref = 1;
        return SYS_ERR_OK;
    }

    /* First init — set up PTS baseline */
    shm->base_pts_us  = 0;
    shm->base_mono_ns = sys_get_mono_ns();
    shm->bind_cnt = 0;
    memset(shm->binds, 0, sizeof(shm->binds));
    memset(shm->maps, 0, sizeof(shm->maps));
    /* Note: stream_queues are already initialized by shm_init_all() with
     * proper PTHREAD_PROCESS_SHARED mutexes/conds — do NOT memset them here */
    shm->sys_inited = 1;
    pthread_mutex_unlock(&shm->pts_lock);

    /* Initialize DMA heap */
    ret = dma_alloc_init();
    if (ret != 0) {
        SYS_LOG_WARN("DMA heap init failed — MmzAlloc will not work");
    }

    SYS_LOG_INFO("SYS_Init done (shared memory, DMA heap)");
    g_sys_init_ref = 1;
    return SYS_ERR_OK;
}

S32 SYS_Exit(VOID)
{
    MppSharedMem *shm = mpp_shm_get();

    if (!shm || !shm->sys_inited || g_sys_init_ref <= 0) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    /* Per-process refcount: if not last exit, just decrement */
    if (--g_sys_init_ref > 0) {
        return SYS_ERR_OK;
    }

    /* warn about active binds */
    pthread_rwlock_wrlock(&shm->bind_lock);
    for (U32 i = 0; i < MPP_MAX_BIND; i++) {
        if (shm->binds[i].state == SYS_BIND_ACTIVE) {
            SYS_LOG_WARN("bind [mod%d dev%d chn%d]->[mod%d dev%d chn%d] still active at exit",
                         shm->binds[i].src.eModId, shm->binds[i].src.s32DevId, shm->binds[i].src.s32ChnId,
                         shm->binds[i].sink.eModId, shm->binds[i].sink.s32DevId, shm->binds[i].sink.s32ChnId);
            shm->binds[i].state = SYS_BIND_FREE;
        }
    }
    shm->bind_cnt = 0;
    pthread_rwlock_unlock(&shm->bind_lock);

    /* warn about leaked map records and free DMA buffers */
    sys_mutex_lock(&shm->map_lock);
    for (U32 i = 0; i < MPP_MAX_MAP; i++) {
        SysMapRecord *r = &shm->maps[i];
        if (r->used && r->owner_pid == getpid()) {
            SYS_LOG_WARN("leaked map: type=%d phy=0x%llx vir=%p size=%u",
                         r->type, (unsigned long long)r->phy_addr, r->vir_addr, r->size);
            if (r->dma_fd >= 0) {
                dma_free_buf(r->dma_fd, r->vir_addr, r->size);
            }
            r->used = MPP_FALSE;
        }
    }
    pthread_mutex_unlock(&shm->map_lock);

    shm->sys_inited = 0;

    dma_alloc_deinit();
    mpp_shm_detach();

    SYS_LOG_INFO("SYS_Exit done");
    return SYS_ERR_OK;
}

/* ======================== Bind / UnBind ======================== */

S32 SYS_Bind(const MppNode *pstSrcNode, const MppNode *pstSinkNode)
{
    MppSharedMem *shm = mpp_shm_get();

    if (!pstSrcNode || !pstSinkNode) {
        SYS_LOG_ERR("null node pointer");
        return SYS_ERR_INVAL;
    }
    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }
    if (pstSrcNode->eModId <= 0 || pstSrcNode->eModId >= MPP_ID_MAX ||
        pstSinkNode->eModId <= 0 || pstSinkNode->eModId >= MPP_ID_MAX) {
        SYS_LOG_ERR("invalid module id: src=%d sink=%d",
                     pstSrcNode->eModId, pstSinkNode->eModId);
        return SYS_ERR_INVAL;
    }

    pthread_rwlock_wrlock(&shm->bind_lock);

    /* check duplicate */
    if (sys_find_bind_idx(shm, pstSrcNode, pstSinkNode) >= 0) {
        SYS_LOG_ERR("bind already exists");
        pthread_rwlock_unlock(&shm->bind_lock);
        return SYS_ERR_EXIST;
    }

    /* find free slot */
    SysBindEntry *slot = NULL;
    U32 slot_idx = 0;
    for (U32 i = 0; i < MPP_MAX_BIND; i++) {
        if (shm->binds[i].state == SYS_BIND_FREE) {
            slot = &shm->binds[i];
            slot_idx = i;
            break;
        }
    }
    if (!slot) {
        SYS_LOG_ERR("bind table full, max=%u", MPP_MAX_BIND);
        pthread_rwlock_unlock(&shm->bind_lock);
        return SYS_ERR_FULL;
    }

    slot->state = SYS_BIND_ACTIVE;
    slot->src   = *pstSrcNode;
    slot->sink  = *pstSinkNode;
    shm->bind_cnt++;

    /* reset associated channel queue */
    MppChanQueue *q = &shm->queues[slot_idx];
    sys_mutex_lock(&q->lock);
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_unlock(&q->lock);

    sys_reset_stream_queue(&shm->stream_queues[slot_idx]);

    pthread_rwlock_unlock(&shm->bind_lock);

    SYS_LOG_INFO("SYS_Bind: [mod%d dev%d chn%d]->[mod%d dev%d chn%d] slot=%u",
                 pstSrcNode->eModId, pstSrcNode->s32DevId, pstSrcNode->s32ChnId,
                 pstSinkNode->eModId, pstSinkNode->s32DevId, pstSinkNode->s32ChnId,
                 slot_idx);
    return SYS_ERR_OK;
}

S32 SYS_UnBind(const MppNode *pstSrcNode, const MppNode *pstSinkNode)
{
    MppSharedMem *shm = mpp_shm_get();

    if (!pstSrcNode || !pstSinkNode) {
        SYS_LOG_ERR("null node pointer");
        return SYS_ERR_INVAL;
    }
    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    pthread_rwlock_wrlock(&shm->bind_lock);

    S32 idx = sys_find_bind_idx(shm, pstSrcNode, pstSinkNode);
    if (idx < 0) {
        SYS_LOG_ERR("bind not found");
        pthread_rwlock_unlock(&shm->bind_lock);
        return SYS_ERR_NOT_FOUND;
    }

    shm->binds[idx].state = SYS_BIND_FREE;
    shm->bind_cnt--;

    pthread_rwlock_unlock(&shm->bind_lock);

    SYS_LOG_INFO("SYS_UnBind: [mod%d dev%d chn%d]->[mod%d dev%d chn%d]",
                 pstSrcNode->eModId, pstSrcNode->s32DevId, pstSrcNode->s32ChnId,
                 pstSinkNode->eModId, pstSinkNode->s32DevId, pstSinkNode->s32ChnId);
    return SYS_ERR_OK;
}

/* ======================== PTS ======================== */

S32 SYS_GetCurPTS(U64 *pu64CurPTS)
{
    MppSharedMem *shm = mpp_shm_get();

    if (!pu64CurPTS) {
        SYS_LOG_ERR("null output pointer");
        return SYS_ERR_INVAL;
    }
    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    sys_mutex_lock(&shm->pts_lock);
    U64 now_ns = sys_get_mono_ns();
    U64 delta_us = (now_ns - shm->base_mono_ns) / 1000;
    *pu64CurPTS = shm->base_pts_us + delta_us;
    pthread_mutex_unlock(&shm->pts_lock);

    return SYS_ERR_OK;
}

S32 SYS_InitPTSBase(U64 u64PTSBase)
{
    MppSharedMem *shm = mpp_shm_get();

    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    sys_mutex_lock(&shm->pts_lock);
    shm->base_pts_us  = u64PTSBase;
    shm->base_mono_ns = sys_get_mono_ns();
    pthread_mutex_unlock(&shm->pts_lock);

    SYS_LOG_INFO("SYS_InitPTSBase: base=%llu us", (unsigned long long)u64PTSBase);
    return SYS_ERR_OK;
}

S32 SYS_SyncPTS(U64 u64PTSBase)
{
    MppSharedMem *shm = mpp_shm_get();

    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    sys_mutex_lock(&shm->pts_lock);
    shm->base_pts_us  = u64PTSBase;
    shm->base_mono_ns = sys_get_mono_ns();
    pthread_mutex_unlock(&shm->pts_lock);

    SYS_LOG_INFO("SYS_SyncPTS: synced to %llu us", (unsigned long long)u64PTSBase);
    return SYS_ERR_OK;
}

/* ======================== Mmap ======================== */

S32 SYS_Mmap(U64 u64PhyAddr, U32 u32Size)
{
    MppSharedMem *shm = mpp_shm_get();
    if (!shm || !shm->sys_inited) return SYS_ERR_NOT_INIT;
    if (u32Size == 0) return SYS_ERR_INVAL;

    SYS_LOG_WARN("SYS_Mmap: phy=0x%llx size=%u (stub — use DMA heap via MmzAlloc)",
                 (unsigned long long)u64PhyAddr, u32Size);
    return SYS_ERR_OK;
}

S32 SYS_MmapCache(U64 u64PhyAddr, U32 u32Size)
{
    MppSharedMem *shm = mpp_shm_get();
    if (!shm || !shm->sys_inited) return SYS_ERR_NOT_INIT;
    if (u32Size == 0) return SYS_ERR_INVAL;

    SYS_LOG_WARN("SYS_MmapCache: phy=0x%llx size=%u (stub — use DMA heap via MmzAlloc)",
                 (unsigned long long)u64PhyAddr, u32Size);
    return SYS_ERR_OK;
}

S32 SYS_Munmap(VOID *pVirAddr, U32 u32Size)
{
    if (!pVirAddr) return SYS_ERR_INVAL;
    MppSharedMem *shm = mpp_shm_get();
    if (!shm || !shm->sys_inited) return SYS_ERR_NOT_INIT;

    SYS_LOG_WARN("SYS_Munmap: vir=%p size=%u (stub)", pVirAddr, u32Size);
    return SYS_ERR_OK;
}

S32 SYS_MflushCache(U64 u64PhyAddr, VOID *pVirAddr, U32 u32Size)
{
    if (!pVirAddr) return SYS_ERR_INVAL;
    MppSharedMem *shm = mpp_shm_get();
    if (!shm || !shm->sys_inited) return SYS_ERR_NOT_INIT;

    (void)u64PhyAddr;
    (void)u32Size;
    return SYS_ERR_OK;
}

/* ======================== MMZ Alloc (DMA Heap) ======================== */

S32 SYS_MmzAlloc(U64 *pu64PhyAddr, VOID **ppVirAddr,
                  const CHAR *sMb, const CHAR *sZone, U32 u32Len)
{
    MppSharedMem *shm = mpp_shm_get();

    if (!pu64PhyAddr || !ppVirAddr || u32Len == 0) {
        SYS_LOG_ERR("invalid params");
        return SYS_ERR_INVAL;
    }
    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    int fd = -1;
    U64 phy = 0;
    void *vir = NULL;

    S32 ret = dma_alloc_buf(u32Len, &fd, &phy, &vir);
    if (ret != 0) {
        SYS_LOG_ERR("DMA heap alloc failed, len=%u", u32Len);
        return SYS_ERR_NOMEM;
    }

    sys_mutex_lock(&shm->map_lock);
    SysMapRecord *rec = sys_alloc_map_record(shm);
    if (!rec) {
        SYS_LOG_ERR("map record table full");
        pthread_mutex_unlock(&shm->map_lock);
        dma_free_buf(fd, vir, u32Len);
        return SYS_ERR_FULL;
    }
    rec->used      = MPP_TRUE;
    rec->type      = SYS_MAP_MMZ;
    rec->phy_addr  = phy;
    rec->vir_addr  = vir;
    rec->size      = u32Len;
    rec->dma_fd    = fd;
    rec->owner_pid = getpid();
    pthread_mutex_unlock(&shm->map_lock);

    *pu64PhyAddr = phy;
    *ppVirAddr   = vir;

    SYS_LOG_INFO("SYS_MmzAlloc: mb=%s zone=%s len=%u phy=0x%llx vir=%p fd=%d",
                 sMb ? sMb : "(null)", sZone ? sZone : "(default)",
                 u32Len, (unsigned long long)phy, vir, fd);
    return SYS_ERR_OK;
}

S32 SYS_MmzAlloc_Cached(U64 *pu64PhyAddr, VOID **ppVirAddr,
                         const CHAR *sMb, const CHAR *sZone, U32 u32Len)
{
    MppSharedMem *shm = mpp_shm_get();

    if (!pu64PhyAddr || !ppVirAddr || u32Len == 0) {
        SYS_LOG_ERR("invalid params");
        return SYS_ERR_INVAL;
    }
    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    int fd = -1;
    U64 phy = 0;
    void *vir = NULL;

    S32 ret = dma_alloc_buf(u32Len, &fd, &phy, &vir);
    if (ret != 0) {
        SYS_LOG_ERR("DMA heap alloc failed, len=%u", u32Len);
        return SYS_ERR_NOMEM;
    }

    sys_mutex_lock(&shm->map_lock);
    SysMapRecord *rec = sys_alloc_map_record(shm);
    if (!rec) {
        SYS_LOG_ERR("map record table full");
        pthread_mutex_unlock(&shm->map_lock);
        dma_free_buf(fd, vir, u32Len);
        return SYS_ERR_FULL;
    }
    rec->used      = MPP_TRUE;
    rec->type      = SYS_MAP_MMZ_CACHED;
    rec->phy_addr  = phy;
    rec->vir_addr  = vir;
    rec->size      = u32Len;
    rec->dma_fd    = fd;
    rec->owner_pid = getpid();
    pthread_mutex_unlock(&shm->map_lock);

    *pu64PhyAddr = phy;
    *ppVirAddr   = vir;

    SYS_LOG_INFO("SYS_MmzAlloc_Cached: mb=%s zone=%s len=%u phy=0x%llx vir=%p fd=%d",
                 sMb ? sMb : "(null)", sZone ? sZone : "(default)",
                 u32Len, (unsigned long long)phy, vir, fd);
    return SYS_ERR_OK;
}

S32 SYS_MmzFlushCache(U64 u64PhyAddr, VOID *pVirAddr, U32 u32Size)
{
    MppSharedMem *shm = mpp_shm_get();

    if (!pVirAddr) {
        SYS_LOG_ERR("null vir addr");
        return SYS_ERR_INVAL;
    }
    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    /* Find the dma-buf fd for this mapping */
    sys_mutex_lock(&shm->map_lock);
    SysMapRecord *rec = sys_find_map_by_vir(shm, pVirAddr);
    if (!rec || rec->dma_fd < 0) {
        pthread_mutex_unlock(&shm->map_lock);
        /* no dma-buf fd — silently succeed (might be non-DMA memory) */
        (void)u64PhyAddr;
        (void)u32Size;
        return SYS_ERR_OK;
    }
    int fd = rec->dma_fd;
    pthread_mutex_unlock(&shm->map_lock);

    /* Real cache flush via DMA_BUF_IOCTL_SYNC */
    S32 ret = dma_sync_buf(fd, DMA_SYNC_RW | DMA_SYNC_END);
    if (ret != 0) {
        SYS_LOG_ERR("dma_sync_buf failed for fd=%d", fd);
        return ret;
    }

    return SYS_ERR_OK;
}

S32 SYS_MmzFree(U64 u64PhyAddr, VOID *pVirAddr)
{
    MppSharedMem *shm = mpp_shm_get();

    if (!pVirAddr) {
        SYS_LOG_ERR("null vir addr");
        return SYS_ERR_INVAL;
    }
    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    sys_mutex_lock(&shm->map_lock);
    SysMapRecord *rec = sys_find_map_by_vir(shm, pVirAddr);
    if (!rec) {
        SYS_LOG_ERR("map record not found for vir=%p", pVirAddr);
        pthread_mutex_unlock(&shm->map_lock);
        return SYS_ERR_NOT_FOUND;
    }

    int fd = rec->dma_fd;
    void *vir = rec->vir_addr;
    U32 size = rec->size;
    rec->used = MPP_FALSE;
    pthread_mutex_unlock(&shm->map_lock);

    dma_free_buf(fd, vir, size);

    SYS_LOG_INFO("SYS_MmzFree: phy=0x%llx vir=%p fd=%d",
                 (unsigned long long)u64PhyAddr, pVirAddr, fd);
    return SYS_ERR_OK;
}

/* ======================== SendFrame / RecvFrame ======================== */

static S32 sys_build_frame_info_from_buffer(const MppNode *pstSrc, UL ulBuff,
                                            VideoFrameInfo *pstFrameInfo)
{
    U64 u64CurPts = 0;
    S32 s32Ret = 0;

    if (!pstSrc || ulBuff == 0 || !pstFrameInfo)
        return SYS_ERR_INVAL;

    memset(pstFrameInfo, 0, sizeof(*pstFrameInfo));
    s32Ret = VB_GetFrameInfo(ulBuff, pstFrameInfo);
    if (s32Ret != 0) {
        SYS_LOG_ERR("SendFrame: VB_GetFrameInfo failed, buf=%lu ret=%d", ulBuff, s32Ret);
        return s32Ret;
    }

    pstFrameInfo->ulBufferId = ulBuff;
    if (pstFrameInfo->eModId == 0)
        pstFrameInfo->eModId = pstSrc->eModId;

    /* Commercial data path behavior: if producer did not stamp PTS, stamp it
     * at SYS ingress so downstream modules always receive a usable timestamp. */
    if (pstFrameInfo->stVFrame.u64PTS == 0 && SYS_GetCurPTS(&u64CurPts) == SYS_ERR_OK) {
        pstFrameInfo->stVFrame.u64PTS = u64CurPts;
        (void)VB_SetBufferPTS(ulBuff, u64CurPts);
    }

    (void)VB_UpdateBufferFrameInfo(ulBuff, pstFrameInfo);
    return SYS_ERR_OK;
}

S32 SYS_SendFrame(const MppNode *pstSrc, UL ulBuff)
{
    MppSharedMem *shm = mpp_shm_get();
    VideoFrameInfo stFrameInfo;
    S32 s32FrameRet;

    if (!pstSrc || ulBuff == 0) {
        SYS_LOG_ERR("invalid params");
        return SYS_ERR_INVAL;
    }
    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    s32FrameRet = sys_build_frame_info_from_buffer(pstSrc, ulBuff, &stFrameInfo);
    if (s32FrameRet != SYS_ERR_OK)
        return s32FrameRet;

    /* Find all binds where src matches — fan-out to all sinks */
    pthread_rwlock_rdlock(&shm->bind_lock);

    BOOL found = MPP_FALSE;
    for (U32 i = 0; i < MPP_MAX_BIND; i++) {
        SysBindEntry *e = &shm->binds[i];
        if (e->state != SYS_BIND_ACTIVE || !sys_node_equal(&e->src, pstSrc))
            continue;

        found = MPP_TRUE;
        MppChanQueue *q = &shm->queues[i];

        /* Add a reference for the sink — zero-copy semantics:
         * sender keeps its ref, each sink gets an additional ref. */
        extern S32 VB_RefAdd(UL ulBufHandle);
        S32 ref_ret = VB_RefAdd(ulBuff);
        if (ref_ret != 0) {
            SYS_LOG_ERR("SendFrame: VB_RefAdd failed for queue[%u]", i);
            continue;
        }

        sys_mutex_lock(&q->lock);
        if (q->count >= MPP_CHAN_DEPTH) {
            SYS_LOG_WARN("SendFrame: queue[%u] full, dropping frame", i);
            pthread_mutex_unlock(&q->lock);
            extern S32 VB_RefSub(UL ulBufHandle);
            VB_RefSub(ulBuff);  /* undo the ref we just added */
            continue;
        }

        q->entries[q->tail].frame_info = stFrameInfo;
        q->tail = (q->tail + 1) % MPP_CHAN_DEPTH;
        q->count++;

        pthread_cond_signal(&q->not_empty);
        pthread_mutex_unlock(&q->lock);
    }

    pthread_rwlock_unlock(&shm->bind_lock);

    if (!found) {
        // SYS_LOG_ERR("SendFrame: no bind found for src mod%d dev%d chn%d",
        //              pstSrc->eModId, pstSrc->s32DevId, pstSrc->s32ChnId);
        return SYS_ERR_NOT_FOUND;
    }

    return SYS_ERR_OK;
}

S32 SYS_RecvFrame(const MppNode *pstSink, UL *pulBuff, U32 u32TimeoutMs)
{
    MppSharedMem *shm = mpp_shm_get();
    VideoFrameInfo stFrameInfo;

    if (!pstSink || !pulBuff) {
        SYS_LOG_ERR("invalid params");
        return SYS_ERR_INVAL;
    }
    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    /* Find the bind where sink matches */
    pthread_rwlock_rdlock(&shm->bind_lock);

    S32 bind_idx = sys_find_sink_bind_idx(shm, pstSink);
    pthread_rwlock_unlock(&shm->bind_lock);

    if (bind_idx < 0) {
        // SYS_LOG_ERR("RecvFrame: no bind found for sink mod%d dev%d chn%d",
        //              pstSink->eModId, pstSink->s32DevId, pstSink->s32ChnId);
        return SYS_ERR_NOT_FOUND;
    }

    MppChanQueue *q = &shm->queues[bind_idx];

    sys_mutex_lock(&q->lock);

    if (q->count == 0 && u32TimeoutMs > 0) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += u32TimeoutMs / 1000;
        ts.tv_nsec += (u32TimeoutMs % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }

        while (q->count == 0) {
            int r = pthread_cond_timedwait(&q->not_empty, &q->lock, &ts);
            if (r == ETIMEDOUT) break;
            if (r == EOWNERDEAD) {
                pthread_mutex_consistent(&q->lock);
                break;
            }
        }
    }

    if (q->count == 0) {
        pthread_mutex_unlock(&q->lock);
        return SYS_ERR_TIMEOUT;
    }

    stFrameInfo = q->entries[q->head].frame_info;
    q->head = (q->head + 1) % MPP_CHAN_DEPTH;
    q->count--;

    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);

    *pulBuff = stFrameInfo.ulBufferId;
    if (*pulBuff != 0) {
        (void)VB_UpdateBufferFrameInfo(*pulBuff, &stFrameInfo);
    }

    return SYS_ERR_OK;
}

S32 SYS_SendStream(const MppNode *pstSrc, const StreamBufferInfo *pstStream)
{
    MppSharedMem *shm = mpp_shm_get();
    BOOL found = MPP_FALSE;

    if (!pstSrc || !pstStream || !pstStream->pu8Addr || pstStream->u32Size == 0) {
        SYS_LOG_ERR("invalid params");
        return SYS_ERR_INVAL;
    }
    if (pstStream->u32Size > MPP_STREAM_MAX_PAYLOAD) {
        SYS_LOG_ERR("stream payload too large: %u", pstStream->u32Size);
        return SYS_ERR_FULL;
    }
    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    pthread_rwlock_rdlock(&shm->bind_lock);
    for (U32 i = 0; i < MPP_MAX_BIND; i++) {
        SysBindEntry *e = &shm->binds[i];
        MppStreamQueue *q;
        MppStreamQueueEntry *entry;
        int dma_fd = -1;
        U64 dma_phy = 0;
        void *dma_vir = NULL;

        if (e->state != SYS_BIND_ACTIVE || !sys_node_equal(&e->src, pstSrc)) {
            continue;
        }

        q = &shm->stream_queues[i];
        found = MPP_TRUE;
        sys_mutex_lock(&q->lock);
        if (q->count >= MPP_STREAM_CHAN_DEPTH) {
            SYS_LOG_WARN("stream queue full, bind=%u", i);
            pthread_mutex_unlock(&q->lock);
            continue;
        }

        /* Allocate DMA buffer for this stream packet (outside shm) */
        if (dma_alloc_buf(pstStream->u32Size, &dma_fd, &dma_phy, &dma_vir) != 0) {
            SYS_LOG_ERR("DMA alloc failed for stream, size=%u", pstStream->u32Size);
            pthread_mutex_unlock(&q->lock);
            continue;
        }

        /* Copy payload into DMA buffer */
        memcpy(dma_vir, pstStream->pu8Addr, pstStream->u32Size);

        /* Sync DMA buffer for consumer to read */
        dma_sync_buf(dma_fd, DMA_SYNC_WRITE | DMA_SYNC_END);

        /* Unmap producer's virtual mapping — consumer will re-mmap via fd */
        munmap(dma_vir, pstStream->u32Size);

        /* Store metadata in shared queue entry */
        entry = &q->entries[q->tail];
        memset(entry, 0, sizeof(*entry));
        entry->used      = MPP_TRUE;
        entry->info       = *pstStream;
        entry->info.pu8Addr = NULL;  /* no direct pointer in shm */
        entry->dma_fd     = dma_fd;
        entry->dma_phy    = dma_phy;
        entry->dma_size   = pstStream->u32Size;
        entry->owner_pid  = getpid();

        q->tail = (q->tail + 1) % MPP_STREAM_CHAN_DEPTH;
        q->count++;
        pthread_cond_signal(&q->not_empty);
        pthread_mutex_unlock(&q->lock);
    }
    pthread_rwlock_unlock(&shm->bind_lock);

    return found ? SYS_ERR_OK : SYS_ERR_NOT_FOUND;
}

S32 SYS_RecvStream(const MppNode *pstSink, StreamBufferInfo *pstStream, U32 u32TimeoutMs)
{
    MppSharedMem *shm = mpp_shm_get();
    S32 bind_idx;
    MppStreamQueue *q;
    MppStreamQueueEntry *entry;
    U8 *dst;
    void *dma_vir = NULL;

    if (!pstSink || !pstStream || !pstStream->pu8Addr || pstStream->u32Size == 0) {
        SYS_LOG_ERR("invalid params");
        return SYS_ERR_INVAL;
    }
    if (!shm || !shm->sys_inited) {
        SYS_LOG_ERR("SYS not initialized");
        return SYS_ERR_NOT_INIT;
    }

    pthread_rwlock_rdlock(&shm->bind_lock);
    bind_idx = sys_find_sink_bind_idx(shm, pstSink);
    pthread_rwlock_unlock(&shm->bind_lock);
    if (bind_idx < 0) {
        return SYS_ERR_NOT_FOUND;
    }

    q = &shm->stream_queues[bind_idx];
    dst = (U8 *)pstStream->pu8Addr;

    sys_mutex_lock(&q->lock);
    if (q->count == 0 && u32TimeoutMs > 0) {
        struct timespec ts;

        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += u32TimeoutMs / 1000;
        ts.tv_nsec += (u32TimeoutMs % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        while (q->count == 0) {
            int r = pthread_cond_timedwait(&q->not_empty, &q->lock, &ts);
            if (r == ETIMEDOUT) {
                break;
            }
            if (r == EOWNERDEAD) {
                pthread_mutex_consistent(&q->lock);
                break;
            }
        }
    }

    if (q->count == 0) {
        pthread_mutex_unlock(&q->lock);
        return SYS_ERR_TIMEOUT;
    }

    entry = &q->entries[q->head];
    if (pstStream->u32Size < entry->info.u32Size) {
        pthread_mutex_unlock(&q->lock);
        return SYS_ERR_FULL;
    }

    /* Map the DMA buffer into this process to read the payload */
    dma_vir = mmap(NULL, entry->dma_size, PROT_READ, MAP_SHARED,
                   entry->dma_fd, 0);
    if (dma_vir == MAP_FAILED) {
        SYS_LOG_ERR("mmap DMA fd=%d failed: %s", entry->dma_fd, strerror(errno));
        pthread_mutex_unlock(&q->lock);
        return SYS_ERR_NOMEM;
    }

    /* Sync for CPU read */
    dma_sync_buf(entry->dma_fd, DMA_SYNC_READ | DMA_SYNC_START);

    /* Copy payload to caller's buffer */
    memcpy(dst, dma_vir, entry->info.u32Size);

    /* Unmap and close the DMA buffer — consumer is done */
    munmap(dma_vir, entry->dma_size);
    close(entry->dma_fd);

    /* Return metadata to caller */
    *pstStream = entry->info;
    pstStream->pu8Addr = dst;

    /* Clear entry and advance queue */
    memset(entry, 0, sizeof(*entry));
    entry->dma_fd = -1;
    q->head = (q->head + 1) % MPP_STREAM_CHAN_DEPTH;
    q->count--;
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->lock);
    return SYS_ERR_OK;
}

/* ======================== Debug Dump ======================== */

VOID SYS_DumpStatus(VOID)
{
    MppSharedMem *shm = mpp_shm_get();
    U64 cur_pts = 0;

    printf("\n========== SYS Status ==========\n");
    if (!shm) {
        printf("  (shared memory not attached)\n");
        printf("================================\n\n");
        return;
    }
    printf("  inited    : %s\n", shm->sys_inited ? "YES" : "NO");
    printf("  proc_ref  : %d\n", atomic_load(&shm->proc_ref));

    if (!shm->sys_inited) {
        printf("================================\n\n");
        return;
    }

    SYS_GetCurPTS(&cur_pts);
    printf("  PTS base  : %llu us\n", (unsigned long long)shm->base_pts_us);
    printf("  PTS cur   : %llu us\n", (unsigned long long)cur_pts);

    printf("  Binds (%u):\n", shm->bind_cnt);
    pthread_rwlock_rdlock(&shm->bind_lock);
    for (U32 i = 0; i < MPP_MAX_BIND; i++) {
        SysBindEntry *e = &shm->binds[i];
        if (e->state == SYS_BIND_ACTIVE) {
            printf("    [%u] mod%d.dev%d.chn%d -> mod%d.dev%d.chn%d\n",
                   i, e->src.eModId, e->src.s32DevId, e->src.s32ChnId,
                   e->sink.eModId, e->sink.s32DevId, e->sink.s32ChnId);
        }
    }
    pthread_rwlock_unlock(&shm->bind_lock);

    printf("  Map records:\n");
    sys_mutex_lock(&shm->map_lock);
    {
        static const char *type_str[] = { "MMAP", "MMAP_CACHE", "MMZ", "MMZ_CACHED" };
        U32 cnt = 0;
        for (U32 i = 0; i < MPP_MAX_MAP; i++) {
            SysMapRecord *r = &shm->maps[i];
            if (r->used) {
                printf("    [%u] %s phy=0x%llx vir=%p size=%u fd=%d pid=%d\n",
                       i, type_str[r->type], (unsigned long long)r->phy_addr,
                       r->vir_addr, r->size, r->dma_fd, r->owner_pid);
                cnt++;
            }
        }
        if (cnt == 0)
            printf("    (none)\n");
    }
    pthread_mutex_unlock(&shm->map_lock);
    printf("================================\n\n");
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
