/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-04-28 17:10:20
 * @LastEditTime: 2024-04-30 10:30:52
 * @FilePath: \mpp\al\vi\file\vi_file.c
 * @Description:
 */

#define ENABLE_DEBUG 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "al_interface_vi.h"
#include "log.h"
#include "parse.h"

#define MODULE_TAG "vi_file"

#define MPP_PACKET_PARSE_REGION_SIZE (2 * 1024 * 1024)
#define FRAME_FREAD(data, size, count, fp)        \
  ({                                              \
    S32 total = (size) * (count);                 \
    S32 n = fread((data), (size), (count), (fp)); \
    if (n != total && ferror(fp)) {               \
      error("Failed to read frame from file");    \
      exit(EXIT_FAILURE);                         \
    }                                             \
    n;                                            \
  })

typedef struct _ALViFileContext ALViFileContext;

struct _ALViFileContext {
  /**
   * parent class
   */
  ALViBaseContext stAlViBaseContext;
  BOOL bIsFrame;

  S32 nOutputWidth;
  S32 nOutputHeight;
  S32 nOutputStride;
  MppPixelFormat eOutputPixelFormat;
  MppCodingType eCodingType;
  U8 *pInputFileName;
  FILE *pInputFile;
  MppParseContext *pParseCtx;
  S32 nFileOffset;
  S64 nTimeStamp;

  U8 *stream_data;
  U8 *tmp_stream_data;
  S32 stream_length;
  S32 need_drain;
  S32 length;
  S32 fileSize;
  BOOL eos;
};

ALBaseContext *al_vi_create() {
  ALViFileContext *context = (ALViFileContext *)malloc(sizeof(ALViFileContext));
  if (!context) {
    error("can not malloc ALViFileContext, please check!");
    return NULL;
  }

  memset(context, 0, sizeof(ALViFileContext));

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

  ALViFileContext *context = (ALViFileContext *)ctx;
  S32 ret = 0;

  context->bIsFrame = para->bIsFrame;
  context->nOutputWidth = para->nWidth;
  context->nOutputHeight = para->nHeight;
  context->nOutputStride = para->nStride;
  context->eOutputPixelFormat = para->ePixelFormat;
  context->pInputFileName = para->pInputFileName;
  context->eCodingType = para->eCodingType;
  context->stream_data = NULL;
  context->tmp_stream_data = NULL;
  context->stream_length = 0;
  context->need_drain = 0;
  context->length = 0;
  context->eos = MPP_FALSE;

  context->pInputFile = fopen(context->pInputFileName, "r");
  if (!context->pInputFile) {
    error("can not open context->pInputFileName, please check !");
    goto exit;
  }

  if (!context->bIsFrame) {
    // create parser
    context->pParseCtx = PARSE_Create(context->eCodingType);
    if (!context->pParseCtx) {
      error("create context->pParseCtx failed, please check!");
      goto exit;
    }
    context->pParseCtx->ops->init(context->pParseCtx);

    context->stream_data = (U8 *)malloc(MPP_PACKET_PARSE_REGION_SIZE);
    context->tmp_stream_data = context->stream_data;
  }

  context->nFileOffset = 0;

  fseek(context->pInputFile, 0, SEEK_END);
  context->fileSize = ftell(context->pInputFile);
  rewind(context->pInputFile);
  debug("start do_parse: %d", context->fileSize);

  debug("init finish");

  return MPP_OK;

exit:
  error("k1 vi_sdl2 init fail");
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
    error("input para MppData is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALViFileContext *context = (ALViFileContext *)ctx;
  S32 ret = 0;

  if (context->bIsFrame) {
    MppFrame *sink_frame = FRAME_GetFrame(sink_data);

  } else {
    MppPacket *sink_packet = PACKET_GetPacket(sink_data);
  }

  return MPP_OK;
}

S32 al_vi_request_output_data(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!src_data) {
    error("input para MppData is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALViFileContext *context = (ALViFileContext *)ctx;
  S32 ret = 0;

  if (context->bIsFrame) {
    MppFrame *sink_frame = FRAME_GetFrame(src_data);
    S32 read_byte = 0, size[3], pnum, i;

    switch (context->eOutputPixelFormat) {
      case PIXEL_FORMAT_I420:
        size[0] = context->nOutputWidth * context->nOutputHeight;
        size[1] = (context->nOutputWidth / 2) * (context->nOutputHeight / 2);
        size[2] = (context->nOutputWidth / 2) * (context->nOutputHeight / 2);
        pnum = 3;
        break;
      case PIXEL_FORMAT_YUV422P:
        size[0] = context->nOutputWidth * context->nOutputHeight;
        size[1] = (context->nOutputWidth / 2) * (context->nOutputHeight);
        size[2] = (context->nOutputWidth / 2) * (context->nOutputHeight);
        pnum = 3;
        break;
      case PIXEL_FORMAT_NV12:
      case PIXEL_FORMAT_NV21:
        size[0] = context->nOutputWidth * context->nOutputHeight;
        size[1] = (context->nOutputWidth / 2) * (context->nOutputHeight);
        pnum = 2;
        break;
      case PIXEL_FORMAT_RGBA:
      case PIXEL_FORMAT_ARGB:
      case PIXEL_FORMAT_BGRA:
      case PIXEL_FORMAT_ABGR:
        size[0] = context->nOutputWidth * context->nOutputHeight * 4;
        pnum = 1;
        break;
      case PIXEL_FORMAT_YUYV:
      case PIXEL_FORMAT_UYVY:
        size[0] = context->nOutputWidth * context->nOutputHeight * 2;
        pnum = 1;
        break;
      default:
        error("Unsupported picture format (%d)", context->eOutputPixelFormat);
        return -MPP_CHECK_FAILED;
    }

    for (i = 0; i < pnum; i++) {
      read_byte += FRAME_FREAD(FRAME_GetDataPointer(sink_frame, i), 1, size[i],
                               context->pInputFile);
      if (read_byte == 0) context->eos = MPP_TRUE;
    }
  } else {
    MppPacket *sink_packet = PACKET_GetPacket(src_data);

    context->stream_data = context->tmp_stream_data;
    context->stream_length =
        fread(context->stream_data, 1, MPP_PACKET_PARSE_REGION_SIZE,
              context->pInputFile);
    debug("stream_length = %d length = %d, offset = %d", context->stream_length,
          context->length, context->nFileOffset);

    if (0 == context->stream_length) {
      error("There is no data, quit!");
      context->eos = MPP_TRUE;
      PACKET_SetEos(sink_packet, MPP_TRUE);
      PACKET_SetLength(sink_packet, 0);
      return MPP_CODER_EOS;
    }

    ret = context->pParseCtx->ops->parse(
        context->pParseCtx, (U8 *)(context->stream_data),
        context->stream_length, (U8 *)PACKET_GetDataPointer(sink_packet),
        &(context->length), 0);
    if (0 == ret) {
      // context->stream_data += context->length;
      // context->stream_length -= context->length;
      context->nFileOffset += context->length;

      PACKET_SetEos(sink_packet, MPP_FALSE);
      PACKET_SetLength(sink_packet, context->length);
      PACKET_SetPts(sink_packet, context->nTimeStamp);
      context->nTimeStamp += 1000000;

      debug("we get a packet, length = %d, ret = %d %p %x %x %x %x",
            context->length, ret, PACKET_GetDataPointer(sink_packet),
            *(S32 *)PACKET_GetDataPointer(sink_packet),
            *(S32 *)(PACKET_GetDataPointer(sink_packet) + 4),
            *(S32 *)(PACKET_GetDataPointer(sink_packet) + 8),
            *(S32 *)(PACKET_GetDataPointer(sink_packet) + 12));

      fseek(context->pInputFile, context->nFileOffset, SEEK_SET);
      debug("fileoffset = %d", context->nFileOffset);
    } else {
      error("something wrong?");
    }
  }

  if (context->eos) return MPP_CODER_EOS;

  return MPP_OK;
}

S32 al_vi_return_output_data(ALBaseContext *ctx, MppData *src_data) {}

void al_vi_destory(ALBaseContext *ctx) {
  ALViFileContext *context = (ALViFileContext *)ctx;
  S32 ret = 0;

  if (context->pInputFile) {
    fflush(context->pInputFile);
    fclose(context->pInputFile);
    context->pInputFile = NULL;
  }

  if (context->pParseCtx) {
    PARSE_Destory(context->pParseCtx);
    context->pParseCtx = NULL;
  }

  free(context);
}
