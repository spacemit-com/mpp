/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-11 10:28:37
 * @LastEditTime: 2024-03-15 14:53:57
 * @Description: MPP VENC API, use these API to do video encode
 *               from frame(YUV420) to stream(H.264 etc.)
 */

#ifndef _MPP_VENC_H_
#define _MPP_VENC_H_

#include "data.h"
#include "frame.h"
#include "module.h"
#include "packet.h"
#include "processflow.h"
#include "type.h"

/***
 * MPP_INPUT_ASYNC_OUTPUT_SYNC (always used, ffmpeg,etc.)
 *
 *            +--------------------------+
 *            |    VENC_CreateChannel    |
 *            +------------+-------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     VENC_Init            |
 *            +------------+-------------+
 *                         |
 *     +-------------------v--------------------+
 *     |         SEND FRAME LOOP                |
 *     |      +--------------------------+      |
 *     |      |    VENC_SendInputFrame   |      |
 *     |      +--------------------------+      |
 *     |                                        |
 *     |      +--------------------------+      |
 *     |      |   VENC_ReturnInputFrame  |      |
 *     |      +--------------------------+      |
 *     +-------------------+--------------------+
 *                         |
 *                         |
 *     +-------------------v--------------------+
 *     |         GET STREAM LOOP                |
 *     |      +--------------------------+      |
 *     |      |VENC_GetOutputStreamBuffer|      |
 *     |      +--------------------------+      |
 *     +-------------------+--------------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     VENC_DestoryChannel  |
 *            +--------------------------+
 */

/***
 * MPP_SYNC (NOT always used, some soft encoder maybe)
 *
 *            +--------------------------+
 *            |    VENC_CreateChannel    |
 *            +------------+-------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     VENC_Init            |
 *            +------------+-------------+
 *                         |
 *     +-------------------v--------------------+
 *     |              ENCODE LOOP               |
 *     |      +--------------------------+      |
 *     |      |     VENC_Process         |      |
 *     |      +--------------------------+      |
 *     +-------------------+--------------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     VENC_DestoryChannel  |
 *            +--------------------------+
 */

/***
 * MPP_INPUT_SYNC_OUTPUT_SYNC
 *
 *            +--------------------------+
 *            |    VENC_CreateChannel    |
 *            +------------+-------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     VENC_Init            |
 *            +------------+-------------+
 *                         |
 *     +-------------------v--------------------+
 *     |         SEND FRAME LOOP                |
 *     |      +--------------------------+      |
 *     |      |        VENC_Encode       |      |
 *     |      +--------------------------+      |
 *     +-------------------+--------------------+
 *                         |
 *                         |
 *     +-------------------v--------------------+
 *     |         GET STREAM LOOP                |
 *     |      +--------------------------+      |
 *     |      |VENC_GetOutputStreamBuffer|      |
 *     |      +--------------------------+      |
 *     +-------------------+--------------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     VENC_DestoryChannel  |
 *            +--------------------------+
 */

typedef struct _MppVencCtx {
  MppProcessNode pNode;
  MppModuleType eCodecType;
  MppVencPara stVencPara;
  MppModule *pModule;
} MppVencCtx;

/******************* standard API ******************/

/**
 * @description:
 * @return {*}
 */
MppVencCtx *VENC_CreateChannel();

/**
 * @description:
 * @param {MppVencCtx} *ctx
 * @return {*}
 */
S32 VENC_Init(MppVencCtx *ctx);

/**
 * @description:
 * @param {MppVencCtx} *ctx
 * @param {MppVencPara} *para
 * @return {*}
 */
S32 VENC_SetParam(MppVencCtx *ctx, MppVencPara *para);

/**
 * @description:
 * @param {MppVencCtx} *ctx
 * @param {MppVencPara} *para
 * @return {*}
 */
S32 VENC_GetParam(MppVencCtx *ctx, MppVencPara *para);

/**
 * @description: send full frame data to MPP, need return, input ASYNC mode
 * @param {MppVencCtx} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 VENC_SendInputFrame(MppVencCtx *ctx, MppData *sink_data);

/**
 * @description: get null frame data from MPP, input ASYNC mode
 * @param {MppVencCtx} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 VENC_ReturnInputFrame(MppVencCtx *ctx, MppData *sink_data);

/**
 * @description: send full frame data to MPP, input SYNC mode
 * @param {MppVencCtx} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 VENC_Encode(MppVencCtx *ctx, MppData *sink_data);

/**
 * @description: send full frame data to MPP, and get stream data from MPP, SYNC
 * mode
 * @param {MppVencCtx} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 VENC_Process(MppVencCtx *ctx, MppData *sink_data, MppData *src_data);

/**
 * @description: get full stream data from MPP, output SYNC mode
 * @param {MppVencCtx} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 VENC_GetOutputStreamBuffer(MppVencCtx *ctx, MppData *src_data);

/**
 * @description: get full stream data from MPP, need return, output ASYNC mode
 * @param {MppVencCtx} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 VENC_RequestOutputStreamBuffer(MppVencCtx *ctx, MppData *src_data);

/**
 * @description: return null stream data to MPP, output ASYNC mode
 * @param {MppVencCtx} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 VENC_ReturnOutputStreamBuffer(MppVencCtx *ctx, MppData *src_data);

/**
 * @description: flush data in MPP(include hardware decoder), must flush output
 * frame, maybe flush input stream.
 * @param {MppVencCtx} *ctx: channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VENC_Flush(MppVencCtx *ctx);

/**
 * @description:
 * @param {MppVencCtx} *ctx
 * @return {*}
 */
S32 VENC_DestoryChannel(MppVencCtx *ctx);

/**
 * @description:
 * @param {MppVencCtx} *ctx
 * @return {*}
 */
S32 VENC_ResetChannel(MppVencCtx *ctx);

/******************* for bind system ******************/

/**
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 handle_venc_data(ALBaseContext *base_context, MppData *sink_data);

/***
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData} *sink_data
 * @param {MppData} *src_data
 * @return {*}
 */
S32 process_venc_data(ALBaseContext *base_context, MppData *sink_data,
                      MppData *src_data);

/***
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData} *src_data
 * @return {*}
 */
S32 get_venc_result_sync(ALBaseContext *base_context, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData *} src_data
 * @return {*}
 */
S32 get_venc_result(ALBaseContext *base_context, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData *} src_data
 * @return {*}
 */
S32 return_venc_result(ALBaseContext *base_context, MppData *src_data);

#endif /*_MPP_VENC_H*/
