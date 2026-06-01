/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    h265_utils.h
 * @Brief     :    H265/HEVC NAL unit utilities.
 *------------------------------------------------------------------------------
 */

#ifndef H265_UTILS_H
#define H265_UTILS_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NAL unit types */
#define H265_NAL_TRAIL_N 0
#define H265_NAL_TRAIL_R 1
#define H265_NAL_TSA_N 2
#define H265_NAL_TSA_R 3
#define H265_NAL_STSA_N 4
#define H265_NAL_STSA_R 5
#define H265_NAL_RADL_N 6
#define H265_NAL_RADL_R 7
#define H265_NAL_RASL_N 8
#define H265_NAL_RASL_R 9
#define H265_NAL_BLA_W_LP 16
#define H265_NAL_BLA_W_RADL 17
#define H265_NAL_BLA_N_LP 18
#define H265_NAL_IDR_W_RADL 19
#define H265_NAL_IDR_N_LP 20
#define H265_NAL_CRA_NUT 21
#define H265_NAL_VPS 32
#define H265_NAL_SPS 33
#define H265_NAL_PPS 34
#define H265_NAL_AUD 35
#define H265_NAL_EOS 36
#define H265_NAL_EOB 37
#define H265_NAL_FD 38
#define H265_NAL_PREFIX_SEI 39
#define H265_NAL_SUFFIX_SEI 40

/**
 * @brief  Get NAL unit type from first two bytes
 */
static inline U8 H265_GetNalType(const U8 *pu8Nal) { return (pu8Nal[0] >> 1) & 0x3F; }

/**
 * @brief  Check if NAL is a keyframe (IDR/CRA/BLA)
 */
static inline BOOL H265_IsKeyFrame(U8 u8NalType) {
    return (u8NalType >= H265_NAL_BLA_W_LP && u8NalType <= H265_NAL_CRA_NUT);
}

/**
 * @brief  Check if NAL is VCL
 */
static inline BOOL H265_IsVcl(U8 u8NalType) { return (u8NalType <= 31); }

/**
 * @brief  Parse SPS to extract resolution
 */
S32 H265_ParseSps(const U8 *pu8Sps, U32 u32Len, U32 *pu32Width, U32 *pu32Height);

#ifdef __cplusplus
}
#endif

#endif /* __H265_UTILS_H__ */
