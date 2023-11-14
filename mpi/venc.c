/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:36
 * @LastEditTime: 2023-11-14 11:10:04
 * @Description: MPP VENC API, use these API to do video encode
 *               from frame(YUV420) to stream(H.264 etc.)
 */

#define ENABLE_DEBUG 1

#include "venc.h"

#include <stdio.h>
#include <stdlib.h>

#include "al_interface_enc.h"
#include "log.h"

#define MODULE_TAG "mpp_venc"

ALBaseContext *(*enc_create)();
void (*enc_init)(ALBaseContext *ctx, MppVencPara *para);
S32 (*enc_set_para)(ALBaseContext *ctx, MppVencPara *para);
S32 (*enc_return_input_frame)(ALBaseContext *ctx, MppData *sink_Data);
S32 (*enc_send_input_frame)(ALBaseContext *ctx, MppData *sink_Data);
S32 (*enc_encode)(ALBaseContext *ctx, MppData *sink_data);
S32 (*enc_process)(ALBaseContext *ctx, MppData *sink_data, MppData *src_data);
S32 (*enc_get_output_stream)(ALBaseContext *ctx, MppData *src_Data);
S32 (*enc_request_output_stream)(ALBaseContext *ctx, MppData *src_Data);
S32 (*enc_return_output_stream)(ALBaseContext *ctx, MppData *src_Data);
void (*enc_destory)(ALBaseContext *ctx);

MppVencCtx *VENC_CreateChannel() {
  MppVencCtx *ctx = (MppVencCtx *)malloc(sizeof(MppVencCtx));
  if (!ctx) {
    error("can not create MppVencCtx, please check !");
    return NULL;
  }

  return ctx;
}

S32 VENC_Init(MppVencCtx *ctx) {
  ctx->pModule = module_init(ctx->eCodecType);

  enc_create = (ALBaseContext * (*)())
      dlsym(module_get_so_path(ctx->pModule), "al_enc_create");
  enc_init = (void (*)(ALBaseContext * ctx, MppVencPara * para))
      dlsym(module_get_so_path(ctx->pModule), "al_enc_init");
  enc_set_para = (S32(*)(ALBaseContext * ctx, MppVencPara * para))
      dlsym(module_get_so_path(ctx->pModule), "al_enc_set_para");
  enc_return_input_frame = (S32(*)(ALBaseContext * ctx, MppData * sink_Data))
      dlsym(module_get_so_path(ctx->pModule), "al_enc_return_input_frame");
  enc_send_input_frame = (S32(*)(ALBaseContext * ctx, MppData * sink_Data))
      dlsym(module_get_so_path(ctx->pModule), "al_enc_send_input_frame");
  enc_encode = (S32(*)(ALBaseContext * ctx, MppData * sink_data))
      dlsym(module_get_so_path(ctx->pModule), "al_enc_encode");
  enc_process =
      (S32(*)(ALBaseContext * ctx, MppData * sink_data, MppData * src_data))
          dlsym(module_get_so_path(ctx->pModule), "al_enc_process");
  enc_get_output_stream = (S32(*)(ALBaseContext * ctx, MppData * src_Data))
      dlsym(module_get_so_path(ctx->pModule), "al_enc_get_output_stream");
  enc_request_output_stream = (S32(*)(ALBaseContext * ctx, MppData * src_Data))
      dlsym(module_get_so_path(ctx->pModule), "al_enc_request_output_stream");
  enc_return_output_stream = (S32(*)(ALBaseContext * ctx, MppData * src_Data))
      dlsym(module_get_so_path(ctx->pModule), "al_enc_return_output_stream");
  enc_destory = (void (*)(ALBaseContext * ctx))
      dlsym(module_get_so_path(ctx->pModule), "al_enc_destory");

  ctx->pNode.pAlBaseContext = enc_create();

  enc_init(ctx->pNode.pAlBaseContext, &(ctx->stVencPara));
  return 0;
}

S32 VENC_SetParam(MppVencCtx *ctx, MppVencPara *para) {
  S32 ret = enc_set_para(ctx->pNode.pAlBaseContext, para);

  return ret;
}

S32 VENC_GetParam(MppVencCtx *ctx, MppVencPara *para) { return 0; }

S32 VENC_SendInputFrame(MppVencCtx *ctx, MppData *sink_data) {
  S32 ret = 0;
  ret = enc_send_input_frame(ctx->pNode.pAlBaseContext, sink_data);

  return ret;
}

S32 VENC_ReturnInputFrame(MppVencCtx *ctx, MppData *sink_data) {
  S32 ret = 0;
  ret = enc_return_input_frame(ctx->pNode.pAlBaseContext, sink_data);

  return ret;
}

S32 handle_venc_data(ALBaseContext *base_context, MppData *sink_data) {
  S32 ret = 0;
  ret = enc_encode(base_context, sink_data);

  return ret;
}

S32 VENC_Encode(MppVencCtx *ctx, MppData *sink_data) {
  S32 ret = 0;
  ret = handle_venc_data(ctx->pNode.pAlBaseContext, sink_data);
  return ret;
}

S32 process_venc_data(ALBaseContext *base_context, MppData *sink_data,
                      MppData *src_data) {
  S32 ret = 0;
  ret = enc_process(base_context, sink_data, src_data);

  return ret;
}

S32 VENC_Process(MppVencCtx *ctx, MppData *sink_data, MppData *src_data) {
  S32 ret = 0;
  ret = process_venc_data(ctx->pNode.pAlBaseContext, sink_data, src_data);
  return ret;
}

S32 get_venc_result_sync(ALBaseContext *base_context, MppData *src_data) {
  S32 ret = 0;
  ret = enc_get_output_stream(base_context, src_data);

  return ret;
}

S32 VENC_GetOutputStreamBuffer(MppVencCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = get_venc_result_sync(ctx->pNode.pAlBaseContext, src_data);

  return ret;
}

S32 get_venc_result(ALBaseContext *base_context, MppData *src_data) {
  S32 ret = 0;
  ret = enc_request_output_stream(base_context, src_data);

  return ret;
}

S32 VENC_RequestOutputStreamBuffer(MppVencCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = get_venc_result(ctx->pNode.pAlBaseContext, src_data);

  return ret;
}

S32 return_venc_result(ALBaseContext *base_context, MppData *src_data) {
  S32 ret = 0;
  ret = enc_return_output_stream(base_context, src_data);

  return ret;
}

S32 VENC_ReturnOutputStreamBuffer(MppVencCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = return_venc_result(ctx->pNode.pAlBaseContext, src_data);

  return ret;
}

S32 VENC_DestoryChannel(MppVencCtx *ctx) {
  if (ctx) {
    enc_destory(ctx->pNode.pAlBaseContext);
    module_destory(ctx->pModule);
    free(ctx);
  }
  return 0;
}

S32 VENC_ResetChannel(MppVencCtx *ctx) { return 0; }
