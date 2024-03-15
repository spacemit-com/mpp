/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:40
 * @LastEditTime: 2023-02-01 19:13:45
 * @Description: video encode plugin for openh264, only can encode H.264 stream
 */

//#define ENABLE_DEBUG 1

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "al_interface_enc.h"
#include "log.h"
#include "wels/codec_api.h"
#include "wels/codec_app_def.h"
#include "wels/codec_def.h"
#include "wels/codec_ver.h"

#define MODULE_TAG "openh264enc"

typedef struct _ALSoftOpenh264EncContext ALSoftOpenh264EncContext;

struct _ALSoftOpenh264EncContext {
  ALEncBaseContext stAlEncBaseContext;
  ISVCEncoder *pSvcEncoder;
  SEncParamBase stParam;
  SFrameBSInfo stInfo;
  SSourcePicture stPic;
  S32 bResult;
  S32 EncRetEos;
};
/*node*/
struct DNode {
  struct DNode *next;

  MppPacket *pPacket;
  S32 num;
};

struct DList {
  struct DNode *fnode;
  S32 count;
  pthread_mutex_t mutex;
};

static struct DList *srcDataList = NULL;

ALBaseContext *al_enc_create() {
  ALSoftOpenh264EncContext *enc_context =
      (ALSoftOpenh264EncContext *)malloc(sizeof(ALSoftOpenh264EncContext));
  if (!enc_context) {
    error("can not malloc ALSoftOpenh264EncContext, please check!");
    return NULL;
  }
  memset(enc_context, 0, sizeof(ALSoftOpenh264EncContext));

  enc_context->pSvcEncoder = NULL;

  memset(&(enc_context->stParam), 0, sizeof(SEncParamBase));
  memset(&(enc_context->stInfo), 0, sizeof(SFrameBSInfo));
  memset(&(enc_context->stPic), 0, sizeof(SSourcePicture));

  enc_context->bResult = 1;

  return &(enc_context->stAlEncBaseContext.stAlBaseContext);
}

RETURN al_enc_init(ALBaseContext *ctx, MppVencPara *para) {
  ALSoftOpenh264EncContext *enc_context = (ALSoftOpenh264EncContext *)ctx;
  S32 ret = 0;

  if (enc_context->pSvcEncoder != NULL) {
    enc_context->pSvcEncoder->Uninitialize();
    WelsDestroySVCEncoder(enc_context->pSvcEncoder);
    enc_context->pSvcEncoder = NULL;
  }

  ret = WelsCreateSVCEncoder(&enc_context->pSvcEncoder);
  if (ret || !enc_context->pSvcEncoder) {
    error("Create Openh264 encoder failed, Please check it !");
  }

  // create & init node list
  srcDataList = (struct DList *)malloc(sizeof(struct DList));
  if (!srcDataList) {
    error("srcDataList is NULL, please check it !");
    return MPP_NULL_POINTER;
  }
  srcDataList->count = 0;
  srcDataList->fnode = NULL;

  pthread_mutex_init(&srcDataList->mutex, NULL);

  return MPP_OK;
}

S32 al_enc_set_para(ALBaseContext *ctx, MppVencPara *para) {
  ALSoftOpenh264EncContext *enc_context = (ALSoftOpenh264EncContext *)ctx;
  S32 ret = 0;
  S32 video_format = -1;

  enc_context->stParam.iUsageType =
      CAMERA_VIDEO_REAL_TIME;  // from EUsageType enum
  enc_context->stParam.fMaxFrameRate = para->nFrameRate;
  enc_context->stParam.iPicWidth = para->nWidth;
  enc_context->stParam.iPicHeight = para->nHeight;
  enc_context->stParam.iTargetBitrate = para->nBitrate;
  ret = enc_context->pSvcEncoder->Initialize(&enc_context->stParam);
  if (ret != cmResultSuccess) error("Initialize encoder is fail");

  // enc_context->pSvcEncoder->SetOption (ENCODER_OPTION_TRACE_LEVEL,
  // &g_LevelSetting);
  video_format = videoFormatI420;
  enc_context->pSvcEncoder->SetOption(ENCODER_OPTION_DATAFORMAT, &video_format);

  enc_context->stPic.iPicWidth = para->nWidth;
  enc_context->stPic.iPicHeight = para->nHeight;
  enc_context->stPic.iColorFormat = videoFormatI420;
  enc_context->stPic.iStride[0] = para->nStride;
  enc_context->stPic.iStride[1] = enc_context->stPic.iStride[2] =
      para->nStride >> 1;

  return MPP_OK;
}

S32 al_enc_encode(ALBaseContext *ctx, MppData *sink_data) {
  ALSoftOpenh264EncContext *enc_context = (ALSoftOpenh264EncContext *)ctx;
  MppFrame *frame = FRAME_GetFrame(sink_data);
  struct DNode *ins_node = NULL;
  static U32 num = 1;
  S32 ret = 0;

  if (!ctx) {
    error("input para ctx is NULL, please check it !");
    return MPP_NULL_POINTER;
  }

  if (!sink_data) {
    error("input para sink_data is NULL, please check it !");
    return MPP_NULL_POINTER;
  }

  if (enc_context->EncRetEos) {
    error("Para error, please check it !");
    return MPP_CHECK_FAILED;
  }

  if (FRAME_GetEos(frame)) {
    enc_context->EncRetEos = MPP_TRUE;
  }

  enc_context->stPic.pData[0] = (U8 *)FRAME_GetDataPointer(frame, 0);
  enc_context->stPic.pData[1] = (U8 *)FRAME_GetDataPointer(frame, 1);
  enc_context->stPic.pData[2] = (U8 *)FRAME_GetDataPointer(frame, 2);

  // encode a frame
  ret = enc_context->pSvcEncoder->EncodeFrame(&enc_context->stPic,
                                              &enc_context->stInfo);
  if (cmResultSuccess == ret) {
    enc_context->bResult = 0;
  } else {
    error("encode a frame failed, ret = %d, please check !", ret);
  }

  if (!enc_context->bResult &&
      enc_context->stInfo.eFrameType != videoFrameTypeSkip) {
    // output bitstream handling
    S32 buf_length = 0;

    for (S32 i = 0; i < enc_context->stInfo.iLayerNum; ++i) {
      const SLayerBSInfo &layerInfo = enc_context->stInfo.sLayerInfo[i];
      S32 layer_size = 0;
      for (S32 j = 0; j < layerInfo.iNalCount; ++j) {
        layer_size += layerInfo.pNalLengthInByte[j];
      }
      buf_length += layer_size;
      debug("buf_length = %d", buf_length);
    }

    // insert node malloc
    ins_node = (struct DNode *)malloc(sizeof(struct DNode));
    ins_node->pPacket = PACKET_Create();
    PACKET_Alloc(ins_node->pPacket, buf_length);
    PACKET_SetLength(ins_node->pPacket, buf_length);

    buf_length = 0;
    for (S32 i = 0; i < enc_context->stInfo.iLayerNum; ++i) {
      const SLayerBSInfo &layerInfo = enc_context->stInfo.sLayerInfo[i];
      S32 layer_size = 0;
      for (S32 j = 0; j < layerInfo.iNalCount; ++j) {
        layer_size += layerInfo.pNalLengthInByte[j];
      }
      memcpy((U8 *)PACKET_GetDataPointer(ins_node->pPacket) + buf_length,
             layerInfo.pBsBuf, layer_size);
      buf_length += layer_size;
    }
    enc_context->bResult = 1;

    // insert node to list
    pthread_mutex_lock(&srcDataList->mutex);
    if (srcDataList->fnode == NULL) {
      srcDataList->fnode = ins_node;
      ins_node->next = NULL;
    } else {
      ins_node->next = srcDataList->fnode;
      srcDataList->fnode = ins_node;
    }

    ins_node->num = num;
    num++;
    srcDataList->count++;
    pthread_mutex_unlock(&srcDataList->mutex);

    return 0;
  } else {
    debug("encode a frame, but skip");
  }

  return ret;
}

S32 al_enc_request_output_stream(ALBaseContext *ctx, MppData *src_data) {
  ALSoftOpenh264EncContext *enc_context = (ALSoftOpenh264EncContext *)ctx;
  struct DNode *req_node;
  struct DNode *pre_node;
  S32 ret = MPP_OK;

  pthread_mutex_lock(&srcDataList->mutex);

  req_node = srcDataList->fnode;
  pre_node = req_node;

  if (req_node == NULL) {
    ret = MPP_CODER_NO_DATA;

    if (enc_context->EncRetEos) {
      ret = MPP_CODER_EOS;
    }

    pthread_mutex_unlock(&srcDataList->mutex);
    return ret;
  }
#if 0
    struct DNode *tmp_node = req_node;
    debug ("be req");

    while (tmp_node != NULL) {
        debug ("%d ", tmp_node->num);
        tmp_node = tmp_node->next;
    }
    debug ("af req");
#endif

  while (req_node->next != NULL) req_node = req_node->next;

  // TODO: if src_frame pData had memory, need copy again
  MppPacket *src_packet = PACKET_GetPacket(src_data);

  PACKET_SetDataPointer(src_packet,
                        (U8 *)PACKET_GetDataPointer(req_node->pPacket));
  PACKET_SetLength(src_packet, PACKET_GetLength(req_node->pPacket));

  pthread_mutex_unlock(&srcDataList->mutex);

  return ret;
}
S32 al_enc_return_output_stream(ALBaseContext *ctx, MppData *src_data) {
  S32 ret = MPP_OK;
  struct DNode *del_node;
  struct DNode *pre_node;

  pthread_mutex_lock(&srcDataList->mutex);

  del_node = srcDataList->fnode;
  pre_node = del_node;

  if (del_node == NULL) {
    pthread_mutex_unlock(&srcDataList->mutex);
    return 1;
  }
#if 0
    struct DNode *tmp_node = del_node;
    debug ("be out");

    while (tmp_node != NULL) {
        debug ("%d ", tmp_node->num);
        tmp_node = tmp_node->next;
    }
    debug ("af out");
#endif

  // find the last node in list, del it
  if (del_node->next == NULL) {
    srcDataList->fnode = NULL;
  } else {
    while (1) {
      if (del_node->next == NULL) {
        pre_node->next = del_node->next;
        break;
      }
      pre_node = del_node;
      del_node = del_node->next;
    }
  }

  srcDataList->count--;

  pthread_mutex_unlock(&srcDataList->mutex);

  PACKET_Free(del_node->pPacket);
  PACKET_Destory(del_node->pPacket);
  free(del_node);

  MppPacket *src_packet = PACKET_GetPacket(src_data);
  PACKET_SetLength(src_packet, 0);
  // src_packet->nLength = 0;
  // src_packet->pData = NULL;

  return ret;
}

void al_enc_destory(ALBaseContext *ctx) {
  ALSoftOpenh264EncContext *enc_context = (ALSoftOpenh264EncContext *)ctx;
  if (!ctx) {
    error("No need to destory, return !");
    return;
  }

  if (enc_context->pSvcEncoder) {
    enc_context->pSvcEncoder->Uninitialize();
    WelsDestroySVCEncoder(enc_context->pSvcEncoder);
    enc_context->pSvcEncoder = NULL;
  }
  if (srcDataList) {
    pthread_mutex_destroy(&srcDataList->mutex);
    free(srcDataList);
  }
  free(enc_context);
  enc_context = NULL;
}
