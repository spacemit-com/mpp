/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:36
 * @LastEditTime: 2023-11-09 15:38:06
 * @Description: video decode plugin for ffmpeg
 */

#define ENABLE_DEBUG 0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "al_interface_dec.h"
#include "libavcodec/avcodec.h"
#include "libavutil/pixfmt.h"
#include "log.h"

#define MODULE_TAG "ffmpegdec"

#define INBUF_SIZE 4096

PIXEL_FORMAT_MAPPING_DEFINE(FFMpegDec, enum AVPixelFormat)
static const ALFFMpegDecPixelFormatMapping stALFFMpegDecPixelFormatMapping[] = {
    {PIXEL_FORMAT_I420, AV_PIX_FMT_YUV420P},
    {PIXEL_FORMAT_NV12, AV_PIX_FMT_NV12},
    {PIXEL_FORMAT_YVYU, AV_PIX_FMT_YVYU422},
    {PIXEL_FORMAT_UYVY, AV_PIX_FMT_UYVY422},
    {PIXEL_FORMAT_YUYV, AV_PIX_FMT_YUYV422},
    {PIXEL_FORMAT_RGBA, AV_PIX_FMT_RGBA},
    {PIXEL_FORMAT_BGRA, AV_PIX_FMT_BGRA},
    {PIXEL_FORMAT_ARGB, AV_PIX_FMT_ARGB},
    {PIXEL_FORMAT_ABGR, AV_PIX_FMT_ABGR},
};
PIXEL_FORMAT_MAPPING_CONVERT(FFMpegDec, ffmpegdec, enum AVPixelFormat)

CODING_TYPE_MAPPING_DEFINE(FFMpegDec, enum AVCodecID)
static const ALFFMpegDecCodingTypeMapping stALFFMpegDecCodingTypeMapping[] = {
    {CODING_H264, AV_CODEC_ID_H264},
    {CODING_H265, AV_CODEC_ID_H265},
    {CODING_MJPEG, AV_CODEC_ID_MJPEG},
    {CODING_VP8, AV_CODEC_ID_VP8},
    {CODING_VP9, AV_CODEC_ID_VP9},
    {CODING_AV1, AV_CODEC_ID_NONE},
    {CODING_AVS, AV_CODEC_ID_AVS},
    {CODING_AVS2, AV_CODEC_ID_AVS2},
    {CODING_MPEG1, AV_CODEC_ID_MPEG1VIDEO},
    {CODING_MPEG2, AV_CODEC_ID_MPEG2VIDEO},
    {CODING_MPEG4, AV_CODEC_ID_MPEG4},
};
CODING_TYPE_MAPPING_CONVERT(FFMpegDec, ffmpegdec, enum AVCodecID)

typedef struct _ALFFMpegDecContext ALFFMpegDecContext;

struct _ALFFMpegDecContext {
  ALDecBaseContext stAlDecBaseContext;
  const AVCodec *pCodec;
  AVCodecContext *pCodecContext;
  AVFrame *pFrame;
  AVPacket *pPacket;
  U8 pInputBuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
};

ALBaseContext *al_dec_create() {
  ALFFMpegDecContext *context =
      (ALFFMpegDecContext *)malloc(sizeof(ALFFMpegDecContext));
  if (!context) {
    return NULL;
  }

  memset(context, 0, sizeof(ALFFMpegDecContext));

  context->pCodec = NULL;
  context->pCodecContext = NULL;
  context->pFrame = NULL;
  context->pPacket = NULL;

  return &(context->stAlDecBaseContext.stAlBaseContext);
}

RETURN al_dec_init(ALBaseContext *ctx, MppVdecPara *para) {
  if (!ctx) return MPP_NULL_POINTER;

  ALFFMpegDecContext *context = (ALFFMpegDecContext *)ctx;

  context->pCodec =
      avcodec_find_decoder(get_ffmpegdec_codec_coding_type(para->eCodingType));
  if (!context->pCodec) {
    error("can not find decoder, please check!");
    return MPP_NULL_POINTER;
  }

  context->pPacket = av_packet_alloc();
  if (!context->pPacket) {
    error("can not alloc packet, please check!");
    return MPP_NULL_POINTER;
  }

  context->pFrame = av_frame_alloc();
  if (!context->pFrame) {
    error("can not alloc frame, please check!");
    return MPP_NULL_POINTER;
  }

  context->pCodecContext = avcodec_alloc_context3(context->pCodec);
  if (!context->pCodecContext) {
    error("can not alloc context, please check!");
    return MPP_NULL_POINTER;
  }

  if (avcodec_open2(context->pCodecContext, context->pCodec, NULL) < 0) {
    error("Could not open codec, please check");
    return MPP_NULL_POINTER;
  }

  debug("init finish");

  return MPP_OK;
}

S32 al_dec_decode(ALBaseContext *ctx, MppData *sink_data) {
  if (!ctx) return MPP_NULL_POINTER;

  ALFFMpegDecContext *context = (ALFFMpegDecContext *)ctx;
  S32 ret = 0;
  MppPacket *sink_packet = PACKET_GetPacket(sink_data);

  context->pPacket->data = PACKET_GetDataPointer(sink_packet);
  context->pPacket->size = PACKET_GetLength(sink_packet);
  debug("head:%x %x %x %x", context->pPacket->data[0],
        context->pPacket->data[1], context->pPacket->data[2],
        context->pPacket->data[3]);

  do {
    ret = avcodec_send_packet(context->pCodecContext, context->pPacket);
  } while (AVERROR(EAGAIN) == ret);

  if (ret < 0) {
    error("Error sending a packet for decoding %d", ret);
    return 1;
  }

  av_packet_unref(context->pPacket);

  return 0;
}

S32 al_dec_request_output_frame(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) return MPP_NULL_POINTER;

  ALFFMpegDecContext *context = (ALFFMpegDecContext *)ctx;
  MppFrame *frame = FRAME_GetFrame(src_data);
  U8 *tmp_pdata0;
  U8 *tmp_pdata1;
  U8 *tmp_pdata2;
  S32 ret = 0;

  do {
    ret = avcodec_receive_frame(context->pCodecContext, context->pFrame);
  } while (AVERROR(EAGAIN) == ret);

  if (AVERROR_EOF == ret) {
    error("Get EOF");
    return 1;
  } else if (ret < 0) {
    error("Error during decoding");
    return 1;
  }

  FRAME_SetWidth(frame, context->pFrame->width);
  FRAME_SetHeight(frame, context->pFrame->height);
  FRAME_SetLineStride(frame, context->pFrame->width);
  FRAME_SetPixelFormat(frame,
                       get_ffmpegdec_mpp_pixel_format(
                           (enum AVPixelFormat)(context->pFrame->format)));
  FRAME_SetPts(frame, context->pFrame->pts);

  MppFrame *src_frame = FRAME_GetFrame(src_data);
  if (!FRAME_GetDataPointer(src_frame, 0)) {
    FRAME_Alloc(src_frame, FRAME_GetPixelFormat(frame),
                context->pFrame->linesize[0], context->pFrame->height);
  }

  tmp_pdata0 = (U8 *)FRAME_GetDataPointer(src_frame, 0);
  tmp_pdata1 = (U8 *)FRAME_GetDataPointer(src_frame, 1);
  tmp_pdata2 = (U8 *)FRAME_GetDataPointer(src_frame, 2);

  debug("---%d %d %d %d %d", context->pFrame->width, context->pFrame->height,
        context->pFrame->linesize[0], context->pFrame->linesize[1],
        context->pFrame->linesize[2]);

  for (S32 i = 0; i < context->pFrame->height; i++) {
    memcpy(tmp_pdata0, context->pFrame->data[0], context->pFrame->width);
    tmp_pdata0 += context->pFrame->width;
    context->pFrame->data[0] += context->pFrame->linesize[0];
  }

  for (S32 i = 0; i < context->pFrame->height / 2; i++) {
    memcpy(tmp_pdata1, context->pFrame->data[1], context->pFrame->width / 2);
    memcpy(tmp_pdata2, context->pFrame->data[2], context->pFrame->width / 2);
    tmp_pdata1 += context->pFrame->width / 2;
    tmp_pdata2 += context->pFrame->width / 2;
    context->pFrame->data[1] += context->pFrame->linesize[0] / 2;
    context->pFrame->data[2] += context->pFrame->linesize[0] / 2;
  }

  av_frame_unref(context->pFrame);

  return 0;
}

S32 al_dec_return_output_frame(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) return MPP_NULL_POINTER;

  ALFFMpegDecContext *context = (ALFFMpegDecContext *)ctx;

  return 0;
}

void al_dec_destory(ALBaseContext *ctx) {
  if (!ctx) return;

  ALFFMpegDecContext *context = (ALFFMpegDecContext *)ctx;

  if (context->pCodecContext) avcodec_free_context(&(context->pCodecContext));
  if (context->pFrame) av_frame_free(&(context->pFrame));
  if (context->pPacket) av_packet_free(&(context->pPacket));

  free(context);
  context = NULL;
}
