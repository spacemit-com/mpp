/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-09-26 19:28:42
 * @LastEditTime: 2024-04-09 17:57:06
 * @Description:
 */

#define ENABLE_DEBUG 1

#include "linlonv5v7_codec.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MODULE_TAG "linlonv5v7_codec"

/***
 *
 * When we create a decode or encode channel, a codec session will be created in
 * the init function, a codec include two ports(input port and output port),
 * port is responsible for managing buffer.
 *
 *
 *                +--------------------------+
 *                |                          |
 *                |        CODEC             |
 *                |                          |
 *                +---+-----------------+----+
 *                    |                 |
 *                    |                 |
 *                    |                 |
 *   +----------------v-----+       +---v------------------+
 *   |                      |       |                      |
 *   |      INPUT PORT      |       |     OUTPUT PORT      |
 *   |                      |       |                      |
 *   +----------+-----------+       +-----------+----------+
 *              |                               |
 *              |                               |
 *              |                               |
 * +--------+---v----+--------+   +--------+----v---+--------+
 * | BUFFER | BUFFER | BUFFER |   | BUFFER | BUFFER | BUFFER |
 * +--------+--------+--------+   +--------+--------+--------+
 *
 */

struct _Codec {
  /***
   * parameters copyed from dec or enc
   */
  U8 sDevicePath[20];
  S32 nVideoFd;
  BOOL bIsBlockMode;
  S32 nWidth;
  S32 nHeight;
  BOOL bIsInterlaced;
  U32 nInputFormatFourcc;
  U32 nOutputFormatFourcc;
  U32 nInputMemtype;  // V4L2_MEMORY_MMAP/V4L2_MEMORY_USERPTR/V4L2_MEMORY_DMABUF
  U32 nOutputMemtype;  // V4L2_MEMORY_MMAP/V4L2_MEMORY_USERPTR/V4L2_MEMORY_DMABUF
  U32 nInputBufferNum;
  U32 nOutputBufferNum;

  /***
   * context of input port(managing input buffers)
   */
  Port *stInputPort;

  /***
   * context of output port(managing output buffers)
   */
  Port *stOutputPort;

  /***
   * parameters always used for encoder
   */
  BOOL bCsweo;
  U32 nFps;
  U32 nBps;
  U32 nMinqp;
  U32 nMaxqp;
  U32 nFixedqp;

  /***
   * only for frame, not used for packet
   */
  MppFrameBufferType eBufferType;
};

Codec *createCodec(S32 fd, S32 width, S32 height, BOOL isInterlaced,
                   enum v4l2_buf_type inputType, enum v4l2_buf_type outputType,
                   U32 input_format_fourcc, U32 output_format_fourcc,
                   U32 input_memtype, U32 output_memtype, U32 input_buffer_num,
                   U32 output_buffer_num, BOOL block,
                   MppFrameBufferType buffer_type) {
  Codec *codec_tmp = (Codec *)malloc(sizeof(Codec));
  if (!codec_tmp) {
    error("can not malloc Codec, please check! (%s)", strerror(errno));
    return NULL;
  }
  memset(codec_tmp, 0, sizeof(Codec));

  debug(
      "create a codec, width=%d height=%d inputtype=%d outputtype=%d "
      "inputformat=%x outputformat=%x inputbufnum=%d outputbufnum=%d",
      width, height, inputType, outputType, input_format_fourcc,
      output_format_fourcc, input_buffer_num, output_buffer_num);

  codec_tmp->nVideoFd = fd;
  codec_tmp->bIsBlockMode = block;
  codec_tmp->nInputFormatFourcc = input_format_fourcc;
  codec_tmp->nOutputFormatFourcc = output_format_fourcc;
  codec_tmp->nInputMemtype = input_memtype;
  codec_tmp->nOutputMemtype = output_memtype;
  codec_tmp->nInputBufferNum = input_buffer_num;
  codec_tmp->nOutputBufferNum = output_buffer_num;
  codec_tmp->eBufferType = buffer_type;
  codec_tmp->stInputPort =
      createPort(fd, inputType, input_format_fourcc, input_memtype,
                 input_buffer_num, buffer_type);
  if (!codec_tmp->stInputPort) {
    error("create input port failed, please check!");
    free(codec_tmp);
    return NULL;
  }
  codec_tmp->stOutputPort =
      createPort(fd, outputType, output_format_fourcc, output_memtype,
                 output_buffer_num, buffer_type);
  if (!codec_tmp->stOutputPort) {
    error("create output port failed, please check!");
    free(codec_tmp->stInputPort);
    free(codec_tmp);
    return NULL;
  }

  codec_tmp->nWidth = width;
  codec_tmp->nHeight = height;
  codec_tmp->bIsInterlaced = isInterlaced;
  codec_tmp->bCsweo = MPP_FALSE;
  codec_tmp->nFps = 0;
  codec_tmp->nBps = 0;
  codec_tmp->nMinqp = 0;
  codec_tmp->nMaxqp = 0;
  codec_tmp->nFixedqp = 0;

  return codec_tmp;
}

void destoryCodec(Codec *codec) {
  debug("destory input port");
  destoryPort(codec->stInputPort);
  debug("destory output port");
  destoryPort(codec->stOutputPort);
  debug("free codec");
  free(codec);
}

Port *getInputPort(Codec *codec) { return codec->stInputPort; }

Port *getOutputPort(Codec *codec) { return codec->stOutputPort; }

BOOL getCsweo(Codec *codec) { return codec->bCsweo; }

U32 getFps(Codec *codec) { return codec->nFps; }

U32 getBps(Codec *codec) { return codec->nBps; }

U32 getMinqp(Codec *codec) { return codec->nMinqp; }

U32 getMaxqp(Codec *codec) { return codec->nMaxqp; }

U32 getFixedqp(Codec *codec) { return codec->nFixedqp; }

void setCsweo(Codec *codec, BOOL csweo) { codec->bCsweo = csweo; }

void setFps(Codec *codec, U32 fps) { codec->nFps = fps; }

void setBps(Codec *codec, U32 bps) { codec->nBps = bps; }

void setMinqp(Codec *codec, U32 minqp) { codec->nMinqp = minqp; }

void setMaxqp(Codec *codec, U32 maxqp) { codec->nMaxqp = maxqp; }

void setFixedqp(Codec *codec, U32 fixedqp) { codec->nFixedqp = fixedqp; }

S32 stream(Codec *codec) {
  /* Set NALU. */
  // setNALU(codec->stInputPort, NALU_FORMAT_START_CODES);
  if (isVPx(getFormatFourcc(codec->stInputPort))) {
    setNALU(codec->stInputPort, NALU_FORMAT_ONE_NALU_PER_BUFFER);
  }

  if (getFormatFourcc(codec->stInputPort) == V4L2_PIX_FMT_VC1_ANNEX_L) {
    setNALU(codec->stInputPort, NALU_FORMAT_ONE_NALU_PER_BUFFER);
  }
  /*
      if (input.io->getNaluFormat() == NALU_FORMAT_ONE_NALU_PER_BUFFER
              || input.io->getNaluFormat() == NALU_FORMAT_ONE_BYTE_LENGTH_FIELD
              || input.io->getNaluFormat() == NALU_FORMAT_TWO_BYTE_LENGTH_FIELD
              || input.io->getNaluFormat() ==
     NALU_FORMAT_FOUR_BYTE_LENGTH_FIELD){
          input.setNALU((NaluFormat)input.io->getNaluFormat());
      }
  */
  if ((getFormatFourcc(codec->stInputPort) == V4L2_PIX_FMT_VC1_ANNEX_L) ||
      (getFormatFourcc(codec->stInputPort) == V4L2_PIX_FMT_VC1_ANNEX_G)) {
    struct v4l2_control control;
    S32 profile = 0xff;
    /*
            switch (input.io->getProfile())
            {
                case 0:
                {
                    profile = 0;
                    break;
                }
                case 4:
                {
                    profile = 1;
                    break;
                }
                case 12:
                {
                    profile = 2;
                    break;
                }
                default:
                {
                    throw Exception("Unsupported VC1 profile.");
                }
            }
    */
    memset(&control, 0, sizeof(control));

    control.id = V4L2_CID_MVE_VIDEO_VC1_PROFILE;
    control.value = 2;  // profile;

    if (-1 == ioctl(codec->nVideoFd, VIDIOC_S_CTRL, &control)) {
      error("Failed to set profile=%u for fmt: %u .", profile,
            getFormatFourcc(codec->stInputPort));
    }
  }

  /* Add VPx file header. */
  if (isVPx(getFormatFourcc(codec->stOutputPort))) {
    setNALU(codec->stOutputPort, NALU_FORMAT_ONE_NALU_PER_BUFFER);
  }

  queryCapabilities(codec);
  /* enumerateFormats(); */
  enumerateFramesizes(codec, getFormatFourcc(codec->stOutputPort));
  setFormats(codec);
  subscribeEvents(codec);
  allocateCodecBuffers(codec);
  queueCodecBuffers(codec, MPP_FALSE);
  streamonCodec(codec);

  return MPP_OK;
}

BOOL isVPx(U32 format) {
  return format == V4L2_PIX_FMT_VP8 || format == V4L2_PIX_FMT_VP9;
}

BOOL isAFBC(U32 format) {
  switch (format) {
    case V4L2_PIX_FMT_YUV420_AFBC_8:
    case V4L2_PIX_FMT_YUV420_AFBC_10:
    case V4L2_PIX_FMT_YUV422_AFBC_8:
    case V4L2_PIX_FMT_YUV422_AFBC_10:
      return MPP_TRUE;
    default:
      return MPP_FALSE;
  }
}

void enumerateCodecFormats(Codec *codec) {
  enumerateFormats(codec->stInputPort);
  enumerateFormats(codec->stOutputPort);
}

void openDev(Codec *codec) {
  S32 flags = O_RDWR;

  if (!codec->bIsBlockMode) {
    flags |= O_NONBLOCK;
  }

  /* Open the video device in read/write mode. */
  codec->nVideoFd = open(codec->sDevicePath, flags);
  if (codec->nVideoFd < 0) {
    error("Failed to open device.");
  }
}

void closeDev(Codec *codec) {
  close(codec->nVideoFd);
  codec->nVideoFd = -1;
}

void queryCapabilities(Codec *codec) {
  struct v4l2_capability cap;
  S32 ret;

  /* Query capabilities. */
  ret = ioctl(codec->nVideoFd, VIDIOC_QUERYCAP, &cap);
  if (ret != 0) {
    error("Failed to query for capabilities");
  }

  if (0 ==
      (cap.capabilities & (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE))) {
    error("Device is missing m2m support.");
  }
}

void enumerateFramesizes(Codec *codec, U32 format) {
  struct v4l2_frmsizeenum frmsize;

  frmsize.index = 0;
  frmsize.pixel_format = format;

  S32 ret = ioctl(codec->nVideoFd, VIDIOC_ENUM_FRAMESIZES, &frmsize);
  if (ret != 0) {
    error("Failed to enumerate frame sizes. fd=%d format=%d ret=%d error=%s",
          codec->nVideoFd, format, ret, strerror(errno));
  }

  // debug("Enumerate frame size. index=%d pixel_format=%x", frmsize.index,
  //       frmsize.pixel_format);

  switch (frmsize.type) {
    case V4L2_FRMIVAL_TYPE_DISCRETE:
      break;
    case V4L2_FRMIVAL_TYPE_CONTINUOUS:
    case V4L2_FRMIVAL_TYPE_STEPWISE:
      // debug(
      //     "min_width=%d max_width=%d step_width=%d  min_height=%d "
      //     "max_height=%d step_height=%d",
      //     frmsize.stepwise.min_width, frmsize.stepwise.max_width,
      //     frmsize.stepwise.step_width, frmsize.stepwise.min_height,
      //     frmsize.stepwise.max_height, frmsize.stepwise.step_height);
      break;
    default:
      error("Unsupported enumerate frame size type. type=%d", frmsize.type);
  }
}

void setFormats(Codec *codec) {
  getTrySetFormat(codec->stInputPort, codec->nWidth, codec->nHeight,
                  getFormatFourcc(codec->stInputPort), codec->bIsInterlaced);
  getTrySetFormat(codec->stOutputPort, codec->nWidth, codec->nHeight,
                  getFormatFourcc(codec->stOutputPort), codec->bIsInterlaced);
}

struct v4l2_mvx_color_desc getColorDesc(Codec *codec) {
  struct v4l2_mvx_color_desc color;
  S32 ret = ioctl(codec->nVideoFd, VIDIOC_G_MVX_COLORDESC, &color);
  if (ret) {
    error("Failed to get color description!");
  }
  return color;
}

void subscribeEvents(Codec *codec) {
  subscribeEvent(codec, V4L2_EVENT_EOS);
  subscribeEvent(codec, V4L2_EVENT_SOURCE_CHANGE);
  subscribeEvent(codec, V4L2_EVENT_MVX_COLOR_DESC);
}

void subscribeEvent(Codec *codec, U32 event) {
  struct v4l2_event_subscription sub = {.type = event, .id = 0};
  S32 ret;

  ret = ioctl(codec->nVideoFd, VIDIOC_SUBSCRIBE_EVENT, &sub);
  if (ret != 0) {
    error("Failed to subscribe for event.");
  }
}

void unsubscribeEvents(Codec *codec) {
  unsubscribeEvent(codec, V4L2_EVENT_ALL);
}

void unsubscribeEvent(Codec *codec, U32 event) {
  struct v4l2_event_subscription sub;
  S32 ret;

  sub.type = event;
  ret = ioctl(codec->nVideoFd, VIDIOC_UNSUBSCRIBE_EVENT, &sub);
  if (ret != 0) {
    error("Failed to unsubscribe for event.");
  }
}

void allocateCodecBuffers(Codec *codec) {
  allocateBuffers(codec->stInputPort, codec->nInputBufferNum);
  allocateBuffers(codec->stOutputPort, codec->nOutputBufferNum);
}

void freeCodecBuffers(Codec *codec) {
  freeBuffers(codec->stInputPort);
  freeBuffers(codec->stOutputPort);
}

void queueCodecBuffers(Codec *codec, BOOL eof) {
  // only queue output buffer, input buffer is not filled yet
  queueBuffers(codec->stOutputPort, eof);
  // queueBuffers(codec->stInputPort, eof);
}

void streamonCodec(Codec *codec) {
  streamon(codec->stInputPort);
  streamon(codec->stOutputPort);
}

void streamoffCodec(Codec *codec) {
  streamoff(codec->stInputPort);
  streamoff(codec->stOutputPort);
}

S32 handleEvent(Codec *codec) {
  struct v4l2_event event;
  S32 ret;

  ret = ioctl(codec->nVideoFd, VIDIOC_DQEVENT, &event);
  if (ret != 0) {
    error("Failed to dequeue event, please check!");
    return MPP_IOCTL_FAILED;
  }

  if (event.type == V4L2_EVENT_MVX_COLOR_DESC) {
    struct v4l2_mvx_color_desc color = getColorDesc(codec);
    // printColorDesc(color);
    error("V4L2_EVENT_MVX_COLOR_DESC event is not support yet, please check!");
  }

  if (event.type == V4L2_EVENT_SOURCE_CHANGE &&
      (event.u.src_change.changes & V4L2_EVENT_SRC_CH_RESOLUTION)) {
    debug("get V4L2_EVENT_SOURCE_CHANGE event, do notify!");
    notifySourceChange(codec->stOutputPort);
  }

  if (event.type == V4L2_EVENT_EOS) {
    error("V4L2_EVENT_EOS event is not support yet, please check!");
  }

  return MPP_OK;
}

void handleFlush(Codec *codec, BOOL eof) {
  streamoff(codec->stInputPort);
  streamoff(codec->stOutputPort);
  streamon(codec->stInputPort);
  // this sleep is used to fix a bug, ffplay on linux sometimes get a streamon
  // failed(Operation now in progress) error
  usleep(5000);
  streamon(codec->stOutputPort);
  queueBuffers(codec->stOutputPort, MPP_FALSE);
  // port->nFramesProcessed = 0;
}

S32 runPoll(Codec *codec, struct pollfd *p) {
  // debug("input:%d output:%d", codec->stInputPort->pending,
  //       codec->stOutputPort->pending);
  /*if (codec->stInputPort->pending > 0)
  {
      p->events |= POLLOUT;
  }

  if (codec->stOutputPort->pending > 0)
  {
      p->events |= POLLIN;
  }*/

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
