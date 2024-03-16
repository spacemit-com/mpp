/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-03-15 13:41:24
 * @LastEditTime: 2024-03-16 15:40:27
 * @FilePath: \mpp\al\vo\sdl2\vo_sdl2.c
 * @Description:
 */

#define ENABLE_DEBUG 1

#include <SDL.h>
#include <SDL_thread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "al_interface_vo.h"
#include "log.h"

#define MODULE_TAG "vo_sdl2"

PIXEL_FORMAT_MAPPING_DEFINE(VoSdl2, SDL_PixelFormatEnum)
static const ALVoSdl2PixelFormatMapping stALVoSdl2PixelFormatMapping[] = {
    {PIXEL_FORMAT_I420, SDL_PIXELFORMAT_IYUV},
    {PIXEL_FORMAT_NV12, SDL_PIXELFORMAT_NV12},
    {PIXEL_FORMAT_NV21, SDL_PIXELFORMAT_NV21},
    {PIXEL_FORMAT_YVYU, SDL_PIXELFORMAT_YVYU},
    {PIXEL_FORMAT_UYVY, SDL_PIXELFORMAT_UYVY},
    {PIXEL_FORMAT_YUYV, SDL_PIXELFORMAT_YUY2},
    {PIXEL_FORMAT_RGBA, SDL_PIXELFORMAT_RGBA32},
    {PIXEL_FORMAT_BGRA, SDL_PIXELFORMAT_BGRA32},
    {PIXEL_FORMAT_ARGB, SDL_PIXELFORMAT_ARGB32},
    {PIXEL_FORMAT_ABGR, SDL_PIXELFORMAT_ABGR32},
};
PIXEL_FORMAT_MAPPING_CONVERT(VoSdl2, vosdl2, SDL_PixelFormatEnum)

typedef struct _ALVoSdl2Context ALVoSdl2Context;

struct _ALVoSdl2Context {
  /**
   * parent class
   */
  ALVoBaseContext stAlVoBaseContext;

  S32 nOutputWidth;
  S32 nOutputHeight;
  MppPixelFormat eOutputPixelFormat;

  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  SDL_Rect rect;
  SDL_Event event;
  SDL_bool quit;
};

ALBaseContext *al_vo_create() {
  ALVoSdl2Context *context = (ALVoSdl2Context *)malloc(sizeof(ALVoSdl2Context));
  if (!context) {
    error("can not malloc ALVoSdl2Context, please check!");
    return NULL;
  }

  memset(context, 0, sizeof(ALVoSdl2Context));

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

  ALVoSdl2Context *context = (ALVoSdl2Context *)ctx;
  S32 ret = 0;

  context->nOutputWidth = para->nWidth;
  context->nOutputHeight = para->nHeight;
  context->eOutputPixelFormat = para->ePixelFormat;

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    error("SDL could not initialize! SDL_Error: %s", SDL_GetError());
    goto exit;
  }

  context->window = SDL_CreateWindow(
      "Spacemit YUV Display", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      context->nOutputWidth, context->nOutputHeight, 0);
  if (!context->window) {
    error("Window could not be created! SDL_Error: %s", SDL_GetError());
    goto exit;
  }

  context->renderer =
      SDL_CreateRenderer(context->window, -1,
                         SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!context->renderer) {
    error("Renderer could not be created! SDL_Error: %s", SDL_GetError());
    goto exit;
  }

  context->texture = SDL_CreateTexture(
      context->renderer,
      get_vosdl2_codec_pixel_format(
          context->eOutputPixelFormat) /*SDL_PIXELFORMAT_NV12*/,
      SDL_TEXTUREACCESS_STREAMING, context->nOutputWidth,
      context->nOutputHeight);
  if (!context->texture) {
    error("Texture could not be created! SDL_Error: %s", SDL_GetError());
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

  ALVoSdl2Context *context = (ALVoSdl2Context *)ctx;
  MppFrame *sink_frame = FRAME_GetFrame(sink_data);
  S32 ret = 0;

  SDL_SetRenderDrawColor(context->renderer, 0, 0, 0, 255);
  SDL_RenderClear(context->renderer);
  if (context->eOutputPixelFormat == PIXEL_FORMAT_I420) {
    SDL_UpdateYUVTexture(
        context->texture, NULL, FRAME_GetDataPointer(sink_frame, 0),
        context->nOutputWidth, FRAME_GetDataPointer(sink_frame, 1),
        context->nOutputWidth, FRAME_GetDataPointer(sink_frame, 2),
        context->nOutputWidth);
  } else if (context->eOutputPixelFormat == PIXEL_FORMAT_NV12 ||
             context->eOutputPixelFormat == PIXEL_FORMAT_NV21) {
    SDL_UpdateNVTexture(
        context->texture, NULL, FRAME_GetDataPointer(sink_frame, 0),
        context->nOutputWidth, FRAME_GetDataPointer(sink_frame, 1),
        context->nOutputWidth);
  }

  context->rect.x = 0;
  context->rect.y = 0;
  context->rect.w = context->nOutputWidth;
  context->rect.h = context->nOutputHeight;
  SDL_RenderCopy(context->renderer, context->texture, NULL, &(context->rect));

  SDL_RenderPresent(context->renderer);

  return MPP_OK;
}

void al_vo_destory(ALBaseContext *ctx) {
  ALVoSdl2Context *context = (ALVoSdl2Context *)ctx;
  S32 ret = 0;

  SDL_DestroyTexture(context->texture);
  SDL_DestroyRenderer(context->renderer);
  SDL_DestroyWindow(context->window);
  SDL_Quit();

  free(context);
}
