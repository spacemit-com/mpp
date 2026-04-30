/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:36
 * @LastEditTime: 2026-04-19 10:00:00
 * @Description: MPP VENC legacy context API (venc_ctx_*).
 *               Wraps AL encoder plugin via dlsym.
 */

#define ENABLE_DEBUG 0

#include "venc.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "al_interface_enc.h"
#include "log.h"

#define MODULE_TAG "mpp_venc"

static ALBaseContext *(*enc_create)();
static void (*enc_init)(ALBaseContext *ctx, MppVencPara *para);
static S32 (*enc_set_para)(ALBaseContext *ctx, MppVencCmd cmd, void *para);
static S32 (*enc_return_input_frame)(ALBaseContext *ctx, MppData *sink_Data);
static S32 (*enc_send_input_frame)(ALBaseContext *ctx, MppData *sink_Data);
static S32 (*enc_encode)(ALBaseContext *ctx, MppData *sink_data);
static S32 (*enc_process)(ALBaseContext *ctx, MppData *sink_data,
                          MppData *src_data);
static S32 (*enc_get_output_stream)(ALBaseContext *ctx, MppData *src_Data);
static S32 (*enc_request_output_stream)(ALBaseContext *ctx, MppData *src_Data);
static S32 (*enc_return_output_stream)(ALBaseContext *ctx, MppData *src_Data);
static S32 (*enc_flush)(ALBaseContext *ctx);
static void (*enc_destory)(ALBaseContext *ctx);

MppVencCtx *venc_ctx_create(void) {
  MppVencCtx *ctx = (MppVencCtx *)malloc(sizeof(MppVencCtx));
  if (!ctx) {
    error("can not create MppVencCtx, please check! (%s)", strerror(errno));
    return NULL;
  }

  memset(ctx, 0, sizeof(MppVencCtx));

  debug("create VENC Channel success!");
  return ctx;
}

S32 venc_ctx_init(MppVencCtx *ctx) {
  void *handle;

  ctx->pModule = module_init(ctx->eCodecType);
  if (!ctx->pModule) {
    error("module_init failed for codec type %d", (int)ctx->eCodecType);
    return MPP_INIT_FAILED;
  }

  handle = module_get_so_path(ctx->pModule);
  if (!handle) {
    error("module handle is NULL after module_init");
    module_destory(ctx->pModule);
    ctx->pModule = NULL;
    return MPP_INIT_FAILED;
  }

  dlerror();
  enc_create = (ALBaseContext * (*)()) dlsym(handle, "al_enc_create");
  enc_init = (void (*)(ALBaseContext * ctx, MppVencPara * para))
      dlsym(handle, "al_enc_init");
  enc_set_para = (S32(*)(ALBaseContext * ctx, MppVencCmd cmd, void * para))
      dlsym(handle, "al_enc_set_para");
  enc_return_input_frame = (S32(*)(ALBaseContext * ctx, MppData * sink_Data))
      dlsym(handle, "al_enc_return_input_frame");
  enc_send_input_frame = (S32(*)(ALBaseContext * ctx, MppData * sink_Data))
      dlsym(handle, "al_enc_send_input_frame");
  enc_encode = (S32(*)(ALBaseContext * ctx, MppData * sink_data))
      dlsym(handle, "al_enc_encode");
  enc_process =
      (S32(*)(ALBaseContext * ctx, MppData * sink_data, MppData * src_data))
          dlsym(handle, "al_enc_process");
  enc_get_output_stream = (S32(*)(ALBaseContext * ctx, MppData * src_Data))
      dlsym(handle, "al_enc_get_output_stream");
  enc_request_output_stream = (S32(*)(ALBaseContext * ctx, MppData * src_Data))
      dlsym(handle, "al_enc_request_output_stream");
  enc_return_output_stream = (S32(*)(ALBaseContext * ctx, MppData * src_Data))
      dlsym(handle, "al_enc_return_output_stream");
  enc_flush = (S32(*)(ALBaseContext * ctx))
      dlsym(handle, "al_enc_flush");
  enc_destory = (void (*)(ALBaseContext * ctx))
      dlsym(handle, "al_enc_destory");

  if (!enc_create || !enc_init || !enc_encode || !enc_destory) {
    error("required encoder symbols missing from plugin "
          "(create/init/encode/destroy)");
    module_destory(ctx->pModule);
    ctx->pModule = NULL;
    return MPP_INIT_FAILED;
  }

  ctx->pNode.pAlBaseContext = enc_create();
  if (!ctx->pNode.pAlBaseContext) {
    error("al_enc_create returned NULL");
    module_destory(ctx->pModule);
    ctx->pModule = NULL;
    return MPP_INIT_FAILED;
  }

  enc_init(ctx->pNode.pAlBaseContext, &(ctx->stVencPara));
  debug("init VENC Channel success");

  return MPP_OK;
}

S32 venc_ctx_set_param(MppVencCtx *ctx, MppVencCmd cmd, void *para) {
  S32 ret = enc_set_para(ctx->pNode.pAlBaseContext, cmd, para);
  return ret;
}

S32 venc_ctx_get_param(MppVencCtx *ctx, MppVencPara *para) {
  /* TODO: implement get param from AL layer */
  return MPP_OK;
}

S32 venc_ctx_send_input_frame(MppVencCtx *ctx, MppData *sink_data) {
  S32 ret = 0;
  ret = enc_send_input_frame(ctx->pNode.pAlBaseContext, sink_data);
  return ret;
}

S32 venc_ctx_return_input_frame(MppVencCtx *ctx, MppData *sink_data) {
  S32 ret = 0;
  ret = enc_return_input_frame(ctx->pNode.pAlBaseContext, sink_data);
  return ret;
}

S32 handle_venc_data(ALBaseContext *base_context, MppData *sink_data) {
  S32 ret = 0;
  ret = enc_encode(base_context, sink_data);
  return ret;
}

S32 venc_ctx_encode(MppVencCtx *ctx, MppData *sink_data) {
  S32 ret = 0;
  ret = handle_venc_data(ctx->pNode.pAlBaseContext, sink_data);
  debug("encode one frame, ret = %d", ret);
  return ret;
}

S32 process_venc_data(ALBaseContext *base_context, MppData *sink_data,
                      MppData *src_data) {
  S32 ret = 0;
  ret = enc_process(base_context, sink_data, src_data);
  return ret;
}

S32 venc_ctx_process(MppVencCtx *ctx, MppData *sink_data, MppData *src_data) {
  S32 ret = 0;
  ret = process_venc_data(ctx->pNode.pAlBaseContext, sink_data, src_data);
  return ret;
}

S32 get_venc_result_sync(ALBaseContext *base_context, MppData *src_data) {
  S32 ret = 0;
  ret = enc_get_output_stream(base_context, src_data);
  return ret;
}

S32 venc_ctx_get_output_stream(MppVencCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = get_venc_result_sync(ctx->pNode.pAlBaseContext, src_data);
  return ret;
}

S32 get_venc_result(ALBaseContext *base_context, MppData *src_data) {
  S32 ret = 0;
  ret = enc_request_output_stream(base_context, src_data);
  return ret;
}

S32 venc_ctx_request_output_stream(MppVencCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = get_venc_result(ctx->pNode.pAlBaseContext, src_data);
  return ret;
}

S32 return_venc_result(ALBaseContext *base_context, MppData *src_data) {
  S32 ret = 0;
  ret = enc_return_output_stream(base_context, src_data);
  return ret;
}

S32 venc_ctx_return_output_stream(MppVencCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = return_venc_result(ctx->pNode.pAlBaseContext, src_data);
  return ret;
}

S32 venc_ctx_flush(MppVencCtx *ctx) {
  S32 ret = 0;

  debug("begin flush!");
  ret = enc_flush(ctx->pNode.pAlBaseContext);
  debug("finish flush ret = %d", ret);

  return ret;
}

S32 venc_ctx_destroy(MppVencCtx *ctx) {
  if (!ctx) {
    error("input para ctx is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (ctx->pModule == NULL) {
    info("module not init!");
    free(ctx);
    return 0;
  }

  if (enc_destory) enc_destory(ctx->pNode.pAlBaseContext);
  debug("finish destory encoder");

  module_destory(ctx->pModule);
  debug("finish destory module");

  free(ctx);

  return MPP_OK;
}

S32 venc_ctx_reset(MppVencCtx *ctx) {
  /* TODO: implement encoder reset if AL layer supports it */
  return MPP_OK;
}
