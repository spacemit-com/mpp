/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: MppDataQueue - data buffering queue for MppData
 */

#ifndef DATAQUEUE_H
#define DATAQUEUE_H

#include <pthread.h>

#include "data.h"
#include "para.h"
#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _MppDataQueueNode MppDataQueueNode;
typedef struct _MppDataQueue MppDataQueue;

MppDataQueue *DATAQUEUE_Init(BOOL inblk, BOOL outblk);
S32 DATAQUEUE_GetQueueStructSize();
S32 DATAQUEUE_GetNodeStructSize();
RETURN DATAQUEUE_Push(MppDataQueue *queue, MppDataQueueNode *node);
MppDataQueueNode *DATAQUEUE_Pop(MppDataQueue *queue);
MppDataQueueNode *DATAQUEUE_First(MppDataQueue *queue);
BOOL DATAQUEUE_IsEmpty(MppDataQueue *queue);
BOOL DATAQUEUE_IsFull(MppDataQueue *queue);
S32 DATAQUEUE_GetCurrentSize(MppDataQueue *queue);
RETURN DATAQUEUE_SetMaxSize(MppDataQueue *queue, S32 max_size);
S32 DATAQUEUE_GetMaxSize(MppDataQueue *queue);
MppData *DATAQUEUE_GetData(MppDataQueueNode *node);
RETURN DATAQUEUE_SetData(MppDataQueueNode *node, MppData *data);
RETURN DATAQUEUE_SetWaitExit(MppDataQueue *queue, BOOL val);
RETURN DATAQUEUE_Cond_BroadCast(MppDataQueue *queue);
void DATAQUEUE_Destory(MppDataQueue *queue);
MppDataQueueNode *DATAQUEUE_Node_Create();
void DATAQUEUE_Node_Destory(MppDataQueueNode *node);

#ifdef __cplusplus
};
#endif

#endif /*DATAQUEUE_H*/
