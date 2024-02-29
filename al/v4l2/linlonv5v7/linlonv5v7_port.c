/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-10-07 17:37:14
 * @LastEditTime: 2024-01-20 14:35:13
 * @Description:
 */

#define ENABLE_DEBUG 1

#include "linlonv5v7_port.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "env.h"

struct _Port {
  U32 nFormatFourcc;
  U32 nMemType;
  U32 nNeededBufNum;
  enum v4l2_buf_type
      eBufType;  // V4L2_BUF_TYPE_VIDEO_OUTPUT/V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
  struct v4l2_format stFormat;
  S32 nQueuedNum;
  S32 nBufNum;
  Buffer *stBuf[MAX_BUF_NUM];

  S32 nVideoFd;
  DIRECTION ePortDirection;

  S32 nRotation;
  BOOL bInterlaced;
  BOOL bTryEncStop;
  BOOL bTryDecStop;
  S32 nMirror;
  S32 nScale;
  S32 nFramesProcessed;
  S32 nFramesCount;
  S32 nRcType;

  BOOL bIsSourceChange;

  S32 nQueueNumInput;
  S32 nQueueNumOutput;

  // environment variable
  BOOL bEnableBufferPrint;
  BOOL bEnableOutputBufferSave;
  U8 *nOutputBufferSavePath;
  FILE *pOutputFile;
};

Port *createPort(S32 fd, enum v4l2_buf_type type, U32 format_fourcc,
                 U32 memtype, U32 buffer_num) {
  Port *port_tmp = (Port *)malloc(sizeof(Port));
  if (!port_tmp) {
    error("can not malloc Port, please check! (%s)", strerror(errno));
    return NULL;
  }
  memset(port_tmp, 0, sizeof(Port));

  debug("create a port, type=%d format_fourcc=%d", type, format_fourcc);

  port_tmp->nVideoFd = fd;
  port_tmp->eBufType = type;
  port_tmp->nFormatFourcc = format_fourcc;
  port_tmp->nMemType = memtype;
  port_tmp->nNeededBufNum = buffer_num;
  port_tmp->bInterlaced = MPP_FALSE;
  port_tmp->bTryEncStop = MPP_FALSE;
  port_tmp->bTryDecStop = MPP_FALSE;
  port_tmp->nMirror = 0;
  port_tmp->nScale = 1;
  port_tmp->nFramesProcessed = 0;
  port_tmp->nFramesCount = 0;
  port_tmp->bIsSourceChange = MPP_FALSE;
  port_tmp->nQueueNumInput = 0;
  port_tmp->nQueueNumOutput = 0;

  if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT ||
      type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
    port_tmp->ePortDirection = INPUT;
  if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE ||
      type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
    port_tmp->ePortDirection = OUTPUT;

  // init environment variable
  mpp_env_get_u32("MPP_PRINT_BUFFER", &(port_tmp->bEnableBufferPrint), 0);
  mpp_env_get_u32("MPP_SAVE_OUTPUT_BUFFER",
                  &(port_tmp->bEnableOutputBufferSave), 0);

  if (port_tmp->bEnableOutputBufferSave && OUTPUT == port_tmp->ePortDirection) {
    mpp_env_get_str("MPP_SAVE_OUTPUT_BUFFER_PATH",
                    &(port_tmp->nOutputBufferSavePath),
                    "/home/bianbu/output.yuv");
    debug("save output buffer to (%s)", port_tmp->nOutputBufferSavePath);
    port_tmp->pOutputFile = fopen(port_tmp->nOutputBufferSavePath, "w+");
    if (!port_tmp->pOutputFile) {
      error("can not open port_tmp->pOutputFile, please check! (%s)",
            strerror(errno));
      free(port_tmp);
      return NULL;
    }
  }

  return port_tmp;
}

void destoryPort(Port *port) {
  /*for (S32 i = 0; i < port->nBufNum; i++) {
    debug("--- destory buffer[%d]", i);
    destoryBuffer(port->stBuf[i]);
  }*/
  allocateBuffers(port, 0);
  if (port->bEnableOutputBufferSave && OUTPUT == port->ePortDirection &&
      port->pOutputFile) {
    fflush(port->pOutputFile);
    fclose(port->pOutputFile);
    port->pOutputFile = NULL;
  }
  debug("--- free port");
  free(port);
}

Buffer *getBuffer(Port *port, S32 index) { return port->stBuf[index]; }

enum v4l2_buf_type getV4l2BufType(Port *port) { return port->eBufType; }

U32 getFormatFourcc(Port *port) { return port->nFormatFourcc; }

void enumerateFormats(Port *port) {
  struct v4l2_fmtdesc fmtdesc;
  S32 ret;

  fmtdesc.index = 0;
  fmtdesc.type = port->eBufType;

  while (1) {
    ret = ioctl(port->nVideoFd, VIDIOC_ENUM_FMT, &fmtdesc);
    if (ret) {
      break;
    }
    debug("fmt: index=%d, type=%d, flags=%x, pixelformat=%d, description=%s",
          fmtdesc.index, fmtdesc.type, fmtdesc.flags, fmtdesc.pixelformat,
          fmtdesc.description);

    fmtdesc.index++;
  }
}

struct v4l2_format getPortFormat(Port *port) {
  /* Get and print format. */
  port->stFormat.type = port->eBufType;
  S32 ret = ioctl(port->nVideoFd, VIDIOC_G_FMT, &(port->stFormat));
  if (ret) {
    error("Failed to get format.");
  }

  return port->stFormat;
}

void tryFormat(Port *port, struct v4l2_format format) {
  S32 ret = ioctl(port->nVideoFd, VIDIOC_TRY_FMT, &format);
  if (ret) {
    error("Failed to try format.");
  }
}

void setFormat(Port *port, struct v4l2_format format) {
  S32 ret = ioctl(port->nVideoFd, VIDIOC_S_FMT, &format);
  if (ret) {
    error("Failed to set format.");
  }

  port->stFormat = format;
}

void getTrySetFormat(Port *port, S32 width, S32 height, U32 pixel_format,
                     BOOL interlaced) {
  debug("width=%d height=%d pixel_format=%x", width, height, pixel_format);
  S32 width_tmp = 0, height_tmp = 0;

  struct v4l2_format fmt = getPortFormat(port);
  if (V4L2_TYPE_IS_MULTIPLANAR(port->eBufType)) {
    struct v4l2_pix_format_mplane *f = &(fmt.fmt.pix_mp);

    f->pixelformat = pixel_format;
    f->width = width;
    f->height = height;
    f->num_planes = 2;
    // f->field = interlaced ? V4L2_FIELD_SEQ_TB : V4L2_FIELD_NONE;

    for (S32 i = 0; i < 3; ++i) {
      f->plane_fmt[i].bytesperline = 0;
      f->plane_fmt[i].sizeimage = 0;
    }
  } else {
    struct v4l2_pix_format *f = &(fmt.fmt.pix);

    f->pixelformat = pixel_format;
    f->width = width;
    f->height = height;
    f->bytesperline = 0;
    f->sizeimage = SIZE_IMAGE;
    // f->field = interlaced ? V4L2_FIELD_SEQ_TB : V4L2_FIELD_NONE;
  }

  /* Try format. */
  tryFormat(port, fmt);

  if (V4L2_TYPE_IS_MULTIPLANAR(port->eBufType)) {
    struct v4l2_pix_format_mplane *f = &(fmt.fmt.pix_mp);
    width_tmp = f->width;
    height_tmp = f->height;
  } else {
    struct v4l2_pix_format *f = &(fmt.fmt.pix);
    width_tmp = f->width;
    height_tmp = f->height;
  }
  // for dsl frame case, this is not suitable, remove this.
  if (V4L2_TYPE_IS_OUTPUT(port->eBufType) &&
      (width_tmp != width || height_tmp != height)) {
    error(
        "Selected resolution is not supported for this format width:%d, io "
        "width:%d",
        width_tmp, width);
  }

  setFormat(port, fmt);

  printFormat(fmt);
}

void printFormat(const struct v4l2_format format) {
  if (V4L2_TYPE_IS_MULTIPLANAR(format.type)) {
    const struct v4l2_pix_format_mplane f = format.fmt.pix_mp;

    debug(
        "PRINTFORMAT ===== type: %u, format: %u, width: %u, height: %u, "
        "nplanes: %d, "
        "bytesperline: [%u %u %u], sizeimage: [%u %u %u]",
        format.type, f.pixelformat, f.width, f.height, f.num_planes,
        f.plane_fmt[0].bytesperline, f.plane_fmt[1].bytesperline,
        f.plane_fmt[2].bytesperline, f.plane_fmt[0].sizeimage,
        f.plane_fmt[1].sizeimage, f.plane_fmt[2].sizeimage);
  } else {
    const struct v4l2_pix_format f = format.fmt.pix;

    debug(
        "PRINTFORMAT ===== type: %u, format: %u, width: %u, height: %u, "
        "bytesperline: %u, "
        "sizeimage: %u",
        format.type, f.pixelformat, f.width, f.height, f.bytesperline,
        f.sizeimage);
  }
}

struct v4l2_crop getPortCrop(Port *port) {
  struct v4l2_crop crop = {.type = port->eBufType};

  S32 ret = ioctl(port->nVideoFd, VIDIOC_G_CROP, &crop);
  if (ret) {
    error("Failed to get crop.");
  }

  return crop;
}

void setPortInterlaced(Port *port, BOOL interlaced) {
  port->bInterlaced = interlaced;
}

void tryEncStopCmd(Port *port, BOOL tryStop) { port->bTryEncStop = tryStop; }

void tryDecStopCmd(Port *port, BOOL tryStop) { port->bTryDecStop = tryStop; }

void allocateBuffers(Port *port, S32 count) {
  struct v4l2_requestbuffers reqbuf;
  S32 i;
  S32 ret;

  /* Free existing meta buffer. */
  freeBuffers(port);

  /* Request new buffer to be allocated. */
  reqbuf.count = count;
  reqbuf.type = port->eBufType;
  reqbuf.memory = port->nMemType;
  ret = ioctl(port->nVideoFd, VIDIOC_REQBUFS, &reqbuf);
  if (ret) {
    error("Failed to request buffers.");
  }

  debug("Request buffers. type:%d count:%d(%d) memory:%d", reqbuf.type,
        reqbuf.count, count, reqbuf.memory);

  port->nBufNum = reqbuf.count;

  /* Query each buffer and create a new meta buffer. */
  for (i = 0; i < reqbuf.count; ++i) {
    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];

    buf.type = port->eBufType;
    buf.memory = port->nMemType;
    buf.index = i;
    buf.length = 3;
    buf.m.planes = planes;

    ret = ioctl(port->nVideoFd, VIDIOC_QUERYBUF, &buf);
    if (ret) {
      error("Failed to query buffer.");
    }

    printBuffer(port, buf, "Query");

    port->stBuf[buf.index] = createBuffer(buf, port->nVideoFd, port->stFormat);
  }
}

void freeBuffers(Port *port) {
  for (S32 i = 0; i < port->nBufNum; i++) {
    destoryBuffer(port->stBuf[i]);
  }
}

U32 getBufferCount(Port *port) {
  struct v4l2_control control;

  control.id = V4L2_TYPE_IS_OUTPUT(port->eBufType)
                   ? V4L2_CID_MIN_BUFFERS_FOR_OUTPUT
                   : V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
  if (-1 == ioctl(port->nVideoFd, VIDIOC_G_CTRL, &control)) {
    error("Failed to get minimum buffers.");
  }

  return control.value;
}

void queueBuffers(Port *port, BOOL eof) {
  S32 ret = 0;

  for (S32 i = 0; i < port->nBufNum; i++) {
    if (!eof) {
      /* Remove vendor custom flags. */
      resetVendorFlags(port->stBuf[i]);

      setEndOfStream(port->stBuf[i], eof);
      queueBuffer(port, port->stBuf[i]);
    }
  }
}

void queueBuffer(Port *port, Buffer *buf) {
  struct v4l2_buffer *b = getV4l2Buffer(buf);
  S32 ret;

  setInterlaced(buf, port->bInterlaced);
  setRotation(buf, port->nRotation);
  setMirror(buf, port->nMirror);
  setDownScale(buf, port->nScale);

  if (getRoiCfgflag(buf) && getBytesUsed(b) != 0) {
    struct v4l2_mvx_roi_regions roi = getRoiCfg(buf);
    ret = ioctl(port->nVideoFd, VIDIOC_S_MVX_ROI_REGIONS, &roi);
    if (ret) {
      error("Failed to queue roi param.");
    }
  }

  if (getQPofEPR(buf) > 0) {
    S32 qp = getQPofEPR(buf);
    ret = ioctl(port->nVideoFd, VIDIOC_S_MVX_QP_EPR, &qp);
    if (ret) {
      error("Failed to queue roi param.");
    }
    setQPofEPR(buf, 0);
  }
  /* Mask buffer offset. */
  if (!V4L2_TYPE_IS_MULTIPLANAR(b->type)) {
    switch (b->memory) {
      case V4L2_MEMORY_MMAP:
        b->m.offset &= ~((1 << 12) - 1);
        break;
      default:
        break;
    }
  }
  // encoder specfied frames count to be processed
  if (port->ePortDirection == INPUT && port->nFramesCount > 0 &&
      port->nFramesProcessed >= port->nFramesCount - 1 &&
      !isGeneralBuffer(buf)) {
    if (port->nFramesProcessed >= port->nFramesCount) {
      clearBytesUsed(buf);
      resetVendorFlags(buf);
    }
    setEndOfStream(buf, MPP_TRUE);
  }

  struct timeval time;
  gettimeofday(&time, NULL);
  // debug("queue buffer : %ld", time.tv_sec * 1000 + time.tv_usec / 1000);
  printBuffer(port, *b, "---->");

  ret = ioctl(port->nVideoFd, VIDIOC_QBUF, b);
  if (ret) {
    error("Failed to queue buffer. (%s)", strerror(errno));
  }
  if (port->ePortDirection == INPUT && V4L2_TYPE_IS_MULTIPLANAR(b->type) &&
      !isGeneralBuffer(buf)) {
    port->nFramesProcessed++;
  }

  if (!ret) {
    if (port->ePortDirection == INPUT) {
      port->nQueueNumInput++;
      // error("input queue:%d", port->nQueueNumInput);
    }
    if (port->ePortDirection == OUTPUT) {
      port->nQueueNumOutput++;
      // error("output queue:%d", port->nQueueNumOutput);
    }
  }
}

Buffer *dequeueBuffer(Port *port) {
  struct v4l2_plane planes[VIDEO_MAX_PLANES];
  struct v4l2_buffer buf;
  buf.m.planes = planes;
  S32 ret;

  buf.type = port->eBufType;
  buf.memory = port->nMemType;
  buf.length = 3;

  ret = ioctl(port->nVideoFd, VIDIOC_DQBUF, &buf);
  if (ret) {
    error("Failed to dequeue buffer. type=%u, memory=%u", buf.type, buf.memory);
    return NULL;
  }

  if (!ret) {
    if (port->ePortDirection == INPUT) {
      port->nQueueNumInput--;
      // error("input dequeue:%d", port->nQueueNumInput);
    }
    if (port->ePortDirection == OUTPUT) {
      port->nQueueNumOutput--;
      // error("output dequeue:%d", port->nQueueNumOutput);
    }
  }

  struct timeval time;
  gettimeofday(&time, NULL);
  // debug("dequeue buffer %ld", time.tv_sec * 1000 + time.tv_usec / 1000);
  printBuffer(port, buf, "<----");

  Buffer *buffer = port->stBuf[buf.index];
  update(buffer, buf);
  setCrop(buffer, getPortCrop(port));

  return buffer;
}

void printBuffer(Port *port, struct v4l2_buffer buf, const U8 *prefix) {
  if (port->bEnableBufferPrint) {
    debug_pre(
        "%s type:%u, index:%u, sequence:%d, timestamp:[%ld, "
        "%ld], flags:%x ",
        prefix, buf.type, buf.index, buf.sequence, buf.timestamp.tv_sec,
        buf.timestamp.tv_usec, buf.flags);

    if (V4L2_TYPE_IS_MULTIPLANAR(buf.type)) {
      debug_mid("num_planes:%d ", buf.length);
      if (2 == buf.length) {
        debug_after(
            "bytesused:[%u %u], length:[%u %u], offset:[%u "
            "%u]",
            buf.m.planes[0].bytesused, buf.m.planes[1].bytesused,
            buf.m.planes[0].length, buf.m.planes[1].length,
            buf.m.planes[0].data_offset, buf.m.planes[1].data_offset);
      } else if (3 == buf.length) {
        debug_after(
            "bytesused:[%u %u %u], length:[%u %u %u], offset:[%u "
            "%u %u]",
            buf.m.planes[0].bytesused, buf.m.planes[1].bytesused,
            buf.m.planes[2].bytesused, buf.m.planes[0].length,
            buf.m.planes[1].length, buf.m.planes[2].length,
            buf.m.planes[0].data_offset, buf.m.planes[1].data_offset,
            buf.m.planes[2].data_offset);
      }
    } else {
      debug_after("bytesused:%u, length:%u", buf.bytesused, buf.length);
    }
  }
}

void streamon(Port *port) {
  struct timeval time;
  gettimeofday(&time, NULL);
  debug("Stream on %ld", time.tv_sec * 1000 + time.tv_usec / 1000);

  S32 ret = ioctl(port->nVideoFd, VIDIOC_STREAMON, &(port->eBufType));
  if (ret) {
    error("Failed to stream on.  nVideoFd = %d, (%s)", port->nVideoFd,
          strerror(errno));
  }
}

void streamoff(Port *port) {
  struct timeval time;
  gettimeofday(&time, NULL);

  debug("Stream off %ld", time.tv_sec * 1000 + time.tv_usec / 1000);

  S32 ret = ioctl(port->nVideoFd, VIDIOC_STREAMOFF, &(port->eBufType));
  if (ret) {
    error("Failed to stream off.");
  }
}

void sendEncStopCommand(Port *port) {
  struct v4l2_encoder_cmd cmd = {.cmd = V4L2_ENC_CMD_STOP};

  if (port->bTryEncStop) {
    if (0 != ioctl(port->nVideoFd, VIDIOC_TRY_ENCODER_CMD, &cmd)) {
      error("Failed to send try encoder stop command.");
    }
  }

  if (0 != ioctl(port->nVideoFd, VIDIOC_ENCODER_CMD, &cmd)) {
    error("Failed to send encoding stop command.");
  }
}

void sendDecStopCommand(Port *port) {
  struct v4l2_decoder_cmd cmd = {.cmd = V4L2_DEC_CMD_STOP};

  if (port->bTryDecStop) {
    if (0 != ioctl(port->nVideoFd, VIDIOC_TRY_DECODER_CMD, &cmd)) {
      error("Failed to send try decoder stop command.");
    }
  }

  if (0 != ioctl(port->nVideoFd, VIDIOC_DECODER_CMD, &cmd)) {
    error("Failed to send decoding stop command.");
  }
}

void setH264DecIntBufSize(Port *port, U32 ibs) {
  debug("setH264DecIntBufSize(%u)", ibs);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_INTBUF_SIZE;
  control.value = ibs;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 ibs=%u.", ibs);
  }
}

void setDecFrameReOrdering(Port *port, U32 fro) {
  debug("setDecFrameReOrdering(%u)", fro);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_FRAME_REORDERING;
  control.value = fro;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set decoding fro=%u.", fro);
  }
}

void setDecIgnoreStreamHeaders(Port *port, U32 ish) {
  debug("setDecIgnoreStreamHeaders(%u)", ish);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_IGNORE_STREAM_HEADERS;
  control.value = ish;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set decoding ish=%u.", ish);
  }
}

void setNALU(Port *port, enum v4l2_nalu_format nalu) {
  mpp_v4l2_set_ctrl(port->nVideoFd, V4L2_CID_MVE_VIDEO_NALU_FORMAT, nalu);
}

void setEncFramerate(Port *port, U32 frame_rate) {
  debug("setEncFramerate(%u)", frame_rate);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_FRAME_RATE;
  control.value = frame_rate;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set frame_rate=%u.", frame_rate);
  }
}

void setEncBitrate(Port *port, U32 bit_rate) {
  debug("setEncBitrate(%u)", bit_rate);
  debug("setRctype(%u)", port->nRcType);

  if (0 == bit_rate && 0 == port->nRcType) {
    return;
  }
  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_BITRATE;
  control.value = bit_rate;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set bit_rate=%u.", bit_rate);
  }
}

void setRateControl(Port *port, struct v4l2_rate_control *rc) {
  debug(
      "setRateControl(rc->rc_type:%u rc->target_bitrate:%u "
      "rc->maximum_bitrate:%u)",
      rc->rc_type, rc->target_bitrate, rc->maximum_bitrate);

  S32 ret = ioctl(port->nVideoFd, VIDIOC_S_MVX_RATE_CONTROL, rc);
  if (ret) {
    error("Failed to set rate control.");
  }

  return;
}

void setEncPFrames(Port *port, U32 pframes) {
  debug("setEncPFrames(%u)", pframes);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_P_FRAMES;
  control.value = pframes;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set pframes=%u.", pframes);
  }
}

void setEncBFrames(Port *port, U32 bframes) {
  debug("setEncBFrames(%u)", bframes);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_B_FRAMES;
  control.value = bframes;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set bframes=%u.", bframes);
  }
}

void setEncSliceSpacing(Port *port, U32 spacing) {
  debug("setEncSliceSpacing(%u)", spacing);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB;
  control.value = spacing;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set slice spacing=%u.", spacing);
  }

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE;
  control.value = spacing != 0;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set slice mode.");
  }
}

void setH264EncForceChroma(Port *port, U32 fmt) {
  debug("setH264EncForceChroma(%u)", fmt);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_FORCE_CHROMA_FORMAT;
  control.value = fmt;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 chroma fmt=%u.", fmt);
  }
}

void setH264EncBitdepth(Port *port, U32 bd) {
  debug("setH264EncBitdepth(%u)", bd);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_BITDEPTH_LUMA;
  control.value = bd;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 luma bd=%u.", bd);
  }

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_BITDEPTH_CHROMA;
  control.value = bd;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 chroma bd=%u.", bd);
  }
}

void setH264EncIntraMBRefresh(Port *port, U32 period) {
  debug("setH264EncIntraMBRefresh(%u)", period);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB;
  control.value = period;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 period=%u.", period);
  }
}

void setEncProfile(Port *port, U32 profile) {
  debug("setEncProfile(%u)", profile);

  BOOL setProfile = MPP_FALSE;
  struct v4l2_control control;

  memset(&control, 0, sizeof(control));

  if (port->nFormatFourcc == V4L2_PIX_FMT_H264) {
    setProfile = MPP_TRUE;
    control.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
    control.value = profile;
  } else if (port->nFormatFourcc == V4L2_PIX_FMT_HEVC) {
    setProfile = MPP_TRUE;
    control.id = V4L2_CID_MVE_VIDEO_H265_PROFILE;
    control.value = profile;
  }

  if (setProfile) {
    if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
      error("Failed to set profile=%u for fmt: %u .", profile,
            port->nFormatFourcc);
    }
  } else {
    debug("Profile cannot be set for this codec");
  }
}

void setEncLevel(Port *port, U32 level) {
  debug("setEncLevel(%u)", level);

  BOOL setLevel = MPP_FALSE;
  struct v4l2_control control;

  memset(&control, 0, sizeof(control));

  if (port->nFormatFourcc == V4L2_PIX_FMT_H264) {
    setLevel = MPP_TRUE;
    control.id = V4L2_CID_MPEG_VIDEO_H264_LEVEL;
    control.value = level;
  } else if (port->nFormatFourcc == V4L2_PIX_FMT_HEVC) {
    setLevel = MPP_TRUE;
    control.id = V4L2_CID_MVE_VIDEO_H265_LEVEL;
    control.value = level;
  }

  if (setLevel) {
    if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
      error("Failed to set level=%u for fmt: %u .", level, port->nFormatFourcc);
    }
  } else {
    debug("Level cannot be set for this codec");
  }
}

void setEncConstrainedIntraPred(Port *port, U32 cip) {
  debug("setEncConstrainedIntraPred(%u)", cip);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_CONSTR_IPRED;
  control.value = cip;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set encoding cip=%u.", cip);
  }
}

void setH264EncEntropyMode(Port *port, U32 ecm) {
  debug("setH264EncEntropyMode(%u)", ecm);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE;
  control.value = ecm;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 ecm=%u.", ecm);
  }
}

void setH264EncGOPType(Port *port, U32 gop) {
  debug("setH264EncGOPType(%u)", gop);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_GOP_TYPE;
  control.value = gop;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 gop=%u.", gop);
  }
}

void setH264EncMinQP(Port *port, U32 minqp) {
  debug("setH264EncMinQP(%u)", minqp);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_H264_MIN_QP;
  control.value = minqp;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 minqp=%u.", minqp);
  }
}

void setH264EncMaxQP(Port *port, U32 maxqp) {
  debug("setH264EncMaxQP(%u)", maxqp);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE;
  control.value = 1;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to enable/disable rate control.");
  }

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_H264_MAX_QP;
  control.value = maxqp;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 maxqp=%u.", maxqp);
  }
}

void setH264EncFixedQP(Port *port, U32 fqp) {
  debug("setH264EncFixedQP(%u)", fqp);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP;
  control.value = fqp;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 I frame fqp=%u.", fqp);
  }

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP;
  control.value = fqp;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 P frame fqp=%u.", fqp);
  }

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP;
  control.value = fqp;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 B frame fqp=%u.", fqp);
  }

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE;
  control.value = 0;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to enable/disable rate control.");
  }
}

void setH264EncFixedQPI(Port *port, U32 fqp) {
  debug("setH264EncFixedQPI(%u)", fqp);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP;
  control.value = fqp;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 I frame fqp=%u.", fqp);
  }
}

void setH264EncFixedQPP(Port *port, U32 fqp) {
  debug("setH264EncFixedQPP(%u)", fqp);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP;
  control.value = fqp;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 P frame fqp=%u.", fqp);
  }
}

void setH264EncFixedQPB(Port *port, U32 fqp) {
  debug("setH264EncFixedQPB(%u)", fqp);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP;
  control.value = fqp;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 B frame fqp=%u.", fqp);
  }
}

void setH264EncBandwidth(Port *port, U32 bw) {
  debug("setH264EncBandwidth(%u)", bw);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_BANDWIDTH_LIMIT;
  control.value = bw;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set H264 bw=%u.", bw);
  }
}

void setHEVCEncEntropySync(Port *port, U32 es) {
  debug("setHEVCEncEntropySync(%u)", es);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_ENTROPY_SYNC;
  control.value = es;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set HEVC es=%u.", es);
  }
}

void setHEVCEncTemporalMVP(Port *port, U32 tmvp) {
  debug("setHEVCEncTemporalMVP(%u)", tmvp);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_TEMPORAL_MVP;
  control.value = tmvp;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set HEVC tmvp=%u.", tmvp);
  }
}

void setEncStreamEscaping(Port *port, U32 sesc) {
  debug("setEncStreamEscaping(%u)", sesc);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_STREAM_ESCAPING;
  control.value = sesc;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set encoding sesc=%u.", sesc);
  }
}

void setEncHorizontalMVSearchRange(Port *port, U32 hmvsr) {
  debug("setEncHorizontalMVSearchRange(%u)", hmvsr);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_MV_H_SEARCH_RANGE;
  control.value = hmvsr;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set encoding hmvsr=%u.", hmvsr);
  }
}

void setEncVerticalMVSearchRange(Port *port, U32 vmvsr) {
  debug("setEncVerticalMVSearchRange(%u)", vmvsr);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MPEG_VIDEO_MV_V_SEARCH_RANGE;
  control.value = vmvsr;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set encoding vmvsr=%u.", vmvsr);
  }
}

void setVP9EncTileCR(Port *port, U32 tcr) {
  debug("setVP9EncTileCR(%u)", tcr);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_TILE_COLS;
  control.value = tcr;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set VP9 tile cols=%u.", tcr);
  }

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_TILE_ROWS;
  control.value = tcr;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set VP9 tile rows=%u.", tcr);
  }
}

void setJPEGEncRefreshInterval(Port *port, U32 r) {
  debug("setJPEGEncRefreshInterval(%u)", r);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_JPEG_RESTART_INTERVAL;
  control.value = r;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set JPEG refresh interval=%u.", r);
  }
}

void setJPEGEncQuality(Port *port, U32 q) {
  debug("setJPEGEncQuality(%u)", q);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_JPEG_COMPRESSION_QUALITY;
  control.value = q;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set JPEG compression quality=%u.", q);
  }
}

void setPortRotation(Port *port, S32 rotation) { port->nRotation = rotation; }

void setPortMirror(Port *port, S32 mirror) { port->nMirror = mirror; }

void setPortDownScale(Port *port, S32 scale) { port->nScale = scale; }

void setDSLFrame(Port *port, S32 width, S32 height) {
  debug("setDSLFrame(%d x %d)", width, height);

  struct v4l2_mvx_dsl_frame dsl_frame;
  memset(&dsl_frame, 0, sizeof(dsl_frame));
  dsl_frame.width = width;
  dsl_frame.height = height;
  S32 ret = ioctl(port->nVideoFd, VIDIOC_S_MVX_DSL_FRAME, &dsl_frame);
  if (ret != 0) {
    error("Failed to set DSL frame width/height.");
  }

  return;
}

void setDSLRatio(Port *port, S32 hor, S32 ver) {
  debug("setDSLRatio(%d x %d)", hor, ver);

  struct v4l2_mvx_dsl_ratio dsl_ratio;
  memset(&dsl_ratio, 0, sizeof(dsl_ratio));
  dsl_ratio.hor = hor;
  dsl_ratio.ver = ver;
  S32 ret = ioctl(port->nVideoFd, VIDIOC_S_MVX_DSL_RATIO, &dsl_ratio);
  if (ret != 0) {
    error("Failed to set DSL frame hor/ver.");
  }

  return;
}

void setDSLMode(Port *port, S32 mode) {
  debug("setDSLMode(%d)", mode);
  S32 dsl_pos_mode = mode;
  S32 ret = ioctl(port->nVideoFd, VIDIOC_S_MVX_DSL_MODE, &dsl_pos_mode);
  if (ret != 0) {
    error("Failed to set dsl mode.");
  }
}

void setLongTermRef(Port *port, U32 mode, U32 period) {
  debug("setLongTermRef(mode:%u period:%u)", mode, period);
  struct v4l2_mvx_long_term_ref ltr;
  memset(&ltr, 0, sizeof(ltr));
  ltr.mode = mode;
  ltr.period = period;
  S32 ret = ioctl(port->nVideoFd, VIDIOC_S_MVX_LONG_TERM_REF, &ltr);
  if (ret != 0) {
    error("Failed to set long term mode/period.");
  }
}

void setFrameCount(Port *port, S32 frames) { port->nFramesCount = frames; }

void setCropLeft(Port *port, S32 left) {
  debug("setCropLeft(%d)", left);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_CROP_LEFT;
  control.value = left;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set crop left=%u.", left);
  }
}

void setCropRight(Port *port, S32 right) {
  debug("setCropRight(%d)", right);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_CROP_RIGHT;
  control.value = right;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set crop right=%u.", right);
  }
}

void setCropTop(Port *port, S32 top) {
  debug("setCropTop(%d)", top);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_CROP_TOP;
  control.value = top;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set crop top=%u.", top);
  }
}

void setCropBottom(Port *port, S32 bottom) {
  debug("setCropBottom(%d)", bottom);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_CROP_BOTTOM;
  control.value = bottom;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set crop bottom=%u.", bottom);
  }
}

void setVuiColourDesc(Port *port, struct v4l2_mvx_color_desc *color) {
  S32 ret = ioctl(port->nVideoFd, VIDIOC_S_MVX_COLORDESC, color);
  if (ret != 0) {
    error("Failed to set color description.");
  }

  return;
}

void setSeiUserData(Port *port, struct v4l2_sei_user_data *sei_user_data) {
  S32 ret = ioctl(port->nVideoFd, VIDIOC_S_MVX_SEI_USERDATA, sei_user_data);
  if (ret != 0) {
    error("Failed to set sei user data.");
  }

  return;
}

void setHRDBufferSize(Port *port, S32 size) {
  debug("setHRDBufferSize(%d)", size);

  struct v4l2_control control;

  memset(&control, 0, sizeof(control));
  control.id = V4L2_CID_MVE_VIDEO_HRD_BUFFER_SIZE;
  control.value = size;

  if (-1 == ioctl(port->nVideoFd, VIDIOC_S_CTRL, &control)) {
    error("Failed to set crop bottom=%u.", size);
  }
}

S32 getBufWidth(Port *port) { return port->stFormat.fmt.pix_mp.width; }

S32 getBufHeight(Port *port) { return port->stFormat.fmt.pix_mp.height; }

S32 handleInputBuffer(Port *port, BOOL eof, MppData *data) {
  Buffer *buffer = dequeueBuffer(port);
  struct v4l2_buffer *b = getV4l2Buffer(buffer);
  if (eof) {
    debug("****************************************** eos2");
    if (port->bTryDecStop) {
      debug("dec ****************************************** eos3");
      sendDecStopCommand(port);
    }
  }

  /* Remove vendor custom flags. */
  resetVendorFlags(buffer);

  if (V4L2_BUF_TYPE_VIDEO_OUTPUT == port->eBufType &&
      V4L2_MEMORY_MMAP == port->nMemType) {
    // decode input
    MppPacket *packet = PACKET_GetPacket(data);
    memcpy(getUserPtr(buffer, 0), PACKET_GetDataPointer(packet),
           PACKET_GetLength(packet));
    b->bytesused = PACKET_GetLength(packet);
    setTimeStamp(port->stBuf[b->index], PACKET_GetPts(packet));
    // debug("xxx %x %x %x timestamp: %lld",
    //       *(S32 *)port->stBuf[b->index]->pUserPtr[0],
    //       *(S32 *)(port->stBuf[b->index]->pUserPtr[0] + 4),
    //       *(S32 *)(port->stBuf[b->index]->pUserPtr[0] + 8),
    //       PACKET_GetPts(packet));
  } else if (V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE == port->eBufType &&
             V4L2_MEMORY_DMABUF == port->nMemType) {
    // encode input
    MppFrame *frame = FRAME_GetFrame(data);
    S32 width = getBufWidth(port);
    S32 height = getBufHeight(port);
    S32 y_size = width * height;

    // if(!eof)
    memcpy(getUserPtr(buffer, 0), FRAME_GetDataPointer(frame, 0),
           y_size * 3 / 2);

    b->m.planes[0].bytesused = y_size;
    b->m.planes[1].bytesused = y_size * 3 / 2;
    b->m.planes[0].data_offset = 0;
    b->m.planes[1].data_offset = y_size;
    b->m.planes[0].length = y_size;
    b->m.planes[1].length = y_size * 3 / 2;
  }

  setEndOfStream(buffer, eof);
  queueBuffer(port, buffer);

  if (eof) {
    debug("enc ****************************************** eos");
    sendEncStopCommand(port);
  }

  return MPP_OK;
}

S32 handleOutputBuffer(Port *port, BOOL eof, MppData *data) {
  // debug("handle output frame!!!");
  Buffer *buffer = dequeueBuffer(port);
  if (!buffer) {
    error("buf is NULL, please check!");
    return MPP_OK;
  }

  struct v4l2_buffer *b = getV4l2Buffer(buffer);
  MppFrame *frame = FRAME_GetFrame(data);

  if (V4L2_BUF_TYPE_VIDEO_CAPTURE == port->eBufType) {
    if (V4L2_MEMORY_MMAP == port->nMemType) {
      // to do
    } else if (V4L2_MEMORY_DMABUF == port->nMemType) {
      // to do
    }
  } else if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == port->eBufType) {
    if (V4L2_MEMORY_DMABUF == port->nMemType) {
      FRAME_SetDataUsedNum(frame, b->length);
      for (S32 i = 0; i < b->length; i++) {
        FRAME_SetFD(frame, b->m.planes[i].m.fd, i);
        FRAME_SetDataPointer(frame, i, getUserPtr(buffer, i));
      }
      FRAME_SetID(frame, b->index);

      // debug("request output idx = %d", b->index);
    } else if (V4L2_MEMORY_MMAP == port->nMemType) {
      FRAME_SetDataUsedNum(frame, b->length);
      for (S32 i = 0; i < b->length; i++) {
        FRAME_SetDataPointer(frame, i, getUserPtr(buffer, i));
      }

      FRAME_SetID(frame, b->index);
    }

    FRAME_SetPts(frame,
                 (S64)(b->timestamp.tv_sec * 1000000 + b->timestamp.tv_usec));
  }

  /* EOS on capture port-> */
  if (!V4L2_TYPE_IS_OUTPUT(b->type) && b->flags & V4L2_BUF_FLAG_LAST) {
    debug("Capture EOS.");
    return MPP_CODER_EOS;
  }

  /* Resolution change. we should only handle this on decode
   * output:V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE*/
  if (!V4L2_TYPE_IS_OUTPUT(b->type) && V4L2_TYPE_IS_MULTIPLANAR(b->type) &&
      /*(getBytesUsed(b) == 0 ||
       (b->flags & V4L2_BUF_FLAG_MVX_BUFFER_FRAME_PRESENT) !=
           V4L2_BUF_FLAG_MVX_BUFFER_FRAME_PRESENT) &&*/
      0 == (b->flags & V4L2_BUF_FLAG_ERROR)) {
    struct v4l2_format fmt = port->stFormat;
    getPortFormat(port);
    BOOL isResChange = MPP_TRUE;
    if (V4L2_TYPE_IS_MULTIPLANAR(port->eBufType)) {
      struct v4l2_pix_format_mplane f = fmt.fmt.pix_mp;
      isResChange = ((f.width != port->stFormat.fmt.pix_mp.width) ||
                     (f.height != port->stFormat.fmt.pix_mp.height)) &&
                    (f.width * f.height) < (port->stFormat.fmt.pix_mp.height *
                                            port->stFormat.fmt.pix_mp.width);
    } else {
      struct v4l2_pix_format f = fmt.fmt.pix;
      isResChange = ((f.width != port->stFormat.fmt.pix.width) ||
                     (f.height != port->stFormat.fmt.pix.height)) &&
                    (f.width * f.height) < (port->stFormat.fmt.pix.height *
                                            port->stFormat.fmt.pix.width);
    }
    /*
        if ((b.flags & V4L2_BUF_FLAG_MVX_BUFFER_NEED_REALLOC) ==
            V4L2_BUF_FLAG_MVX_BUFFER_NEED_REALLOC) {
          debug("..Resolution changed:%d  new size: %d x %d", isResChange,
       port->stFormat.fmt.pix_mp.width, port->stFormat.fmt.pix_mp.height);
          handleResolutionChange(port, eof);
          return MPP_RESOLUTION_CHANGED;
          //return MPP_FALSE;
        }
    */
    if (port->bIsSourceChange) {
      debug("Resolution changed:%d new size: %d x %d", isResChange,
            port->stFormat.fmt.pix_mp.width, port->stFormat.fmt.pix_mp.height);
      handleResolutionChange(port, eof);
      port->bIsSourceChange = MPP_FALSE;
      return MPP_RESOLUTION_CHANGED;
    }
  }

  if (!getBytesUsed(b)) {
    return MPP_CODER_NO_DATA;
  }

  if (b->flags & V4L2_BUF_FLAG_ERROR) {
    error("this is a error frame, app decide what to do!");
    return MPP_ERROR_FRAME;
  }

  if (port->bEnableOutputBufferSave && OUTPUT == port->ePortDirection &&
      port->pOutputFile) {
    if (V4L2_BUF_TYPE_VIDEO_CAPTURE == port->eBufType) {
      // to do
    } else if (V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE == port->eBufType) {
      for (S32 i = 0; i < b->length; i++) {
        fwrite(FRAME_GetDataPointer(frame, i), b->m.planes[i].length, 1,
               port->pOutputFile);
      }
    }
  }

  /* Remove vendor custom flags. */
  // decoder specfied frames count to be processed
  if (port->ePortDirection == OUTPUT && V4L2_TYPE_IS_MULTIPLANAR(b->type) &&
      (b->flags & V4L2_BUF_FLAG_MVX_BUFFER_FRAME_PRESENT) ==
          V4L2_BUF_FLAG_MVX_BUFFER_FRAME_PRESENT) {
    port->nFramesProcessed++;
  }
  resetVendorFlags(buffer);
  if (port->ePortDirection == OUTPUT && port->nFramesCount > 0 &&
      port->nFramesProcessed >= port->nFramesCount) {
    clearBytesUsed(buffer);
    setEndOfStream(buffer, MPP_TRUE);
    queueBuffer(port, buffer);
    return MPP_OK;
  } else {
    setEndOfStream(buffer, eof);
  }

  // queueBuffer(port, buffer);

  if (eof) {
    sendEncStopCommand(port);
  }

  return MPP_OK;
}

BOOL handleBuffer(Port *port, BOOL eof, MppData *data) {
  Buffer *buffer = dequeueBuffer(port);
  struct v4l2_buffer *b = getV4l2Buffer(buffer);
  if (eof) {
    if (port->bTryDecStop) {
      sendDecStopCommand(port);
    }
    return MPP_TRUE;
  }

  /* EOS on capture port-> */
  if (!V4L2_TYPE_IS_OUTPUT(b->type) && b->flags & V4L2_BUF_FLAG_LAST) {
    debug("Capture EOS.");
    return MPP_TRUE;
  }

  /* Resolution change. we should only handle this on decode
   * output:V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE*/
  if (!V4L2_TYPE_IS_OUTPUT(b->type) && V4L2_TYPE_IS_MULTIPLANAR(b->type) &&
      (0 == getBytesUsed(b) ||
       (b->flags & V4L2_BUF_FLAG_MVX_BUFFER_FRAME_PRESENT) !=
           V4L2_BUF_FLAG_MVX_BUFFER_FRAME_PRESENT) &&
      0 == (b->flags & V4L2_BUF_FLAG_ERROR)) {
    struct v4l2_format fmt = port->stFormat;
    getPortFormat(port);
    BOOL isResChange = MPP_TRUE;
    if (V4L2_TYPE_IS_MULTIPLANAR(port->eBufType)) {
      struct v4l2_pix_format_mplane f = fmt.fmt.pix_mp;
      isResChange = ((f.width != port->stFormat.fmt.pix_mp.width) ||
                     (f.height != port->stFormat.fmt.pix_mp.height)) &&
                    (f.width * f.height) < (port->stFormat.fmt.pix_mp.height *
                                            port->stFormat.fmt.pix_mp.width);
    } else {
      struct v4l2_pix_format f = fmt.fmt.pix;
      isResChange = ((f.width != port->stFormat.fmt.pix.width) ||
                     (f.height != port->stFormat.fmt.pix.height)) &&
                    (f.width * f.height) < (port->stFormat.fmt.pix.height *
                                            port->stFormat.fmt.pix.width);
    }
    if ((b->flags & V4L2_BUF_FLAG_MVX_BUFFER_NEED_REALLOC) ==
        V4L2_BUF_FLAG_MVX_BUFFER_NEED_REALLOC) {
      debug("Resolution changed:%d", isResChange);
      handleResolutionChange(port, eof);
      return MPP_FALSE;
    }
  }

  /* Remove vendor custom flags. */
  // decoder specfied frames count to be processed
  if (port->ePortDirection == OUTPUT && V4L2_TYPE_IS_MULTIPLANAR(b->type) &&
      (b->flags & V4L2_BUF_FLAG_MVX_BUFFER_FRAME_PRESENT) ==
          V4L2_BUF_FLAG_MVX_BUFFER_FRAME_PRESENT) {
    port->nFramesProcessed++;
  }
  resetVendorFlags(buffer);
  if (port->ePortDirection == OUTPUT && port->nFramesCount > 0 &&
      port->nFramesProcessed >= port->nFramesCount) {
    clearBytesUsed(buffer);
    setEndOfStream(buffer, MPP_TRUE);
    queueBuffer(port, buffer);
    return MPP_TRUE;
  } else {
    setEndOfStream(buffer, eof);
  }

  queueBuffer(port, buffer);

  if (eof) {
    sendEncStopCommand(port);
  }

  return MPP_TRUE;
}

void handleResolutionChange(Port *port, BOOL eof) {
  streamoff(port);
  getPortFormat(port);
  allocateBuffers(port, 0);
  getTrySetFormat(port, port->stFormat.fmt.pix_mp.width,
                  port->stFormat.fmt.pix_mp.height,
                  port->stFormat.fmt.pix_mp.pixelformat, MPP_FALSE);
  U32 count = getBufferCount(port);
  if (count < port->nNeededBufNum) count = port->nNeededBufNum;
  count += 4;
  allocateBuffers(port, count);
  port->nBufNum = count;
  streamon(port);
  queueBuffers(port, eof);
  port->nFramesProcessed = 0;
}

S32 getBufNum(Port *port) { return port->nBufNum; }

S32 getBufFd(Port *port, U32 index) {
  struct v4l2_buffer *b = getV4l2Buffer(port->stBuf[index]);
  return b->m.planes[0].m.fd;
}

void notifySourceChange(Port *port) { port->bIsSourceChange = MPP_TRUE; }
