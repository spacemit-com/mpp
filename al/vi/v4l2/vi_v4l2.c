/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-04-25 20:00:13
 * @LastEditTime: 2024-04-26 15:34:51
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

typedef struct _ALViV4l2Context ALViV4l2Context;

struct _ALViV4l2Context {
  /**
   * parent class
   */
  ALViBaseContext stAlViBaseContext;

  S32 nOutputWidth;
  S32 nOutputHeight;
  MppPixelFormat eOutputPixelFormat;
  U8 pVideoDevice[128];
  S32 fd;
  struct v4l2_capability cap;
  struct v4l2_format format;
  struct v4l2_requestbuffers reqbuf;
  struct v4l2_buffer buf;
  enum v4l2_buf_type type;
  void *buffer;
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
  context->eOutputPixelFormat = para->ePixelFormat;
  memcpy(context->pVideoDevice, para->pVideoDeviceName,
         strlen(para->pVideoDeviceName));

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
  context->reqbuf.count = 1;
  if (-1 == ioctl(context->fd, VIDIOC_REQBUFS, &(context->reqbuf))) {
    error("Failed to request buffers");
    goto exit;
  }

  // query buffers
  context->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  context->buf.memory = V4L2_MEMORY_MMAP;
  context->buf.index = 0;
  if (-1 == ioctl(context->fd, VIDIOC_QUERYBUF, &(context->buf))) {
    error("Failed to query buffer");
    goto exit;
  }
  context->buffer = mmap(NULL, context->buf.length, PROT_READ | PROT_WRITE,
                         MAP_SHARED, context->fd, context->buf.m.offset);
  if (context->buffer == MAP_FAILED) {
    error("Failed to mmap buffer");
    goto exit;
  }

  // queue buffers
  if (-1 == ioctl(context->fd, VIDIOC_QBUF, &(context->buf))) {
    error("Failed to enqueue buffer");
    munmap(context->buffer, context->buf.length);
    goto exit;
  }

  // stream on
  context->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl(context->fd, VIDIOC_STREAMON, &(context->type)) == -1) {
    error("Failed to start streaming");
    munmap(context->buffer, context->buf.length);
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

  ret = runPoll(&p);
  if (MPP_OK == ret && p.revents & POLLIN) {
    if (ioctl(context->fd, VIDIOC_DQBUF, &(context->buf)) == -1) {
      error("======== Failed to dequeue buffer");
      munmap(context->buffer, context->buf.length);
      close(context->fd);
      return -1;
    }
    FRAME_SetDataUsedNum(src_frame, 1);
    FRAME_SetDataPointer(src_frame, 0, context->buffer);
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

  if (ioctl(context->fd, VIDIOC_QBUF, &(context->buf)) == -1) {
    error("======== Failed to queue buffer");
    munmap(context->buffer, context->buf.length);
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
    munmap(context->buffer, context->buf.length);
    close(context->fd);
  }

  // munmap buffers
  munmap(context->buffer, context->buf.length);

  // close fd
  close(context->fd);

  free(context);
}