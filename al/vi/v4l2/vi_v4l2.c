/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-04-25 20:00:13
 * @LastEditTime: 2024-04-26 17:43:28
 * @FilePath: \mpp\al\vi\v4l2\vi_v4l2.c
 * @Description:
 */

#define ENABLE_DEBUG 1

#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "al_interface_vi.h"
#include "log.h"

#define MODULE_TAG "vi_v4l2"

#define POLL_TIMEOUT 0

PIXEL_FORMAT_MAPPING_DEFINE(ViV4l2, S32)
static const ALViV4l2PixelFormatMapping stALViV4l2PixelFormatMapping[] = {
    {PIXEL_FORMAT_I420, V4L2_PIX_FMT_YUV420M},
    {PIXEL_FORMAT_NV12, V4L2_PIX_FMT_NV12},
    {PIXEL_FORMAT_NV21, V4L2_PIX_FMT_NV21},
    {PIXEL_FORMAT_YV12, V4L2_PIX_FMT_YVU420M},
    {PIXEL_FORMAT_UYVY, V4L2_PIX_FMT_UYVY},
    {PIXEL_FORMAT_YUYV, V4L2_PIX_FMT_YUYV},
};
PIXEL_FORMAT_MAPPING_CONVERT(ViV4l2, viv4l2, S32)

typedef struct _ALViV4l2Context ALViV4l2Context;

struct _ALViV4l2Context {
  /**
   * parent class
   */
  ALViBaseContext stAlViBaseContext;

  S32 nOutputWidth;
  S32 nOutputHeight;
  U32 nFormatFourcc;
  U32 nMemType;
  U32 nBufferNum;
  U8 pVideoDevice[128];
  S32 fd;
  struct v4l2_capability cap;
  struct v4l2_format format;
  struct v4l2_requestbuffers reqbuf;
  struct v4l2_buffer buf[VIDEO_MAX_FRAME];
  enum v4l2_buf_type type;
  U8 *buffer[VIDEO_MAX_FRAME];

  BOOL bIsPixfmtSupported;
  BOOL bIsResolutionSupported;
};

S32 runPoll(struct pollfd *p) {
  S32 ret = poll(p, 1, POLL_TIMEOUT);
  // debug("poll ret = %d p->revents=%x", ret, p->revents);

  if (ret < 0) {
    error("Poll returned error code.");
    return MPP_POLL_FAILED;
  }

  if (p->revents & POLLERR) {
    error("Poll returned error event.");
    return MPP_POLL_FAILED;
  }

  if (0 == ret) {
    // error("Queue and dequeue poll timed out.");
    return MPP_POLL_FAILED;
  }

  return MPP_OK;
}

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
  context->nFormatFourcc = get_viv4l2_codec_pixel_format(para->ePixelFormat);
  memcpy(context->pVideoDevice, para->pVideoDeviceName,
         strlen(para->pVideoDeviceName));
  context->nMemType = V4L2_MEMORY_MMAP;
  if (para->eFrameBufferType == MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL) {
    context->nMemType = V4L2_MEMORY_DMABUF;
  } else if (para->eFrameBufferType == MPP_FRAME_BUFFERTYPE_NORMAL_INTERNAL) {
    context->nMemType = V4L2_MEMORY_MMAP;
  }
  context->nBufferNum = para->nBufferNum;

  // open video device
  context->fd = open(context->pVideoDevice, O_RDWR);
  if (-1 == context->fd) {
    error("Failed to open video device(%s)", context->pVideoDevice);
    goto exit;
  }

  // query capibility
  if (-1 == ioctl(context->fd, VIDIOC_QUERYCAP, &(context->cap))) {
    error("Failed to query capability");
    goto exit;
  }

  debug("Driver: %s", context->cap.driver);
  debug("Card: %s", context->cap.card);
  debug("Bus info: %s", context->cap.bus_info);
  debug("Capabilities: 0x%08x", context->cap.capabilities);

  // enum support formats
  context->bIsPixfmtSupported = MPP_FALSE;
  struct v4l2_fmtdesc fmt;
  memset(&fmt, 0, sizeof(fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.index = 0;
  debug("Supported formats:");
  while (ioctl(context->fd, VIDIOC_ENUM_FMT, &fmt) != -1) {
    debug("\tFormat: %s", fmt.description);
    if (fmt.pixelformat == context->nFormatFourcc) {
      context->bIsPixfmtSupported = MPP_TRUE;
    }
    fmt.index++;
  }

  if (!context->bIsPixfmtSupported) goto exit;

  // check resolution support
  context->bIsResolutionSupported = MPP_FALSE;
  struct v4l2_frmsizeenum frmsize;
  memset(&frmsize, 0, sizeof(frmsize));
  frmsize.pixel_format = context->nFormatFourcc;
  debug("Supported resolutions for format YUYV:");
  while (ioctl(context->fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) != -1) {
    if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
      debug("\tResolution: %dx%d", frmsize.discrete.width,
            frmsize.discrete.height);
      if (frmsize.discrete.width == context->nOutputWidth &&
          frmsize.discrete.height == context->nOutputHeight) {
        context->bIsResolutionSupported = MPP_TRUE;
      }
    } else if (frmsize.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
      debug("\tResolution: %dx%d (stepwise)", frmsize.stepwise.max_width,
            frmsize.stepwise.max_height);
    }
    frmsize.index++;
  }

  if (!context->bIsResolutionSupported) goto exit;

  // set fmt
  context->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  context->format.fmt.pix.width = context->nOutputWidth;
  context->format.fmt.pix.height = context->nOutputHeight;
  context->format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
  context->format.fmt.pix.field = V4L2_FIELD_NONE;
  if (-1 == ioctl(context->fd, VIDIOC_S_FMT, &(context->format))) {
    error("Failed to set format");
    goto exit;
  }

  // request buffers
  context->reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  context->reqbuf.memory = V4L2_MEMORY_MMAP;
  context->reqbuf.count = context->nBufferNum;
  if (-1 == ioctl(context->fd, VIDIOC_REQBUFS, &(context->reqbuf))) {
    error("Failed to request buffers");
    goto exit;
  }

  if (context->nBufferNum != context->reqbuf.count) {
    error("can not request so many buffers, want(%d), actual(%d)",
          context->nBufferNum, context->reqbuf.count);
    context->nBufferNum = context->reqbuf.count;
  }

  // query buffers
  for (S32 i = 0; i < context->nBufferNum; i++) {
    context->buf[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    context->buf[i].memory = V4L2_MEMORY_MMAP;
    context->buf[i].index = i;
    if (-1 == ioctl(context->fd, VIDIOC_QUERYBUF, &(context->buf[i]))) {
      error("Failed to query buffer(index=%d)", i);
      goto exit;
    }
    context->buffer[i] =
        mmap(NULL, context->buf[i].length, PROT_READ | PROT_WRITE, MAP_SHARED,
             context->fd, context->buf[i].m.offset);
    if (context->buffer[i] == MAP_FAILED) {
      error("Failed to mmap buffer");
      goto exit;
    }
  }

  // queue buffers
  for (S32 i = 0; i < context->nBufferNum; i++) {
    if (-1 == ioctl(context->fd, VIDIOC_QBUF, &(context->buf[i]))) {
      error("Failed to enqueue buffer(index=%d)", i);
      munmap(context->buffer[i], context->buf[i].length);
      goto exit;
    }
  }

  // stream on
  context->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(context->fd, VIDIOC_STREAMON, &(context->type)) == -1) {
    error("Failed to start streaming");
    // munmap(context->buffer, context->buf.length);
    //  to do munmap
    goto exit;
  }

  debug("init finish");

  return MPP_OK;

exit:
  error("k1 vi_v4l2 init fail");
  if (context->fd > 0) close(context->fd);
  free(context);
  return MPP_INIT_FAILED;
}

S32 al_vi_get_para(ALBaseContext *ctx, MppViPara **para) { return 0; }

S32 al_vi_request_output_frame(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!src_data) {
    error("input para MppData_sink is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALViV4l2Context *context = (ALViV4l2Context *)ctx;
  S32 ret = 0;
  struct pollfd p = {.fd = context->fd, .events = POLLIN};
  MppFrame *src_frame = FRAME_GetFrame(src_data);

  struct v4l2_buffer buf;
  buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buf.memory = V4L2_MEMORY_MMAP;

  ret = runPoll(&p);
  if (MPP_OK == ret && p.revents & POLLIN) {
    if (ioctl(context->fd, VIDIOC_DQBUF, &buf) == -1) {
      error("======== Failed to dequeue buffer");
      // munmap(context->buffer, context->buf.length);
      // to do munmap
      close(context->fd);
      return -1;
    }
    FRAME_SetDataUsedNum(src_frame, 1);
    FRAME_SetID(src_frame, buf.index);
    FRAME_SetDataPointer(src_frame, 0, context->buffer[buf.index]);
  } else {
    error("no data");
    usleep(20000);
    return MPP_CODER_NO_DATA;
  }

  return MPP_OK;
}

S32 al_vi_return_output_frame(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!src_data) {
    error("input para MppData_sink is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALViV4l2Context *context = (ALViV4l2Context *)ctx;
  S32 ret = 0;

  MppFrame *src_frame = FRAME_GetFrame(src_data);
  S32 index = FRAME_GetID(src_frame);

  if (ioctl(context->fd, VIDIOC_QBUF, &(context->buf[index])) == -1) {
    error("======== Failed to queue buffer");
    // munmap(context->buffer, context->buf.length);
    // to do munmap
    close(context->fd);
    return -1;
  }

  return MPP_OK;
}

void al_vi_destory(ALBaseContext *ctx) {
  ALViV4l2Context *context = (ALViV4l2Context *)ctx;
  S32 ret = 0;

  // stream off
  if (-1 == ioctl(context->fd, VIDIOC_STREAMOFF, &(context->type))) {
    error("Failed to stop streaming");
    // munmap(context->buffer, context->buf.length);
    close(context->fd);
  }

  // munmap buffers
  for (S32 i = 0; i < context->nBufferNum; i++) {
    munmap(context->buffer[i], context->buf[i].length);
  }

  // close fd
  close(context->fd);

  free(context);
}