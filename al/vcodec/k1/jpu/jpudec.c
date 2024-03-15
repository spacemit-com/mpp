/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:36
 * @LastEditTime: 2023-12-13 20:13:38
 * @Description: video decode plugin for K1 JPU
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

#define MODULE_TAG "jpu"

typedef struct _ALK1JpuDecContext ALK1JpuDecContext;

struct _ALK1JpuDecContext {
  ALDecBaseContext stAlDecBaseContext;
  MppVdecPara *pVdecPara;
  MppCodingType eCodingType;
};

ALBaseContext *al_dec_create() {
  ALK1JpuDecContext *context =
      (ALK1JpuDecContext *)malloc(sizeof(ALK1JpuDecContext));
  if (!context) return NULL;

  return &(context->stAlDecBaseContext.stAlBaseContext);
}

RETURN al_dec_init(ALBaseContext *ctx, MppVdecPara *para) {
  if (!ctx || !para) return MPP_NULL_POINTER;

  ALK1JpuDecContext *context = (ALK1JpuDecContext *)ctx;
  S32 ret = 0;

  context->pVdecPara = para;

  debug("init finish");

  return MPP_OK;
}

S32 al_dec_decode(ALBaseContext *ctx, MppData *sink_data) {
  ALK1JpuDecContext *context;

  return 0;
}

S32 al_dec_request_output_frame(ALBaseContext *ctx, MppData *src_data) {
  ALK1JpuDecContext *context;
  MppDataQueueNode *node;
  static U32 count = 0;

  if (!ctx) return MPP_NULL_POINTER;

  return MPP_OK;
}

S32 al_dec_return_output_frame(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) return MPP_NULL_POINTER;

  ALK1JpuDecContext *context = (ALK1JpuDecContext *)ctx;

  return MPP_OK;
}

void al_dec_destory(ALBaseContext *ctx) {
  ALK1JpuDecContext *context = (ALK1JpuDecContext *)ctx;
}
