/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    h264_utils.h
 * @Brief     :    H264 NAL unit utilities.
 *------------------------------------------------------------------------------
 */

#ifndef H264_UTILS_H
#define H264_UTILS_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NAL unit types */
#define H264_NAL_SLICE 1
#define H264_NAL_DPA 2
#define H264_NAL_DPB 3
#define H264_NAL_DPC 4
#define H264_NAL_IDR_SLICE 5
#define H264_NAL_SEI 6
#define H264_NAL_SPS 7
#define H264_NAL_PPS 8
#define H264_NAL_AUD 9

/**
 * @brief  Get NAL unit type from first byte
 */
static inline U8 H264_GetNalType(U8 u8FirstByte) { return u8FirstByte & 0x1F; }

/**
 * @brief  Check if NAL is a keyframe (IDR)
 */
static inline BOOL H264_IsKeyFrame(U8 u8NalType) { return (u8NalType == H264_NAL_IDR_SLICE); }

/**
 * @brief  Check if NAL is VCL (Video Coding Layer)
 */
static inline BOOL H264_IsVcl(U8 u8NalType) { return (u8NalType >= 1 && u8NalType <= 5); }

/**
 * @brief  Find next start code in buffer
 * @return Offset to start code, or -1 if not found
 */
S32 H264_FindStartCode(const U8 *pu8Data, U32 u32Len);

/**
 * @brief  Parse SPS to extract resolution
 */
S32 H264_ParseSps(const U8 *pu8Sps, U32 u32Len, U32 *pu32Width, U32 *pu32Height);

/**
 * @brief  Convert Annex-B to AVCC format (length-prefixed)
 * @param  u32OutCap  Capacity of pu8Out in bytes; conversion stops without
 *                    overrunning the buffer if the output would exceed it.
 */
S32 H264_AnnexBToAvcc(const U8 *pu8In, U32 u32InLen, U8 *pu8Out, U32 u32OutCap, U32 *pu32OutLen);

/**
 * @brief  Convert AVCC to Annex-B format (start code prefixed)
 * @param  u32OutCap  Capacity of pu8Out in bytes; conversion stops without
 *                    overrunning the buffer if the output would exceed it.
 */
S32 H264_AvccToAnnexB(const U8 *pu8In, U32 u32InLen, U8 *pu8Out, U32 u32OutCap, U32 *pu32OutLen, U8 u8NalLenSize);

#ifdef __cplusplus
}
#endif

#endif /* __H264_UTILS_H__ */
