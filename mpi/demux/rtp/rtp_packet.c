/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    rtp_packet.c
 * @Brief     :    RTP packet parsing (RFC 3550).
 *------------------------------------------------------------------------------
 */

#include "rtp_packet.h"
#include <string.h>

S32 Rtp_ParseHeader(const U8 *pu8Data, U32 u32Len, RtpHeader *pstHdr) {
    if (!pu8Data || !pstHdr || u32Len < 12) {
        return -1;
    }

    /* Parse first byte */
    pstHdr->u8Version = (pu8Data[0] >> 6) & 0x03;
    pstHdr->u8Padding = (pu8Data[0] >> 5) & 0x01;
    pstHdr->u8Extension = (pu8Data[0] >> 4) & 0x01;
    pstHdr->u8CsrcCount = pu8Data[0] & 0x0F;

    /* Validate version */
    if (pstHdr->u8Version != 2) {
        return -1;
    }

    /* Parse second byte */
    pstHdr->u8Marker = (pu8Data[1] >> 7) & 0x01;
    pstHdr->u8PayloadType = pu8Data[1] & 0x7F;

    /* Parse sequence number (big endian) */
    pstHdr->u16SeqNum = (pu8Data[2] << 8) | pu8Data[3];

    /* Parse timestamp (big endian) */
    pstHdr->u32Timestamp = ((U32)pu8Data[4] << 24) | ((U32)pu8Data[5] << 16) | ((U32)pu8Data[6] << 8) | (U32)pu8Data[7];

    /* Parse SSRC (big endian) */
    pstHdr->u32Ssrc = ((U32)pu8Data[8] << 24) | ((U32)pu8Data[9] << 16) | ((U32)pu8Data[10] << 8) | (U32)pu8Data[11];

    /* Calculate header length */
    U32 u32HdrLen = 12 + (pstHdr->u8CsrcCount * 4);

    /* Handle extension header */
    if (pstHdr->u8Extension && u32Len > u32HdrLen + 4) {
        U16 u16ExtLen = (pu8Data[u32HdrLen + 2] << 8) | pu8Data[u32HdrLen + 3];
        u32HdrLen += 4 + (u16ExtLen * 4);
    }

    if (u32HdrLen >= u32Len) {
        return -1;
    }

    pstHdr->u32PayloadOffset = u32HdrLen;
    return (S32)u32HdrLen; /* Return payload offset */
}

S32 RTP_ParseHeader(const U8 *pu8Data, U32 u32Len, RtpHeader *pstHeader) {
    return Rtp_ParseHeader(pu8Data, u32Len, pstHeader) < 0 ? -1 : 0;
}

const U8 *RTP_GetPayload(const U8 *pu8Data, const RtpHeader *pstHeader) {
    if (!pu8Data || !pstHeader) {
        return NULL;
    }
    return pu8Data + pstHeader->u32PayloadOffset;
}

U32 RTP_GetPayloadSize(U32 u32PacketLen, const RtpHeader *pstHeader) {
    if (!pstHeader || pstHeader->u32PayloadOffset >= u32PacketLen) {
        return 0;
    }
    return u32PacketLen - pstHeader->u32PayloadOffset;
}
