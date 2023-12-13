/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-11 17:20:27
 * @LastEditTime: 2023-02-02 15:04:49
 * @Description: MppDataQueue is the managerment queue of MppData,
 *               which is used for data buffering
 */

#ifndef _MPP_DATA_QUEUE_H_
#define _MPP_DATA_QUEUE_H_

#include <pthread.h>

#include "data.h"
#include "para.h"
#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _MppDataQueueNode MppDataQueueNode;
typedef struct _MppDataQueue MppDataQueue;

/**
 * @description:
 * @param {BOOL} inblk
 * @param {BOOL} outblk
 * @return {*}
 */
MppDataQueue *DATAQUEUE_Init(BOOL inblk, BOOL outblk);

/**
 * @description:
 * @return {*}
 */
S32 DATAQUEUE_GetQueueStructSize();

/**
 * @description:
 * @return {*}
 */
S32 DATAQUEUE_GetNodeStructSize();

/**
 * @description:
 * @param {MppDataQueue} *queue
 * @param {MppDataQueueNode} *node
 * @return {*}
 */
RETURN DATAQUEUE_Push(MppDataQueue *queue, MppDataQueueNode *node);

/**
 * @description:
 * @param {MppDataQueue} *queue
 * @return {*}
 */
MppDataQueueNode *DATAQUEUE_Pop(MppDataQueue *queue);

/**
 * @description:
 * @param {MppDataQueue} *queue
 * @return {*}
 */
MppDataQueueNode *DATAQUEUE_First(MppDataQueue *queue);

/**
 * @description:
 * @param {MppDataQueue} *queue
 * @return {*}
 */
BOOL DATAQUEUE_IsEmpty(MppDataQueue *queue);

/**
 * @description:
 * @param {MppDataQueue} *queue
 * @return {*}
 */
BOOL DATAQUEUE_IsFull(MppDataQueue *queue);

/**
 * @description:
 * @param {MppDataQueue} *queue
 * @return {*}
 */
S32 DATAQUEUE_GetCurrentSize(MppDataQueue *queue);

/**
 * @description:
 * @param {MppDataQueue} *queue
 * @param {S32} max_size
 * @return {*}
 */
RETURN DATAQUEUE_SetMaxSize(MppDataQueue *queue, S32 max_size);

/**
 * @description:
 * @param {MppDataQueue} *queue
 * @return {*}
 */
S32 DATAQUEUE_GetMaxSize(MppDataQueue *queue);

/**
 * @description:
 * @param {MppDataQueueNode} *node
 * @return {*}
 */
MppData *DATAQUEUE_GetData(MppDataQueueNode *node);

/**
 * @description:
 * @param {MppDataQueueNode} *node
 * @param {MppData} *data
 * @return {*}
 */
RETURN DATAQUEUE_SetData(MppDataQueueNode *node, MppData *data);

/**
 * @description:
 * @param {MppDataQueue} *node
 * @param {BOOL} *VAL
 * @return {*}
 */
RETURN DATAQUEUE_SetWaitExit(MppDataQueue *queue, BOOL val);

/**
 * @description:
 * @param {MppDataQueue} *node
 * @return {*}
 */
RETURN DATAQUEUE_Cond_BroadCast(MppDataQueue *queue);

/**
 * @description:
 * @param {MppDataQueue} *queue
 * @return {*}
 */
void DATAQUEUE_Destory(MppDataQueue *queue);

/**
 * @description:
 * @return {*}
 */
MppDataQueueNode *DATAQUEUE_Node_Create();

/**
 * @description:
 * @param {MppDataQueueNode} *node
 * @return {*}
 */
void DATAQUEUE_Node_Destory(MppDataQueueNode *node);
#ifdef __cplusplus
};
#endif

#endif /*_MPP_DATA_QUEUE_H_*/
