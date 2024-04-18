/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-02-01 10:31:08
 * @LastEditTime: 2024-04-18 09:44:17
 * @Description: video decode plugin for V4L2 codec interface
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

#include "al_interface_dec.h"
#include "linlonv5v7_codec.h"
#include "log.h"
#include "mvx-v4l2-controls.h"
#include "v4l2_utils.h"

#define MODULE_TAG "linlonv5v7_dec"

CODING_TYPE_MAPPING_DEFINE(Linlonv5v7Dec, S32)
static const ALLinlonv5v7DecCodingTypeMapping
    stALLinlonv5v7DecCodingTypeMapping[] = {
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
CODING_TYPE_MAPPING_CONVERT(Linlonv5v7Dec, linlonv5v7dec, S32)

PIXEL_FORMAT_MAPPING_DEFINE(Linlonv5v7Dec, S32)
static const ALLinlonv5v7DecPixelFormatMapping
    stALLinlonv5v7DecPixelFormatMapping[] = {
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
PIXEL_FORMAT_MAPPING_CONVERT(Linlonv5v7Dec, linlonv5v7dec, S32)

typedef struct _ALLinlonv5v7DecContext ALLinlonv5v7DecContext;

struct _ALLinlonv5v7DecContext {
  ALDecBaseContext stAlDecBaseContext;
  MppVdecPara *pVdecPara;       // parameters
  MppCodingType eCodingType;    // input stream format
  MppPixelFormat ePixelFormat;  // output frame format

  Codec *stCodec;

  /***
   * for open video device, such as /dev/video0
   */
  U8 sDevicePath[20];
  S32 nVideoFd;

  /***
   * enum v4l2_buf_type
   * nInputType: always V4L2_BUF_TYPE_VIDEO_OUTPUT
   * nOutputType: always V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
   */
  U32 nInputType;
  U32 nOutputType;

  /***
   * nInputFormatFourcc: V4L2_PIX_FMT_H264, etc.
   * nOutputFormatFourcc: V4L2_PIX_FMT_NV12, etc.
   */
  U32 nInputFormatFourcc;
  U32 nOutputFormatFourcc;

  /***
   * enum v4l2_memory
   * nInputMemType: always V4L2_MEMORY_MMAP
   * nOutputMemType: always V4L2_MEMORY_DMABUF
   */
  U32 nInputMemType;
  U32 nOutputMemType;

  U32 nInputBufferNum;
  U32 nOutputBufferNum;

  /***
   * MPP_FALSE, meaning that open device node with O_NONBLOCK
   */
  BOOL bIsBlockMode;
  BOOL bIsInterlaced;

  /***
   * video width and height
   * 0x0 is supported, driver can parse real width and height and return by
   * V4L2_EVENT_SOURCE_CHANGE.
   */
  S32 nWidth;
  S32 nHeight;

  S32 nRotation;
  S32 nScale;
  S32 nFrames;

  S32 nNaluFmt;

  /***
   * EOS flag, default MPP_FALSE
   * when a packet with length=0 or with eos=1 comes, bInputEos is set to
   * MPP_TRUE.
   */
  BOOL bInputEos;

  pthread_t pollthread;

  /***
   * default MPP_FLASE
   *
   * when al_dec_destory is called, bIsDestoryed will be set to MPP_TRUE, some
   * threads stop and some resources recycle.
   */
  BOOL bIsDestoryed;

  /***
   * num of input buffer in driver
   * default 0, also 0 after flush
   */
  U32 nInputQueuedNum;

  S64 nEosPts;
};

/***
 * V4L2_CID_MVE_VIDEO_INTBUF_SIZE
 *
 * This message configures the size of internal memory used for intermediate
 * stream buffering by the firmware. For memory constrained systems this could
 * be used to reduce memory size but with a performance penalty. Most systems
 * should not use this parameter – the firmware will by default configure the
 * size based on the stream resolution.
 */
static void setH264IntBufSize(ALLinlonv5v7DecContext *context, U32 ibs) {
  setH264DecIntBufSize(getOutputPort(context->stCodec), ibs);
}

static void setDecoderInterlaced(ALLinlonv5v7DecContext *context,
                                 BOOL interlaced) {
  setPortInterlaced(getOutputPort(context->stCodec), interlaced);
}

/***
 * V4L2_CID_MVE_VIDEO_FRAME_REORDERING
 *
 * In general, frames in a video stream are not decoded in the same order they
 * should be displayed. By default, MVE outputs frames in display order, but
 * this option instead causes MVE to output frames in decoded order, as soon as
 * they are completed.
 */
static void setFrameReOrdering(ALLinlonv5v7DecContext *context, U32 fro) {
  setDecFrameReOrdering(getOutputPort(context->stCodec), fro);
}

/***
 * V4L2_CID_MVE_VIDEO_IGNORE_STREAM_HEADERS
 *
 * Specifies if the decoder shall ignore new parameter updates contained in the
 * bitstream. This makes the decoding more robust in error prone environments
 * where it is known that new parameter updates are not allowed. If the stream
 * appears to contain such updates, they are introduced as a result of
 * bit-errors.
 *
 * In H.264 this feature translates to ignoring all except the first SPS/PPS
 * NALs.
 *
 * By default this option is off.
 */
static void setIgnoreStreamHeaders(ALLinlonv5v7DecContext *context, U32 ish) {
  setDecIgnoreStreamHeaders(getOutputPort(context->stCodec), ish);
}

static void tryStopCmd(ALLinlonv5v7DecContext *context, BOOL tryStop) {
  tryDecStopCmd(getInputPort(context->stCodec), tryStop);
}

static void setNaluFormat(ALLinlonv5v7DecContext *context, S32 nalu) {
  context->nNaluFmt = nalu;
}

static void setDecoderRotation(ALLinlonv5v7DecContext *context, S32 rotation) {
  setPortRotation(getOutputPort(context->stCodec), rotation);
}

static void setDecoderDownScale(ALLinlonv5v7DecContext *context, S32 scale) {
  setPortDownScale(getOutputPort(context->stCodec), scale);
}

static void setDecoderFrameCount(ALLinlonv5v7DecContext *context, S32 frames) {
  setFrameCount(getOutputPort(context->stCodec), frames);
}

/***
 * VIDIOC_S_MVX_DSL_FRAME
 *
 * struct v4l2_mvx_dsl_frame
 * {
 *   uint32_t width;
 *   uint32_t height;
 * };
 */
static void setDecoderDSLFrame(ALLinlonv5v7DecContext *context, S32 width,
                               S32 height) {
  setDSLFrame(getOutputPort(context->stCodec), width, height);
}

/***
 * VIDIOC_S_MVX_DSL_RATIO
 *
 * struct v4l2_mvx_dsl_ratio
 * {
 *   uint32_t hor;
 *   uint32_t ver;
 * };
 */
static void setDecoderDSLRatio(ALLinlonv5v7DecContext *context, S32 hor,
                               S32 ver) {
  setDSLRatio(getOutputPort(context->stCodec), hor, ver);
}

/***
 * VIDIOC_S_MVX_DSL_MODE
 */
static void setDecoderDSLMode(ALLinlonv5v7DecContext *context, S32 mode) {
  setDSLMode(getOutputPort(context->stCodec), mode);
}

static S32 checkCodingTypeAndProfile(MppCodingType type, S32 profile) {
  if (type == CODING_MPEG2 && profile == PROFILE_MPEG2_HIGH) {
    error("not support CODING_MPEG2->PROFILE_MPEG2_HIGH!");
    return MPP_NOT_SUPPORTED_FORMAT;
  }

  return MPP_OK;
}

/***
 * pthread for poll event, need usleep, or CPU usage will soar, biubiu.
 */
void *runpoll(void *private_data) {
  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)private_data;
  S32 ret = 0;

  while (1) {
    static S32 tmp = 0;
    if (context->bIsDestoryed) pthread_exit(NULL);

    struct pollfd p = {.fd = context->nVideoFd, .events = POLLPRI};
    S32 ret = poll(&p, 1, POLL_TIMEOUT);

    if (ret < 0) {
      error("Poll returned error code.");
    }

    if (p.revents & POLLERR) {
      error("Poll returned error event.");
    }

    if (0 == ret) {
      // this log is boring, disable
      // error("Event poll timed out.");
    }

    if (p.revents & POLLPRI) {
      handleEvent(context->stCodec);
    }

    usleep(10000);
    tmp++;
    if (200 == tmp) {
      tmp = 0;
      info("Now k1 hardware decoding ...");
    }
  }
}

ALBaseContext *al_dec_create() {
  ALLinlonv5v7DecContext *context =
      (ALLinlonv5v7DecContext *)malloc(sizeof(ALLinlonv5v7DecContext));
  if (!context) {
    error("can not malloc ALLinlonv5v7DecContext, please check! (%s)",
          strerror(errno));
    return NULL;
  }

  memset(context, 0, sizeof(ALLinlonv5v7DecContext));

  debug("init create");

  return &(context->stAlDecBaseContext.stAlBaseContext);
}

RETURN al_dec_init(ALBaseContext *ctx, MppVdecPara *para) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!para) {
    error("input para MppVdecPara is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  S32 ret = 0;

  ret = checkCodingTypeAndProfile(para->eCodingType, para->nProfile);
  if (ret) {
    error("not support this format or profile, please check!");
    return MPP_NOT_SUPPORTED_FORMAT;
  }

  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;

  context->pVdecPara = para;
  context->eCodingType = para->eCodingType;
  context->ePixelFormat = para->eOutputPixelFormat;
  context->nInputFormatFourcc =
      get_linlonv5v7dec_codec_coding_type(context->eCodingType);
  context->nOutputFormatFourcc =
      get_linlonv5v7dec_codec_pixel_format(context->ePixelFormat);
  context->bIsBlockMode = MPP_FALSE;
  context->nWidth = para->nWidth;
  context->nHeight = para->nHeight;
  context->bIsInterlaced = para->bIsInterlaced;
  context->nRotation = para->nRotateDegree;
  context->nScale = para->nScale;
  context->nInputMemType = V4L2_MEMORY_MMAP;
  if (para->eFrameBufferType == MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL) {
    context->nOutputMemType = V4L2_MEMORY_DMABUF;
  } else if (para->eFrameBufferType == MPP_FRAME_BUFFERTYPE_NORMAL_INTERNAL) {
    context->nOutputMemType = V4L2_MEMORY_MMAP;
  }
  context->nInputType = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  context->nOutputType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  context->bInputEos = MPP_FALSE;
  context->bIsDestoryed = MPP_FALSE;
  context->nInputQueuedNum = 0;
  context->nEosPts = -1;

  // if APP do not set nInputBufferNum and nOutputBufferNum, use default value,
  // and sync MPP this default value to APP.
  if (!context->pVdecPara->nInputBufferNum)
    context->pVdecPara->nInputBufferNum = DECODER_INPUT_BUF_NUM;
  if (!context->pVdecPara->nOutputBufferNum)
    context->pVdecPara->nOutputBufferNum = DECODER_OUTPUT_BUF_NUM;

  debug(
      "input para check: foramt:0x%x output format:0x%x input buffer num:%d "
      "output buffer num:%d",
      context->nInputFormatFourcc, context->nOutputFormatFourcc,
      context->pVdecPara->nInputBufferNum,
      context->pVdecPara->nOutputBufferNum);

  for (S32 i = 0; i < MAX_OUTPUT_BUF_NUM; i++)
    context->pVdecPara->bIsBufferInDecoder[i] = MPP_TRUE;

  context->nInputBufferNum = context->pVdecPara->nInputBufferNum;
  context->nOutputBufferNum = context->pVdecPara->nOutputBufferNum;

  para->eDataTransmissinMode = MPP_INPUT_SYNC_OUTPUT_ASYNC;

  context->nVideoFd =
      find_v4l2_decoder(context->sDevicePath,
                        get_linlonv5v7dec_codec_coding_type(para->eCodingType));
  if (-1 == context->nVideoFd) {
    error("can not find and open the v4l2 codec device, please check!");
    return MPP_OPEN_FAILED;
  }

  debug("video fd = %d, device path = '%s'", context->nVideoFd,
        context->sDevicePath);

  context->stCodec = createCodec(
      context->nVideoFd, context->nWidth, context->nHeight,
      context->bIsInterlaced, context->nInputType, context->nOutputType,
      context->nInputFormatFourcc, context->nOutputFormatFourcc,
      context->nInputMemType, context->nOutputMemType, context->nInputBufferNum,
      context->nOutputBufferNum, context->bIsBlockMode, para->eFrameBufferType);
  if (!context->stCodec) {
    error("create Codec failed, please check!");
    return MPP_INIT_FAILED;
  }

  // set some parameters on the stream level
  setDecoderInterlaced(context, context->bIsInterlaced);
  setDecoderRotation(context, context->nRotation);
  setDecoderDownScale(context, context->nScale);

  // setformat, allocate buffer, stream on
  stream(context->stCodec);

  // pthread for handle event or something
  ret = pthread_create(&context->pollthread, NULL, runpoll, (void *)context);

  context->pVdecPara->nInputQueueLeftNum =
      getBufNum(getInputPort(context->stCodec));

  debug("init finish");

  return MPP_OK;
}

RETURN al_dec_getparam(ALBaseContext *ctx, MppVdecPara **para) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;

  struct pollfd p = {.fd = context->nVideoFd, .events = POLLOUT};
  S32 ret = poll(&p, 1, POLL_TIMEOUT);

  if (ret < 0) {
    error("Poll returned error code.");
  }

  if (p.revents & POLLERR) {
    error("Poll returned error event.");
  }

  if (0 == ret) {
    // error("Getpara poll timed out.");
  }

  // used for update nInputQueueLeftNum, APP use it to decide whether send
  // packet to MPP, now FFmpeg do like this.
  if (p.revents & POLLOUT && !context->pVdecPara->nInputQueueLeftNum) {
    context->pVdecPara->nInputQueueLeftNum = 1;
  }

  *para = context->pVdecPara;
  return MPP_OK;
}

S32 al_dec_decode(ALBaseContext *ctx, MppData *sink_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!sink_data) {
    error("input para MppData is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;
  MppPacket *packet = PACKET_GetPacket(sink_data);
  S32 ret = 0;
  struct pollfd p = {.fd = context->nVideoFd, .events = POLLOUT};

  if ((!PACKET_GetLength(packet))) {
    debug("length of input packet is 0, EOS is coming, pts(%ld)",
          PACKET_GetPts(packet));
    context->bInputEos = MPP_TRUE;
    context->nEosPts = PACKET_GetPts(packet);
  }

  if (PACKET_GetEos(packet)) {
    debug("eos flag of input packet is set, EOS is coming(%ld)",
          PACKET_GetPts(packet));
    context->bInputEos = MPP_TRUE;
    context->nEosPts = PACKET_GetPts(packet);
  }

  if (unlikely(context->nInputQueuedNum <
               getBufNum(getInputPort(context->stCodec)))) {
    Buffer *buf =
        getBuffer(getInputPort(context->stCodec), context->nInputQueuedNum);
    memcpy(getUserPtr(buf, 0), PACKET_GetDataPointer(packet),
           PACKET_GetLength(packet));
#if 0
    debug("input check:%x %x %x %x %x %x %x %x %x",
          *(S32 *)PACKET_GetDataPointer(packet),
          *(S32 *)(PACKET_GetDataPointer(packet) + 4),
          *(S32 *)(PACKET_GetDataPointer(packet) + 8),
          *(S32 *)(PACKET_GetDataPointer(packet) + 12),
          *(S32 *)(PACKET_GetDataPointer(packet) + 16),
          *(S32 *)(PACKET_GetDataPointer(packet) + 20),
          *(S32 *)(PACKET_GetDataPointer(packet) + 24),
          *(S32 *)(PACKET_GetDataPointer(packet) + 28),
          *(S32 *)(PACKET_GetDataPointer(packet) + 32));
#endif
    struct v4l2_buffer *b = getV4l2Buffer(buf);
    b->bytesused = PACKET_GetLength(packet);
    setEndOfFrame(buf, MPP_TRUE);
    setEndOfStream(buf, MPP_FALSE);
    setTimeStamp(buf, PACKET_GetPts(packet));
    ret = queueBuffer(getInputPort(context->stCodec), buf);
    if (ret) {
      error("queueBuffer failed, should not failed, please check!");
      return ret;
    }
    context->nInputQueuedNum++;
    context->pVdecPara->nInputQueueLeftNum--;
  } else {
    ret = runPoll(context->stCodec, &p);
    if (MPP_OK == ret && p.revents & POLLOUT) {
      ret = handleInputBuffer(getInputPort(context->stCodec),
                              context->bInputEos, sink_data);
      if (ret) {
        error("handleInputBuffer failed, should not failed, please check!");
        return ret;
      }
      context->pVdecPara->nInputQueueLeftNum--;
    } else {
      // error("can not get input buffer");
      // usleep(1000);
      return MPP_POLL_FAILED;
    }
  }
  return MPP_OK;
}

RETURN al_dec_request_output_frame(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;
  S32 ret = 0;

  struct pollfd p = {.fd = context->nVideoFd, .events = POLLIN};

  ret = runPoll(context->stCodec, &p);
  if (MPP_OK == ret && p.revents & POLLIN) {
    ret = handleOutputBuffer(getOutputPort(context->stCodec), MPP_FALSE,
                             src_data);
    // debug("ok, a frame is handled, get it! ret = %d", ret);
    if (ret == MPP_RESOLUTION_CHANGED) {
      if (context->nRotation == 90 || context->nRotation == 270) {
        context->pVdecPara->nWidth =
            getBufHeight(getOutputPort(context->stCodec));
        context->pVdecPara->nHeight =
            getBufWidth(getOutputPort(context->stCodec));
      } else {
        context->pVdecPara->nWidth =
            getBufWidth(getOutputPort(context->stCodec));
        context->pVdecPara->nHeight =
            getBufHeight(getOutputPort(context->stCodec));
      }
      context->pVdecPara->nOutputBufferNum =
          getBufNum(getOutputPort(context->stCodec));

      // when resolution changed, output buffers should be realloced, so set
      // bIsBufferInDecoder to MPP_TRUE.
      for (U32 i = 0; i < context->pVdecPara->nOutputBufferNum; i++) {
        context->pVdecPara->nOutputBufferFd[i] =
            getBufFd(getOutputPort(context->stCodec), i);
        context->pVdecPara->bIsBufferInDecoder[i] = MPP_TRUE;
      }
    } else if (ret == MPP_CODER_NO_DATA) {
      return MPP_CODER_NO_DATA;
    } else if (ret == MPP_ERROR_FRAME) {
      // check if it is the last frame
      if (context->nEosPts > 0 &&
          FRAME_GetPts(FRAME_GetFrame(src_data)) == context->nEosPts) {
        error("it is a EOS frame eos pts:(%ld)", context->nEosPts);
        return MPP_CODER_EOS;
      }
    }
  } else {
    // debug("no data, please try again!");
    // usleep(1000);
    // error("can not get output buffer");
    return MPP_CODER_NO_DATA;
  }

  if (ret == MPP_OK)
    context->pVdecPara
        ->bIsBufferInDecoder[FRAME_GetID(FRAME_GetFrame(src_data))] = MPP_FALSE;

  return ret;
}

RETURN al_dec_return_output_frame(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!src_data) {
    error("input para MppData is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;
  S32 ret = 0;

  S32 buf_idx = FRAME_GetID(FRAME_GetFrame(src_data));
  // debug("release output idx = %d", buf_idx);
  Buffer *buf = getBuffer(getOutputPort(context->stCodec), buf_idx);
  if (!buf) {
    error("buf is NULL, this should not happen, please check!");
  } else {
    clearBytesUsed(buf);

    ret = queueBuffer(getOutputPort(context->stCodec), buf);
    if (ret) {
      error("queueBuffer failed, this should not happen, please check!");
    }
    // debug("release output ret = %d", ret);

    context->pVdecPara->bIsBufferInDecoder[buf_idx] = MPP_TRUE;
  }

  return MPP_OK;
}

S32 al_dec_reset(ALBaseContext *ctx) {
  if (!ctx) return MPP_NULL_POINTER;
  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;

  debug("Reset start ========================================");

  handleFlush(context->stCodec, MPP_FALSE);
  context->nInputQueuedNum = 0;
  context->pVdecPara->nInputQueueLeftNum =
      getBufNum(getInputPort(context->stCodec));
  context->bInputEos = MPP_FALSE;

  debug("Reset finish ========================================");

  return MPP_OK;
}

S32 al_dec_flush(ALBaseContext *ctx) {
  if (!ctx) return MPP_NULL_POINTER;
  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;

  debug("Flush start ========================================");

  handleFlush(context->stCodec, MPP_FALSE);
  context->nInputQueuedNum = 0;
  context->pVdecPara->nInputQueueLeftNum =
      getBufNum(getInputPort(context->stCodec));
  context->bInputEos = MPP_FALSE;

  debug("Flush finish ========================================");

  return MPP_OK;
}

void al_dec_destory(ALBaseContext *ctx) {
  if (!ctx) return;
  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;
  context->bIsDestoryed = MPP_TRUE;
  debug("destory start");
  pthread_join(context->pollthread, NULL);
  debug("pthread join finish");

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
