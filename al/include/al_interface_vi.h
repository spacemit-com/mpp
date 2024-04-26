/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-04-25 19:31:10
 * @LastEditTime: 2024-04-26 09:48:58
 * @FilePath: \mpp\al\include\al_interface_vi.h
 * @Description:
 */

#ifndef _AL_INTERFACE_VI_H_
#define _AL_INTERFACE_VI_H_

#include "al_interface_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @description: create a context for video input
 * @return {*}: the base context of video input
 */
ALBaseContext *al_vi_create();

/**
 * @description: init the video input with paramters
 * @param {ALBaseContext} *ctx: the base context of video input
 * @param {MppViPara} *para: the paramters set from APP
 * @return {*}: 0 on sucess, else error code.
 */
RETURN al_vi_init(ALBaseContext *ctx, MppViPara *para);

/**
 * @description:
 * @param {ALBaseContext} *ctx: the base context of video input
 * @param {MppViPara} **para: the paramters set from APP
 * @return {*}: 0 on sucess, else error code.
 */
RETURN al_vi_getparam(ALBaseContext *ctx, MppViPara **para);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 al_vi_process(ALBaseContext *ctx, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 al_vi_request_output_frame(ALBaseContext *ctx, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *src_data
 * @return {*}
 */
S32 al_vi_return_output_frame(ALBaseContext *ctx, MppData *src_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @return {*}
 */
void al_vi_destory(ALBaseContext *ctx);

#ifdef __cplusplus
};
#endif

#endif /*_AL_INTERFACE_VI_H_*/
