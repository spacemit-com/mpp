/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    h264_depacketizer.h
 * @Brief     :    H264 RTP depacketizer (RFC 6184).
 *                 Supports Single NAL, FU-A, STAP-A.
 *------------------------------------------------------------------------------
 */

#ifndef H264_DEPACKETIZER_H
#define H264_DEPACKETIZER_H

#include "rtp_depacketizer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Create H264 depacketizer
 */
RtpDepacketizer *H264Depack_Create(VOID);

#ifdef __cplusplus
}
#endif

#endif /* __H264_DEPACKETIZER_H__ */
