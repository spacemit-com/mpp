/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      : mpp_queue.h
 * @Brief     : Thread-safe bounded queue with backpressure control.
 *------------------------------------------------------------------------------
 */

#ifndef MPP_QUEUE_H
#define MPP_QUEUE_H

#include <pthread.h>

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Drop callback function type.
 *        Called when an item is dropped due to queue full.
 * @param item The item being dropped
 * @param userData User-provided context
 */
typedef void (*MppQueueDropCallback)(void *item, void *userData);

/**
 * @brief Thread-safe bounded queue with optional drop-oldest policy.
 */
typedef struct MppQueue {
    void **items; /* Circular buffer */
    U32 capacity; /* Maximum capacity */
    U32 head;     /* Read position (next pop) */
    U32 tail;     /* Write position (next push) */
    U32 count;    /* Current element count */

    pthread_mutex_t lock;    /* Mutex for thread safety */
    pthread_cond_t notEmpty; /* Condition: queue not empty */
    pthread_cond_t notFull;  /* Condition: queue not full */

    BOOL dropOldest;  /* If TRUE, drop oldest when full */
    BOOL initialized; /* Initialization flag */
    BOOL bClosed;     /* If TRUE, queue is closed. */
                      /* Waiters wake and return MPP_QUEUE_CLOSED; new push/pop fail immediately. */

    /* Statistics */
    U64 pushCount; /* Total successful pushes */
    U64 popCount;  /* Total successful pops */
    U64 dropCount; /* Total dropped items */

    /* Optional drop callback */
    MppQueueDropCallback dropCallback;
    void *dropUserData;
} MppQueue;

/**
 * @brief Queue operation return codes.
 */
typedef enum {
    MPP_QUEUE_OK = 0,       /* Success */
    MPP_QUEUE_TIMEOUT = -1, /* Timeout or empty/full */
    MPP_QUEUE_DROPPED = -2, /* Success but dropped old item */
    MPP_QUEUE_ERROR = -3,   /* Internal error */
    MPP_QUEUE_CLOSED = -4,  /* Queue is closed */
} MppQueueResult;

/**
 * @brief Initialize a bounded queue.
 *
 * @param q         Queue pointer (must be pre-allocated)
 * @param capacity  Maximum number of elements (must be > 0)
 * @param dropOldest If TRUE, drop oldest element when full;
 *                   If FALSE, block or timeout when full
 * @return MPP_QUEUE_OK on success, MPP_QUEUE_ERROR on failure
 */
S32 MppQueue_Init(MppQueue *q, U32 capacity, BOOL dropOldest);

/**
 * @brief Destroy a queue and free internal resources.
 *        WARNING: Does NOT free remaining items. Call MppQueue_Clear first.
 *
 * @param q Queue pointer
 */
void MppQueue_Destroy(MppQueue *q);

/**
 * @brief Set drop callback for resource cleanup.
 *
 * @param q         Queue pointer
 * @param callback  Function to call when item is dropped
 * @param userData  User context passed to callback
 */
void MppQueue_SetDropCallback(MppQueue *q, MppQueueDropCallback callback, void *userData);

/**
 * @brief Push an item to the queue.
 *
 * If queue is full:
 * - dropOldest=TRUE: Drop oldest item (call dropCallback) and push new item
 * - dropOldest=FALSE: Wait until space available or timeout
 *
 * @param q         Queue pointer
 * @param item      Item to push (ownership transferred to queue)
 * @param timeoutMs Timeout in milliseconds:
 *                  0  = non-blocking (return immediately)
 *                  >0 = wait up to timeoutMs
 *                  -1 = wait forever
 * @return MPP_QUEUE_OK      = success
 *         MPP_QUEUE_TIMEOUT = timeout (dropOldest=FALSE only)
 *         MPP_QUEUE_DROPPED = success, but dropped old item (dropOldest=TRUE)
 *         MPP_QUEUE_ERROR   = internal error
 */
S32 MppQueue_Push(MppQueue *q, void *item, S32 timeoutMs);

/**
 * @brief Pop an item from the queue.
 *
 * @param q         Queue pointer
 * @param pItem     Output: popped item (ownership transferred to caller)
 * @param timeoutMs Timeout in milliseconds:
 *                  0  = non-blocking (return immediately if empty)
 *                  >0 = wait up to timeoutMs
 *                  -1 = wait forever
 * @return MPP_QUEUE_OK      = success
 *         MPP_QUEUE_TIMEOUT = timeout or empty
 *         MPP_QUEUE_ERROR   = internal error
 */
S32 MppQueue_Pop(MppQueue *q, void **pItem, S32 timeoutMs);

/**
 * @brief Peek at the front item without removing it.
 *
 * @param q     Queue pointer
 * @param pItem Output: front item (ownership NOT transferred)
 * @return MPP_QUEUE_OK if item exists, MPP_QUEUE_TIMEOUT if empty
 */
S32 MppQueue_Peek(MppQueue *q, void **pItem);

/**
 * @brief Clear all items from the queue.
 *        Calls dropCallback for each item if set.
 *
 * @param q Queue pointer
 */
void MppQueue_Clear(MppQueue *q);

/**
 * @brief Get current element count.
 *
 * @param q Queue pointer
 * @return Current number of elements
 */
U32 MppQueue_Count(MppQueue *q);

/**
 * @brief Check if queue is empty.
 *
 * @param q Queue pointer
 * @return TRUE if empty
 */
BOOL MppQueue_IsEmpty(MppQueue *q);

/**
 * @brief Check if queue is full.
 *
 * @param q Queue pointer
 * @return TRUE if full
 */
BOOL MppQueue_IsFull(MppQueue *q);

/**
 * @brief Get queue statistics.
 *
 * @param q          Queue pointer
 * @param pushCount  Output: total pushes (can be NULL)
 * @param popCount   Output: total pops (can be NULL)
 * @param dropCount  Output: total drops (can be NULL)
 */
void MppQueue_GetStats(MppQueue *q, U64 *pushCount, U64 *popCount, U64 *dropCount);

/**
 * @brief Wake up all threads waiting on this queue.
 *        Useful for shutdown.
 *
 * @param q Queue pointer
 */
void MppQueue_WakeAll(MppQueue *q);

/**
 * @brief Close the queue.
 *
 * Marks the queue as closed and wakes all waiters. After this call:
 * - Threads blocked in MppQueue_Push/MppQueue_Pop return MPP_QUEUE_CLOSED.
 * - Subsequent MppQueue_Push/MppQueue_Pop return MPP_QUEUE_CLOSED immediately.
 * This provides a deterministic shutdown handshake; a bare condition-variable
 * broadcast cannot, because woken waiters would re-check their predicate and
 * go back to sleep.
 *
 * @param q Queue pointer
 */
void MppQueue_Close(MppQueue *q);

#ifdef __cplusplus
}
#endif

#endif /* MPP_QUEUE_H */
