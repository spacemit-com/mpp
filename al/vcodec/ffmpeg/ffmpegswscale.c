/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-31 15:46:44
 * @LastEditTime: 2023-11-14 11:20:13
 * @Description: video scale plugin for ffmpeg
 */

#define ENABLE_DEBUG 0

#include <assert.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "al_interface_g2d.h"
#include "log.h"

#define MODULE_TAG "ffmpegswscale"

typedef struct _ALFFMpegSwscaleContext ALFFMpegSwscaleContext;

struct _ALFFMpegSwscaleContext {
  ALG2dBaseContext stAlG2dBaseContext;
  struct SwsContext *pSwsContext;
  U8 *pSrcData[4];
  U8 *pDstData[4];
  S32 nSrcLinesize[4];
  S32 nDstLinesize[4];
  S32 nSrcWidth;
  S32 nSrcHeight;
  S32 nDstWidth;
  S32 nDstHeight;
  enum AVPixelFormat eSrcPixFmt;
  enum AVPixelFormat eDstPixFmt;
};

ALBaseContext *al_g2d_create() {
  ALFFMpegSwscaleContext *context =
      (ALFFMpegSwscaleContext *)malloc(sizeof(ALFFMpegSwscaleContext));
  if (!context) return NULL;
  memset(context, 0, sizeof(ALFFMpegSwscaleContext));

  context->pSwsContext = NULL;

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

  S32 ret = 0;

  ALFFMpegSwscaleContext *context = (ALFFMpegSwscaleContext *)ctx;

  context->nSrcWidth = 1280;
  context->nSrcHeight = 720;
  context->eSrcPixFmt = AV_PIX_FMT_YUV420P;
  context->nDstWidth = 640;
  context->nDstHeight = 360;
  context->eDstPixFmt = AV_PIX_FMT_YUV420P;

  context->pSwsContext = sws_getContext(
      context->nSrcWidth, context->nSrcHeight, context->eSrcPixFmt,
      context->nDstWidth, context->nDstHeight, context->eDstPixFmt,
      SWS_BILINEAR, NULL, NULL, NULL);
  if (!context->pSwsContext) return MPP_NULL_POINTER;

  if ((av_image_alloc(context->pSrcData, context->nSrcLinesize,
                      context->nSrcWidth, context->nSrcHeight,
                      context->eSrcPixFmt, 16)) < 0) {
    error("Could not allocate source image");
    return MPP_MALLOC_FAILED;
  }

  if ((av_image_alloc(context->pDstData, context->nDstLinesize,
                      context->nDstWidth, context->nDstHeight,
                      context->eDstPixFmt, 1)) < 0) {
    error("Could not allocate destination image");
    return MPP_MALLOC_FAILED;
  }

  debug("init finish %d %d", context->nSrcLinesize[0],
        context->nDstLinesize[0]);

  return MPP_OK;
}

S32 al_g2d_set_para(ALBaseContext *ctx, MppG2dPara *para) { return 0; }

S32 al_g2d_convert(ALBaseContext *ctx, MppData *sink_data) {
  ALFFMpegSwscaleContext *context = (ALFFMpegSwscaleContext *)ctx;
  S32 ret = 0;
  MppFrame *sink_frame = FRAME_GetFrame(sink_data);

  context->pSrcData[0] = (U8 *)FRAME_GetDataPointer(sink_frame, 0);
  context->pSrcData[1] = (U8 *)FRAME_GetDataPointer(sink_frame, 1);
  context->pSrcData[2] = (U8 *)FRAME_GetDataPointer(sink_frame, 2);

  // scale a frame
  sws_scale(context->pSwsContext, (const uint8_t *const *)context->pSrcData,
            context->nSrcLinesize, 0, context->nSrcHeight, context->pDstData,
            context->nDstLinesize);

  return ret;
}

S32 al_g2d_request_output_frame(ALBaseContext *ctx, MppData *src_data) {
  ALFFMpegSwscaleContext *context = (ALFFMpegSwscaleContext *)ctx;
  S32 ret = 0;

  // av_packet_unref(enc_context->pPacket);

  return 0;
}

void al_g2d_destory(ALBaseContext *ctx) {}
