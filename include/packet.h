/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:11:03
 * @LastEditTime: 2023-02-03 10:56:18
 * @Description: MppPacket is the carrier the stream(data before decode or after
 * encode)
 */

#ifndef _MPP_PACKET_H_
#define _MPP_PACKET_H_

#include "data.h"
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

/***
 *     +-----------------+        +----------------+
 *     |                 |        |                |
 *     |  PACKET_Create  |        |  PACKET_Copy   |
 *     |                 |        |                |
 *     +--------+--------+        +--------+-------+
 *              |                          |
 *     +--------v--------+                 |
 *     |                 |                 |
 *     |  PACKET_Alloc   |                 |
 *     |                 |                 |
 *     +--------+--------+                 |
 *              |                          |
 *              |                          |
 *              +-------------+------------+
 *                            |
 *                            v
 *                      USE THE BUFFER
 *                            +
 *                            |
 *                 +----------v---------+
 *                 |                    |
 *                 |    PACKET_Free     |
 *                 |                    |
 *                 +----------+---------+
 *                            |
 *                            |
 *                 +----------v---------+
 *                 |                    |
 *                 |    PACKET_Destory  |
 *                 |                    |
 *                 +--------------------+
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _MppPacket MppPacket;

/**
 * @description:
 * @return {*}
 */
MppPacket *PACKET_Create();

/**
 * @description:
 * @param {MppPacket} *src_packet
 * @return {*}
 */
MppPacket *PACKET_Copy(MppPacket *src_packet);

/**
 * @description:
 * @return {*}
 */
S32 PACKET_GetStructSize();

/**
 * @description:
 * @param {MppPacket} *packet
 * @param {S32} size: total size you need, size > 0
 * @return {*}
 */
RETURN PACKET_Alloc(MppPacket *packet, S32 size);

/**
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
RETURN PACKET_Free(MppPacket *packet);

/**
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
MppData *PACKET_GetBaseData(MppPacket *packet);

/**
 * @description:
 * @param {MppData} *base_data
 * @return {*}
 */
MppPacket *PACKET_GetPacket(MppData *base_data);

/**
 * @description:
 * @param {MppPacket} *packet
 * @param {S32} length: actual length of the buffer, length >= 0 && length <=
 * totalsize
 * @return {*}
 */
RETURN PACKET_SetLength(MppPacket *packet, S32 length);

/**
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
S32 PACKET_GetLength(MppPacket *packet);

/**
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
void *PACKET_GetDataPointer(MppPacket *packet);

/**
 * @description:
 * @param {MppPacket} *packet
 * @param {U8} *data_pointer
 * @return {*}
 */
RETURN PACKET_SetDataPointer(MppPacket *packet, U8 *data_pointer);

/**
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
void *PACKET_GetMetaData(MppPacket *packet);

/**
 * @description:
 * @param {MppPacket} *packet
 * @param {void} *metadata_pointer
 * @return {*}
 */
RETURN PACKET_SetMetaData(MppPacket *packet, void *metadata_pointer);

/**
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
S32 PACKET_GetID(MppPacket *packet);

/**
 * @description:
 * @param {MppPacket} *packet
 * @param {S32} id: packet index, id >= 0
 * @return {*}
 */
RETURN PACKET_SetID(MppPacket *packet, S32 id);

/**
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
S64 PACKET_GetPts(MppPacket *packet);

/**
 * @description:
 * @param {MppPacket} *packet
 * @param {S64} pts
 * @return {*}
 */
RETURN PACKET_SetPts(MppPacket *packet, S64 pts);

/**
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
S64 PACKET_GetDts(MppPacket *packet);

/**
 * @description:
 * @param {MppPacket} *packet
 * @param {S64} dts
 * @return {*}
 */
RETURN PACKET_SetDts(MppPacket *packet, S64 dts);

/**
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
BOOL PACKET_GetEos(MppPacket *packet);

/**
 * @description:
 * @param {MppPacket} *packet
 * @param {BOOL} eos : MPP_TRUE(1) or MPP_FALSE(0)
 * @return {*}
 */
RETURN PACKET_SetEos(MppPacket *packet, BOOL eos);

/***
 * @description:
 * @param {MppPacket} *packet
 * @param {S32} width
 * @return {*}
 */
RETURN PACKET_SetWidth(MppPacket *packet, S32 width);

/***
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
S32 PACKET_GetWidth(MppPacket *packet);

/***
 * @description:
 * @param {MppPacket} *packet
 * @param {S32} height
 * @return {*}
 */
RETURN PACKET_SetHeight(MppPacket *packet, S32 height);

/***
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
S32 PACKET_GetHeight(MppPacket *packet);

/***
 * @description:
 * @param {MppPacket} *packet
 * @param {S32} line_stride
 * @return {*}
 */
RETURN PACKET_SetLineStride(MppPacket *packet, S32 line_stride);

/***
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
S32 PACKET_GetLineStride(MppPacket *packet);

/***
 * @description:
 * @param {MppPacket} *packet
 * @param {S32} pixel_format
 * @return {*}
 */
RETURN PACKET_SetPixelFormat(MppPacket *packet, S32 pixel_format);

/***
 * @description:
 * @param {MppPacket} *packet
 * @return {*}
 */
S32 PACKET_GetPixelFormat(MppPacket *packet);

/**
 * @description:
 * @return {*}
 */
void PACKET_Destory(MppPacket *packet);

#ifdef __cplusplus
};
#endif

#endif /*_MPP_PACKET_H_*/