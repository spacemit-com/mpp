/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-02-01 10:43:49
 * @LastEditTime: 2023-11-09 15:53:56
 * @Description: video encode plugin for V4L2 codec standard interface
 */

#define ENABLE_DEBUG 1

#include <errno.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "al_interface_enc.h"
#include "log.h"
#include "v4l2_utils.h"

#define MODULE_TAG "v4l2enc"

#define INPUT_BUF_NUM (3)
#define OUTPUT_BUF_NUM (3)
#define BS_BUF_SIZE (1 << 20)

CODING_TYPE_MAPPING_DEFINE(V4l2Enc, S32)
static const ALV4l2EncCodingTypeMapping stALV4l2EncCodingTypeMapping[] = {
    {CODING_H264, V4L2_PIX_FMT_H264},
    {CODING_H265, V4L2_PIX_FMT_HEVC},
    {CODING_MJPEG, V4L2_PIX_FMT_MJPEG},
    {CODING_VP8, V4L2_PIX_FMT_VP8},
    {CODING_VP9, V4L2_PIX_FMT_VP9},
    {CODING_AV1, 0},
    {CODING_AVS, 0},
    {CODING_AVS2, 0},
    {CODING_MPEG1, V4L2_PIX_FMT_MPEG},
    {CODING_MPEG2, V4L2_PIX_FMT_MPEG2},
    {CODING_MPEG4, V4L2_PIX_FMT_MPEG4},
    {CODING_FWHT, V4L2_PIX_FMT_FWHT},
};
CODING_TYPE_MAPPING_CONVERT(V4l2Enc, v4l2enc, S32)

typedef struct _ALV4l2EncContext ALV4l2EncContext;

struct _ALV4l2EncContext {
  ALEncBaseContext stAlEncBaseContext;
  U8 sDevicePath[20];
  S32 nVideoFd;

  // common
  S32 nMemType;
  S32 nWidth;
  S32 nHeight;

  // in
  S32 nInputFormatFourcc;
  enum v4l2_buf_type eInputBufType;
  struct v4l2_format stInputFormat;
  struct v4l2_buffer stInputBufArr[INPUT_BUF_NUM];
  struct v4l2_plane stInputBufPlanes[INPUT_BUF_NUM][VIDEO_MAX_PLANES];
  U8 *pInputUserPtr[INPUT_BUF_NUM][8];

  // out
  S32 nOutputFormatFourcc;
  enum v4l2_buf_type eOutputBufType;
  struct v4l2_format stOutputFormat;
  struct v4l2_buffer stOutputBufArr[OUTPUT_BUF_NUM];
  struct v4l2_plane stOutputBufPlanes[OUTPUT_BUF_NUM][VIDEO_MAX_PLANES];
  U8 *pOutputUserPtr[OUTPUT_BUF_NUM][8];
};

ALBaseContext *al_enc_create() {
  ALV4l2EncContext *context =
      (ALV4l2EncContext *)malloc(sizeof(ALV4l2EncContext));
  if (!context) {
    error("can not malloc ALV4l2EncContext, please check! (%s)",
          strerror(errno));
    return NULL;
  }

  return &(context->stAlEncBaseContext.stAlBaseContext);
}

RETURN al_enc_init(ALBaseContext *ctx, MppVencPara *para) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!para) {
    error("input para MppVencData is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  S32 ret = 0;

  ALV4l2EncContext *context = (ALV4l2EncContext *)ctx;

  context->nVideoFd = find_v4l2_encoder(
      context->sDevicePath,
      V4L2_PIX_FMT_FWHT /*get_v4l2enc_coding_type(para->eCodingType)*/);

  if (-1 == context->nVideoFd) {
    error("can not find the v4l2 codec device, please check!");
    return MPP_OPEN_FAILED;
  }

  debug("video fd = %d, device path = '%s'", context->nVideoFd,
        context->sDevicePath);

  context->nMemType = V4L2_MEMORY_MMAP;

  context->nInputFormatFourcc = V4L2_PIX_FMT_YUV420;
  context->eInputBufType = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  // context->nOutputFormatFourcc = V4L2_PIX_FMT_YUV420M;
  // context->eOutputBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  context->nOutputFormatFourcc = V4L2_PIX_FMT_FWHT;
  context->eOutputBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  context->nWidth = 1280;
  context->nHeight = 720;

  // set naul format
  // mpp_v4l2_set_ctrl(context->nVideoFd, V4L2_CID_MVE_VIDEO_NALU_FORMAT,
  // V4L2_OPT_NALU_FORMAT_START_CODES);

  // set format
  {
    debug("=========== frame format ============");
    ret = mpp_v4l2_get_format(context->nVideoFd, &(context->stInputFormat),
                              context->eInputBufType);
    if (ret) {
      error("get format failed, please check!");
      return MPP_IOCTL_FAILED;
    }

    struct v4l2_pix_format *pix_format_tmp = &(context->stInputFormat.fmt.pix);
    pix_format_tmp->pixelformat = context->nInputFormatFourcc;
    pix_format_tmp->width = context->nWidth;
    pix_format_tmp->height = context->nHeight;
    pix_format_tmp->bytesperline = 0;
    pix_format_tmp->sizeimage = (1 << 20);
    pix_format_tmp->field = V4L2_FIELD_NONE;
    ret = mpp_v4l2_try_format(context->nVideoFd, &(context->stInputFormat));
    if (ret) {
      error("try format failed, please check!");
      return MPP_IOCTL_FAILED;
    }

    context->nWidth = pix_format_tmp->width;
    context->nHeight = pix_format_tmp->height;
    debug("after try_format of frame, w = %d, h = %d", context->nWidth,
          context->nHeight);
    ret = mpp_v4l2_set_format(context->nVideoFd, &(context->stInputFormat));
    if (ret) {
      error("set format failed, please check!");
      return MPP_IOCTL_FAILED;
    }
  }

  {
    debug("============ stream format ==========");
    ret = mpp_v4l2_get_format(context->nVideoFd, &(context->stOutputFormat),
                              context->eOutputBufType);
    if (ret) {
      error("get format failed, please check!");
      return MPP_IOCTL_FAILED;
    }

    // struct v4l2_pix_format_mplane *pix_format_tmp =
    // &(context->stOutputFormat.fmt.pix_mp);
    struct v4l2_pix_format *pix_format_tmp = &(context->stOutputFormat.fmt.pix);
    pix_format_tmp->pixelformat = context->nOutputFormatFourcc;
    pix_format_tmp->width = context->nWidth;
    pix_format_tmp->height = context->nHeight;

    // pix_format_tmp->num_planes  = 3;
    pix_format_tmp->field = V4L2_FIELD_NONE;
    ret = mpp_v4l2_try_format(context->nVideoFd, &(context->stOutputFormat));
    if (ret) {
      error("try format failed, please check!");
      return MPP_IOCTL_FAILED;
    }

    context->nWidth = pix_format_tmp->width;
    context->nHeight = pix_format_tmp->height;
    debug("after try_format of stream, w = %d, h = %d", context->nWidth,
          context->nHeight);
    ret = mpp_v4l2_set_format(context->nVideoFd, &(context->stOutputFormat));
    if (ret) {
      error("set format failed, please check!");
      return MPP_IOCTL_FAILED;
    }
  }

  // alloc input buffers
  struct v4l2_requestbuffers reqbuf_in_tmp;
  reqbuf_in_tmp.count = INPUT_BUF_NUM;
  reqbuf_in_tmp.type = context->eInputBufType;
  reqbuf_in_tmp.memory = context->nMemType;

  ret = mpp_v4l2_req_buffers(context->nVideoFd, &reqbuf_in_tmp);
  if (ret) {
    error("request buffers failed, please check!");
    return MPP_IOCTL_FAILED;
  }
  debug("=======> bitstream buffer count : %d", reqbuf_in_tmp.count);
  for (S32 i = 0; i < reqbuf_in_tmp.count; i++) {
    context->stInputBufArr[i].type = context->eInputBufType;
    context->stInputBufArr[i].memory = context->nMemType;
    // context->stInputBufArr[i].m.planes = context->stInputBufPlanes[i];
    context->stInputBufArr[i].index = i;
    // context->stInputBufArr[i].length   = 1024*1024;

    ret =
        mpp_v4l2_query_buffer(context->nVideoFd, &(context->stInputBufArr[i]));
    if (ret) {
      error("query buffer failed, please check!");
      return MPP_IOCTL_FAILED;
    }

    if (V4L2_MEMORY_MMAP == context->nMemType) {
      debug("mmap input buffer! length = %d, offset = %d",
            context->stInputBufArr[i].length,
            context->stInputBufArr[i].m.offset);
      ret = mpp_v4l2_map_memory(context->nVideoFd, &(context->stInputBufArr[i]),
                                context->pInputUserPtr[i]);
      if (ret) {
        error("mmap buffer failed, please check!");
        return MPP_MMAP_FAILED;
      }
    }
  }

  struct v4l2_requestbuffers reqbuf_out_tmp;
  reqbuf_out_tmp.count = OUTPUT_BUF_NUM;
  reqbuf_out_tmp.type = context->eOutputBufType;
  reqbuf_out_tmp.memory = context->nMemType;

  ret = mpp_v4l2_req_buffers(context->nVideoFd, &reqbuf_out_tmp);
  if (ret) {
    error("request buffers failed, please check!");
    return MPP_IOCTL_FAILED;
  }
  debug("=======> output yuv buffer count : %d", reqbuf_out_tmp.count);
  for (S32 i = 0; i < reqbuf_out_tmp.count; i++) {
    context->stOutputBufArr[i].type = context->eOutputBufType;
    context->stOutputBufArr[i].memory = context->nMemType;
    // context->stOutputBufArr[i].m.planes = context->stOutputBufPlanes[i];
    context->stOutputBufArr[i].index = i;
    // context->stOutputBufArr[i].length   = 1280*720*3;

    ret =
        mpp_v4l2_query_buffer(context->nVideoFd, &(context->stOutputBufArr[i]));
    if (ret) {
      error("query buffer failed, please check!");
      return MPP_IOCTL_FAILED;
    }

    if (V4L2_MEMORY_MMAP == context->nMemType) {
      debug("mmap output buffer! length = %d, offset = %d",
            context->stInputBufArr[i].length,
            context->stInputBufArr[i].m.offset);
      ret =
          mpp_v4l2_map_memory(context->nVideoFd, &(context->stOutputBufArr[i]),
                              context->pOutputUserPtr[i]);
      if (ret) {
        error("mmap buffer failed, please check!");
        return MPP_MMAP_FAILED;
      }
    }
  }

  // queue output buffer
  for (S32 i = 0; i < OUTPUT_BUF_NUM; i++) {
    // remove vendor custom flags.
    // context->stOutputBufArr[i].flags &= ~V4L2_BUF_FLAG_MVX_MASK;

    // mask buffer offset
    if (!V4L2_TYPE_IS_MULTIPLANAR(context->stOutputBufArr[i].type) &&
        V4L2_MEMORY_MMAP == context->stOutputBufArr[i].memory) {
      context->stOutputBufArr[i].m.offset &= ~((1 << 12) - 1);
    }
    debug("queue all output buffer!");
    ret =
        mpp_v4l2_queue_buffer(context->nVideoFd, &(context->stOutputBufArr[i]));
    if (ret) {
      error("queue buffer failed, please check!");
      return MPP_IOCTL_FAILED;
    }
  }

  // stream on
  debug("stream on!");
  ret = mpp_v4l2_stream_on(context->nVideoFd, &context->eInputBufType);
  if (ret) {
    error("stream on input buffer failed, please check!");
    return MPP_IOCTL_FAILED;
  }
  ret = mpp_v4l2_stream_on(context->nVideoFd, &context->eOutputBufType);
  if (ret) {
    error("stream on output buffer failed, please check!");
    return MPP_IOCTL_FAILED;
  }

  debug("init finish");

  return MPP_OK;
}

S32 al_enc_set_para(ALBaseContext *ctx, MppVencPara *para) { return 0; }

S32 al_enc_encode(ALBaseContext *ctx, MppData *sink_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!sink_data) {
    error("input para MppData is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALV4l2EncContext *context = (ALV4l2EncContext *)ctx;
  S32 ret = 0;
  static S32 i = 0;

  if (i < INPUT_BUF_NUM) {
    memcpy(context->pInputUserPtr[i][0],
           PACKET_GetDataPointer(PACKET_GetPacket(sink_data)),
           PACKET_GetLength(PACKET_GetPacket(sink_data)));
    context->stInputBufArr[i].bytesused =
        PACKET_GetLength(PACKET_GetPacket(sink_data));
    context->stInputBufArr[i].flags |= V4L2_BUF_FLAG_LAST;
    ret = mpp_v4l2_queue_buffer(context->nVideoFd, &context->stInputBufArr[i]);
    i++;
  } else {
    struct v4l2_buffer buf_tmp;

    buf_tmp.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf_tmp.memory = V4L2_MEMORY_MMAP;
    ret = mpp_v4l2_dequeue_buffer(context->nVideoFd, &buf_tmp);
    memcpy(context->pInputUserPtr[buf_tmp.index][0],
           PACKET_GetDataPointer(PACKET_GetPacket(sink_data)),
           PACKET_GetLength(PACKET_GetPacket(sink_data)));
    context->stInputBufArr[buf_tmp.index].bytesused =
        PACKET_GetLength(PACKET_GetPacket(sink_data));
    context->stInputBufArr[buf_tmp.index].flags |= V4L2_BUF_FLAG_LAST;
    ret = mpp_v4l2_queue_buffer(context->nVideoFd,
                                &context->stInputBufArr[buf_tmp.index]);
  }

  return 0;
}

S32 al_enc_request_output_stream(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!src_data) {
    error("input para MppData is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALV4l2EncContext *context = (ALV4l2EncContext *)ctx;
  S32 ret = 0;

  debug("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@request output");

  struct v4l2_buffer buf_tmp;
  // struct v4l2_plane  planes[8];
  // buf_tmp.m.planes = planes;
  buf_tmp.type = context->eOutputBufType;
  buf_tmp.memory = context->nMemType;
  // buf_tmp.length   = 3;

  ret = mpp_v4l2_dequeue_buffer(context->nVideoFd, &buf_tmp);
  if (ret) {
    error("dequeue output buffer failed, please check!");
    return MPP_IOCTL_FAILED;
  }

  context->stOutputBufArr[buf_tmp.index] = buf_tmp;
  if (V4L2_TYPE_IS_MULTIPLANAR(buf_tmp.type)) {
    context->stOutputBufArr[buf_tmp.index].m.planes =
        context->stOutputBufPlanes[buf_tmp.index];
    for (size_t i = 0; i < context->stOutputBufArr[buf_tmp.index].length; ++i) {
      context->stOutputBufArr[buf_tmp.index].m.planes[i] = buf_tmp.m.planes[i];
    }
  }

  // handle output picture
  struct v4l2_buffer *p_tmp = &context->stOutputBufArr[buf_tmp.index];
  // U8 *user_ptr = context->pOutputUserPtr[buf_idx];
  show_buffer_info(p_tmp);
  for (S32 i = 0; i < p_tmp->length; ++i) {
    // struct v4l2_plane *plane = &(p->m.planes);

    // U8 *pl     = user_ptr[i] + plane->data_offset;
    // S32   len    = plane->bytesused - plane->data_offset;
    // S32   wr_len = fwrite(pl, 1, len, out_pic);
  }

  FRAME_SetID(FRAME_GetFrame(src_data), buf_tmp.index);

  debug("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@ request output idx = %d",
        buf_tmp.index);

  return 0;
}

S32 al_enc_return_output_stream(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!src_data) {
    error("input para MppData is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALV4l2EncContext *context = (ALV4l2EncContext *)ctx;
  S32 ret = 0;

  S32 buf_idx = FRAME_GetID(FRAME_GetFrame(src_data));
  debug("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@release output idx = %d", buf_idx);

  ret = mpp_v4l2_queue_buffer(context->nVideoFd,
                              &context->stOutputBufArr[buf_idx]);
  if (ret) {
    error("queue output buffer failed, please check!");
    return MPP_IOCTL_FAILED;
  }
  debug("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@release output ret = %d", ret);

  return MPP_OK;
}

void al_enc_destory(ALBaseContext *ctx) {
  if (!ctx) return;

  ALV4l2EncContext *context = (ALV4l2EncContext *)ctx;

  mpp_v4l2_stream_off(context->nVideoFd, &context->eInputBufType);
  mpp_v4l2_stream_off(context->nVideoFd, &context->eOutputBufType);

  close(context->nVideoFd);
  free(context);
  context = NULL;
}
