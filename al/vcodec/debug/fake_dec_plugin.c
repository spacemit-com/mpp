/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:36
 * @LastEditTime: 2023-12-13 20:13:57
 * @Description: video decode plugin for fake test
 */

#define ENABLE_DEBUG 1

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

#define MODULE_TAG "fake_dec"

#define NUM_OF_FRAMES 5

typedef struct _ALFakeDecContext ALFakeDecContext;

struct _ALFakeDecContext {
  ALDecBaseContext stAlDecBaseContext;
  MppVdecPara *pVdecPara;
  MppCodingType eCodingType;
  MppFrame *pOutputFrame[NUM_OF_FRAMES];
  BOOL bIsFrameUsed[NUM_OF_FRAMES];
  S32 nMagicNum;
  U8 nMagicColor;

  U32 gPacketNum;
  U32 rFrameNum;
  S32 DecRetEos;
};

ALBaseContext *al_dec_create() {
  ALFakeDecContext *context =
      (ALFakeDecContext *)malloc(sizeof(ALFakeDecContext));
  if (!context) return NULL;

  return &(context->stAlDecBaseContext.stAlBaseContext);
}

RETURN al_dec_init(ALBaseContext *ctx, MppVdecPara *para) {
  if (!ctx || !para) return MPP_NULL_POINTER;

  ALFakeDecContext *context = (ALFakeDecContext *)ctx;
  S32 ret = 0;

  context->pVdecPara = para;
  for (S32 i = 0; i < NUM_OF_FRAMES; i++) {
    context->pOutputFrame[i] = FRAME_Create();
    FRAME_Alloc(context->pOutputFrame[i],
                context->pVdecPara->eOutputPixelFormat,
                context->pVdecPara->nWidth, context->pVdecPara->nHeight);
    FRAME_SetID(context->pOutputFrame[i], i);
    context->bIsFrameUsed[i] = MPP_FALSE;
  }
  context->nMagicNum = 0;
  context->gPacketNum = 0;
  context->rFrameNum = 0;
  context->DecRetEos = MPP_FALSE;

  context->nMagicColor = 255;

  debug("init finish");

  return MPP_OK;
}

S32 al_dec_decode(ALBaseContext *ctx, MppData *sink_data) {
  ALFakeDecContext *context;
  static U32 count = 0;
  static U64 length = 0;
  MppPacket *packet = NULL;

  if (!ctx || !sink_data) return MPP_NULL_POINTER;

  context = (ALFakeDecContext *)ctx;
  packet = PACKET_GetPacket(sink_data);

  if (PACKET_GetEos(packet)) {
    context->DecRetEos = MPP_TRUE;
  } else {
    context->gPacketNum++;
  }

  return 0;
}

S32 al_dec_request_output_frame(ALBaseContext *ctx, MppData *src_data) {
  ALFakeDecContext *context;
  MppDataQueueNode *node;
  static U32 count = 0;

  if (!ctx) return MPP_NULL_POINTER;

  context = (ALFakeDecContext *)ctx;

  if (!context->DecRetEos && context->rFrameNum >= context->gPacketNum) {
    usleep(500);
    return MPP_CODER_NO_DATA;
  }

  if (context->DecRetEos && context->rFrameNum >= context->gPacketNum) {
    return MPP_CODER_EOS;
  }

  S32 i = 0;
  for (i = 0; i < NUM_OF_FRAMES; i++) {
    if (context->bIsFrameUsed[i] == MPP_FALSE) {
      memcpy(src_data, FRAME_GetBaseData(context->pOutputFrame[i]),
             FRAME_GetStructSize());
      for (S32 k = 0; k < 20; k++) {
        for (S32 j = context->nMagicNum; j < context->nMagicNum + 20; j++) {
          *(U8 *)(FRAME_GetDataPointer(context->pOutputFrame[i], 0) +
                  (k * context->pVdecPara->nWidth) + j) = context->nMagicColor;
        }
      }
      context->bIsFrameUsed[i] = MPP_TRUE;

      error("------------------------------------get id %d %d, %d, %d", i,
            FRAME_GetID(context->pOutputFrame[i]), context->rFrameNum,
            context->gPacketNum);

      context->nMagicNum++;
      if (context->nMagicNum == 100) context->nMagicNum = 0;
      context->nMagicColor -= 10;
      if (context->nMagicColor < 0) context->nMagicColor = 255;

      context->rFrameNum++;

      return MPP_OK;
    }
  }

  error("------------------------------- no frame, please wait!");
  return MPP_CODER_NO_DATA;
}

S32 al_dec_return_output_frame(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) return MPP_NULL_POINTER;

  ALFakeDecContext *context = (ALFakeDecContext *)ctx;
  MppFrame *frame = FRAME_GetFrame(src_data);
  S32 ret = 0;

  if (!src_data) return -1;

  context->bIsFrameUsed[FRAME_GetID(frame)] = MPP_FALSE;
  error("------------------------------------return id %d", FRAME_GetID(frame));

  return 0;
}

void al_dec_destory(ALBaseContext *ctx) {
  ALFakeDecContext *context = (ALFakeDecContext *)ctx;
  for (S32 i = 0; i < NUM_OF_FRAMES; i++) {
    free(context->pOutputFrame[i]);
  }
}
