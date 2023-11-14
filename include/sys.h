/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-30 17:52:04
 * @LastEditTime: 2023-02-02 14:39:47
 * @Description:
 */

#ifndef _MPP_SYS_H_
#define _MPP_SYS_H_

#include "g2d.h"
#include "processflow.h"
#include "vdec.h"
#include "venc.h"

/**
 * @description:
 * @return {*}
 */
S32 SYS_GetVersion();

/**
 * @description:
 * @return {*}
 */
MppProcessFlowCtx *SYS_CreateFlow();

/**
 * @description:
 * @param {MppProcessNodeType} type
 * @return {*}
 */
MppProcessNode *SYS_CreateNode(MppProcessNodeType type);

/**
 * @description:
 * @param {MppProcessFlowCtx} *ctx
 * @return {*}
 */
void SYS_Init(MppProcessFlowCtx *ctx);

/**
 * @description:
 * @param {MppProcessFlowCtx} *ctx
 * @param {MppProcessNode} *src_ctx
 * @param {MppProcessNode} *sink_ctx
 * @return {*}
 */
S32 SYS_Bind(MppProcessFlowCtx *ctx, MppProcessNode *src_ctx,
             MppProcessNode *sink_ctx);

/**
 * @description:
 * @param {MppProcessFlowCtx} *ctx
 * @return {*}
 */
void SYS_Unbind(MppProcessFlowCtx *ctx);

/**
 * @description:
 * @param {MppProcessFlowCtx} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
void SYS_Handledata(MppProcessFlowCtx *ctx, MppData *sink_data);

/**
 * @description:
 * @param {MppProcessFlowCtx} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
void SYS_Getresult(MppProcessFlowCtx *ctx, MppData *src_data);

/**
 * @description:
 * @param {MppProcessFlowCtx} *ctx
 * @return {*}
 */
void SYS_Destory(MppProcessFlowCtx *ctx);

#endif /*_MPP_SYS_H_*/
