/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    h265_depacketizer.h
 * @Brief     :    H265/HEVC RTP depacketizer (RFC 7798).
 *                 Supports Single NAL, FU, AP.
 *------------------------------------------------------------------------------
 */

#ifndef H265_DEPACKETIZER_H
#define H265_DEPACKETIZER_H

#include "rtp_depacketizer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Create H265 depacketizer
 */
RtpDepacketizer *H265Depack_Create(VOID);

VOID H265Depack_SetVps(RtpDepacketizer *pDepack, const U8 *pu8Vps, U32 u32Len);
VOID H265Depack_SetSps(RtpDepacketizer *pDepack, const U8 *pu8Sps, U32 u32Len);
VOID H265Depack_SetPps(RtpDepacketizer *pDepack, const U8 *pu8Pps, U32 u32Len);
S32 H265Depack_Input(RtpDepacketizer *pDepack, const U8 *pu8Rtp, U32 u32Len);

#ifdef __cplusplus
}
#endif

#endif /* __H265_DEPACKETIZER_H__ */
