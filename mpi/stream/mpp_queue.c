/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      : mpp_queue.c
 * @Brief     : Thread-safe bounded queue implementation.
 *------------------------------------------------------------------------------
 */

#include "stream/mpp_queue.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

/* Convert relative timeout (ms) to absolute timespec */
static void timeout_to_abstime(S32 timeoutMs, struct timespec *ts) {
    struct timeval tv;
    gettimeofday(&tv, NULL);

    ts->tv_sec = tv.tv_sec + (timeoutMs / 1000);
    ts->tv_nsec = (tv.tv_usec * 1000) + ((timeoutMs % 1000) * 1000000);

    if (ts->tv_nsec >= 1000000000) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000;
    }
}

S32 MppQueue_Init(MppQueue *q, U32 capacity, BOOL dropOldest) {
    if (!q || capacity == 0) {
        return MPP_QUEUE_ERROR;
    }

    memset(q, 0, sizeof(MppQueue));

    q->items = (void **)calloc(capacity, sizeof(void *));
    if (!q->items) {
        return MPP_QUEUE_ERROR;
    }

    q->capacity = capacity;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->dropOldest = dropOldest;

    if (pthread_mutex_init(&q->lock, NULL) != 0) {
        free(q->items);
        q->items = NULL;
        return MPP_QUEUE_ERROR;
    }

    if (pthread_cond_init(&q->notEmpty, NULL) != 0) {
        pthread_mutex_destroy(&q->lock);
        free(q->items);
        q->items = NULL;
        return MPP_QUEUE_ERROR;
    }

    if (pthread_cond_init(&q->notFull, NULL) != 0) {
        pthread_cond_destroy(&q->notEmpty);
        pthread_mutex_destroy(&q->lock);
        free(q->items);
        q->items = NULL;
        return MPP_QUEUE_ERROR;
    }

    q->pushCount = 0;
    q->popCount = 0;
    q->dropCount = 0;
    q->dropCallback = NULL;
    q->dropUserData = NULL;
    q->initialized = MPP_TRUE;

    return MPP_QUEUE_OK;
}

void MppQueue_Destroy(MppQueue *q) {
    if (!q || !q->initialized) {
        return;
    }

    pthread_mutex_lock(&q->lock);
    q->initialized = MPP_FALSE;
    pthread_cond_broadcast(&q->notEmpty);
    pthread_cond_broadcast(&q->notFull);
    pthread_mutex_unlock(&q->lock);

    pthread_cond_destroy(&q->notFull);
    pthread_cond_destroy(&q->notEmpty);
    pthread_mutex_destroy(&q->lock);

    if (q->items) {
        free(q->items);
        q->items = NULL;
    }
}

void MppQueue_SetDropCallback(MppQueue *q, MppQueueDropCallback callback, void *userData) {
    if (!q)
        return;

    pthread_mutex_lock(&q->lock);
    q->dropCallback = callback;
    q->dropUserData = userData;
    pthread_mutex_unlock(&q->lock);
}

S32 MppQueue_Push(MppQueue *q, void *item, S32 timeoutMs) {
    if (!q || !q->initialized) {
        return MPP_QUEUE_ERROR;
    }

    pthread_mutex_lock(&q->lock);

    /* Check if queue is full */
    if (q->count >= q->capacity) {
        if (q->dropOldest) {
            /* Drop oldest item */
            void *droppedItem = q->items[q->head];
            q->head = (q->head + 1) % q->capacity;
            q->count--;
            q->dropCount++;

            /* Call drop callback outside lock if possible, but for safety do it here */
            if (q->dropCallback && droppedItem) {
                /* Note: callback called with lock held - keep it fast! */
                q->dropCallback(droppedItem, q->dropUserData);
            }
        } else {
            /* Wait for space */
            if (timeoutMs == 0) {
                /* Non-blocking: return immediately */
                pthread_mutex_unlock(&q->lock);
                return MPP_QUEUE_TIMEOUT;
            } else if (timeoutMs < 0) {
                /* Wait forever */
                while (q->count >= q->capacity && q->initialized) {
                    pthread_cond_wait(&q->notFull, &q->lock);
                }
            } else {
                /* Wait with timeout */
                struct timespec ts;
                timeout_to_abstime(timeoutMs, &ts);

                while (q->count >= q->capacity && q->initialized) {
                    int ret = pthread_cond_timedwait(&q->notFull, &q->lock, &ts);
                    if (ret == ETIMEDOUT) {
                        pthread_mutex_unlock(&q->lock);
                        return MPP_QUEUE_TIMEOUT;
                    }
                }
            }

            if (!q->initialized) {
                pthread_mutex_unlock(&q->lock);
                return MPP_QUEUE_CLOSED;
            }
        }
    }

    /* Push item */
    q->items[q->tail] = item;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    q->pushCount++;

    /* Signal waiters */
    pthread_cond_signal(&q->notEmpty);

    BOOL dropped = (q->dropOldest && q->dropCount > 0);
    pthread_mutex_unlock(&q->lock);

    return dropped ? MPP_QUEUE_DROPPED : MPP_QUEUE_OK;
}

S32 MppQueue_Pop(MppQueue *q, void **pItem, S32 timeoutMs) {
    if (!q || !pItem || !q->initialized) {
        return MPP_QUEUE_ERROR;
    }

    pthread_mutex_lock(&q->lock);

    /* Wait for item */
    if (q->count == 0) {
        if (timeoutMs == 0) {
            /* Non-blocking: return immediately */
            pthread_mutex_unlock(&q->lock);
            return MPP_QUEUE_TIMEOUT;
        } else if (timeoutMs < 0) {
            /* Wait forever */
            while (q->count == 0 && q->initialized) {
                pthread_cond_wait(&q->notEmpty, &q->lock);
            }
        } else {
            /* Wait with timeout */
            struct timespec ts;
            timeout_to_abstime(timeoutMs, &ts);

            while (q->count == 0 && q->initialized) {
                int ret = pthread_cond_timedwait(&q->notEmpty, &q->lock, &ts);
                if (ret == ETIMEDOUT) {
                    pthread_mutex_unlock(&q->lock);
                    return MPP_QUEUE_TIMEOUT;
                }
            }
        }

        if (!q->initialized) {
            pthread_mutex_unlock(&q->lock);
            return MPP_QUEUE_CLOSED;
        }

        /* Re-check after wait */
        if (q->count == 0) {
            pthread_mutex_unlock(&q->lock);
            return MPP_QUEUE_TIMEOUT;
        }
    }

    /* Pop item */
    *pItem = q->items[q->head];
    q->items[q->head] = NULL;
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    q->popCount++;

    /* Signal waiters */
    pthread_cond_signal(&q->notFull);

    pthread_mutex_unlock(&q->lock);
    return MPP_QUEUE_OK;
}

S32 MppQueue_Peek(MppQueue *q, void **pItem) {
    if (!q || !pItem || !q->initialized) {
        return MPP_QUEUE_ERROR;
    }

    pthread_mutex_lock(&q->lock);

    if (q->count == 0) {
        pthread_mutex_unlock(&q->lock);
        return MPP_QUEUE_TIMEOUT;
    }

    *pItem = q->items[q->head];

    pthread_mutex_unlock(&q->lock);
    return MPP_QUEUE_OK;
}

void MppQueue_Clear(MppQueue *q) {
    if (!q || !q->initialized) {
        return;
    }

    pthread_mutex_lock(&q->lock);

    while (q->count > 0) {
        void *item = q->items[q->head];
        q->items[q->head] = NULL;
        q->head = (q->head + 1) % q->capacity;
        q->count--;

        if (q->dropCallback && item) {
            q->dropCallback(item, q->dropUserData);
        }
        q->dropCount++;
    }

    pthread_cond_broadcast(&q->notFull);
    pthread_mutex_unlock(&q->lock);
}

U32 MppQueue_Count(MppQueue *q) {
    if (!q || !q->initialized) {
        return 0;
    }

    pthread_mutex_lock(&q->lock);
    U32 count = q->count;
    pthread_mutex_unlock(&q->lock);

    return count;
}

BOOL MppQueue_IsEmpty(MppQueue *q) { return MppQueue_Count(q) == 0; }

BOOL MppQueue_IsFull(MppQueue *q) {
    if (!q || !q->initialized) {
        return MPP_TRUE;
    }

    pthread_mutex_lock(&q->lock);
    BOOL full = (q->count >= q->capacity);
    pthread_mutex_unlock(&q->lock);

    return full;
}

void MppQueue_GetStats(MppQueue *q, U64 *pushCount, U64 *popCount, U64 *dropCount) {
    if (!q || !q->initialized) {
        if (pushCount)
            *pushCount = 0;
        if (popCount)
            *popCount = 0;
        if (dropCount)
            *dropCount = 0;
        return;
    }

    pthread_mutex_lock(&q->lock);
    if (pushCount)
        *pushCount = q->pushCount;
    if (popCount)
        *popCount = q->popCount;
    if (dropCount)
        *dropCount = q->dropCount;
    pthread_mutex_unlock(&q->lock);
}

void MppQueue_WakeAll(MppQueue *q) {
    if (!q || !q->initialized) {
        return;
    }

    pthread_mutex_lock(&q->lock);
    pthread_cond_broadcast(&q->notEmpty);
    pthread_cond_broadcast(&q->notFull);
    pthread_mutex_unlock(&q->lock);
}
