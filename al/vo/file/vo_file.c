/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-03-16 11:17:51
 * @LastEditTime: 2024-03-16 11:37:57
 * @FilePath: \mpp\al\vo\file\vo_file.c
 * @Description:
 */

#define ENABLE_DEBUG 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "al_interface_vo.h"
#include "log.h"

#define MODULE_TAG "vo_file"

typedef struct _ALVoFileContext ALVoFileContext;

struct _ALVoFileContext {
  /**
   * parent class
   */
  ALVoBaseContext stAlVoBaseContext;
  BOOL bIsFrame;

  S32 nOutputWidth;
  S32 nOutputHeight;
  MppPixelFormat eOutputPixelFormat;
  U8 *pOutputFileName;
};

ALBaseContext *al_vo_create() {
  ALVoFileContext *context = (ALVoFileContext *)malloc(sizeof(ALVoFileContext));
  if (!context) {
    error("can not malloc ALVoFileContext, please check!");
    return NULL;
  }

  memset(context, 0, sizeof(ALVoFileContext));

  return &(context->stAlVoBaseContext.stAlBaseContext);
}

RETURN al_vo_init(ALBaseContext *ctx, MppVoPara *para) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!para) {
    error("input para MppVoPara is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALVoFileContext *context = (ALVoFileContext *)ctx;
  S32 ret = 0;

  context->bIsFrame = para->bIsFrame;
  context->nOutputWidth = para->nWidth;
  context->nOutputHeight = para->nHeight;
  context->eOutputPixelFormat = para->ePixelFormat;
  context->pOutputFileName = para->pOutputFileName;

  debug("init finish");

  return MPP_OK;

exit:
  error("k1 vo_sdl2 init fail");
  free(context);
  return MPP_INIT_FAILED;
}

S32 al_vo_get_para(ALBaseContext *ctx, MppVoPara **para) { return 0; }

S32 al_vo_process(ALBaseContext *ctx, MppData *sink_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!sink_data) {
    error("input para MppData_sink is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALVoFileContext *context = (ALVoFileContext *)ctx;
  S32 ret = 0;

  if (context->bIsFrame) {
    MppFrame *sink_frame = FRAME_GetFrame(sink_data);
  } else {
    MppPacket *sink_packet = PACKET_GetPacket(sink_data);
  }

  return MPP_OK;
}

void al_vo_destory(ALBaseContext *ctx) {
  ALVoFileContext *context = (ALVoFileContext *)ctx;
  S32 ret = 0;

  free(context);
}