/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    amf0.h
 * @Brief     :    AMF0 (Action Message Format 0) encoding/decoding.
 *------------------------------------------------------------------------------
 */

#ifndef AMF0_H
#define AMF0_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AMF0 data types */
#define AMF0_TYPE_NUMBER 0x00
#define AMF0_TYPE_BOOLEAN 0x01
#define AMF0_TYPE_STRING 0x02
#define AMF0_TYPE_OBJECT 0x03
#define AMF0_TYPE_NULL 0x05
#define AMF0_TYPE_UNDEFINED 0x06
#define AMF0_TYPE_ECMA_ARRAY 0x08
#define AMF0_TYPE_OBJECT_END 0x09
#define AMF0_TYPE_STRICT_ARRAY 0x0A
#define AMF0_TYPE_DATE 0x0B
#define AMF0_TYPE_LONG_STRING 0x0C

typedef struct _Amf0Value {
    U8 u8Type;
    union {
        double dNumber;
        BOOL bBool;
        struct {
            char *pStr;
            U32 u32Len;
        } stString;
    } uData;
} Amf0Value;

/* Encoding functions */
S32 Amf0_WriteNumber(U8 *pu8Buf, U32 u32BufSize, double dVal);
S32 Amf0_WriteBoolean(U8 *pu8Buf, U32 u32BufSize, BOOL bVal);
S32 Amf0_WriteString(U8 *pu8Buf, U32 u32BufSize, const char *pStr);
S32 Amf0_WriteNull(U8 *pu8Buf, U32 u32BufSize);
S32 Amf0_WriteObjectStart(U8 *pu8Buf, U32 u32BufSize);
S32 Amf0_WriteObjectEnd(U8 *pu8Buf, U32 u32BufSize);
S32 Amf0_WriteProperty(U8 *pu8Buf, U32 u32BufSize, const char *pName, const Amf0Value *pVal);

/* Decoding functions */
S32 Amf0_ReadNumber(const U8 *pu8Data, U32 u32Len, double *pdVal);
S32 Amf0_ReadBoolean(const U8 *pu8Data, U32 u32Len, BOOL *pbVal);
S32 Amf0_ReadString(const U8 *pu8Data, U32 u32Len, char *pBuf, U32 u32BufSize, U32 *pu32StrLen);
S32 Amf0_ReadValue(const U8 *pu8Data, U32 u32Len, Amf0Value *pVal, U32 *pu32Consumed);

#ifdef __cplusplus
}
#endif

#endif /* __AMF0_H__ */
