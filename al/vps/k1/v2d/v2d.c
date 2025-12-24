/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-11-13 10:10:18
 * @LastEditTime: 2023-11-14 14:05:12
 * @Description:
 */

#define ENABLE_DEBUG 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "al_interface_g2d.h"
#include "asr_v2d_api.h"
#include "asr_v2d_type.h"
#include "log.h"

#define MODULE_TAG "v2d"

#define ALIGN_UP(size, shift) (((size + shift - 1) / shift) * shift)
#define PAGESIZE (4096)

typedef struct _ALK1V2dContext ALK1V2dContext;

struct _ALK1V2dContext {
  /**
   * parent class
   */
  ALG2dBaseContext stAlG2dBaseContext;

  /**
   * V2D API
   */
  V2D_HANDLE hHandle;
  V2D_SURFACE_S stBackGround, stForeGround, stDst;
  V2D_AREA_S stBackGroundRect, stForeGroundRect, stDstRect;
  V2D_BLEND_CONF_S stBlendConf;
  V2D_ROTATE_ANGLE_E enForeRotate, enBackRotate;
  V2D_CSC_MODE_E enForeCSCMode, enBackCSCMode;
  V2D_DITHER_E dither;

  /**
   * other
   */
  // S32 nInputBufFd;
  // S32 nOutputBufFd;

  S32 nInputBufSize;
  S32 nOutputBufSize;

  S32 nInputBufMapSize;
  S32 nOutputBufMapSize;

  // void *pInputBufMapAddr;
  // void *pOutputBufMapAddr;

  S32 nInputWidth;
  S32 nInputHeight;
  S32 nOutputWidth;
  S32 nOutputHeight;

  MppPixelFormat eInputPixelFormat;
  MppPixelFormat eOutputPixelFormat;
};

PIXEL_FORMAT_MAPPING_DEFINE(K1V2d, V2D_COLOR_FORMAT_E)
static const ALK1V2dPixelFormatMapping stALK1V2dPixelFormatMapping[] = {
    {PIXEL_FORMAT_RGB_888, V2D_COLOR_FORMAT_RGB888},
    {PIXEL_FORMAT_RGBA, V2D_COLOR_FORMAT_RGBA8888},
    {PIXEL_FORMAT_ARGB, V2D_COLOR_FORMAT_ARGB8888},
    {PIXEL_FORMAT_RGB_565, V2D_COLOR_FORMAT_RGB565},
    {PIXEL_FORMAT_NV12, V2D_COLOR_FORMAT_NV12},
    {PIXEL_FORMAT_BGR_888, V2D_COLOR_FORMAT_BGR888},
    {PIXEL_FORMAT_BGRA, V2D_COLOR_FORMAT_BGRA8888},
    {PIXEL_FORMAT_ABGR, V2D_COLOR_FORMAT_ABGR8888},
    {PIXEL_FORMAT_BGR_888, V2D_COLOR_FORMAT_BGR565},
    {PIXEL_FORMAT_NV21, V2D_COLOR_FORMAT_NV21},
    {PIXEL_FORMAT_UNKNOWN, V2D_COLOR_FORMAT_BUTT},
};
PIXEL_FORMAT_MAPPING_CONVERT(K1V2d, k1v2d, V2D_COLOR_FORMAT_E)

ALBaseContext *al_g2d_create() {
  ALK1V2dContext *context = (ALK1V2dContext *)malloc(sizeof(ALK1V2dContext));
  if (!context) {
    error("can not malloc ALK1V2dContext, please check!");
    return NULL;
  }

  memset(context, 0, sizeof(ALK1V2dContext));

  return &(context->stAlG2dBaseContext.stAlBaseContext);
}

RETURN al_g2d_init(ALBaseContext *ctx, MppG2dPara *para) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!para) {
    error("input para MppG2dPara is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALK1V2dContext *context = (ALK1V2dContext *)ctx;
  S32 ret = 0;

  // context->nInputBufFd = para->nInputBufFd;
  // context->nOutputBufFd = para->nOutputBufFd;

  context->eInputPixelFormat = para->eInputPixelFormat;
  context->eOutputPixelFormat = para->eOutputPixelFormat;

  context->nInputBufSize = para->nInputBufSize;
  context->nOutputBufSize = para->nOutputBufSize;

  context->nInputWidth = para->nInputWidth;
  context->nInputHeight = para->nInputHeight;
  context->nOutputWidth = para->nOutputWidth;
  context->nOutputHeight = para->nOutputHeight;

  context->nInputBufMapSize = ALIGN_UP(context->nInputBufSize, PAGESIZE);
  context->nOutputBufMapSize = ALIGN_UP(context->nOutputBufSize, PAGESIZE);
  /*
    context->pInputBufMapAddr =
        mmap(NULL, context->nInputBufMapSize, PROT_READ | PROT_WRITE,
    MAP_SHARED, context->nInputBufFd, 0); if (context->pInputBufMapAddr ==
    MAP_FAILED) { error(" v2d mmap input fd failed, please check!"); return
    MPP_MMAP_FAILED;
    }
    memset(context->pInputBufMapAddr, 0, context->nInputBufMapSize);

    context->pOutputBufMapAddr =
        mmap(NULL, context->nOutputBufMapSize, PROT_READ | PROT_WRITE,
    MAP_SHARED, context->nOutputBufFd, 0);
    ;
    if (context->pOutputBufMapAddr == MAP_FAILED) {
      error(" v2d mmap output fd failed, please check!");
      return MPP_MMAP_FAILED;
    }
    memset(context->pOutputBufMapAddr, 0, context->nOutputBufMapSize);
  */
  debug("init finish");

  return MPP_OK;

exit:
  error("k1 v2d init fail");
  free(context);
  return MPP_INIT_FAILED;
}

S32 al_g2d_set_para(ALBaseContext *ctx, MppG2dPara *para) { return 0; }

S32 al_g2d_process(ALBaseContext *ctx, MppData *sink_data, MppData *src_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!sink_data) {
    error("input para MppData_sink is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!src_data) {
    error("input para MppData_src is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALK1V2dContext *context = (ALK1V2dContext *)ctx;
  MppFrame *sink_frame = FRAME_GetFrame(sink_data);
  MppFrame *src_frame = FRAME_GetFrame(src_data);
  S32 ret = 0;

  // config layer0
  context->enBackRotate = V2D_ROT_0;
  context->enBackCSCMode = V2D_CSC_MODE_BUTT;
  memset(&(context->stBackGround), 0, sizeof(V2D_SURFACE_S));
  context->stBackGround.fbc_enable = 0;
  context->stBackGround.fd = FRAME_GetFD(sink_frame, 0);
  context->stBackGround.offset = context->nInputWidth * context->nInputHeight;
  context->stBackGround.w = context->nInputWidth;
  context->stBackGround.h = context->nInputHeight;
  context->stBackGround.stride = context->nInputWidth;
  context->stBackGround.format =
      get_k1v2d_codec_pixel_format(PIXEL_FORMAT_NV12);
  context->stBackGroundRect.x = 0;
  context->stBackGroundRect.y = 0;
  context->stBackGroundRect.w = context->nInputWidth;
  context->stBackGroundRect.h = context->nInputHeight;

  // config layer1
  context->enForeRotate = V2D_ROT_0;
  context->enForeCSCMode = V2D_CSC_MODE_BUTT;

  // config output
  context->dither = V2D_NO_DITHER;

  memset(&(context->stDst), 0, sizeof(V2D_SURFACE_S));
  context->stDst.fbc_enable = 0;
  context->stDst.fd = FRAME_GetFD(src_frame, 0);
  context->stDst.offset = context->nOutputWidth * context->nOutputHeight;
  context->stDst.w = context->nOutputWidth;
  context->stDst.h = context->nOutputHeight;
  context->stDst.stride = context->nOutputWidth;
  context->stDst.format = get_k1v2d_codec_pixel_format(PIXEL_FORMAT_NV12);
  context->stDstRect.x = 0;
  context->stDstRect.y = 0;
  context->stDstRect.w = context->nOutputWidth;
  context->stDstRect.h = context->nOutputHeight;

  memset(&(context->stBlendConf), 0, sizeof(V2D_BLEND_CONF_S));
  context->stBlendConf.blendlayer[0].blend_area.x = 0;
  context->stBlendConf.blendlayer[0].blend_area.y = 0;
  context->stBlendConf.blendlayer[0].blend_area.w = context->nOutputWidth;
  context->stBlendConf.blendlayer[0].blend_area.h = context->nOutputHeight;

  ret = ASR_V2D_BeginJob(&(context->hHandle));
  if (ret) {
    error("can not begin v2d job, please check!");
    return MPP_CONVERTER_ERROR;
  }

  ret = ASR_V2D_AddBlendTask(
      context->hHandle, &(context->stBackGround), &(context->stBackGroundRect),
      NULL, NULL, NULL, NULL, &(context->stDst), &(context->stDstRect),
      &(context->stBlendConf), context->enForeRotate, context->enBackRotate,
      context->enForeCSCMode, context->enBackCSCMode, NULL, context->dither);
  if (ret) {
    error("can not add blend task, please check!");
    return MPP_CONVERTER_ERROR;
  }

  ret = ASR_V2D_EndJob(context->hHandle);
  if (ret) {
    error("can not end v2d job, please check!");
    return MPP_CONVERTER_ERROR;
  }

  return MPP_OK;
}

void al_g2d_destory(ALBaseContext *ctx) {
  ALK1V2dContext *context = (ALK1V2dContext *)ctx;
  S32 ret = 0;

  // munmap(context->pInputBufMapAddr, context->nInputBufMapSize);
  // munmap(context->pOutputBufMapAddr, context->nOutputBufMapSize);

  free(context);
}
