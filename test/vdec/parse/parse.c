/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:10:10
 * @LastEditTime: 2023-11-14 17:58:28
 * @Description:
 */

#define ENABLE_DEBUG 0

#include "parse.h"

#include <stdio.h>
#include <stdlib.h>

#include "log.h"

MppParseContext* PARSE_Create(MppCodingType type) {
  MppParseContext* parse_context =
      (MppParseContext*)malloc(sizeof(MppParseContext));
  if (!parse_context) {
    error("can not malloc MppParseContext, please check!");
    return NULL;
  }

  parse_context->ops = (ParseOps*)malloc(sizeof(ParseOps));
  if (!parse_context->ops) {
    error("can not malloc MppParseContext->ops, please check!");
    free(parse_context);
    return NULL;
  }

  if (CODING_H264 == type) {
    parse_context->ops->init = &PARSE_H264_Init;
    parse_context->ops->parse = &PARSE_H264_Parse;
  } else if (CODING_H265 == type) {
    parse_context->ops->init = &PARSE_H265_Init;
    parse_context->ops->parse = &PARSE_H265_Parse;
  } else if (CODING_MJPEG == type) {
    parse_context->ops->init = &PARSE_MJPEG_Init;
    parse_context->ops->parse = &PARSE_MJPEG_Parse;
  } else {
    parse_context->ops->init = &PARSE_DEFAULT_Init;
    parse_context->ops->parse = &PARSE_DEFAULT_Parse;
  }

  return parse_context;
}

void PARSE_Destory(MppParseContext* ctx) {
  if (!ctx) return;

  if (ctx->ops) free(ctx->ops);
  free(ctx);
}