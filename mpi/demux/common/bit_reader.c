/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "bit_reader.h"

VOID BitReader_Init(BitReader *pBr, const U8 *pu8Data, U32 u32Size)
{
    pBr->pu8Data = pu8Data;
    pBr->u32Size = u32Size;
    pBr->u32BitPos = 0;
}

U32 BitReader_ReadBits(BitReader *pBr, U32 u32NumBits)
{
    U32 u32Result = 0;
    U32 i;

    for (i = 0; i < u32NumBits; i++) {
        U32 u32BytePos = pBr->u32BitPos / 8;
        U32 u32BitInByte = 7 - (pBr->u32BitPos % 8);

        if (u32BytePos >= pBr->u32Size) {
            break;
        }

        u32Result = (u32Result << 1) | ((pBr->pu8Data[u32BytePos] >> u32BitInByte) & 1);
        pBr->u32BitPos++;
    }

    return u32Result;
}

U32 BitReader_ReadUE(BitReader *pBr)
{
    U32 u32LeadingZeros = 0;
    U32 u32Value;

    while (!BitReader_IsEof(pBr) && BitReader_ReadBits(pBr, 1) == 0) {
        u32LeadingZeros++;
    }

    if (u32LeadingZeros == 0) {
        return 0;
    }

    u32Value = BitReader_ReadBits(pBr, u32LeadingZeros);
    return (1 << u32LeadingZeros) - 1 + u32Value;
}

S32 BitReader_ReadSE(BitReader *pBr)
{
    U32 u32Val = BitReader_ReadUE(pBr);
    S32 s32Val;

    if (u32Val & 1) {
        s32Val = (S32)((u32Val + 1) / 2);
    } else {
        s32Val = -(S32)(u32Val / 2);
    }

    return s32Val;
}

VOID BitReader_Skip(BitReader *pBr, U32 u32NumBits)
{
    pBr->u32BitPos += u32NumBits;
    if (pBr->u32BitPos > pBr->u32Size * 8) {
        pBr->u32BitPos = pBr->u32Size * 8;
    }
}

U32 BitReader_Remaining(const BitReader *pBr)
{
    U32 u32TotalBits = pBr->u32Size * 8;
    if (pBr->u32BitPos >= u32TotalBits) {
        return 0;
    }
    return u32TotalBits - pBr->u32BitPos;
}

BOOL BitReader_IsEof(const BitReader *pBr)
{
    return pBr->u32BitPos >= pBr->u32Size * 8;
}
