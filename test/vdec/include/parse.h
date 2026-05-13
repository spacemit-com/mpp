/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:11:03
 * @LastEditTime: 2024-04-29 19:26:05
 * @Description:
 */

#ifndef _MPP_PARSE_H_
#define _MPP_PARSE_H_

#include "para.h"

#define STREAM_BUFFER_SIZE (5 * 1024 * 1024)

typedef struct _MppParseContext MppParseContext;

typedef struct _ParseOps {
  S32 (*init)(MppParseContext *base_context);
  S32(*parse)
  (MppParseContext *base_context, U8 *stream_start_addr, S32 stream_size,
   U8 *frame_start_addr, S32 *frame_size, S32 is_first_seq_header);
} ParseOps;

struct _MppParseContext {
  ParseOps *ops;
};

/**
 * @description:
 * @param {MppCodingType} type
 * @return {*}
 */
MppParseContext *PARSE_Create(MppCodingType type);

/***
 * @description:
 * @param {MppParseContext} *ctx
 * @return {*}
 */
void PARSE_Destory(MppParseContext *ctx);

/**
 * @description:
 * @param {MppParseContext} *ctx
 * @return {*}
 */
S32 PARSE_H264_Init(MppParseContext *ctx);

/**
 * @description:
 * @param {MppParseContext} *ctx
 * @param {U8  } *stream_start_addr
 * @param {S32 } stream_size
 * @param {U8  } *frame_start_addr
 * @param {S32 } *frame_size
 * @param {S32 } is_first_seq_header
 * @return {*}
 */
S32 PARSE_H264_Parse(MppParseContext *ctx, U8 *stream_start_addr,
                     S32 stream_size, U8 *frame_start_addr, S32 *frame_size,
                     S32 is_first_seq_header);

/**
 * @description:
 * @param {MppParseContext} *ctx
 * @return {*}
 */
S32 PARSE_H265_Init(MppParseContext *ctx);

/**
 * @description:
 * @param {MppParseContext} *ctx
 * @param {U8  } *stream_start_addr
 * @param {S32 } stream_size
 * @param {U8  } *frame_start_addr
 * @param {S32 } *frame_size
 * @param {S32 } is_first_seq_header
 * @return {*}
 */
S32 PARSE_H265_Parse(MppParseContext *ctx, U8 *stream_start_addr,
                     S32 stream_size, U8 *frame_start_addr, S32 *frame_size,
                     S32 is_first_seq_header);

/**
 * @description:
 * @param {MppParseContext} *ctx
 * @return {*}
 */
S32 PARSE_MJPEG_Init(MppParseContext *ctx);

/**
 * @description:
 * @param {MppParseContext} *ctx
 * @param {U8  } *stream_start_addr
 * @param {S32 } stream_size
 * @param {U8  } *frame_start_addr
 * @param {S32 } *frame_size
 * @param {S32 } is_first_seq_header
 * @return {*}
 */
S32 PARSE_MJPEG_Parse(MppParseContext *ctx, U8 *stream_start_addr,
                      S32 stream_size, U8 *frame_start_addr, S32 *frame_size,
                      S32 is_first_seq_header);

/**
 * @description:
 * @param {MppParseContext} *ctx
 * @return {*}
 */
S32 PARSE_DEFAULT_Init(MppParseContext *ctx);

/**
 * @description:
 * @param {MppParseContext} *ctx
 * @param {U8  } *stream_start_addr
 * @param {S32 } stream_size
 * @param {U8  } *frame_start_addr
 * @param {S32 } *frame_size
 * @param {S32 } is_first_seq_header
 * @return {*}
 */
S32 PARSE_DEFAULT_Parse(MppParseContext *ctx, U8 *stream_start_addr,
                        S32 stream_size, U8 *frame_start_addr, S32 *frame_size,
                        S32 is_first_seq_header);
#endif /*_MPP_PARSE_H_*/