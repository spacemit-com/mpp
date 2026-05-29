/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    aac_depacketizer.h
 * @Brief     :    AAC RTP depacketizer (RFC 3640 / RFC 6416).
 *------------------------------------------------------------------------------
 */

#ifndef AAC_DEPACKETIZER_H
#define AAC_DEPACKETIZER_H

#include "rtp_depacketizer.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Create AAC RTP depacketizer
 * @param  u8SizeLength   AU-size-length from SDP (usually 13)
 * @param  u8IndexLength  AU-index-length from SDP (usually 3)
 */
RtpDepacketizer *AacDepacketizer_Create(U8 u8SizeLength, U8 u8IndexLength);

/**
 * @brief  Set audio config from SDP config parameter
 */
S32 AacDepacketizer_SetConfig(RtpDepacketizer *pDepkt, const U8 *pu8Config, U32 u32Len);

#ifdef __cplusplus
}
#endif

#endif /* __AAC_DEPACKETIZER_H__ */
