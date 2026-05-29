/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    pes_parser.c
 * @Brief     :    MPEG-2 PES (Packetized Elementary Stream) parser implementation.
 *------------------------------------------------------------------------------
 */

#include "pes_parser.h"
#include <string.h>

/*
 * PES Header Structure (ISO/IEC 13818-1):
 *
 * packet_start_code_prefix     24 bits  0x000001
 * stream_id                     8 bits
 * PES_packet_length            16 bits
 *
 * Optional PES header (if not padding/private_stream_2):
 * '10'                          2 bits
 * PES_scrambling_control        2 bits
 * PES_priority                  1 bit
 * data_alignment_indicator      1 bit
 * copyright                     1 bit
 * original_or_copy              1 bit
 * PTS_DTS_flags                 2 bits  (00=none, 10=PTS only, 11=PTS+DTS)
 * ESCR_flag                     1 bit
 * ES_rate_flag                  1 bit
 * DSM_trick_mode_flag           1 bit
 * additional_copy_info_flag     1 bit
 * PES_CRC_flag                  1 bit
 * PES_extension_flag            1 bit
 * PES_header_data_length        8 bits
 * [optional fields based on flags]
 */

/* Parse PTS/DTS value from 5 bytes */
static S64 parse_pts_dts(const U8 *p) {
    S64 pts;

    /* Format: 4 bits marker, 3 bits [32..30], 1 bit marker,
     *         15 bits [29..15], 1 bit marker,
     *         15 bits [14..0], 1 bit marker
     */
    pts = ((S64)(p[0] & 0x0E)) << 29;
    pts |= ((S64)p[1]) << 22;
    pts |= ((S64)(p[2] & 0xFE)) << 14;
    pts |= ((S64)p[3]) << 7;
    pts |= ((S64)(p[4] & 0xFE)) >> 1;

    return pts;
}

S32 Pes_ParseHeader(const U8 *pu8Data, U32 u32Len, PesHeader *pHeader) {
    U32 u32Offset;
    U8 u8Flags1, u8Flags2;

    if (!pu8Data || !pHeader) {
        return -1;
    }

    /* Minimum PES packet: start code (3) + stream_id (1) + length (2) = 6 bytes */
    if (u32Len < 6) {
        return 1; /* Need more data */
    }

    memset(pHeader, 0, sizeof(PesHeader));

    /* Check start code prefix: 0x000001 */
    if (pu8Data[0] != 0x00 || pu8Data[1] != 0x00 || pu8Data[2] != 0x01) {
        return -1; /* Invalid start code */
    }

    /* Stream ID */
    pHeader->u8StreamId = pu8Data[3];

    /* PES packet length (0 = unbounded, valid for video) */
    pHeader->u16PacketLen = (pu8Data[4] << 8) | pu8Data[5];

    u32Offset = 6;

    /* Check if optional header is present */
    /* Padding stream (0xBE) and private_stream_2 (0xBF) have no optional header */
    if (pHeader->u8StreamId != PES_STREAM_ID_PADDING && pHeader->u8StreamId != PES_STREAM_ID_PRIVATE_2 &&
        pHeader->u8StreamId != PES_STREAM_ID_PROGRAM_MAP) {
        /* Need at least 3 more bytes for optional header base */
        if (u32Len < u32Offset + 3) {
            return 1; /* Need more data */
        }

        /* First optional byte: '10' + scrambling(2) + priority + align + copy + orig */
        u8Flags1 = pu8Data[u32Offset];
        if ((u8Flags1 & 0xC0) != 0x80) {
            /* Should start with '10' marker bits */
            return -1;
        }

        pHeader->u8ScramblingCtrl = (u8Flags1 >> 4) & 0x03;
        pHeader->u8Priority = (u8Flags1 >> 3) & 0x01;
        pHeader->u8DataAlignIndicator = (u8Flags1 >> 2) & 0x01;
        pHeader->u8Copyright = (u8Flags1 >> 1) & 0x01;
        pHeader->u8Original = u8Flags1 & 0x01;
        u32Offset++;

        /* Second optional byte: PTS/DTS flags and other flags */
        u8Flags2 = pu8Data[u32Offset];
        pHeader->u8PtsDtsFlags = (u8Flags2 >> 6) & 0x03;
        pHeader->u8EscrFlag = (u8Flags2 >> 5) & 0x01;
        pHeader->u8EsRateFlag = (u8Flags2 >> 4) & 0x01;
        pHeader->u8DsmTrickMode = (u8Flags2 >> 3) & 0x01;
        pHeader->u8AdditionalCopyInfo = (u8Flags2 >> 2) & 0x01;
        pHeader->u8CrcFlag = (u8Flags2 >> 1) & 0x01;
        pHeader->u8ExtensionFlag = u8Flags2 & 0x01;
        u32Offset++;

        /* PES header data length */
        pHeader->u8HeaderDataLen = pu8Data[u32Offset];
        u32Offset++;

        /* Check if we have enough data for optional fields */
        if (u32Len < u32Offset + pHeader->u8HeaderDataLen) {
            return 1; /* Need more data */
        }

        U32 u32OptStart = u32Offset;

        /* Parse PTS if present */
        if (pHeader->u8PtsDtsFlags == 0x02 || pHeader->u8PtsDtsFlags == 0x03) {
            if (u32Offset + 5 > u32OptStart + pHeader->u8HeaderDataLen) {
                return -1;
            }
            pHeader->s64Pts = parse_pts_dts(&pu8Data[u32Offset]);
            u32Offset += 5;
        }

        /* Parse DTS if present (only when PTS_DTS_flags == '11') */
        if (pHeader->u8PtsDtsFlags == 0x03) {
            if (u32Offset + 5 > u32OptStart + pHeader->u8HeaderDataLen) {
                return -1;
            }
            pHeader->s64Dts = parse_pts_dts(&pu8Data[u32Offset]);
            u32Offset += 5;
        } else {
            pHeader->s64Dts = pHeader->s64Pts; /* DTS = PTS if not present */
        }

        /* Skip remaining optional fields (ESCR, ES_rate, etc.) */
        u32Offset = u32OptStart + pHeader->u8HeaderDataLen;
    }

    pHeader->u32HeaderSize = u32Offset;
    pHeader->u32DataOffset = u32Offset;

    return 0;
}
