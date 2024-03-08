/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-02-01 09:34:03
 * @LastEditTime: 2024-03-08 09:10:14
 * @Description: abstract layer interface of video decode
 */

#ifndef _AL_INTERFACE_DEC_H_
#define _AL_INTERFACE_DEC_H_

#include "al_interface_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @description: create a context for video decoder
 * @return {*}: the base context of video decoder
 */
ALBaseContext *al_dec_create();

/**
 * @description: init the video decoder with paramters
 * @param {ALBaseContext} *ctx: the base context of video decoder
 * @param {MppVdecPara} *para: the paramters set from APP
 * @return {*}: 0 on sucess, else error code.
 */
RETURN al_dec_init(ALBaseContext *ctx, MppVdecPara *para);

/**
 * @description:
 * @param {ALBaseContext} *ctx: the base context of video decoder
 * @param {MppVdecPara} **para: the paramters set from APP
 * @return {*}: 0 on sucess, else error code.
 */
RETURN al_dec_getparam(ALBaseContext *ctx, MppVdecPara **para);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 al_dec_decode(ALBaseContext *ctx, MppData *sink_data);

/***
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *sink_data
 * @param {MppData} *src_data
 * @return {*}
 */
S32 al_dec_process(ALBaseContext *ctx, MppData *sink_data, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 al_dec_get_output_frame(ALBaseContext *ctx, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 al_dec_request_output_frame(ALBaseContext *ctx, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} **src_data
 * @return {*}
 */
S32 al_dec_request_output_frame_2(ALBaseContext *ctx, MppData **src_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 al_dec_return_output_frame(ALBaseContext *ctx, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @return {*}
 */
S32 al_dec_flush(ALBaseContext *ctx);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @return {*}
 */
S32 al_dec_reset(ALBaseContext *ctx);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @return {*}
 */
void al_dec_destory(ALBaseContext *ctx);

#ifdef __cplusplus
};
#endif

#endif /*_AL_INTERFACE_DEC_H_*/