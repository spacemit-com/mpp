/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    byte_buffer.h
 * @Brief     :    Dynamic byte buffer for stream assembly.
 *------------------------------------------------------------------------------
 */

#ifndef BYTE_BUFFER_H
#define BYTE_BUFFER_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ByteBuffer {
    U8 *pu8Data;
    U32 u32Size;
    U32 u32Capacity;
    U32 u32ReadPos;
} ByteBuffer;

void ByteBuffer_Init(ByteBuffer *pBuf);
void ByteBuffer_Destroy(ByteBuffer *pBuf);
S32 ByteBuffer_Reserve(ByteBuffer *pBuf, U32 u32Capacity);
S32 ByteBuffer_Append(ByteBuffer *pBuf, const U8 *pu8Data, U32 u32Len);
S32 ByteBuffer_Prepend(ByteBuffer *pBuf, const U8 *pu8Data, U32 u32Len);
void ByteBuffer_Clear(ByteBuffer *pBuf);
U32 ByteBuffer_GetSize(const ByteBuffer *pBuf);
U8 *ByteBuffer_GetData(ByteBuffer *pBuf);
S32 ByteBuffer_Read(ByteBuffer *pBuf, U8 *pu8Dst, U32 u32Len);
S32 ByteBuffer_Skip(ByteBuffer *pBuf, U32 u32Len);
U32 ByteBuffer_Remaining(const ByteBuffer *pBuf);

#ifdef __cplusplus
}
#endif

#endif /* __BYTE_BUFFER_H__ */
