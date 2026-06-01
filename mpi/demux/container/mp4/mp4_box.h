/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    mp4_box.h
 * @Brief     :    MP4/ISO Base Media File Format box definitions.
 *------------------------------------------------------------------------------
 */

#ifndef MP4_BOX_H
#define MP4_BOX_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Box type codes (FourCC) */
#define MP4_BOX_FTYP 0x66747970 /* ftyp */
#define MP4_BOX_MOOV 0x6D6F6F76 /* moov */
#define MP4_BOX_MVHD 0x6D766864 /* mvhd */
#define MP4_BOX_TRAK 0x7472616B /* trak */
#define MP4_BOX_TKHD 0x746B6864 /* tkhd */
#define MP4_BOX_MDIA 0x6D646961 /* mdia */
#define MP4_BOX_MDHD 0x6D646864 /* mdhd */
#define MP4_BOX_HDLR 0x68646C72 /* hdlr */
#define MP4_BOX_MINF 0x6D696E66 /* minf */
#define MP4_BOX_STBL 0x7374626C /* stbl */
#define MP4_BOX_STSD 0x73747364 /* stsd */
#define MP4_BOX_STTS 0x73747473 /* stts */
#define MP4_BOX_STSS 0x73747373 /* stss */
#define MP4_BOX_STSC 0x73747363 /* stsc */
#define MP4_BOX_STSZ 0x7374737A /* stsz */
#define MP4_BOX_STCO 0x7374636F /* stco */
#define MP4_BOX_CO64 0x636F3634 /* co64 */
#define MP4_BOX_CTTS 0x63747473 /* ctts */
#define MP4_BOX_MDAT 0x6D646174 /* mdat */
#define MP4_BOX_FREE 0x66726565 /* free */
#define MP4_BOX_SKIP 0x736B6970 /* skip */
#define MP4_BOX_UDTA 0x75647461 /* udta */

/* Codec-specific boxes */
#define MP4_BOX_AVC1 0x61766331 /* avc1 */
#define MP4_BOX_AVCC 0x61766343 /* avcC */
#define MP4_BOX_HVC1 0x68766331 /* hvc1 */
#define MP4_BOX_HEV1 0x68657631 /* hev1 */
#define MP4_BOX_HVCC 0x68766343 /* hvcC */
#define MP4_BOX_MP4A 0x6D703461 /* mp4a */
#define MP4_BOX_ESDS 0x65736473 /* esds */

/* Handler types */
#define MP4_HANDLER_VIDEO 0x76696465 /* vide */
#define MP4_HANDLER_AUDIO 0x736F756E /* soun */

/* Box header */
typedef struct _Mp4BoxHeader {
    U32 u32Size;
    U32 u32Type;
    U64 u64LargeSize; /* Used if u32Size == 1 */
    U64 u64Offset;    /* File offset of this box */
} Mp4BoxHeader;

/* Sample entry base */
typedef struct _Mp4SampleEntry {
    U16 u16DataRefIndex;
} Mp4SampleEntry;

/* Visual sample entry (avc1, hvc1) */
typedef struct _Mp4VisualSampleEntry {
    Mp4SampleEntry stBase;
    U16 u16Width;
    U16 u16Height;
    U32 u32HorizRes;
    U32 u32VertRes;
    U16 u16FrameCount;
    U16 u16Depth;
} Mp4VisualSampleEntry;

/* AVC Decoder Configuration */
typedef struct _AvcDecoderConfig {
    U8 u8ConfigVersion;
    U8 u8ProfileIdc;
    U8 u8ProfileCompat;
    U8 u8LevelIdc;
    U8 u8NaluLengthSize; /* 1, 2, or 4 */
    U8 u8NumSps;
    U8 u8NumPps;
    U8 *pu8Sps;
    U16 u16SpsLen;
    U8 *pu8Pps;
    U16 u16PpsLen;
} AvcDecoderConfig;

/* HEVC Decoder Configuration */
typedef struct _HevcDecoderConfig {
    U8 u8ConfigVersion;
    U8 u8GeneralProfileIdc;
    U8 u8GeneralLevelIdc;
    U8 u8NaluLengthSize;
    U8 u8NumArrays;
    /* VPS/SPS/PPS arrays */
    U8 *pu8Vps;
    U16 u16VpsLen;
    U8 *pu8Sps;
    U16 u16SpsLen;
    U8 *pu8Pps;
    U16 u16PpsLen;
} HevcDecoderConfig;

/* Sample table entry */
typedef struct _Mp4SttsEntry {
    U32 u32SampleCount;
    U32 u32SampleDelta;
} Mp4SttsEntry;

typedef struct _Mp4StscEntry {
    U32 u32FirstChunk;
    U32 u32SamplesPerChunk;
    U32 u32SampleDescIdx;
} Mp4StscEntry;

/**
 * @brief  Read box header
 */
S32 Mp4Box_ReadHeader(const U8 *pu8Data, U32 u32Len, Mp4BoxHeader *pHeader);

/**
 * @brief  Get box type as string
 */
void Mp4Box_TypeToString(U32 u32Type, char *pStr);

/**
 * @brief  Parse AVC decoder configuration (avcC)
 */
S32 Mp4Box_ParseAvcC(const U8 *pu8Data, U32 u32Len, AvcDecoderConfig *pConfig);

/**
 * @brief  Parse HEVC decoder configuration (hvcC)
 */
S32 Mp4Box_ParseHvcC(const U8 *pu8Data, U32 u32Len, HevcDecoderConfig *pConfig);

#ifdef __cplusplus
}
#endif

#endif /* __MP4_BOX_H__ */
