/*
* Copyright 2022-2023 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @Description: MppFrame - carrier for decoded/pre-encode frame data
*/

#ifndef FRAME_H
#define FRAME_H

#include "data.h"
#include "dmabufwrapper.h"
#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MPP_MAX_PLANES 4

typedef struct _MppFrame MppFrame;

MppFrame *FRAME_Create();
S32 FRAME_GetStructSize();
RETURN FRAME_Alloc(MppFrame *frame, MppPixelFormat pixelformat, S32 width,
    S32 height);
RETURN FRAME_Free(MppFrame *frame);
RETURN FRAME_SetDataUsedNum(MppFrame *frame, S32 data_used_num);
S32 FRAME_GetDataUsedNum(MppFrame *frame);
MppData *FRAME_GetBaseData(MppFrame *frame);
MppFrame *FRAME_GetFrame(MppData *base_data);
void *FRAME_GetDataPointer(MppFrame *frame, S32 data_num);
RETURN FRAME_SetDataPointer(MppFrame *frame, S32 data_num, U8 *data);
void *FRAME_GetMetaData(MppFrame *frame);
RETURN FRAME_SetMetaData(MppFrame *frame, void *meta_data);
U32 FRAME_Ref(MppFrame *frame);
U32 FRAME_UnRef(MppFrame *frame);
U32 FRAME_GetRef(MppFrame *frame);
S32 FRAME_GetID(MppFrame *frame);
RETURN FRAME_SetID(MppFrame *frame, S32 id);
S64 FRAME_GetPts(MppFrame *frame);
RETURN FRAME_SetPts(MppFrame *frame, S64 pts);
S32 FRAME_GetEos(MppFrame *frame);
S32 FRAME_SetEos(MppFrame *frame, MppFrameEos eos);
RETURN FRAME_SetWidth(MppFrame *frame, S32 width);
S32 FRAME_GetWidth(MppFrame *frame);
RETURN FRAME_SetHeight(MppFrame *frame, S32 height);
S32 FRAME_GetHeight(MppFrame *frame);
RETURN FRAME_SetLineStride(MppFrame *frame, S32 line_stride);
S32 FRAME_GetLineStride(MppFrame *frame);
RETURN FRAME_SetPixelFormat(MppFrame *frame, S32 pixel_format);
S32 FRAME_GetPixelFormat(MppFrame *frame);
S32 FRAME_GetFD(MppFrame *frame, S32 index);
RETURN FRAME_SetFD(MppFrame *frame, S32 fd, S32 index);
S32 FRAME_GetBufferType(MppFrame *frame);
RETURN FRAME_SetBufferType(MppFrame *frame, MppFrameBufferType eBufferType);
void FRAME_Destory(MppFrame *frame);

#ifdef __cplusplus
};
#endif

#endif /* FRAME_H */
