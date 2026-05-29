/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    flv_demuxer.c
 * @Brief     :    FLV container demuxer implementation.
 *------------------------------------------------------------------------------
 */

#include "flv_demuxer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FLV tag types */
#define FLV_TAG_AUDIO 8
#define FLV_TAG_VIDEO 9
#define FLV_TAG_SCRIPT 18

/* Video codec IDs */
#define FLV_VIDEO_H264 7
#define FLV_VIDEO_H265 12

/* AVC packet types */
#define AVC_SEQUENCE_HEADER 0
#define AVC_NALU 1
#define AVC_END_OF_SEQUENCE 2

struct _FlvDemuxer {
    FILE *pFile;
    BOOL bHasVideo;
    BOOL bHasAudio;

    /* Stream info */
    DemuxCodecType eCodec;
    U32 u32Width;
    U32 u32Height;

    /* Codec config */
    U8 au8ExtraData[1024];
    U32 u32ExtraDataLen;

    /* Read buffer */
    U8 au8ReadBuf[512 * 1024];

    /* State */
    BOOL bHeaderParsed;
    U32 u32LastTimestamp;
};

static U32 read_be24(const U8 *p) { return ((U32)p[0] << 16) | ((U32)p[1] << 8) | p[2]; }

static U32 read_be32(const U8 *p) { return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | p[3]; }

FlvDemuxer *FlvDemuxer_Create(VOID) { return (FlvDemuxer *)calloc(1, sizeof(FlvDemuxer)); }

VOID FlvDemuxer_Destroy(FlvDemuxer *pDemux) {
    if (pDemux) {
        FlvDemuxer_Close(pDemux);
        free(pDemux);
    }
}

S32 FlvDemuxer_Open(FlvDemuxer *pDemux, const CHAR *pszPath) {
    if (!pDemux || !pszPath)
        return ERR_DEMUX_NULL_PTR;

    pDemux->pFile = fopen(pszPath, "rb");
    if (!pDemux->pFile) {
        return ERR_DEMUX_OPEN_FAIL;
    }

    /* Read FLV header (9 bytes) */
    U8 au8Hdr[9];
    if (fread(au8Hdr, 1, 9, pDemux->pFile) != 9) {
        fclose(pDemux->pFile);
        pDemux->pFile = NULL;
        return ERR_DEMUX_OPEN_FAIL;
    }

    /* Verify signature "FLV" */
    if (au8Hdr[0] != 'F' || au8Hdr[1] != 'L' || au8Hdr[2] != 'V') {
        fclose(pDemux->pFile);
        pDemux->pFile = NULL;
        return ERR_DEMUX_OPEN_FAIL;
    }

    pDemux->bHasVideo = (au8Hdr[4] & 0x01) != 0;
    pDemux->bHasAudio = (au8Hdr[4] & 0x04) != 0;

    /* Skip data offset to first tag */
    U32 u32DataOffset = read_be32(&au8Hdr[5]);
    fseek(pDemux->pFile, u32DataOffset, SEEK_SET);

    /* Skip first PreviousTagSize (4 bytes) */
    fseek(pDemux->pFile, 4, SEEK_CUR);

    pDemux->bHeaderParsed = MPP_TRUE;

    return 0;
}

VOID FlvDemuxer_Close(FlvDemuxer *pDemux) {
    if (pDemux && pDemux->pFile) {
        fclose(pDemux->pFile);
        pDemux->pFile = NULL;
    }
}

S32 FlvDemuxer_GetStreamInfo(FlvDemuxer *pDemux, DemuxStreamInfo *pstInfo) {
    if (!pDemux || !pstInfo)
        return ERR_DEMUX_NULL_PTR;

    pstInfo->eCodecType = pDemux->eCodec;
    pstInfo->u32Width = pDemux->u32Width;
    pstInfo->u32Height = pDemux->u32Height;
    pstInfo->u32Fps = 25;

    return 0;
}

S32 FlvDemuxer_ReadPacket(FlvDemuxer *pDemux, DemuxPacket *pstPkt) {
    if (!pDemux || !pstPkt || !pDemux->pFile)
        return ERR_DEMUX_NULL_PTR;

    while (1) {
        /* Read tag header (11 bytes) */
        U8 au8TagHdr[11];
        if (fread(au8TagHdr, 1, 11, pDemux->pFile) != 11) {
            return -1; /* EOF */
        }

        U8 u8TagType = au8TagHdr[0];
        U32 u32DataSize = read_be24(&au8TagHdr[1]);
        U32 u32Timestamp = read_be24(&au8TagHdr[4]) | (au8TagHdr[7] << 24);

        if (u32DataSize > sizeof(pDemux->au8ReadBuf)) {
            /* Skip oversized tag */
            fseek(pDemux->pFile, u32DataSize + 4, SEEK_CUR);
            continue;
        }

        if (fread(pDemux->au8ReadBuf, 1, u32DataSize, pDemux->pFile) != u32DataSize) {
            return -1;
        }

        /* Skip PreviousTagSize */
        fseek(pDemux->pFile, 4, SEEK_CUR);

        if (u8TagType == FLV_TAG_VIDEO && u32DataSize > 5) {
            U8 u8FrameType = (pDemux->au8ReadBuf[0] >> 4) & 0x0F;
            U8 u8CodecId = pDemux->au8ReadBuf[0] & 0x0F;
            U8 u8AvcType = pDemux->au8ReadBuf[1];

            if (u8CodecId == FLV_VIDEO_H264) {
                pDemux->eCodec = DEMUX_CODEC_H264;
            } else if (u8CodecId == FLV_VIDEO_H265) {
                pDemux->eCodec = DEMUX_CODEC_H265;
            } else {
                continue; /* Unsupported codec */
            }

            if (u8AvcType == AVC_SEQUENCE_HEADER) {
                /* Store codec config */
                U32 extraLen = u32DataSize - 5;
                if (extraLen <= sizeof(pDemux->au8ExtraData)) {
                    memcpy(pDemux->au8ExtraData, &pDemux->au8ReadBuf[5], extraLen);
                    pDemux->u32ExtraDataLen = extraLen;
                }
                continue; /* Don't output sequence header as packet */
            }

            if (u8AvcType == AVC_NALU) {
                pstPkt->pu8Data = &pDemux->au8ReadBuf[5];
                pstPkt->u32Size = u32DataSize - 5;
                pstPkt->bKeyFrame = (u8FrameType == 1);
                pstPkt->eCodecType = pDemux->eCodec;
                pstPkt->u64PTS = u32Timestamp * 1000; /* ms to us */
                pstPkt->u32Width = pDemux->u32Width;
                pstPkt->u32Height = pDemux->u32Height;

                return 0;
            }
        }
        /* Skip audio and script tags */
    }
}
