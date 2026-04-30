/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: Process-node types for legacy context APIs (VDEC/VENC bind).
 */

#ifndef _MPP_BASE_H_
#define _MPP_BASE_H_

#include <pthread.h>

#include "al_interface_base.h"
#include "data.h"

#define MAX_NODE_NUM 10
#define MAX_NODE_NAME_LENGTH 20

typedef struct _MppOps {
  S32 (*handle_data)(ALBaseContext *base_context, MppData *sink_data);
  S32 (*get_result)(ALBaseContext *base_context, MppData *src_data);
  S32 (*return_result)(ALBaseContext *base_context, MppData *src_data);
} MppOps;

typedef enum _MppProcessNodeType {
  VDEC = 1,
  VENC = 2,
  G2D = 3,
} MppProcessNodeType;

typedef struct _MppProcessNodeTypeMapping {
  MppProcessNodeType eType;
  S8 sNodeName[MAX_NODE_NAME_LENGTH];
} MppProcessNodeTypeMapping;

typedef struct _MppProcessNodeBindCouple {
  MppProcessNodeType eSrcNodeType;
  MppProcessNodeType eSInkNodeType;
} MppProcessNodeBindCouple;

typedef struct _MppProcessNode {
  S32 nNodeId;
  MppProcessNodeType eType;
  ALBaseContext *pAlBaseContext;
  MppOps *ops;
} MppProcessNode;

typedef struct _MppProcessFlowCtx {
  S32 nNodeNum;
  MppProcessNode *pNode[MAX_NODE_NUM];
  pthread_t pthread[MAX_NODE_NUM];
} MppProcessFlowCtx;

#endif /* _MPP_BASE_H_ */
