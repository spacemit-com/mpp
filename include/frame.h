/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:11:03
 * @LastEditTime: 2024-04-03 15:06:54
 * @Description: MppFrame is the carrier the frame(data before encode or after
 * decode)
 */

#ifndef _MPP_FRAME_H_
#define _MPP_FRAME_H_

#include "data.h"
#include "dmabufwrapper.h"
#include "type.h"

/*
 *                  +------------------------+
 *                  |       MppData          |
 *                  +------------------------+
 *                  |   nWidth               |
 *                  |   nHeight              |
 *                  |   nLineStride          |
 *                  |   nPts                 |
 *                  |   ePixelFormat         |
 *                  +-----------^------------+
 *                              |
 *            +-----------------+---------------+
 *            |                                 |
 * +----------+-------------+       +-----------+-----------+
 * |       MppPacket        |       |       MppFrame        |
 * +------------------------+       +-----------------------+
 * |   eBaseData            |       |   eBaseData           |
 * |   pData                |       |   nDataUsedNum        |
 * |   nLength              |       |   pData0              |
 * |                        |       |   pData1              |
 * |                        |       |   pData2              |
 * +------------------------+       +-----------------------+
 *
 */

/*
 *            +-----------------+
 *            |                 |
 *            |   FRAME_Create  |
 *            |                 |
 *            +--------+--------+
 *                     |
 *            +--------v--------+
 *            |                 |
 *            |   Frame_Alloc   |
 *            |                 |
 *            +--------+--------+
 *                     |
 *                     v
 *               USE THE BUFFER
 *     +---------------------------------+
 *     |                                 |
 *     |      +-----------------+        |
 *     |      |                 |        |
 *     |      |   FRAME_Ref     <-----+  |
 *     |      |                 |        |
 *     |      +-----------------+        |
 *     |                                 |
 *     |                                 |
 *     |      +-----------------+        |
 *     |      |                 |        |
 *     |      |   FRAME_UnRef   <-----+  |
 *     |      |                 |        |
 *     |      +--------+--------+        |
 *     |               |                 |
 *     +---------------------------------+
 *                     |
 *                     |
 *            +--------v--------+
 *            |                 |
 *            |   FRAME_Free    |
 *            |                 |
 *            +--------+--------+
 *                     |
 *                     |
 *            +--------v--------+
 *            |                 |
 *            |   FRAME_Destory |
 *            |                 |
 *            +-----------------+
 */

#ifdef __cplusplus
extern "C" {
#endif

#define MPP_MAX_PLANES 4

typedef struct _MppFrame MppFrame;

/**
 * @description:
 * @return {*}
 */
MppFrame *FRAME_Create();

/**
 * @description:
 * @return {*}
 */
S32 FRAME_GetStructSize();

/**
 * @description:
 * @param {MppFrame} *frame
 * @param {S32} pixelformat
 * @param {S32} width
 * @param {S32} height
 * @return {*}
 */
RETURN FRAME_Alloc(MppFrame *frame, MppPixelFormat pixelformat, S32 width,
                   S32 height);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
RETURN FRAME_Free(MppFrame *frame);

/**
 * @description:
 * @param {MppFrame} *frame
 * @param {S32} data_used_num
 * @return {*}
 */
RETURN FRAME_SetDataUsedNum(MppFrame *frame, S32 data_used_num);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
S32 FRAME_GetDataUsedNum(MppFrame *frame);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
MppData *FRAME_GetBaseData(MppFrame *frame);

/**
 * @description:
 * @param {MppData} *base_data
 * @return {*}
 */
MppFrame *FRAME_GetFrame(MppData *base_data);

/**
 * @description:
 * @param {MppFrame} *frame
 * @param {S32} data_num
 * @return {*}
 */
void *FRAME_GetDataPointer(MppFrame *frame, S32 data_num);

/**
 * @description:
 * @param {MppFrame} *frame
 * @param {S32} data_num
 * @param {U8} *data
 * @return {*}
 */
RETURN FRAME_SetDataPointer(MppFrame *frame, S32 data_num, U8 *data);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
void *FRAME_GetMetaData(MppFrame *frame);

/**
 * @description:
 * @param {MppFrame} *frame
 * @param {void} *meta_data
 * @return {*}
 */
RETURN FRAME_SetMetaData(MppFrame *frame, void *meta_data);

/***
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
U32 FRAME_Ref(MppFrame *frame);

/***
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
U32 FRAME_UnRef(MppFrame *frame);

/***
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
U32 FRAME_GetRef(MppFrame *frame);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
S32 FRAME_GetID(MppFrame *frame);

/**
 * @description:
 * @param {MppFrame} *frame
 * @param {S32} id
 * @return {*}
 */
RETURN FRAME_SetID(MppFrame *frame, S32 id);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
S64 FRAME_GetPts(MppFrame *frame);

/**
 * @description:
 * @param {MppFrame} *frame
 * @param {S64} pts
 * @return {*}
 */
RETURN FRAME_SetPts(MppFrame *frame, S64 pts);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
MppFrameEos FRAME_GetEos(MppFrame *frame);

/**
 * @description:
 * @param {MppFrame} *frame
 * @param {BOOL} eos
 * @return {*}
 */
S32 FRAME_SetEos(MppFrame *frame, MppFrameEos eos);

/***
 * @description:
 * @param {MppFrame} *frame
 * @param {S32} width
 * @return {*}
 */
RETURN FRAME_SetWidth(MppFrame *frame, S32 width);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
S32 FRAME_GetWidth(MppFrame *frame);

/***
 * @description:
 * @param {MppFrame} *frame
 * @param {S32} height
 * @return {*}
 */
RETURN FRAME_SetHeight(MppFrame *frame, S32 height);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
S32 FRAME_GetHeight(MppFrame *frame);

/***
 * @description:
 * @param {MppFrame} *frame
 * @param {S32} line_stride
 * @return {*}
 */
RETURN FRAME_SetLineStride(MppFrame *frame, S32 line_stride);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
S32 FRAME_GetLineStride(MppFrame *frame);

/***
 * @description:
 * @param {MppFrame} *frame
 * @param {S32} pixel_format
 * @return {*}
 */
RETURN FRAME_SetPixelFormat(MppFrame *frame, S32 pixel_format);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
S32 FRAME_GetPixelFormat(MppFrame *frame);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
S32 FRAME_GetFD(MppFrame *frame, S32 index);

/**
 * @description:
 * @param {MppFrame} *frame
 * @param {S32} fd
 * @return {*}
 */
RETURN FRAME_SetFD(MppFrame *frame, S32 fd, S32 index);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
MppFrameBufferType FRAME_GetBufferType(MppFrame *frame);

/**
 * @description:
 * @param {MppFrame} *frame
 * @param {MppFrameBufferType} eBufferType
 * @return {*}
 */
RETURN FRAME_SetBufferType(MppFrame *frame, MppFrameBufferType eBufferType);

/**
 * @description:
 * @param {MppFrame} *frame
 * @return {*}
 */
void FRAME_Destory(MppFrame *frame);

#ifdef __cplusplus
};
#endif

#endif /*_MPP_FRAME_H_*/
