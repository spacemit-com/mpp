/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    aac_utils.h
 * @Brief     :    AAC audio utilities (ADTS parsing).
 *------------------------------------------------------------------------------
 */

#ifndef AAC_UTILS_H
#define AAC_UTILS_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AAC ADTS header info */
typedef struct _AdtsHeader {
    U8 u8Profile;       /* AAC profile (1=LC, 2=HE) */
    U8 u8SampleRateIdx; /* Sample rate index */
    U8 u8ChannelConfig; /* Channel configuration */
    U16 u16FrameLen;    /* Frame length including header */
    U32 u32SampleRate;  /* Actual sample rate */
} AdtsHeader;

/**
 * @brief  Parse ADTS header
 * @return 0 on success, -1 on error
 */
S32 AAC_ParseAdts(const U8 *pu8Data, U32 u32Len, AdtsHeader *pstHeader);

/**
 * @brief  Create ADTS header
 * @return Header size (7 bytes)
 */
S32 AAC_CreateAdts(U8 *pu8Header, U8 u8Profile, U8 u8SampleRateIdx, U8 u8ChannelConfig, U16 u16DataLen);

/**
 * @brief  Get sample rate from index
 */
U32 AAC_GetSampleRate(U8 u8Idx);

/**
 * @brief  Parse AudioSpecificConfig (from MPEG-4)
 */
S32 AAC_ParseAsc(const U8 *pu8Data, U32 u32Len, U8 *pu8Profile, U8 *pu8SampleRateIdx, U8 *pu8ChannelConfig);

#ifdef __cplusplus
}
#endif

#endif /* __AAC_UTILS_H__ */
