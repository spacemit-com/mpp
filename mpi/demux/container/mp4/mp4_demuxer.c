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
#include <inttypes.h>
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

/* hdlr handler_type FourCC */
#define HDLR_VIDE 0x76696465 /* 'vide' */
#define HDLR_SOUN 0x736F756E /* 'soun' */

#define MAX_SAMPLES 100000

/* Upper bound for an in-memory moov box (256 MiB). Guards against a crafted
 * (but technically in-file-range) size that would exhaust memory, and keeps the
 * value within the 32-bit size used by the parser. */
#define MP4_MAX_MOOV_SIZE (256U * 1024U * 1024U)

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

/* Parse hvcC box to extract NAL length size and VPS/SPS/PPS in Annex-B format.
 *
 * HEVCDecoderConfigurationRecord layout (ISO/IEC 14496-15):
 *   [0]      configurationVersion
 *   [1]      general_profile_space/tier_flag/profile_idc
 *   [2..5]   general_profile_compatibility_flags
 *   [6..11]  general_constraint_indicator_flags
 *   [12]     general_level_idc
 *   [13..14] min_spatial_segmentation_idc
 *   [15]     parallelismType
 *   [16]     chromaFormat
 *   [17]     bitDepthLumaMinus8
 *   [18]     bitDepthChromaMinus8
 *   [19..20] avgFrameRate
 *   [21]     constantFrameRate/numTemporalLayers/temporalIdNested/lengthSizeMinusOne
 *   [22]     numOfArrays
 *   [23..]   arrays: each = NAL_unit_type(1) + numNalus(2) + {nalLen(2) + nal}*
 */
static S32 parse_hvcc(TrackInfo *pTrack, const U8 *pData, U32 u32Size) {
    if (u32Size < 23)
        return -1;

    pTrack->u8NalLenSize = (pData[21] & 0x03) + 1;

    U32 numArrays = pData[22];
    U32 offset = 23;
    U32 outLen = 0;
    static const U8 startCode[4] = {0x00, 0x00, 0x00, 0x01};

    for (U32 a = 0; a < numArrays; a++) {
        if (offset + 3 > u32Size)
            break;
        /* skip array_completeness/reserved/NAL_unit_type byte */
        offset += 1;
        U16 numNalus = read_be16(pData + offset);
        offset += 2;

        for (U32 n = 0; n < numNalus; n++) {
            if (offset + 2 > u32Size)
                return -1;
            U16 nalLen = read_be16(pData + offset);
            offset += 2;
            if (offset + nalLen > u32Size)
                return -1;
            if (outLen + 4 + nalLen > sizeof(pTrack->au8AnnexBPS))
                return -1;
            memcpy(pTrack->au8AnnexBPS + outLen, startCode, 4);
            outLen += 4;
            memcpy(pTrack->au8AnnexBPS + outLen, pData + offset, nalLen);
            outLen += nalLen;
            offset += nalLen;
        }
    }

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

    /* The entry must fit within the stsd payload (which starts 8 bytes in) and
     * be large enough to hold the visual sample entry header we read below
     * (8-byte box header + 78-byte visual sample entry = 86 bytes). Otherwise a
     * bogus size would let pEnd run past the buffer or the width/height reads
     * fall out of bounds. */
    if (u32EntrySize < 86 || u32EntrySize > u32Size - 8)
        return -1;

    if (u32EntryType == BOX_AVC1) {
        pTrack->eCodec = DEMUX_CODEC_H264;
        pTrack->u32Width = read_be16(pEntry + 24 + 8); /* +8 for visual sample entry header */
        pTrack->u32Height = read_be16(pEntry + 26 + 8);

        /* Find avcC box */
        const U8 *p = pEntry + 78 + 8; /* skip visual sample entry header */
        const U8 *pEnd = pEntry + u32EntrySize;
        while (p + 8 <= pEnd) {
            U32 boxSize = read_be32(p);
            U32 boxType = read_be32(p + 4);
            /* A child box must cover its own 8-byte header and stay within the
             * entry; a zero/short size would underflow or loop forever. */
            if (boxSize < 8 || p + boxSize > pEnd)
                break;
            if (boxType == BOX_AVCC) {
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
        while (p + 8 <= pEnd) {
            U32 boxSize = read_be32(p);
            U32 boxType = read_be32(p + 4);
            if (boxSize < 8 || p + boxSize > pEnd)
                break;
            if (boxType == BOX_HVCC) {
                U32 len = boxSize - 8;
                if (len > sizeof(pTrack->au8ExtraData)) {
                    len = sizeof(pTrack->au8ExtraData);
                }
                memcpy(pTrack->au8ExtraData, p + 8, len);
                pTrack->u32ExtraDataLen = len;
                /* Convert hvcC to Annex-B VPS/SPS/PPS for keyframe injection */
                parse_hvcc(pTrack, p + 8, len);
                break;
            }
            p += boxSize;
        }
    }

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

/* Parse stbl (sample table) box.
 * Resolves per-sample file offsets from the stsc (sample-to-chunk),
 * stco/co64 (chunk offset) and stsz (sample size) tables. A chunk may hold
 * multiple samples, so sample offsets are accumulated within each chunk. */
static S32 parse_stbl_box(Mp4Demuxer *pDemux, TrackInfo *pTrack, const U8 *pData, U32 u32Size) {
    U32 offset = 0;
    U64 *pChunkOffsets = NULL;
    U32 u32ChunkCount = 0;
    U32 *pSampleSizes = NULL;
    U32 u32SampleSizeCount = 0;
    U32 *pSyncSamples = NULL;
    U32 u32SyncCount = 0;
    U32 *pStscFirstChunk = NULL;       /* stsc: first_chunk per entry */
    U32 *pStscSamplesPerChunk = NULL;  /* stsc: samples_per_chunk per entry */
    U32 u32StscCount = 0;

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
            /* Chunk offset table (32-bit) */
            u32ChunkCount = read_be32(pBox + 4);
            if (u32ChunkCount > 0 && u32ChunkCount < 100000 && boxDataSize >= 8 + u32ChunkCount * 4) {
                pChunkOffsets = (U64 *)malloc(u32ChunkCount * sizeof(U64));
                if (pChunkOffsets) {
                    for (U32 i = 0; i < u32ChunkCount; i++) {
                        pChunkOffsets[i] = read_be32(pBox + 8 + i * 4);
                    }
                }
            }
        } else if (boxType == BOX_CO64 && boxDataSize >= 8) {
            /* Chunk offset table (64-bit) */
            u32ChunkCount = read_be32(pBox + 4);
            if (u32ChunkCount > 0 && u32ChunkCount < 100000 && boxDataSize >= 8 + (U64)u32ChunkCount * 8) {
                pChunkOffsets = (U64 *)malloc(u32ChunkCount * sizeof(U64));
                if (pChunkOffsets) {
                    for (U32 i = 0; i < u32ChunkCount; i++) {
                        pChunkOffsets[i] = read_be64(pBox + 8 + i * 8);
                    }
                }
            }
        } else if (boxType == BOX_STSC && boxDataSize >= 8) {
            /* Sample-to-chunk table: entries of {first_chunk, samples_per_chunk,
             * sample_description_index}, 12 bytes each. */
            u32StscCount = read_be32(pBox + 4);
            if (u32StscCount > 0 && u32StscCount < MAX_SAMPLES && boxDataSize >= 8 + (U64)u32StscCount * 12) {
                pStscFirstChunk = (U32 *)malloc(u32StscCount * sizeof(U32));
                pStscSamplesPerChunk = (U32 *)malloc(u32StscCount * sizeof(U32));
                if (pStscFirstChunk && pStscSamplesPerChunk) {
                    for (U32 i = 0; i < u32StscCount; i++) {
                        pStscFirstChunk[i] = read_be32(pBox + 8 + i * 12);
                        pStscSamplesPerChunk[i] = read_be32(pBox + 8 + i * 12 + 4);
                    }
                }
            }
        } else if (boxType == BOX_STSZ && boxDataSize >= 12) {
            /* Sample size table */
            U32 uniformSize = read_be32(pBox + 4);
            u32SampleSizeCount = read_be32(pBox + 8);

            if (uniformSize == 0 && u32SampleSizeCount > 0 && u32SampleSizeCount < MAX_SAMPLES) {
                if (boxDataSize >= 12 + u32SampleSizeCount * 4) {
                    pSampleSizes = (U32 *)malloc(u32SampleSizeCount * sizeof(U32));
                    if (pSampleSizes) {
                        for (U32 i = 0; i < u32SampleSizeCount; i++) {
                            pSampleSizes[i] = read_be32(pBox + 12 + i * 4);
                        }
                    } else {
                        u32SampleSizeCount = 0;
                    }
                }
            } else if (uniformSize > 0) {
                /* All samples same size */
                pSampleSizes = (U32 *)malloc(u32SampleSizeCount * sizeof(U32));
                if (pSampleSizes) {
                    for (U32 i = 0; i < u32SampleSizeCount; i++) {
                        pSampleSizes[i] = uniformSize;
                    }
                } else {
                    u32SampleSizeCount = 0;
                }
            }
        } else if (boxType == BOX_STSS && boxDataSize >= 8) {
            /* Sync sample table (keyframes) */
            u32SyncCount = read_be32(pBox + 4);
            if (u32SyncCount > 0 && u32SyncCount < MAX_SAMPLES && boxDataSize >= 8 + u32SyncCount * 4) {
                pSyncSamples = (U32 *)malloc(u32SyncCount * sizeof(U32));
                if (pSyncSamples) {
                    for (U32 i = 0; i < u32SyncCount; i++) {
                        pSyncSamples[i] = read_be32(pBox + 8 + i * 4);
                    }
                } else {
                    u32SyncCount = 0;
                }
            }
        }

        offset += boxSize;
    }

    /* Build sample table using stsc to map samples to chunks.
     * For each chunk we know its file offset (stco/co64) and how many samples
     * it holds (stsc). Sample offset = chunk_offset + sum(prev sample sizes in
     * the same chunk). Samples are stored contiguously within a chunk. */
    if (pChunkOffsets && pSampleSizes && u32ChunkCount > 0 && u32SampleSizeCount > 0) {
        pTrack->pstSamples = (SampleEntry *)calloc(u32SampleSizeCount, sizeof(SampleEntry));
        if (!pTrack->pstSamples)
            goto cleanup;

        U32 sampleIdx = 0;

        if (pStscFirstChunk && pStscSamplesPerChunk && u32StscCount > 0) {
            /* Walk stsc runs. Each run i spans chunks [first_chunk[i],
             * first_chunk[i+1]) with samples_per_chunk[i] samples each. */
            for (U32 run = 0; run < u32StscCount && sampleIdx < u32SampleSizeCount; run++) {
                U32 firstChunk = pStscFirstChunk[run];           /* 1-based */
                U32 nextFirst = (run + 1 < u32StscCount) ? pStscFirstChunk[run + 1] : u32ChunkCount + 1;
                U32 samplesPerChunk = pStscSamplesPerChunk[run];

                if (firstChunk < 1)
                    firstChunk = 1;

                for (U32 chunk = firstChunk; chunk < nextFirst && chunk <= u32ChunkCount; chunk++) {
                    U64 chunkOffset = pChunkOffsets[chunk - 1];
                    U64 sampleOffset = chunkOffset;

                    for (U32 s = 0; s < samplesPerChunk && sampleIdx < u32SampleSizeCount; s++) {
                        pTrack->pstSamples[sampleIdx].u64Offset = sampleOffset;
                        pTrack->pstSamples[sampleIdx].u32Size = pSampleSizes[sampleIdx];
                        pTrack->pstSamples[sampleIdx].bSync = (pSyncSamples == NULL) ? MPP_TRUE : MPP_FALSE;
                        sampleOffset += pSampleSizes[sampleIdx];
                        sampleIdx++;
                    }
                }
            }
        } else {
            /* Fallback: no stsc -> assume 1 sample per chunk (legacy behaviour). */
            U32 n = (u32ChunkCount < u32SampleSizeCount) ? u32ChunkCount : u32SampleSizeCount;
            for (U32 i = 0; i < n; i++) {
                pTrack->pstSamples[i].u64Offset = pChunkOffsets[i];
                pTrack->pstSamples[i].u32Size = pSampleSizes[i];
                pTrack->pstSamples[i].bSync = (pSyncSamples == NULL) ? MPP_TRUE : MPP_FALSE;
            }
            sampleIdx = n;
        }

        pTrack->u32SampleCount = sampleIdx;

        /* Mark sync samples (1-based index in stss) */
        if (pSyncSamples) {
            for (U32 i = 0; i < u32SyncCount; i++) {
                U32 idx = pSyncSamples[i] - 1;
                if (idx < pTrack->u32SampleCount) {
                    pTrack->pstSamples[idx].bSync = MPP_TRUE;
                }
            }
        }
    }

cleanup:
    if (pChunkOffsets)
        free(pChunkOffsets);
    if (pSampleSizes)
        free(pSampleSizes);
    if (pSyncSamples)
        free(pSyncSamples);
    if (pStscFirstChunk)
        free(pStscFirstChunk);
    if (pStscSamplesPerChunk)
        free(pStscSamplesPerChunk);

    return 0;
}

/* Find the handler_type inside mdia/hdlr. Returns 0 if not found.
 * hdlr layout: version(1)+flags(3), pre_defined(4), handler_type(4), ... */
static U32 find_handler_type(const U8 *pMdia, U32 u32MdiaSize) {
    U32 off = 0;
    while (off + 8 <= u32MdiaSize) {
        U32 boxSize = read_be32(pMdia + off);
        U32 boxType = read_be32(pMdia + off + 4);
        if (boxSize < 8 || off + boxSize > u32MdiaSize)
            break;
        if (boxType == BOX_HDLR && boxSize >= 8 + 12) {
            return read_be32(pMdia + off + 8 + 8);
        }
        off += boxSize;
    }
    return 0;
}

/* Parse trak (track) box. Returns 1 if this track is a video track that was
 * parsed into pTrack, 0 otherwise (audio/other handler). */
static S32 parse_trak_box(Mp4Demuxer *pDemux, TrackInfo *pTrack, const U8 *pData, U32 u32Size) {
    U32 offset = 0;
    S32 bIsVideo = 0;

    while (offset + 8 <= u32Size) {
        U32 boxSize = read_be32(pData + offset);
        U32 boxType = read_be32(pData + offset + 4);

        if (boxSize < 8 || offset + boxSize > u32Size)
            break;

        const U8 *pBox = pData + offset + 8;
        U32 boxDataSize = boxSize - 8;

        if (boxType == BOX_MDIA) {
            /* Only parse the sample table for video tracks; skip audio/other. */
            U32 handlerType = find_handler_type(pBox, boxDataSize);
            if (handlerType != HDLR_VIDE)
                break;
            bIsVideo = 1;

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

    return bIsVideo;
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
            /* Parse track into the video slot; only commit if hdlr == 'vide'.
             * This avoids mis-tagging an audio-first track as video. */
            if (!pDemux->bHasVideo) {
                TrackInfo stTmp;
                memset(&stTmp, 0, sizeof(stTmp));
                stTmp.eCodec = DEMUX_CODEC_H264;
                stTmp.u32Width = 640;
                stTmp.u32Height = 480;

                if (parse_trak_box(pDemux, &stTmp, pBox, boxDataSize) == 1) {
                    pDemux->stVideoTrack = stTmp;
                    pDemux->bHasVideo = MPP_TRUE;
                } else if (stTmp.pstSamples) {
                    /* Non-video track allocated samples? free defensively. */
                    free(stTmp.pstSamples);
                }
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

    fprintf(stderr, "[MP4] File size: %" PRId64 " bytes\n", (int64_t)pDemux->s64FileSize);

    /* Parse top-level boxes */
    U8 au8Hdr[16];
    while (fread(au8Hdr, 1, 8, pDemux->pFile) == 8) {
        U64 boxSize = read_be32(au8Hdr);
        U32 boxType = read_be32(au8Hdr + 4);
        U32 u32HdrLen = 8;

        if (boxSize == 1) {
            if (fread(au8Hdr + 8, 1, 8, pDemux->pFile) != 8)
                break;
            boxSize = read_be64(au8Hdr + 8);
            u32HdrLen = 16;
        }

        char typeStr[5];
        typeStr[0] = (boxType >> 24) & 0xFF;
        typeStr[1] = (boxType >> 16) & 0xFF;
        typeStr[2] = (boxType >> 8) & 0xFF;
        typeStr[3] = boxType & 0xFF;
        typeStr[4] = '\0';
        fprintf(stderr, "[MP4] Found box: %s size=%" PRIu64 "\n", typeStr, (uint64_t)boxSize);

        /* Validate box size: must at least cover its own header and must not
         * exceed the remaining bytes in the file. A bogus size would otherwise
         * lead to an oversized malloc or an out-of-range fseek. */
        S64 s64Pos = ftell(pDemux->pFile);
        if (s64Pos < 0)
            break;
        if (boxSize < u32HdrLen || (S64)(boxSize - u32HdrLen) > pDemux->s64FileSize - s64Pos) {
            fprintf(stderr, "[MP4] Invalid box size %" PRIu64 ", aborting\n", (uint64_t)boxSize);
            break;
        }

        U64 u64BodySize = boxSize - u32HdrLen;

        if (boxType == BOX_MOOV) {
            /* Read and parse moov box. Cap the body size to a sane limit so a
             * crafted (but in-range) huge moov cannot exhaust memory, and so the
             * value fits the 32-bit size used by the parser. */
            if (u64BodySize == 0 || u64BodySize > MP4_MAX_MOOV_SIZE) {
                fprintf(stderr, "[MP4] moov size %" PRIu64 " out of range\n", (uint64_t)u64BodySize);
                break;
            }
            U32 moovSize = (U32)u64BodySize;
            U8 *pMoov = (U8 *)malloc(moovSize);
            if (pMoov && fread(pMoov, 1, moovSize, pDemux->pFile) == moovSize) {
                parse_moov_box(pDemux, pMoov, moovSize);
            }
            if (pMoov)
                free(pMoov);
            break;
        }

        /* Skip to next box */
        fseeko(pDemux->pFile, (off_t)u64BodySize, SEEK_CUR);
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
    fseeko(pDemux->pFile, (off_t)pSample->u64Offset, SEEK_SET);

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
