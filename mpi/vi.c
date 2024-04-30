/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-04-25 19:27:35
 * @LastEditTime: 2024-04-29 11:28:47
 * @FilePath: \mpp\mpi\vi.c
 * @Description:
 */

#define ENABLE_DEBUG 1

#include "vi.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "al_interface_vi.h"
#include "log.h"

#define MODULE_TAG "mpp_vi"

ALBaseContext *(*vi_create)();
S32 (*vi_init)(ALBaseContext *ctx, MppViPara *para);
S32 (*vi_getparam)(ALBaseContext *ctx, MppViPara **para);
S32 (*vi_process)(ALBaseContext *ctx, MppData *src_data);
S32 (*vi_request_output_data)(ALBaseContext *ctx, MppData *src_data);
S32 (*vi_return_output_data)(ALBaseContext *ctx, MppData *src_data);
void (*vi_destory)(ALBaseContext *ctx);

MppViCtx *VI_CreateChannel() {
  MppViCtx *ctx = (MppViCtx *)malloc(sizeof(MppViCtx));
  if (!ctx) {
    error("can not create MppViCtx, please check! (%s)", strerror(errno));
    return NULL;
  }

  memset(ctx, 0, sizeof(MppViCtx));

  debug("create VI Channel success!");
  return ctx;
}

S32 VI_Init(MppViCtx *ctx) {
  S32 ret = 0;

  ctx->pModule = module_init(ctx->eViType);

  vi_create = (ALBaseContext * (*)())
      dlsym(module_get_so_path(ctx->pModule), "al_vi_create");
  vi_init = (S32(*)(ALBaseContext * ctx, MppViPara * para))
      dlsym(module_get_so_path(ctx->pModule), "al_vi_init");
  vi_getparam = (S32(*)(ALBaseContext * ctx, MppViPara * *para))
      dlsym(module_get_so_path(ctx->pModule), "al_vi_getparam");
  vi_process = (S32(*)(ALBaseContext * ctx, MppData * src_data))
      dlsym(module_get_so_path(ctx->pModule), "al_vi_process");
  vi_request_output_data = (S32(*)(ALBaseContext * ctx, MppData * src_data))
      dlsym(module_get_so_path(ctx->pModule), "al_vi_request_output_data");
  vi_return_output_data = (S32(*)(ALBaseContext * ctx, MppData * src_data))
      dlsym(module_get_so_path(ctx->pModule), "al_vi_return_output_data");
  vi_destory = (void (*)(ALBaseContext * ctx))
      dlsym(module_get_so_path(ctx->pModule), "al_vi_destory");

  ctx->pNode.pAlBaseContext = vi_create();

  ret = vi_init(ctx->pNode.pAlBaseContext, &(ctx->stViPara));
  debug("init VI Channel, ret = %d", ret);

  return ret;
}

S32 VI_GetParam(MppViCtx *ctx, MppViPara **stViPara) {
  S32 ret = 0;
  ret = vi_getparam(ctx->pNode.pAlBaseContext, stViPara);
  debug("get VI parameters, ret = %d", ret);

  return ret;
}

S32 VI_Process(MppViCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = vi_process(ctx->pNode.pAlBaseContext, src_data);
  debug("vi one frame, ret = %d", ret);

  return ret;
}

S32 VI_RequestOutputData(MppViCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = vi_request_output_data(ctx->pNode.pAlBaseContext, src_data);
  debug("vi rquest one data, ret = %d", ret);

  return ret;
}

S32 VI_ReturnOutputData(MppViCtx *ctx, MppData *src_data) {
  S32 ret = 0;
  ret = vi_return_output_data(ctx->pNode.pAlBaseContext, src_data);
  debug("vi return one data, ret = %d", ret);

  return ret;
}

S32 VI_DestoryChannel(MppViCtx *ctx) {
  if (!ctx) {
    error("input para ctx is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  vi_destory(ctx->pNode.pAlBaseContext);
  debug("finish destory vi");

  module_destory(ctx->pModule);
  debug("finish destory module");

  free(ctx);
  // ctx = NULL;

  return MPP_OK;
}
