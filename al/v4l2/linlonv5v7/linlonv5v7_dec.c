/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-02-01 10:31:08
 * @LastEditTime: 2024-01-20 14:25:10
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

#define MODULE_TAG "v4l2dec"

//#define BS_BUF_SIZE (1 << 20)

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
  MppVdecPara *pVdecPara;
  MppCodingType eCodingType;    // input format
  MppPixelFormat ePixelFormat;  // output format

  Codec *stCodec;

  U8 sDevicePath[20];
  S32 nVideoFd;

  U32 nInputType;
  U32 nOutputType;

  U32 nInputFormatFourcc;
  U32 nOutputFormatFourcc;

  U32 nInputMemType;
  U32 nOutputMemType;

  U32 nInputBufferNum;
  U32 nOutputBufferNum;

  BOOL bIsBlockMode;
  BOOL bIsInterlaced;
  S32 nWidth;
  S32 nHeight;
  S32 nRotation;
  S32 nScale;
  S32 nFrames;

  S32 nNaluFmt;
  BOOL bInputEos;

  pthread_t pollthread;
  BOOL bInputReady;
  BOOL bOutputReady;

  BOOL bIsDestoryed;
  U32 nInputQueuedNum;
  BOOL bIsFlushed;
};

static void setH264IntBufSize(ALLinlonv5v7DecContext *context, U32 ibs) {
  setH264DecIntBufSize(getOutputPort(context->stCodec), ibs);
}

static void setDecoderInterlaced(ALLinlonv5v7DecContext *context,
                                 BOOL interlaced) {
  setPortInterlaced(getOutputPort(context->stCodec), interlaced);
}

static void setFrameReOrdering(ALLinlonv5v7DecContext *context, U32 fro) {
  setDecFrameReOrdering(getOutputPort(context->stCodec), fro);
}

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

static void setDecoderDSLFrame(ALLinlonv5v7DecContext *context, S32 width,
                               S32 height) {
  setDSLFrame(getOutputPort(context->stCodec), width, height);
}

static void setDecoderDSLRatio(ALLinlonv5v7DecContext *context, S32 hor,
                               S32 ver) {
  setDSLRatio(getOutputPort(context->stCodec), hor, ver);
}

static void setDecoderDSLMode(ALLinlonv5v7DecContext *context, S32 mode) {
  setDSLMode(getOutputPort(context->stCodec), mode);
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

  return &(context->stAlDecBaseContext.stAlBaseContext);
}

void *runpoll(void *private_data) {
  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)private_data;
  S32 ret = 0;

  while (1) {
    static S32 tmp = 0;
    if (context->bIsDestoryed) pthread_exit(NULL);

    struct pollfd p = {.fd = context->nVideoFd, .events = POLLPRI};

    // if (context->stCodec->stInputPort->pending > 0)
    //{ p.events |= POLLOUT; }

    // if (context->stCodec->stOutputPort->pending > 0)
    //{ p.events |= POLLIN; }

    S32 ret = poll(&p, 1, POLL_TIMEOUT);

    // debug("pending %d %d ret = %d revents=%x",
    // context->stCodec->stInputPort->pending,
    // context->stCodec->stOutputPort->pending, ret, p.revents);

    if (ret < 0) {
      error("Poll returned error code.");
    }

    if (p.revents & POLLERR) {
      error("Poll returned error event.");
    }

    if (0 == ret) {
      // error("Event poll timed out.");
    }

    // if (p.revents & POLLOUT) {
    //   context->bInputReady = MPP_TRUE;
    // }
    // if (p.revents & POLLIN) {
    //  context->bOutputReady = MPP_TRUE;
    //}
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

RETURN al_dec_init(ALBaseContext *ctx, MppVdecPara *para) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!para) {
    error("input para MppVdecPara is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (para->eCodingType == CODING_MPEG2 &&
      para->nProfile == PROFILE_MPEG2_HIGH) {
    error("not support this format or profile, please check!");
    return MPP_NOT_SUPPORTED_FORMAT;
  }

  S32 ret = 0;

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
  context->nOutputMemType = V4L2_MEMORY_DMABUF;
  context->nInputType = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  context->nOutputType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  context->bInputEos = MPP_FALSE;
  context->bInputReady = MPP_FALSE;
  context->bOutputReady = MPP_FALSE;
  context->bIsDestoryed = MPP_FALSE;
  context->nInputQueuedNum = 0;
  context->bIsFlushed = MPP_FALSE;

  context->pVdecPara->nInputQueueLeftNum = 1;

  if (!context->pVdecPara->nInputBufferNum)
    context->pVdecPara->nInputBufferNum = INPUT_BUF_NUM;
  if (!context->pVdecPara->nOutputBufferNum)
    context->pVdecPara->nOutputBufferNum = OUTPUT_BUF_NUM;

  debug(
      "input para check: foramt:0x%x output format:0x%x input buffer num:%d "
      "output buffer num:%d",
      context->nInputFormatFourcc, context->nOutputFormatFourcc,
      context->pVdecPara->nInputBufferNum,
      context->pVdecPara->nOutputBufferNum);

  for (S32 i = 0; i < 64; i++)
    context->pVdecPara->bIsBufferInDecoder[i] = MPP_TRUE;

  context->nInputBufferNum = context->pVdecPara->nInputBufferNum;
  context->nOutputBufferNum = context->pVdecPara->nOutputBufferNum;

  para->eFrameBufferType = MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL;
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
      context->nOutputBufferNum, context->bIsBlockMode);

  setDecoderInterlaced(context, context->bIsInterlaced);
  setDecoderRotation(context, context->nRotation);
  setDecoderDownScale(context, context->nScale);

  stream(context->stCodec);

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
    debug("****************************************** eos");
    context->bInputEos = MPP_TRUE;
  }

  if (PACKET_GetEos(packet)) {
    debug("****************************************** eos 1");
    context->bInputEos = MPP_TRUE;
  }

  if (unlikely(context->nInputQueuedNum <
               getBufNum(getInputPort(context->stCodec)))) {
    Buffer *buf =
        getBuffer(getInputPort(context->stCodec), context->nInputQueuedNum);
    memcpy(getUserPtr(buf, 0), PACKET_GetDataPointer(packet),
           PACKET_GetLength(packet));
    struct v4l2_buffer *b = getV4l2Buffer(buf);
    b->bytesused = PACKET_GetLength(packet);
    setEndOfFrame(buf, MPP_TRUE);

    setTimeStamp(buf, PACKET_GetPts(packet));

    queueBuffer(getInputPort(context->stCodec), buf);
    context->nInputQueuedNum++;
    context->pVdecPara->nInputQueueLeftNum--;
  } else {
    ret = runPoll(context->stCodec, &p);

    if (/*context->bInputReady*/ MPP_OK == ret && p.revents & POLLOUT) {
      handleInputBuffer(getInputPort(context->stCodec), context->bInputEos,
                        sink_data);
      context->bInputReady = MPP_FALSE;
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
  context->bIsFlushed = MPP_FALSE;

  struct pollfd p = {.fd = context->nVideoFd, .events = POLLIN};

  ret = runPoll(context->stCodec, &p);

  if (/*context->bOutputReady*/ MPP_OK == ret && p.revents & POLLIN) {
    // debug(
    //     "============================= ok, a frame is ready, "
    //     "get it!");
    if (!context->stCodec) {
      error("aoaoaoao\n");
    } else {
      ret = handleOutputBuffer(getOutputPort(context->stCodec), MPP_FALSE,
                               src_data);
      // debug("============ ok, a frame is handled, get it! ret = %d", ret);
      context->bOutputReady = MPP_FALSE;
      if (ret == MPP_RESOLUTION_CHANGED) {
        context->pVdecPara->nWidth =
            getBufWidth(getOutputPort(context->stCodec));
        context->pVdecPara->nHeight =
            getBufHeight(getOutputPort(context->stCodec));
        context->pVdecPara->nOutputBufferNum =
            getBufNum(getOutputPort(context->stCodec));
        for (U32 i = 0; i < context->pVdecPara->nOutputBufferNum; i++) {
          context->pVdecPara->nOutputBufferFd[i] =
              getBufFd(getOutputPort(context->stCodec), i);
          context->pVdecPara->bIsBufferInDecoder[i] = MPP_TRUE;
        }
      }
    }
  } else {
    // debug("============ no data, please try again!");
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
    error("buf is NULL, please check!");
  } else {
    clearBytesUsed(buf);

    queueBuffer(getOutputPort(context->stCodec), buf);
    // debug("release output ret = %d", ret);

    context->pVdecPara->bIsBufferInDecoder[buf_idx] = MPP_TRUE;
  }

  return MPP_OK;
}

S32 al_dec_reset(ALBaseContext *ctx) {
  if (!ctx) return MPP_NULL_POINTER;
  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;
  if (!context->bIsFlushed) {
    debug("al_Dec_reset0");
    usleep(100000);
    debug("al_Dec_reset1");
    context->bInputEos = MPP_FALSE;
    debug("al_Dec_reset2");
    handleFlush(context->stCodec, MPP_FALSE);
    context->bIsFlushed = MPP_TRUE;
    debug("al_Dec_reset3");
  } else {
    error("already flushed, please check!");
  }

  return MPP_OK;
}

S32 al_dec_flush(ALBaseContext *ctx) {
  if (!ctx) return MPP_NULL_POINTER;
  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;
  // MppFrame *mppframe = FRAME_Create();
  // S32 ret = -1;
  // S32 counter = 0;

  debug("Flush start ========================================");
  /*
  while (counter < 50) {
    ret = al_dec_request_output_frame(ctx, FRAME_GetBaseData(mppframe));
    if (ret == MPP_OK) {
      debug("Flush one frame");
      al_dec_return_output_frame(ctx, FRAME_GetBaseData(mppframe));
    } else {
      usleep(5000);
    }
    counter++;
  }
  */
  handleFlush(context->stCodec, MPP_FALSE);

  context->nInputQueuedNum = 0;
  context->pVdecPara->nInputQueueLeftNum =
      getBufNum(getInputPort(context->stCodec));

  debug("Flush finish ========================================");

  // FRAME_Destory(mppframe);
  // mppframe = NULL;

  return MPP_OK;
}

void al_dec_destory(ALBaseContext *ctx) {
  if (!ctx) return;
  debug("1111111111110");
  ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;

  context->bIsDestoryed = MPP_TRUE;
  debug("111111111111");
  pthread_join(context->pollthread, NULL);
  debug("111111111112");
  if (context->nVideoFd && context->stCodec) {
    enum v4l2_buf_type input_type =
        getV4l2BufType(getInputPort(context->stCodec));
    enum v4l2_buf_type output_type =
        getV4l2BufType(getOutputPort(context->stCodec));
    mpp_v4l2_stream_off(context->nVideoFd, &input_type);
    mpp_v4l2_stream_off(context->nVideoFd, &output_type);
    debug("111111111113");
    usleep(50000);
    destoryCodec(context->stCodec);
    debug("111111111114");
    close(context->nVideoFd);
  }
  free(context);
  context = NULL;
}
