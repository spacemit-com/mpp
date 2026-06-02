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

#include "codec/h264_utils.h"
#include "codec/h265_utils.h"

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

    /* SPS/PPS/VPS in Annex-B format (with start codes), parsed from the AVC/HEVC
     * sequence header and prepended to each keyframe. */
    U8 au8AnnexBPS[1024];
    U32 u32AnnexBPSLen;
    U8 u8NalLenSize; /* NAL length field size from avcC/hvcC (1..4) */

    /* Read buffer */
    U8 au8ReadBuf[512 * 1024];

    /* Annex-B output buffer for converted packets */
    U8 au8AnnexBBuf[512 * 1024];

    /* State */
    BOOL bHeaderParsed;
    U32 u32LastTimestamp;
};

static U32 read_be24(const U8 *p) { return ((U32)p[0] << 16) | ((U32)p[1] << 8) | p[2]; }

static U32 read_be32(const U8 *p) { return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | p[3]; }

static U16 read_be16(const U8 *p) { return (U16)(((U16)p[0] << 8) | p[1]); }

static const U8 g_au8StartCode4[4] = {0x00, 0x00, 0x00, 0x01};

/* Parse an AVCDecoderConfigurationRecord (avcC) into Annex-B SPS/PPS stored in
 * pDemux->au8AnnexBPS, and record the NAL length field size. */
static S32 flv_parse_avcc(FlvDemuxer *pDemux, const U8 *pData, U32 u32Size) {
    if (u32Size < 7)
        return -1;

    pDemux->u8NalLenSize = (pData[4] & 0x03) + 1;

    U32 offset = 5;
    U32 outLen = 0;

    /* SPS array */
    U32 numSps = pData[offset++] & 0x1F;
    for (U32 i = 0; i < numSps; i++) {
        if (offset + 2 > u32Size)
            return -1;
        U16 spsLen = read_be16(pData + offset);
        offset += 2;
        if (offset + spsLen > u32Size)
            return -1;
        if (outLen + 4 + spsLen > sizeof(pDemux->au8AnnexBPS))
            return -1;
        memcpy(pDemux->au8AnnexBPS + outLen, g_au8StartCode4, 4);
        outLen += 4;
        memcpy(pDemux->au8AnnexBPS + outLen, pData + offset, spsLen);
        outLen += spsLen;
        offset += spsLen;
    }

    /* PPS array */
    if (offset >= u32Size)
        goto done;
    U32 numPps = pData[offset++];
    for (U32 i = 0; i < numPps; i++) {
        if (offset + 2 > u32Size)
            return -1;
        U16 ppsLen = read_be16(pData + offset);
        offset += 2;
        if (offset + ppsLen > u32Size)
            return -1;
        if (outLen + 4 + ppsLen > sizeof(pDemux->au8AnnexBPS))
            return -1;
        memcpy(pDemux->au8AnnexBPS + outLen, g_au8StartCode4, 4);
        outLen += 4;
        memcpy(pDemux->au8AnnexBPS + outLen, pData + offset, ppsLen);
        outLen += ppsLen;
        offset += ppsLen;
    }

done:
    pDemux->u32AnnexBPSLen = outLen;
    return 0;
}

/* Parse an HEVCDecoderConfigurationRecord (hvcC) into Annex-B VPS/SPS/PPS. */
static S32 flv_parse_hvcc(FlvDemuxer *pDemux, const U8 *pData, U32 u32Size) {
    if (u32Size < 23)
        return -1;

    pDemux->u8NalLenSize = (pData[21] & 0x03) + 1;

    U32 numArrays = pData[22];
    U32 offset = 23;
    U32 outLen = 0;

    for (U32 a = 0; a < numArrays; a++) {
        if (offset + 3 > u32Size)
            break;
        /* Low 6 bits of the first byte carry the HEVC NAL_unit_type. */
        U8 u8NalType = pData[offset] & 0x3F;
        offset += 1; /* array_completeness/reserved/NAL_unit_type */
        U16 numNalus = read_be16(pData + offset);
        offset += 2;

        for (U32 n = 0; n < numNalus; n++) {
            if (offset + 2 > u32Size)
                return -1;
            U16 nalLen = read_be16(pData + offset);
            offset += 2;
            if (offset + nalLen > u32Size)
                return -1;
            if (outLen + 4 + nalLen > sizeof(pDemux->au8AnnexBPS))
                return -1;
            memcpy(pDemux->au8AnnexBPS + outLen, g_au8StartCode4, 4);
            outLen += 4;
            memcpy(pDemux->au8AnnexBPS + outLen, pData + offset, nalLen);
            outLen += nalLen;

            /* HEVC SPS (nal_type 33) carries the resolution. Parse it here so
             * GetStreamInfo can report width/height like the H.264 path. */
            if (u8NalType == 33) {
                H265_ParseSps(pData + offset, nalLen, &pDemux->u32Width, &pDemux->u32Height);
            }

            offset += nalLen;
        }
    }

    pDemux->u32AnnexBPSLen = outLen;
    return 0;
}

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

    /* FLV header TypeFlags byte: bit0 = audio present, bit2 = video present. */
    pDemux->bHasAudio = (au8Hdr[4] & 0x01) != 0;
    pDemux->bHasVideo = (au8Hdr[4] & 0x04) != 0;

    /* Skip data offset to first tag */
    U32 u32DataOffset = read_be32(&au8Hdr[5]);
    fseek(pDemux->pFile, u32DataOffset, SEEK_SET);

    /* Skip first PreviousTagSize (4 bytes) */
    fseek(pDemux->pFile, 4, SEEK_CUR);

    /* The codec is not known until the first video tag (with its codec id)
     * is parsed in ReadPacket. Mark it UNKNOWN so GetStreamInfo cannot report
     * a bogus codec: note DEMUX_CODEC_H264 == 0, so a zero-initialised struct
     * would otherwise masquerade as H.264 before any video tag is seen. */
    pDemux->eCodec = DEMUX_CODEC_UNKNOWN;

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

    /* The codec id only becomes known once the first video tag is parsed. If
     * GetStreamInfo is queried before then, report failure rather than a
     * fabricated codec (DEMUX_CODEC_H264 == 0 would otherwise leak through). */
    if (pDemux->eCodec == DEMUX_CODEC_UNKNOWN)
        return ERR_DEMUX_NO_STREAM;

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
        /* The extended-timestamp byte is the high 8 bits. Cast to U32 before
         * the shift; shifting a value >= 0x80 as a promoted (signed) int by 24
         * would overflow and is undefined behaviour. */
        U32 u32Timestamp = read_be24(&au8TagHdr[4]) | ((U32)au8TagHdr[7] << 24);

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
                /* Store codec config and parse it into Annex-B parameter sets. */
                U32 extraLen = u32DataSize - 5;
                if (extraLen <= sizeof(pDemux->au8ExtraData)) {
                    memcpy(pDemux->au8ExtraData, &pDemux->au8ReadBuf[5], extraLen);
                    pDemux->u32ExtraDataLen = extraLen;

                    if (pDemux->eCodec == DEMUX_CODEC_H264) {
                        flv_parse_avcc(pDemux, &pDemux->au8ReadBuf[5], extraLen);
                        /* SPS is the first parameter set after a 4-byte start code. */
                        if (pDemux->u32AnnexBPSLen > 4) {
                            U8 *pSps = pDemux->au8AnnexBPS + 4;
                            U32 u32SpsLen = pDemux->u32AnnexBPSLen - 4;
                            H264_ParseSps(pSps, u32SpsLen, &pDemux->u32Width, &pDemux->u32Height);
                        }
                    } else {
                        flv_parse_hvcc(pDemux, &pDemux->au8ReadBuf[5], extraLen);
                    }
                }
                continue; /* Don't output sequence header as packet */
            }

            if (u8AvcType == AVC_NALU) {
                /* FLV carries AVC/HEVC NALUs in length-prefixed form. Convert
                 * to Annex-B (start-code prefixed) so the output matches the
                 * MP4 path, and prepend SPS/PPS/VPS before each keyframe. */
                const U8 *pNalData = &pDemux->au8ReadBuf[5];
                U32 u32NalDataSize = u32DataSize - 5;
                BOOL bKeyFrame = (u8FrameType == 1);

                /* The tag timestamp is the DTS. For AVC/HEVC, bytes [2..4] of
                 * the video tag hold a signed 24-bit CompositionTime offset
                 * (in ms); PTS = DTS + CompositionTime. Sign-extend the 24-bit
                 * value before adding. */
                S32 s32Cts = (S32)read_be24(&pDemux->au8ReadBuf[2]);
                if (s32Cts & 0x00800000)
                    s32Cts |= (S32)0xFF000000; /* sign-extend bit 23 */
                S64 s64Pts = (S64)u32Timestamp + s32Cts;
                if (s64Pts < 0)
                    s64Pts = 0; /* clamp: PTS must not go negative */

                U8 *pOut = pDemux->au8AnnexBBuf;
                U32 u32MaxOut = sizeof(pDemux->au8AnnexBBuf);
                U32 u32OutLen = 0;
                U8 nls = pDemux->u8NalLenSize ? pDemux->u8NalLenSize : 4;

                /* Inject parameter sets before keyframes. A keyframe must carry
                 * its parameter sets to be decodable, so if they exist but do
                 * not fit (including the case where they exactly fill the
                 * buffer, which the old '<' test wrongly rejected) skip the tag
                 * rather than emit a keyframe with missing SPS/PPS/VPS. */
                if (bKeyFrame && pDemux->u32AnnexBPSLen > 0) {
                    if (pDemux->u32AnnexBPSLen > u32MaxOut) {
                        continue; /* parameter sets cannot fit: drop this tag */
                    }
                    memcpy(pOut, pDemux->au8AnnexBPS, pDemux->u32AnnexBPSLen);
                    u32OutLen += pDemux->u32AnnexBPSLen;
                }

                /* Convert each length-prefixed NAL to a start-code-prefixed NAL. */
                U32 inPos = 0;
                BOOL bConvertOk = MPP_TRUE;
                while (inPos + nls <= u32NalDataSize) {
                    U32 nalLen = 0;
                    for (U8 b = 0; b < nls; b++) {
                        nalLen = (nalLen << 8) | pNalData[inPos + b];
                    }
                    inPos += nls;
                    if (inPos + nalLen > u32NalDataSize) {
                        /* Truncated NAL: declared length exceeds the tag. */
                        bConvertOk = MPP_FALSE;
                        break;
                    }
                    if (u32OutLen + 4 + nalLen > u32MaxOut) {
                        /* Output buffer too small for the whole frame. */
                        bConvertOk = MPP_FALSE;
                        break;
                    }

                    memcpy(pOut + u32OutLen, g_au8StartCode4, 4);
                    u32OutLen += 4;
                    memcpy(pOut + u32OutLen, pNalData + inPos, nalLen);
                    u32OutLen += nalLen;
                    inPos += nalLen;
                }

                /* Skip this tag instead of emitting a truncated/empty packet
                 * when conversion failed, the input was not fully consumed, or
                 * no NAL payload was produced. */
                if (!bConvertOk || inPos != u32NalDataSize || u32OutLen == 0) {
                    continue;
                }

                pstPkt->pu8Data = pOut;
                pstPkt->u32Size = u32OutLen;
                pstPkt->bKeyFrame = bKeyFrame;
                pstPkt->eCodecType = pDemux->eCodec;
                pstPkt->u64PTS = (U64)s64Pts * 1000; /* ms to us */
                pstPkt->u32Width = pDemux->u32Width;
                pstPkt->u32Height = pDemux->u32Height;

                return 0;
            }
        }
        /* Skip audio and script tags */
    }
}
