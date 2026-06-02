/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    rtp_packet.h
 * @Brief     :    RTP packet parsing (RFC 3550).
 *------------------------------------------------------------------------------
 */

#ifndef RTP_PACKET_H
#define RTP_PACKET_H

#include "rtp_depacketizer.h"
#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RtpHeader is defined in rtp_depacketizer.h */

/**
 * @brief  Parse RTP header
 * @return 0 on success, -1 on error
 */
S32 RTP_ParseHeader(const U8 *pu8Data, U32 u32Len, RtpHeader *pstHeader);

/**
 * @brief  Get payload pointer
 */
const U8 *RTP_GetPayload(const U8 *pu8Data, const RtpHeader *pstHeader);

/**
 * @brief  Get payload size
 */
U32 RTP_GetPayloadSize(U32 u32PacketLen, const RtpHeader *pstHeader);

#ifdef __cplusplus
}
#endif

#endif /* __RTP_PACKET_H__ */
