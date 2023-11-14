/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-30 17:52:11
 * @LastEditTime: 2023-07-31 15:17:49
 * @Description:
 */

#define ENABLE_DEBUG 0

#include "sys.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "log.h"

#define MODULE_TAG "mpp_sys"

/*
 *    Data
 * +--+--+--+--+
 * |  |  |  |  |
 * +--+--+--+--+
 *             |
 *             |
 * +-----------v-----------------------------------+
 * |                  ProcessNode[0]               |
 * +-----------+-----------------------------------+
 *             |
 *             |Get
 *             |
 *      -------+-->Thread+-+-------
 *                         |
 *                         |Send
 *                         |
 * +-----------------------v-----------------------+
 * |                  ProcessNode[1]               |
 * +-----------------------+-----------------------+
 *                         |
 *                         |Get
 *                         |
 *                 --------+-->Thread+-+-------
 *                                     |
 *                                     |Send
 *                                     |
 * +-----------------------------------v-----------+
 * |                  ProcessNode[2]               |
 * +-----------------------------------+-----------+
 *                                     |
 *                                     |
 *                                     v   Data
 *                                     +--+--+--+--+
 *                                     |  |  |  |  |
 *                                     +--+--+--+--+
 *
 */

static const MppProcessNodeTypeMapping mapping[] = {
    {VDEC, "Vdec"}, {VENC, "Venc"},
    //    {G2D, "G2d"},
};

static const MppProcessNodeBindCouple couple[] = {
    {VDEC, VENC},
    {VDEC, G2D},

    {VENC, VDEC},

    {G2D, VENC},
};

MppProcessFlowCtx *ctx;

S32 SYS_GetVersion() { return 0; }

MppProcessFlowCtx *SYS_CreateFlow() {
  ctx = (MppProcessFlowCtx *)malloc(sizeof(MppProcessFlowCtx));
  if (!ctx) {
    error("Can not create MppProcessFlowCtx, please check it !");
    return NULL;
  }

  return ctx;
}

void SYS_Init(MppProcessFlowCtx *ctx) { ctx->nNodeNum = 0; }

#define CREATE_NODE_BY_TYPE(TYPE, Type, type)                 \
  {                                                           \
    Mpp##Type##Ctx *ctx = TYPE##_CreateChannel();             \
    if (!ctx) {                                               \
      error("can not create Mpp##type##Ctx, please check !"); \
      return NULL;                                            \
    }                                                         \
    ctx->pNode.eType = TYPE;                                  \
    ctx->pNode.ops = (MppOps *)malloc(sizeof(MppOps));        \
    ctx->pNode.ops->handle_data = &handle_##type##_data;      \
    ctx->pNode.ops->get_result = &get_##type##_result;        \
    ctx->pNode.ops->return_result = &return_##type##_result;  \
    TYPE##_Init(ctx);                                         \
    return &(ctx->pNode);                                     \
  }

MppProcessNode *SYS_CreateNode(MppProcessNodeType type) {
  switch (type) {
    case 1:
      CREATE_NODE_BY_TYPE(VDEC, Vdec, vdec)
      break;
    case 2:
      CREATE_NODE_BY_TYPE(VENC, Venc, venc)
      break;
      //    case 3:
      //        CREATE_NODE_BY_TYPE(G2d)
      //        break;
    default:
      break;
  }

  return NULL;
}

RETURN check_bind_couple(MppProcessNode *src_node, MppProcessNode *sink_node) {
  S32 i = 0;
  if (!src_node || !sink_node) {
    error("src_node or sink_node is NULL, please check !");
    return MPP_NULL_POINTER;
  }

  for (i = 0; i < NUM_OF(couple); i++) {
    if (couple[i].eSrcNodeType == src_node->eType &&
        couple[i].eSInkNodeType == sink_node->eType)
      return MPP_OK;
  }

  return MPP_CHECK_FAILED;
}

void *data_transfer(void *private_data) {
  debug("------------------new thread-------------------");
  // MppProcessFlowCtx* ctx = (MppProcessFlowCtx*)private_data;
  S32 pthread_num = *(int *)private_data;
  S32 ret = 0;
  debug("pthread_num = %d", pthread_num);

  MppFrame *tmp_frame = NULL;
  MppPacket *tmp_packet = NULL;

  if (VDEC == ctx->pNode[pthread_num]->eType) {
    tmp_frame = FRAME_Create();

    while (1) {
      while (1) {
        ret = ctx->pNode[pthread_num]->ops->get_result(
            ctx->pNode[pthread_num]->pAlBaseContext,
            FRAME_GetBaseData(tmp_frame));
        debug("dec-enc get_result pthread_num = %d, ret = %d", pthread_num,
              ret);
        if (!ret)
          break;
        else
          usleep(10 * 1000);
      }

      ctx->pNode[pthread_num + 1]->ops->handle_data(
          ctx->pNode[pthread_num + 1]->pAlBaseContext,
          FRAME_GetBaseData(tmp_frame));
    }
  } else if (VENC == ctx->pNode[pthread_num]->eType) {
    tmp_packet = PACKET_Create();

    while (1) {
      while (1) {
        ret = ctx->pNode[pthread_num]->ops->get_result(
            ctx->pNode[pthread_num]->pAlBaseContext,
            PACKET_GetBaseData(tmp_packet));
        debug("enc-dec get_result pthread_num = %d, ret = %d", pthread_num,
              ret);
        if (!ret)
          break;
        else
          usleep(10 * 1000);
      }

      ctx->pNode[pthread_num + 1]->ops->handle_data(
          ctx->pNode[pthread_num + 1]->pAlBaseContext,
          PACKET_GetBaseData(tmp_packet));
    }
  }

  return NULL;
}

S32 SYS_Bind(MppProcessFlowCtx *ctx, MppProcessNode *src_node,
             MppProcessNode *sink_node) {
  S32 ret = MPP_OK;
  ret = check_bind_couple(src_node, sink_node);
  if (ret != MPP_OK) return ret;
  if (0 == ctx->nNodeNum) {
    ctx->pNode[0] = src_node;
    ctx->pNode[1] = sink_node;
    ctx->nNodeNum = 2;
    ctx->pNode[0]->nNodeId = 0;
    ctx->pNode[1]->nNodeId = 1;
  } else {
    if (ctx->pNode[ctx->nNodeNum - 1]->eType != src_node->eType) {
      error("not match");
      return MPP_BIND_NOT_MATCH;
    }
    ctx->pNode[ctx->nNodeNum] = sink_node;
    ctx->pNode[ctx->nNodeNum]->nNodeId = ctx->nNodeNum;
    ctx->nNodeNum++;
  }

  ret = pthread_create(&(ctx->pthread[ctx->nNodeNum - 2]), NULL, data_transfer,
                       (void *)&ctx->pNode[ctx->nNodeNum - 2]->nNodeId);

  return ret;
}

void SYS_Unbind(MppProcessFlowCtx *ctx) {
  S32 i = 0;
  if (ctx) {
    ctx->nNodeNum = 0;
    for (i = 0; i < MAX_NODE_NUM; i++) {
      if (ctx->pNode[i]) {
        free(ctx->pNode[i]);
        ctx->pNode[i] = NULL;
      }
    }
  }
}

void SYS_Handledata(MppProcessFlowCtx *ctx, MppData *sink_data) {
  ctx->pNode[0]->ops->handle_data(ctx->pNode[0]->pAlBaseContext, sink_data);
}

void SYS_Getresult(MppProcessFlowCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  while (1) {
    ret = ctx->pNode[ctx->nNodeNum - 1]->ops->get_result(
        ctx->pNode[ctx->nNodeNum - 1]->pAlBaseContext, src_data);
    debug("get_result ctx->nNodeNum = %d, ret = %d", ctx->nNodeNum - 1, ret);
    if (!ret)
      break;
    else
      usleep(20 * 1000);
  }
}

void SYS_Destory(MppProcessFlowCtx *ctx) {
  if (ctx) {
    free(ctx);
    // ctx = NULL;
  }
}
