/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-11 17:24:41
 * @LastEditTime: 2024-03-15 14:53:30
 * @Description: MPP VDEC API, use these API to do video decode
 *               from stream(H.264 etc.) to frame(YUV420)
 */

#ifndef _MPP_VDEC_H_
#define _MPP_VDEC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "data.h"
#include "frame.h"
#include "module.h"
#include "packet.h"
#include "processflow.h"
#include "type.h"

/***
 * MPP_INPUT_SYNC_OUTPUT_ASYNC (always used, ffmpeg,etc.)
 *
 *            +--------------------------+
 *            |    VDEC_CreateChannel    |
 *            +------------+-------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     VDEC_Init            |
 *            +------------+-------------+
 *                         |
 *     +-------------------v--------------------+
 *     |         SEND STREAM LOOP               |
 *     |      +--------------------------+      |
 *     |      |     VDEC_Decode          |      |
 *     |      +--------------------------+      |
 *     +-------------------+--------------------+
 *                         |
 *                         |
 *     +-------------------v--------------------+
 *     |         GET FRAME LOOP                 |
 *     |      +--------------------------+      |
 *     |      |  VDEC_RequestOutputFrame |      |
 *     |      |            or            |      |
 *     |      |VDEC_RequestOutputFrame_2 |      |
 *     |      +--------------------------+      |
 *     |                                        |
 *     |      +--------------------------+      |
 *     |      |  VDEC_ReturnOutputFrame  |      |
 *     |      +--------------------------+      |
 *     +-------------------+--------------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     VDEC_DestoryChannel  |
 *            +--------------------------+
 */

/***
 * MPP_SYNC (NOT always used, some soft decoder maybe)
 *
 *            +--------------------------+
 *            |    VDEC_CreateChannel    |
 *            +------------+-------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     VDEC_Init            |
 *            +------------+-------------+
 *                         |
 *     +-------------------v--------------------+
 *     |              DECODE LOOP               |
 *     |      +--------------------------+      |
 *     |      |     VDEC_Process         |      |
 *     |      +--------------------------+      |
 *     +-------------------+--------------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     VDEC_DestoryChannel  |
 *            +--------------------------+
 */

/***
 * MPP_INPUT_SYNC_OUTPUT_SYNC
 *
 *            +--------------------------+
 *            |    VDEC_CreateChannel    |
 *            +------------+-------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     VDEC_Init            |
 *            +------------+-------------+
 *                         |
 *     +-------------------v--------------------+
 *     |         SEND STREAM LOOP               |
 *     |      +--------------------------+      |
 *     |      |     VDEC_Decode          |      |
 *     |      +--------------------------+      |
 *     +-------------------+--------------------+
 *                         |
 *                         |
 *     +-------------------v--------------------+
 *     |         GET FRAME LOOP                 |
 *     |      +--------------------------+      |
 *     |      |  VDEC_GetOutputFrame     |      |
 *     |      +--------------------------+      |
 *     +-------------------+--------------------+
 *                         |
 *                         |
 *            +------------v-------------+
 *            |     VDEC_DestoryChannel  |
 *            +--------------------------+
 */

typedef struct _MppVdecCtx {
  MppProcessNode pNode;
  MppModuleType eCodecType;
  MppModule *pModule;
  MppVdecPara stVdecPara;
} MppVdecCtx;

/******************* standard API ******************/

/**
 * @description: create a channel for video decoding, multi-channels can be
 * created simultaneously.
 * @return {*} : context for this channel.
 */
MppVdecCtx *VDEC_CreateChannel();

/**
 * @description: init channel.
 * @param {MppVdecCtx} *ctx: channel context, parameters are set by this
 * context.
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VDEC_Init(MppVdecCtx *ctx);

/**
 * @description: set parameters, not always used now, parameters are set by
 * VDEC_Init, maybe useful later.
 * @param {MppVdecCtx} *ctx： channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VDEC_SetParam(MppVdecCtx *ctx);

/**
 * @description: get parameters, app get newest parameters, always used for get
 * number of buffer freeslots.
 * @param {MppVdecCtx} *ctx: channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VDEC_GetParam(MppVdecCtx *ctx, MppVdecPara **stVdecPara);

/**
 * @description: get default parameters, not always used.
 * @param {MppVdecCtx} *ctx: channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VDEC_GetDefaultParam(MppVdecCtx *ctx);

/**
 * @description: send stream data to MPP, input sync mode, meaning that input
 * stream data can be released after calling this interface.
 * @param {MppVdecCtx} *ctx: channel context
 * @param {MppData} *sink_data: input stream data
 * @return {*}: MPP_OK:successful, !MPP_OK:need to do something
 */
S32 VDEC_Decode(MppVdecCtx *ctx, MppData *sink_data);

/**
 * @description: send stream data to MPP, and at the same times, get frame data
 * from MPP, sync mode, not always used.
 * @param {MppVdecCtx} *ctx: channel context
 * @param {MppData} *sink_data: input stream data
 * @param {MppData} *src_data: output frame data
 * @return {*}: MPP_OK:successful, !MPP_OK:need to do something
 */
S32 VDEC_Process(MppVdecCtx *ctx, MppData *sink_data, MppData *src_data);

/**
 * @description: get frame data from MPP, no need return, output sync mode, not
 * always used.
 * @param {MppVdecCtx} *ctx: channel context
 * @param {MppData} *src_data: output frame data
 * @return {*}: MPP_OK:successful, !MPP_OK:need to do something
 */
S32 VDEC_GetOutputFrame(MppVdecCtx *ctx, MppData *src_data);

/**
 * @description: get frame data from MPP, output async mode, APP should pair
 * with VDEC_ReturnOutputFrame for use.
 * @param {MppVdecCtx} *ctx: channel context
 * @param {MppData} *src_data: output frame data
 * @return {*}: MPP_OK:successful, !MPP_OK:need to do something
 */
S32 VDEC_RequestOutputFrame(MppVdecCtx *ctx, MppData *src_data);

/**
 * @description: get frame data from MPP, output async mode, not always used.
 * @param {MppVdecCtx} *ctx: channel context
 * @param {MppData} **src_data: output frame data
 * @return {*}: MPP_OK:successful, !MPP_OK:need to do something
 */
S32 VDEC_RequestOutputFrame_2(MppVdecCtx *ctx, MppData **src_data);

/**
 * @description: return frame data to MPP, output async mode
 * @param {MppVdecCtx} *ctx: channel context
 * @param {MppData} *src_data: output frame data
 * @return {*}: MPP_OK:successful, !MPP_OK:need to do something
 */
S32 VDEC_ReturnOutputFrame(MppVdecCtx *ctx, MppData *src_data);

/**
 * @description: flush data in MPP(include hardware decoder), must flush output
 * frame, maybe flush input stream.
 * @param {MppVdecCtx} *ctx: channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VDEC_Flush(MppVdecCtx *ctx);

/**
 * @description: destory the channel for decoding.
 * @param {MppVdecCtx} *ctx: channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VDEC_DestoryChannel(MppVdecCtx *ctx);

/**
 * @description: reset the channel for decoding, not always used.
 * @param {MppVdecCtx} *ctx: channel context
 * @return {*}: MPP_OK:successful, !MPP_OK:failed
 */
S32 VDEC_ResetChannel(MppVdecCtx *ctx);

/******************* for bind system ******************/

/**
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 handle_vdec_data(ALBaseContext *base_context, MppData *sink_data);

/***
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData} *sink_data
 * @param {MppData} *src_data
 * @return {*}
 */
S32 process_vdec_data(ALBaseContext *base_context, MppData *sink_data,
                      MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData *} src_data
 * @return {*}
 */
S32 get_vdec_result(ALBaseContext *base_context, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData **} src_data
 * @return {*}
 */
S32 get_vdec_result_2(ALBaseContext *base_context, MppData **src_data);

/**
 * @description:
 * @param {ALBaseContext} *base_context
 * @param {MppData *} src_data
 * @return {*}
 */
S32 return_vdec_result(ALBaseContext *base_context, MppData *src_data);

#ifdef __cplusplus
};
#endif

#endif /*_MPP_VDEC_H*/
