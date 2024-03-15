/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:40
 * @LastEditTime: 2023-12-13 20:18:37
 * @Description: video encode plugin for k1 JPU
 */

//#define ENABLE_DEBUG 1

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "al_interface_enc.h"
#include "log.h"

#define MODULE_TAG "jpuenc"

typedef struct _ALK1JpuEncContext ALK1JpuEncContext;

struct _ALK1JpuEncContext {
  ALEncBaseContext stAlEncBaseContext;
};

ALBaseContext *al_enc_create() {
  ALK1JpuEncContext *enc_context =
      (ALK1JpuEncContext *)malloc(sizeof(ALK1JpuEncContext));
  if (!enc_context) {
    error("can not malloc ALK1JpuEncContext, please check!");
    return NULL;
  }
  memset(enc_context, 0, sizeof(ALK1JpuEncContext));

  return &(enc_context->stAlEncBaseContext.stAlBaseContext);
}

RETURN al_enc_init(ALBaseContext *ctx, MppVencPara *para) {
  ALK1JpuEncContext *enc_context = (ALK1JpuEncContext *)ctx;
  S32 ret = 0;

  return MPP_OK;
}

S32 al_enc_set_para(ALBaseContext *ctx, MppVencPara *para) {
  ALK1JpuEncContext *enc_context = (ALK1JpuEncContext *)ctx;
  S32 ret = 0;

  return MPP_OK;
}

S32 al_enc_encode(ALBaseContext *ctx, MppData *sink_data) {
  ALK1JpuEncContext *enc_context = (ALK1JpuEncContext *)ctx;

  return MPP_OK;
}

S32 al_enc_request_output_stream(ALBaseContext *ctx, MppData *src_data) {
  ALK1JpuEncContext *enc_context = (ALK1JpuEncContext *)ctx;
  S32 ret = MPP_OK;

  return ret;
}
S32 al_enc_return_output_stream(ALBaseContext *ctx, MppData *src_data) {
  S32 ret = MPP_OK;

  return ret;
}

void al_enc_destory(ALBaseContext *ctx) {
  ALK1JpuEncContext *enc_context = (ALK1JpuEncContext *)ctx;
  if (!ctx) {
    error("No need to destory, return !");
    return;
  }
}
