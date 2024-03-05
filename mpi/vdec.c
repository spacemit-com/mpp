/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-18 11:46:03
 * @LastEditTime: 2024-01-10 14:36:25
 * @Description: MPP VDEC API, use these API to do video decode
 *               from stream(H.264 etc.) to frame(YUV420)
 */

#define ENABLE_DEBUG 0

#include "vdec.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "al_interface_dec.h"
#include "log.h"

#define MODULE_TAG "mpp_vdec"

ALBaseContext *(*dec_create)();
S32 (*dec_init)(ALBaseContext *ctx, MppVdecPara *para);
S32 (*dec_getparam)(ALBaseContext *ctx, MppVdecPara **para);
S32 (*dec_request_input_stream)(ALBaseContext *ctx, MppData *sink_data);
S32 (*dec_return_input_stream)(ALBaseContext *ctx, MppData *sink_data);
S32 (*dec_decode)(ALBaseContext *ctx, MppData *sink_data);
S32 (*dec_process)(ALBaseContext *ctx, MppData *sink_data, MppData *src_data);
S32 (*dec_get_output_frame)(ALBaseContext *ctx, MppData *src_data);
S32 (*dec_request_output_frame)(ALBaseContext *ctx, MppData *src_data);
S32 (*dec_request_output_frame_2)(ALBaseContext *ctx, MppData **src_data);
S32 (*dec_return_output_frame)(ALBaseContext *ctx, MppData *src_data);
S32 (*dec_flush)(ALBaseContext *ctx);
S32 (*dec_reset)(ALBaseContext *ctx);
void (*dec_destory)(ALBaseContext *ctx);

MppVdecCtx *VDEC_CreateChannel() {
  MppVdecCtx *ctx = (MppVdecCtx *)malloc(sizeof(MppVdecCtx));
  if (!ctx) {
    error("can not create MppVdecCtx, please check! (%s)", strerror(errno));
    return NULL;
  }

  memset(ctx, 0, sizeof(MppVdecCtx));
  VDEC_GetDefaultParam(ctx);

  debug("create VDEC Channel success!");
  return ctx;
}

S32 VDEC_Init(MppVdecCtx *ctx) {
  S32 ret = 0;

  ctx->pModule = module_init(ctx->eCodecType);

  dec_create = (ALBaseContext * (*)())
      dlsym(module_get_so_path(ctx->pModule), "al_dec_create");
  dec_init = (S32(*)(ALBaseContext * ctx, MppVdecPara * para))
      dlsym(module_get_so_path(ctx->pModule), "al_dec_init");
  dec_getparam = (S32(*)(ALBaseContext * ctx, MppVdecPara * *para))
      dlsym(module_get_so_path(ctx->pModule), "al_dec_getparam");
  dec_request_input_stream = (S32(*)(ALBaseContext * ctx, MppData * sink_data))
      dlsym(module_get_so_path(ctx->pModule), "al_dec_request_input_stream");
  dec_return_input_stream = (S32(*)(ALBaseContext * ctx, MppData * sink_data))
      dlsym(module_get_so_path(ctx->pModule), "al_dec_return_input_stream");
  dec_decode = (S32(*)(ALBaseContext * ctx, MppData * sink_data))
      dlsym(module_get_so_path(ctx->pModule), "al_dec_decode");
  dec_process =
      (S32(*)(ALBaseContext * ctx, MppData * sink_data, MppData * src_data))
          dlsym(module_get_so_path(ctx->pModule), "al_dec_process");
  dec_get_output_frame = (S32(*)(ALBaseContext * ctx, MppData * src_data))
      dlsym(module_get_so_path(ctx->pModule), "al_dec_get_output_frame");
  dec_request_output_frame = (S32(*)(ALBaseContext * ctx, MppData * src_data))
      dlsym(module_get_so_path(ctx->pModule), "al_dec_request_output_frame");
  dec_request_output_frame_2 =
      (S32(*)(ALBaseContext * ctx, MppData * *src_data)) dlsym(
          module_get_so_path(ctx->pModule), "al_dec_request_output_frame_2");
  dec_return_output_frame = (S32(*)(ALBaseContext * ctx, MppData * src_data))
      dlsym(module_get_so_path(ctx->pModule), "al_dec_return_output_frame");
  dec_destory = (void (*)(ALBaseContext * ctx))
      dlsym(module_get_so_path(ctx->pModule), "al_dec_destory");
  dec_flush = (S32(*)(ALBaseContext * ctx))
      dlsym(module_get_so_path(ctx->pModule), "al_dec_flush");
  dec_reset = (S32(*)(ALBaseContext * ctx))
      dlsym(module_get_so_path(ctx->pModule), "al_dec_reset");

  ctx->pNode.pAlBaseContext = dec_create();

  ret = dec_init(ctx->pNode.pAlBaseContext, &(ctx->stVdecPara));
  debug("init VDEC Channel, ret = %d", ret);

  return ret;
}

S32 VDEC_SetParam(MppVdecCtx *ctx) {
  error("VDEC_SetParam is not supported yet, return MPP_OK directly!");
  return MPP_OK;
}

S32 VDEC_GetParam(MppVdecCtx *ctx, MppVdecPara **stVdecPara) {
  S32 ret = 0;
  ret = dec_getparam(ctx->pNode.pAlBaseContext, stVdecPara);
  debug("get VDEC parameters, ret = %d", ret);

  return ret;
}

S32 VDEC_GetDefaultParam(MppVdecCtx *ctx) {
  ctx->stVdecPara.eFrameBufferType = MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL;
  // ctx->stVdecPara.bScaleDownEn = MPP_FALSE;
  ctx->stVdecPara.nHorizonScaleDownRatio = 1;
  ctx->stVdecPara.nVerticalScaleDownRatio = 1;
  // ctx->stVdecPara.bRotationEn = MPP_FALSE;
  ctx->stVdecPara.nRotateDegree = 0;
  ctx->stVdecPara.bThumbnailMode = 0;
  ctx->stVdecPara.bIsInterlaced = MPP_FALSE;
  ctx->stVdecPara.bIsFrameReordering = MPP_TRUE;
  ctx->stVdecPara.bIgnoreStreamHeaders = MPP_FALSE;
  ctx->stVdecPara.eOutputPixelFormat = PIXEL_FORMAT_I420;
  ctx->stVdecPara.bNoBFrames = MPP_FALSE;
  ctx->stVdecPara.bDisable3D = MPP_FALSE;
  ctx->stVdecPara.bSupportMaf = MPP_FALSE;
  ctx->stVdecPara.bDispErrorFrame = MPP_TRUE;
  ctx->stVdecPara.bInputBlockModeEnable = MPP_FALSE;
  ctx->stVdecPara.bOutputBlockModeEnable = MPP_TRUE;

  return MPP_OK;
}

S32 handle_vdec_data(ALBaseContext *base_context, MppData *sink_data) {
  S32 ret = 0;
  ret = dec_decode(base_context, sink_data);

  return ret;
}

S32 VDEC_Decode(MppVdecCtx *ctx, MppData *sink_data) {
  S32 ret = 0;
  ret = handle_vdec_data(ctx->pNode.pAlBaseContext, sink_data);
  debug("decode one packet, ret = %d", ret);

  return ret;
}

S32 process_vdec_data(ALBaseContext *base_context, MppData *sink_data,
                      MppData *src_data) {
  S32 ret = 0;
  ret = dec_process(base_context, sink_data, src_data);

  return ret;
}

S32 VDEC_Process(MppVdecCtx *ctx, MppData *sink_data, MppData *src_data) {
  S32 ret = 0;
  ret = process_vdec_data(ctx->pNode.pAlBaseContext, sink_data, src_data);

  return ret;
}

S32 get_vdec_result_sync(ALBaseContext *base_context, MppData *src_data) {
  S32 ret = 0;
  ret = dec_get_output_frame(base_context, src_data);

  return ret;
}

S32 VDEC_GetOutputFrame(MppVdecCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = get_vdec_result_sync(ctx->pNode.pAlBaseContext, src_data);

  return ret;
}

S32 get_vdec_result(ALBaseContext *base_context, MppData *src_data) {
  S32 ret = 0;
  ret = dec_request_output_frame(base_context, src_data);

  return ret;
}

S32 get_vdec_result_2(ALBaseContext *base_context, MppData **src_data) {
  S32 ret = 0;
  ret = dec_request_output_frame_2(base_context, src_data);

  return ret;
}

S32 VDEC_RequestOutputFrame(MppVdecCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = get_vdec_result(ctx->pNode.pAlBaseContext, src_data);

  return ret;
}

S32 VDEC_RequestOutputFrame_2(MppVdecCtx *ctx, MppData **src_data) {
  S32 ret = 0;
  ret = get_vdec_result_2(ctx->pNode.pAlBaseContext, src_data);

  return ret;
}

S32 return_vdec_result(ALBaseContext *base_context, MppData *src_data) {
  S32 ret = 0;
  ret = dec_return_output_frame(base_context, src_data);

  return ret;
}

S32 VDEC_ReturnOutputFrame(MppVdecCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = return_vdec_result(ctx->pNode.pAlBaseContext, src_data);

  return ret;
}
S32 VDEC_Flush(MppVdecCtx *ctx) {
  S32 ret = 0;

  debug("begin flush!");
  ret = dec_flush(ctx->pNode.pAlBaseContext);
  debug("finish flush ret = %d", ret);

  return ret;
}

S32 VDEC_DestoryChannel(MppVdecCtx *ctx) {
  if (!ctx) {
    error("input para ctx is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  dec_destory(ctx->pNode.pAlBaseContext);
  debug("finish destory decoder");

  module_destory(ctx->pModule);
  debug("finish destory module");

  free(ctx);
  // ctx = NULL;

  return MPP_OK;
}

S32 VDEC_ResetChannel(MppVdecCtx *ctx) {
  S32 ret = 0;
  ret = dec_reset(ctx->pNode.pAlBaseContext);

  return ret;
}
