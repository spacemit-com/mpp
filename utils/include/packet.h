/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: MppPacket - carrier for stream data (before decode / after encode)
 */

#ifndef PACKET_H
#define PACKET_H

#include "data.h"
#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _MppPacket MppPacket;

MppPacket *PACKET_Create();
MppPacket *PACKET_Copy(MppPacket *src_packet);
S32 PACKET_GetStructSize();
RETURN PACKET_Alloc(MppPacket *packet, S32 size);
RETURN PACKET_Free(MppPacket *packet);
MppData *PACKET_GetBaseData(MppPacket *packet);
MppPacket *PACKET_GetPacket(MppData *base_data);
RETURN PACKET_SetLength(MppPacket *packet, S32 length);
S32 PACKET_GetLength(MppPacket *packet);
void *PACKET_GetDataPointer(MppPacket *packet);
RETURN PACKET_SetDataPointer(MppPacket *packet, U8 *data_pointer);
void *PACKET_GetMetaData(MppPacket *packet);
RETURN PACKET_SetMetaData(MppPacket *packet, void *metadata_pointer);
S32 PACKET_GetID(MppPacket *packet);
RETURN PACKET_SetID(MppPacket *packet, S32 id);
S64 PACKET_GetPts(MppPacket *packet);
RETURN PACKET_SetPts(MppPacket *packet, S64 pts);
S64 PACKET_GetDts(MppPacket *packet);
RETURN PACKET_SetDts(MppPacket *packet, S64 dts);
BOOL PACKET_GetEos(MppPacket *packet);
RETURN PACKET_SetEos(MppPacket *packet, BOOL eos);
RETURN PACKET_SetWidth(MppPacket *packet, S32 width);
S32 PACKET_GetWidth(MppPacket *packet);
RETURN PACKET_SetHeight(MppPacket *packet, S32 height);
S32 PACKET_GetHeight(MppPacket *packet);
RETURN PACKET_SetLineStride(MppPacket *packet, S32 line_stride);
S32 PACKET_GetLineStride(MppPacket *packet);
RETURN PACKET_SetPixelFormat(MppPacket *packet, S32 pixel_format);
S32 PACKET_GetPixelFormat(MppPacket *packet);
void PACKET_Destory(MppPacket *packet);

#ifdef __cplusplus
};
#endif

#endif /*PACKET_H*/
