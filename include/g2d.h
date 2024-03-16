/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:11:03
 * @LastEditTime: 2024-03-15 14:54:04
 * @Description:
 */

#ifndef _MPP_G2D_H_
#define _MPP_G2D_H_

#include "al_interface_g2d.h"
#include "data.h"
#include "frame.h"
#include "module.h"
#include "para.h"
#include "processflow.h"
#include "type.h"

/***
 * MPP_SYNC (always used, k1 v2d,etc.)
 *
 *            +--------------------------+
 *            |    G2D_CreateChannel     |
 *            +------------+-------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     G2D_Init             |
 *            +------------+-------------+
 *                         |
 *                         |
 *     +-------------------v--------------------+
 *     |            PROCESS LOOP                |
 *     |      +--------------------------+      |
 *     |      |      G2D_Process         |      |
 *     |      +--------------------------+      |
 *     +-------------------+--------------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     G2D_DestoryChannel   |
 *            +--------------------------+
 */

typedef enum _MppG2DProcessResult {
  G2D_RESULT_UNSUPPORTED = -1,
  G2D_RESULT_OK = 0,
  G2D_RESULT_DEV_NOT_OPEN = 1,
  G2D_RESULT_NULL_PTR = 2,
  G2D_RESULT_INVALID_HANDLE = 3,
  G2D_RESULT_INVALID_PARA = 4,
  G2D_RESULT_NO_MEM = 5,
  G2D_RESULT_MINIFICATION = 6,
  G2D_RESULT_UNSUPPORTED_OPERATION = 7,
  G2D_RESULT_JOB_TIMEOUT = 8,
  G2D_RESULT_INTERRUPT = 9,
  G2D_RESULT_NOT_ALIGNED = 10,
} MppG2DProcessResult;

typedef struct _MppG2dCtx {
  MppProcessNode pNode;
  MppModuleType eCodecType;
  MppModule *pModule;
  MppG2dPara stG2dPara;
} MppG2dCtx;

/******************* standard API ******************/

/**
 * @description:
 * @return {*}
 */
MppG2dCtx *G2D_CreateChannel();

/**
 * @description:
 * @param {MppG2dCtx} *ctx
 * @return {*}
 */
S32 G2D_Init(MppG2dCtx *ctx);

/**
 * @description:
 * @param {MppG2dCtx} *ctx
 * @param {MppG2dPara} *para
 * @return {*}
 */
S32 G2D_SetParam(MppG2dCtx *ctx, MppG2dPara *para);

/**
 * @description:
 * @param {MppG2dCtx} *ctx
 * @param {MppG2dPara} *para
 * @return {*}
 */
S32 G2D_GetParam(MppG2dCtx *ctx, MppG2dPara *para);

/**
 * @description: send full frame data to MPP, need return, input ASYNC mode
 * @param {MppG2dCtx} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 G2D_SendInputFrame(MppG2dCtx *ctx, MppData *sink_data);

/**
 * @description: return null frame data from MPP, input ASYNC mode
 * @param {MppG2dCtx} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 G2D_ReturnInputFrame(MppG2dCtx *ctx, MppData *sink_data);

/**
 * @description: send full frame data to MPP, no need to return, input SYNC mode
 * @param {MppG2dCtx} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 G2D_Convert(MppG2dCtx *ctx, MppData *sink_data);

/***
 * @description: send full frame data to MPP, and get full stream data from MPP,
 * SYNC mode
 * @param {MppG2dCtx} *ctx
 * @param {MppData} *sink_data
 * @param {MppData} *src_data
 * @return {*}
 */
S32 G2D_Process(MppG2dCtx *ctx, MppData *sink_data, MppData *src_data);

/**
 * @description: get full frame data from MPP, no need return, output SYNC mode
 * @param {MppG2dCtx} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 G2D_GetOutputFrame(MppG2dCtx *ctx, MppData *src_data);

/**
 * @description: get full frame data from MPP, need return, output ASYNC mode
 * @param {MppG2dCtx} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 G2D_RequestOutputFrame(MppG2dCtx *ctx, MppData *src_data);

/**
 * @description: return null frame data to MPP, output ASYNC mode
 * @param {MppG2dCtx} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 G2D_ReturnOutputFrame(MppG2dCtx *ctx, MppData *src_data);

/**
 * @description:
 * @param {MppG2dCtx} *ctx
 * @return {*}
 */
S32 G2D_DestoryChannel(MppG2dCtx *ctx);

/**
 * @description:
 * @param {MppG2dCtx} *ctx
 * @return {*}
 */
S32 G2D_ResetChannel(MppG2dCtx *ctx);

/******************* for bind system ******************/

/**
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 handle_g2d_data(ALBaseContext *base_context, MppData *sink_data);

/***
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData} *sink_data
 * @param {MppData} *src_data
 * @return {*}
 */
S32 handle_g2d_data_sync(ALBaseContext *base_context, MppData *sink_data,
                         MppData *src_data);

/***
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData} *src_data
 * @return {*}
 */
S32 get_g2d_result_sync(ALBaseContext *base_context, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData *} src_data
 * @return {*}
 */
S32 get_g2d_result(ALBaseContext *base_context, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData *} src_data
 * @return {*}
 */
S32 return_g2d_result(ALBaseContext *base_context, MppData *src_data);

#endif /*_MPP_G2D_H_*/
