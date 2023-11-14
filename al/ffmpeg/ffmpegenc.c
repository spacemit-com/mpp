/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:36
 * @LastEditTime: 2023-10-19 16:51:35
 * @Description: video encode plugin for ffmpeg
 */

#define ENABLE_DEBUG 0

#include <assert.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "al_interface_enc.h"
#include "libavcodec/avcodec.h"
#include "log.h"

#define MODULE_TAG "ffmpegenc"

typedef struct _ALFFMpegEncContext ALFFMpegEncContext;

struct _ALFFMpegEncContext {
  ALEncBaseContext stAlEncBaseContext;
  const AVCodec *pCodec;
  AVCodecContext *pCodecContext;
  AVFrame *pFrame;
  AVPacket *pPacket;
};

ALBaseContext *al_enc_create() {
  ALFFMpegEncContext *context =
      (ALFFMpegEncContext *)malloc(sizeof(ALFFMpegEncContext));
  if (!context) return NULL;
  memset(context, 0, sizeof(ALFFMpegEncContext));

  context->pCodec = NULL;
  context->pCodecContext = NULL;
  context->pFrame = NULL;
  context->pPacket = NULL;

  return &(context->stAlEncBaseContext.stAlBaseContext);
}

RETURN al_enc_init(ALBaseContext *ctx, MppVencPara *para) {
  if (!ctx) return MPP_NULL_POINTER;
  S32 ret = 0;

  ALFFMpegEncContext *context = (ALFFMpegEncContext *)ctx;

  context->pCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
  if (!context->pCodec) {
    error("can not find encoder, please check!");
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

  context->pCodecContext->bit_rate = 400000;  // put sample parameters
  context->pCodecContext->width = 1280;
  context->pCodecContext->height = 720;
  context->pCodecContext->time_base = (AVRational){1, 25};
  context->pCodecContext->framerate = (AVRational){25, 1};

  context->pCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;

  av_opt_set(context->pCodecContext->priv_data, "tune", "zerolatency", 0);

  if (avcodec_open2(context->pCodecContext, context->pCodec, NULL) < 0) {
    error("Could not open codec");
    return MPP_NULL_POINTER;
  }

  context->pFrame->format = context->pCodecContext->pix_fmt;
  context->pFrame->width = context->pCodecContext->width;
  context->pFrame->height = context->pCodecContext->height;

  ret = av_frame_get_buffer(context->pFrame, 0);

  if (ret != 0) return MPP_NULL_POINTER;

  debug("init finish");

  return MPP_OK;
}

S32 al_enc_set_para(ALBaseContext *ctx, MppVencPara *para) { return 0; }

S32 al_enc_encode(ALBaseContext *ctx, MppData *sink_data) {
  ALFFMpegEncContext *enc_context = (ALFFMpegEncContext *)ctx;
  S32 ret = 0;
  MppFrame *sink_frame = FRAME_GetFrame(sink_data);

  ret = av_frame_make_writable(enc_context->pFrame);
  if (ret < 0) return MPP_ERROR_UNKNOWN;

  enc_context->pFrame->data[0] = (U8 *)FRAME_GetDataPointer(sink_frame, 0);
  enc_context->pFrame->data[1] = (U8 *)FRAME_GetDataPointer(sink_frame, 1);
  enc_context->pFrame->data[2] = (U8 *)FRAME_GetDataPointer(sink_frame, 2);
  enc_context->pFrame->pts = 1;
  // encode a frame
  ret = avcodec_send_frame(enc_context->pCodecContext, enc_context->pFrame);
  if (ret < 0) {
    error("Error sending a frame for encoding");
  }
  debug("encode ret = %d", ret);

  return ret;
}

S32 al_enc_request_output_stream(ALBaseContext *ctx, MppData *src_data) {
  ALFFMpegEncContext *enc_context = (ALFFMpegEncContext *)ctx;
  S32 ret = 0;

  ret =
      avcodec_receive_packet(enc_context->pCodecContext, enc_context->pPacket);
  debug("receive ret = %d, enc_context->pPacket->size = %d", ret,
        enc_context->pPacket->size);
  if (AVERROR(EAGAIN) == ret || AVERROR_EOF == ret)
    return 5;
  else if (ret < 0) {
    error("Error during encoding");
    return 6;
  }

  PACKET_Alloc(PACKET_GetPacket(src_data), enc_context->pPacket->size);
  // PACKET_GetPacket(src_data)->pData = (
  // U8*)malloc(enc_context->pPacket->size);
  // PACKET_GetPacket(src_data)->nLength = enc_context->pPacket->size;
  // PACKET_SetLength(PACKET_GetPacket(src_data), enc_context->pPacket->size);

  memcpy(PACKET_GetDataPointer(PACKET_GetPacket(src_data)),
         enc_context->pPacket->data, enc_context->pPacket->size);

  av_packet_unref(enc_context->pPacket);

  return 1;
}

void al_enc_destory(ALBaseContext *ctx) {
  if (!ctx) return;

  ALFFMpegEncContext *context = (ALFFMpegEncContext *)ctx;

  if (context->pCodecContext) avcodec_free_context(&(context->pCodecContext));
  if (context->pFrame) av_frame_free(&(context->pFrame));
  if (context->pPacket) av_packet_free(&(context->pPacket));

  free(context);
  context = NULL;
}
