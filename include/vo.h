/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-03-15 11:26:01
 * @LastEditTime: 2024-03-15 15:25:20
 * @FilePath: \mpp\include\vo.h
 * @Description:
 */

#ifndef _MPP_VO_H_
#define _MPP_VO_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "data.h"
#include "frame.h"
#include "module.h"
#include "processflow.h"
#include "type.h"

typedef struct _MppVoCtx {
  MppProcessNode pNode;
  MppModuleType eVoType;
  MppModule *pModule;
  MppVoPara stVoPara;
} MppVoCtx;

/******************* standard API ******************/

/**
 * @description: create a channel for video output, multi-channels can be
 * created simultaneously.
 * @return {*} : context for this channel.
 */
MppVoCtx *VO_CreateChannel();

/**
 * @description: init channel.
 * @param {MppVoCtx} *ctx: channel context, parameters are set by this
 * context.
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VO_Init(MppVoCtx *ctx);

/**
 * @description: set parameters, not always used now.
 * @param {MppVoCtx} *ctx： channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VO_SetParam(MppVoCtx *ctx);

/**
 * @description: get parameters, app get newest parameters.
 * @param {MppVoCtx} *ctx: channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VO_GetParam(MppVoCtx *ctx, MppVoPara **stVoPara);

/**
 * @description: handle data.
 * @param {MppVoCtx} *ctx: channel context
 * @param {MppData} *sink_data: input stream data
 * @return {*}: MPP_OK:successful, !MPP_OK:need to do something
 */
S32 VO_Process(MppVoCtx *ctx, MppData *sink_data);

/**
 * @description: destory the channel for video output.
 * @param {MppVoCtx} *ctx: channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VO_DestoryChannel(MppVoCtx *ctx);

/******************* for bind system ******************/

#ifdef __cplusplus
};
#endif

#endif /*_MPP_VO_H*/
