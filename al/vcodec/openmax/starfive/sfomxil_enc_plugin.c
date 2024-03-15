/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:36
 * @LastEditTime: 2023-11-13 15:55:59
 * @Description: video encode plugin for starfive omxIL layer
 */

#define ENABLE_DEBUG 1

#include <OMX_Component.h>
#include <OMX_Core.h>
#include <OMX_IndexExt.h>
#include <OMX_Video.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "al_interface_enc.h"
#include "log.h"
#include "sfomxil_find_enc_library.h"

#define MODULE_TAG "sfomxil_enc"

#define OMX_INIT_STRUCTURE(a)       \
  memset(&(a), 0, sizeof(a));       \
  (a).nSize = sizeof(a);            \
  (a).nVersion.nVersion = 1;        \
  (a).nVersion.s.nVersionMajor = 1; \
  (a).nVersion.s.nVersionMinor = 1; \
  (a).nVersion.s.nRevision = 1;     \
  (a).nVersion.s.nStep = 1

OMX_ERRORTYPE (*omx_enc_init)(void);
OMX_ERRORTYPE (*omx_enc_deinit)(void);
OMX_ERRORTYPE(*omx_enc_gethandle)
(OMX_HANDLETYPE *pHandle, OMX_STRING cComponentName, OMX_PTR pAppData,
 OMX_CALLBACKTYPE *pCallBacks);
OMX_ERRORTYPE (*omx_enc_freehandle)(OMX_HANDLETYPE hComponent);

typedef struct Message {
  long msg_type;
  OMX_S32 msg_flag;
  OMX_BUFFERHEADERTYPE *pBuffer;
} Message;

typedef struct _ALSfOmxilEncContext ALSfOmxilEncContext;

struct _ALSfOmxilEncContext {
  ALEncBaseContext stAlEncBaseContext;
  OMX_HANDLETYPE hComponentEncoder;
  OMX_CALLBACKTYPE callbacks;
  U8 sOutputFilePath[64];
  U8 sInputFilePath[64];
  U8 sInputFormat[64];
  U8 sOutputFormat[64];
  OMX_U32 nFrameBufferSize;
  OMX_U32 nBitrate;
  OMX_U32 nFrameRate;
  OMX_U32 nNumPFrame;
  volatile OMX_STATETYPE comState;
  OMX_PARAM_PORTDEFINITIONTYPE pInputPortDefinition;
  OMX_PARAM_PORTDEFINITIONTYPE pOutputPortDefinition;
  OMX_BUFFERHEADERTYPE *pInputBufferArray[64];
  OMX_BUFFERHEADERTYPE *pOutputBufferArray[64];
  MppDataQueue *pInputQueue;
  MppDataQueue *pOutputQueue;
  S32 msgid;
  S32 EncRetEos;
  pthread_t workthread;
  // load openmax il so
  U8 so_path[256];
  void *load_so;
};

PIXEL_FORMAT_MAPPING_DEFINE(SoftSfomxEnc, OMX_COLOR_FORMATTYPE)
static const ALSoftSfomxEncPixelFormatMapping
    stALSoftSfomxEncPixelFormatMapping[] = {
        {PIXEL_FORMAT_I420, OMX_COLOR_FormatYUV420Planar},
        {PIXEL_FORMAT_NV12, OMX_COLOR_FormatYUV420SemiPlanar},
        {PIXEL_FORMAT_NV21, OMX_COLOR_FormatYVU420SemiPlanar},
};
PIXEL_FORMAT_MAPPING_CONVERT(SoftSfomxEnc, softsfomxenc, OMX_COLOR_FORMATTYPE)

static OMX_BOOL inputEndFlag = OMX_FALSE;
// static OMX_S32 FillInputBuffer(ALSfOmxilEncContext *encodeTestContext,
// OMX_BUFFERHEADERTYPE *pInputBuffer);
static OMX_BOOL disableEVnt;
static OMX_BOOL justQuit = OMX_FALSE;
static S32 enc_eos = MPP_FALSE;

static OMX_ERRORTYPE event_handler(OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
                                   OMX_EVENTTYPE eEvent, OMX_U32 nData1,
                                   OMX_U32 nData2, OMX_PTR pEventData) {
  ALSfOmxilEncContext *pEncodeTestContext = (ALSfOmxilEncContext *)pAppData;

  switch (eEvent) {
    case OMX_EventPortSettingsChanged: {
      OMX_PARAM_PORTDEFINITIONTYPE pOutputPortDefinition;
      OMX_INIT_STRUCTURE(pOutputPortDefinition);
      pOutputPortDefinition.nPortIndex = 1;
      OMX_GetParameter(pEncodeTestContext->hComponentEncoder,
                       OMX_IndexParamPortDefinition, &pOutputPortDefinition);
      OMX_U32 nOutputBufferSize = pOutputPortDefinition.nBufferSize;
      OMX_U32 nOutputBufferCount = pOutputPortDefinition.nBufferCountMin;

      debug("enable output port and alloc buffer");
      OMX_SendCommand(pEncodeTestContext->hComponentEncoder,
                      OMX_CommandPortEnable, 1, NULL);

      for (S32 i = 0; i < nOutputBufferCount; i++) {
        OMX_BUFFERHEADERTYPE *pBuffer = NULL;
        OMX_AllocateBuffer(hComponent, &pBuffer, 1, NULL, nOutputBufferSize);
        pEncodeTestContext->pOutputBufferArray[i] = pBuffer;
        OMX_FillThisBuffer(hComponent, pBuffer);
      }
    } break;
    case OMX_EventBufferFlag: {
      Message data;
      data.msg_type = 1;
      data.msg_flag = -1;

      if (-1 == msgsnd(pEncodeTestContext->msgid, (void *)&data,
                       sizeof(data) - sizeof(data.msg_type), 0)) {
        error("msgsnd failed");
      }
    } break;
    case OMX_EventCmdComplete: {
      switch ((OMX_COMMANDTYPE)(nData1)) {
        case OMX_CommandStateSet: {
          pEncodeTestContext->comState = (OMX_STATETYPE)(nData2);
        }
        case OMX_CommandPortDisable: {
          if (1 == nData2) disableEVnt = OMX_TRUE;
        } break;
        default:
          break;
      }
    } break;
    case OMX_EventError: {
      error("receive err event %d %d", nData1, nData2);
      justQuit = OMX_TRUE;
    } break;
    default:
      break;
  }
  return OMX_ErrorNone;
}

static OMX_ERRORTYPE fill_output_buffer_done_handler(
    OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
    OMX_BUFFERHEADERTYPE *pBuffer) {
  ALSfOmxilEncContext *pEncodeTestContext = (ALSfOmxilEncContext *)pAppData;

  Message data;
  data.msg_type = 1;
  if (pBuffer->nFlags & OMX_BUFFERFLAG_EOS) {
    data.msg_flag = -1;
  } else {
    data.msg_flag = 1;
    data.pBuffer = pBuffer;
  }
  if (-1 == msgsnd(pEncodeTestContext->msgid, (void *)&data,
                   sizeof(data) - sizeof(data.msg_type), 0)) {
    error("msgsnd failed");
  }

  return OMX_ErrorNone;
}

static OMX_ERRORTYPE empty_buffer_done_handler(OMX_HANDLETYPE hComponent,
                                               OMX_PTR pAppData,
                                               OMX_BUFFERHEADERTYPE *pBuffer) {
  ALSfOmxilEncContext *pEncodeTestContext = (ALSfOmxilEncContext *)pAppData;
  Message data;
  data.msg_type = 1;
  data.msg_flag = 0;
  data.pBuffer = pBuffer;
  if (-1 == msgsnd(pEncodeTestContext->msgid, (void *)&data,
                   sizeof(data) - sizeof(data.msg_type), 0)) {
    error("msgsnd failed");
  }
  return OMX_ErrorNone;
}

static OMX_S32 FillInputBuffer(ALSfOmxilEncContext *encodeTestContext,
                               MppData *sink_data,
                               OMX_BUFFERHEADERTYPE *pInputBuffer) {
  ALSfOmxilEncContext *context = (ALSfOmxilEncContext *)encodeTestContext;
  S32 pnum = 0;
  S32 width, height, i, size[3], offset, length;
  MppFrame *frame = FRAME_GetFrame(sink_data);

  // OMX_S32 error;
  if (!sink_data) {
    pInputBuffer->nFlags = 0x10;
    pInputBuffer->nFilledLen = 0;
    return pInputBuffer->nFilledLen;
  }

  if (FRAME_GetEos(frame)) {
    pInputBuffer->nFlags = 0x01;
    pInputBuffer->nFilledLen = 0;
    return pInputBuffer->nFilledLen;
  }
  width = context->pInputPortDefinition.format.video.nFrameWidth;
  height = context->pInputPortDefinition.format.video.nFrameHeight;

  if (context->pInputPortDefinition.format.video.eColorFormat ==
      OMX_COLOR_FormatYUV420Planar) {
    pnum = 3;
    size[0] = width * height;
    size[1] = width * height / 4;
    size[2] = width * height / 4;
  } else {
    pnum = 2;
    size[0] = width * height;
    size[1] = width * height / 2;
  }

  for (i = 0, offset = 0, length = 0; i < pnum; i++) {
    memcpy(pInputBuffer->pBuffer + offset, FRAME_GetDataPointer(frame, i),
           size[i]);
    length += size[i];
    offset += size[i];
  }
  pInputBuffer->nFlags = 0x10;
  pInputBuffer->nFilledLen = length;
  FRAME_Free(frame);
  FRAME_Destory(frame);

  // input node free

  return pInputBuffer->nFilledLen;
}

ALBaseContext *al_enc_create() {
  ALSfOmxilEncContext *context =
      (ALSfOmxilEncContext *)malloc(sizeof(ALSfOmxilEncContext));
  if (!context) return NULL;

  memset(context, 0, sizeof(ALSfOmxilEncContext));

  return &(context->stAlEncBaseContext.stAlBaseContext);
}

void *do_encode(void *private_data) {
  debug("------------------new thread-------------------");
  ALSfOmxilEncContext *context = (ALSfOmxilEncContext *)private_data;
  S32 ret = 0;

  Message data;

  while (OMX_TRUE) {
    static S32 i = 0;

    if (i < 5) {
      MppDataQueueNode *node = DATAQUEUE_Pop(context->pInputQueue);
      if (node) {
        MppData *sink_data = DATAQUEUE_GetData(node);
        FillInputBuffer(context, sink_data, context->pInputBufferArray[i]);
        OMX_EmptyThisBuffer(context->hComponentEncoder,
                            context->pInputBufferArray[i]);
        i++;
      } else {
      }
      continue;
    } else if (5 == i) {
      debug("start process");
      OMX_SendCommand(context->hComponentEncoder, OMX_CommandStateSet,
                      OMX_StateExecuting, NULL);
      i++;
      debug("start process finish");
      // usleep(100000);
      continue;
    }

    if (-1 == msgrcv(context->msgid, (void *)&data, BUFSIZ, 0, 0)) {
      error("msgrcv failed with errno: %d .................", errno);
      continue;
    }

    switch (data.msg_flag) {
      case 0: {
        OMX_BUFFERHEADERTYPE *pBuffer = data.pBuffer;
        // debug("......input");
        MppDataQueueNode *node = NULL;
        // while(!node)
        //{
        node = DATAQUEUE_Pop(context->pInputQueue);
        //   usleep(10000);
        // }

        if (node) {
          MppData *sink_data = DATAQUEUE_GetData(node);
          FillInputBuffer(context, sink_data, pBuffer);
          OMX_EmptyThisBuffer(context->hComponentEncoder, pBuffer);
          // debug("......input finish, need_d: %d", sink_data->bEos);
        } else {
          Message data;
          data.msg_type = 1;
          data.msg_flag = 0;
          data.pBuffer = pBuffer;
          if (-1 == msgsnd(context->msgid, (void *)&data,
                           sizeof(data) - sizeof(data.msg_type), 0)) {
            error("msgsnd failed .....");
          }
        }

      } break;
      case 1: {
        OMX_BUFFERHEADERTYPE *pBuffer = data.pBuffer;
        MppPacket *packet = PACKET_Create();

        PACKET_SetMetaData(packet, (void *)pBuffer);
        PACKET_SetDataPointer(packet, pBuffer->pBuffer);
        PACKET_SetLength(packet, pBuffer->nFilledLen);

        MppDataQueueNode *node =
            (MppDataQueueNode *)malloc(DATAQUEUE_GetNodeStructSize());
        DATAQUEUE_SetData(node, PACKET_GetBaseData(packet));

        ret = DATAQUEUE_Push(context->pOutputQueue, node);

        if ((pBuffer->nFlags) & (OMX_BUFFERFLAG_EOS == OMX_BUFFERFLAG_EOS)) {
          error("decoder commit EOS 111!");
          goto finish;
        } else if (!ret) {
          // debug("......output");
          // OMX_FillThisBuffer(context->hComponentEncoder, pBuffer);
          // debug("......output finish");
        } else {
          Message data;
          data.msg_type = 1;
          data.msg_flag = 1;
          data.pBuffer = pBuffer;
          if (-1 == msgsnd(context->msgid, (void *)&data,
                           sizeof(data) - sizeof(data.msg_type), 0)) {
            error("msgsnd failed....");
          }
        }
      } break;
      case -1: {
        error("decoder commit EOS 222!");
        goto finish;
      } break;
      default:
        error("data.msg_flag:%d out of switch", data.msg_flag);
        break;
    }
    // debug("while finish ------------------------------");
  }

finish:
  context->EncRetEos = MPP_TRUE;

  debug("finish encode!");
}

RETURN al_enc_init(ALBaseContext *ctx, MppVencPara *para) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  if (!para) {
    error("input para MppVencPara is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALSfOmxilEncContext *context = (ALSfOmxilEncContext *)ctx;

  OMX_S32 msgid = -1;
  msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
  if (msgid < 0) {
    error("get ipc_id error");
    return -1;
  }
  context->msgid = msgid;

  find_enc_sfomx(context->so_path);
  context->load_so = dlopen(context->so_path, RTLD_LAZY | RTLD_LOCAL);
  omx_enc_init = (OMX_ERRORTYPE(*)(void))dlsym(context->load_so, "OMX_Init");
  omx_enc_deinit =
      (OMX_ERRORTYPE(*)(void))dlsym(context->load_so, "OMX_Deinit");
  omx_enc_gethandle =
      (OMX_ERRORTYPE(*)(OMX_HANDLETYPE * pHandle, OMX_STRING cComponentName,
                        OMX_PTR pAppData, OMX_CALLBACKTYPE * pCallBacks))
          dlsym(context->load_so, "OMX_GetHandle");
  omx_enc_freehandle = (OMX_ERRORTYPE(*)(OMX_HANDLETYPE hComponent))dlsym(
      context->load_so, "OMX_FreeHandle");

  /*omx init*/
  S32 ret = OMX_ErrorNone;
  // signal(SIGINT, signal_handle);
  debug("init omx");
  ret = omx_enc_init();
  if (ret != OMX_ErrorNone) {
    error("run OMX_Init failed. ret is %d ", ret);
    return -1;
  }

  context->callbacks.EventHandler = event_handler;
  context->callbacks.FillBufferDone = fill_output_buffer_done_handler;
  context->callbacks.EmptyBufferDone = empty_buffer_done_handler;

  omx_enc_gethandle(&context->hComponentEncoder, "OMX.sf.video_encoder.hevc",
                    context, &context->callbacks);
  if (!context->hComponentEncoder) {
    error("could not get handle");
    omx_enc_deinit();
    return -1;
  }
  OMX_INIT_STRUCTURE(context->pInputPortDefinition);
  context->pInputPortDefinition.nPortIndex = 0;
  OMX_GetParameter(context->hComponentEncoder, OMX_IndexParamPortDefinition,
                   &context->pInputPortDefinition);
  context->pInputPortDefinition.format.video.nFrameWidth = para->nWidth;
  context->pInputPortDefinition.format.video.nFrameHeight = para->nHeight;

  if (para->PixelFormat == PIXEL_FORMAT_I420) {
    context->pInputPortDefinition.format.video.eColorFormat =
        OMX_COLOR_FormatYUV420Planar;
  } else if (para->PixelFormat == PIXEL_FORMAT_NV12) {
    context->pInputPortDefinition.format.video.eColorFormat =
        OMX_COLOR_FormatYUV420SemiPlanar;
  } else if (para->PixelFormat == PIXEL_FORMAT_NV21) {
    context->pInputPortDefinition.format.video.eColorFormat =
        OMX_COLOR_FormatYVU420SemiPlanar;
  } else {
    error("unsupported color format: %d", para->PixelFormat);
    return -1;
  }
  debug("------------------ %d %d, %d, %x", para->PixelFormat,
        OMX_COLOR_FormatYVU420SemiPlanar, OMX_COLOR_FormatYUV420SemiPlanar,
        context->pInputPortDefinition.format.video.eColorFormat);

  /*
  if(encodeTestContext->nFrameRate){
      pInputPortDefinition.format.video.xFramerate =
  encodeTestContext->nFrameRate;
  }
  */
  OMX_SetParameter(context->hComponentEncoder, OMX_IndexParamPortDefinition,
                   &context->pInputPortDefinition);
  OMX_GetParameter(context->hComponentEncoder, OMX_IndexParamPortDefinition,
                   &context->pInputPortDefinition);

  OMX_INIT_STRUCTURE(context->pOutputPortDefinition);
  context->pOutputPortDefinition.nPortIndex = 1;
  OMX_GetParameter(context->hComponentEncoder, OMX_IndexParamPortDefinition,
                   &context->pOutputPortDefinition);

  context->pOutputPortDefinition.format.video.nFrameWidth = para->nWidth;
  context->pOutputPortDefinition.format.video.nFrameHeight = para->nHeight;
  // pcontext->OutputPortDefinition.format.video.nBitrate =
  // encodeTestContext->nBitrate;
  OMX_SetParameter(context->hComponentEncoder, OMX_IndexParamPortDefinition,
                   &context->pOutputPortDefinition);
  /*
      if(encodeTestContext->nNumPFrame){
          if (strstr(encodeTestContext->sOutputFormat, "h264") != NULL)
          {
              OMX_VIDEO_PARAM_AVCTYPE avcType;
              OMX_INIT_STRUCTURE(avcType);
              avcType.nPortIndex = 1;
              OMX_GetParameter(hComponentEncoder, OMX_IndexParamVideoAvc,
     &avcType); avcType.nPFrames = encodeTestContext->nNumPFrame;
              OMX_SetParameter(hComponentEncoder, OMX_IndexParamVideoAvc,
     &avcType);
          }
          else if (strstr(encodeTestContext->sOutputFormat, "h265") != NULL)
          {
              OMX_VIDEO_PARAM_HEVCTYPE hevcType;
              OMX_INIT_STRUCTURE(hevcType);
              hevcType.nPortIndex = 1;
              OMX_GetParameter(hComponentEncoder, OMX_IndexParamVideoHevc,
     &hevcType); hevcType.nKeyFrameInterval = encodeTestContext->nNumPFrame;
              OMX_SetParameter(hComponentEncoder, OMX_IndexParamVideoHevc,
     &hevcType);
          }
      }
  */

  /*Alloc input buffer*/
  OMX_U32 nInputBufferSize = context->pInputPortDefinition.nBufferSize;
  OMX_U32 nInputBufferCount = context->pInputPortDefinition.nBufferCountActual;

  disableEVnt = OMX_FALSE;
  OMX_SendCommand(context->hComponentEncoder, OMX_CommandPortDisable, 1, NULL);
  debug("wait for output port disable");
  // while (!disableEVnt && !justQuit);
  // if (justQuit)
  // goto end;
  debug("output port disabled");

  OMX_SendCommand(context->hComponentEncoder, OMX_CommandStateSet,
                  OMX_StateIdle, NULL);

  for (S32 i = 0; i < nInputBufferCount; i++) {
    OMX_BUFFERHEADERTYPE *pBuffer = NULL;
    OMX_AllocateBuffer(context->hComponentEncoder, &pBuffer, 0, NULL,
                       nInputBufferSize);
    context->pInputBufferArray[i] = pBuffer;
  }

  debug("wait for Component idle");
  while (context->comState != OMX_StateIdle && !justQuit)
    ;
  if (justQuit) return -1;
  debug("Component in idle");
  /*
      for (S32 i = 0; i < nInputBufferCount; i++)
      {
          FillInputBuffer(context, context->pInputBufferArray[i]);
          if (context->pInputBufferArray[i]->nFilledLen ||
     context->pInputBufferArray[i]->nFlags)
              OMX_EmptyThisBuffer(context->hComponentEncoder,
     context->pInputBufferArray[i]);
      }

      debug("start process");
      OMX_SendCommand(context->hComponentEncoder, OMX_CommandStateSet,
     OMX_StateExecuting, NULL);
  */
  context->pInputQueue = DATAQUEUE_Init(MPP_TRUE, MPP_FALSE);

  context->pOutputQueue = DATAQUEUE_Init(MPP_TRUE, MPP_FALSE);
  context->EncRetEos = MPP_FALSE;

  ret = pthread_create(&context->workthread, NULL, do_encode, (void *)context);

  debug("init finish");

  return MPP_OK;
exit:
  error("sfomxenc init fail");
  free(context);
  return MPP_INIT_FAILED;
}

S32 al_enc_set_para(ALBaseContext *ctx, MppVencPara *para) { return 0; }

S32 al_enc_encode(ALBaseContext *ctx, MppData *sink_data) {
  if (!ctx) return MPP_NULL_POINTER;

  ALSfOmxilEncContext *context = (ALSfOmxilEncContext *)ctx;
  S32 ret = 0;
  S32 width, height, i, size[3];
  MppPixelFormat ePixelFormat;

  MppDataQueueNode *node =
      (MppDataQueueNode *)malloc(DATAQUEUE_GetNodeStructSize());
  MppFrame *sink_frame = FRAME_GetFrame(sink_data);
  MppFrame *frame = FRAME_Create();

  if (FRAME_GetEos(sink_frame)) {
    FRAME_SetEos(frame, 1);
    DATAQUEUE_SetData(node, FRAME_GetBaseData(frame));

    ret = DATAQUEUE_Push(context->pInputQueue, node);

    return ret;
  }

  width = context->pInputPortDefinition.format.video.nFrameWidth;
  height = context->pInputPortDefinition.format.video.nFrameHeight;

  if (context->pInputPortDefinition.format.video.eColorFormat ==
      OMX_COLOR_FormatYUV420Planar) {
    size[0] = width * height;
    size[1] = width * height / 4;
    size[2] = width * height / 4;
  } else {
    size[0] = width * height;
    size[1] = width * height / 2;
  }
  ePixelFormat = get_softsfomxenc_mpp_pixel_format(
      (OMX_COLOR_FORMATTYPE)(context->pInputPortDefinition.format.video
                                 .eColorFormat));

  // debug("enc %d, %d %d, %d, %x", ePixelFormat,
  // width, height, size[1],
  // (OMX_COLOR_FORMATTYPE)(context->pInputPortDefinition.format.video.eColorFormat));
  ret = FRAME_Alloc(frame, ePixelFormat, width, height);

  for (i = 0; i < FRAME_GetDataUsedNum(frame); i++) {
    memcpy(FRAME_GetDataPointer(frame, i), FRAME_GetDataPointer(sink_frame, i),
           size[i]);
#if 0
        //SF_OMX_BUF_INFO *pBufInfo = pOMXBuffer->pOutputPortPrivate;
        //LOG(SF_LOG_INFO, "%p %d %p", pOMXBuffer->pBuffer, pOMXBuffer->nFilledLen, pBufInfo->remap_vaddr);
        FILE *fbbb;

        fbbb = fopen("/tmp/out.yuv", "ab+");
        fwrite(FRAME_GetDataPointer(sink_frame, i), 1, size[i], fbbb);
        fclose(fbbb);
#endif
  }

  DATAQUEUE_SetData(node, FRAME_GetBaseData(frame));
  //    debug("ret = %d, sink_data bEos:%d, %d, %d, %d", ret,
  //    sink_data->bEos, FRAME_GetDataUsedNum(frame), width, height);

  ret = DATAQUEUE_Push(context->pInputQueue, node);

  return ret;
}

S32 al_enc_request_output_stream(ALBaseContext *ctx, MppData *src_data) {
  static U32 count = 0;

  if (!ctx) return MPP_NULL_POINTER;

  ALSfOmxilEncContext *context = (ALSfOmxilEncContext *)ctx;
  S32 ret = 0;
  static S32 length = 0;

  if (context->EncRetEos && DATAQUEUE_IsEmpty(context->pOutputQueue) == 1) {
    return MPP_CODER_EOS;
  }

  MppDataQueueNode *node = DATAQUEUE_Pop(context->pOutputQueue);

  if (!node) return MPP_CODER_NO_DATA;

  // src_data = node->data;
  memcpy(src_data, DATAQUEUE_GetData(node), PACKET_GetStructSize());
  length += PACKET_GetLength(PACKET_GetPacket(src_data));
  debug("request output, %d, %d, %d",
        PACKET_GetLength(PACKET_GetPacket(src_data)), length, ++count);

  return MPP_OK;
}

S32 al_enc_return_output_stream(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) return MPP_NULL_POINTER;

  ALSfOmxilEncContext *context = (ALSfOmxilEncContext *)ctx;
  S32 ret = 0;

  if (!src_data) return -1;

  OMX_FillThisBuffer(
      context->hComponentEncoder,
      (OMX_BUFFERHEADERTYPE *)(PACKET_GetMetaData(PACKET_GetPacket(src_data))));

  // output node free

  //    debug("release output");
  return MPP_OK;
}

void al_enc_destory(ALBaseContext *ctx) {
  ALSfOmxilEncContext *context = (ALSfOmxilEncContext *)ctx;
  S32 ret = 0;

  if (OMX_StateExecuting == context->comState) {
    OMX_SendCommand(context->hComponentEncoder, OMX_CommandStateSet,
                    OMX_StateIdle, NULL);
    debug("wait for Component idle");
    while (context->comState != OMX_StateIdle)
      ;
    debug("Component in idle");
  }
  omx_enc_freehandle(context->hComponentEncoder);
  omx_enc_deinit();
}
