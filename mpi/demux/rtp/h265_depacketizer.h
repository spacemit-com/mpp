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

#ifdef __cplusplus
}
#endif

#endif /* __H265_DEPACKETIZER_H__ */
