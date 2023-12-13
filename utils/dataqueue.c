/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2023-12-05 11:30:01
 * @Description:
 */

#define ENABLE_DEBUG 0

#include "dataqueue.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

//#define DEBUG_MEMORY

#ifdef DEBUG_MEMORY
S32 num_of_unfree_queue = 0;
S32 num_of_unfree_node = 0;
#endif

struct _MppDataQueueNode {
  struct _MppDataQueueNode *next;
  MppData *data;
};

struct _MppDataQueue {
  S32 nMaxNum;
  atomic_int nCurrentNum;
  MppDataQueueNode *pQueueHead;
  MppDataQueueNode *pQueueTail;

  BOOL bInputBlock;
  BOOL bOutputBlock;

  pthread_mutex_t mutex;
  pthread_cond_t inCond;
  pthread_cond_t outCond;
  BOOL bExit;
};

MppDataQueue *DATAQUEUE_Init(BOOL inblk, BOOL outblk) {
  MppDataQueue *queue = (MppDataQueue *)malloc(sizeof(MppDataQueue));
  if (!queue) {
    error("can not malloc MppDataQueue, please check! (%s)", strerror(errno));
    return NULL;
  }

#ifdef DEBUG_MEMORY
  num_of_unfree_queue++;
#endif

  queue->nMaxNum = 10;
  queue->nCurrentNum = ATOMIC_VAR_INIT(0);
  queue->pQueueHead = NULL;
  queue->pQueueTail = NULL;
  queue->bInputBlock = inblk;
  queue->bOutputBlock = outblk;
  queue->bExit = MPP_FALSE;

  pthread_mutex_init(&queue->mutex, NULL);
  pthread_cond_init(&queue->inCond, NULL);
  pthread_cond_init(&queue->outCond, NULL);

  return queue;
}

S32 DATAQUEUE_GetQueueStructSize() { return sizeof(MppDataQueue); }

S32 DATAQUEUE_GetNodeStructSize() { return sizeof(MppDataQueueNode); }

RETURN DATAQUEUE_Push(MppDataQueue *queue, MppDataQueueNode *node) {
  if (!queue) {
    error("input para MppDataQueue is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!node) {
    error("input para MppDataQueueNode is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  pthread_mutex_lock(&queue->mutex);

  if (atomic_load(&queue->nCurrentNum) == queue->nMaxNum) {
    if (queue->bInputBlock && queue->bExit == MPP_FALSE) {
      pthread_cond_wait(&queue->inCond, &queue->mutex);
    } else if (queue->bInputBlock && queue->bExit == MPP_TRUE) {
      debug("get exit singal, no push wait and exit!");
      goto exit;
    } else {
      goto exit;
    }
  }

  if (atomic_load(&queue->nCurrentNum) == 0) {
    queue->pQueueHead = node;
    queue->pQueueTail = node;
  } else {
    queue->pQueueHead->next = node;
    queue->pQueueHead = node;
  }
  atomic_fetch_add(&queue->nCurrentNum, 1);
  pthread_mutex_unlock(&queue->mutex);
  pthread_cond_signal(&queue->outCond);

  return MPP_OK;

exit:
  pthread_mutex_unlock(&queue->mutex);
  return MPP_DATAQUEUE_FULL;
}

MppDataQueueNode *DATAQUEUE_Pop(MppDataQueue *queue) {
  if (!queue) {
    error("input para MppDataQueue is NULL, please check!");
    return NULL;
  }

  pthread_mutex_lock(&queue->mutex);

  if (atomic_load(&queue->nCurrentNum) == 0) {
    if (queue->bOutputBlock && queue->bExit == MPP_FALSE) {
      pthread_cond_wait(&queue->outCond, &queue->mutex);
    } else if (queue->bOutputBlock && queue->bExit == MPP_TRUE) {
      debug("get exit singal, no pop wait and exit!");
      goto exit;
    } else {
      goto exit;
    }
  }

  // this is used for exit, after get signal, here quit
  if (atomic_load(&queue->nCurrentNum) == 0) {
    debug("wait up, but dataqueue is empty!");
    goto exit;
  }

  MppDataQueueNode *tmp = queue->pQueueTail;
  queue->pQueueTail = queue->pQueueTail->next;
  atomic_fetch_sub(&queue->nCurrentNum, 1);
  pthread_mutex_unlock(&queue->mutex);
  pthread_cond_signal(&queue->inCond);

  return tmp;

exit:
  pthread_mutex_unlock(&queue->mutex);
  return NULL;
}

MppDataQueueNode *DATAQUEUE_First(MppDataQueue *queue) {
  if (!queue) {
    error("input para MppDataQueue is NULL, please check!");
    return NULL;
  }

  pthread_mutex_lock(&queue->mutex);

  if (atomic_load(&queue->nCurrentNum) == 0) {
    if (queue->bOutputBlock && queue->bExit == MPP_FALSE) {
      pthread_cond_wait(&queue->outCond, &queue->mutex);
    } else if (queue->bOutputBlock && queue->bExit == MPP_TRUE) {
      debug("get singal, no pop wait and exit!");
      goto exit;
    } else {
      goto exit;
    }
  }

  // this is used for exit, after get signal, here quit
  if (atomic_load(&queue->nCurrentNum) == 0) {
    debug("wait up, but dataqueue is empty!");
    goto exit;
  }

  MppDataQueueNode *tmp = queue->pQueueTail;
  pthread_mutex_unlock(&queue->mutex);

  return tmp;

exit:
  pthread_mutex_unlock(&queue->mutex);
  return NULL;
}

RETURN DATAQUEUE_SetMaxSize(MppDataQueue *queue, S32 max_size) {
  if (!queue) {
    error("input para MppDataQueue is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (max_size <= 0) {
    error("max_size is not a valid value, please check!");
    return MPP_CHECK_FAILED;
  }

  queue->nMaxNum = max_size;

  return MPP_OK;
}

S32 DATAQUEUE_GetMaxSize(MppDataQueue *queue) {
  if (!queue) {
    error("input para MppDataQueue is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return queue->nMaxNum;
}

BOOL DATAQUEUE_IsEmpty(MppDataQueue *queue) {
  if (!queue) {
    error("input para MppDataQueue is NULL, please check!");
    return MPP_CHECK_FAILED;
  }

  if (atomic_load(&queue->nCurrentNum) == 0) {
    return MPP_TRUE;
  }

  return MPP_FALSE;
}

BOOL DATAQUEUE_IsFull(MppDataQueue *queue) {
  if (!queue) {
    error("input para MppDataQueue is NULL, please check!");
    return MPP_CHECK_FAILED;
  }

  if (queue->nMaxNum - atomic_load(&queue->nCurrentNum) == 0) {
    return MPP_TRUE;
  }

  return MPP_FALSE;
}

S32 DATAQUEUE_GetCurrentSize(MppDataQueue *queue) {
  if (!queue) {
    error("input para MppDataQueue is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  return atomic_load(&queue->nCurrentNum);
}

MppData *DATAQUEUE_GetData(MppDataQueueNode *node) {
  if (!node) {
    error("input para MppDataQueueNode is NULL, please check!");
    return NULL;
  }

  return node->data;
}

RETURN DATAQUEUE_SetData(MppDataQueueNode *node, MppData *data) {
  if (!node) {
    error("input para MppDataQueueNode is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!data) {
    error("input para MppData is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  node->data = data;

  return MPP_OK;
}

RETURN DATAQUEUE_SetWaitExit(MppDataQueue *queue, BOOL val) {
  if (!queue) {
    error("input para MppDataQueue is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  queue->bExit = val;

  return MPP_OK;
}

RETURN DATAQUEUE_Cond_BroadCast(MppDataQueue *queue) {
  if (!queue) {
    error("input para MppDataQueue is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  pthread_cond_broadcast(&queue->inCond);
  pthread_cond_broadcast(&queue->outCond);

  return MPP_OK;
}

void DATAQUEUE_Destory(MppDataQueue *queue) {
  if (!queue) {
    error("input para MppDataQueue is NULL, please check!");
    return;
  }

  pthread_mutex_destroy(&queue->mutex);
  pthread_cond_destroy(&queue->inCond);
  pthread_cond_destroy(&queue->outCond);

  free(queue);
  // queue = NULL;

#ifdef DEBUG_MEMORY
  num_of_unfree_queue--;
  debug("debug dataqueue memory: num of unfree queue: %d", num_of_unfree_queue);
#endif
}

MppDataQueueNode *DATAQUEUE_Node_Create() {
  MppDataQueueNode *node = (MppDataQueueNode *)malloc(sizeof(MppDataQueueNode));

  if (!node) {
    error("can not malloc MppDataQueueNode! please check! (%s)",
          strerror(errno));
    return NULL;
  }

#ifdef DEBUG_MEMORY
  num_of_unfree_node++;
#endif

  memset(node, 0, sizeof(MppDataQueueNode));

  return node;
}

void DATAQUEUE_Node_Destory(MppDataQueueNode *node) {
  if (!node) {
    error("input para MppDataQueueNode is NULL, please check!");
    return;
  }

  free(node);
  // node = NULL;

#ifdef DEBUG_MEMORY
  num_of_unfree_node--;
  debug("debug dataqueue memory: num of unfree node: %d", num_of_unfree_node);
#endif
}
