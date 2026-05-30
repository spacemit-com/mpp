/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "mp4_box.h"
#include <string.h>

static U32 ReadU32BE(const U8 *p) { return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | p[3]; }

static U16 ReadU16BE(const U8 *p) { return ((U16)p[0] << 8) | p[1]; }

static U64 ReadU64BE(const U8 *p) {
    U64 hi = ReadU32BE(p);
    U64 lo = ReadU32BE(p + 4);
    return (hi << 32) | lo;
}

S32 Mp4Box_ReadHeader(const U8 *pu8Data, U32 u32Len, Mp4BoxHeader *pHeader) {
    if (!pu8Data || u32Len < 8 || !pHeader)
        return -1;

    memset(pHeader, 0, sizeof(Mp4BoxHeader));

    pHeader->u32Size = ReadU32BE(pu8Data);
    pHeader->u32Type = ReadU32BE(pu8Data + 4);

    if (pHeader->u32Size == 1) {
        /* 64-bit extended size: an 8-byte largesize field follows the
         * 8-byte box header, so the header itself spans 16 bytes. */
        if (u32Len < 16)
            return -1;
        pHeader->u64LargeSize = ReadU64BE(pu8Data + 8);
        /* The box must at least contain its own 16-byte extended header. */
        if (pHeader->u64LargeSize < 16)
            return -1;
    } else if (pHeader->u32Size == 0) {
        /* Box extends to EOF */
        pHeader->u64LargeSize = 0;
    } else {
        /* A 32-bit-sized box must at least contain its own 8-byte header;
         * sizes 1..7 are impossible and would stall/overflow the caller's
         * box-walking loop, so reject them as corrupt. */
        if (pHeader->u32Size < 8)
            return -1;
        pHeader->u64LargeSize = pHeader->u32Size;
    }

    return 0;
}

void Mp4Box_TypeToString(U32 u32Type, char *pStr) {
    if (!pStr)
        return;
    pStr[0] = (u32Type >> 24) & 0xFF;
    pStr[1] = (u32Type >> 16) & 0xFF;
    pStr[2] = (u32Type >> 8) & 0xFF;
    pStr[3] = u32Type & 0xFF;
    pStr[4] = '\0';
}

S32 Mp4Box_ParseAvcC(const U8 *pu8Data, U32 u32Len, AvcDecoderConfig *pConfig) {
    if (!pu8Data || u32Len < 7 || !pConfig)
        return -1;

    memset(pConfig, 0, sizeof(AvcDecoderConfig));

    pConfig->u8ConfigVersion = pu8Data[0];
    pConfig->u8ProfileIdc = pu8Data[1];
    pConfig->u8ProfileCompat = pu8Data[2];
    pConfig->u8LevelIdc = pu8Data[3];
    pConfig->u8NaluLengthSize = (pu8Data[4] & 0x03) + 1;

    U32 offset = 5;

    /* SPS */
    pConfig->u8NumSps = pu8Data[offset++] & 0x1F;
    if (pConfig->u8NumSps > 0) {
        if (offset + 2 > u32Len)
            return -1; /* truncated: no room for SPS length field */
        pConfig->u16SpsLen = ReadU16BE(&pu8Data[offset]);
        offset += 2;
        if (offset + pConfig->u16SpsLen > u32Len)
            return -1; /* truncated: SPS payload exceeds buffer */
        pConfig->pu8Sps = (U8 *)&pu8Data[offset];
        offset += pConfig->u16SpsLen;
    }

    /* PPS */
    if (offset < u32Len) {
        pConfig->u8NumPps = pu8Data[offset++];
        if (pConfig->u8NumPps > 0) {
            if (offset + 2 > u32Len)
                return -1; /* truncated: no room for PPS length field */
            pConfig->u16PpsLen = ReadU16BE(&pu8Data[offset]);
            offset += 2;
            if (offset + pConfig->u16PpsLen > u32Len)
                return -1; /* truncated: PPS payload exceeds buffer */
            pConfig->pu8Pps = (U8 *)&pu8Data[offset];
        }
    }

    return 0;
}

S32 Mp4Box_ParseHvcC(const U8 *pu8Data, U32 u32Len, HevcDecoderConfig *pConfig) {
    if (!pu8Data || u32Len < 23 || !pConfig)
        return -1;

    memset(pConfig, 0, sizeof(HevcDecoderConfig));

    pConfig->u8ConfigVersion = pu8Data[0];
    pConfig->u8GeneralProfileIdc = pu8Data[1] & 0x1F;
    pConfig->u8GeneralLevelIdc = pu8Data[12];
    pConfig->u8NaluLengthSize = (pu8Data[21] & 0x03) + 1;
    pConfig->u8NumArrays = pu8Data[22];

    U32 offset = 23;

    /* Parse arrays (VPS, SPS, PPS) */
    for (U8 i = 0; i < pConfig->u8NumArrays; i++) {
        if (offset + 3 > u32Len)
            return -1; /* truncated: array header doesn't fit */
        U8 naluType = pu8Data[offset] & 0x3F;
        U16 numNalus = ReadU16BE(&pu8Data[offset + 1]);
        offset += 3;

        for (U16 j = 0; j < numNalus; j++) {
            if (offset + 2 > u32Len)
                return -1; /* truncated: NAL length field doesn't fit */
            U16 naluLen = ReadU16BE(&pu8Data[offset]);
            offset += 2;

            if (offset + naluLen > u32Len)
                return -1; /* truncated: NAL payload exceeds buffer */

            if (naluType == 32 && pConfig->pu8Vps == NULL) {
                /* VPS */
                pConfig->pu8Vps = (U8 *)&pu8Data[offset];
                pConfig->u16VpsLen = naluLen;
            } else if (naluType == 33 && pConfig->pu8Sps == NULL) {
                /* SPS */
                pConfig->pu8Sps = (U8 *)&pu8Data[offset];
                pConfig->u16SpsLen = naluLen;
            } else if (naluType == 34 && pConfig->pu8Pps == NULL) {
                /* PPS */
                pConfig->pu8Pps = (U8 *)&pu8Data[offset];
                pConfig->u16PpsLen = naluLen;
            }

            offset += naluLen;
        }
    }

    return 0;
}
