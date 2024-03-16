/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-03-15 11:36:22
 * @LastEditTime: 2024-03-15 17:29:15
 * @FilePath: \mpp\mpi\vo.c
 * @Description:
 */

#define ENABLE_DEBUG 1

#include "vo.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "al_interface_vo.h"
#include "log.h"

#define MODULE_TAG "mpp_vo"

ALBaseContext *(*vo_create)();
S32 (*vo_init)(ALBaseContext *ctx, MppVoPara *para);
S32 (*vo_getparam)(ALBaseContext *ctx, MppVoPara **para);
S32 (*vo_process)(ALBaseContext *ctx, MppData *sink_data);
void (*vo_destory)(ALBaseContext *ctx);

MppVoCtx *VO_CreateChannel() {
  MppVoCtx *ctx = (MppVoCtx *)malloc(sizeof(MppVoCtx));
  if (!ctx) {
    error("can not create MppVoCtx, please check! (%s)", strerror(errno));
    return NULL;
  }

  memset(ctx, 0, sizeof(MppVoCtx));

  debug("create VO Channel success!");
  return ctx;
}

S32 VO_Init(MppVoCtx *ctx) {
  S32 ret = 0;

  ctx->pModule = module_init(ctx->eVoType);

  vo_create = (ALBaseContext * (*)())
      dlsym(module_get_so_path(ctx->pModule), "al_vo_create");
  vo_init = (S32(*)(ALBaseContext * ctx, MppVoPara * para))
      dlsym(module_get_so_path(ctx->pModule), "al_vo_init");
  vo_getparam = (S32(*)(ALBaseContext * ctx, MppVoPara * *para))
      dlsym(module_get_so_path(ctx->pModule), "al_vo_getparam");
  vo_process = (S32(*)(ALBaseContext * ctx, MppData * sink_data))
      dlsym(module_get_so_path(ctx->pModule), "al_vo_process");
  vo_destory = (void (*)(ALBaseContext * ctx))
      dlsym(module_get_so_path(ctx->pModule), "al_vo_destory");

  ctx->pNode.pAlBaseContext = vo_create();

  ret = vo_init(ctx->pNode.pAlBaseContext, &(ctx->stVoPara));
  debug("init VO Channel, ret = %d", ret);

  return ret;
}

S32 VO_GetParam(MppVoCtx *ctx, MppVoPara **stVoPara) {
  S32 ret = 0;
  ret = vo_getparam(ctx->pNode.pAlBaseContext, stVoPara);
  debug("get VO parameters, ret = %d", ret);

  return ret;
}

S32 VO_Process(MppVoCtx *ctx, MppData *sink_data) {
  S32 ret = 0;
  ret = vo_process(ctx->pNode.pAlBaseContext, sink_data);
  debug("vo one packet, ret = %d", ret);

  return ret;
}

S32 VO_DestoryChannel(MppVoCtx *ctx) {
  if (!ctx) {
    error("input para ctx is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  vo_destory(ctx->pNode.pAlBaseContext);
  debug("finish destory vo");

  module_destory(ctx->pModule);
  debug("finish destory module");

  free(ctx);
  // ctx = NULL;

  return MPP_OK;
}
