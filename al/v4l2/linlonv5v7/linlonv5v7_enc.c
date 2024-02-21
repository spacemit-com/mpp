/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-02-01 10:43:49
 * @LastEditTime: 2024-01-09 16:21:31
 * @Description: video encode plugin for V4L2 codec standard interface
 */

#define ENABLE_DEBUG 1

#include <errno.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "al_interface_enc.h"
#include "linlonv5v7_codec.h"
#include "log.h"
#include "mvx-v4l2-controls.h"
#include "v4l2_utils.h"

#define MODULE_TAG "v4l2enc"

#define BS_BUF_SIZE (1 << 20)

CODING_TYPE_MAPPING_DEFINE(Linlonv5v7Enc, S32)
static const ALLinlonv5v7EncCodingTypeMapping
    stALLinlonv5v7EncCodingTypeMapping[] = {
        {CODING_H263, V4L2_PIX_FMT_H263},
        {CODING_H264, V4L2_PIX_FMT_H264},
        {CODING_H264_MVC, V4L2_PIX_FMT_H264_MVC},
        {CODING_H264_NO_SC, V4L2_PIX_FMT_H264_NO_SC},
        {CODING_H265, V4L2_PIX_FMT_HEVC},
        {CODING_MJPEG, V4L2_PIX_FMT_JPEG},
        {CODING_JPEG, V4L2_PIX_FMT_JPEG},
        {CODING_VP8, V4L2_PIX_FMT_VP8},
        {CODING_VP9, V4L2_PIX_FMT_VP9},
        {CODING_AVS, V4L2_PIX_FMT_AVS},
        {CODING_AVS2, V4L2_PIX_FMT_AVS2},
        {CODING_MPEG1, V4L2_PIX_FMT_MPEG},
        {CODING_MPEG2, V4L2_PIX_FMT_MPEG2},
        {CODING_MPEG4, V4L2_PIX_FMT_MPEG4},
        {CODING_RV, V4L2_PIX_FMT_RV},
        {CODING_VC1, V4L2_PIX_FMT_VC1_ANNEX_G},
        {CODING_VC1_ANNEX_L, V4L2_PIX_FMT_VC1_ANNEX_L},
        {CODING_FWHT, V4L2_PIX_FMT_FWHT},
};
CODING_TYPE_MAPPING_CONVERT(Linlonv5v7Enc, linlonv5v7enc, S32)

PIXEL_FORMAT_MAPPING_DEFINE(Linlonv5v7Enc, S32)
static const ALLinlonv5v7EncPixelFormatMapping
    stALLinlonv5v7EncPixelFormatMapping[] = {
        {PIXEL_FORMAT_I420, V4L2_PIX_FMT_YUV420M},
        {PIXEL_FORMAT_NV12, V4L2_PIX_FMT_NV12},
        {PIXEL_FORMAT_NV21, V4L2_PIX_FMT_NV21},
        {PIXEL_FORMAT_YV12, V4L2_PIX_FMT_YVU420M},
        {PIXEL_FORMAT_UYVY, V4L2_PIX_FMT_UYVY},
        {PIXEL_FORMAT_YUYV, V4L2_PIX_FMT_YUYV},
        {PIXEL_FORMAT_AFBC_YUV420_8, V4L2_PIX_FMT_YUV420_AFBC_8},
        {PIXEL_FORMAT_AFBC_YUV420_10, V4L2_PIX_FMT_YUV420_AFBC_10},
        {PIXEL_FORMAT_AFBC_YUV422_8, V4L2_PIX_FMT_YUV422_AFBC_8},
        {PIXEL_FORMAT_AFBC_YUV422_10, V4L2_PIX_FMT_YUV422_AFBC_10},
};
PIXEL_FORMAT_MAPPING_CONVERT(Linlonv5v7Enc, linlonv5v7enc, S32)

typedef struct _ALLinlonv5v7EncContext ALLinlonv5v7EncContext;

struct _ALLinlonv5v7EncContext {
  ALEncBaseContext stAlEncBaseContext;
  MppVencPara *pVencPara;
  MppPixelFormat ePixelFormat;  // input format
  MppCodingType eCodingType;    // output format

  Codec *stCodec;

  U8 sDevicePath[20];
  S32 nVideoFd;

  U32 nInputType;
  U32 nOutputType;

  U32 nInputFormatFourcc;
  U32 nOutputFormatFourcc;

  U32 nInputMemType;
  U32 nOutputMemType;

  BOOL bIsBlockMode;
  BOOL bIsInterlaced;
  S32 nWidth;
  S32 nHeight;
  S32 nRotation;
  S32 nScale;
  S32 nFrames;

  S32 nNaluFmt;
  BOOL bInputEos;
};

static void changeSWEO(ALLinlonv5v7EncContext *context, U32 csweo) {
  setCsweo(context->stCodec, (csweo == 1));
}

static void setEncoderFramerate(ALLinlonv5v7EncContext *context, U32 fps) {
  if (!getCsweo(context->stCodec)) {
    setEncFramerate(getOutputPort(context->stCodec), fps << 16);
  } else {
    setFps(context->stCodec, fps << 16);
  }
}

static void setBitrate(ALLinlonv5v7EncContext *context, U32 bps) {
  if (!getCsweo(context->stCodec)) {
    setEncBitrate(getOutputPort(context->stCodec), bps);
  } else {
    setEncBitrate(getOutputPort(context->stCodec), bps);
    setBps(context->stCodec, bps - 500);
  }
}

static void setPFrames(ALLinlonv5v7EncContext *context, U32 pframes) {
  setEncPFrames(getOutputPort(context->stCodec), pframes);
}

static void setBFrames(ALLinlonv5v7EncContext *context, U32 bframes) {
  setEncBFrames(getOutputPort(context->stCodec), bframes);
}

static void setSliceSpacing(ALLinlonv5v7EncContext *context, U32 spacing) {
  setEncSliceSpacing(getOutputPort(context->stCodec), spacing);
}

static void setHorizontalMVSearchRange(ALLinlonv5v7EncContext *context,
                                       U32 hmvsr) {
  setEncHorizontalMVSearchRange(getOutputPort(context->stCodec), hmvsr);
}

static void setVerticalMVSearchRange(ALLinlonv5v7EncContext *context,
                                     U32 vmvsr) {
  setEncVerticalMVSearchRange(getOutputPort(context->stCodec), vmvsr);
}

static void setH264ForceChroma(ALLinlonv5v7EncContext *context, U32 fmt) {
  setH264EncForceChroma(getOutputPort(context->stCodec), fmt);
}

static void setH264Bitdepth(ALLinlonv5v7EncContext *context, U32 bd) {
  setH264EncBitdepth(getOutputPort(context->stCodec), bd);
}

static void setH264IntraMBRefresh(ALLinlonv5v7EncContext *context, U32 period) {
  setH264EncIntraMBRefresh(getOutputPort(context->stCodec), period);
}

static void setProfile(ALLinlonv5v7EncContext *context, U32 profile) {
  setEncProfile(getOutputPort(context->stCodec), profile);
}

static void setLevel(ALLinlonv5v7EncContext *context, U32 level) {
  setEncLevel(getOutputPort(context->stCodec), level);
}

static void setConstrainedIntraPred(ALLinlonv5v7EncContext *context, U32 cip) {
  setEncConstrainedIntraPred(getOutputPort(context->stCodec), cip);
}

static void setH264EntropyCodingMode(ALLinlonv5v7EncContext *context, U32 ecm) {
  setH264EncEntropyMode(getOutputPort(context->stCodec), ecm);
}

static void setH264GOPType(ALLinlonv5v7EncContext *context, U32 gop) {
  setH264EncGOPType(getOutputPort(context->stCodec), gop);
}

static void setH264MinQP(ALLinlonv5v7EncContext *context, U32 minqp) {
  if (!getCsweo(context->stCodec)) {
    setH264EncMinQP(getOutputPort(context->stCodec), minqp);
  } else {
    setMinqp(context->stCodec, minqp);
  }
}

static void setH264MaxQP(ALLinlonv5v7EncContext *context, U32 maxqp) {
  if (!getCsweo(context->stCodec)) {
    setH264EncMaxQP(getOutputPort(context->stCodec), maxqp);
  } else {
    setMaxqp(context->stCodec, maxqp);
  }
}

static void setH264FixedQP(ALLinlonv5v7EncContext *context, U32 fqp) {
  if (!getCsweo(context->stCodec)) {
    setH264EncFixedQP(getOutputPort(context->stCodec), fqp);
  } else {
    setH264EncFixedQP(getOutputPort(context->stCodec), fqp);
    setFixedqp(context->stCodec, fqp + 2);
  }
}

static void setH264FixedQPI(ALLinlonv5v7EncContext *context, U32 fqp) {
  setH264EncFixedQPI(getOutputPort(context->stCodec), fqp);
}

static void setH264FixedQPP(ALLinlonv5v7EncContext *context, U32 fqp) {
  setH264EncFixedQPP(getOutputPort(context->stCodec), fqp);
}

static void setH264FixedQPB(ALLinlonv5v7EncContext *context, U32 fqp) {
  setH264EncFixedQPB(getOutputPort(context->stCodec), fqp);
}

static void setH264Bandwidth(ALLinlonv5v7EncContext *context, U32 bw) {
  setH264EncBandwidth(getOutputPort(context->stCodec), bw);
}

static void setVP9TileCR(ALLinlonv5v7EncContext *context, U32 tcr) {
  setVP9EncTileCR(getOutputPort(context->stCodec), tcr);
}

static void setJPEGRefreshInterval(ALLinlonv5v7EncContext *context, U32 r) {
  setJPEGEncRefreshInterval(getOutputPort(context->stCodec), r);
}

static void setJPEGQuality(ALLinlonv5v7EncContext *context, U32 q) {
  setJPEGEncQuality(getOutputPort(context->stCodec), q);
}

static void setHEVCEntropySync(ALLinlonv5v7EncContext *context, U32 es) {
  setHEVCEncEntropySync(getOutputPort(context->stCodec), es);
}

static void setHEVCTemporalMVP(ALLinlonv5v7EncContext *context, U32 tmvp) {
  setHEVCEncTemporalMVP(getOutputPort(context->stCodec), tmvp);
}

static void setStreamEscaping(ALLinlonv5v7EncContext *context, U32 sesc) {
  setEncStreamEscaping(getOutputPort(context->stCodec), sesc);
}

static void tryStopCmd(ALLinlonv5v7EncContext *context, BOOL tryStop) {
  tryEncStopCmd(getInputPort(context->stCodec), tryStop);
}

static void setEncoderMirror(ALLinlonv5v7EncContext *context, S32 mirror) {
  setPortMirror(getInputPort(context->stCodec), mirror);
}

static void setEncoderFrameCount(ALLinlonv5v7EncContext *context, S32 frames) {
  setFrameCount(getInputPort(context->stCodec), frames);
}

static void setEncoderRateControl(ALLinlonv5v7EncContext *context, const U8 *rc,
                                  S32 target_bitrate, S32 maximum_bitrate) {
  struct v4l2_rate_control v4l2_rc;
  memset(&v4l2_rc, 0, sizeof(v4l2_rc));
  if (strcmp(rc, "standard") == 0) {
    v4l2_rc.rc_type = V4L2_OPT_RATE_CONTROL_MODE_STANDARD;
  } else if (strcmp(rc, "constant") == 0) {
    v4l2_rc.rc_type = V4L2_OPT_RATE_CONTROL_MODE_CONSTANT;
  } else if (strcmp(rc, "variable") == 0) {
    v4l2_rc.rc_type = V4L2_OPT_RATE_CONTROL_MODE_VARIABLE;
  } else if (strcmp(rc, "cvbr") == 0) {
    v4l2_rc.rc_type = V4L2_OPT_RATE_CONTROL_MODE_C_VARIABLE;
  } else if (strcmp(rc, "off") == 0) {
    v4l2_rc.rc_type = V4L2_OPT_RATE_CONTROL_MODE_OFF;
  } else {
    v4l2_rc.rc_type = V4L2_OPT_RATE_CONTROL_MODE_OFF;
  }
  if (v4l2_rc.rc_type) {
    v4l2_rc.target_bitrate = target_bitrate;
  }
  if (v4l2_rc.rc_type == V4L2_OPT_RATE_CONTROL_MODE_C_VARIABLE) {
    v4l2_rc.maximum_bitrate = maximum_bitrate;
  }
  setRateControl(getOutputPort(context->stCodec), &v4l2_rc);
}

static void setEncoderCropLeft(ALLinlonv5v7EncContext *context, S32 left) {
  setCropLeft(getInputPort(context->stCodec), left);
}

static void setEncoderCropRight(ALLinlonv5v7EncContext *context, S32 right) {
  setCropRight(getInputPort(context->stCodec), right);
}

static void setEncoderCropTop(ALLinlonv5v7EncContext *context, S32 top) {
  setCropTop(getInputPort(context->stCodec), top);
}

static void setEncoderCropBottom(ALLinlonv5v7EncContext *context, S32 bottom) {
  setCropBottom(getInputPort(context->stCodec), bottom);
}

static void setEncoderVuiColourDesc(ALLinlonv5v7EncContext *context,
                                    struct v4l2_mvx_color_desc *color) {
  setVuiColourDesc(getInputPort(context->stCodec), color);
}

static void setEncoderSeiUserData(ALLinlonv5v7EncContext *context,
                                  struct v4l2_sei_user_data *sei_user_data) {
  setSeiUserData(getInputPort(context->stCodec), sei_user_data);
}

static void setEncoderHRDBufferSize(ALLinlonv5v7EncContext *context, S32 size) {
  setHRDBufferSize(getInputPort(context->stCodec), size);
}

static void setEncoderLongTermRef(ALLinlonv5v7EncContext *context, U32 mode,
                                  U32 period) {
  setLongTermRef(getInputPort(context->stCodec), mode, period);
}

ALBaseContext *al_enc_create() {
  ALLinlonv5v7EncContext *context =
      (ALLinlonv5v7EncContext *)malloc(sizeof(ALLinlonv5v7EncContext));
  if (!context) {
    error("can not malloc ALLinlonv5v7EncContext, please check! (%s)",
          strerror(errno));
    return NULL;
  }

  memset(context, 0, sizeof(ALLinlonv5v7EncContext));

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

  ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;

  context->pVencPara = para;
  context->ePixelFormat = para->PixelFormat;
  context->eCodingType = para->eCodingType;
  context->nInputFormatFourcc =
      get_linlonv5v7enc_codec_pixel_format(context->ePixelFormat);
  context->nOutputFormatFourcc =
      get_linlonv5v7enc_codec_coding_type(context->eCodingType);
  context->bIsBlockMode = MPP_FALSE;
  context->nWidth = para->nWidth;
  context->nHeight = para->nHeight;
  // context->bIsInterlaced = para->bIsInterlaced;
  // context->nRotation = para->nRotateDegree;
  // context->nScale = para->nScale;
  context->nInputMemType = V4L2_MEMORY_DMABUF;
  context->nOutputMemType = V4L2_MEMORY_MMAP;
  context->nInputType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  context->nOutputType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  context->bInputEos = MPP_FALSE;

  context->nVideoFd =
      find_v4l2_encoder(context->sDevicePath,
                        get_linlonv5v7enc_codec_coding_type(para->eCodingType));

  if (-1 == context->nVideoFd) {
    error("can not find the v4l2 codec device, please check!");
    return MPP_OPEN_FAILED;
  }

  debug("video fd = %d, device path = '%s'", context->nVideoFd,
        context->sDevicePath);

  context->stCodec = createCodec(
      context->nVideoFd, context->nWidth, context->nHeight,
      context->bIsInterlaced, context->nInputType, context->nOutputType,
      context->nInputFormatFourcc, context->nOutputFormatFourcc,
      context->nInputMemType, context->nOutputMemType, INPUT_BUF_NUM,
      OUTPUT_BUF_NUM, context->bIsBlockMode);

  setH264MinQP(context, 1);
  setH264MaxQP(context, 20);

  stream(context->stCodec);

  debug("init finish");

  return MPP_OK;
}

S32 al_enc_set_para(ALBaseContext *ctx, MppVencPara *para) { return 0; }

S32 al_enc_return_input_frame(ALBaseContext *ctx, MppData *sink_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!sink_data) {
    error("input para MppData is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;
  S32 ret = 0;
  static S32 i = 0;

  struct pollfd p = {.fd = context->nVideoFd, .events = POLLOUT};

  ret = runPoll(context->stCodec, &p);

  debug(
      "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB encoder "
      "return frame");

  if (MPP_OK == ret && p.revents & POLLOUT) {
    Buffer *buffer = dequeueBuffer(getInputPort(context->stCodec));
    struct v4l2_buffer *b = getV4l2Buffer(buffer);
    FRAME_SetID(FRAME_GetFrame(sink_data), b->index);
  } else {
    return MPP_POLL_FAILED;
  }
  return MPP_OK;
}

S32 al_enc_send_input_frame(ALBaseContext *ctx, MppData *sink_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!sink_data) {
    error("input para MppData is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;
  MppFrame *sink_frame = FRAME_GetFrame(sink_data);
  S32 ret = 0;
  debug(
      "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA encoder get "
      "frame");
  S32 buf_idx = FRAME_GetID(sink_frame);
  debug("input buffer buf_idx = %d", buf_idx);
  Buffer *buf = getBuffer(getInputPort(context->stCodec), buf_idx);
  struct v4l2_buffer *b = getV4l2Buffer(buf);

  setUserPtr(buf, 0, FRAME_GetDataPointer(sink_frame, 0));
  setUserPtr(buf, 1, FRAME_GetDataPointer(sink_frame, 1));

  b->m.planes[0].m.fd = FRAME_GetFD(sink_frame, 0);
  b->m.planes[1].m.fd = FRAME_GetFD(sink_frame, 1);
  b->m.planes[0].bytesused = 1280 * 720;
  b->m.planes[1].bytesused = 1280 * 720 * 3 / 2;
  b->m.planes[0].data_offset = 0;           // 1280 * 720;
  b->m.planes[1].data_offset = 1280 * 720;  // / 2;
  b->m.planes[0].length = 1280 * 720;
  b->m.planes[1].length = 1280 * 720 * 3 / 2;
  // debug(
  //     "sigeyi   iiiiiiiiiiiiiiiiiiiiiiiiiiiii %p %p %d",
  //    context->stCodec->stInputPort->stBuf[buf_idx]->pUserPtr[0],
  //    context->stCodec->stInputPort->stBuf[buf_idx]->pUserPtr[1],
  //     context->stCodec->stInputPort->stBuf[buf_idx]->stBufArr.m.planes[0].m.fd);

  queueBuffer(getInputPort(context->stCodec), buf);
  debug("queue input buffer, ret = %d", ret);
}

S32 al_enc_encode(ALBaseContext *ctx, MppData *sink_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!sink_data) {
    error("input para MppData is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;
  MppFrame *sink_frame = FRAME_GetFrame(sink_data);
  S32 ret = 0;
  static S32 i = 0;

  struct pollfd p = {.fd = context->nVideoFd, .events = POLLOUT};
  debug("0000000000000000000000000000000000 i = %d eos=%d", i,
        FRAME_GetEos(sink_frame));

  if (FRAME_GetEos(sink_frame)) {
    debug("enc ****************************************** eos 1");
    context->bInputEos = MPP_TRUE;
  }

  if (unlikely(i < getBufNum(getInputPort(context->stCodec)))) {
    Buffer *buf = getBuffer(getInputPort(context->stCodec), i);
    struct v4l2_buffer *b = getV4l2Buffer(buf);
    S32 y_size = context->pVencPara->nWidth * context->pVencPara->nHeight;

    memcpy(getUserPtr(buf, 0), FRAME_GetDataPointer(sink_frame, 0),
           y_size * 3 / 2);

    b->m.planes[0].bytesused = y_size;
    b->m.planes[1].bytesused = y_size * 3 / 2;
    b->m.planes[0].data_offset = 0;
    b->m.planes[1].data_offset = y_size;
    b->m.planes[0].length = y_size;
    b->m.planes[1].length = y_size * 3 / 2;

    queueBuffer(getInputPort(context->stCodec), buf);
    i++;
  } else {
    ret = runPoll(context->stCodec, &p);

    if (/*context->bInputReady*/ MPP_OK == ret && p.revents & POLLOUT) {
      handleInputBuffer(getInputPort(context->stCodec), context->bInputEos,
                        sink_data);
      i++;
    } else {
      // error("can not get input buffer");
      usleep(2000);
      return MPP_POLL_FAILED;
    }
  }

  return MPP_OK;
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

  ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;
  S32 ret = 0;

  struct pollfd p = {.fd = context->nVideoFd, .events = POLLIN};

  ret = runPoll(context->stCodec, &p);

  if (MPP_OK == ret && p.revents & POLLIN) {
    Buffer *buffer = dequeueBuffer(getOutputPort(context->stCodec));
    struct v4l2_buffer *b = getV4l2Buffer(buffer);
    if (!V4L2_TYPE_IS_OUTPUT(b->type) && b->flags & V4L2_BUF_FLAG_LAST) {
      debug("Capture EOS.");
      return MPP_CODER_EOS;
    }
    // if (buffer == NULL) return MPP_CODER_NO_DATA;
    memcpy(PACKET_GetDataPointer(PACKET_GetPacket(src_data)),
           getUserPtr(buffer, 0), b->bytesused);
    PACKET_SetLength(PACKET_GetPacket(src_data), b->bytesused);
    resetVendorFlags(buffer);
    queueBuffer(getOutputPort(context->stCodec), buffer);
  } else {
    usleep(2000);
    return MPP_CODER_NO_DATA;
  }
  return MPP_OK;
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
  /*
    ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;
    S32 ret = 0;
    S32 buf_idx = FRAME_GetID(FRAME_GetFrame(src_data));
    debug("release output idx = %d", buf_idx);
    Buffer *buf = getBuffer(getOutputPort(context->stCodec), buf_idx);

    queueBuffer(getOutputPort(context->stCodec), buf);
    debug("release output ret = %d", ret);
  */
  return MPP_OK;
}

void al_enc_destory(ALBaseContext *ctx) {
  if (!ctx) return;

  ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;

  enum v4l2_buf_type input_type =
      getV4l2BufType(getInputPort(context->stCodec));
  enum v4l2_buf_type output_type =
      getV4l2BufType(getOutputPort(context->stCodec));
  mpp_v4l2_stream_off(context->nVideoFd, &input_type);
  mpp_v4l2_stream_off(context->nVideoFd, &output_type);

  destoryCodec(context->stCodec);

  close(context->nVideoFd);
  free(context);
  context = NULL;
}
