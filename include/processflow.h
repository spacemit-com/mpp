/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-11 14:35:20
 * @LastEditTime: 2023-02-03 11:06:10
 * @Description: base class for pipeline flow
 */

#ifndef _MPP_BASE_H_
#define _MPP_BASE_H_

#include <pthread.h>

#include "al_interface_base.h"
#include "data.h"

#define MAX_NODE_NUM 10
#define MAX_NODE_NAME_LENGTH 20

/*
 *                  +------------------------+
 *                  |  MppProcessNodeCtx     |
 *                  +------------------------+
 *                  |   nNodeNum             |
 *                  |   pNode[]              |
 *                  |   pthread[]            |
 *                  +-----------+------------+
 *                              |
 *                              |
 *                  +-----------v------------+
 *                  |  MppProcessNode        |
 *                  +------------------------+
 *                  |   nNodeId              |
 *                  |   eType                |
 *                  |   pAlBaseContext       |
 *                  |   MppOps               |
 *                  +-----------^------------+
 *                              |
 *                              |
 *            +-----------------+---------------+
 *            |                                 |
 *            |                                 |
 * +----------+-------------+       +-----------+-----------+
 * |       MppVdecCtx       |       |       MppVencCtx      |
 * +------------------------+       +-----------------------+
 * |   pNode                |       |   pNode               |
 * +------------------------+       +-----------------------+
 *
 */

/***
 * @description: common ops for bind node
 *
 */
typedef struct _MppOps {
  /***
   * @description: send unhandled data to process node
   * @param {ALBaseContext} *base_context
   * @param {MppData} *sink_data
   * @return {*}
   */
  S32 (*handle_data)(ALBaseContext *base_context, MppData *sink_data);
  /***
   * @description: get handled result from process node
   * @param {ALBaseContext} *base_context
   * @param {MppData} *src_data
   * @return {*}
   */
  S32 (*get_result)(ALBaseContext *base_context, MppData *src_data);
  /***
   * @description: return the buffer to process node if needed
   * @param {ALBaseContext} *base_context
   * @param {MppData} *src_data
   * @return {*}
   */
  S32 (*return_result)(ALBaseContext *base_context, MppData *src_data);
} MppOps;

/***
 * @description: process node type enum
 *
 */
typedef enum _MppProcessNodeType {
  /***
   * video decoder node, bitstream in and frame out
   */
  VDEC = 1,

  /***
   * video encoder node, frame in and bitstream out
   */
  VENC = 2,

  /***
   * graphic 2d node, frame in and frame out
   */
  G2D = 3,
} MppProcessNodeType;

/***
 * @description: mapping struct from MppProcessNodeType to sNodeName
 *
 */
typedef struct _MppProcessNodeTypeMapping {
  MppProcessNodeType eType;
  S8 sNodeName[MAX_NODE_NAME_LENGTH];
} MppProcessNodeTypeMapping;

/***
 * @description: process node bind-couple supported
 *
 * some couples are not supported, vdec-vdec,etc.
 */
typedef struct _MppProcessNodeBindCouple {
  MppProcessNodeType eSrcNodeType;
  MppProcessNodeType eSInkNodeType;
} MppProcessNodeBindCouple;

/***
 * @description: main process node struct
 *
 * manage every process node.
 */
typedef struct _MppProcessNode {
  S32 nNodeId;
  MppProcessNodeType eType;
  ALBaseContext *pAlBaseContext;
  MppOps *ops;
} MppProcessNode;

/***
 * @description: main process flow struct
 *
 * manage the whole process flow, maybe include some process nodes.
 */
typedef struct _MppProcessFlowCtx {
  /***
   * the total node num of the flow.
   */
  S32 nNodeNum;

  /***
   * every node in this flow.
   */
  MppProcessNode *pNode[MAX_NODE_NUM];

  /***
   * data transmission thread between nodes.
   */
  pthread_t pthread[MAX_NODE_NUM];
} MppProcessFlowCtx;

#endif /*_MPP_BASE_H_*/
