/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-03-16 11:17:51
 * @LastEditTime: 2024-04-28 14:27:34
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
  S32 nOutputStride;
  MppPixelFormat eOutputPixelFormat;
  U8 *pOutputFileName;
  FILE *pOutputFile;
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
  context->nOutputStride = para->nStride;
  context->eOutputPixelFormat = para->ePixelFormat;
  context->pOutputFileName = para->pOutputFileName;

  context->pOutputFile = fopen(context->pOutputFileName, "w+");
  if (!context->pOutputFile) {
    error("can not open context->pOutputFileName, please check !");
    goto exit;
  }

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
    S32 size[4];

    switch (context->eOutputPixelFormat) {
      case PIXEL_FORMAT_I420:
        size[0] = context->nOutputWidth * context->nOutputHeight;
        size[1] = context->nOutputWidth * context->nOutputHeight / 4;
        size[2] = context->nOutputWidth * context->nOutputHeight / 4;
        break;
      case PIXEL_FORMAT_NV12:
      case PIXEL_FORMAT_NV21:
        size[0] = context->nOutputWidth * context->nOutputHeight;
        size[1] = context->nOutputWidth * context->nOutputHeight / 2;
        break;
      case PIXEL_FORMAT_YUYV:
      case PIXEL_FORMAT_YVYU:
        size[0] = context->nOutputWidth * context->nOutputHeight * 2;
        break;
      default:
        error("Unsupported picture format (%d)! Please check!",
              context->eOutputPixelFormat);
        return MPP_CHECK_FAILED;
    }

    if (1 == FRAME_GetDataUsedNum(sink_frame)) {
      fwrite(FRAME_GetDataPointer(sink_frame, 0), size[0], 1,
             context->pOutputFile);
    } else if (2 == FRAME_GetDataUsedNum(sink_frame)) {
      fwrite(FRAME_GetDataPointer(sink_frame, 0), size[0], 1,
             context->pOutputFile);
      fwrite(FRAME_GetDataPointer(sink_frame, 1), size[1], 1,
             context->pOutputFile);
    } else {
      fwrite(FRAME_GetDataPointer(sink_frame, 0), size[0], 1,
             context->pOutputFile);
      fwrite(FRAME_GetDataPointer(sink_frame, 1), size[1], 1,
             context->pOutputFile);
      fwrite(FRAME_GetDataPointer(sink_frame, 2), size[2], 1,
             context->pOutputFile);
    }
    fflush(context->pOutputFile);
  } else {
    MppPacket *sink_packet = PACKET_GetPacket(sink_data);
    fwrite(PACKET_GetDataPointer(sink_packet), PACKET_GetLength(sink_packet), 1,
           context->pOutputFile);
    fflush(context->pOutputFile);
  }

  return MPP_OK;
}

void al_vo_destory(ALBaseContext *ctx) {
  ALVoFileContext *context = (ALVoFileContext *)ctx;
  S32 ret = 0;

  if (context->pOutputFile) {
    fflush(context->pOutputFile);
    fclose(context->pOutputFile);
    context->pOutputFile = NULL;
  }

  free(context);
}