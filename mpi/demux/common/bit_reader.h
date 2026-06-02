/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    bit_reader.h
 * @Brief     :    Bit stream reader for NAL unit parsing.
 *------------------------------------------------------------------------------
 */

#ifndef BIT_READER_H
#define BIT_READER_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _BitReader {
    const U8 *pu8Data;
    U32 u32Size;
    U32 u32BitPos;
} BitReader;

/**
 * @brief  Initialize bit reader
 */
VOID BitReader_Init(BitReader *pBr, const U8 *pu8Data, U32 u32Size);

/**
 * @brief  Read unsigned bits
 */
U32 BitReader_ReadBits(BitReader *pBr, U32 u32NumBits);

/**
 * @brief  Read unsigned Exp-Golomb
 */
U32 BitReader_ReadUE(BitReader *pBr);

/**
 * @brief  Read signed Exp-Golomb
 */
S32 BitReader_ReadSE(BitReader *pBr);

/**
 * @brief  Skip bits
 */
VOID BitReader_Skip(BitReader *pBr, U32 u32NumBits);

/**
 * @brief  Get remaining bits
 */
U32 BitReader_Remaining(const BitReader *pBr);

/**
 * @brief  Check if at end
 */
BOOL BitReader_IsEof(const BitReader *pBr);

#ifdef __cplusplus
}
#endif

#endif /* __BIT_READER_H__ */
