/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    pes_parser.h
 * @Brief     :    MPEG-2 PES (Packetized Elementary Stream) parser.
 *------------------------------------------------------------------------------
 */

#ifndef PES_PARSER_H
#define PES_PARSER_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PES start code prefix */
#define PES_START_CODE_PREFIX 0x000001

/* Stream IDs */
#define PES_STREAM_ID_PROGRAM_MAP 0xBC
#define PES_STREAM_ID_PRIVATE_1 0xBD
#define PES_STREAM_ID_PADDING 0xBE
#define PES_STREAM_ID_PRIVATE_2 0xBF
#define PES_STREAM_ID_AUDIO_START 0xC0 /* 0xC0-0xDF */
#define PES_STREAM_ID_VIDEO_START 0xE0 /* 0xE0-0xEF */

typedef struct _PesHeader {
    U8 u8StreamId;
    U16 u16PacketLen;
    U8 u8ScramblingCtrl;
    U8 u8Priority;
    U8 u8DataAlignIndicator;
    U8 u8Copyright;
    U8 u8Original;
    U8 u8PtsDtsFlags;
    U8 u8EscrFlag;
    U8 u8EsRateFlag;
    U8 u8DsmTrickMode;
    U8 u8AdditionalCopyInfo;
    U8 u8CrcFlag;
    U8 u8ExtensionFlag;
    U8 u8HeaderDataLen;

    S64 s64Pts;
    S64 s64Dts;

    U32 u32HeaderSize; /* Total header size including optional fields */
    U32 u32DataOffset; /* Offset to ES data */
} PesHeader;

/**
 * @brief  Parse PES header
 * @return 0 on success, -1 on error, 1 if need more data
 */
S32 Pes_ParseHeader(const U8 *pu8Data, U32 u32Len, PesHeader *pHeader);

/**
 * @brief  Check if stream ID is video
 */
static inline BOOL Pes_IsVideo(U8 u8StreamId) { return (u8StreamId >= 0xE0 && u8StreamId <= 0xEF); }

/**
 * @brief  Check if stream ID is audio
 */
static inline BOOL Pes_IsAudio(U8 u8StreamId) { return (u8StreamId >= 0xC0 && u8StreamId <= 0xDF); }

/**
 * @brief  Get PES payload
 */
static inline const U8 *Pes_GetPayload(const U8 *pu8Data, const PesHeader *pHeader) {
    return pu8Data + pHeader->u32DataOffset;
}

/**
 * @brief  Calculate PES payload size
 */
static inline U32 Pes_GetPayloadSize(const PesHeader *pHeader) {
    if (pHeader->u16PacketLen == 0) {
        return 0; /* Unbounded */
    }
    return pHeader->u16PacketLen - (pHeader->u32DataOffset - 6);
}

#ifdef __cplusplus
}
#endif

#endif /* __PES_PARSER_H__ */
