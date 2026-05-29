/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    h264_depacketizer.c
 * @Brief     :    H264 RTP depacketizer (RFC 6184).
 *                 Supports Single NAL, FU-A, STAP-A.
 *------------------------------------------------------------------------------
 */

#include "h264_depacketizer.h"

#include <stdlib.h>
#include <string.h>

#include "rtp_depacketizer.h"

/* NAL unit types */
#define NAL_TYPE_STAP_A 24
#define NAL_TYPE_FU_A 28
#define NAL_TYPE_FU_B 29

/* NAL type masks */
#define NAL_TYPE_MASK 0x1F
#define NAL_REF_IDC_MASK 0x60
#define NAL_F_BIT_MASK 0x80

/* Key frame NAL types */
#define NAL_TYPE_IDR 5
#define NAL_TYPE_SPS 7
#define NAL_TYPE_PPS 8

/* Start code */
static const U8 START_CODE[] = {0x00, 0x00, 0x00, 0x01};

typedef struct _H264Depack {
    RtpFrameCallback pfnCallback;
    void *pPriv;

    /* SPS/PPS */
    U8 au8Sps[256];
    U32 u32SpsLen;
    U8 au8Pps[64];
    U32 u32PpsLen;

    /* Frame assembly buffer */
    U8 au8Frame[512 * 1024];
    U32 u32FrameLen;

    /* FU-A state */
    BOOL bFuStarted;
    U32 u32FuTimestamp;
    U8 u8FuNalHdr;

    /* Current frame state */
    U32 u32LastTimestamp;
    BOOL bKeyFrame;
} H264Depack;

static BOOL is_key_frame_nal(U8 u8NalType) {
    return (u8NalType == NAL_TYPE_IDR || u8NalType == NAL_TYPE_SPS || u8NalType == NAL_TYPE_PPS);
}

static void emit_frame(H264Depack *pDepack, U32 u32Timestamp) {
    if (pDepack->pfnCallback && pDepack->u32FrameLen > 0) {
        pDepack->pfnCallback(pDepack->pPriv, pDepack->au8Frame, pDepack->u32FrameLen, u32Timestamp, pDepack->bKeyFrame);
    }
    pDepack->u32FrameLen = 0;
    pDepack->bKeyFrame = MPP_FALSE;
}

static void append_nal(H264Depack *pDepack, const U8 *pu8Data, U32 u32Len) {
    if (pDepack->u32FrameLen + 4 + u32Len > sizeof(pDepack->au8Frame)) {
        return; /* Buffer overflow, drop */
    }

    /* Add start code */
    memcpy(&pDepack->au8Frame[pDepack->u32FrameLen], START_CODE, 4);
    pDepack->u32FrameLen += 4;

    /* Add NAL data */
    memcpy(&pDepack->au8Frame[pDepack->u32FrameLen], pu8Data, u32Len);
    pDepack->u32FrameLen += u32Len;

    /* Check if key frame */
    U8 u8NalType = pu8Data[0] & NAL_TYPE_MASK;
    if (is_key_frame_nal(u8NalType)) {
        pDepack->bKeyFrame = MPP_TRUE;
    }
}

static void handle_single_nal(H264Depack *pDepack, const U8 *pu8Data, U32 u32Len, U32 u32Timestamp, U8 u8Marker) {
    /* New timestamp = new frame */
    if (u32Timestamp != pDepack->u32LastTimestamp && pDepack->u32FrameLen > 0) {
        emit_frame(pDepack, pDepack->u32LastTimestamp);
    }
    pDepack->u32LastTimestamp = u32Timestamp;

    append_nal(pDepack, pu8Data, u32Len);

    /* Marker bit = end of frame */
    if (u8Marker) {
        emit_frame(pDepack, u32Timestamp);
    }
}

static void handle_stap_a(H264Depack *pDepack, const U8 *pu8Data, U32 u32Len, U32 u32Timestamp, U8 u8Marker) {
    /* New timestamp = new frame */
    if (u32Timestamp != pDepack->u32LastTimestamp && pDepack->u32FrameLen > 0) {
        emit_frame(pDepack, pDepack->u32LastTimestamp);
    }
    pDepack->u32LastTimestamp = u32Timestamp;

    /* Skip STAP-A header */
    const U8 *p = pu8Data + 1;
    const U8 *pEnd = pu8Data + u32Len;

    while (p + 2 < pEnd) {
        U16 u16NalLen = (p[0] << 8) | p[1];
        p += 2;

        if (p + u16NalLen > pEnd)
            break;

        append_nal(pDepack, p, u16NalLen);
        p += u16NalLen;
    }

    if (u8Marker) {
        emit_frame(pDepack, u32Timestamp);
    }
}

static void handle_fu_a(H264Depack *pDepack, const U8 *pu8Data, U32 u32Len, U32 u32Timestamp, U8 u8Marker) {
    if (u32Len < 2)
        return;

    U8 u8FuIndicator = pu8Data[0];
    U8 u8FuHeader = pu8Data[1];

    U8 u8Start = (u8FuHeader >> 7) & 0x01;
    U8 u8End = (u8FuHeader >> 6) & 0x01;
    U8 u8NalType = u8FuHeader & NAL_TYPE_MASK;

    if (u8Start) {
        /* New timestamp = new frame */
        if (u32Timestamp != pDepack->u32LastTimestamp && pDepack->u32FrameLen > 0) {
            emit_frame(pDepack, pDepack->u32LastTimestamp);
        }
        pDepack->u32LastTimestamp = u32Timestamp;

        /* Start of fragmented NAL */
        pDepack->bFuStarted = MPP_TRUE;
        pDepack->u32FuTimestamp = u32Timestamp;

        /* Reconstruct NAL header */
        pDepack->u8FuNalHdr = (u8FuIndicator & NAL_REF_IDC_MASK) | u8NalType;

        /* Add start code and NAL header */
        memcpy(&pDepack->au8Frame[pDepack->u32FrameLen], START_CODE, 4);
        pDepack->u32FrameLen += 4;
        pDepack->au8Frame[pDepack->u32FrameLen++] = pDepack->u8FuNalHdr;

        /* Check if key frame */
        if (is_key_frame_nal(u8NalType)) {
            pDepack->bKeyFrame = MPP_TRUE;
        }
    }

    if (!pDepack->bFuStarted) {
        return; /* Missed start packet */
    }

    /* Append FU payload (skip FU indicator and header) */
    const U8 *pPayload = pu8Data + 2;
    U32 u32PayloadLen = u32Len - 2;

    if (pDepack->u32FrameLen + u32PayloadLen > sizeof(pDepack->au8Frame)) {
        pDepack->bFuStarted = MPP_FALSE;
        return; /* Buffer overflow */
    }

    memcpy(&pDepack->au8Frame[pDepack->u32FrameLen], pPayload, u32PayloadLen);
    pDepack->u32FrameLen += u32PayloadLen;

    if (u8End) {
        pDepack->bFuStarted = MPP_FALSE;

        if (u8Marker) {
            emit_frame(pDepack, u32Timestamp);
        }
    }
}

/* Public interface */
RtpDepacketizer *H264Depack_Create(VOID) {
    H264Depack *pDepack = (H264Depack *)calloc(1, sizeof(H264Depack));
    return (RtpDepacketizer *)pDepack;
}

VOID RtpDepack_Destroy(RtpDepacketizer *pDepack) {
    if (pDepack) {
        free(pDepack);
    }
}

VOID RtpDepack_SetCallback(RtpDepacketizer *pDepack, RtpFrameCallback pfnCb, void *pPriv) {
    H264Depack *p = (H264Depack *)pDepack;
    if (p) {
        p->pfnCallback = pfnCb;
        p->pPriv = pPriv;
    }
}

VOID RtpDepack_SetSps(RtpDepacketizer *pDepack, const U8 *pu8Sps, U32 u32Len) {
    H264Depack *p = (H264Depack *)pDepack;
    if (p && pu8Sps && u32Len < sizeof(p->au8Sps)) {
        memcpy(p->au8Sps, pu8Sps, u32Len);
        p->u32SpsLen = u32Len;
    }
}

VOID RtpDepack_SetPps(RtpDepacketizer *pDepack, const U8 *pu8Pps, U32 u32Len) {
    H264Depack *p = (H264Depack *)pDepack;
    if (p && pu8Pps && u32Len < sizeof(p->au8Pps)) {
        memcpy(p->au8Pps, pu8Pps, u32Len);
        p->u32PpsLen = u32Len;
    }
}

VOID RtpDepack_SetVps(RtpDepacketizer *pDepack, const U8 *pu8Vps, U32 u32Len) {
    /* H264 doesn't have VPS, ignored */
    (void)pDepack;
    (void)pu8Vps;
    (void)u32Len;
}

S32 RtpDepack_Input(RtpDepacketizer *pDepack, const U8 *pu8Rtp, U32 u32Len) {
    H264Depack *p = (H264Depack *)pDepack;
    if (!p || !pu8Rtp || u32Len < 12)
        return -1;

    /* Parse RTP header */
    RtpHeader hdr;
    S32 s32PayloadOff = Rtp_ParseHeader(pu8Rtp, u32Len, &hdr);
    if (s32PayloadOff < 0)
        return -1;

    const U8 *pPayload = pu8Rtp + s32PayloadOff;
    U32 u32PayloadLen = u32Len - s32PayloadOff;

    if (u32PayloadLen < 1)
        return -1;

    U8 u8NalType = pPayload[0] & NAL_TYPE_MASK;

    switch (u8NalType) {
    case NAL_TYPE_STAP_A:
        handle_stap_a(p, pPayload, u32PayloadLen, hdr.u32Timestamp, hdr.u8Marker);
        break;

    case NAL_TYPE_FU_A:
        handle_fu_a(p, pPayload, u32PayloadLen, hdr.u32Timestamp, hdr.u8Marker);
        break;

    case NAL_TYPE_FU_B:
        /* FU-B not commonly used, treat as FU-A */
        handle_fu_a(p, pPayload, u32PayloadLen, hdr.u32Timestamp, hdr.u8Marker);
        break;

    default:
        /* Single NAL unit (types 1-23) */
        if (u8NalType >= 1 && u8NalType <= 23) {
            handle_single_nal(p, pPayload, u32PayloadLen, hdr.u32Timestamp, hdr.u8Marker);
        }
        break;
    }

    return 0;
}
