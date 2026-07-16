/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_mp4.c
 * @Date      :    2026-06-11
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Fragmented MP4 (fMP4) writer for MUX file recording.
 *
 * Layout:
 *   [ftyp][moov(+mvex)]            <- init segment, written once on start
 *   [moof][mdat] [moof][mdat] ...  <- media fragments, each self-contained
 *
 * Each fragment is flushed (and optionally fsync'd) right after it is
 * written, so a power loss only loses the in-progress fragment; every
 * fragment already on disk remains playable. This avoids the classic
 * "moov at end of file" corruption of plain MP4 recordings.
 *------------------------------------------------------------------------------
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec/h264_utils.h"
#include "codec/h265_utils.h"
#include "mux_common.h"
#include "mux_writer.h"

#define MP4_TIMESCALE 90000             /* 90kHz, classic video timescale */
#define MP4_TRACK_ID 1
#define MP4_MAX_SAMPLES_PER_FRAG 512    /* cap samples buffered per moof */
#define MP4_FRAG_BUF_CAP (4 * 1024 * 1024)
#define MP4_MAX_DUR_FACTOR 16           /* clamp PTS-gap sample duration */

/* A single sample (access unit) pending in the current fragment. */
typedef struct _Mp4Sample {
    U32 u32Offset; /* offset within frag mdat payload buffer */
    U32 u32Size;   /* length-prefixed sample size */
    U32 u32Duration;
    BOOL bKeyFrame;
} Mp4Sample;

typedef struct _Mp4Priv {
    BOOL bInitDone;
    BOOL bSpsParsed;
    /* parameter sets for avcC/hvcC */
    MuxParamSets stSets;
    /* fragment accumulation */
    U8 *pu8FragBuf;      /* mdat payload (length-prefixed NALs) */
    U32 u32FragLen;
    U32 u32FragCap;
    Mp4Sample astSamples[MP4_MAX_SAMPLES_PER_FRAG];
    U32 u32SampleCount;
    U32 u32SeqNumber;    /* moof sequence number, 1-based */
    U64 u64BaseDecodeTime; /* running decode time in MP4_TIMESCALE units */
    U64 u64FirstPtsUs;
    U64 u64LastPtsUs;
    U32 u32DefaultDurTicks;
    /* Per-channel scratch buffers for box serialization. Kept in the private
     * context (not static locals) so concurrent channels never share storage. */
    U8 au8MoofBuf[64 * 1024];
    U8 au8InitBuf[16 * 1024];
} Mp4Priv;

/* ---- forward decls ---- */
static S32 mp4_start(MuxWriter *pWr);
static S32 mp4_write(MuxWriter *pWr, const U8 *pu8Data, U32 u32Size, BOOL bKeyFrame, U64 u64PtsUs);
static S32 mp4_finish(MuxWriter *pWr);
static VOID mp4_destroy(MuxWriter *pWr);

static const MuxWriterOps g_stMp4Ops = {
    "fmp4", mp4_start, mp4_write, mp4_finish, mp4_destroy,
};

S32 MuxMp4_Attach(MuxWriter *pWr) {
    Mp4Priv *pPriv;

    if (!pWr) {
        return -1;
    }

    pPriv = (Mp4Priv *)calloc(1, sizeof(Mp4Priv));
    if (!pPriv) {
        return -1;
    }

    pPriv->pu8FragBuf = (U8 *)malloc(MP4_FRAG_BUF_CAP);
    if (!pPriv->pu8FragBuf) {
        free(pPriv);
        return -1;
    }
    pPriv->u32FragCap = MP4_FRAG_BUF_CAP;
    pPriv->u32SeqNumber = 1;
    pPriv->u32DefaultDurTicks = MP4_TIMESCALE / (pWr->u32Fps > 0 ? pWr->u32Fps : 25);

    pWr->pstOps = &g_stMp4Ops;
    pWr->pPriv = pPriv;
    return 0;
}

static VOID mp4_destroy(MuxWriter *pWr) {
    Mp4Priv *pPriv = (Mp4Priv *)pWr->pPriv;
    if (pPriv && pPriv->pu8FragBuf) {
        free(pPriv->pu8FragBuf);
        pPriv->pu8FragBuf = NULL;
    }
    /* pPriv itself is freed by MuxWriter_Destroy. */
}

/* ---------------------------------------------------------------------------
 * Box builder: writes into a caller buffer and back-patches box sizes.
 * ------------------------------------------------------------------------ */
typedef struct _BoxBuilder {
    U8 *pu8Buf;
    U32 u32Cap;
    U32 u32Pos;
    BOOL bOverflow;
} BoxBuilder;

static VOID box_init(BoxBuilder *pBld, U8 *pu8Buf, U32 u32Cap) {
    pBld->pu8Buf = pu8Buf;
    pBld->u32Cap = u32Cap;
    pBld->u32Pos = 0;
    pBld->bOverflow = MPP_FALSE;
}

static VOID box_bytes(BoxBuilder *pBld, const U8 *pu8Data, U32 u32Len) {
    if (pBld->u32Pos + u32Len > pBld->u32Cap) {
        pBld->bOverflow = MPP_TRUE;
        return;
    }
    memcpy(pBld->pu8Buf + pBld->u32Pos, pu8Data, u32Len);
    pBld->u32Pos += u32Len;
}

static VOID box_u8(BoxBuilder *pBld, U8 v) {
    if (pBld->u32Pos + 1 > pBld->u32Cap) {
        pBld->bOverflow = MPP_TRUE;
        return;
    }
    pBld->pu8Buf[pBld->u32Pos] = v;
    pBld->u32Pos += 1;
}

static VOID box_be16(BoxBuilder *pBld, U16 v) {
    U8 tmp[2];
    MuxPutBe16(tmp, v);
    box_bytes(pBld, tmp, 2);
}

static VOID box_be32(BoxBuilder *pBld, U32 v) {
    U8 tmp[4];
    MuxPutBe32(tmp, v);
    box_bytes(pBld, tmp, 4);
}

/* Open a box header, return position of size field for later patching. */
static U32 box_open(BoxBuilder *pBld, U32 u32Type) {
    U32 u32SizePos = pBld->u32Pos;
    box_be32(pBld, 0); /* size placeholder */
    box_be32(pBld, u32Type);
    return u32SizePos;
}

/* Patch the size field of a box opened at u32SizePos. */
static VOID box_close(BoxBuilder *pBld, U32 u32SizePos) {
    U32 u32Size = pBld->u32Pos - u32SizePos;
    if (u32SizePos + 4 <= pBld->u32Cap) {
        MuxPutBe32(pBld->pu8Buf + u32SizePos, u32Size);
    }
}

/* ---- codec configuration boxes ---- */

/* avcC: AVCDecoderConfigurationRecord wrapping SPS/PPS. */
static VOID box_write_avcc(BoxBuilder *pBld, const MuxParamSets *pSets) {
    U32 u32Pos = box_open(pBld, MUX_FOURCC('a', 'v', 'c', 'C'));
    box_u8(pBld, 1);                          /* configurationVersion */
    box_u8(pBld, pSets->u32SpsLen > 1 ? pSets->au8Sps[1] : 0x42); /* AVCProfileIndication */
    box_u8(pBld, pSets->u32SpsLen > 2 ? pSets->au8Sps[2] : 0x00); /* profile_compatibility */
    box_u8(pBld, pSets->u32SpsLen > 3 ? pSets->au8Sps[3] : 0x1E); /* AVCLevelIndication */
    box_u8(pBld, 0xFF);                        /* 6 bits reserved + lengthSizeMinusOne=3 */
    box_u8(pBld, 0xE1);                        /* 3 bits reserved + numOfSPS=1 */
    box_be16(pBld, (U16)pSets->u32SpsLen);
    box_bytes(pBld, pSets->au8Sps, pSets->u32SpsLen);
    box_u8(pBld, 1);                           /* numOfPPS */
    box_be16(pBld, (U16)pSets->u32PpsLen);
    box_bytes(pBld, pSets->au8Pps, pSets->u32PpsLen);
    box_close(pBld, u32Pos);
}

/* hvcC: HEVCDecoderConfigurationRecord wrapping VPS/SPS/PPS. */
static VOID box_write_hvcc(BoxBuilder *pBld, const MuxParamSets *pSets) {
    U8 au8Ptl[MUX_HEVC_PTL_LEN];
    BOOL bPtl = (BOOL)(MuxHevcExtractPtl(pSets->au8Sps, pSets->u32SpsLen, au8Ptl) == 0);
    U32 u32Pos = box_open(pBld, MUX_FOURCC('h', 'v', 'c', 'C'));
    box_u8(pBld, 1);    /* configurationVersion */
    if (bPtl) {
        /* The hvcC bytes general_profile_space..general_level_idc are exactly
         * the 12-byte general profile_tier_level block from the SPS, in the
         * same order, so copy it verbatim to advertise the stream's real
         * profile/tier/level and constraint flags. */
        box_bytes(pBld, au8Ptl, MUX_HEVC_PTL_LEN);
    } else {
        /* Fallback PTL block — must be exactly MUX_HEVC_PTL_LEN (12) bytes
         * to match the normal path:  1 + 4 + 6 + 1 = 12.
         *   general_profile_space/tier_flag/profile_idc :  1 byte
         *   general_profile_compatibility_flags         :  4 bytes
         *   general_constraint_indicator_flags (48-bit) :  6 bytes
         *   general_level_idc                           :  1 byte */
        box_u8(pBld, 0x01);          /* profile_space=0,tier=0,profile_idc=1 */
        box_be32(pBld, 0x60000000);  /* general_profile_compatibility_flags */
        box_be32(pBld, 0x00000000);  /* constraint_indicator[0..3] */
        box_be16(pBld, 0x0000);      /* constraint_indicator[4..5] */
        box_u8(pBld, 0x5A);          /* general_level_idc = 90 (level 3.0) */
    }
    box_be16(pBld, 0xF000);     /* min_spatial_segmentation_idc */
    box_u8(pBld, 0xFC);         /* parallelismType */
    box_u8(pBld, 0xFD);         /* chromaFormat = 4:2:0 */
    box_u8(pBld, 0xF8);         /* bitDepthLumaMinus8 */
    box_u8(pBld, 0xF8);         /* bitDepthChromaMinus8 */
    box_be16(pBld, 0x0000);     /* avgFrameRate */
    box_u8(pBld, 0x0F);         /* constantFrameRate/numTemporalLayers/lengthSizeMinusOne=3 */

    /* numOfArrays: VPS + SPS + PPS = 3 when VPS is available.
     * When VPS is absent (u32VpsLen == 0), write only SPS + PPS (2 arrays)
     * to produce a valid hvcC box that players can decode without choking
     * on an empty VPS NAL unit array. */
    if (pSets->u32VpsLen > 0) {
        box_u8(pBld, 3);            /* numOfArrays: VPS, SPS, PPS */
        /* VPS array */
        box_u8(pBld, 0xA0);         /* array_completeness=1 + NAL_unit_type=32 (VPS) */
        box_be16(pBld, 1);
        box_be16(pBld, (U16)pSets->u32VpsLen);
        box_bytes(pBld, pSets->au8Vps, pSets->u32VpsLen);
    } else {
        box_u8(pBld, 2);            /* numOfArrays: SPS, PPS (no VPS) */
    }
    /* SPS array */
    box_u8(pBld, 0xA1);         /* array_completeness=1 + NAL_unit_type=33 (SPS) */
    box_be16(pBld, 1);
    box_be16(pBld, (U16)pSets->u32SpsLen);
    box_bytes(pBld, pSets->au8Sps, pSets->u32SpsLen);
    /* PPS array */
    box_u8(pBld, 0xA2);         /* array_completeness=1 + NAL_unit_type=34 (PPS) */
    box_be16(pBld, 1);
    box_be16(pBld, (U16)pSets->u32PpsLen);
    box_bytes(pBld, pSets->au8Pps, pSets->u32PpsLen);
    box_close(pBld, u32Pos);
}

/* Sample entry box: avc1 or hvc1 wrapping visual sample entry + config. */
static VOID box_write_sample_entry(BoxBuilder *pBld, MuxWriter *pWr) {
    Mp4Priv *pPriv = (Mp4Priv *)pWr->pPriv;
    U32 u32Type = (pWr->eCodecType == MUX_CODEC_H265) ? MUX_FOURCC('h', 'v', 'c', '1')
        : MUX_FOURCC('a', 'v', 'c', '1');
    U32 u32Pos = box_open(pBld, u32Type);
    U32 i;

    /* SampleEntry: 6 reserved bytes + data_reference_index */
    for (i = 0; i < 6; ++i) {
        box_u8(pBld, 0);
    }
    box_be16(pBld, 1); /* data_reference_index */
    /* VisualSampleEntry */
    box_be16(pBld, 0); /* pre_defined */
    box_be16(pBld, 0); /* reserved */
    box_be32(pBld, 0); /* pre_defined */
    box_be32(pBld, 0);
    box_be32(pBld, 0);
    box_be16(pBld, (U16)pWr->u32Width);
    box_be16(pBld, (U16)pWr->u32Height);
    box_be32(pBld, 0x00480000); /* horizresolution 72dpi */
    box_be32(pBld, 0x00480000); /* vertresolution 72dpi */
    box_be32(pBld, 0);          /* reserved */
    box_be16(pBld, 1);          /* frame_count */
    for (i = 0; i < 32; ++i) {
        box_u8(pBld, 0); /* compressorname */
    }
    box_be16(pBld, 0x0018); /* depth */
    box_be16(pBld, 0xFFFF); /* pre_defined */

    if (pWr->eCodecType == MUX_CODEC_H265) {
        box_write_hvcc(pBld, &pPriv->stSets);
    } else {
        /* avcC requires at least 4 SPS bytes (NAL header + profile/compat/
         * level) to produce a spec-compliant DecoderConfigurationRecord.
         * If the SPS has not been captured yet (e.g. stream started without
         * IDR), skip writing avcC — the file will lack decoder config but
         * avoids writing an invalid box with zeroed profile/level fields. */
        if (pPriv->stSets.u32SpsLen >= 4) {
            box_write_avcc(pBld, &pPriv->stSets);
        }
    }
    box_close(pBld, u32Pos);
}

/* ---- moov init segment ---- */

static VOID box_write_ftyp(BoxBuilder *pBld, MuxCodecType eCodecType) {
    U32 u32Pos = box_open(pBld, MUX_FOURCC('f', 't', 'y', 'p'));
    box_be32(pBld, MUX_FOURCC('i', 's', 'o', 'm')); /* major_brand */
    box_be32(pBld, 0x00000200);                     /* minor_version */
    box_be32(pBld, MUX_FOURCC('i', 's', 'o', 'm'));
    box_be32(pBld, MUX_FOURCC('i', 's', 'o', '6'));
    box_be32(pBld, MUX_FOURCC('m', 'p', '4', '1'));
    /* Codec brand: hvc1 for H265, avc1 for H264. Players use the compatible
     * brand list to recognise the contained elementary stream. */
    if (eCodecType == MUX_CODEC_H265) {
        box_be32(pBld, MUX_FOURCC('h', 'v', 'c', '1'));
    } else {
        box_be32(pBld, MUX_FOURCC('a', 'v', 'c', '1'));
    }
    box_close(pBld, u32Pos);
}

static VOID box_write_mvhd(BoxBuilder *pBld) {
    U32 u32Pos = box_open(pBld, MUX_FOURCC('m', 'v', 'h', 'd'));
    U32 i;
    box_be32(pBld, 0);             /* version + flags */
    box_be32(pBld, 0);             /* creation_time */
    box_be32(pBld, 0);             /* modification_time */
    box_be32(pBld, MP4_TIMESCALE); /* timescale */
    box_be32(pBld, 0);             /* duration (unknown for fragmented) */
    box_be32(pBld, 0x00010000);    /* rate 1.0 */
    box_be16(pBld, 0x0100);        /* volume 1.0 */
    box_be16(pBld, 0);             /* reserved */
    box_be32(pBld, 0);             /* reserved */
    box_be32(pBld, 0);             /* reserved */
    /* unity matrix */
    box_be32(pBld, 0x00010000);
    box_be32(pBld, 0);
    box_be32(pBld, 0);
    box_be32(pBld, 0);
    box_be32(pBld, 0x00010000);
    box_be32(pBld, 0);
    box_be32(pBld, 0);
    box_be32(pBld, 0);
    box_be32(pBld, 0x40000000);
    for (i = 0; i < 6; ++i) {
        box_be32(pBld, 0); /* pre_defined */
    }
    box_be32(pBld, MP4_TRACK_ID + 1); /* next_track_ID */
    box_close(pBld, u32Pos);
}

static VOID box_write_tkhd(BoxBuilder *pBld, MuxWriter *pWr) {
    U32 u32Pos = box_open(pBld, MUX_FOURCC('t', 'k', 'h', 'd'));
    box_be32(pBld, 0x00000007); /* version=0, flags=enabled|in_movie|in_preview */
    box_be32(pBld, 0);          /* creation_time */
    box_be32(pBld, 0);          /* modification_time */
    box_be32(pBld, MP4_TRACK_ID);
    box_be32(pBld, 0);          /* reserved */
    box_be32(pBld, 0);          /* duration */
    box_be32(pBld, 0);          /* reserved */
    box_be32(pBld, 0);          /* reserved */
    box_be16(pBld, 0);          /* layer */
    box_be16(pBld, 0);          /* alternate_group */
    box_be16(pBld, 0);          /* volume */
    box_be16(pBld, 0);          /* reserved */
    /* unity matrix */
    box_be32(pBld, 0x00010000);
    box_be32(pBld, 0);
    box_be32(pBld, 0);
    box_be32(pBld, 0);
    box_be32(pBld, 0x00010000);
    box_be32(pBld, 0);
    box_be32(pBld, 0);
    box_be32(pBld, 0);
    box_be32(pBld, 0x40000000);
    box_be32(pBld, pWr->u32Width << 16);  /* width 16.16 */
    box_be32(pBld, pWr->u32Height << 16); /* height 16.16 */
    box_close(pBld, u32Pos);
}

static VOID box_write_mdhd(BoxBuilder *pBld) {
    U32 u32Pos = box_open(pBld, MUX_FOURCC('m', 'd', 'h', 'd'));
    box_be32(pBld, 0);             /* version + flags */
    box_be32(pBld, 0);             /* creation_time */
    box_be32(pBld, 0);             /* modification_time */
    box_be32(pBld, MP4_TIMESCALE); /* timescale */
    box_be32(pBld, 0);             /* duration */
    box_be16(pBld, 0x55C4);        /* language 'und' */
    box_be16(pBld, 0);             /* pre_defined */
    box_close(pBld, u32Pos);
}

static VOID box_write_hdlr(BoxBuilder *pBld) {
    const CHAR *pszName = "VideoHandler";
    U32 u32Pos = box_open(pBld, MUX_FOURCC('h', 'd', 'l', 'r'));
    box_be32(pBld, 0); /* version + flags */
    box_be32(pBld, 0); /* pre_defined */
    box_be32(pBld, MUX_FOURCC('v', 'i', 'd', 'e'));
    box_be32(pBld, 0);
    box_be32(pBld, 0);
    box_be32(pBld, 0);
    box_bytes(pBld, (const U8 *)pszName, (U32)strlen(pszName) + 1);
    box_close(pBld, u32Pos);
}

/* Sample table boxes are all empty for fragmented MP4. */
static VOID box_write_stbl(BoxBuilder *pBld, MuxWriter *pWr) {
    U32 u32Stbl = box_open(pBld, MUX_FOURCC('s', 't', 'b', 'l'));
    U32 u32Stsd = box_open(pBld, MUX_FOURCC('s', 't', 's', 'd'));
    box_be32(pBld, 0); /* version + flags */
    box_be32(pBld, 1); /* entry_count */
    box_write_sample_entry(pBld, pWr);
    box_close(pBld, u32Stsd);
    /* stts */
    {
        U32 u32P = box_open(pBld, MUX_FOURCC('s', 't', 't', 's'));
        box_be32(pBld, 0);
        box_be32(pBld, 0); /* entry_count */
        box_close(pBld, u32P);
    }
    /* stsc */
    {
        U32 u32P = box_open(pBld, MUX_FOURCC('s', 't', 's', 'c'));
        box_be32(pBld, 0);
        box_be32(pBld, 0);
        box_close(pBld, u32P);
    }
    /* stsz */
    {
        U32 u32P = box_open(pBld, MUX_FOURCC('s', 't', 's', 'z'));
        box_be32(pBld, 0);
        box_be32(pBld, 0); /* sample_size */
        box_be32(pBld, 0); /* sample_count */
        box_close(pBld, u32P);
    }
    /* stco */
    {
        U32 u32P = box_open(pBld, MUX_FOURCC('s', 't', 'c', 'o'));
        box_be32(pBld, 0);
        box_be32(pBld, 0);
        box_close(pBld, u32P);
    }
    box_close(pBld, u32Stbl);
}

static VOID box_write_minf(BoxBuilder *pBld, MuxWriter *pWr) {
    U32 u32Minf = box_open(pBld, MUX_FOURCC('m', 'i', 'n', 'f'));
    /* vmhd */
    {
        U32 u32P = box_open(pBld, MUX_FOURCC('v', 'm', 'h', 'd'));
        box_be32(pBld, 1); /* version + flags=1 */
        box_be32(pBld, 0); /* graphicsmode + opcolor */
        box_be32(pBld, 0);
        box_close(pBld, u32P);
    }
    /* dinf/dref */
    {
        U32 u32Dinf = box_open(pBld, MUX_FOURCC('d', 'i', 'n', 'f'));
        U32 u32Dref = box_open(pBld, MUX_FOURCC('d', 'r', 'e', 'f'));
        U32 u32Url;
        box_be32(pBld, 0); /* version + flags */
        box_be32(pBld, 1); /* entry_count */
        u32Url = box_open(pBld, MUX_FOURCC('u', 'r', 'l', ' '));
        box_be32(pBld, 1); /* flags=self-contained */
        box_close(pBld, u32Url);
        box_close(pBld, u32Dref);
        box_close(pBld, u32Dinf);
    }
    box_write_stbl(pBld, pWr);
    box_close(pBld, u32Minf);
}

static VOID box_write_trak(BoxBuilder *pBld, MuxWriter *pWr) {
    U32 u32Trak = box_open(pBld, MUX_FOURCC('t', 'r', 'a', 'k'));
    U32 u32Mdia;
    box_write_tkhd(pBld, pWr);
    u32Mdia = box_open(pBld, MUX_FOURCC('m', 'd', 'i', 'a'));
    box_write_mdhd(pBld);
    box_write_hdlr(pBld);
    box_write_minf(pBld, pWr);
    box_close(pBld, u32Mdia);
    box_close(pBld, u32Trak);
}

/* mvex declares the file as fragmented (trex defaults per track). */
static VOID box_write_mvex(BoxBuilder *pBld) {
    U32 u32Mvex = box_open(pBld, MUX_FOURCC('m', 'v', 'e', 'x'));
    U32 u32Trex = box_open(pBld, MUX_FOURCC('t', 'r', 'e', 'x'));
    box_be32(pBld, 0); /* version + flags */
    box_be32(pBld, MP4_TRACK_ID);
    box_be32(pBld, 1); /* default_sample_description_index */
    box_be32(pBld, 0); /* default_sample_duration */
    box_be32(pBld, 0); /* default_sample_size */
    box_be32(pBld, 0); /* default_sample_flags */
    box_close(pBld, u32Trex);
    box_close(pBld, u32Mvex);
}

static U32 box_build_init_segment(MuxWriter *pWr, U8 *pu8Buf, U32 u32Cap) {
    BoxBuilder stBld;
    U32 u32Moov;
    box_init(&stBld, pu8Buf, u32Cap);
    box_write_ftyp(&stBld, pWr->eCodecType);
    u32Moov = box_open(&stBld, MUX_FOURCC('m', 'o', 'o', 'v'));
    box_write_mvhd(&stBld);
    box_write_trak(&stBld, pWr);
    box_write_mvex(&stBld);
    box_close(&stBld, u32Moov);
    if (stBld.bOverflow) {
        return 0;
    }
    return stBld.u32Pos;
}

/* ---------------------------------------------------------------------------
 * Fragment (moof + mdat) writing.
 *
 * trun data_offset must point from the start of the moof box to the first
 * byte of mdat payload. We build moof into a scratch buffer first so we know
 * its size, patch data_offset, then write moof followed by mdat.
 * ------------------------------------------------------------------------ */
static U32 box_build_moof(MuxWriter *pWr, U8 *pu8Buf, U32 u32Cap, U32 *pu32TrunDataOffPos) {
    Mp4Priv *pPriv = (Mp4Priv *)pWr->pPriv;
    BoxBuilder stBld;
    U32 u32Moof;
    U32 u32Traf;
    U32 u32Trun;
    U32 u32TrunFlags;
    U32 i;

    box_init(&stBld, pu8Buf, u32Cap);
    u32Moof = box_open(&stBld, MUX_FOURCC('m', 'o', 'o', 'f'));

    /* mfhd */
    {
        U32 u32P = box_open(&stBld, MUX_FOURCC('m', 'f', 'h', 'd'));
        box_be32(&stBld, 0);
        box_be32(&stBld, pPriv->u32SeqNumber);
        box_close(&stBld, u32P);
    }

    u32Traf = box_open(&stBld, MUX_FOURCC('t', 'r', 'a', 'f'));
    /* tfhd: default-base-is-moof(0x020000) + default_sample_duration present(0x08) */
    {
        U32 u32P = box_open(&stBld, MUX_FOURCC('t', 'f', 'h', 'd'));
        box_be32(&stBld, 0x00020008);
        box_be32(&stBld, MP4_TRACK_ID);
        box_be32(&stBld, pPriv->u32DefaultDurTicks);
        box_close(&stBld, u32P);
    }
    /* tfdt: base media decode time (version 1, 64-bit) */
    {
        U32 u32P = box_open(&stBld, MUX_FOURCC('t', 'f', 'd', 't'));
        U8 tmp[8];
        box_be32(&stBld, 0x01000000); /* version 1 */
        MuxPutBe64(tmp, pPriv->u64BaseDecodeTime);
        box_bytes(&stBld, tmp, 8);
        box_close(&stBld, u32P);
    }
    /* trun flags are a 24-bit field (the high byte is the box version, kept 0).
     * Set only the defined presence bits: data-offset(0x000001),
     * sample-duration(0x000100), sample-size(0x000200) and sample-flags
     * (0x000400). Together: 0x00000701. */
    u32TrunFlags = 0x00000701;
    u32Trun = box_open(&stBld, MUX_FOURCC('t', 'r', 'u', 'n'));
    box_be32(&stBld, u32TrunFlags);
    box_be32(&stBld, pPriv->u32SampleCount);
    *pu32TrunDataOffPos = stBld.u32Pos; /* remember position to patch later */
    box_be32(&stBld, 0);                /* data_offset placeholder */
    for (i = 0; i < pPriv->u32SampleCount; ++i) {
        Mp4Sample *pS = &pPriv->astSamples[i];
        box_be32(&stBld, pS->u32Duration);
        box_be32(&stBld, pS->u32Size);
        /* sample_flags: key frame => sample_depends_on=2 & not non-sync;
         * non-key => sample_is_non_sync_sample(0x10000) + depends_on=1 */
        if (pS->bKeyFrame) {
            box_be32(&stBld, 0x02000000);
        } else {
            box_be32(&stBld, 0x01010000);
        }
    }
    box_close(&stBld, u32Trun);
    box_close(&stBld, u32Traf);
    box_close(&stBld, u32Moof);

    if (stBld.bOverflow) {
        return 0;
    }
    return stBld.u32Pos;
}

/* Flush the accumulated samples as one moof+mdat fragment to disk. */
static S32 mp4_flush_fragment(MuxWriter *pWr) {
    Mp4Priv *pPriv = (Mp4Priv *)pWr->pPriv;
    U8 *au8Moof = pPriv->au8MoofBuf;
    U32 u32MoofLen;
    U32 u32TrunDataOffPos = 0;
    U32 u32DataOffset;
    U8 au8MdatHdr[8];
    U64 u64FragDurTicks = 0;
    U32 i;

    if (pPriv->u32SampleCount == 0 || pPriv->u32FragLen == 0) {
        return ERR_MUX_OK;
    }

    u32MoofLen = box_build_moof(pWr, au8Moof, sizeof(pPriv->au8MoofBuf), &u32TrunDataOffPos);
    if (u32MoofLen == 0) {
        MUX_LOGE("mp4 moof build overflow (%u samples)", pPriv->u32SampleCount);
        return ERR_MUX_WRITE_FAIL;
    }

    /* data_offset = moof size + mdat header(8) to reach first sample byte. */
    u32DataOffset = u32MoofLen + 8;
    MuxPutBe32(au8Moof + u32TrunDataOffPos, u32DataOffset);

    /* mdat header */
    MuxPutBe32(au8MdatHdr, pPriv->u32FragLen + 8);
    MuxPutBe32(au8MdatHdr + 4, MUX_FOURCC('m', 'd', 'a', 't'));

    if (fwrite(au8Moof, 1, u32MoofLen, pWr->pFile) != u32MoofLen ||
        fwrite(au8MdatHdr, 1, 8, pWr->pFile) != 8 ||
        fwrite(pPriv->pu8FragBuf, 1, pPriv->u32FragLen, pWr->pFile) != pPriv->u32FragLen) {
        MUX_LOGE("mp4 fragment write failed");
        return ERR_MUX_WRITE_FAIL;
    }

    /* Advance running decode time and reset the fragment accumulator. */
    for (i = 0; i < pPriv->u32SampleCount; ++i) {
        u64FragDurTicks += pPriv->astSamples[i].u32Duration;
    }
    pPriv->u64BaseDecodeTime += u64FragDurTicks;
    pPriv->u32SeqNumber++;
    pPriv->u32SampleCount = 0;
    pPriv->u32FragLen = 0;
    return ERR_MUX_OK;
}

/* ---- writer ops ---- */

/* The init segment is written lazily on the first key frame so that parameter
 * sets (SPS/PPS/VPS) are available for the avcC/hvcC config boxes. */
static S32 mp4_write_init_segment(MuxWriter *pWr) {
    Mp4Priv *pPriv = (Mp4Priv *)pWr->pPriv;
    U8 *au8Init = pPriv->au8InitBuf;
    U32 u32Len;

    /* Derive resolution from SPS if attr did not provide it. */
    if (pWr->u32Width == 0 || pWr->u32Height == 0) {
        U32 u32W = 0;
        U32 u32H = 0;
        if (pWr->eCodecType == MUX_CODEC_H265) {
            H265_ParseSps(pPriv->stSets.au8Sps, pPriv->stSets.u32SpsLen, &u32W, &u32H);
        } else {
            H264_ParseSps(pPriv->stSets.au8Sps, pPriv->stSets.u32SpsLen, &u32W, &u32H);
        }
        if (u32W > 0 && u32H > 0) {
            pWr->u32Width = u32W;
            pWr->u32Height = u32H;
        }
    }

    u32Len = box_build_init_segment(pWr, au8Init, sizeof(pPriv->au8InitBuf));
    if (u32Len == 0) {
        MUX_LOGE("mp4 init segment build failed");
        return ERR_MUX_WRITE_FAIL;
    }
    if (fwrite(au8Init, 1, u32Len, pWr->pFile) != u32Len) {
        MUX_LOGE("mp4 init segment write failed");
        return ERR_MUX_WRITE_FAIL;
    }
    pPriv->bInitDone = MPP_TRUE;
    return ERR_MUX_OK;
}

static S32 mp4_start(MuxWriter *pWr) {
    Mp4Priv *pPriv = (Mp4Priv *)pWr->pPriv;
    /* Reset per-file fragment state; init segment deferred to first key frame.
     * Clear the cached parameter sets too: on file rolling mp4_start runs again
     * for the next segment, and a stale SPS/PPS/VPS would make the new file's
     * init segment carry the previous file's avcC/hvcC if the stream format
     * changed at the split boundary. */
    memset(&pPriv->stSets, 0, sizeof(pPriv->stSets));
    pPriv->bSpsParsed = MPP_FALSE;
    pPriv->bInitDone = MPP_FALSE;
    pPriv->u32SeqNumber = 1;
    pPriv->u32SampleCount = 0;
    pPriv->u32FragLen = 0;
    pPriv->u64BaseDecodeTime = 0;
    /* Use UINT64_MAX as a sentinel: mp4_write detects this on the first
     * frame and anchors both values to the actual PTS, avoiding a spurious
     * large delta when PTS >> 0 (e.g. monotonic clock timestamps). */
    pPriv->u64FirstPtsUs = UINT64_MAX;
    pPriv->u64LastPtsUs = UINT64_MAX;
    return ERR_MUX_OK;
}

static S32 mp4_write(MuxWriter *pWr, const U8 *pu8Data, U32 u32Size, BOOL bKeyFrame, U64 u64PtsUs) {
    Mp4Priv *pPriv = (Mp4Priv *)pWr->pPriv;
    Mp4Sample *pSample;
    U32 u32AvccLen = 0;
    U32 u32Dur;
    S32 ret;

    /* Always refresh cached parameter sets so the very first init segment and
     * any later format hints are correct. */
    MuxCollectParamSets(pu8Data, u32Size, pWr->eCodecType, &pPriv->stSets);

    if (!pPriv->bInitDone) {
        /* Need parameter sets before we can write avcC/hvcC. Wait for the
         * first key frame, which carries SPS/PPS(/VPS). */
        if (!bKeyFrame || pPriv->stSets.u32SpsLen == 0 || pPriv->stSets.u32PpsLen == 0) {
            return ERR_MUX_OK; /* drop until first complete key frame */
        }
        ret = mp4_write_init_segment(pWr);
        if (ret != ERR_MUX_OK) {
            return ret;
        }
        pPriv->u64FirstPtsUs = u64PtsUs;
        pPriv->u64LastPtsUs = u64PtsUs;
    }

    /* fMP4 fragments must start on a key frame so each moof is independently
     * seekable and decodable. When u32FragDurationMs == 0 the policy is
     * "one fragment per GOP": flush the accumulated fragment at every key
     * frame. When > 0, flush when the target duration is reached. Non-key
     * frames never trigger a duration/GOP flush. */
    if (bKeyFrame && pPriv->u32SampleCount > 0) {
        BOOL bFlush = MPP_FALSE;
        if (pWr->u32FragDurationMs == 0) {
            /* Per-GOP fragmentation: every key frame starts a new fragment. */
            bFlush = MPP_TRUE;
        } else {
            U64 u64FragMs = (u64PtsUs - pPriv->u64FirstPtsUs) / 1000ULL;
            if (u64FragMs >= pWr->u32FragDurationMs) {
                bFlush = MPP_TRUE;
            }
        }
        if (bFlush) {
            ret = mp4_flush_fragment(pWr);
            if (ret != ERR_MUX_OK) {
                pPriv->u32SampleCount = 0;
                pPriv->u32FragLen = 0;
                return ret;
            }
            pPriv->u64FirstPtsUs = u64PtsUs;
        }
    }

    if (pPriv->u32SampleCount >= MP4_MAX_SAMPLES_PER_FRAG) {
        ret = mp4_flush_fragment(pWr);
        if (ret != ERR_MUX_OK) {
            pPriv->u32SampleCount = 0;
            pPriv->u32FragLen = 0;
            return ret;
        }
        /* Re-anchor the fragment duration clock so the next fragment measures
         * from its own first sample, not the start of the file. */
        pPriv->u64FirstPtsUs = u64PtsUs;
    }

    /* Convert Annex-B access units to length-prefixed payloads after dropping
     * SPS/PPS/VPS. The parameter sets are already advertised in avcC/hvcC, so
     * only VCL NALs should land in mdat. */

    /* Reject a single access unit that can never fit in the fragment buffer
     * regardless of how much is currently accumulated. Check BEFORE flush so
     * we surface the error immediately instead of silently writing into a
     * too-small buffer after flush resets u32FragLen to zero.
     * Use subtraction form to avoid u32Size+64 unsigned overflow when u32Size
     * is near UINT32_MAX. pPriv->u32FragCap is always >= 64. */
    if (u32Size > pPriv->u32FragCap - 64) {
        MUX_LOGE("mp4 sample too large (%u > cap %u), aborting",
            u32Size, pPriv->u32FragCap - 64);
        return ERR_MUX_WRITE_FAIL;
    }

    if (pPriv->u32FragLen + u32Size + 64 > pPriv->u32FragCap) {
        ret = mp4_flush_fragment(pWr);
        if (ret != ERR_MUX_OK) {
            pPriv->u32SampleCount = 0;
            pPriv->u32FragLen = 0;
            return ret;
        }
    }

    /* After flush or when first entering, verify remaining capacity can hold
     * the worst-case converted payload (each NAL gains a 4-byte length prefix
     * replacing the 3/4-byte start code, so output ≤ input + 64 is a safe
     * upper bound). */
    if (pPriv->u32FragCap - pPriv->u32FragLen < u32Size + 64) {
        MUX_LOGE("mp4 frag buf still too small after flush (need %u, have %u)",
            u32Size + 64, pPriv->u32FragCap - pPriv->u32FragLen);
        return ERR_MUX_WRITE_FAIL;
    }

    ret = MuxAnnexBToAvccVcl(pu8Data, u32Size, pWr->eCodecType, pPriv->pu8FragBuf + pPriv->u32FragLen,
        pPriv->u32FragCap - pPriv->u32FragLen, &u32AvccLen);
    if (ret != 0 || u32AvccLen == 0) {
        MUX_LOGE("mp4 annexb->avcc conversion failed (size=%u, ret=%d)", u32Size, ret);
        return ERR_MUX_WRITE_FAIL;
    }

    /* Compute sample duration in timescale ticks from PTS delta. A PTS
     * discontinuity (gap or backward jump) must never produce an absurd single
     * sample duration that would skew the player's seek timeline, so clamp the
     * delta-derived value to a sane multiple of the nominal frame duration and
     * fall back to the default whenever the delta is zero or non-monotonic.
     *
     * PTS discontinuity detection: a gap exceeding the tolerance threshold
     * indicates a stream restart, source switch or timestamp reset.  The
     * threshold is MAX(2×defaultDur, MP4_MAX_DUR_FACTOR×defaultDur) so that
     * variable-frame-rate streams or longer GOPs are not falsely classified
     * as discontinuities. */
    if (u64PtsUs > pPriv->u64LastPtsUs) {
        U64 u64DeltaUs = u64PtsUs - pPriv->u64LastPtsUs;
        U64 u64DefaultUs = (U64)pPriv->u32DefaultDurTicks * 1000000ULL / MP4_TIMESCALE;
        U64 u64ThreshUs = u64DefaultUs * MP4_MAX_DUR_FACTOR;
        if (u64ThreshUs < 2 * u64DefaultUs) {
            u64ThreshUs = 2 * u64DefaultUs;
        }
        if (u64DeltaUs > u64ThreshUs) {
            /* PTS discontinuity — treat as stream restart. */
            u32Dur = pPriv->u32DefaultDurTicks;
        } else {
            u32Dur = (U32)((u64DeltaUs * MP4_TIMESCALE) / 1000000ULL);
            if (u32Dur == 0) {
                u32Dur = pPriv->u32DefaultDurTicks;
            }
        }
    } else {
        u32Dur = pPriv->u32DefaultDurTicks;
    }
    pPriv->u64LastPtsUs = u64PtsUs;

    pSample = &pPriv->astSamples[pPriv->u32SampleCount];
    pSample->u32Offset = pPriv->u32FragLen;
    pSample->u32Size = u32AvccLen;
    pSample->u32Duration = u32Dur;
    pSample->bKeyFrame = bKeyFrame;
    pPriv->u32FragLen += u32AvccLen;
    pPriv->u32SampleCount++;

    return ERR_MUX_OK;
}

static S32 mp4_finish(MuxWriter *pWr) {
    Mp4Priv *pPriv = (Mp4Priv *)pWr->pPriv;
    if (!pPriv->bInitDone) {
        return ERR_MUX_OK;
    }
    return mp4_flush_fragment(pWr);
}





