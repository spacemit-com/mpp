/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    mp4_demuxer.c
 * @Brief     :    MP4/MOV container demuxer implementation.
 *------------------------------------------------------------------------------
 */

#include "mp4_demuxer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Box types (FourCC) */
#define BOX_FTYP 0x66747970
#define BOX_MOOV 0x6D6F6F76
#define BOX_MVHD 0x6D766864
#define BOX_TRAK 0x7472616B
#define BOX_TKHD 0x746B6864
#define BOX_MDIA 0x6D646961
#define BOX_MDHD 0x6D646864
#define BOX_HDLR 0x68646C72
#define BOX_MINF 0x6D696E66
#define BOX_STBL 0x7374626C
#define BOX_STSD 0x73747364
#define BOX_STTS 0x73747473
#define BOX_STSS 0x73747373
#define BOX_STSC 0x73747363
#define BOX_STSZ 0x7374737A
#define BOX_STCO 0x7374636F
#define BOX_CO64 0x636F3634
#define BOX_MDAT 0x6D646174
#define BOX_AVC1 0x61766331
#define BOX_HVC1 0x68766331
#define BOX_HEV1 0x68657631
#define BOX_AVCC 0x61766343
#define BOX_HVCC 0x68766343

#define MAX_SAMPLES 100000

typedef struct _SampleEntry {
    U64 u64Offset;
    U32 u32Size;
    U32 u32Duration;
    BOOL bSync;
} SampleEntry;

typedef struct _TrackInfo {
    U32 u32Id;
    U32 u32Timescale;
    U64 u64Duration;
    DemuxCodecType eCodec;
    U32 u32Width;
    U32 u32Height;

    /* Codec specific */
    U8 au8ExtraData[1024];
    U32 u32ExtraDataLen;

    /* SPS/PPS in Annex-B format (with start codes), prepended to each keyframe */
    U8 au8AnnexBPS[1024];
    U32 u32AnnexBPSLen;
    U8 u8NalLenSize; /* 1, 2, or 4 from avcC */

    /* Sample table */
    SampleEntry *pstSamples;
    U32 u32SampleCount;
    U32 u32CurrentSample;
} TrackInfo;

struct _Mp4Demuxer {
    FILE *pFile;
    S64 s64FileSize;

    /* Movie info */
    U32 u32Timescale;
    U64 u64Duration;

    /* Tracks */
    TrackInfo stVideoTrack;
    TrackInfo stAudioTrack;
    BOOL bHasVideo;
    BOOL bHasAudio;

    /* Read buffer */
    U8 au8ReadBuf[512 * 1024];

    /* Output buffer for Annex-B converted packet */
    U8 au8AnnexBBuf[512 * 1024];

    /* Stream info cache */
    DemuxStreamInfo stStreamInfo;
};

/* Helper: Read big endian integers */
static U32 read_be32(const U8 *p) { return ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | p[3]; }

static U64 read_be64(const U8 *p) { return ((U64)read_be32(p) << 32) | read_be32(p + 4); }

static U16 read_be16(const U8 *p) { return ((U16)p[0] << 8) | p[1]; }

/* Parse avcC box to extract NAL length size, SPS, PPS in Annex-B format */
static S32 parse_avcc(TrackInfo *pTrack, const U8 *pData, U32 u32Size) {
    if (u32Size < 7)
        return -1;

    /* avcC structure:
     *  configurationVersion(8)=1, AVCProfileIndication(8), profile_compatibility(8),
     *  AVCLevelIndication(8), reserved(6)=0x3F | lengthSizeMinusOne(2),
     *  reserved(3)=0x07 | numOfSequenceParameterSets(5)
     *  for each SPS: u16 length, u8[length] data
     *  numOfPictureParameterSets(8), for each PPS: u16 length, u8[length] data
     */
    pTrack->u8NalLenSize = (pData[4] & 0x03) + 1;

    U32 offset = 5;
    U32 outLen = 0;
    static const U8 startCode[4] = {0x00, 0x00, 0x00, 0x01};

    /* SPS array */
    if (offset >= u32Size)
        return -1;
    U32 numSps = pData[offset++] & 0x1F;
    for (U32 i = 0; i < numSps; i++) {
        if (offset + 2 > u32Size)
            return -1;
        U16 spsLen = read_be16(pData + offset);
        offset += 2;
        if (offset + spsLen > u32Size)
            return -1;
        if (outLen + 4 + spsLen > sizeof(pTrack->au8AnnexBPS))
            return -1;
        memcpy(pTrack->au8AnnexBPS + outLen, startCode, 4);
        outLen += 4;
        memcpy(pTrack->au8AnnexBPS + outLen, pData + offset, spsLen);
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
        if (outLen + 4 + ppsLen > sizeof(pTrack->au8AnnexBPS))
            return -1;
        memcpy(pTrack->au8AnnexBPS + outLen, startCode, 4);
        outLen += 4;
        memcpy(pTrack->au8AnnexBPS + outLen, pData + offset, ppsLen);
        outLen += ppsLen;
        offset += ppsLen;
    }

done:
    pTrack->u32AnnexBPSLen = outLen;
    return 0;
}

/* Parse stsd box to get codec info */
static S32 parse_stsd(Mp4Demuxer *pDemux, TrackInfo *pTrack, const U8 *pData, U32 u32Size) {
    if (u32Size < 16)
        return -1;

    U32 u32EntryCount = read_be32(pData + 4);
    if (u32EntryCount < 1)
        return -1;

    const U8 *pEntry = pData + 8;
    U32 u32EntrySize = read_be32(pEntry);
    U32 u32EntryType = read_be32(pEntry + 4);

    if (u32EntryType == BOX_AVC1) {
        pTrack->eCodec = DEMUX_CODEC_H264;
        pTrack->u32Width = read_be16(pEntry + 24 + 8); /* +8 for visual sample entry header */
        pTrack->u32Height = read_be16(pEntry + 26 + 8);

        /* Find avcC box */
        const U8 *p = pEntry + 78 + 8; /* skip visual sample entry header */
        const U8 *pEnd = pEntry + u32EntrySize;
        while (p + 8 < pEnd) {
            U32 boxSize = read_be32(p);
            U32 boxType = read_be32(p + 4);
            if (boxType == BOX_AVCC && boxSize > 8) {
                U32 len = boxSize - 8;
                if (len > sizeof(pTrack->au8ExtraData)) {
                    len = sizeof(pTrack->au8ExtraData);
                }
                memcpy(pTrack->au8ExtraData, p + 8, len);
                pTrack->u32ExtraDataLen = len;
                /* Convert avcC to Annex-B SPS/PPS */
                parse_avcc(pTrack, p + 8, len);
                break;
            }
            p += boxSize;
        }
    } else if (u32EntryType == BOX_HVC1 || u32EntryType == BOX_HEV1) {
        pTrack->eCodec = DEMUX_CODEC_H265;
        pTrack->u32Width = read_be16(pEntry + 24 + 8);
        pTrack->u32Height = read_be16(pEntry + 26 + 8);

        /* Find hvcC box */
        const U8 *p = pEntry + 78 + 8;
        const U8 *pEnd = pEntry + u32EntrySize;
        while (p + 8 < pEnd) {
            U32 boxSize = read_be32(p);
            U32 boxType = read_be32(p + 4);
            if (boxType == BOX_HVCC && boxSize > 8) {
                U32 len = boxSize - 8;
                if (len > sizeof(pTrack->au8ExtraData)) {
                    len = sizeof(pTrack->au8ExtraData);
                }
                memcpy(pTrack->au8ExtraData, p + 8, len);
                pTrack->u32ExtraDataLen = len;
                break;
            }
            p += boxSize;
        }
    }

    return 0;
}

/* Parse sample table (stbl) */
static S32 parse_stbl(Mp4Demuxer *pDemux, TrackInfo *pTrack, const U8 *pData, U32 u32Size) {
    U32 u32Pos = 0;

    /* Temporary storage */
    U32 *pu32Sizes = NULL;
    U32 u32SizeCount = 0;
    U64 *pu64Offsets = NULL;
    U32 u32OffsetCount = 0;
    U32 *pu32SyncSamples = NULL;
    U32 u32SyncCount = 0;

    while (u32Pos + 8 <= u32Size) {
        U32 boxSize = read_be32(pData + u32Pos);
        U32 boxType = read_be32(pData + u32Pos + 4);

        if (boxSize < 8 || u32Pos + boxSize > u32Size)
            break;

        const U8 *pBox = pData + u32Pos + 8;
        U32 boxDataLen = boxSize - 8;

        switch (boxType) {
        case BOX_STSD:
            parse_stsd(pDemux, pTrack, pBox, boxDataLen);
            break;

        case BOX_STSZ:
            if (boxDataLen >= 12) {
                U32 sampleSize = read_be32(pBox + 4);
                u32SizeCount = read_be32(pBox + 8);

                if (sampleSize == 0 && u32SizeCount > 0) {
                    pu32Sizes = (U32 *)malloc(u32SizeCount * sizeof(U32));
                    if (pu32Sizes && boxDataLen >= 12 + u32SizeCount * 4) {
                        for (U32 i = 0; i < u32SizeCount; i++) {
                            pu32Sizes[i] = read_be32(pBox + 12 + i * 4);
                        }
                    }
                }
            }
            break;

        case BOX_STCO:
            if (boxDataLen >= 8) {
                u32OffsetCount = read_be32(pBox + 4);
                if (u32OffsetCount > 0) {
                    pu64Offsets = (U64 *)malloc(u32OffsetCount * sizeof(U64));
                    if (pu64Offsets && boxDataLen >= 8 + u32OffsetCount * 4) {
                        for (U32 i = 0; i < u32OffsetCount; i++) {
                            pu64Offsets[i] = read_be32(pBox + 8 + i * 4);
                        }
                    }
                }
            }
            break;

        case BOX_CO64:
            if (boxDataLen >= 8) {
                u32OffsetCount = read_be32(pBox + 4);
                if (u32OffsetCount > 0) {
                    pu64Offsets = (U64 *)malloc(u32OffsetCount * sizeof(U64));
                    if (pu64Offsets && boxDataLen >= 8 + u32OffsetCount * 8) {
                        for (U32 i = 0; i < u32OffsetCount; i++) {
                            pu64Offsets[i] = read_be64(pBox + 8 + i * 8);
                        }
                    }
                }
            }
            break;

        case BOX_STSS:
            if (boxDataLen >= 8) {
                u32SyncCount = read_be32(pBox + 4);
                if (u32SyncCount > 0) {
                    pu32SyncSamples = (U32 *)malloc(u32SyncCount * sizeof(U32));
                    if (pu32SyncSamples && boxDataLen >= 8 + u32SyncCount * 4) {
                        for (U32 i = 0; i < u32SyncCount; i++) {
                            pu32SyncSamples[i] = read_be32(pBox + 8 + i * 4);
                        }
                    }
                }
            }
            break;
        }

        u32Pos += boxSize;
    }

    /* Build sample table (simplified: assuming 1 sample per chunk) */
    if (pu32Sizes && pu64Offsets && u32SizeCount > 0) {
        pTrack->pstSamples = (SampleEntry *)malloc(u32SizeCount * sizeof(SampleEntry));
        if (pTrack->pstSamples) {
            pTrack->u32SampleCount = u32SizeCount;

            for (U32 i = 0; i < u32SizeCount; i++) {
                pTrack->pstSamples[i].u32Size = pu32Sizes[i];
                pTrack->pstSamples[i].u64Offset =
                    (i < u32OffsetCount) ? pu64Offsets[i] : pu64Offsets[u32OffsetCount - 1];
                pTrack->pstSamples[i].bSync = MPP_FALSE;
            }

            /* Mark sync samples */
            if (pu32SyncSamples) {
                for (U32 i = 0; i < u32SyncCount; i++) {
                    U32 idx = pu32SyncSamples[i] - 1;
                    if (idx < pTrack->u32SampleCount) {
                        pTrack->pstSamples[idx].bSync = MPP_TRUE;
                    }
                }
            } else {
                /* All samples are sync if no stss */
                for (U32 i = 0; i < pTrack->u32SampleCount; i++) {
                    pTrack->pstSamples[i].bSync = MPP_TRUE;
                }
            }
        }
    }

    if (pu32Sizes)
        free(pu32Sizes);
    if (pu64Offsets)
        free(pu64Offsets);
    if (pu32SyncSamples)
        free(pu32SyncSamples);

    return 0;
}

Mp4Demuxer *Mp4Demuxer_Create(VOID) {
    Mp4Demuxer *pDemux = (Mp4Demuxer *)calloc(1, sizeof(Mp4Demuxer));
    return pDemux;
}

VOID Mp4Demuxer_Destroy(Mp4Demuxer *pDemux) {
    if (pDemux) {
        Mp4Demuxer_Close(pDemux);
        free(pDemux);
    }
}

/* Parse stbl (sample table) box */
static S32 parse_stbl_box(Mp4Demuxer *pDemux, TrackInfo *pTrack, const U8 *pData, U32 u32Size) {
    U32 offset = 0;
    U32 *pChunkOffsets = NULL;
    U32 u32ChunkCount = 0;
    U32 *pSampleSizes = NULL;
    U32 u32SampleSizeCount = 0;
    U32 *pSyncSamples = NULL;
    U32 u32SyncCount = 0;

    /* Parse child boxes */
    while (offset + 8 <= u32Size) {
        U32 boxSize = read_be32(pData + offset);
        U32 boxType = read_be32(pData + offset + 4);

        if (boxSize < 8 || offset + boxSize > u32Size)
            break;

        const U8 *pBox = pData + offset + 8;
        U32 boxDataSize = boxSize - 8;

        if (boxType == BOX_STSD) {
            /* Sample description - contains codec info and avcC extradata */
            parse_stsd(pDemux, pTrack, pBox, boxDataSize);
        } else if (boxType == BOX_STCO && boxDataSize >= 8) {
            /* Chunk offset table */
            u32ChunkCount = read_be32(pBox + 4);
            if (u32ChunkCount > 0 && u32ChunkCount < 100000 && boxDataSize >= 8 + u32ChunkCount * 4) {
                pChunkOffsets = (U32 *)malloc(u32ChunkCount * sizeof(U32));
                for (U32 i = 0; i < u32ChunkCount; i++) {
                    pChunkOffsets[i] = read_be32(pBox + 8 + i * 4);
                }
            }
        } else if (boxType == BOX_STSZ && boxDataSize >= 12) {
            /* Sample size table */
            U32 uniformSize = read_be32(pBox + 4);
            u32SampleSizeCount = read_be32(pBox + 8);

            if (uniformSize == 0 && u32SampleSizeCount > 0 && u32SampleSizeCount < MAX_SAMPLES) {
                if (boxDataSize >= 12 + u32SampleSizeCount * 4) {
                    pSampleSizes = (U32 *)malloc(u32SampleSizeCount * sizeof(U32));
                    for (U32 i = 0; i < u32SampleSizeCount; i++) {
                        pSampleSizes[i] = read_be32(pBox + 12 + i * 4);
                    }
                }
            } else if (uniformSize > 0) {
                /* All samples same size */
                pSampleSizes = (U32 *)malloc(u32SampleSizeCount * sizeof(U32));
                for (U32 i = 0; i < u32SampleSizeCount; i++) {
                    pSampleSizes[i] = uniformSize;
                }
            }
        } else if (boxType == BOX_STSS && boxDataSize >= 8) {
            /* Sync sample table (keyframes) */
            u32SyncCount = read_be32(pBox + 4);
            if (u32SyncCount > 0 && u32SyncCount < MAX_SAMPLES && boxDataSize >= 8 + u32SyncCount * 4) {
                pSyncSamples = (U32 *)malloc(u32SyncCount * sizeof(U32));
                for (U32 i = 0; i < u32SyncCount; i++) {
                    pSyncSamples[i] = read_be32(pBox + 8 + i * 4);
                }
            }
        }

        offset += boxSize;
    }

    /* Build sample table (simplified: assume 1 sample per chunk) */
    if (pChunkOffsets && pSampleSizes && u32ChunkCount > 0 && u32SampleSizeCount > 0) {
        U32 sampleCount = (u32ChunkCount < u32SampleSizeCount) ? u32ChunkCount : u32SampleSizeCount;
        pTrack->pstSamples = (SampleEntry *)calloc(sampleCount, sizeof(SampleEntry));
        pTrack->u32SampleCount = sampleCount;

        for (U32 i = 0; i < sampleCount; i++) {
            pTrack->pstSamples[i].u64Offset = pChunkOffsets[i];
            pTrack->pstSamples[i].u32Size = pSampleSizes[i];
            /* If no stss, all samples are sync */
            pTrack->pstSamples[i].bSync = (pSyncSamples == NULL) ? MPP_TRUE : MPP_FALSE;
        }

        /* Mark sync samples (1-based index in stss) */
        if (pSyncSamples) {
            for (U32 i = 0; i < u32SyncCount; i++) {
                U32 idx = pSyncSamples[i] - 1;
                if (idx < sampleCount) {
                    pTrack->pstSamples[idx].bSync = MPP_TRUE;
                }
            }
        }
    }

    if (pChunkOffsets)
        free(pChunkOffsets);
    if (pSampleSizes)
        free(pSampleSizes);
    if (pSyncSamples)
        free(pSyncSamples);

    return 0;
}

/* Parse trak (track) box */
static S32 parse_trak_box(Mp4Demuxer *pDemux, TrackInfo *pTrack, const U8 *pData, U32 u32Size) {
    U32 offset = 0;

    while (offset + 8 <= u32Size) {
        U32 boxSize = read_be32(pData + offset);
        U32 boxType = read_be32(pData + offset + 4);

        if (boxSize < 8 || offset + boxSize > u32Size)
            break;

        const U8 *pBox = pData + offset + 8;
        U32 boxDataSize = boxSize - 8;

        if (boxType == BOX_MDIA) {
            /* Parse mdia -> minf -> stbl */
            U32 mdiaOff = 0;
            while (mdiaOff + 8 <= boxDataSize) {
                U32 mdiaBoxSize = read_be32(pBox + mdiaOff);
                U32 mdiaBoxType = read_be32(pBox + mdiaOff + 4);

                if (mdiaBoxSize < 8 || mdiaOff + mdiaBoxSize > boxDataSize)
                    break;

                if (mdiaBoxType == BOX_MINF) {
                    const U8 *pMinf = pBox + mdiaOff + 8;
                    U32 minfSize = mdiaBoxSize - 8;

                    /* Find stbl in minf */
                    U32 minfOff = 0;
                    while (minfOff + 8 <= minfSize) {
                        U32 minfBoxSize = read_be32(pMinf + minfOff);
                        U32 minfBoxType = read_be32(pMinf + minfOff + 4);

                        if (minfBoxSize < 8 || minfOff + minfBoxSize > minfSize)
                            break;

                        if (minfBoxType == BOX_STBL) {
                            parse_stbl_box(pDemux, pTrack, pMinf + minfOff + 8, minfBoxSize - 8);
                            break;
                        }

                        minfOff += minfBoxSize;
                    }
                    break;
                }

                mdiaOff += mdiaBoxSize;
            }
        }

        offset += boxSize;
    }

    return 0;
}

/* Parse moov (movie) box */
static S32 parse_moov_box(Mp4Demuxer *pDemux, const U8 *pData, U32 u32Size) {
    U32 offset = 0;

    while (offset + 8 <= u32Size) {
        U32 boxSize = read_be32(pData + offset);
        U32 boxType = read_be32(pData + offset + 4);

        if (boxSize < 8 || offset + boxSize > u32Size)
            break;

        const U8 *pBox = pData + offset + 8;
        U32 boxDataSize = boxSize - 8;

        if (boxType == BOX_TRAK) {
            /* Parse track - assume first video track */
            if (!pDemux->bHasVideo) {
                pDemux->bHasVideo = MPP_TRUE;
                pDemux->stVideoTrack.eCodec = DEMUX_CODEC_H264;
                pDemux->stVideoTrack.u32Width = 640;
                pDemux->stVideoTrack.u32Height = 480;
                parse_trak_box(pDemux, &pDemux->stVideoTrack, pBox, boxDataSize);
            }
        }

        offset += boxSize;
    }

    return 0;
}

/* Forward declarations for parsing */
static S32 parse_moov_box(Mp4Demuxer *pDemux, const U8 *pData, U32 u32Size);
static S32 parse_trak_box(Mp4Demuxer *pDemux, TrackInfo *pTrack, const U8 *pData, U32 u32Size);
static S32 parse_stbl_box(Mp4Demuxer *pDemux, TrackInfo *pTrack, const U8 *pData, U32 u32Size);

S32 Mp4Demuxer_Open(Mp4Demuxer *pDemux, const CHAR *pszPath) {
    if (!pDemux || !pszPath)
        return ERR_DEMUX_NULL_PTR;

    pDemux->pFile = fopen(pszPath, "rb");
    if (!pDemux->pFile) {
        fprintf(stderr, "[MP4] Failed to open file: %s\n", pszPath);
        return ERR_DEMUX_OPEN_FAIL;
    }

    /* Get file size */
    fseek(pDemux->pFile, 0, SEEK_END);
    pDemux->s64FileSize = ftell(pDemux->pFile);
    fseek(pDemux->pFile, 0, SEEK_SET);

    fprintf(stderr, "[MP4] File size: %lld bytes\n", (int64_t)pDemux->s64FileSize);

    /* Parse top-level boxes */
    U8 au8Hdr[16];
    while (fread(au8Hdr, 1, 8, pDemux->pFile) == 8) {
        U64 boxSize = read_be32(au8Hdr);
        U32 boxType = read_be32(au8Hdr + 4);

        if (boxSize == 1) {
            if (fread(au8Hdr + 8, 1, 8, pDemux->pFile) != 8)
                break;
            boxSize = read_be64(au8Hdr + 8);
        }

        char typeStr[5];
        typeStr[0] = (boxType >> 24) & 0xFF;
        typeStr[1] = (boxType >> 16) & 0xFF;
        typeStr[2] = (boxType >> 8) & 0xFF;
        typeStr[3] = boxType & 0xFF;
        typeStr[4] = '\0';
        fprintf(stderr, "[MP4] Found box: %s size=%llu\n", typeStr, (uint64_t)boxSize);

        if (boxType == BOX_MOOV) {
            /* Read and parse moov box */
            U32 moovSize = (U32)boxSize - 8;
            U8 *pMoov = (U8 *)malloc(moovSize);
            if (pMoov && fread(pMoov, 1, moovSize, pDemux->pFile) == moovSize) {
                parse_moov_box(pDemux, pMoov, moovSize);
            }
            if (pMoov)
                free(pMoov);
            break;
        }

        /* Skip to next box */
        fseek(pDemux->pFile, boxSize - 8, SEEK_CUR);
    }

    if (pDemux->bHasVideo) {
        fprintf(stderr, "[MP4] Video track: %ux%u, %u samples\n", pDemux->stVideoTrack.u32Width,
            pDemux->stVideoTrack.u32Height, pDemux->stVideoTrack.u32SampleCount);
    }

    return 0;
}

VOID Mp4Demuxer_Close(Mp4Demuxer *pDemux) {
    if (!pDemux)
        return;

    if (pDemux->pFile) {
        fclose(pDemux->pFile);
        pDemux->pFile = NULL;
    }

    if (pDemux->stVideoTrack.pstSamples) {
        free(pDemux->stVideoTrack.pstSamples);
        pDemux->stVideoTrack.pstSamples = NULL;
    }

    if (pDemux->stAudioTrack.pstSamples) {
        free(pDemux->stAudioTrack.pstSamples);
        pDemux->stAudioTrack.pstSamples = NULL;
    }
}

S32 Mp4Demuxer_GetStreamInfo(Mp4Demuxer *pDemux, DemuxStreamInfo *pstInfo) {
    if (!pDemux || !pstInfo)
        return ERR_DEMUX_NULL_PTR;

    pstInfo->eCodecType = pDemux->stVideoTrack.eCodec;
    pstInfo->u32Width = pDemux->stVideoTrack.u32Width;
    pstInfo->u32Height = pDemux->stVideoTrack.u32Height;
    pstInfo->u32Fps = 25; /* TODO: Calculate from timescale/duration */

    return 0;
}

S32 Mp4Demuxer_ReadPacket(Mp4Demuxer *pDemux, DemuxPacket *pstPkt) {
    if (!pDemux || !pstPkt || !pDemux->pFile)
        return ERR_DEMUX_NULL_PTR;

    TrackInfo *pTrack = &pDemux->stVideoTrack;

    if (pTrack->u32CurrentSample >= pTrack->u32SampleCount) {
        return ERR_DEMUX_NO_STREAM; /* EOF */
    }

    SampleEntry *pSample = &pTrack->pstSamples[pTrack->u32CurrentSample];

    /* Seek and read raw AVCC sample */
    fseek(pDemux->pFile, pSample->u64Offset, SEEK_SET);

    if (pSample->u32Size > sizeof(pDemux->au8ReadBuf)) {
        return ERR_DEMUX_NO_STREAM;
    }

    if (fread(pDemux->au8ReadBuf, 1, pSample->u32Size, pDemux->pFile) != pSample->u32Size) {
        return ERR_DEMUX_NO_STREAM;
    }

    /* Convert AVCC (length-prefixed) to Annex-B (start-code-prefixed).
     * For keyframes, prepend SPS/PPS from avcC box. */
    U8 *pOut = pDemux->au8AnnexBBuf;
    U32 u32OutLen = 0;
    U32 u32MaxOut = sizeof(pDemux->au8AnnexBBuf);
    U8 nls = pTrack->u8NalLenSize ? pTrack->u8NalLenSize : 4;

    /* Inject SPS/PPS before keyframe */
    if (pSample->bSync && pTrack->u32AnnexBPSLen > 0 && pTrack->u32AnnexBPSLen < u32MaxOut) {
        memcpy(pOut, pTrack->au8AnnexBPS, pTrack->u32AnnexBPSLen);
        u32OutLen += pTrack->u32AnnexBPSLen;
    }

    /* Convert AVCC NALs */
    static const U8 startCode[4] = {0x00, 0x00, 0x00, 0x01};
    U32 inPos = 0;
    while (inPos + nls <= pSample->u32Size) {
        U32 nalLen = 0;
        for (U8 b = 0; b < nls; b++) {
            nalLen = (nalLen << 8) | pDemux->au8ReadBuf[inPos + b];
        }
        inPos += nls;
        if (inPos + nalLen > pSample->u32Size)
            break;
        if (u32OutLen + 4 + nalLen > u32MaxOut)
            break;

        memcpy(pOut + u32OutLen, startCode, 4);
        u32OutLen += 4;
        memcpy(pOut + u32OutLen, pDemux->au8ReadBuf + inPos, nalLen);
        u32OutLen += nalLen;
        inPos += nalLen;
    }

    pstPkt->pu8Data = pOut;
    pstPkt->u32Size = u32OutLen;
    pstPkt->bKeyFrame = pSample->bSync;
    pstPkt->eCodecType = pTrack->eCodec;
    pstPkt->u32Width = pTrack->u32Width;
    pstPkt->u32Height = pTrack->u32Height;
    pstPkt->u64PTS = (U64)pTrack->u32CurrentSample * 40000; /* 25fps approx */

    /* Debug: dump first 3 packets */
    if (pTrack->u32CurrentSample < 3) {
        printf("[MP4] Packet %u: size=%u sync=%d hex=", pTrack->u32CurrentSample, u32OutLen, pSample->bSync);
        for (U32 i = 0; i < (u32OutLen < 32 ? u32OutLen : 32); i++) {
            printf("%02x", pOut[i]);
        }
        printf("\n");
    }

    pTrack->u32CurrentSample++;

    return 0;
}

S32 Mp4Demuxer_Seek(Mp4Demuxer *pDemux, S64 s64PtsUs) {
    if (!pDemux)
        return ERR_DEMUX_NULL_PTR;

    /* Find nearest sync sample */
    TrackInfo *pTrack = &pDemux->stVideoTrack;
    U32 u32TargetSample;
    U32 u32SeekSample;

    if (!pTrack->pstSamples || pTrack->u32SampleCount == 0) {
        return ERR_DEMUX_NOT_STARTED;
    }

    if (s64PtsUs <= 0) {
        pTrack->u32CurrentSample = 0;
        return 0;
    }

    /* Current MP4 parser uses 25fps approximate PTS in ReadPacket. Keep seek
     * timestamp mapping consistent with that until full stts/ctts timing is
     * implemented. Always seek backward to the closest sync sample so decoder
     * receives an IDR before dependent frames. */
    u32TargetSample = (U32)(s64PtsUs / 40000);
    if (u32TargetSample >= pTrack->u32SampleCount) {
        u32TargetSample = pTrack->u32SampleCount - 1;
    }

    u32SeekSample = u32TargetSample;
    while (u32SeekSample > 0 && !pTrack->pstSamples[u32SeekSample].bSync) {
        u32SeekSample--;
    }

    pTrack->u32CurrentSample = u32SeekSample;

    return 0;
}

S64 Mp4Demuxer_GetDuration(Mp4Demuxer *pDemux) {
    if (!pDemux)
        return 0;

    if (pDemux->u32Timescale > 0) {
        return (pDemux->u64Duration * 1000000) / pDemux->u32Timescale;
    }

    return 0;
}
