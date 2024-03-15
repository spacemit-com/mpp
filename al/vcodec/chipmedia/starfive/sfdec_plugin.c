/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-17 09:38:36
 * @LastEditTime: 2023-11-09 16:12:51
 * @Description:
 */

#define ENABLE_DEBUG 0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "al_interface_dec.h"
#include "log.h"
#include "wave511/config.h"
#include "wave511/vpuapi/vpuapi.h"
#include "wave511/vpuapi/vpuapifunc.h"
#include "wave511/vpuapi/vputypes.h"

#define MODULE_TAG "sfdec"

CODING_TYPE_MAPPING_DEFINE(SfDec, CodStd)
static const ALSfDecCodingTypeMapping stALSfDecCodingTypeMapping[] = {
    {CODING_H264, STD_AVC},
    {CODING_H265, STD_HEVC},
    {
        CODING_MJPEG,
    },
    {CODING_VP8, STD_VP8},
    {CODING_VP9, STD_VP9},
    {CODING_AV1, STD_AV1},
    {CODING_AVS, STD_AVS},
    {CODING_AVS2, STD_AVS2},
    {
        CODING_MPEG1,
    },
    {CODING_MPEG2, STD_MPEG2},
    {CODING_MPEG4, STD_MPEG4},
};
CODING_TYPE_MAPPING_CONVERT(SfDec, sfdec, CodStd)

typedef struct _ALSfDecContext ALSfDecContext;

struct _ALSfDecContext {
  ALDecBaseContext stAlDecBaseContext;
};

ALBaseContext *al_dec_create() {
  ALSfDecContext *context = (ALSfDecContext *)malloc(sizeof(ALSfDecContext));
  if (!context) return NULL;

  return &(context->stAlDecBaseContext.stAlBaseContext);
}

RETURN al_dec_init(ALBaseContext *ctx, MppVdecPara *para) {
  if (!ctx) return MPP_NULL_POINTER;

  ALSfDecContext *context = (ALSfDecContext *)ctx;

  debug("init finish");

  return MPP_OK;
}

S32 al_dec_decode(ALBaseContext *ctx, MppData *sink_data) {
  if (!ctx) return MPP_NULL_POINTER;

  ALSfDecContext *context = (ALSfDecContext *)ctx;

  S32 ret = 0;

  return 0;
}

S32 al_dec_request_output_frame(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) return MPP_NULL_POINTER;

  ALSfDecContext *context = (ALSfDecContext *)ctx;

  S32 ret = 0;

  return 0;
}

S32 al_dec_return_output_frame(ALBaseContext *ctx, MppData *src_data) {
  if (!ctx) return MPP_NULL_POINTER;

  ALSfDecContext *context = (ALSfDecContext *)ctx;

  S32 ret = 0;

  return 0;
}

void al_dec_destory(ALBaseContext *ctx) {}
