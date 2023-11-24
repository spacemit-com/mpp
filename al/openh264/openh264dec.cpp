/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:40
 * @LastEditTime: 2023-02-02 10:06:32
 * @Description: video decode plugin for openh264, only can decode H.264 stream
 */

#define ENABLE_DEBUG 1

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "al_interface_dec.h"
#include "log.h"
#include "wels/codec_api.h"
#include "wels/codec_app_def.h"
#include "wels/codec_def.h"
#include "wels/codec_ver.h"

#define MODULE_TAG "openh264dec"

// for test gstreamer single thread handle openh264(no memory copy)
//#define SINGLE_THREAD_TEST

// for test gstreamer dmabuf handle
//#define MULTI_THREAD_DMABUF

PIXEL_FORMAT_MAPPING_DEFINE(SoftOpenh264Dec, EVideoFormatType)
static const ALSoftOpenh264DecPixelFormatMapping
    stALSoftOpenh264DecPixelFormatMapping[] = {
        {PIXEL_FORMAT_I420, videoFormatI420},
        {PIXEL_FORMAT_NV12, videoFormatNV12},
        {PIXEL_FORMAT_YV12, videoFormatYV12},
        {PIXEL_FORMAT_YVYU, videoFormatYVYU},
        {PIXEL_FORMAT_UYVY, videoFormatUYVY},
        {PIXEL_FORMAT_YUYV, videoFormatYUY2},
        {PIXEL_FORMAT_RGBA, videoFormatRGBA},
        {PIXEL_FORMAT_BGRA, videoFormatBGRA},
        {PIXEL_FORMAT_ARGB, videoFormatARGB},
        {PIXEL_FORMAT_ABGR, videoFormatABGR},
};
PIXEL_FORMAT_MAPPING_CONVERT(SoftOpenh264Dec, softopenh264dec, EVideoFormatType)

typedef struct _ALSoftOpenh264DecContext ALSoftOpenh264DecContext;

struct _ALSoftOpenh264DecContext {
  ALDecBaseContext stAlDecBaseContext;
  ISVCDecoder *pSvcDecoder;  // decoder declaration
  U8 *pData[3];              // output: [0~2] for Y,U,V buffer
  SBufferInfo stDstBufInfo;
  SDecodingParam stDecParam;
  S32 DecRetEos;
  S32 RequestApi;
  MppDataQueue *pOutputQueue;
};

#ifdef SINGLE_THREAD_TEST
enum _gFlag {
  NO_FRAME,
  END_OF_DEC,
  REQ_FRAME,
};

pthread_mutex_t gMutex;
pthread_cond_t gCond;
S32 gFlag = NO_FRAME;
S32 iStride[3];

static S32 singlethread_decode(ALBaseContext *ctx, MppData *sink_data) {
  ALSoftOpenh264DecContext *context = (ALSoftOpenh264DecContext *)ctx;
  S32 ret;
  MppPacket *sink_packet = PACKET_GetPacket(sink_data);

  if (gFlag != NO_FRAME) {
    pthread_cond_wait(&gCond, &gMutex);
    pthread_mutex_unlock(&gMutex);
  }
  debug("start dec a frame .....");
  memset(&(context->stDstBufInfo), 0, sizeof(SBufferInfo));

  ret = context->pSvcDecoder->DecodeFrameNoDelay(
      (const U8 *)PACKET_GetDataPointer(sink_packet),
      PACKET_GetLength(sink_packet), context->pData, &(context->stDstBufInfo));
  if (ret != 0) {
    // error handling (RequestIDR or something like that)
    error("decode ret = %d", ret);
  } else {
    debug("------- get a decode frame (%p, %p, %p)", context->pData[0],
          context->pData[1], context->pData[2]);
  }
  if (sink_data->bEos) {
    context->DecRetEos = MPP_TRUE;
  }
  iStride[0] = context->stDstBufInfo.UsrData.sSystemBuffer.iStride[0];
  iStride[1] = context->stDstBufInfo.UsrData.sSystemBuffer.iStride[1];
  debug("info : %d %d %d %d %d %d",
        context->stDstBufInfo.UsrData.sSystemBuffer.iWidth,
        context->stDstBufInfo.UsrData.sSystemBuffer.iHeight,
        context->stDstBufInfo.UsrData.sSystemBuffer.iFormat,
        context->stDstBufInfo.UsrData.sSystemBuffer.iStride[0],
        context->stDstBufInfo.UsrData.sSystemBuffer.iStride[1],
        context->stDstBufInfo.UsrData.sSystemBuffer.iStride[2]);

  gFlag = END_OF_DEC;

  return ret;
}
static S32 singlethread_request(ALBaseContext *ctx, MppData *src_data) {
  ALSoftOpenh264DecContext *context = (ALSoftOpenh264DecContext *)ctx;
  MppFrame *src_frame = FRAME_GetFrame(src_data);
  S32 ret = MPP_OK;

  if (context->DecRetEos == MPP_TRUE) return MPP_CODER_EOS;

  if (gFlag == NO_FRAME || gFlag == REQ_FRAME) return MPP_CODER_NO_DATA;

  FRAME_SetMetaData(src_frame, iStride);
  FRAME_SetDataUsedNum(src_frame, 3);
  FRAME_SetDataPointer(src_frame, 0, (U8 *)context->pData[0]);
  FRAME_SetDataPointer(src_frame, 1, (U8 *)context->pData[1]);
  FRAME_SetDataPointer(src_frame, 2, (U8 *)context->pData[2]);

  debug("request a frame .....(%p, %p, %p)", context->pData[0],
        context->pData[1], context->pData[2]);

  gFlag = REQ_FRAME;

  return ret;
}

static S32 singlethread_return(ALBaseContext *ctx, MppData *src_data) {
  ALSoftOpenh264DecContext *context = (ALSoftOpenh264DecContext *)ctx;
  S32 ret = MPP_OK;

  gFlag = NO_FRAME;

  context->pData[0] = NULL;
  context->pData[1] = NULL;
  context->pData[2] = NULL;
  pthread_cond_signal(&gCond);

  debug("return a frame .....");

  return ret;
}

#else

static S32 multithread_decode(ALBaseContext *ctx, MppData *sink_data) {
  ALSoftOpenh264DecContext *context = (ALSoftOpenh264DecContext *)ctx;
  static U32 num = 1;
  static U32 id = 0;
  MppFrame *pFrame = NULL;
  MppPacket *sink_packet = PACKET_GetPacket(sink_data);
  U8 *pData[3];
  U8 *tmp_pdata0;
  U8 *tmp_pdata1;
  U8 *tmp_pdata2;
  S32 ret;

  memset(&(context->stDstBufInfo), 0, sizeof(SBufferInfo));

  ret = context->pSvcDecoder->DecodeFrameNoDelay(
      (const U8 *)PACKET_GetDataPointer(sink_packet),
      PACKET_GetLength(sink_packet), pData, &(context->stDstBufInfo));
  if (ret != 0) {
    // error handling (RequestIDR or something like that)
    error("decode ret = %d", ret);
  } else {
    // debug("------- get a decode frame");
  }

  if (1 != context->stDstBufInfo.iBufferStatus) {
    if (PACKET_GetEos(sink_packet)) {
      context->DecRetEos = MPP_TRUE;
    }

    debug("decoded but drop it in sinkdata, %d, %d", ret,
          context->stDstBufInfo.iBufferStatus);
    return ret;
  }

  PACKET_SetWidth(sink_packet,
                  context->stDstBufInfo.UsrData.sSystemBuffer.iWidth);
  PACKET_SetHeight(sink_packet,
                   context->stDstBufInfo.UsrData.sSystemBuffer.iHeight);
  PACKET_SetLineStride(sink_packet,
                       context->stDstBufInfo.UsrData.sSystemBuffer.iStride[0]);
  PACKET_SetPixelFormat(sink_packet,
                        get_softopenh264dec_mpp_pixel_format(
                            (EVideoFormatType)(context->stDstBufInfo.UsrData
                                                   .sSystemBuffer.iFormat)));
  PACKET_SetPts(sink_packet, context->stDstBufInfo.uiOutYuvTimeStamp);

  pFrame = FRAME_Create();

#ifdef MULTI_THREAD_DMABUF
  FRAME_SetBufferType(pFrame, MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL);

  FRAME_Alloc(pFrame, PACKET_GetPixelFormat(sink_packet),
              PACKET_GetWidth(sink_packet), PACKET_GetHeight(sink_packet));
  FRAME_SetID(pFrame, id++);

  tmp_pdata0 = (U8 *)FRAME_GetDataPointer(pFrame, 0);
  tmp_pdata1 =
      tmp_pdata0 + PACKET_GetWidth(sink_packet) * PACKET_GetHeight(sink_packet);
  tmp_pdata2 = tmp_pdata1 +
               PACKET_GetWidth(sink_packet) * PACKET_GetHeight(sink_packet) / 4;

#else
  FRAME_Alloc(pFrame, (MppPixelFormat)PACKET_GetPixelFormat(sink_packet),
              PACKET_GetWidth(sink_packet), PACKET_GetHeight(sink_packet));
  FRAME_SetID(pFrame, id++);

  tmp_pdata0 = (U8 *)FRAME_GetDataPointer(pFrame, 0);
  tmp_pdata1 = (U8 *)FRAME_GetDataPointer(pFrame, 1);
  tmp_pdata2 = (U8 *)FRAME_GetDataPointer(pFrame, 2);
#endif

  for (S32 i = 0; i < context->stDstBufInfo.UsrData.sSystemBuffer.iHeight;
       i++) {
    memcpy(tmp_pdata0, pData[0],
           context->stDstBufInfo.UsrData.sSystemBuffer.iWidth);
    tmp_pdata0 += context->stDstBufInfo.UsrData.sSystemBuffer.iWidth;
    pData[0] += context->stDstBufInfo.UsrData.sSystemBuffer.iStride[0];
  }

  for (S32 i = 0; i < context->stDstBufInfo.UsrData.sSystemBuffer.iHeight / 2;
       i++) {
    memcpy(tmp_pdata1, pData[1],
           context->stDstBufInfo.UsrData.sSystemBuffer.iWidth / 2);
    memcpy(tmp_pdata2, pData[2],
           context->stDstBufInfo.UsrData.sSystemBuffer.iWidth / 2);
    tmp_pdata1 += context->stDstBufInfo.UsrData.sSystemBuffer.iWidth / 2;
    tmp_pdata2 += context->stDstBufInfo.UsrData.sSystemBuffer.iWidth / 2;
    pData[1] += context->stDstBufInfo.UsrData.sSystemBuffer.iStride[1];
    pData[2] += context->stDstBufInfo.UsrData.sSystemBuffer.iStride[1];
  }

  debug("finish decode a frame, info : %d %d %d %d %d(%d)",
        context->stDstBufInfo.UsrData.sSystemBuffer.iWidth,
        context->stDstBufInfo.UsrData.sSystemBuffer.iHeight,
        context->stDstBufInfo.UsrData.sSystemBuffer.iFormat,
        context->stDstBufInfo.UsrData.sSystemBuffer.iStride[0],
        context->stDstBufInfo.UsrData.sSystemBuffer.iStride[1],
        FRAME_GetDataUsedNum(pFrame));

  MppDataQueueNode *node = DATAQUEUE_Node_Create();
  DATAQUEUE_SetData(node, FRAME_GetBaseData(pFrame));

  ret = DATAQUEUE_Push(context->pOutputQueue, node);
  if (PACKET_GetEos(sink_packet)) {
    context->DecRetEos = MPP_TRUE;
  }

  return ret;
}

static S32 multithread_return(ALBaseContext *ctx, MppData *src_data) {
  ALSoftOpenh264DecContext *context = (ALSoftOpenh264DecContext *)ctx;
  struct DNode *del_node;
  struct DNode *pre_node;
  S32 ret = MPP_OK;

  if (!src_data) return -1;

  MppFrame *src_frame = FRAME_GetFrame(src_data);

  debug("-------------- return a frame, %d", FRAME_GetDataUsedNum(src_frame));

  if (context->RequestApi == 2) {
    FRAME_Free(src_frame);
    FRAME_Destory(src_frame);
  } else {
    FRAME_Free(src_frame);
  }

  return ret;
}

#endif

ALBaseContext *al_dec_create() {
  ALSoftOpenh264DecContext *context =
      (ALSoftOpenh264DecContext *)malloc(sizeof(ALSoftOpenh264DecContext));
  if (!context) {
    error("can not malloc ALSoftOpenh264DecContext, please check!");
    return NULL;
  }
  memset(context, 0, sizeof(ALSoftOpenh264DecContext));

  context->pSvcDecoder = NULL;
  context->DecRetEos = MPP_FALSE;

  memset(&(context->stDstBufInfo), 0, sizeof(SBufferInfo));
  memset(&(context->stDecParam), 0, sizeof(SDecodingParam));

  return &(context->stAlDecBaseContext.stAlBaseContext);
}

RETURN al_dec_init(ALBaseContext *ctx, MppVdecPara *para) {
  if (!ctx) {
    error("ctx is NULL, please check it !");
    return MPP_NULL_POINTER;
  }

  ALSoftOpenh264DecContext *context = (ALSoftOpenh264DecContext *)ctx;

  if (context->pSvcDecoder != NULL) {
    context->pSvcDecoder->Uninitialize();
    WelsDestroyDecoder(context->pSvcDecoder);
    context->pSvcDecoder = NULL;
  }
  para->eDataTransmissinMode = MPP_INPUT_SYNC_OUTPUT_ASYNC;

  WelsCreateDecoder(&(context->pSvcDecoder));

  context->stDecParam.uiTargetDqLayer = 255;
  context->stDecParam.eEcActiveIdc = ERROR_CON_FRAME_COPY;
#if OPENH264_MAJOR == 1 && OPENH264_MINOR < 6
  context->stDecParam.eOutputColorFormat = videoFormatI420;
  debug("config eOutputColorFormat i420 ");
#endif
  context->stDecParam.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
  context->pSvcDecoder->Initialize(&(context->stDecParam));

#ifdef SINGLE_THREAD_TEST
  context->pData[0] = NULL;
  context->pData[1] = NULL;
  context->pData[2] = NULL;
  pthread_mutex_init(&gMutex, NULL);
  pthread_cond_init(&gCond, NULL);

#else
  context->pOutputQueue = DATAQUEUE_Init(MPP_TRUE, MPP_TRUE);

#endif
  debug("finish ----------------- init (%d, %d, %d)", para->eOutputPixelFormat,
        para->nWidth, para->nHeight);

  return MPP_OK;
}

S32 al_dec_decode(ALBaseContext *ctx, MppData *sink_data) {
  if (!ctx) {
    error("input para ctx is NULL, please check it !");
    return MPP_NULL_POINTER;
  }

  if (!sink_data) {
    error("input para sink_data is NULL, please check it !");
    return MPP_NULL_POINTER;
  }

  ALSoftOpenh264DecContext *context = (ALSoftOpenh264DecContext *)ctx;
  MppPacket *sink_packet = PACKET_GetPacket(sink_data);
  S32 ret = 0;

  if (PACKET_GetEos(sink_packet)) {
    debug("get EOS!");
    int end_of_stream = 1;
    context->pSvcDecoder->SetOption(DECODER_OPTION_END_OF_STREAM,
                                    &end_of_stream);
    debug("set EOS to openh264 decoder!");
  }

  if (context->DecRetEos) {
    debug("no need to decode again");
    return ret;
  }

#ifdef SINGLE_THREAD_TEST
  ret = singlethread_decode(ctx, sink_data);

#else
  ret = multithread_decode(ctx, sink_data);

#endif
  return ret;
}
S32 al_dec_request_output_frame_2(ALBaseContext *ctx, MppData **src_data) {
  ALSoftOpenh264DecContext *context = (ALSoftOpenh264DecContext *)ctx;
  S32 ret = MPP_OK;
  static int flag = 0;
  MppDataQueueNode *node;
  MppFrame *src_frame = FRAME_GetFrame(*src_data);

  if (context->DecRetEos && DATAQUEUE_IsEmpty(context->pOutputQueue) == 1) {
    debug("ret dec eos");

    return MPP_CODER_EOS;
  }

  if (flag == 0) {
    // gstreamer first handle frame, but buffer pool still not active, wait some
    // time
    if (DATAQUEUE_GetCurrentSize(context->pOutputQueue) > 3) flag = 1;
    return MPP_CODER_NO_DATA;
  }

  node = DATAQUEUE_Pop(context->pOutputQueue);
  if (!node) {
    return context->DecRetEos ? MPP_CODER_EOS : MPP_CODER_NO_DATA;
  }
  *src_data = DATAQUEUE_GetData(node);

  DATAQUEUE_Node_Destory(node);

  debug("----- frame %d request, left: %d ", FRAME_GetDataUsedNum(src_frame),
        DATAQUEUE_GetCurrentSize(context->pOutputQueue));

  context->RequestApi = 2;
  return ret;
}

S32 al_dec_request_output_frame(ALBaseContext *ctx, MppData *src_data) {
  ALSoftOpenh264DecContext *context = (ALSoftOpenh264DecContext *)ctx;
  MppDataQueueNode *node;
  S32 ret = MPP_OK;
  static int flag = 0;
  MppFrame *src_frame = FRAME_GetFrame(src_data);

#ifdef SINGLE_THREAD_TEST
  ret = singlethread_request(ctx, src_data);

#else
  if (context->DecRetEos && DATAQUEUE_IsEmpty(context->pOutputQueue) == 1) {
    debug("ret dec eos");

    return MPP_CODER_EOS;
  }

  if (flag == 0) {
    if (DATAQUEUE_GetCurrentSize(context->pOutputQueue) > 3) flag = 1;
    return MPP_CODER_NO_DATA;
  }

  node = DATAQUEUE_Pop(context->pOutputQueue);
  if (!node) {
    return context->DecRetEos ? MPP_CODER_EOS : MPP_CODER_NO_DATA;
  }

  memcpy(src_data, DATAQUEUE_GetData(node), FRAME_GetStructSize());
  FRAME_Destory(FRAME_GetFrame(DATAQUEUE_GetData(node)));

  DATAQUEUE_Node_Destory(node);

  debug("----- frame %d request, left: %d ", FRAME_GetDataUsedNum(src_frame),
        DATAQUEUE_GetCurrentSize(context->pOutputQueue));

#endif

  context->RequestApi = 0;

  return ret;
}

S32 al_dec_return_output_frame(ALBaseContext *ctx, MppData *src_data) {
  ALSoftOpenh264DecContext *context = (ALSoftOpenh264DecContext *)ctx;
  S32 ret = MPP_OK;

#ifdef SINGLE_THREAD_TEST
  ret = singlethread_return(ctx, src_data);

#else
  ret = multithread_return(ctx, src_data);
#endif

  return ret;
}

void al_dec_destory(ALBaseContext *ctx) {
  if (!ctx) {
    error("No need to destory, return !");
    return;
  }

  ALSoftOpenh264DecContext *context = (ALSoftOpenh264DecContext *)ctx;

  if (context->pSvcDecoder) {
    context->pSvcDecoder->Uninitialize();
    WelsDestroyDecoder(context->pSvcDecoder);
    context->pSvcDecoder = NULL;
  }

  free(context);
  context = NULL;
}
