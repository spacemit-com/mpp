/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "amf0.h"
#include <string.h>

static void WriteU16BE(U8 *p, U16 val) {
    p[0] = (val >> 8) & 0xFF;
    p[1] = val & 0xFF;
}

static void WriteU32BE(U8 *p, U32 val) {
    p[0] = (val >> 24) & 0xFF;
    p[1] = (val >> 16) & 0xFF;
    p[2] = (val >> 8) & 0xFF;
    p[3] = val & 0xFF;
}

static void WriteDouble(U8 *p, double val) {
    union {
        double d;
        U8 bytes[8];
    } u;
    u.d = val;
    /* Big-endian */
    for (int i = 0; i < 8; i++) {
        p[i] = u.bytes[7 - i];
    }
}

static U16 ReadU16BE(const U8 *p) { return ((U16)p[0] << 8) | p[1]; }

static double ReadDouble(const U8 *p) {
    union {
        double d;
        U8 bytes[8];
    } u;
    for (int i = 0; i < 8; i++) {
        u.bytes[7 - i] = p[i];
    }
    return u.d;
}

S32 Amf0_WriteNumber(U8 *pu8Buf, U32 u32BufSize, double dVal) {
    if (!pu8Buf || u32BufSize < 9)
        return -1;
    pu8Buf[0] = AMF0_TYPE_NUMBER;
    WriteDouble(&pu8Buf[1], dVal);
    return 9;
}

S32 Amf0_WriteBoolean(U8 *pu8Buf, U32 u32BufSize, BOOL bVal) {
    if (!pu8Buf || u32BufSize < 2)
        return -1;
    pu8Buf[0] = AMF0_TYPE_BOOLEAN;
    pu8Buf[1] = bVal ? 1 : 0;
    return 2;
}

S32 Amf0_WriteString(U8 *pu8Buf, U32 u32BufSize, const char *pStr) {
    if (!pu8Buf || !pStr)
        return -1;
    U32 len = (U32)strlen(pStr);

    if (len > 0xFFFF) {
        /* Long string */
        if (u32BufSize < 5 + len)
            return -1;
        pu8Buf[0] = AMF0_TYPE_LONG_STRING;
        WriteU32BE(&pu8Buf[1], len);
        memcpy(&pu8Buf[5], pStr, len);
        return 5 + len;
    } else {
        if (u32BufSize < 3 + len)
            return -1;
        pu8Buf[0] = AMF0_TYPE_STRING;
        WriteU16BE(&pu8Buf[1], (U16)len);
        memcpy(&pu8Buf[3], pStr, len);
        return 3 + len;
    }
}

S32 Amf0_WriteNull(U8 *pu8Buf, U32 u32BufSize) {
    if (!pu8Buf || u32BufSize < 1)
        return -1;
    pu8Buf[0] = AMF0_TYPE_NULL;
    return 1;
}

S32 Amf0_WriteObjectStart(U8 *pu8Buf, U32 u32BufSize) {
    if (!pu8Buf || u32BufSize < 1)
        return -1;
    pu8Buf[0] = AMF0_TYPE_OBJECT;
    return 1;
}

S32 Amf0_WriteObjectEnd(U8 *pu8Buf, U32 u32BufSize) {
    if (!pu8Buf || u32BufSize < 3)
        return -1;
    pu8Buf[0] = 0x00;
    pu8Buf[1] = 0x00;
    pu8Buf[2] = AMF0_TYPE_OBJECT_END;
    return 3;
}

S32 Amf0_WriteProperty(U8 *pu8Buf, U32 u32BufSize, const char *pName, const Amf0Value *pVal) {
    if (!pu8Buf || !pName || !pVal)
        return -1;

    U32 nameLen = (U32)strlen(pName);
    if (u32BufSize < 2 + nameLen)
        return -1;

    /* Property name (without type marker) */
    WriteU16BE(pu8Buf, (U16)nameLen);
    memcpy(&pu8Buf[2], pName, nameLen);

    S32 total = 2 + nameLen;
    S32 valLen = 0;

    switch (pVal->u8Type) {
    case AMF0_TYPE_NUMBER:
        valLen = Amf0_WriteNumber(&pu8Buf[total], u32BufSize - total, pVal->uData.dNumber);
        break;
    case AMF0_TYPE_BOOLEAN:
        valLen = Amf0_WriteBoolean(&pu8Buf[total], u32BufSize - total, pVal->uData.bBool);
        break;
    case AMF0_TYPE_STRING:
        valLen = Amf0_WriteString(&pu8Buf[total], u32BufSize - total, pVal->uData.stString.pStr);
        break;
    case AMF0_TYPE_NULL:
        valLen = Amf0_WriteNull(&pu8Buf[total], u32BufSize - total);
        break;
    default:
        return -1;
    }

    if (valLen < 0)
        return -1;
    return total + valLen;
}

S32 Amf0_ReadNumber(const U8 *pu8Data, U32 u32Len, double *pdVal) {
    if (!pu8Data || u32Len < 9 || !pdVal)
        return -1;
    if (pu8Data[0] != AMF0_TYPE_NUMBER)
        return -1;
    *pdVal = ReadDouble(&pu8Data[1]);
    return 9;
}

S32 Amf0_ReadBoolean(const U8 *pu8Data, U32 u32Len, BOOL *pbVal) {
    if (!pu8Data || u32Len < 2 || !pbVal)
        return -1;
    if (pu8Data[0] != AMF0_TYPE_BOOLEAN)
        return -1;
    *pbVal = pu8Data[1] != 0;
    return 2;
}

S32 Amf0_ReadString(const U8 *pu8Data, U32 u32Len, char *pBuf, U32 u32BufSize, U32 *pu32StrLen) {
    if (!pu8Data || u32Len < 3)
        return -1;
    if (pu8Data[0] != AMF0_TYPE_STRING)
        return -1;

    U16 strLen = ReadU16BE(&pu8Data[1]);
    if (u32Len < 3 + (U32)strLen)
        return -1;

    if (pBuf && u32BufSize > 0) {
        U32 copyLen = (strLen < u32BufSize - 1) ? strLen : u32BufSize - 1;
        memcpy(pBuf, &pu8Data[3], copyLen);
        pBuf[copyLen] = '\0';
    }
    if (pu32StrLen)
        *pu32StrLen = strLen;

    return 3 + strLen;
}

S32 Amf0_ReadValue(const U8 *pu8Data, U32 u32Len, Amf0Value *pVal, U32 *pu32Consumed) {
    if (!pu8Data || u32Len < 1 || !pVal)
        return -1;

    pVal->u8Type = pu8Data[0];
    S32 consumed = 0;

    switch (pVal->u8Type) {
    case AMF0_TYPE_NUMBER:
        if (u32Len < 9)
            return -1;
        pVal->uData.dNumber = ReadDouble(&pu8Data[1]);
        consumed = 9;
        break;
    case AMF0_TYPE_BOOLEAN:
        if (u32Len < 2)
            return -1;
        pVal->uData.bBool = pu8Data[1] != 0;
        consumed = 2;
        break;
    case AMF0_TYPE_STRING: {
        if (u32Len < 3)
            return -1;
        U16 strLen = ReadU16BE(&pu8Data[1]);
        if (u32Len < 3 + (U32)strLen)
            return -1;
        pVal->uData.stString.pStr = (char *)&pu8Data[3];
        pVal->uData.stString.u32Len = strLen;
        consumed = 3 + strLen;
        break;
    }
    case AMF0_TYPE_NULL:
    case AMF0_TYPE_UNDEFINED:
        consumed = 1;
        break;
    default:
        return -1;
    }

    if (pu32Consumed)
        *pu32Consumed = consumed;
    return 0;
}
