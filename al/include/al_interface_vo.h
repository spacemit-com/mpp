/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-03-15 11:44:05
 * @LastEditTime: 2024-03-15 11:47:27
 * @FilePath: \mpp\al\include\al_interface_vo.h
 * @Description:
 */

#ifndef _AL_INTERFACE_VO_H_
#define _AL_INTERFACE_VO_H_

#include "al_interface_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @description: create a context for video output
 * @return {*}: the base context of video output
 */
ALBaseContext *al_vo_create();

/**
 * @description: init the video output with paramters
 * @param {ALBaseContext} *ctx: the base context of video output
 * @param {MppVoPara} *para: the paramters set from APP
 * @return {*}: 0 on sucess, else error code.
 */
RETURN al_vo_init(ALBaseContext *ctx, MppVoPara *para);

/**
 * @description:
 * @param {ALBaseContext} *ctx: the base context of video output
 * @param {MppVoPara} **para: the paramters set from APP
 * @return {*}: 0 on sucess, else error code.
 */
RETURN al_vo_getparam(ALBaseContext *ctx, MppVoPara **para);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 al_vo_process(ALBaseContext *ctx, MppData *sink_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @return {*}
 */
void al_vo_destory(ALBaseContext *ctx);

#ifdef __cplusplus
};
#endif

#endif /*_AL_INTERFACE_VO_H_*/
