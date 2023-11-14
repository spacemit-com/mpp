/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-02-01 09:34:01
 * @LastEditTime: 2023-02-01 10:44:57
 * @Description: abstract layer interface of video encode
 */

#ifndef _AL_INTERFACE_ENC_H_
#define _AL_INTERFACE_ENC_H_

#include "al_interface_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @description:
 * @return {*}
 */
ALBaseContext *al_enc_create();

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppVencPara} *para
 * @return {*}
 */
RETURN al_enc_init(ALBaseContext *ctx, MppVencPara *para);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppVencPara} *para
 * @return {*}
 */
S32 al_enc_set_para(ALBaseContext *ctx, MppVencPara *para);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 al_enc_send_input_frame(ALBaseContext *ctx, MppData *sink_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 al_enc_return_input_frame(ALBaseContext *ctx, MppData *sink_data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *sink_data
 * @return {*}
 */
S32 al_enc_encode(ALBaseContext *ctx, MppData *sink_data);

/***
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *sink_data
 * @param {MppData} *src_data
 * @return {*}
 */
S32 al_enc_process(ALBaseContext *ctx, MppData *sink_data, MppData *src_data);

/***
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *src_Data
 * @return {*}
 */
S32 al_enc_get_output_stream(ALBaseContext *ctx, MppData *src_Data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *src_Data
 * @return {*}
 */
S32 al_enc_request_output_stream(ALBaseContext *ctx, MppData *src_Data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @param {MppData} *src_Data
 * @return {*}
 */
S32 al_enc_return_output_stream(ALBaseContext *ctx, MppData *src_Data);

/**
 * @description:
 * @param {ALBaseContext} *ctx
 * @return {*}
 */
void al_enc_destory(ALBaseContext *ctx);

#ifdef __cplusplus
};
#endif

#endif /*_AL_INTERFACE_ENC_H_*/
