/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-02-01 10:43:49
 * @LastEditTime: 2024-04-09 09:07:41
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

#define MODULE_TAG "linlonv5v7_enc"

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
  MppVencPara *pVencPara;       // parameters
  MppPixelFormat ePixelFormat;  // input stream format
  MppCodingType eCodingType;    // output frame format

  Codec *stCodec;

  /***
   * for open video device, such as /dev/video0
   */
  U8 sDevicePath[20];
  S32 nVideoFd;

  /***
   * enum v4l2_buf_type
   * nInputType: always V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
   * nOutputType: always V4L2_BUF_TYPE_VIDEO_CAPTURE
   */
  U32 nInputType;
  U32 nOutputType;

  /***
   * nInputFormatFourcc: V4L2_PIX_FMT_NV12, etc.
   * nOutputFormatFourcc: V4L2_PIX_FMT_H264, etc.
   */
  U32 nInputFormatFourcc;
  U32 nOutputFormatFourcc;

  /***
   * enum v4l2_memory
   * nInputMemType: always V4L2_MEMORY_DMABUF
   * nOutputMemType: always V4L2_MEMORY_MMAP
   */
  U32 nInputMemType;
  U32 nOutputMemType;

  /***
   * MPP_FALSE, meaning that open device node with O_NONBLOCK
   */
  BOOL bIsBlockMode;
  BOOL bIsInterlaced;

  /***
   * video width and height
   * 0x0 is not supported, because MPP do not know the size of YUV.
   */
  S32 nWidth;
  S32 nHeight;

  S32 nRotation;
  S32 nScale;
  S32 nFrames;

  S32 nNaluFmt;

  /***
   * EOS flag, default MPP_FALSE
   * when a packet with eos=1 comes, bInputEos is set to MPP_TRUE.
   */
  BOOL bInputEos;
  BOOL bOutputEos;

  /***
   * num of input buffer in driver
   * default 0, also 0 after flush
   */
  U32 nInputQueuedNum;
  BOOL bBufferNeedReturned[MAX_INPUT_BUF_NUM];
};

static void changeSWEO(ALLinlonv5v7EncContext *context, U32 csweo) {
  setCsweo(context->stCodec, (csweo == 1));
}

/***
 * V4L2_CID_MVE_VIDEO_FRAME_RATE
 *
 * Sets the frame rate in frames per second (FPS), represented in Q16 format,
 * that is, signed 15.16 fixed-point format. Frame rate values are limited to
 * between 1 and 256 frames per second.
 */
static void setEncoderFramerate(ALLinlonv5v7EncContext *context, U32 fps) {
  if (!getCsweo(context->stCodec)) {
    setEncFramerate(getOutputPort(context->stCodec), fps << 16);
  } else {
    setFps(context->stCodec, fps << 16);
  }
}

/***
 * V4L2_CID_MPEG_VIDEO_BITRATE
 */
static void setBitrate(ALLinlonv5v7EncContext *context, U32 bps) {
  if (!getCsweo(context->stCodec)) {
    setEncBitrate(getOutputPort(context->stCodec), bps);
  } else {
    setEncBitrate(getOutputPort(context->stCodec), bps);
    setBps(context->stCodec, bps - 500);
  }
}

/***
 * V4L2_CID_MVE_VIDEO_P_FRAMES
 *
 * Options related to the group-of-pictures (GOP) and Long Term Reference (LTR)
 * structure, see below.
 *
 * The three options ENC_P_FRAMES, ENC_B_FRAMES, GOP_TYPE can be used to
 * configure the GOP structure. Each GOP begins with an I frame, typically
 * followed by some P, and possibly B frames: ENC_P_FRAMES sets the number of P
 * frames between two I frames. ENC_B_FRAMES sets the number of B frames that
 * comes with each P frame.
 */
static void setPFrames(ALLinlonv5v7EncContext *context, U32 pframes) {
  setEncPFrames(getOutputPort(context->stCodec), pframes);
}

/***
 * V4L2_CID_MPEG_VIDEO_B_FRAMES
 */
static void setBFrames(ALLinlonv5v7EncContext *context, U32 bframes) {
  setEncBFrames(getOutputPort(context->stCodec), bframes);
}

/***
 * V4L2_CID_MVE_VIDEO_GOP_TYPE
 *
 * GOP_TYPE can be one of the following variants:
 * MVE_OPT_GOP_TYPE_BIDIRECTIONAL For example, with this GOP type, setting
 * ENC_P_FRAMES = 3 and ENC_B_FRAMES = 2 gives the following GOP structure, in
 * display order, so that there is frame re-ordering in the bitstream: IBBPBBP
 *
 * MVE_OPT_GOP_TYPE_LOW_DELAY Conversely, setting ENC_P_FRAMES = 3 and
 * ENC_B_FRAMES = 2 and GOP_TYPE = LOW_DELAY gives the following GOP structure,
 * in display order, with no frame re-ordering: IPBBPBB
 *
 * MVE_OPT_GOP_TYPE_PYRAMID Setting this GOP type, forces the ENC_B_FRAMES
 * parameter to 3. The following image shows some of the B frames used as
 * reference frames: IBBBPBBBP
 */
static void setH264GOPType(ALLinlonv5v7EncContext *context, U32 gop) {
  setH264EncGOPType(getOutputPort(context->stCodec), gop);
}

/***
 * V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB
 * V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE
 *
 * Suggested number of macroblocks, CTUs in one slice.
 */
static void setSliceSpacing(ALLinlonv5v7EncContext *context, U32 spacing) {
  setEncSliceSpacing(getOutputPort(context->stCodec), spacing);
}

/***
 * V4L2_CID_MPEG_VIDEO_MV_H_SEARCH_RANGE
 *
 * For testing and debugging, the search range for motion vectors can be
 * restricted. By default, the largest possible values are used.
 */
static void setHorizontalMVSearchRange(ALLinlonv5v7EncContext *context,
                                       U32 hmvsr) {
  setEncHorizontalMVSearchRange(getOutputPort(context->stCodec), hmvsr);
}

/***
 * V4L2_CID_MPEG_VIDEO_MV_V_SEARCH_RANGE
 *
 * For testing and debugging, the search range for motion vectors can be
 * restricted. By default, the largest possible values are used.
 */
static void setVerticalMVSearchRange(ALLinlonv5v7EncContext *context,
                                     U32 vmvsr) {
  setEncVerticalMVSearchRange(getOutputPort(context->stCodec), vmvsr);
}

/***
 * V4L2_CID_MVE_VIDEO_FORCE_CHROMA_FORMAT
 */
static void setH264ForceChroma(ALLinlonv5v7EncContext *context, U32 fmt) {
  setH264EncForceChroma(getOutputPort(context->stCodec), fmt);
}

/***
 * V4L2_CID_MVE_VIDEO_BITDEPTH_LUMA
 * V4L2_CID_MVE_VIDEO_BITDEPTH_CHROMA
 */
static void setH264Bitdepth(ALLinlonv5v7EncContext *context, U32 bd) {
  setH264EncBitdepth(getOutputPort(context->stCodec), bd);
}

/***
 * V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB
 */
static void setH264IntraMBRefresh(ALLinlonv5v7EncContext *context, U32 period) {
  setH264EncIntraMBRefresh(getOutputPort(context->stCodec), period);
}

/***
 * V4L2_CID_MPEG_VIDEO_H264_PROFILE
 * V4L2_CID_MVE_VIDEO_H265_PROFILE
 */
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

/***
 * The valid range of QP values is different for different standards:
 *
 * H.264 and HEVC
 *     0-51.
 *
 * VP8
 *     0-63.
 *     Internally, this is remapped to 0-127 in the bitstream.
 *
 * VP9
 *     0-63.
 *     Internally, this is remapped to 0-255 in the bitstream.
 */
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

static void setHEVCMinQP(ALLinlonv5v7EncContext *context, U32 minqp) {
  if (!getCsweo(context->stCodec)) {
    setHEVCEncMinQP(getOutputPort(context->stCodec), minqp);
  } else {
    setMinqp(context->stCodec, minqp);
  }
}

static void setHEVCMaxQP(ALLinlonv5v7EncContext *context, U32 maxqp) {
  if (!getCsweo(context->stCodec)) {
    setHEVCEncMaxQP(getOutputPort(context->stCodec), maxqp);
  } else {
    setMaxqp(context->stCodec, maxqp);
  }
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

/***
 * V4L2_CID_MVE_VIDEO_STREAM_ESCAPING
 *
 * This configures whether the input byte stream contains escape codes or
 * whether it is a raw byte stream without escape codes. The default value is
 * escape codes enabled.
 */
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

/***
 * The struct mve_buffer_param_rate_control is used to enable rate control, and
 * to set the target bitrate.
 *
 * MVE_OPT_RATE_CONTROL_MODE_OFF:
 * This sets fixed a QP mode, this is the default. The target_bitrate value is
 * ignored and the quantization values QP_I, QP_P and QP_B are used. These can
 * be set in separate options, otherwise default built-in values are used.
 *
 * MVE_OPT_RATE_CONTROL_MODE_VARIABLE:
 * This mode aims to match bitrate target_bitrate while maximizing visual
 * quality. Arm China recommends you use this mode.
 * MVE_OPT_RATE_CONTROL_MODE_STANDARD is deprecated but corresponds to the same
 * mode as MVE_OPT_RATE_CONTROL_MODE_VARIABLE.
 *
 * MVE_OPT_RATE_CONTROL_MODE_CONSTANT:
 * This mode aims to keep the output bitstream at a fixed bitrate. The bitrate
 * is based on a model of a Hypothetical Reference Decoder (HRD) buffer that is
 * emptied at target_bitrate. The size of the HRD buffer is set by default by
 * the encoder, but can also be configured by the host. See
 * MVE_BUFFER_PARAM_TYPE_RATE_CONTOL_HRD_BUF_SIZE. Typically this mode gives a
 * bitrate closer to the target_bitrate, but can sometimes result in lower
 * visual quality.
 *
 * MVE_OPT_RATE_CONTROL_MODE_C_VARIABLE:
 * Argument: max_bitrate. This mode aims to constrain the maximum bitrate to a
 * desired max_bitrate, and in the meantime to match target_bitrate as described
 * in MVE_OPT_RATE_CONTROL_MODE_VARIABLE.
 */
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

static S32 checkPixelFormatAndCodingTypeAndProfile(MppPixelFormat format,
                                                   MppCodingType type,
                                                   S32 profile) {
  // to do
  return MPP_OK;
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

  ret = checkPixelFormatAndCodingTypeAndProfile(
      para->PixelFormat, para->eCodingType, para->nProfile);
  if (ret) {
    error("not support this format or profile, please check!");
    return MPP_NOT_SUPPORTED_FORMAT;
  }

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
  context->bOutputEos = MPP_FALSE;
  context->nInputQueuedNum = 0;
  for (S32 i = 0; i < MAX_INPUT_BUF_NUM; i++) {
    context->bBufferNeedReturned[i] = 0;
  }

  if (para->eFrameBufferType == MPP_FRAME_BUFFERTYPE_NORMAL_EXTERNAL) {
    context->nInputMemType = V4L2_MEMORY_USERPTR;
  }
  para->eDataTransmissinMode = MPP_INPUT_SYNC_OUTPUT_SYNC;

  debug("input para check: foramt:0x%x output format:0x%x",
        context->nInputFormatFourcc, context->nOutputFormatFourcc);

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
      context->nInputMemType, context->nOutputMemType, ENCODER_INPUT_BUF_NUM,
      ENCODER_OUTPUT_BUF_NUM, context->bIsBlockMode, para->eFrameBufferType);
  if (!context->stCodec) {
    error("create Codec failed, please check!");
    return MPP_INIT_FAILED;
  }

  // set some parameters on the stream level
  // setH264MinQP(context, 1);
  // setH264MaxQP(context, 20);

  // setHEVCMinQP(context, 1);
  // setHEVCMaxQP(context, 20);

  setEncoderRateControl(context, "off", 0, 0);

  // setformat, allocate buffer, stream on
  stream(context->stCodec);

  debug("init finish");

  return MPP_OK;
}

S32 al_enc_set_para(ALBaseContext *ctx, MppVencPara *para) { return MPP_OK; }

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
  static S32 i = 0;
  S32 index;
  struct pollfd p = {.fd = context->nVideoFd, .events = POLLOUT};

  if (FRAME_GetEos(sink_frame) == FRAME_EOS_WITH_DATA) {
    debug("eos flag of input frame with data is set, EOS is coming");
    context->bInputEos = MPP_TRUE;
  }

  if (FRAME_GetEos(sink_frame) == FRAME_EOS_WITHOUT_DATA) {
    debug("eos flag of input frame without data is set, EOS is coming");
    context->bInputEos = MPP_TRUE;

    // gstreamer last sink_data only had eos flag, no memory
    sendEncStopCommand(getInputPort(context->stCodec));
    return MPP_OK;
  }

  if (unlikely(context->nInputQueuedNum <
               getBufNum(getInputPort(context->stCodec)))) {
    Buffer *buf =
        getBuffer(getInputPort(context->stCodec), context->nInputQueuedNum);
    struct v4l2_buffer *b = getV4l2Buffer(buf);
    if (context->nInputMemType == V4L2_MEMORY_USERPTR) {
      if (context->ePixelFormat == PIXEL_FORMAT_NV12) {
        setExternalUserPtrFrame(buf, (U8 *)FRAME_GetDataPointer(sink_frame, 0),
                                (U8 *)FRAME_GetDataPointer(sink_frame, 1), NULL,
                                FRAME_GetID(sink_frame));
      } else if (context->ePixelFormat == PIXEL_FORMAT_I420) {
        setExternalUserPtrFrame(buf, (U8 *)FRAME_GetDataPointer(sink_frame, 0),
                                (U8 *)FRAME_GetDataPointer(sink_frame, 1),
                                (U8 *)FRAME_GetDataPointer(sink_frame, 2),
                                FRAME_GetID(sink_frame));
      }
    } else if (context->nInputMemType == V4L2_MEMORY_DMABUF) {
      setExternalDmaBuf(buf, FRAME_GetFD(sink_frame, 0),
                        (U8 *)FRAME_GetDataPointer(sink_frame, 0),
                        FRAME_GetID(sink_frame));
    }
    setTimeStamp(buf, FRAME_GetPts(sink_frame));

    queueBuffer(getInputPort(context->stCodec), buf);
    if (ret) {
      error("Failed to queue buffer. type = %d (%s)", b->type, strerror(errno));
      return MPP_IOCTL_FAILED;
    }

    context->nInputQueuedNum++;
  } else {
    Buffer *buf =
        getBuffer(getInputPort(context->stCodec), FRAME_GetID(sink_frame));

    if (!getIsQueued(buf)) {
      if (context->nInputMemType == V4L2_MEMORY_USERPTR) {
        if (context->ePixelFormat == PIXEL_FORMAT_NV12) {
          setExternalUserPtrFrame(buf,
                                  (U8 *)FRAME_GetDataPointer(sink_frame, 0),
                                  (U8 *)FRAME_GetDataPointer(sink_frame, 1),
                                  NULL, FRAME_GetID(sink_frame));
        } else if (context->ePixelFormat == PIXEL_FORMAT_I420) {
          setExternalUserPtrFrame(buf,
                                  (U8 *)FRAME_GetDataPointer(sink_frame, 0),
                                  (U8 *)FRAME_GetDataPointer(sink_frame, 1),
                                  (U8 *)FRAME_GetDataPointer(sink_frame, 2),
                                  FRAME_GetID(sink_frame));
        }
      } else if (context->nInputMemType == V4L2_MEMORY_DMABUF) {
        setExternalDmaBuf(buf, FRAME_GetFD(sink_frame, 0),
                          (U8 *)FRAME_GetDataPointer(sink_frame, 0),
                          FRAME_GetID(sink_frame));
      }
      setTimeStamp(buf, FRAME_GetPts(sink_frame));
      setEndOfStream(buf, context->bInputEos);
      ret = queueBuffer(getInputPort(context->stCodec), buf);
      if (ret) {
        error("should not queue fail, please check!");
        return MPP_POLL_FAILED;
      }
      setIsQueued(buf, MPP_TRUE);
    } else {
      error("wait a moment!");
      return MPP_POLL_FAILED;
    }
  }

  return MPP_OK;
}

S32 al_enc_return_input_frame(ALBaseContext *ctx, MppData *sink_data) {
  ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;
  S32 ret = 0;
  struct pollfd p = {.fd = context->nVideoFd, .events = POLLOUT};

  ret = runPoll(context->stCodec, &p);
  if (MPP_OK == ret && p.revents & POLLOUT) {
    Buffer *buffer = dequeueBuffer(getInputPort(context->stCodec));
    if (!buffer) {
      error(
          "dequeueBuffer failed, this dequeueBuffer must successed, because it "
          "is after Poll, please check!");
    }
    setIsQueued(buffer, MPP_FALSE);
    return getExtraId(buffer);
  } else {
    // error("can not get input buffer");
    // usleep(2000);
    return -1;
  }

  return -1;
}

S32 al_enc_get_output_stream(ALBaseContext *ctx, MppData *src_data) {
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
      context->bOutputEos = MPP_TRUE;
    }
    // if (buffer == NULL) return MPP_CODER_NO_DATA;
    if (context->eCodingType == CODING_H265 ||
        context->eCodingType == CODING_VP9) {
      memcpy(PACKET_GetDataPointer(PACKET_GetPacket(src_data)),
             getUserPtrForHevcAndVp9Encode(buffer, 0), b->bytesused);
    } else {
      memcpy(PACKET_GetDataPointer(PACKET_GetPacket(src_data)),
             getUserPtr(buffer, 0), b->bytesused);
    }
    PACKET_SetLength(PACKET_GetPacket(src_data), b->bytesused);
    resetVendorFlags(buffer);
    queueBuffer(getOutputPort(context->stCodec), buffer);

    if (context->bOutputEos) return MPP_CODER_EOS;
  } else {
    // usleep(2000);
    return MPP_CODER_NO_DATA;
  }
  return MPP_OK;
}

S32 al_enc_flush(ALBaseContext *ctx) {
  if (!ctx) return MPP_NULL_POINTER;
  ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;

  debug("Flush start ========================================");
  handleFlush(context->stCodec, MPP_FALSE);
  context->nInputQueuedNum = 0;
  // context->pVdecPara->nInputQueueLeftNum =
  //     getBufNum(getInputPort(context->stCodec));
  debug("Flush finish ========================================");

  return MPP_OK;
}

void al_enc_destory(ALBaseContext *ctx) {
  if (!ctx) return;
  ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;

  debug("destory start");
  if (context->nVideoFd && context->stCodec) {
    enum v4l2_buf_type input_type =
        getV4l2BufType(getInputPort(context->stCodec));
    enum v4l2_buf_type output_type =
        getV4l2BufType(getOutputPort(context->stCodec));
    mpp_v4l2_stream_off(context->nVideoFd, &input_type);
    mpp_v4l2_stream_off(context->nVideoFd, &output_type);
    debug("stream off finish");

    destoryCodec(context->stCodec);
    debug("destory codec finish");

    close(context->nVideoFd);
  }
  free(context);
  context = NULL;
}
