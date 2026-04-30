/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    mpp_shm.c
 * @Date      :    2026-3-26
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    POSIX shared memory control plane implementation.
 *                 First process creates and initializes; others attach.
 *                 All PTHREAD_PROCESS_SHARED primitives are set up here.
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
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

#include "sys/mpp_shm.h"

#define SHM_LOG_ERR(fmt, ...) \
    fprintf(stderr, "[SHM][ERR] %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define SHM_LOG_INFO(fmt, ...) \
    fprintf(stdout, "[SHM][INFO] " fmt "\n", ##__VA_ARGS__)

static MppSharedMem *g_shm = NULL;
static int g_shm_fd = -1;

/* ======================== Init helpers ======================== */

static S32 shm_init_mutex(pthread_mutex_t *mtx)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    int r = pthread_mutex_init(mtx, &attr);
    pthread_mutexattr_destroy(&attr);
    return r == 0 ? 0 : -1;
}

static S32 shm_init_cond(pthread_cond_t *cnd)
{
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    int r = pthread_cond_init(cnd, &attr);
    pthread_condattr_destroy(&attr);
    return r == 0 ? 0 : -1;
}

static S32 shm_init_rwlock(pthread_rwlock_t *rw)
{
    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);
    pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    int r = pthread_rwlock_init(rw, &attr);
    pthread_rwlockattr_destroy(&attr);
    return r == 0 ? 0 : -1;
}

static S32 shm_init_pool_locks(VbPoolShm *pool)
{
    if (shm_init_mutex(&pool->lock) != 0)  return -1;
    if (shm_init_cond(&pool->cond) != 0)   return -1;
    return 0;
}

static S32 shm_init_queue(MppChanQueue *q)
{
    if (shm_init_mutex(&q->lock) != 0)      return -1;
    if (shm_init_cond(&q->not_empty) != 0)  return -1;
    if (shm_init_cond(&q->not_full) != 0)   return -1;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    return 0;
}

static S32 shm_init_stream_queue(MppStreamQueue *q)
{
    if (shm_init_mutex(&q->lock) != 0)      return -1;
    if (shm_init_cond(&q->not_empty) != 0)  return -1;
    if (shm_init_cond(&q->not_full) != 0)   return -1;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    memset(q->entries, 0, sizeof(q->entries));
    return 0;
}

static S32 shm_init_all(MppSharedMem *shm)
{
    memset(shm, 0, sizeof(*shm));

    shm->magic   = MPP_SHM_MAGIC;
    shm->version = MPP_SHM_VERSION;
    atomic_store(&shm->proc_ref, 1);

    /* PTS lock */
    if (shm_init_mutex(&shm->pts_lock) != 0) {
        SHM_LOG_ERR("pts_lock init failed");
        return -1;
    }

    /* Bind rwlock */
    if (shm_init_rwlock(&shm->bind_lock) != 0) {
        SHM_LOG_ERR("bind_lock init failed");
        return -1;
    }

    /* VB rwlock */
    if (shm_init_rwlock(&shm->vb_lock) != 0) {
        SHM_LOG_ERR("vb_lock init failed");
        return -1;
    }

    /* Map lock */
    if (shm_init_mutex(&shm->map_lock) != 0) {
        SHM_LOG_ERR("map_lock init failed");
        return -1;
    }

    /* Pool locks — pre-init all slots so CreatePool doesn't need shm_init */
    for (U32 i = 0; i < MPP_MAX_POOL; i++) {
        if (shm_init_pool_locks(&shm->pools[i]) != 0) {
            SHM_LOG_ERR("pool[%u] lock init failed", i);
            return -1;
        }
        shm->pools[i].state = VB_POOL_FREE;
    }

    /* Channel queues */
    for (U32 i = 0; i < MPP_MAX_BIND; i++) {
        if (shm_init_queue(&shm->queues[i]) != 0) {
            SHM_LOG_ERR("queue[%u] init failed", i);
            return -1;
        }
    }

    /* Stream queues */
    for (U32 i = 0; i < MPP_MAX_BIND; i++) {
        if (shm_init_stream_queue(&shm->stream_queues[i]) != 0) {
            SHM_LOG_ERR("stream_queue[%u] init failed", i);
            return -1;
        }
    }

    SHM_LOG_INFO("shared memory initialized, size=%zu bytes", sizeof(MppSharedMem));
    return 0;
}

/* ======================== Public API ======================== */

S32 mpp_shm_init(void)
{
    if (g_shm) {
        /* already attached in this process */
        atomic_fetch_add(&g_shm->proc_ref, 1);
        return 0;
    }

    int created = 0;
    int fd = shm_open(MPP_SHM_NAME, O_RDWR, 0666);
    if (fd < 0) {
        /* first process — create */
        fd = shm_open(MPP_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd < 0) {
            /* race: another process created between our open and creat */
            fd = shm_open(MPP_SHM_NAME, O_RDWR, 0666);
            if (fd < 0) {
                SHM_LOG_ERR("shm_open failed: %s", strerror(errno));
                return -1;
            }
        } else {
            created = 1;
        }
    }

    if (created) {
        if (ftruncate(fd, sizeof(MppSharedMem)) != 0) {
            SHM_LOG_ERR("ftruncate failed: %s", strerror(errno));
            close(fd);
            shm_unlink(MPP_SHM_NAME);
            return -1;
        }
    }

    MppSharedMem *shm = (MppSharedMem *)mmap(NULL, sizeof(MppSharedMem),
                                              PROT_READ | PROT_WRITE,
                                              MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        SHM_LOG_ERR("mmap failed: %s", strerror(errno));
        close(fd);
        if (created) shm_unlink(MPP_SHM_NAME);
        return -1;
    }

    if (created) {
        if (shm_init_all(shm) != 0) {
            munmap(shm, sizeof(MppSharedMem));
            close(fd);
            shm_unlink(MPP_SHM_NAME);
            return -1;
        }
    } else {
        /* attach — verify magic/version and bump ref */
        if (shm->magic != MPP_SHM_MAGIC || shm->version != MPP_SHM_VERSION) {
            SHM_LOG_ERR("bad shm header magic=0x%08X version=%u (expected magic=0x%08X version=%u)",
                        shm->magic, shm->version, MPP_SHM_MAGIC, MPP_SHM_VERSION);
            munmap(shm, sizeof(MppSharedMem));
            close(fd);
            return -1;
        }
        atomic_fetch_add(&shm->proc_ref, 1);
    }

    g_shm = shm;
    g_shm_fd = fd;

    SHM_LOG_INFO("process attached, proc_ref=%d", atomic_load(&shm->proc_ref));
    return 0;
}

S32 mpp_shm_detach(void)
{
    if (!g_shm)
        return -1;

    int ref = atomic_fetch_sub(&g_shm->proc_ref, 1);

    munmap(g_shm, sizeof(MppSharedMem));
    close(g_shm_fd);

    if (ref <= 1) {
        /* last process — unlink */
        shm_unlink(MPP_SHM_NAME);
        SHM_LOG_INFO("last process detached, shm unlinked");
    } else {
        SHM_LOG_INFO("process detached, proc_ref=%d", ref - 1);
    }

    g_shm = NULL;
    g_shm_fd = -1;
    return 0;
}

MppSharedMem *mpp_shm_get(void)
{
    return g_shm;
}
