/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-04-25 19:21:30
 * @LastEditTime: 2024-04-29 11:48:17
 * @FilePath: \mpp\include\vi.h
 * @Description:
 */

#ifndef _MPP_VI_H_
#define _MPP_VI_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "data.h"
#include "frame.h"
#include "module.h"
#include "processflow.h"
#include "type.h"

typedef struct _MppViCtx {
  MppProcessNode pNode;
  MppModuleType eViType;
  MppModule *pModule;
  MppViPara stViPara;
} MppViCtx;

/******************* standard API ******************/

/**
 * @description: create a channel for video input, multi-channels can be
 * created simultaneously.
 * @return {*} : context for this channel.
 */
MppViCtx *VI_CreateChannel();

/**
 * @description: init channel.
 * @param {MppViCtx} *ctx: channel context, parameters are set by this
 * context.
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VI_Init(MppViCtx *ctx);

/**
 * @description: set parameters, not always used now.
 * @param {MppViCtx} *ctx： channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VI_SetParam(MppViCtx *ctx);

/**
 * @description: get parameters, app get newest parameters.
 * @param {MppViCtx} *ctx: channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VI_GetParam(MppViCtx *ctx, MppViPara **stViPara);

/**
 * @description: handle data.
 * @param {MppViCtx} *ctx: channel context
 * @param {MppData} *src_data: input stream data
 * @return {*}: MPP_OK:successful, !MPP_OK:need to do something
 */
S32 VI_Process(MppViCtx *ctx, MppData *src_data);

/**
 * @description:
 * @param {MppViCtx} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 VI_RequestOutputData(MppViCtx *ctx, MppData *src_data);

/**
 * @description:
 * @param {MppViCtx} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 VI_ReturnOutputData(MppViCtx *ctx, MppData *src_data);

/**
 * @description: destory the channel for video input.
 * @param {MppVoCtx} *ctx: channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VI_DestoryChannel(MppViCtx *ctx);

/******************* for bind system ******************/

#ifdef __cplusplus
};
#endif

#endif /*_MPP_VI_H*/