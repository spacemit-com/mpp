/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "byte_buffer.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 1024

void ByteBuffer_Init(ByteBuffer *pBuf) {
    if (!pBuf)
        return;
    memset(pBuf, 0, sizeof(ByteBuffer));
}

void ByteBuffer_Destroy(ByteBuffer *pBuf) {
    if (!pBuf)
        return;
    if (pBuf->pu8Data) {
        free(pBuf->pu8Data);
    }
    memset(pBuf, 0, sizeof(ByteBuffer));
}

S32 ByteBuffer_Reserve(ByteBuffer *pBuf, U32 u32Capacity) {
    if (!pBuf)
        return -1;
    if (u32Capacity <= pBuf->u32Capacity)
        return 0;

    U32 newCap = pBuf->u32Capacity ? pBuf->u32Capacity : INITIAL_CAPACITY;
    while (newCap < u32Capacity) {
        newCap *= 2;
    }

    U8 *pNew = (U8 *)realloc(pBuf->pu8Data, newCap);
    if (!pNew)
        return -1;

    pBuf->pu8Data = pNew;
    pBuf->u32Capacity = newCap;
    return 0;
}

S32 ByteBuffer_Append(ByteBuffer *pBuf, const U8 *pu8Data, U32 u32Len) {
    if (!pBuf || !pu8Data || u32Len == 0)
        return -1;

    if (ByteBuffer_Reserve(pBuf, pBuf->u32Size + u32Len) != 0) {
        return -1;
    }

    memcpy(pBuf->pu8Data + pBuf->u32Size, pu8Data, u32Len);
    pBuf->u32Size += u32Len;
    return 0;
}

S32 ByteBuffer_Prepend(ByteBuffer *pBuf, const U8 *pu8Data, U32 u32Len) {
    if (!pBuf || !pu8Data || u32Len == 0)
        return -1;

    if (ByteBuffer_Reserve(pBuf, pBuf->u32Size + u32Len) != 0) {
        return -1;
    }

    memmove(pBuf->pu8Data + u32Len, pBuf->pu8Data, pBuf->u32Size);
    memcpy(pBuf->pu8Data, pu8Data, u32Len);
    pBuf->u32Size += u32Len;
    return 0;
}

void ByteBuffer_Clear(ByteBuffer *pBuf) {
    if (!pBuf)
        return;
    pBuf->u32Size = 0;
    pBuf->u32ReadPos = 0;
}

U32 ByteBuffer_GetSize(const ByteBuffer *pBuf) { return pBuf ? pBuf->u32Size : 0; }

U8 *ByteBuffer_GetData(ByteBuffer *pBuf) { return pBuf ? pBuf->pu8Data : NULL; }

S32 ByteBuffer_Read(ByteBuffer *pBuf, U8 *pu8Dst, U32 u32Len) {
    if (!pBuf || !pu8Dst)
        return -1;

    U32 avail = ByteBuffer_Remaining(pBuf);
    if (u32Len > avail)
        u32Len = avail;
    if (u32Len == 0)
        return 0;

    memcpy(pu8Dst, pBuf->pu8Data + pBuf->u32ReadPos, u32Len);
    pBuf->u32ReadPos += u32Len;
    return (S32)u32Len;
}

S32 ByteBuffer_Skip(ByteBuffer *pBuf, U32 u32Len) {
    if (!pBuf)
        return -1;

    U32 avail = ByteBuffer_Remaining(pBuf);
    if (u32Len > avail)
        u32Len = avail;
    pBuf->u32ReadPos += u32Len;
    return (S32)u32Len;
}

U32 ByteBuffer_Remaining(const ByteBuffer *pBuf) {
    if (!pBuf)
        return 0;
    return pBuf->u32Size - pBuf->u32ReadPos;
}
