/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    h265_depacketizer.c
 * @Brief     :    H265/HEVC RTP depacketizer (RFC 7798).
 *                 Supports Single NAL, FU, AP.
 *------------------------------------------------------------------------------
 */

#include "h265_depacketizer.h"

#include <stdlib.h>
#include <string.h>

#include "codec/h265_utils.h"
#include "rtp_depacketizer.h"

/* RTP-specific NAL unit types (RFC 7798, not part of the base H.265 spec) */
#define H265_NAL_TYPE_AP 48 /* Aggregation Packet */
#define H265_NAL_TYPE_FU 49 /* Fragmentation Unit */

/* Start code */
static const U8 START_CODE[] = {0x00, 0x00, 0x00, 0x01};

typedef struct _H265Depack {
    RtpFrameCallback pfnCallback;
    void *pPriv;

    /* VPS/SPS/PPS */
    U8 au8Vps[256];
    U32 u32VpsLen;
    U8 au8Sps[256];
    U32 u32SpsLen;
    U8 au8Pps[64];
    U32 u32PpsLen;

    /* Frame assembly buffer */
    U8 au8Frame[512 * 1024];
    U32 u32FrameLen;

    /* FU state */
    BOOL bFuStarted;
    BOOL bFuIsIdr; /* TRUE when current FU is an IDR slice */
    U32 u32FuTimestamp;
    U8 au8FuNalHdr[2];

    /* Current frame state */
    U32 u32LastTimestamp;
    BOOL bKeyFrame;
    BOOL bFrameHasVps;
    BOOL bFrameHasSps;
    BOOL bFrameHasPps;
} H265Depack;

static U8 get_nal_type(const U8 *pu8Nal) { return (pu8Nal[0] >> 1) & 0x3F; }

static BOOL is_key_frame_nal(U8 u8NalType) {
    return (u8NalType == H265_NAL_VPS || u8NalType == H265_NAL_SPS || u8NalType == H265_NAL_PPS ||
            u8NalType == H265_NAL_IDR_W_RADL || u8NalType == H265_NAL_IDR_N_LP);
}

static void emit_frame(H265Depack *pDepack, U32 u32Timestamp) {
    if (pDepack->pfnCallback && pDepack->u32FrameLen > 0) {
        pDepack->pfnCallback(pDepack->pPriv, pDepack->au8Frame, pDepack->u32FrameLen, u32Timestamp, pDepack->bKeyFrame);
    }
    pDepack->u32FrameLen = 0;
    pDepack->bKeyFrame = MPP_FALSE;
    pDepack->bFrameHasVps = MPP_FALSE;
    pDepack->bFrameHasSps = MPP_FALSE;
    pDepack->bFrameHasPps = MPP_FALSE;
}

static void append_raw_nal(H265Depack *pDepack, const U8 *pu8Data, U32 u32Len) {
    if (pDepack->u32FrameLen + 4 + u32Len > sizeof(pDepack->au8Frame)) {
        return;
    }

    memcpy(&pDepack->au8Frame[pDepack->u32FrameLen], START_CODE, 4);
    pDepack->u32FrameLen += 4;

    memcpy(&pDepack->au8Frame[pDepack->u32FrameLen], pu8Data, u32Len);
    pDepack->u32FrameLen += u32Len;
}

static void save_parameter_set(H265Depack *pDepack, U8 u8NalType, const U8 *pu8Data, U32 u32Len) {
    /* Cache the parameter set data. */
    if (u8NalType == H265_NAL_VPS && u32Len < sizeof(pDepack->au8Vps)) {
        memcpy(pDepack->au8Vps, pu8Data, u32Len);
        pDepack->u32VpsLen = u32Len;
    } else if (u8NalType == H265_NAL_SPS && u32Len < sizeof(pDepack->au8Sps)) {
        memcpy(pDepack->au8Sps, pu8Data, u32Len);
        pDepack->u32SpsLen = u32Len;
    } else if (u8NalType == H265_NAL_PPS && u32Len < sizeof(pDepack->au8Pps)) {
        memcpy(pDepack->au8Pps, pu8Data, u32Len);
        pDepack->u32PpsLen = u32Len;
    }

    /* If we are in the middle of an FU-A IDR reassembly and a parameter set
     * arrives late (separate RTP packet after FU-A start), inject it into the
     * frame buffer immediately.  inject_parameter_sets() already ran at FU-A
     * start and set the bFrameHas* flags for whatever was cached at that
     * time; newly-arrived PS would otherwise be lost.  Writing it now places
     * it between the already-written PS (if any) and the IDR slice data that
     * is still being accumulated, preserving correct decode order. */
    if (pDepack->bFuStarted && pDepack->bFuIsIdr) {
        if (u8NalType == H265_NAL_VPS && !pDepack->bFrameHasVps) {
            append_raw_nal(pDepack, pDepack->au8Vps, pDepack->u32VpsLen);
            pDepack->bFrameHasVps = MPP_TRUE;
        } else if (u8NalType == H265_NAL_SPS && !pDepack->bFrameHasSps) {
            append_raw_nal(pDepack, pDepack->au8Sps, pDepack->u32SpsLen);
            pDepack->bFrameHasSps = MPP_TRUE;
        } else if (u8NalType == H265_NAL_PPS && !pDepack->bFrameHasPps) {
            append_raw_nal(pDepack, pDepack->au8Pps, pDepack->u32PpsLen);
            pDepack->bFrameHasPps = MPP_TRUE;
        }
    }
}

static void inject_parameter_sets(H265Depack *pDepack) {
    if (!pDepack->bFrameHasVps && pDepack->u32VpsLen > 0) {
        append_raw_nal(pDepack, pDepack->au8Vps, pDepack->u32VpsLen);
        pDepack->bFrameHasVps = MPP_TRUE;
    }
    if (!pDepack->bFrameHasSps && pDepack->u32SpsLen > 0) {
        append_raw_nal(pDepack, pDepack->au8Sps, pDepack->u32SpsLen);
        pDepack->bFrameHasSps = MPP_TRUE;
    }
    if (!pDepack->bFrameHasPps && pDepack->u32PpsLen > 0) {
        append_raw_nal(pDepack, pDepack->au8Pps, pDepack->u32PpsLen);
        pDepack->bFrameHasPps = MPP_TRUE;
    }
}

static void append_nal(H265Depack *pDepack, const U8 *pu8Data, U32 u32Len) {
    U8 u8NalType = get_nal_type(pu8Data);

    /* Parameter set NALs (VPS/SPS/PPS) are cached but NOT appended directly.
     * They are only written into the frame buffer by inject_parameter_sets()
     * which guarantees correct placement before the IDR slice.  Appending
     * them here would risk placing them after IDR data when packet order
     * differs from decode order (e.g. FU-A IDR followed by standalone PS). */
    if (u8NalType == H265_NAL_VPS || u8NalType == H265_NAL_SPS ||
        u8NalType == H265_NAL_PPS) {
        save_parameter_set(pDepack, u8NalType, pu8Data, u32Len);
        return;
    }

    if (u8NalType == H265_NAL_IDR_W_RADL || u8NalType == H265_NAL_IDR_N_LP) {
        inject_parameter_sets(pDepack);
    }

    append_raw_nal(pDepack, pu8Data, u32Len);

    if (is_key_frame_nal(u8NalType)) {
        pDepack->bKeyFrame = MPP_TRUE;
    }
}

static void handle_single_nal(H265Depack *pDepack, const U8 *pu8Data, U32 u32Len, U32 u32Timestamp, U8 u8Marker) {
    if (u32Timestamp != pDepack->u32LastTimestamp && pDepack->u32FrameLen > 0) {
        emit_frame(pDepack, pDepack->u32LastTimestamp);
    }
    pDepack->u32LastTimestamp = u32Timestamp;

    append_nal(pDepack, pu8Data, u32Len);

    if (u8Marker) {
        emit_frame(pDepack, u32Timestamp);
    }
}

static void handle_ap(H265Depack *pDepack, const U8 *pu8Data, U32 u32Len, U32 u32Timestamp, U8 u8Marker) {
    if (u32Timestamp != pDepack->u32LastTimestamp && pDepack->u32FrameLen > 0) {
        emit_frame(pDepack, pDepack->u32LastTimestamp);
    }
    pDepack->u32LastTimestamp = u32Timestamp;

    /* Skip AP header (2 bytes) */
    const U8 *p = pu8Data + 2;
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

static void handle_fu(H265Depack *pDepack, const U8 *pu8Data, U32 u32Len, U32 u32Timestamp, U8 u8Marker) {
    if (u32Len < 3)
        return;

    /* FU header layout:
     * Byte 0-1: PayloadHdr (NAL unit header with type = 49)
     * Byte 2: FU header (S|E|type)
     */
    U8 u8FuHdr = pu8Data[2];
    U8 u8Start = (u8FuHdr >> 7) & 0x01;
    U8 u8End = (u8FuHdr >> 6) & 0x01;
    U8 u8NalType = u8FuHdr & 0x3F;

    if (u8Start) {
        if (u32Timestamp != pDepack->u32LastTimestamp && pDepack->u32FrameLen > 0) {
            emit_frame(pDepack, pDepack->u32LastTimestamp);
        }
        pDepack->u32LastTimestamp = u32Timestamp;

        pDepack->bFuStarted = MPP_TRUE;
        pDepack->bFuIsIdr =
            (u8NalType == H265_NAL_IDR_W_RADL || u8NalType == H265_NAL_IDR_N_LP);
        pDepack->u32FuTimestamp = u32Timestamp;

        /* Reconstruct NAL header (2 bytes for H265) */
        pDepack->au8FuNalHdr[0] = (pu8Data[0] & 0x81) | (u8NalType << 1);
        pDepack->au8FuNalHdr[1] = pu8Data[1];

        if (pDepack->bFuIsIdr) {
            inject_parameter_sets(pDepack);
        }

        /* Add start code and NAL header */
        memcpy(&pDepack->au8Frame[pDepack->u32FrameLen], START_CODE, 4);
        pDepack->u32FrameLen += 4;
        memcpy(&pDepack->au8Frame[pDepack->u32FrameLen], pDepack->au8FuNalHdr, 2);
        pDepack->u32FrameLen += 2;

        if (is_key_frame_nal(u8NalType)) {
            pDepack->bKeyFrame = MPP_TRUE;
        }
    }

    if (!pDepack->bFuStarted) {
        return;
    }

    /* Append FU payload (skip 3-byte header) */
    const U8 *pPayload = pu8Data + 3;
    U32 u32PayloadLen = u32Len - 3;

    if (pDepack->u32FrameLen + u32PayloadLen > sizeof(pDepack->au8Frame)) {
        pDepack->bFuStarted = MPP_FALSE;
        return;
    }

    memcpy(&pDepack->au8Frame[pDepack->u32FrameLen], pPayload, u32PayloadLen);
    pDepack->u32FrameLen += u32PayloadLen;

    if (u8End) {
        pDepack->bFuStarted = MPP_FALSE;
        pDepack->bFuIsIdr = MPP_FALSE;

        if (u8Marker) {
            emit_frame(pDepack, u32Timestamp);
        }
    }
}

/* Public interface */
RtpDepacketizer *H265Depack_Create(VOID) {
    H265Depack *pDepack = (H265Depack *)calloc(1, sizeof(H265Depack));
    return (RtpDepacketizer *)pDepack;
}

VOID H265Depack_SetVps(RtpDepacketizer *pDepack, const U8 *pu8Vps, U32 u32Len) {
    H265Depack *p = (H265Depack *)pDepack;
    if (p && pu8Vps && u32Len < sizeof(p->au8Vps)) {
        memcpy(p->au8Vps, pu8Vps, u32Len);
        p->u32VpsLen = u32Len;
    }
}

VOID H265Depack_SetSps(RtpDepacketizer *pDepack, const U8 *pu8Sps, U32 u32Len) {
    H265Depack *p = (H265Depack *)pDepack;
    if (p && pu8Sps && u32Len < sizeof(p->au8Sps)) {
        memcpy(p->au8Sps, pu8Sps, u32Len);
        p->u32SpsLen = u32Len;
    }
}

VOID H265Depack_SetPps(RtpDepacketizer *pDepack, const U8 *pu8Pps, U32 u32Len) {
    H265Depack *p = (H265Depack *)pDepack;
    if (p && pu8Pps && u32Len < sizeof(p->au8Pps)) {
        memcpy(p->au8Pps, pu8Pps, u32Len);
        p->u32PpsLen = u32Len;
    }
}

/* Input for H265 */
S32 H265Depack_Input(RtpDepacketizer *pDepack, const U8 *pu8Rtp, U32 u32Len) {
    H265Depack *p = (H265Depack *)pDepack;
    if (!p || !pu8Rtp || u32Len < 12)
        return -1;

    RtpHeader hdr;
    S32 s32PayloadOff = Rtp_ParseHeader(pu8Rtp, u32Len, &hdr);
    if (s32PayloadOff < 0)
        return -1;

    const U8 *pPayload = pu8Rtp + s32PayloadOff;
    U32 u32PayloadLen = u32Len - s32PayloadOff;

    if (u32PayloadLen < 2)
        return -1;

    U8 u8NalType = get_nal_type(pPayload);

    switch (u8NalType) {
    case H265_NAL_TYPE_AP:
        handle_ap(p, pPayload, u32PayloadLen, hdr.u32Timestamp, hdr.u8Marker);
        break;

    case H265_NAL_TYPE_FU:
        handle_fu(p, pPayload, u32PayloadLen, hdr.u32Timestamp, hdr.u8Marker);
        break;

    default:
        /* Single NAL unit (types 0-47) */
        if (u8NalType <= 47) {
            handle_single_nal(p, pPayload, u32PayloadLen, hdr.u32Timestamp, hdr.u8Marker);
        }
        break;
    }

    return 0;
}
