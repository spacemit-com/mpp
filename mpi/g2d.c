/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:36
 * @LastEditTime: 2024-04-28 15:21:11
 * @Description: MPP G2D API, use these API to do frame(YUV420) format
 * conversion
 */

#include "g2d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "module.h"

#define MODULE_TAG "mpp_g2d"

ALBaseContext *(*g2d_create)();
void (*g2d_init)(ALBaseContext *ctx, MppG2dPara *para);
S32 (*g2d_set_para)(ALBaseContext *ctx, MppG2dPara *para);
S32 (*g2d_send_input_frame)(ALBaseContext *ctx, MppData *sink_data);
S32 (*g2d_return_input_frame)(ALBaseContext *ctx, MppData *sink_data);
S32 (*g2d_convert)(ALBaseContext *ctx, MppData *sink_data);
S32 (*g2d_process)(ALBaseContext *ctx, MppData *sink_data, MppData *src_data);
S32 (*g2d_get_output_frame)(ALBaseContext *ctx, MppData *src_data);
S32 (*g2d_request_output_frame)(ALBaseContext *ctx, MppData *src_data);
S32 (*g2d_return_output_frame)(ALBaseContext *ctx, MppData *src_data);
void (*g2d_destory)(ALBaseContext *ctx);

MppG2dCtx *G2D_CreateChannel() {
  debug("G2D_CreateChannel start!");
  MppG2dCtx *ctx = (MppG2dCtx *)malloc(sizeof(MppG2dCtx));
  if (!ctx) {
    error("can not create MppG2dCtx, please check !");
    return NULL;
  }
  memset(ctx, 0, sizeof(MppG2dCtx));
  debug("G2D_CreateChannel finish!");

  return ctx;
}

S32 G2D_Init(MppG2dCtx *ctx) {
  ctx->pModule = module_init(ctx->eVpsType);

  g2d_create = (ALBaseContext * (*)())
      dlsym(module_get_so_path(ctx->pModule), "al_g2d_create");
  g2d_init = (void (*)(ALBaseContext * ctx, MppG2dPara * para))
      dlsym(module_get_so_path(ctx->pModule), "al_g2d_init");
  g2d_set_para = (S32(*)(ALBaseContext * ctx, MppG2dPara * para))
      dlsym(module_get_so_path(ctx->pModule), "al_g2d_set_para");
  g2d_send_input_frame = (S32(*)(ALBaseContext * ctx, MppData * sink_data))
      dlsym(module_get_so_path(ctx->pModule), "al_g2d_send_input_frame");
  g2d_return_input_frame = (S32(*)(ALBaseContext * ctx, MppData * sink_data))
      dlsym(module_get_so_path(ctx->pModule), "al_g2d_return_input_frame");
  g2d_convert = (S32(*)(ALBaseContext * ctx, MppData * sink_data))
      dlsym(module_get_so_path(ctx->pModule), "al_g2d_convert");
  g2d_process =
      (S32(*)(ALBaseContext * ctx, MppData * sink_data, MppData * src_data))
          dlsym(module_get_so_path(ctx->pModule), "al_g2d_process");
  g2d_get_output_frame = (S32(*)(ALBaseContext * ctx, MppData * src_data))
      dlsym(module_get_so_path(ctx->pModule), "al_g2d_get_output_frame");
  g2d_request_output_frame = (S32(*)(ALBaseContext * ctx, MppData * src_data))
      dlsym(module_get_so_path(ctx->pModule), "al_g2d_request_output_frame");
  g2d_return_output_frame = (S32(*)(ALBaseContext * ctx, MppData * src_data))
      dlsym(module_get_so_path(ctx->pModule), "al_g2d_return_output_frame");
  g2d_destory = (void (*)(ALBaseContext * ctx))
      dlsym(module_get_so_path(ctx->pModule), "al_g2d_destory");

  ctx->pNode.pAlBaseContext = g2d_create();

  g2d_init(ctx->pNode.pAlBaseContext, &(ctx->stG2dPara));
  return 0;
}

S32 G2D_SetParam(MppG2dCtx *ctx, MppG2dPara *para) {
  S32 ret = g2d_set_para(ctx->pNode.pAlBaseContext, para);

  return ret;
}

S32 G2D_GetParam(MppG2dCtx *ctx, MppG2dPara *para) { return 0; }

S32 G2D_SendInputFrame(MppG2dCtx *ctx, MppData *sink_data) {
  S32 ret = 0;
  ret = g2d_send_input_frame(ctx->pNode.pAlBaseContext, sink_data);

  return ret;
}

S32 G2D_ReturnInputFrame(MppG2dCtx *ctx, MppData *sink_data) {
  S32 ret = 0;
  ret = g2d_return_input_frame(ctx->pNode.pAlBaseContext, sink_data);

  return ret;
}

S32 handle_g2d_data(ALBaseContext *base_context, MppData *sink_data) {
  S32 ret = 0;
  ret = g2d_convert(base_context, sink_data);

  return ret;
}

S32 G2D_Convert(MppG2dCtx *ctx, MppData *sink_data) {
  S32 ret = 0;
  ret = handle_g2d_data(ctx->pNode.pAlBaseContext, sink_data);
  return ret;
}

S32 handle_g2d_data_sync(ALBaseContext *base_context, MppData *sink_data,
                         MppData *src_data) {
  S32 ret = 0;
  ret = g2d_process(base_context, sink_data, src_data);

  return ret;
}

S32 G2D_Process(MppG2dCtx *ctx, MppData *sink_data, MppData *src_data) {
  S32 ret = 0;
  ret = handle_g2d_data_sync(ctx->pNode.pAlBaseContext, sink_data, src_data);
  return ret;
}

S32 get_g2d_result_sync(ALBaseContext *base_context, MppData *src_data) {
  S32 ret = 0;
  ret = g2d_get_output_frame(base_context, src_data);

  return ret;
}

S32 G2D_GetOutputFrame(MppG2dCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = get_g2d_result_sync(ctx->pNode.pAlBaseContext, src_data);

  return ret;
}

S32 get_g2d_result(ALBaseContext *base_context, MppData *src_data) {
  S32 ret = 0;
  ret = g2d_request_output_frame(base_context, src_data);

  return ret;
}

S32 G2D_RequestOutputFrame(MppG2dCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = get_g2d_result(ctx->pNode.pAlBaseContext, src_data);

  return ret;
}

S32 return_g2d_result(ALBaseContext *base_context, MppData *src_data) {
  S32 ret = 0;
  ret = g2d_return_output_frame(base_context, src_data);

  return ret;
}

S32 G2D_ReturnOutputFrame(MppG2dCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = return_g2d_result(ctx->pNode.pAlBaseContext, src_data);

  return ret;
}

S32 G2D_DestoryChannel(MppG2dCtx *ctx) {
  g2d_destory(ctx->pNode.pAlBaseContext);

  module_destory(ctx->pModule);
  return 0;
}

S32 G2D_ResetChannel(MppG2dCtx *ctx) { return 0; }
