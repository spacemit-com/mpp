/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    rtp_depacketizer.h
 * @Brief     :    RTP depacketizer interface and common types.
 *------------------------------------------------------------------------------
 */

#ifndef RTP_DEPACKETIZER_H
#define RTP_DEPACKETIZER_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RTP Header (RFC 3550) */
typedef struct _RtpHeader {
    U8 u8Version;     /* 2 bits */
    U8 u8Padding;     /* 1 bit */
    U8 u8Extension;   /* 1 bit */
    U8 u8CsrcCount;   /* 4 bits */
    U8 u8Marker;      /* 1 bit */
    U8 u8PayloadType; /* 7 bits */
    U16 u16SeqNum;    /* Sequence number */
    U32 u32Timestamp;
    U32 u32Ssrc;
    U32 u32PayloadOffset; /* Offset to payload */
} RtpHeader;

/**
 * @brief  Parse RTP header
 * @return Payload offset, -1 on error
 */
S32 Rtp_ParseHeader(const U8 *pu8Data, U32 u32Len, RtpHeader *pstHdr);

/* Frame callback prototype */
typedef void (*RtpFrameCallback)(void *pPriv, const U8 *pu8Data, U32 u32Len, U64 u64Pts, BOOL bKeyFrame);

/* Depacketizer interface (base struct for inheritance) */
typedef struct _RtpDepacketizer {
    S32 eCodec;          /* DemuxCodecType */
    U64 u64Timestamp;    /* Last timestamp */
    BOOL bFrameComplete; /* Frame ready */

    /* Virtual functions */
    S32 (*pfnProcess)(struct _RtpDepacketizer *pThis, const U8 *pu8RtpData, U32 u32Len);
    S32 (*pfnGetFrame)(struct _RtpDepacketizer *pThis, U8 *pu8Buf, U32 u32BufSize, U32 *pu32Size);
    void (*pfnReset)(struct _RtpDepacketizer *pThis);
    void (*pfnDestroy)(struct _RtpDepacketizer *pThis);
} RtpDepacketizer;

/* Common interface */
VOID RtpDepack_SetCallback(RtpDepacketizer *pDepack, RtpFrameCallback pfnCb, void *pPriv);
VOID RtpDepack_SetSps(RtpDepacketizer *pDepack, const U8 *pu8Sps, U32 u32Len);
VOID RtpDepack_SetPps(RtpDepacketizer *pDepack, const U8 *pu8Pps, U32 u32Len);
VOID RtpDepack_SetVps(RtpDepacketizer *pDepack, const U8 *pu8Vps, U32 u32Len);
S32 RtpDepack_Input(RtpDepacketizer *pDepack, const U8 *pu8Rtp, U32 u32Len);
VOID RtpDepack_Destroy(RtpDepacketizer *pDepack);

#ifdef __cplusplus
}
#endif

#endif /* __RTP_DEPACKETIZER_H__ */
