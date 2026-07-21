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
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <errno.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdbool.h>
#include <pthread.h>

#include "sys/mpp_shm.h"

#define SHM_LOG_ERR(fmt, ...) fprintf(stderr, "[SHM][ERR] %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define SHM_LOG_WARN(fmt, ...) fprintf(stderr, "[SHM][WARN] %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define SHM_LOG_INFO(fmt, ...) fprintf(stdout, "[SHM][INFO] " fmt "\n", ##__VA_ARGS__)
#define MPP_SHM_INIT_LOCK_PATH "/tmp/mpp_shm_init.lock"

static MppSharedMem *g_shm = NULL;
static int g_shm_fd = -1;

static int mpp_shm_init_lock(void) {
    int fd = open(MPP_SHM_INIT_LOCK_PATH, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        SHM_LOG_ERR("open init lock failed: %s", strerror(errno));
        return -1;
    }
    if (flock(fd, LOCK_EX) != 0) {
        SHM_LOG_ERR("lock init lock failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static void mpp_shm_init_unlock(int fd) {
    if (fd >= 0) {
        flock(fd, LOCK_UN);
        close(fd);
    }
}

/* ======================== Init helpers ======================== */

static S32 shm_init_mutex(pthread_mutex_t *mtx) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
#ifdef PTHREAD_MUTEX_ROBUST
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
#elif defined(PTHREAD_MUTEX_ROBUST_NP)
    pthread_mutexattr_setrobust_np(&attr, PTHREAD_MUTEX_ROBUST_NP);
#endif
    int r = pthread_mutex_init(mtx, &attr);
    pthread_mutexattr_destroy(&attr);
    return r == 0 ? 0 : -1;
}

static S32 shm_init_cond(pthread_cond_t *cnd) {
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    int r = pthread_cond_init(cnd, &attr);
    pthread_condattr_destroy(&attr);
    return r == 0 ? 0 : -1;
}

static S32 shm_init_rwlock(pthread_rwlock_t *rw) {
    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);
    pthread_rwlockattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    int r = pthread_rwlock_init(rw, &attr);
    pthread_rwlockattr_destroy(&attr);
    return r == 0 ? 0 : -1;
}

static S32 shm_init_pool_locks(VbPoolShm *pool) {
    if (shm_init_mutex(&pool->lock) != 0)
        return -1;
    if (shm_init_cond(&pool->cond) != 0)
        return -1;
    return 0;
}

static S32 shm_init_queue(MppChanQueue *q) {
    if (shm_init_mutex(&q->lock) != 0)
        return -1;
    if (shm_init_cond(&q->not_empty) != 0)
        return -1;
    if (shm_init_cond(&q->not_full) != 0)
        return -1;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    return 0;
}

static bool mpp_shm_file_in_use(void) {
    pid_t self_pid = getpid();
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir)
        return false;

    struct dirent *proc_ent;
    while ((proc_ent = readdir(proc_dir)) != NULL) {
        if (!isdigit((unsigned char)proc_ent->d_name[0]))
            continue;

        pid_t pid = (pid_t)atoi(proc_ent->d_name);
        if (pid == self_pid)
            continue;

        char fd_dir_path[PATH_MAX];
        snprintf(fd_dir_path, sizeof(fd_dir_path), "/proc/%s/fd", proc_ent->d_name);
        DIR *fd_dir = opendir(fd_dir_path);
        if (!fd_dir)
            continue;

        struct dirent *fd_ent;
        while ((fd_ent = readdir(fd_dir)) != NULL) {
            if (fd_ent->d_name[0] == '.')
                continue;

            char link_path[PATH_MAX];
            size_t dir_len = strlen(fd_dir_path);
            size_t name_len = strlen(fd_ent->d_name);
            if (dir_len + 1 + name_len >= sizeof(link_path))
                continue;
            memcpy(link_path, fd_dir_path, dir_len);
            link_path[dir_len] = '/';
            memcpy(link_path + dir_len + 1, fd_ent->d_name, name_len + 1);

            char target[PATH_MAX];
            ssize_t len = readlink(link_path, target, sizeof(target) - 1);
            if (len < 0)
                continue;
            target[len] = '\0';

            if (strcmp(target, "/dev/shm/mpp_ctrl") == 0 || strcmp(target, "/dev/shm/mpp_ctrl (deleted)") == 0) {
                closedir(fd_dir);
                closedir(proc_dir);
                return true;
            }
        }
        closedir(fd_dir);
    }

    closedir(proc_dir);
    return false;
}

static S32 shm_init_stream_queue(MppStreamQueue *q) {
    if (shm_init_mutex(&q->lock) != 0)
        return -1;
    if (shm_init_cond(&q->not_empty) != 0)
        return -1;
    if (shm_init_cond(&q->not_full) != 0)
        return -1;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    memset(q->entries, 0, sizeof(q->entries));
    return 0;
}

static S32 shm_init_all(MppSharedMem *shm) {
    unsigned long long export_generation = 0;
    ssize_t random_size;

    memset(shm, 0, sizeof(*shm));

    shm->magic = MPP_SHM_MAGIC;
    shm->version = MPP_SHM_VERSION;
    atomic_store(&shm->proc_ref, 1);
    do {
        random_size = getrandom(&export_generation, sizeof(export_generation), 0);
    } while (random_size < 0 && errno == EINTR);
    if (random_size != (ssize_t)sizeof(export_generation)) {
        SHM_LOG_ERR("getrandom export generation failed: %s", strerror(errno));
        return -1;
    }
    if (export_generation == 0)
        export_generation = 1;

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
        shm->pools[i].next_export_generation = export_generation;
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

S32 mpp_shm_init(void) {
    if (g_shm) {
        /* already attached in this process */
        atomic_fetch_add(&g_shm->proc_ref, 1);
        return 0;
    }

    int created = 0;
    int lock_fd = mpp_shm_init_lock();
    if (lock_fd < 0) {
        return -1;
    }
    int fd = shm_open(MPP_SHM_NAME, O_RDWR, 0666);
    if (fd < 0) {
        /* first process — create */
        fd = shm_open(MPP_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd < 0) {
            /* race: another process created between our open and creat */
            fd = shm_open(MPP_SHM_NAME, O_RDWR, 0666);
            if (fd < 0) {
                SHM_LOG_ERR("shm_open failed: %s", strerror(errno));
                mpp_shm_init_unlock(lock_fd);
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
            mpp_shm_init_unlock(lock_fd);
            return -1;
        }
    }

    MppSharedMem *shm = (MppSharedMem *)mmap(NULL, sizeof(MppSharedMem), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) {
        SHM_LOG_ERR("mmap failed: %s", strerror(errno));
        close(fd);
        if (created)
            shm_unlink(MPP_SHM_NAME);
        mpp_shm_init_unlock(lock_fd);
        return -1;
    }

    if (created) {
        if (shm_init_all(shm) != 0) {
            munmap(shm, sizeof(MppSharedMem));
            close(fd);
            shm_unlink(MPP_SHM_NAME);
            mpp_shm_init_unlock(lock_fd);
            return -1;
        }
    } else {
        bool stale = false;

        /* Reject layout mismatches while any process still uses the segment;
         * only an orphaned segment may be reinitialized to this version. */
        if (shm->magic != MPP_SHM_MAGIC || shm->version != MPP_SHM_VERSION) {
            SHM_LOG_WARN("bad shm header detected, checking if orphaned");
            if (!mpp_shm_file_in_use()) {
                stale = true;
            } else {
                SHM_LOG_ERR("bad shared memory header and active users present");
                munmap(shm, sizeof(MppSharedMem));
                close(fd);
                mpp_shm_init_unlock(lock_fd);
                return -1;
            }
        } else if (!mpp_shm_file_in_use()) {
            stale = true;
        }

        if (stale) {
            SHM_LOG_WARN("orphaned shared memory detected, reinitializing");
            if (shm_init_all(shm) != 0) {
                munmap(shm, sizeof(MppSharedMem));
                close(fd);
                mpp_shm_init_unlock(lock_fd);
                return -1;
            }
        } else {
            atomic_fetch_add(&shm->proc_ref, 1);
        }
    }

    g_shm = shm;
    g_shm_fd = fd;

    SHM_LOG_INFO("process attached, proc_ref=%d", atomic_load(&shm->proc_ref));
    mpp_shm_init_unlock(lock_fd);
    return 0;
}

S32 mpp_shm_detach(void (*on_last)(MppSharedMem *shm)) {
    if (!g_shm)
        return -1;

    /*
     * Serialize detach against init under the same cross-process file lock.
     * Without this, a concurrent mpp_shm_init() could shm_open() the segment
     * and atomic_fetch_add(proc_ref) after we read ref==last but before we
     * shm_unlink(): the newcomer would then hold a mapping to a segment whose
     * name has just been removed, while the next process shm_open()s and
     * O_CREATs a brand-new segment — leaving the two processes attached to
     * different shared memories. Holding the init lock across the
     * fetch_sub + unlink makes the "am I the last process" decision atomic
     * with respect to init.
     */
    int lock_fd = mpp_shm_init_lock();
    if (lock_fd < 0) {
        SHM_LOG_ERR("detach aborted: init lock unavailable");
        return -1;
    }

    int ref = atomic_fetch_sub(&g_shm->proc_ref, 1);
    int is_last = (ref <= 1);

    /*
     * Last-process global cleanup (e.g. clearing shared bind/map tables) must
     * run while the segment is still mapped AND while we still hold the init
     * lock, so that no other process can attach in between and observe a
     * half-torn-down state. Because is_last comes from the atomic decrement
     * under the lock, exactly one process ever sees is_last==1.
     */
    if (is_last && on_last) {
        on_last(g_shm);
    }

    munmap(g_shm, sizeof(MppSharedMem));
    close(g_shm_fd);

    if (is_last) {
        /* last process — unlink */
        shm_unlink(MPP_SHM_NAME);
        SHM_LOG_INFO("last process detached, shm unlinked");
    } else {
        SHM_LOG_INFO("process detached, proc_ref=%d", ref - 1);
    }

    g_shm = NULL;
    g_shm_fd = -1;

    mpp_shm_init_unlock(lock_fd);
    return 0;
}

MppSharedMem *mpp_shm_get(void) {
    return g_shm;
}
