/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_ts.c
 * @Date      :    2026-06-11
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    MPEG-2 Transport Stream (TS) writer for MUX recording.
 *
 * TS is inherently streamable: PAT/PMT are re-emitted periodically and every
 * 188-byte packet is self-framed, so a recording truncated by power loss is
 * still playable up to the last complete packet. Each access unit is wrapped
 * in a PES packet and sliced across TS packets on the elementary PID.
 *------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec/h264_utils.h"
#include "codec/h265_utils.h"
#include "container/ts/pat_pmt.h"
#include "mux_common.h"
#include "mux_writer.h"

#define TS_PKT_SIZE 188
#define TS_SYNC 0x47
#define TS_PID_PMT_OUT 0x1000
#define TS_PID_VIDEO 0x0100
#define TS_PCR_PID TS_PID_VIDEO
#define TS_PAT_PMT_INTERVAL_TICKS (90000 / 2) /* re-emit PAT/PMT every 0.5s */

typedef struct _TsPriv {
    BOOL bStarted;
    U8 u8PatCc;        /* continuity counter for PAT */
    U8 u8PmtCc;        /* continuity counter for PMT */
    U8 u8VideoCc;      /* continuity counter for video PID */
    U8 u8StreamType;   /* STREAM_TYPE_H264 / H265 */
    U64 u64LastPatPts; /* last PAT/PMT emission time in 90kHz ticks */
    BOOL bFirstAu;
    /* scratch buffer for assembling one PES payload (Annex-B with AUD) */
    U8 *pu8PesBuf;
    U32 u32PesCap;
} TsPriv;

/* ---- forward decls ---- */
static S32 ts_start(MuxWriter *pWr);
static S32 ts_write(MuxWriter *pWr, const U8 *pu8Data, U32 u32Size, BOOL bKeyFrame, U64 u64PtsUs);
static S32 ts_finish(MuxWriter *pWr);
static VOID ts_destroy(MuxWriter *pWr);

static const MuxWriterOps g_stTsOps = {
    "ts", ts_start, ts_write, ts_finish, ts_destroy,
};

S32 MuxTs_Attach(MuxWriter *pWr) {
    TsPriv *pPriv;

    if (!pWr) {
        return -1;
    }
    pPriv = (TsPriv *)calloc(1, sizeof(TsPriv));
    if (!pPriv) {
        return -1;
    }
    pPriv->u32PesCap = 2 * 1024 * 1024;
    pPriv->pu8PesBuf = (U8 *)malloc(pPriv->u32PesCap);
    if (!pPriv->pu8PesBuf) {
        free(pPriv);
        return -1;
    }
    pPriv->u8StreamType = (pWr->eCodecType == MUX_CODEC_H265) ? STREAM_TYPE_H265 : STREAM_TYPE_H264;
    pWr->pstOps = &g_stTsOps;
    pWr->pPriv = pPriv;
    return 0;
}

static VOID ts_destroy(MuxWriter *pWr) {
    TsPriv *pPriv = (TsPriv *)pWr->pPriv;
    if (pPriv && pPriv->pu8PesBuf) {
        free(pPriv->pu8PesBuf);
        pPriv->pu8PesBuf = NULL;
    }
    /* pPriv freed by MuxWriter_Destroy. */
}

/* MPEG-2 systems CRC32 (poly 0x04C11DB7, MSB-first, init 0xFFFFFFFF). */
static U32 ts_crc32(const U8 *pu8Data, U32 u32Len) {
    U32 u32Crc = 0xFFFFFFFFU;
    U32 i;
    U32 j;
    for (i = 0; i < u32Len; ++i) {
        u32Crc ^= (U32)pu8Data[i] << 24;
        for (j = 0; j < 8; ++j) {
            if (u32Crc & 0x80000000U) {
                u32Crc = (u32Crc << 1) ^ 0x04C11DB7U;
            } else {
                u32Crc <<= 1;
            }
        }
    }
    return u32Crc;
}

/* ---------------------------------------------------------------------------
 * PSI section emission. PAT and PMT each fit in a single TS packet for our
 * single-program / single-video-track stream.
 * ------------------------------------------------------------------------ */
static S32 ts_write_pat(MuxWriter *pWr, U8 *pu8Cc) {
    U8 au8Pkt[TS_PKT_SIZE];
    U8 *p = au8Pkt;
    U8 *pSecStart;
    U32 u32Crc;
    U32 u32Len;

    memset(au8Pkt, 0xFF, sizeof(au8Pkt));
    *p++ = TS_SYNC;
    *p++ = 0x40;                       /* PUSI=1, PID high (0x0000) */
    *p++ = 0x00;                       /* PID low = PAT PID 0 */
    *p++ = 0x10 | (*pu8Cc & 0x0F);     /* no adaptation, payload only */
    *p++ = 0x00;                       /* pointer_field */
    pSecStart = p;
    *p++ = TABLE_ID_PAT;               /* table_id */
    *p++ = 0xB0;                       /* section_syntax=1, len high */
    *p++ = 13;                         /* section_length low (5+4 PAT entry... =13) */
    *p++ = 0x00;                       /* transport_stream_id high */
    *p++ = 0x01;                       /* transport_stream_id low */
    *p++ = 0xC1;                       /* version=0, current_next=1 */
    *p++ = 0x00;                       /* section_number */
    *p++ = 0x00;                       /* last_section_number */
    /* one program: program_number=1 -> PMT PID */
    *p++ = 0x00;
    *p++ = 0x01;
    *p++ = (U8)(0xE0 | ((TS_PID_PMT_OUT >> 8) & 0x1F));
    *p++ = (U8)(TS_PID_PMT_OUT & 0xFF);
    u32Len = (U32)(p - pSecStart);
    u32Crc = ts_crc32(pSecStart, u32Len);
    *p++ = (U8)((u32Crc >> 24) & 0xFF);
    *p++ = (U8)((u32Crc >> 16) & 0xFF);
    *p++ = (U8)((u32Crc >> 8) & 0xFF);
    *p++ = (U8)(u32Crc & 0xFF);

    *pu8Cc = (U8)((*pu8Cc + 1) & 0x0F);
    if (fwrite(au8Pkt, 1, TS_PKT_SIZE, pWr->pFile) != TS_PKT_SIZE) {
        return ERR_MUX_WRITE_FAIL;
    }
    return ERR_MUX_OK;
}

static S32 ts_write_pmt(MuxWriter *pWr, U8 u8StreamType, U8 *pu8Cc) {
    U8 au8Pkt[TS_PKT_SIZE];
    U8 *p = au8Pkt;
    U8 *pSecStart;
    U8 *pLenPos;
    U32 u32Crc;
    U32 u32Len;

    memset(au8Pkt, 0xFF, sizeof(au8Pkt));
    *p++ = TS_SYNC;
    *p++ = (U8)(0x40 | ((TS_PID_PMT_OUT >> 8) & 0x1F)); /* PUSI=1 + PID high */
    *p++ = (U8)(TS_PID_PMT_OUT & 0xFF);
    *p++ = 0x10 | (*pu8Cc & 0x0F);
    *p++ = 0x00;                       /* pointer_field */
    pSecStart = p;
    *p++ = TABLE_ID_PMT;               /* table_id */
    pLenPos = p;
    *p++ = 0xB0;                       /* section_syntax=1, len high (patched) */
    *p++ = 0x00;                       /* section_length low (patched) */
    *p++ = 0x00;                       /* program_number high */
    *p++ = 0x01;                       /* program_number low */
    *p++ = 0xC1;                       /* version=0, current_next=1 */
    *p++ = 0x00;                       /* section_number */
    *p++ = 0x00;                       /* last_section_number */
    *p++ = (U8)(0xE0 | ((TS_PCR_PID >> 8) & 0x1F)); /* PCR_PID high */
    *p++ = (U8)(TS_PCR_PID & 0xFF);
    *p++ = 0xF0;                       /* program_info_length high */
    *p++ = 0x00;                       /* program_info_length low */
    /* one elementary stream entry */
    *p++ = u8StreamType;
    *p++ = (U8)(0xE0 | ((TS_PID_VIDEO >> 8) & 0x1F)); /* elementary_PID high */
    *p++ = (U8)(TS_PID_VIDEO & 0xFF);
    *p++ = 0xF0;                       /* ES_info_length high */
    *p++ = 0x00;                       /* ES_info_length low */
    /* section_length counts bytes after the length field including CRC */
    u32Len = (U32)(p - (pLenPos + 2)) + 4;
    pLenPos[0] = (U8)(0xB0 | ((u32Len >> 8) & 0x0F));
    pLenPos[1] = (U8)(u32Len & 0xFF);
    u32Len = (U32)(p - pSecStart);
    u32Crc = ts_crc32(pSecStart, u32Len);
    *p++ = (U8)((u32Crc >> 24) & 0xFF);
    *p++ = (U8)((u32Crc >> 16) & 0xFF);
    *p++ = (U8)((u32Crc >> 8) & 0xFF);
    *p++ = (U8)(u32Crc & 0xFF);

    *pu8Cc = (U8)((*pu8Cc + 1) & 0x0F);
    if (fwrite(au8Pkt, 1, TS_PKT_SIZE, pWr->pFile) != TS_PKT_SIZE) {
        return ERR_MUX_WRITE_FAIL;
    }
    return ERR_MUX_OK;
}

/* Encode a 33-bit timestamp into a 5-byte PTS field (prefix 0x21 = PTS only). */
static VOID ts_put_pts(U8 *p, U8 u8Prefix, U64 u64Pts) {
    p[0] = (U8)(u8Prefix | (U8)((u64Pts >> 29) & 0x0E) | 0x01);
    p[1] = (U8)((u64Pts >> 22) & 0xFF);
    p[2] = (U8)(((u64Pts >> 14) & 0xFE) | 0x01);
    p[3] = (U8)((u64Pts >> 7) & 0xFF);
    p[4] = (U8)(((u64Pts << 1) & 0xFE) | 0x01);
}

/* Write a 6-byte PCR field. base is in 90kHz units; extension domain unused. */
static VOID ts_put_pcr(U8 *p, U64 u64Base) {
    U64 u64Pcr = u64Base * 300; /* 27MHz base; PCR_ext = 0 */
    p[0] = (U8)((u64Pcr >> 25) & 0xFF);
    p[1] = (U8)((u64Pcr >> 17) & 0xFF);
    p[2] = (U8)((u64Pcr >> 9) & 0xFF);
    p[3] = (U8)((u64Pcr >> 1) & 0xFF);
    p[4] = (U8)(((u64Pcr & 0x01) << 7) | 0x7E); /* 6 reserved bits = 1 */
    p[5] = 0x00;
}

/* Slice an assembled PES packet across 188-byte TS packets on the video PID.
 * The first packet carries PUSI; key frames also carry a PCR adaptation field.
 * The final packet is padded with an adaptation field so it fills exactly. */
static S32 ts_write_pes(MuxWriter *pWr, const U8 *pu8Pes, U32 u32PesLen, BOOL bKeyFrame, U64 u64Pcr) {
    TsPriv *pPriv = (TsPriv *)pWr->pPriv;
    U32 u32Off = 0;
    BOOL bFirst = MPP_TRUE;

    while (u32Off < u32PesLen) {
        U8 au8Pkt[TS_PKT_SIZE];
        U32 u32Remain = u32PesLen - u32Off;
        BOOL bPcr = (BOOL)(bFirst && bKeyFrame);
        U32 u32AfTotal = 0; /* adaptation field bytes incl. its length byte */
        U32 u32Cap;
        U32 u32Copy;
        U32 u32Pos = 4;
        U8 u8Afc;

        if (bPcr) {
            u32Cap = TS_PKT_SIZE - 4 - 8; /* len+flags+pcr = 8 */
            if (u32Remain >= u32Cap) {
                u32Copy = u32Cap;
                u32AfTotal = 8;
            } else {
                u32Copy = u32Remain;
                u32AfTotal = TS_PKT_SIZE - 4 - u32Remain;
            }
            u8Afc = 0x30;
        } else {
            u32Cap = TS_PKT_SIZE - 4;
            if (u32Remain >= u32Cap) {
                u32Copy = u32Cap;
                u8Afc = 0x10;
            } else {
                u32Copy = u32Remain;
                u32AfTotal = TS_PKT_SIZE - 4 - u32Remain;
                /* A stuffing adaptation field needs at least 2 bytes (length +
                 * flags). When fewer than 2 bytes would remain (u32Remain ==
                 * 183), shrink the payload so the next packet carries the
                 * spill-over and this packet has a well-formed 2-byte field.
                 * Guard the subtraction so u32Copy can never underflow. */
                if (u32AfTotal < 2) {
                    U32 u32Short = 2 - u32AfTotal;
                    if (u32Copy < u32Short) {
                        /* Not enough payload to borrow from for a well-formed
                         * 2-byte adaptation field. With 188-byte packets this
                         * is unreachable, but never drop the remainder
                         * silently: surface it as an error so a future sizing
                         * regression is observable rather than corrupting the
                         * PES and desyncing the continuity counter. */
                        MUX_LOGE("ts pes tail underflow (remain=%u copy=%u)", u32Remain, u32Copy);
                        return ERR_MUX_WRITE_FAIL;
                    }
                    u32Copy -= u32Short;
                    u32AfTotal = 2;
                }
                u8Afc = 0x30;
            }
        }

        au8Pkt[0] = TS_SYNC;
        au8Pkt[1] = (U8)((bFirst ? 0x40 : 0x00) | ((TS_PID_VIDEO >> 8) & 0x1F));
        au8Pkt[2] = (U8)(TS_PID_VIDEO & 0xFF);
        au8Pkt[3] = (U8)(u8Afc | (pPriv->u8VideoCc & 0x0F));

        if (u8Afc & 0x20) {
            if (u32AfTotal <= 1) {
                au8Pkt[u32Pos++] = 0x00; /* af_len = 0, no flags */
            } else {
                U32 u32Stuff;
                au8Pkt[u32Pos++] = (U8)(u32AfTotal - 1); /* af_len */
                au8Pkt[u32Pos++] = (U8)(bPcr ? 0x10 : 0x00);
                if (bPcr) {
                    ts_put_pcr(&au8Pkt[u32Pos], u64Pcr);
                    u32Pos += 6;
                }
                u32Stuff = u32AfTotal - (bPcr ? 8 : 2);
                if (u32Stuff > 0) {
                    memset(&au8Pkt[u32Pos], 0xFF, u32Stuff);
                    u32Pos += u32Stuff;
                }
            }
        }

        memcpy(&au8Pkt[u32Pos], pu8Pes + u32Off, u32Copy);
        u32Pos += u32Copy;
        /* u32Pos must now equal TS_PKT_SIZE */
        u32Off += u32Copy;
        pPriv->u8VideoCc = (U8)((pPriv->u8VideoCc + 1) & 0x0F);
        bFirst = MPP_FALSE;

        if (fwrite(au8Pkt, 1, TS_PKT_SIZE, pWr->pFile) != TS_PKT_SIZE) {
            return ERR_MUX_WRITE_FAIL;
        }
    }
    return ERR_MUX_OK;
}

/* ---- writer ops ---- */

static S32 ts_start(MuxWriter *pWr) {
    TsPriv *pPriv = (TsPriv *)pWr->pPriv;
    pPriv->bStarted = MPP_TRUE;
    pPriv->bFirstAu = MPP_TRUE;
    pPriv->u8PatCc = 0;
    pPriv->u8PmtCc = 0;
    pPriv->u8VideoCc = 0;
    pPriv->u64LastPatPts = 0;
    return ERR_MUX_OK;
}

static S32 ts_write(MuxWriter *pWr, const U8 *pu8Data, U32 u32Size, BOOL bKeyFrame, U64 u64PtsUs) {
    TsPriv *pPriv = (TsPriv *)pWr->pPriv;
    U8 *p;
    U32 u32PesLen;
    U64 u64Pts90k = (u64PtsUs * 9ULL) / 100ULL; /* us -> 90kHz ticks */
    S32 ret;

    if (u32Size + 64 > pPriv->u32PesCap) {
        MUX_LOGW("ts AU too large (%u), dropped", u32Size);
        return ERR_MUX_OK;
    }

    /* Re-emit PAT/PMT before each key frame (and at start) so a player that
     * joins mid-stream, or a file truncated by power loss, stays decodable. */
    if (pPriv->bFirstAu || bKeyFrame ||
        (u64Pts90k - pPriv->u64LastPatPts) >= TS_PAT_PMT_INTERVAL_TICKS) {
        ret = ts_write_pat(pWr, &pPriv->u8PatCc);
        if (ret != ERR_MUX_OK) {
            return ret;
        }
        ret = ts_write_pmt(pWr, pPriv->u8StreamType, &pPriv->u8PmtCc);
        if (ret != ERR_MUX_OK) {
            return ret;
        }
        pPriv->u64LastPatPts = u64Pts90k;
    }
    pPriv->bFirstAu = MPP_FALSE;

    /* Assemble PES: prefix + stream_id + length + flags + PTS + ES data. */
    p = pPriv->pu8PesBuf;
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 0x01;
    *p++ = 0xE0; /* stream_id: video */
    /* PES_packet_length: 0 = unbounded (allowed for video on TS). */
    *p++ = 0x00;
    *p++ = 0x00;
    *p++ = 0x80; /* '10' marker, no scrambling */
    *p++ = 0x80; /* PTS_flag = 1 */
    *p++ = 0x05; /* PES_header_data_length */
    ts_put_pts(p, 0x20, u64Pts90k);
    p += 5;
    memcpy(p, pu8Data, u32Size);
    p += u32Size;
    u32PesLen = (U32)(p - pPriv->pu8PesBuf);

    return ts_write_pes(pWr, pPriv->pu8PesBuf, u32PesLen, bKeyFrame, u64Pts90k);
}

static S32 ts_finish(MuxWriter *pWr) {
    (VOID)pWr;
    /* TS has no trailer; every packet already written is self-contained. */
    return ERR_MUX_OK;
}



