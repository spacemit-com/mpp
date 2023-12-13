/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:36
 * @LastEditTime: 2023-12-13 16:20:10
 * @Description: video decode plugin for starfive omxIL layer
 */

#define ENABLE_DEBUG 1
//#define OLD_MODE
//#define ENABLE_FILE_SAVE_DEBUG

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

#include "al_interface_dec.h"
#include "log.h"
#include "resolution_utils.h"
#include "sfomxil_find_dec_library.h"

#define MODULE_TAG "sfomxil_dec"

#define OMX_INIT_STRUCTURE(a)       \
  memset(&(a), 0, sizeof(a));       \
  (a).nSize = sizeof(a);            \
  (a).nVersion.nVersion = 1;        \
  (a).nVersion.s.nVersionMajor = 1; \
  (a).nVersion.s.nVersionMinor = 1; \
  (a).nVersion.s.nRevision = 1;     \
  (a).nVersion.s.nStep = 1

#define MORE_OUTPUT_BUFFER_NEEDED 4

//#define ENABLE_FILE_SAVE_DEBUG
//#define USE_CIRCULAR_BUFFER

#ifdef ENABLE_FILE_SAVE_DEBUG
U8 *pOutputFileName = "/tmp/output.yuv";
U8 *pInputFileName = "/tmp/input.264";
#endif

OMX_ERRORTYPE (*omx_init)(void);
OMX_ERRORTYPE (*omx_deinit)(void);
OMX_ERRORTYPE(*omx_gethandle)
(OMX_HANDLETYPE *pHandle, OMX_STRING cComponentName, OMX_PTR pAppData,
 OMX_CALLBACKTYPE *pCallBacks);
OMX_ERRORTYPE (*omx_freehandle)(OMX_HANDLETYPE hComponent);

typedef struct _Message {
  LONG msg_type;
  OMX_S32 msg_flag;
  OMX_BUFFERHEADERTYPE *pBuffer;
} Message;

typedef enum _MessageFlag {
  EMPTY_BUFFER_DONE = 0,
  FILL_BUFFER_DONE,
  EOS,
} MessageFlag;

typedef enum _MessageType {
  MSG_CONTROL = 1,
  MSG_DATA,
} MessageType;

typedef enum _Port {
  INPUT_PORT = 0,
  OUTPUT_PORT,
} Port;

typedef struct _ALSfOmxilDecContext ALSfOmxilDecContext;

struct _ALSfOmxilDecContext {
  /**
   * class parent
   */
  ALDecBaseContext stAlDecBaseContext;

  /**
   * mpp
   */
  MppVdecPara *pVdecPara;
  MppCodingType eCodingType;
  MppDataQueue *pInputQueue;
  MppDataQueue *pOutputQueue;
  // OMX
  OMX_HANDLETYPE hComponentDecoder;
  OMX_CALLBACKTYPE callbacks;
  OMX_PARAM_PORTDEFINITIONTYPE pInputPortDefinition;
  OMX_PARAM_PORTDEFINITIONTYPE pOutputPortDefinition;
  OMX_BUFFERHEADERTYPE *pInputBufferArray[64];
  OMX_BUFFERHEADERTYPE *pOutputBufferArray[64];
  MppFrame *pFrame[64];
  S32 nInputBufferCount;
  S32 nOutputBufferCount;
  volatile OMX_STATETYPE comState;

  /**
   * load openmax il so
   */
  U8 so_path[256];
  void *load_so;

  /**
   * other
   */
  S32 nMsgid;
  S32 DecRetEos;
  pthread_t workthread;
  BOOL bNormalMode;
  BOOL bDisableEvent;
  BOOL bJustQuit;
  volatile S32 port0Flushed;
  volatile S32 port1Flushed;
  S32 decFlushed;
  pthread_cond_t condFlushed;
  pthread_cond_t condEos;
  pthread_mutex_t mutex;
  BOOL bIsDestorying;
  FILE *pOutputFile;
  FILE *pInputFile;
  BOOL needAllocDma;
  U32 decInNum;
  U32 decOutNum;
  BOOL bIsFrameReady;

  MppRingBuffer *rb;
};

static void mppframe_create_and_config(ALSfOmxilDecContext *context, S32 i,
                                       S32 id);

static OMX_ERRORTYPE event_handler(OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
                                   OMX_EVENTTYPE eEvent, OMX_U32 nData1,
                                   OMX_U32 nData2, OMX_PTR pEventData) {
  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)pAppData;
  switch (eEvent) {
    case OMX_EventPortSettingsChanged: {
      OMX_INIT_STRUCTURE(context->pOutputPortDefinition);
      context->pOutputPortDefinition.nPortIndex = OUTPUT_PORT;
      OMX_GetParameter(context->hComponentDecoder, OMX_IndexParamPortDefinition,
                       &context->pOutputPortDefinition);
      OMX_U32 nOutputBufferSize = context->pOutputPortDefinition.nBufferSize;
      context->nOutputBufferCount =
          context->pOutputPortDefinition.nBufferCountActual +
          MORE_OUTPUT_BUFFER_NEEDED;
      context->pVdecPara->nOutputBufferNum = context->nOutputBufferCount;
      context->pOutputPortDefinition.nBufferCountActual =
          context->nOutputBufferCount;
      OMX_SetParameter(context->hComponentDecoder, OMX_IndexParamPortDefinition,
                       &context->pOutputPortDefinition);

      debug("allocate %u output buffers size %u", context->nOutputBufferCount,
            nOutputBufferSize);

      debug("======================================");
      debug("out put resolution [%dx%d], stride [%d]",
            context->pOutputPortDefinition.format.video.nFrameWidth,
            context->pOutputPortDefinition.format.video.nFrameHeight,
            context->pOutputPortDefinition.format.video.nStride);
      debug("======================================");

      OMX_SendCommand(context->hComponentDecoder, OMX_CommandPortEnable,
                      OUTPUT_PORT, NULL);

      for (S32 i = 0; i < context->nOutputBufferCount; i++) {
        OMX_BUFFERHEADERTYPE *pBuffer = NULL;
        static S32 maxID = 0;

#ifdef OLD_MODE
        OMX_AllocateBuffer(hComponent, &pBuffer, 1, NULL, nOutputBufferSize);
        context->pOutputBufferArray[i] = pBuffer;
#else
        if (context->needAllocDma) {
          mppframe_create_and_config(context, i, i);
          maxID = (maxID < i) ? i : maxID;
        } else {
          if (FRAME_GetRef(context->pFrame[i]) == 2) {
            S32 nID = FRAME_GetID(context->pFrame[i]);
            maxID = (maxID < nID) ? nID + 1 : maxID + 1;
            FRAME_UnRef(context->pFrame[i]);
            FRAME_UnRef(context->pFrame[i]);

            debug("id%d frame need to be cover, new frame id is %d", nID,
                  maxID);
            mppframe_create_and_config(context, i, maxID);
          } else if (FRAME_GetRef(context->pFrame[i]) != 1) {
            error("id%d frame had something wrong in refcount",
                  FRAME_GetID(context->pFrame[i]));
          }
        }
        OMX_UseBuffer(hComponent, &pBuffer, 1, context->pFrame[i],
                      nOutputBufferSize,
                      FRAME_GetDataPointer(context->pFrame[i], 0));
        context->pVdecPara->bIsBufferInDecoder[i] = MPP_TRUE;
        FRAME_SetMetaData(context->pFrame[i], pBuffer);
        context->pOutputBufferArray[i] = pBuffer;
#endif

        OMX_FillThisBuffer(hComponent, pBuffer);
      }

      OMX_GetParameter(context->hComponentDecoder, OMX_IndexParamPortDefinition,
                       &context->pOutputPortDefinition);
      debug("allocate %u output buffers size %u",
            context->pOutputPortDefinition.nBufferCountActual,
            context->pOutputPortDefinition.nBufferSize);

    } break;
    case OMX_EventBufferFlag: {
      Message data;
      data.msg_type = MSG_CONTROL;
      data.msg_flag = EOS;
      debug("omx msg flag: %d", data.msg_flag);

      if (-1 == msgsnd(context->nMsgid, (void *)&data,
                       sizeof(data) - sizeof(data.msg_type), 0)) {
        debug("msgsnd failed ....");
      }
    } break;
    case OMX_EventCmdComplete: {
      switch ((OMX_COMMANDTYPE)(nData1)) {
        case OMX_CommandStateSet: {
          context->comState = (OMX_STATETYPE)(nData2);
        } break;
        case OMX_CommandPortDisable: {
          if (1 == nData2) {
            context->bDisableEvent = OMX_TRUE;
          }
        } break;
        case OMX_CommandFlush: {
          debug("flush port:%d", nData2);
          if (nData2 == 0)
            context->port0Flushed = MPP_TRUE;
          else
            context->port1Flushed = MPP_TRUE;
        } break;
        default:
          break;
      }
    } break;
    case OMX_EventError: {
      switch (nData1) {
        case OMX_ErrorInsufficientResources:
          error(
              "OMX_ErrorInsufficientResources, out for memory for buffer, stop "
              "decode!");
          break;
        case OMX_ErrorInvalidState:
          error("OMX_ErrorInvalidState, decoder component into invalid state!");
          context->comState = OMX_StateInvalid;
          break;
        default:
          break;
      }

      Message data;
      data.msg_type = MSG_CONTROL;
      data.msg_flag = EOS;
      if (-1 == msgsnd(context->nMsgid, (void *)&data,
                       sizeof(data) - sizeof(data.msg_type), 0)) {
        error("msgsnd failed ....");
      }
      context->bJustQuit = OMX_TRUE;
    } break;
    default:
      break;
  }
  return OMX_ErrorNone;
}

static void mppframe_create_and_config(ALSfOmxilDecContext *context, S32 i,
                                       S32 id) {
  context->pFrame[i] = FRAME_Create();
  FRAME_SetBufferType(context->pFrame[i], MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL);
  FRAME_Alloc(context->pFrame[i], context->pVdecPara->eOutputPixelFormat,
              context->pOutputPortDefinition.format.video.nFrameWidth,
              context->pOutputPortDefinition.format.video.nFrameHeight);
  FRAME_SetID(context->pFrame[i], id);
  context->pVdecPara->nOutputBufferFd[i] = FRAME_GetFD(context->pFrame[i], 0);
  context->pVdecPara->pFrame[i] = (void *)context->pFrame[i];
}

static OMX_ERRORTYPE fill_output_buffer_done_handler(
    OMX_HANDLETYPE hComponent, OMX_PTR pAppData,
    OMX_BUFFERHEADERTYPE *pBuffer) {
  ALSfOmxilDecContext *pDecodeTestContext = (ALSfOmxilDecContext *)pAppData;

  Message data;
  data.msg_type = MSG_CONTROL;

  pDecodeTestContext->decOutNum++;
  if ((pBuffer->nFlags) & OMX_BUFFERFLAG_EOS) {
    data.msg_flag = EOS;
    debug("omx msg flag: %d, (%d %d)", data.msg_flag,
          pDecodeTestContext->port0Flushed, pDecodeTestContext->port1Flushed);

  } else {
    data.msg_flag = FILL_BUFFER_DONE;
    data.pBuffer = pBuffer;
  }

  if (-1 == msgsnd(pDecodeTestContext->nMsgid, (void *)&data,
                   sizeof(data) - sizeof(data.msg_type), 0)) {
    error("msgsnd failed....");
  }

  return OMX_ErrorNone;
}

static OMX_ERRORTYPE empty_buffer_done_handler(OMX_HANDLETYPE hComponent,
                                               OMX_PTR pAppData,
                                               OMX_BUFFERHEADERTYPE *pBuffer) {
  ALSfOmxilDecContext *pDecodeTestContext = (ALSfOmxilDecContext *)pAppData;
  Message data;
  data.msg_type = MSG_CONTROL;
  data.msg_flag = EMPTY_BUFFER_DONE;
  data.pBuffer = pBuffer;
  if (-1 == msgsnd(pDecodeTestContext->nMsgid, (void *)&data,
                   sizeof(data) - sizeof(data.msg_type), 0)) {
    error("msgsnd failed .....");
  }
  return OMX_ErrorNone;
}

static OMX_S32 FillInputBuffer(ALSfOmxilDecContext *context, MppData *sink_data,
                               OMX_BUFFERHEADERTYPE *pInputBuffer) {
  MppPacket *sink_packet = PACKET_GetPacket(sink_data);

  if ((PACKET_GetLength(sink_packet) == 0)) {
    pInputBuffer->nFlags = OMX_BUFFERFLAG_EOS;
    pInputBuffer->nFilledLen = 0;
    return pInputBuffer->nFilledLen;
  }

  if (PACKET_GetEos(sink_packet)) {
    pInputBuffer->nFlags = OMX_BUFFERFLAG_EOS;
  }

  pInputBuffer->nFilledLen = PACKET_GetLength(sink_packet);
#ifdef USE_CIRCULAR_BUFFER
  RingBufferPop(context->rb, pInputBuffer->nFilledLen, pInputBuffer->pBuffer);
#else
  memcpy(pInputBuffer->pBuffer, PACKET_GetDataPointer(sink_packet),
         pInputBuffer->nFilledLen);
#endif
  pInputBuffer->nTimeStamp = PACKET_GetPts(sink_packet);
  // debug("input pts: %lld", (S64)pInputBuffer->nTimeStamp);

  return pInputBuffer->nFilledLen;
}

ALBaseContext *al_dec_create() {
  ALSfOmxilDecContext *context =
      (ALSfOmxilDecContext *)malloc(sizeof(ALSfOmxilDecContext));
  if (!context) {
    error("can not malloc ALSfOmxilDecContext, please check!");
    return NULL;
  }

  memset(context, 0, sizeof(ALSfOmxilDecContext));

  return &(context->stAlDecBaseContext.stAlBaseContext);
}

void *do_decode(void *private_data) {
  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)private_data;
  S32 ret = 0;
  S32 width = context->pOutputPortDefinition.format.video.nFrameWidth;
  S32 height = context->pOutputPortDefinition.format.video.nFrameHeight;

  Message data;
  U64 i = 0;
  U64 limit_count = context->pInputPortDefinition.nBufferCountActual;
  BOOL start_dec = MPP_FALSE;

  debug("------------------new thread-------------------(%d %d)", width,
        height);

  while (OMX_TRUE) {
    if (context->bIsDestorying) {
      goto finish;
    }
    if (context->decFlushed) {
      debug("do_decode thread stop(flush)");
      pthread_mutex_lock(&context->mutex);
      pthread_cond_wait(&context->condFlushed, &context->mutex);
      pthread_mutex_unlock(&context->mutex);
      debug("do_decode thread start(flush)");
      i = 0;
      limit_count = context->pInputPortDefinition.nBufferCountActual;
    }

    if (i < limit_count) {
      MppDataQueueNode *node = DATAQUEUE_Pop(context->pInputQueue);
      context->pVdecPara->nInputQueueLeftNum =
          DATAQUEUE_GetMaxSize(context->pInputQueue) -
          DATAQUEUE_GetCurrentSize(context->pInputQueue);
      if (node) {
        MppData *sink_data = DATAQUEUE_GetData(node);
        MppPacket *sink_packet = PACKET_GetPacket(sink_data);
#ifndef USE_CIRCULAR_BUFFER
        debug("input check:%x %x %x %x %x %x %x %x %x",
              *(S32 *)PACKET_GetDataPointer(sink_packet),
              *(S32 *)(PACKET_GetDataPointer(sink_packet) + 4),
              *(S32 *)(PACKET_GetDataPointer(sink_packet) + 8),
              *(S32 *)(PACKET_GetDataPointer(sink_packet) + 12),
              *(S32 *)(PACKET_GetDataPointer(sink_packet) + 16),
              *(S32 *)(PACKET_GetDataPointer(sink_packet) + 20),
              *(S32 *)(PACKET_GetDataPointer(sink_packet) + 24),
              *(S32 *)(PACKET_GetDataPointer(sink_packet) + 28),
              *(S32 *)(PACKET_GetDataPointer(sink_packet) + 32));
#endif
        FillInputBuffer(context, sink_data, context->pInputBufferArray[i]);

        OMX_EmptyThisBuffer(context->hComponentDecoder,
                            context->pInputBufferArray[i]);
        i++;
        context->decInNum++;

        // free packet && free packet node
#ifdef USE_CIRCULAR_BUFFER
        PACKET_SetDataPointer(sink_packet, NULL);
#endif
        PACKET_Free(sink_packet);
        PACKET_Destory(sink_packet);
        DATAQUEUE_Node_Destory(node);
      } else {
        // debug("pop node is NULL!");
      }
      continue;
    } else if (limit_count == i && !start_dec) {
      debug("start decode process");
      OMX_SendCommand(context->hComponentDecoder, OMX_CommandStateSet,
                      OMX_StateExecuting, NULL);
      i++;
      debug("start decode process finish");
      start_dec = MPP_TRUE;
      continue;
    }

    if (-1 == msgrcv(context->nMsgid, (void *)&data, BUFSIZ, 0, 0)) {
      error("msgrcv failed with errno: %d", errno);
      continue;
    }

    switch (data.msg_flag) {
      case EMPTY_BUFFER_DONE: {
        OMX_BUFFERHEADERTYPE *pBuffer = data.pBuffer;
        MppDataQueueNode *node = NULL;

        node = DATAQUEUE_Pop(context->pInputQueue);

        context->pVdecPara->nInputQueueLeftNum =
            DATAQUEUE_GetMaxSize(context->pInputQueue) -
            DATAQUEUE_GetCurrentSize(context->pInputQueue);
        if (node) {
          MppData *sink_data = DATAQUEUE_GetData(node);
          MppPacket *sink_packet = PACKET_GetPacket(sink_data);

          FillInputBuffer(context, sink_data, pBuffer);
          context->decInNum++;

          OMX_EmptyThisBuffer(context->hComponentDecoder, pBuffer);
#ifdef USE_CIRCULAR_BUFFER
          PACKET_SetDataPointer(sink_packet, NULL);
#endif
          PACKET_Free(sink_packet);
          PACKET_Destory(sink_packet);
          DATAQUEUE_Node_Destory(node);
        } else {
          Message data;
          data.msg_type = MSG_CONTROL;
          data.msg_flag = EMPTY_BUFFER_DONE;
          data.pBuffer = pBuffer;
          if (-1 == msgsnd(context->nMsgid, (void *)&data,
                           sizeof(data) - sizeof(data.msg_type), 0)) {
            error("msgsnd failed .....");
          }
        }
      } break;
      case FILL_BUFFER_DONE: {
        OMX_BUFFERHEADERTYPE *pBuffer = data.pBuffer;
#ifdef OLD_MODE
        MppFrame *frame = FRAME_Create();
        FRAME_SetMetaData(frame, pBuffer);

        if (context->pOutputPortDefinition.format.video.eColorFormat ==
            OMX_COLOR_FormatYUV420Planar) {
          FRAME_SetDataUsedNum(frame, 3);
          FRAME_SetDataPointer(frame, 0, pBuffer->pBuffer);
          FRAME_SetDataPointer(frame, 1, pBuffer->pBuffer + width * height);
          FRAME_SetDataPointer(
              frame, 2, pBuffer->pBuffer + width * height + width * height / 4);
        } else {
          FRAME_SetDataUsedNum(frame, 2);
          FRAME_SetDataPointer(frame, 0, pBuffer->pBuffer);
          FRAME_SetDataPointer(frame, 1, pBuffer->pBuffer + width * height);
        }

#endif
        MppDataQueueNode *node = DATAQUEUE_Node_Create();
#ifdef OLD_MODE
        DATAQUEUE_SetData(node, FRAME_GetBaseData(frame));
        FRAME_GetBaseData(frame)->nPts = pBuffer->nTimeStamp;
#else
        DATAQUEUE_SetData(node,
                          FRAME_GetBaseData((MppFrame *)pBuffer->pAppPrivate));
        FRAME_SetPts((MppFrame *)pBuffer->pAppPrivate, pBuffer->nTimeStamp);
#endif
        // debug("output pts: %lld", (S64)pBuffer->nTimeStamp);

        ret = DATAQUEUE_Push(context->pOutputQueue, node);

        if ((pBuffer->nFlags) & OMX_BUFFERFLAG_EOS) {
          error("decoder commit EOS 111!");
          goto finish;
        }

        if (!ret) {
        } else {
          // debug("outputqueue push fail!");
          Message data;
          data.msg_type = MSG_CONTROL;
          data.msg_flag = FILL_BUFFER_DONE;
          data.pBuffer = pBuffer;
          if (-1 == msgsnd(context->nMsgid, (void *)&data,
                           sizeof(data) - sizeof(data.msg_type), 0)) {
            error("msgsnd failed....");
          }
        }
      } break;
      case EOS: {
        error("decoder commit EOS 222!");
        goto finish;
      }
      default:
        goto finish;
    }
  }

finish:
  context->DecRetEos = MPP_TRUE;
  DATAQUEUE_Cond_BroadCast(context->pInputQueue);
  DATAQUEUE_SetWaitExit(context->pInputQueue, MPP_TRUE);

  DATAQUEUE_Cond_BroadCast(context->pOutputQueue);
  DATAQUEUE_SetWaitExit(context->pOutputQueue, MPP_TRUE);

  debug("finish decode!");
  pthread_exit(NULL);
}

static S32 msg_queue_init() {
  S32 msgid = -1;
  msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
  if (msgid < 0) {
    error("get ipc_id error");
    return -1;
  }
  return msgid;
}

static S32 omx_get_handle(ALBaseContext *ctx) {
  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;
  OMX_ERRORTYPE ret = 0;

  if (CODING_H264 == context->pVdecPara->eCodingType) {
    if (context->bNormalMode) {
      ret =
          omx_gethandle(&context->hComponentDecoder, "OMX.sf.video_decoder.avc",
                        context, &context->callbacks);
    } else {
      ret = omx_gethandle(&context->hComponentDecoder,
                          "OMX.sf.video_decoder.avc.internal", context,
                          &context->callbacks);
    }

    if (OMX_ErrorNone != ret) {
      error("omx_gethandle fail, please check! error=%x", ret);
      return MPP_INIT_FAILED;
    }
  } else if (CODING_H265 == context->pVdecPara->eCodingType) {
    if (context->bNormalMode) {
      ret = omx_gethandle(&context->hComponentDecoder,
                          "OMX.sf.video_decoder.hevc", context,
                          &context->callbacks);
    } else {
      ret = omx_gethandle(&context->hComponentDecoder,
                          "OMX.sf.video_decoder.hevc.internal", context,
                          &context->callbacks);
    }

    if (OMX_ErrorNone != ret) {
      error("omx_gethandle fail, please check! error=%x", ret);
      return MPP_INIT_FAILED;
    }
  } else {
    error("not support the coding type (%d), please check!",
          context->pVdecPara->eCodingType);
    return MPP_INIT_FAILED;
  }

  return MPP_OK;
}

static S32 output_port_init(ALBaseContext *ctx) {
  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;

  OMX_INIT_STRUCTURE(context->pOutputPortDefinition);
  context->pOutputPortDefinition.nPortIndex = OUTPUT_PORT;
  OMX_GetParameter(context->hComponentDecoder, OMX_IndexParamPortDefinition,
                   &context->pOutputPortDefinition);
  context->pOutputPortDefinition.format.video.nFrameWidth =
      context->pVdecPara->nWidth;
  context->pOutputPortDefinition.format.video.nFrameHeight =
      context->pVdecPara->nHeight;
  context->pOutputPortDefinition.format.video.nStride =
      context->pVdecPara->nStride;
  if (context->pVdecPara->eOutputPixelFormat == PIXEL_FORMAT_NV12) {
    context->pOutputPortDefinition.format.video.eColorFormat =
        OMX_COLOR_FormatYUV420SemiPlanar;
  } else if (context->pVdecPara->eOutputPixelFormat == PIXEL_FORMAT_NV21) {
    context->pOutputPortDefinition.format.video.eColorFormat =
        OMX_COLOR_FormatYVU420SemiPlanar;
  } else if (context->pVdecPara->eOutputPixelFormat == PIXEL_FORMAT_I420) {
    context->pOutputPortDefinition.format.video.eColorFormat =
        OMX_COLOR_FormatYUV420Planar;
  } else {
    error("Unsupported color format %d!",
          context->pVdecPara->eOutputPixelFormat);
    return MPP_NOT_SUPPORTED_FORMAT;
  }

  OMX_SetParameter(context->hComponentDecoder, OMX_IndexParamPortDefinition,
                   &context->pOutputPortDefinition);
  OMX_SendCommand(context->hComponentDecoder, OMX_CommandPortDisable,
                  OUTPUT_PORT, NULL);
  debug("output port disabled");

  return MPP_OK;
}

static S32 input_port_init(ALBaseContext *ctx) {
  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;
  OMX_INIT_STRUCTURE(context->pInputPortDefinition);
  context->pInputPortDefinition.nPortIndex = INPUT_PORT;
  OMX_GetParameter(context->hComponentDecoder, OMX_IndexParamPortDefinition,
                   &context->pInputPortDefinition);
  context->pInputPortDefinition.format.video.nFrameWidth =
      context->pVdecPara->nWidth;
  context->pInputPortDefinition.format.video.nFrameHeight =
      context->pVdecPara->nHeight;
  context->pInputPortDefinition.format.video.nStride =
      context->pVdecPara->nStride;
  OMX_SetParameter(context->hComponentDecoder, OMX_IndexParamPortDefinition,
                   &context->pInputPortDefinition);

  return MPP_OK;
}

static S32 alloc_input_buffer(ALBaseContext *ctx) {
  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;
  OMX_ERRORTYPE ret = 0;

  OMX_U32 nInputWidth = context->pVdecPara->nWidth;
  OMX_U32 nInputHeight = context->pVdecPara->nHeight;
  OMX_U32 nInputBufferSize =
      3 * 1024 * 1024;  // nInputWidth * nInputHeight * 2;
  context->nInputBufferCount = context->pInputPortDefinition.nBufferCountActual;

  context->pInputPortDefinition.nBufferCountActual = context->nInputBufferCount;
  OMX_SetParameter(context->hComponentDecoder, OMX_IndexParamPortDefinition,
                   &context->pInputPortDefinition);

  for (S32 i = 0; i < context->nInputBufferCount; i++) {
    OMX_BUFFERHEADERTYPE *pBuffer = NULL;
    ret = OMX_AllocateBuffer(context->hComponentDecoder, &pBuffer, 0, NULL,
                             nInputBufferSize);
    if (OMX_ErrorNone != ret) {
      error("OMX_AllocateBuffer failed, please check! error = %x", ret);
      return MPP_MALLOC_FAILED;
    }
    context->pInputBufferArray[i] = pBuffer;
  }
  debug("input alloc size = %d, count = %d", nInputBufferSize,
        context->nInputBufferCount);

  return MPP_OK;
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

  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;
  S32 ret = OMX_ErrorNone;

  context->pVdecPara = para;
  context->callbacks.EventHandler = event_handler;
  context->callbacks.FillBufferDone = fill_output_buffer_done_handler;
  context->callbacks.EmptyBufferDone = empty_buffer_done_handler;
  context->bNormalMode = MPP_FALSE;
  context->bDisableEvent = MPP_FALSE;
  context->bJustQuit = MPP_FALSE;
  context->eCodingType = para->eCodingType;
  context->nMsgid = msg_queue_init();
  context->bIsDestorying = MPP_FALSE;
  context->bIsFrameReady = MPP_FALSE;
  context->DecRetEos = MPP_FALSE;

  para->eFrameBufferType = MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL;
  para->eDataTransmissinMode = MPP_INPUT_SYNC_OUTPUT_ASYNC;

  if (-1 == context->nMsgid) {
    error("get msgid error");
    goto exit;
  }

  find_dec_sfomx(context->so_path);
  context->load_so = dlopen(context->so_path, RTLD_LAZY | RTLD_LOCAL);
  if (!context->load_so) {
    error("can not dlopen load_so, please check! (%s)", strerror(errno));
    goto exit;
  }
  omx_init = (OMX_ERRORTYPE(*)(void))dlsym(context->load_so, "OMX_Init");
  omx_deinit = (OMX_ERRORTYPE(*)(void))dlsym(context->load_so, "OMX_Deinit");
  omx_gethandle =
      (OMX_ERRORTYPE(*)(OMX_HANDLETYPE * pHandle, OMX_STRING cComponentName,
                        OMX_PTR pAppData, OMX_CALLBACKTYPE * pCallBacks))
          dlsym(context->load_so, "OMX_GetHandle");
  omx_freehandle = (OMX_ERRORTYPE(*)(OMX_HANDLETYPE hComponent))dlsym(
      context->load_so, "OMX_FreeHandle");

  debug("init omx");
  ret = omx_init();
  if (ret != OMX_ErrorNone) {
    error("run OMX_Init failed. ret is %d, please check!", ret);
    goto exit;
  }

  ret = omx_get_handle(ctx);
  if (OMX_ErrorNone != ret || !context->hComponentDecoder) {
    error("could not get omx handle, please check!");
    omx_deinit();
    goto exit;
  }

  context->pInputQueue = DATAQUEUE_Init(para->bInputBlockModeEnable, MPP_FALSE);
  context->pOutputQueue =
      DATAQUEUE_Init(MPP_TRUE, para->bOutputBlockModeEnable);
  context->DecRetEos = MPP_FALSE;
  context->port0Flushed = MPP_FALSE;
  context->port1Flushed = MPP_FALSE;
  context->decFlushed = MPP_FALSE;
  context->needAllocDma = MPP_TRUE;
  context->decInNum = 0;
  context->decOutNum = 0;

  pthread_cond_init(&context->condFlushed, NULL);
  pthread_cond_init(&context->condEos, NULL);
  pthread_mutex_init(&context->mutex, NULL);

#ifdef USE_CIRCULAR_BUFFER
  context->rb = RingBufferCreate(1024 * 1024 * 10);
#endif
  if (context->pVdecPara->nWidth > 0 && context->pVdecPara->nHeight > 0) {
    debug("init get width and height (%d x %d)", context->pVdecPara->nWidth,
          context->pVdecPara->nHeight);
    if (MPP_OK != output_port_init(ctx)) {
      error("could not init output port, please check!");
      omx_deinit();
      goto exit;
    }

    OMX_SendCommand(context->hComponentDecoder, OMX_CommandStateSet,
                    OMX_StateIdle, NULL);

    if (MPP_OK != input_port_init(ctx)) {
      error("could not init input port, please check!");
      omx_deinit();
      goto exit;
    }

    // Alloc input buffer
    if (MPP_OK != alloc_input_buffer(ctx)) {
      error("could not alloc input buffer, please check!");
      omx_deinit();
      goto exit;
    }

    debug("wait for Component idle, %d, %d, %d", OMX_StateIdle,
          context->comState, context->bJustQuit);

    while (context->comState != OMX_StateIdle && !context->bJustQuit)
      ;

    if (context->bJustQuit) {
      error("wait for Component idle, but get a error");
      omx_deinit();
      goto exit;
    }
    debug("Component in idle");

    ret =
        pthread_create(&context->workthread, NULL, do_decode, (void *)context);
  }

#ifdef ENABLE_FILE_SAVE_DEBUG
  context->pOutputFile = fopen(pOutputFileName, "w+");
  context->pInputFile = fopen(pInputFileName, "w+");
#endif

  debug("sfomxdec init finish");

  return MPP_OK;

exit:
  error("sfomxdec init fail");
  free(context);
  return MPP_INIT_FAILED;
}

RETURN al_dec_getparam(ALBaseContext *ctx, MppVdecPara **para) {
  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;
  *para = context->pVdecPara;
  return MPP_OK;
}

S32 al_dec_decode(ALBaseContext *ctx, MppData *sink_data) {
  static U32 count = 0;
  static U64 length = 0;
  S32 ret = 0;

  if (!ctx) {
    error("input para ALBaseContext is NULL, please check!");
    return MPP_NULL_POINTER;
  }

  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;
  MppPacket *sink_packet = PACKET_GetPacket(sink_data);

  MppDataQueueNode *node = DATAQUEUE_Node_Create();

  // handle EOS
  if (PACKET_GetEos(sink_packet)) {
    MppPacket *dst_packet = PACKET_Create();
    PACKET_SetLength(dst_packet, 0);
    PACKET_SetEos(dst_packet, 1);

    DATAQUEUE_SetData(node, PACKET_GetBaseData(dst_packet));

    ret = DATAQUEUE_Push(context->pInputQueue, node);
    context->pVdecPara->nInputQueueLeftNum =
        DATAQUEUE_GetMaxSize(context->pInputQueue) -
        DATAQUEUE_GetCurrentSize(context->pInputQueue);
    debug("------ eos push ret = %d", ret);

    return ret;
  }

  if (0 == context->pVdecPara->nWidth || 0 == context->pVdecPara->nHeight) {
    S32 tmp_width = 0;
    S32 tmp_height = 0;
    get_resolution_from_stream(PACKET_GetDataPointer(sink_packet),
                               PACKET_GetLength(sink_packet), &tmp_width,
                               &tmp_height);
    debug("get width and height: %d x %d", tmp_width, tmp_height);
    if (tmp_width != 0 && tmp_height != 0) {
      context->pVdecPara->nWidth = tmp_width;
      context->pVdecPara->nHeight = tmp_height;
      context->pVdecPara->nStride = tmp_width;
      context->pVdecPara->bIsResolutionChanged = MPP_TRUE;

      if (MPP_OK != output_port_init(ctx)) {
        error("could not init output port, please check!");
        return MPP_INIT_FAILED;
      }
      OMX_SendCommand(context->hComponentDecoder, OMX_CommandStateSet,
                      OMX_StateIdle, NULL);
      if (MPP_OK != input_port_init(ctx)) {
        error("could not init input port, please check");
        return MPP_INIT_FAILED;
      }

      // Alloc input buffer
      if (MPP_OK != alloc_input_buffer(ctx)) {
        error("could not alloc input buffer, please check!");
        return MPP_INIT_FAILED;
      }
      debug("wait for Component idle, %d, %d, %d", OMX_StateIdle,
            context->comState, context->bJustQuit);

      while (context->comState != OMX_StateIdle && !context->bJustQuit)
        ;

      if (context->bJustQuit) {
        error("wait for Component idle, but get a error");
        return MPP_INIT_FAILED;
      }
      debug("Component in idle");

      ret = pthread_create(&context->workthread, NULL, do_decode,
                           (void *)context);
    }
  }

#ifdef ENABLE_FILE_SAVE_DEBUG
  fwrite(PACKET_GetDataPointer(sink_packet), PACKET_GetLength(sink_packet), 1,
         context->pInputFile);
  fflush(context->pInputFile);
#endif

  MppPacket *packet = NULL;
  if (context->pVdecPara->nInputQueueLeftNum > 0 ||
      context->pVdecPara->bInputBlockModeEnable) {
    packet = PACKET_Create();

#ifdef USE_CIRCULAR_BUFFER
    void *tmp = RingBufferGetTailAddr(context->rb);

    RingBufferPush(context->rb, PACKET_GetDataPointer(sink_packet),
                   PACKET_GetLength(sink_packet));

    PACKET_SetDataPointer(packet, tmp);
    PACKET_SetLength(packet, PACKET_GetLength(sink_packet));

#else
    PACKET_Alloc(packet, PACKET_GetLength(sink_packet));
    PACKET_SetLength(packet, PACKET_GetLength(sink_packet));
    memcpy(PACKET_GetDataPointer(packet), PACKET_GetDataPointer(sink_packet),
           PACKET_GetLength(sink_packet));
#endif
    PACKET_SetPts(packet, PACKET_GetPts(sink_packet));
    PACKET_SetID(packet, PACKET_GetID(sink_packet));

    DATAQUEUE_SetData(node, PACKET_GetBaseData(packet));
  } else {
    context->pVdecPara->nInputQueueLeftNum =
        DATAQUEUE_GetMaxSize(context->pInputQueue) -
        DATAQUEUE_GetCurrentSize(context->pInputQueue);
    return MPP_DATAQUEUE_FULL;
  }

  ret = DATAQUEUE_Push(context->pInputQueue, node);
  context->pVdecPara->nInputQueueLeftNum =
      DATAQUEUE_GetMaxSize(context->pInputQueue) -
      DATAQUEUE_GetCurrentSize(context->pInputQueue);

  //  debug("push packet to dec, %d %d, (%d)", ret,
  //       ++count,  PACKET_GetLength(PACKET_GetPacket(sink_data)));

  if (ret) {
    PACKET_Free(packet);
    PACKET_Destory(packet);
    DATAQUEUE_Node_Destory(node);
  }

  return ret;
}

S32 al_dec_request_output_frame(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) return MPP_NULL_POINTER;
  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;
  MppDataQueueNode *node;
  static U32 count = 0;

  if (context->DecRetEos && DATAQUEUE_IsEmpty(context->pOutputQueue) == 1) {
    debug("ret dec eos");

    return MPP_CODER_EOS;
  }

  node = DATAQUEUE_Pop(context->pOutputQueue);
  if (!node) {
    return context->DecRetEos ? MPP_CODER_EOS : MPP_CODER_NO_DATA;
  }

  memcpy(src_data, DATAQUEUE_GetData(node), FRAME_GetStructSize());

  debug("----- num %d, request: %d left: %d (%d)",
        FRAME_GetDataUsedNum(FRAME_GetFrame(DATAQUEUE_GetData(node))), ++count,
        DATAQUEUE_GetCurrentSize(context->pOutputQueue),
        DATAQUEUE_GetCurrentSize(context->pInputQueue));

  // note: will double free err in vdec_test
  //   FRAME_Destory(FRAME_GetFrame(DATAQUEUE_GetData(node)));

  // free output node
  context->bIsFrameReady = MPP_TRUE;
  DATAQUEUE_Node_Destory(node);

  return MPP_OK;
}

S32 al_dec_request_output_frame_2(ALBaseContext *ctx, MppData **src_data) {
  ALSfOmxilDecContext *context;
  MppDataQueueNode *node;
  static U32 count = 0;
  MppFrame *nframe;
  MppFrame *src_frame;

  if (!ctx) return MPP_NULL_POINTER;

  context = (ALSfOmxilDecContext *)ctx;
  if (context->DecRetEos && DATAQUEUE_IsEmpty(context->pOutputQueue) == 1) {
    debug("ret dec eos");

    return MPP_CODER_EOS;
  }

  node = DATAQUEUE_Pop(context->pOutputQueue);
  if (!node) {
    return context->DecRetEos ? MPP_CODER_EOS : MPP_CODER_NO_DATA;
  }

  nframe = FRAME_GetFrame(DATAQUEUE_GetData(node));
  FRAME_Ref(nframe);

  *src_data = DATAQUEUE_GetData(node);
  src_frame = FRAME_GetFrame(*src_data);

#ifdef ENABLE_FILE_SAVE_DEBUG
  fwrite(FRAME_GetDataPointer(nframe), 0),
         640 * 480 * 3 / 2, 1, context->pOutputFile);
  fflush(context->pOutputFile);
#endif

  // debug("----- num %d %d, request: %d left: %d (%d)",
  //       FRAME_GetDataUsedNum(src_frame),
  //       FRAME_GetDataUsedNum(nframe),
  //       ++count, DATAQUEUE_GetCurrentSize(context->pOutputQueue),
  //       DATAQUEUE_GetCurrentSize(context->pInputQueue));

  // FRAME_Destory(nframe);

  // free output node
  DATAQUEUE_Node_Destory(node);

  context->bIsFrameReady = MPP_TRUE;
  context->pVdecPara->bIsBufferInDecoder[FRAME_GetID(src_frame)] = MPP_FALSE;

  return MPP_OK;
}

S32 al_dec_return_output_frame(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) return MPP_NULL_POINTER;

  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;
  S32 ret = 0;
  MppFrame *src_frame;

  if (!src_data) return -1;

  if (!context->bIsFrameReady) {
    return -1;
  }

  src_frame = FRAME_GetFrame(src_data);

  debug("return a output frame index(%d)", FRAME_GetID(src_frame));

  if (FRAME_GetRef(src_frame) == 0) {
    debug("id%d frame need to be destory", FRAME_GetID(src_frame));

    FRAME_Free(src_frame);
    FRAME_Destory(src_frame);
  } else {
    FRAME_UnRef(src_frame);

    OMX_FillThisBuffer(context->hComponentDecoder,
                       (OMX_BUFFERHEADERTYPE *)(FRAME_GetMetaData(src_frame)));
  }

  context->pVdecPara->bIsBufferInDecoder[FRAME_GetID(src_frame)] = MPP_TRUE;

  return 0;
}

S32 omx_dec_reinit(ALBaseContext *ctx) {
  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;
  S32 ret = 0;
  MppDataQueueNode *node = NULL;
  MppData *sink_data = NULL;
  MppData *src_data = NULL;

  if (DATAQUEUE_IsEmpty(context->pOutputQueue) == 1)
    debug("pOutputQueue is empty");
  else
    debug("pOutputQueue is not empty %d",
          DATAQUEUE_GetCurrentSize(context->pOutputQueue));

  if (DATAQUEUE_IsEmpty(context->pInputQueue) == 1)
    debug("pInputQueue is empty");
  else
    debug("pInputQueue is not empty %d",
          DATAQUEUE_GetCurrentSize(context->pInputQueue));

  if (OMX_StateExecuting == context->comState) {
    OMX_SendCommand(context->hComponentDecoder, OMX_CommandStateSet,
                    OMX_StatePause, NULL);
    debug("wait for Component OMX_StatePause");
    while (context->comState != OMX_StatePause)
      ;
    debug("Component in OMX_StatePause");
  }
  // flush port 0 && flush port 1
  OMX_SendCommand(context->hComponentDecoder, OMX_CommandFlush, 0, NULL);
  while (context->port0Flushed == MPP_FALSE)
    ;

  OMX_SendCommand(context->hComponentDecoder, OMX_CommandFlush, 1, NULL);
  while (context->port1Flushed == MPP_FALSE)
    ;

  if (OMX_StatePause == context->comState) {
    OMX_SendCommand(context->hComponentDecoder, OMX_CommandStateSet,
                    OMX_StateIdle, NULL);
    debug("wait for Component idle");
    while (context->comState != OMX_StateIdle)
      ;
    debug("Component in idle");
  }

  OMX_SendCommand(context->hComponentDecoder, OMX_CommandStateSet,
                  OMX_StateLoaded, NULL);

  for (S32 i = 0; i < context->nOutputBufferCount; i++) {
    OMX_FreeBuffer(context->hComponentDecoder, 1,
                   context->pOutputBufferArray[i]);
  }
  for (S32 i = 0; i < context->nInputBufferCount; i++) {
    OMX_FreeBuffer(context->hComponentDecoder, 0,
                   context->pInputBufferArray[i]);
  }

  while (context->comState != OMX_StateLoaded)
    ;

  omx_freehandle(context->hComponentDecoder);
  omx_deinit();

  DATAQUEUE_Cond_BroadCast(context->pInputQueue);
  DATAQUEUE_Cond_BroadCast(context->pOutputQueue);

  // msgctl Destory -> new
  if (msgctl(context->nMsgid, IPC_RMID, NULL) < 0) {
    error("delete msg queue fail");
    return -MPP_FALSE;
  }

  context->nMsgid = msg_queue_init();
  if (-1 == context->nMsgid) {
    error("get msgid error");
    return MPP_INIT_FAILED;
  }

  debug("init omx");
  ret = omx_init();
  if (ret != OMX_ErrorNone) {
    error("run OMX_Init failed. ret is %d", ret);
    return -MPP_FALSE;
  }

  omx_get_handle(ctx);
  if (!context->hComponentDecoder) {
    error("could not get handle");
    omx_deinit();
    return MPP_INIT_FAILED;
  }
  if (MPP_OK != output_port_init(ctx)) {
    error("could not init output port");
    omx_deinit();
    return MPP_INIT_FAILED;
  }

  OMX_SendCommand(context->hComponentDecoder, OMX_CommandStateSet,
                  OMX_StateIdle, NULL);

  if (MPP_OK != input_port_init(ctx)) {
    error("could not init input port");
    omx_deinit();
    return MPP_INIT_FAILED;
  }

  if (MPP_OK != alloc_input_buffer(ctx)) {
    error("could not alloc input buffer");
    omx_deinit();
    return MPP_INIT_FAILED;
  }

  while (context->comState != OMX_StateIdle && !context->bJustQuit)
    ;
  if (context->bJustQuit) {
    error("wait for Component idle, but get a error");
    return -1;
  }

  context->port0Flushed = MPP_FALSE;
  context->port1Flushed = MPP_FALSE;
  context->DecRetEos = MPP_FALSE;

  DATAQUEUE_SetWaitExit(context->pInputQueue, MPP_FALSE);
  DATAQUEUE_SetWaitExit(context->pOutputQueue, MPP_FALSE);
  context->needAllocDma = MPP_FALSE;

  ret = pthread_create(&context->workthread, NULL, do_decode, (void *)context);
  return ret;
}

S32 al_dec_flush(ALBaseContext *ctx) {
  if (!ctx) return MPP_NULL_POINTER;

  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;
  MppDataQueueNode *node = NULL;
  S32 count = 0;
  U32 wait_num;
  S32 oqueuenum, release_num = 0;

  if (context->DecRetEos) {
    debug("start to restart dec.");
    omx_dec_reinit(ctx);

    DATAQUEUE_SetWaitExit(context->pInputQueue, MPP_FALSE);
    DATAQUEUE_SetWaitExit(context->pOutputQueue, MPP_FALSE);
    context->DecRetEos = MPP_FALSE;
    pthread_cond_signal(&context->condEos);
    debug("finish to restart dec.");

    return MPP_OK;
  }

  debug("start to seek flush.(%d, %d)", context->decInNum, context->decOutNum);

  if (DATAQUEUE_IsEmpty(context->pInputQueue)) {
    debug("pInputQueue is empty");
  } else {
    while (MPP_TRUE) {
      node = DATAQUEUE_Pop(context->pInputQueue);
      if (node) {
        MppData *sink_data = DATAQUEUE_GetData(node);
        MppPacket *sink_packet = PACKET_GetPacket(sink_data);

        PACKET_Free(sink_packet);
        PACKET_Destory(sink_packet);
        DATAQUEUE_Node_Destory(node);
        release_num++;

        if (DATAQUEUE_IsEmpty(context->pInputQueue)) break;
      } else {
        break;
      }
    }
  }
  debug("pInputQueue is empty, released %d node (%d)", release_num,
        DATAQUEUE_GetCurrentSize(context->pOutputQueue));

  release_num = 0;
  oqueuenum = DATAQUEUE_GetCurrentSize(context->pOutputQueue);
  wait_num = context->decInNum - oqueuenum;

  if (DATAQUEUE_IsEmpty(context->pOutputQueue)) {
    debug("pOutputQueue is empty");
  } else {
  again:
    while (MPP_TRUE) {
      node = DATAQUEUE_Pop(context->pOutputQueue);
      if (node) {
        MppData *src_data = DATAQUEUE_GetData(node);

        OMX_FillThisBuffer(context->hComponentDecoder,
                           (OMX_BUFFERHEADERTYPE *)(FRAME_GetMetaData(
                               FRAME_GetFrame(src_data))));

        DATAQUEUE_Node_Destory(node);
        if (oqueuenum > 0) {
          oqueuenum--;
          goto again;
        }

        release_num++;
        debug("(%d)(%d, %d)(%d)", release_num, context->decOutNum, wait_num,
              DATAQUEUE_GetCurrentSize(context->pOutputQueue));

        if (DATAQUEUE_IsEmpty(context->pOutputQueue) &&
            context->decOutNum >= wait_num)
          break;
      } else {
        break;
      }
    }
  }
  debug("pOutputQueue has %d node now, released %d node",
        DATAQUEUE_GetCurrentSize(context->pOutputQueue), release_num);

  debug("finish to seek flush.(%u, %u)", context->decOutNum, context->decInNum);

  return MPP_OK;
}

S32 al_dec_reset(ALBaseContext *ctx) {
  if (!ctx) return MPP_NULL_POINTER;

  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;
  MppDataQueueNode *node = NULL;
  S32 count = 0;
  U32 wait_num;
  S32 oqueuenum, release_num = 0;

  if (context->DecRetEos) {
    debug("start to restart dec.");
    omx_dec_reinit(ctx);

    DATAQUEUE_SetWaitExit(context->pInputQueue, MPP_FALSE);
    DATAQUEUE_SetWaitExit(context->pOutputQueue, MPP_FALSE);
    context->DecRetEos = MPP_FALSE;
    pthread_cond_signal(&context->condEos);
    debug("finish to restart dec.");

    return MPP_OK;
  }

  return MPP_OK;
}

void al_dec_destory(ALBaseContext *ctx) {
  ALSfOmxilDecContext *context = (ALSfOmxilDecContext *)ctx;
  S32 ret = 0;

  debug("destory 1");
  if (OMX_StateExecuting == context->comState) {
    OMX_SendCommand(context->hComponentDecoder, OMX_CommandStateSet,
                    OMX_StateIdle, NULL);
    debug("wait for Component idle");
    while (context->comState != OMX_StateIdle)
      ;
    debug("Component in idle");
  }

  // OMX_SendCommand(context->hComponentDecoder, OMX_CommandPortDisable, 0,
  // NULL);
  // OMX_SendCommand(context->hComponentDecoder, OMX_CommandPortDisable, 1,
  // NULL);
  debug("destory 2");
  OMX_SendCommand(context->hComponentDecoder, OMX_CommandStateSet,
                  OMX_StateLoaded, NULL);

  context->bIsDestorying = MPP_TRUE;

  // note:thread had call pthread_exit
  //   pthread_join(context->workthread, NULL);
  for (S32 i = 0; i < context->nOutputBufferCount; i++) {
    OMX_FreeBuffer(context->hComponentDecoder, 1,
                   context->pOutputBufferArray[i]);
#ifndef OLD_MODE
    FRAME_Free(context->pFrame[i]);
    FRAME_Destory(context->pFrame[i]);
#endif
  }
  error("destory 4");
  for (S32 i = 0; i < context->nInputBufferCount; i++) {
    OMX_FreeBuffer(context->hComponentDecoder, 0,
                   context->pInputBufferArray[i]);
  }
  error("destory 5");
  while (context->comState != OMX_StateLoaded)
    ;
  omx_freehandle(context->hComponentDecoder);
  omx_deinit();
  error("destory 6");
  dlclose(context->load_so);
  error("destory 7");
  DATAQUEUE_Cond_BroadCast(context->pInputQueue);
  DATAQUEUE_SetWaitExit(context->pInputQueue, MPP_TRUE);

  DATAQUEUE_Cond_BroadCast(context->pOutputQueue);
  DATAQUEUE_SetWaitExit(context->pOutputQueue, MPP_TRUE);

  DATAQUEUE_Destory(context->pInputQueue);
  DATAQUEUE_Destory(context->pOutputQueue);
#ifdef USE_CIRCULAR_BUFFER
  RingBufferFree(context->rb);
#endif
  error("destory 8");
#ifdef ENABLE_FILE_SAVE_DEBUG
  fclose(context->pOutputFile);
  fclose(context->pInputFile);
#endif

  free(context);
}
