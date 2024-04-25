/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-04-25 20:00:13
 * @LastEditTime: 2024-04-25 20:32:37
 * @FilePath: \mpp\al\vi\v4l2\vi_v4l2.c
 * @Description:
 */

#define ENABLE_DEBUG 1

#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "al_interface_vi.h"
#include "log.h"

#define MODULE_TAG "vi_v4l2"

typedef struct _ALViV4l2Context ALViV4l2Context;

struct _ALViV4l2Context {
  /**
   * parent class
   */
  ALViBaseContext stAlViBaseContext;

  S32 nOutputWidth;
  S32 nOutputHeight;
  MppPixelFormat eOutputPixelFormat;
};

ALBaseContext *al_vi_create() {
  ALViV4l2Context *context = (ALViV4l2Context *)malloc(sizeof(ALViV4l2Context));
  if (!context) {
    error("can not malloc ALViV4l2Context, please check!");
    return NULL;
  }

  memset(context, 0, sizeof(ALViV4l2Context));

  return &(context->stAlViBaseContext.stAlBaseContext);
}

RETURN al_vi_init(ALBaseContext *ctx, MppViPara *para) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!para) {
    error("input para MppViPara is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALViV4l2Context *context = (ALViV4l2Context *)ctx;
  S32 ret = 0;

  context->nOutputWidth = para->nWidth;
  context->nOutputHeight = para->nHeight;
  context->eOutputPixelFormat = para->ePixelFormat;

  debug("init finish");

  return MPP_OK;

exit:
  error("k1 vi_v4l2 init fail");
  free(context);
  return MPP_INIT_FAILED;
}

S32 al_vi_get_para(ALBaseContext *ctx, MppViPara **para) { return 0; }

S32 al_vi_process(ALBaseContext *ctx, MppData *sink_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!sink_data) {
    error("input para MppData_sink is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALViV4l2Context *context = (ALViV4l2Context *)ctx;
  S32 ret = 0;

  MppFrame *sink_frame = FRAME_GetFrame(sink_data);

  return MPP_OK;
}

void al_vi_destory(ALBaseContext *ctx) {
  ALViV4l2Context *context = (ALViV4l2Context *)ctx;
  S32 ret = 0;

  free(context);
}