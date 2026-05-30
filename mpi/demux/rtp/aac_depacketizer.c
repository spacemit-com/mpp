/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "aac_depacketizer.h"

#include <stdlib.h>
#include <string.h>

#include "codec/aac_utils.h"
#include "demux/demux_type.h"
#include "rtp_packet.h"

typedef struct _AacDepacketizerCtx {
    RtpDepacketizer stBase;
    U8 u8SizeLength;
    U8 u8IndexLength;
    U8 u8IndexDeltaLength;
    U8 au8Config[16];
    U32 u32ConfigLen;
    U8 u8Profile;
    U8 u8SampleRateIdx;
    U8 u8ChannelConfig;

    U8 *pu8FrameBuf;
    U32 u32FrameSize;
    U32 u32FrameCapacity;
} AacDepacketizerCtx;

static S32 AacDepkt_Process(RtpDepacketizer *pDepkt, const U8 *pu8RtpData, U32 u32Len) {
    AacDepacketizerCtx *pCtx = (AacDepacketizerCtx *)pDepkt;
    if (!pCtx || !pu8RtpData || u32Len < 12)
        return -1;

    RtpHeader hdr;
    if (RTP_ParseHeader(pu8RtpData, u32Len, &hdr) != 0) {
        return -1;
    }

    const U8 *pPayload = RTP_GetPayload(pu8RtpData, &hdr);
    U32 payloadSize = RTP_GetPayloadSize(u32Len, &hdr);

    if (payloadSize < 2)
        return -1;

    /* AU-headers-length (16 bits, in bits) */
    U16 auHeadersLen = ((U16)pPayload[0] << 8) | pPayload[1];
    U32 auHeadersBytes = (auHeadersLen + 7) / 8;

    if (payloadSize < 2 + auHeadersBytes)
        return -1;

    /* Parse AU headers */
    const U8 *pAuHeaders = &pPayload[2];
    const U8 *pAuData = &pPayload[2 + auHeadersBytes];
    U32 auDataLen = payloadSize - 2 - auHeadersBytes;

    /* Simple case: single AU per packet */
    U32 auSize = 0;
    if (pCtx->u8SizeLength > 0) {
        /* Extract AU-size from bit stream */
        U32 bitOffset = 0;
        for (U8 i = 0; i < pCtx->u8SizeLength; i++) {
            U32 byteIdx = bitOffset / 8;
            U32 bitIdx = 7 - (bitOffset % 8);
            auSize = (auSize << 1) | ((pAuHeaders[byteIdx] >> bitIdx) & 1);
            bitOffset++;
        }
    } else {
        auSize = auDataLen;
    }

    if (auSize > auDataLen)
        auSize = auDataLen;

    /* Allocate frame buffer with ADTS header */
    U32 totalSize = 7 + auSize; /* 7 bytes ADTS header */
    if (totalSize > pCtx->u32FrameCapacity) {
        U8 *pNew = (U8 *)realloc(pCtx->pu8FrameBuf, totalSize);
        if (!pNew)
            return -1;
        pCtx->pu8FrameBuf = pNew;
        pCtx->u32FrameCapacity = totalSize;
    }

    /* Create ADTS header */
    AAC_CreateAdts(pCtx->pu8FrameBuf, pCtx->u8Profile, pCtx->u8SampleRateIdx, pCtx->u8ChannelConfig, auSize);

    /* Copy AU data */
    memcpy(&pCtx->pu8FrameBuf[7], pAuData, auSize);
    pCtx->u32FrameSize = totalSize;

    pDepkt->u64Timestamp = hdr.u32Timestamp;
    pDepkt->bFrameComplete = MPP_TRUE;

    return 0;
}

static S32 AacDepkt_GetFrame(RtpDepacketizer *pDepkt, U8 *pu8Buf, U32 u32BufSize, U32 *pu32Size) {
    AacDepacketizerCtx *pCtx = (AacDepacketizerCtx *)pDepkt;
    if (!pCtx || !pu8Buf || !pu32Size)
        return -1;

    if (!pDepkt->bFrameComplete || pCtx->u32FrameSize == 0) {
        *pu32Size = 0;
        return 0;
    }

    if (u32BufSize < pCtx->u32FrameSize)
        return -1;

    memcpy(pu8Buf, pCtx->pu8FrameBuf, pCtx->u32FrameSize);
    *pu32Size = pCtx->u32FrameSize;

    pCtx->u32FrameSize = 0;
    pDepkt->bFrameComplete = MPP_FALSE;

    return 0;
}

static void AacDepkt_Reset(RtpDepacketizer *pDepkt) {
    AacDepacketizerCtx *pCtx = (AacDepacketizerCtx *)pDepkt;
    if (!pCtx)
        return;

    pCtx->u32FrameSize = 0;
    pDepkt->bFrameComplete = MPP_FALSE;
}

static void AacDepkt_Destroy(RtpDepacketizer *pDepkt) {
    AacDepacketizerCtx *pCtx = (AacDepacketizerCtx *)pDepkt;
    if (!pCtx)
        return;

    if (pCtx->pu8FrameBuf) {
        free(pCtx->pu8FrameBuf);
    }
    free(pCtx);
}

RtpDepacketizer *AacDepacketizer_Create(U8 u8SizeLength, U8 u8IndexLength) {
    AacDepacketizerCtx *pCtx = (AacDepacketizerCtx *)calloc(1, sizeof(AacDepacketizerCtx));
    if (!pCtx)
        return NULL;

    pCtx->stBase.eCodec = DEMUX_CODEC_AAC;
    pCtx->stBase.pfnProcess = AacDepkt_Process;
    pCtx->stBase.pfnGetFrame = AacDepkt_GetFrame;
    pCtx->stBase.pfnReset = AacDepkt_Reset;
    pCtx->stBase.pfnDestroy = AacDepkt_Destroy;

    pCtx->u8SizeLength = u8SizeLength ? u8SizeLength : 13;
    pCtx->u8IndexLength = u8IndexLength ? u8IndexLength : 3;

    /* Default LC profile, 44100 Hz, stereo */
    pCtx->u8Profile = 2;       /* AAC-LC */
    pCtx->u8SampleRateIdx = 4; /* 44100 */
    pCtx->u8ChannelConfig = 2; /* Stereo */

    return (RtpDepacketizer *)pCtx;
}

S32 AacDepacketizer_SetConfig(RtpDepacketizer *pDepkt, const U8 *pu8Config, U32 u32Len) {
    AacDepacketizerCtx *pCtx = (AacDepacketizerCtx *)pDepkt;
    if (!pCtx || !pu8Config || u32Len < 2)
        return -1;

    if (u32Len > sizeof(pCtx->au8Config)) {
        u32Len = sizeof(pCtx->au8Config);
    }
    memcpy(pCtx->au8Config, pu8Config, u32Len);
    pCtx->u32ConfigLen = u32Len;

    /* Parse AudioSpecificConfig */
    AAC_ParseAsc(pu8Config, u32Len, &pCtx->u8Profile, &pCtx->u8SampleRateIdx, &pCtx->u8ChannelConfig);

    return 0;
}
