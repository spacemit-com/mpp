/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-02-01 09:34:03
 * @LastEditTime: 2023-02-02 14:55:17
 * @Description: abstract layer interface of video g2d
 */

#ifndef _AL_INTERFACE_G2D_H_
#define _AL_INTERFACE_G2D_H_

#include "al_interface_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @description:
 * @return {*}
 */
ALBaseContext *al_g2d_create();

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @return {*}
 */
RETURN al_g2d_init(ALBaseContext *ctx, MppG2dPara *para);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppG2dPara} *para
 * @return {*}
 */
S32 al_g2d_set_para(ALBaseContext *ctx, MppG2dPara *para);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 al_g2d_send_input_frame(ALBaseContext *ctx, MppData *sink_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 al_g2d_return_input_frame(ALBaseContext *ctx, MppData *sink_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 al_g2d_convert(ALBaseContext *ctx, MppData *sink_data);

/***
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *sink_data
 * @param {MppData} *src_data
 * @return {*}
 */
S32 al_g2d_process(ALBaseContext *ctx, MppData *sink_data, MppData *src_data);

/***
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 al_g2d_get_output_frame(ALBaseContext *ctx, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 al_g2d_request_output_frame(ALBaseContext *ctx, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 al_g2d_return_output_frame(ALBaseContext *ctx, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @return {*}
 */
void al_g2d_destory(ALBaseContext *ctx);

#ifdef __cplusplus
};
#endif

#endif /*_AL_INTERFACE_G2D_H_*/
