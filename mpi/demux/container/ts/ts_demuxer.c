/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    ts_demuxer.c
 * @Brief     :    MPEG-TS container demuxer implementation.
 *------------------------------------------------------------------------------
 */

#include "ts_demuxer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TS_PACKET_SIZE 188
#define TS_SYNC_BYTE 0x47
#define TS_MAX_PID 8192

/* Stream types */
#define STREAM_TYPE_H264 0x1B
#define STREAM_TYPE_H265 0x24
#define STREAM_TYPE_AAC 0x0F

/* PES start code */
#define PES_START_CODE 0x000001

typedef struct _PidInfo {
    U16 u16Pid;
    U8 u8StreamType;
    U8 u8ContinuityCounter;

    /* PES assembly */
    U8 au8PesBuf[512 * 1024];
    U32 u32PesLen;
    BOOL bPesStarted;
    U64 u64Pts;
    U64 u64Dts;
} PidInfo;

struct _TsDemuxer {
    FILE *pFile;

    /* PAT/PMT info */
    U16 u16PmtPid;
    U16 u16VideoPid;
    U16 u16AudioPid;

    /* Stream info */
    DemuxCodecType eCodec;
    U32 u32Width;
    U32 u32Height;

    /* PID tracking */
    PidInfo stVideoPid;
    PidInfo stAudioPid;

    /* Output packet */
    U8 au8OutBuf[512 * 1024];
    U32 u32OutLen;
    BOOL bPacketReady;
    BOOL bKeyFrame;
    U64 u64OutPts;

    /* Feed mode buffer */
    U8 au8FeedBuf[TS_PACKET_SIZE * 100];
    U32 u32FeedLen;
};

static U64 parse_pts(const U8 *p) {
    U64 pts = 0;
    pts = ((U64)(p[0] & 0x0E)) << 29;
    pts |= ((U64)p[1]) << 22;
    pts |= ((U64)(p[2] & 0xFE)) << 14;
    pts |= ((U64)p[3]) << 7;
    pts |= ((U64)(p[4] & 0xFE)) >> 1;
    return pts;
}

static S32 parse_pat(TsDemuxer *pDemux, const U8 *pPayload, U32 u32Len) {
    if (u32Len < 8)
        return -1;

    U8 u8TableId = pPayload[0];
    if (u8TableId != 0x00)
        return -1;

    U16 u16SectionLen = ((pPayload[1] & 0x0F) << 8) | pPayload[2];

    /* Skip to program entries (after 8 bytes header) */
    const U8 *p = pPayload + 8;
    const U8 *pEnd = pPayload + 3 + u16SectionLen - 4; /* -4 for CRC */

    while (p + 4 <= pEnd) {
        U16 u16ProgramNum = (p[0] << 8) | p[1];
        U16 u16Pid = ((p[2] & 0x1F) << 8) | p[3];

        if (u16ProgramNum != 0) {
            pDemux->u16PmtPid = u16Pid;
            break;
        }
        p += 4;
    }

    return 0;
}

static S32 parse_pmt(TsDemuxer *pDemux, const U8 *pPayload, U32 u32Len) {
    if (u32Len < 12)
        return -1;

    U8 u8TableId = pPayload[0];
    if (u8TableId != 0x02)
        return -1;

    U16 u16SectionLen = ((pPayload[1] & 0x0F) << 8) | pPayload[2];
    U16 u16ProgramInfoLen = ((pPayload[10] & 0x0F) << 8) | pPayload[11];

    /* Skip to stream entries */
    const U8 *p = pPayload + 12 + u16ProgramInfoLen;
    const U8 *pEnd = pPayload + 3 + u16SectionLen - 4;

    while (p + 5 <= pEnd) {
        U8 u8StreamType = p[0];
        U16 u16EsPid = ((p[1] & 0x1F) << 8) | p[2];
        U16 u16EsInfoLen = ((p[3] & 0x0F) << 8) | p[4];

        if (u8StreamType == STREAM_TYPE_H264) {
            pDemux->u16VideoPid = u16EsPid;
            pDemux->eCodec = DEMUX_CODEC_H264;
            pDemux->stVideoPid.u16Pid = u16EsPid;
            pDemux->stVideoPid.u8StreamType = u8StreamType;
        } else if (u8StreamType == STREAM_TYPE_H265) {
            pDemux->u16VideoPid = u16EsPid;
            pDemux->eCodec = DEMUX_CODEC_H265;
            pDemux->stVideoPid.u16Pid = u16EsPid;
            pDemux->stVideoPid.u8StreamType = u8StreamType;
        } else if (u8StreamType == STREAM_TYPE_AAC) {
            pDemux->u16AudioPid = u16EsPid;
            pDemux->stAudioPid.u16Pid = u16EsPid;
            pDemux->stAudioPid.u8StreamType = u8StreamType;
        }

        p += 5 + u16EsInfoLen;
    }

    return 0;
}

static S32 parse_pes_header(const U8 *pPes, U32 u32Len, U64 *pu64Pts, U32 *pu32HdrLen) {
    if (u32Len < 9)
        return -1;

    /* Check start code */
    if (pPes[0] != 0x00 || pPes[1] != 0x00 || pPes[2] != 0x01) {
        return -1;
    }

    U8 u8PtsFlags = (pPes[7] >> 6) & 0x03;
    U8 u8HdrDataLen = pPes[8];

    *pu32HdrLen = 9 + u8HdrDataLen;

    if (u8PtsFlags >= 2 && u32Len >= 14) {
        *pu64Pts = parse_pts(&pPes[9]);
    } else {
        *pu64Pts = 0;
    }

    return 0;
}

/* Check if NAL unit is a keyframe (IDR slice) */
static BOOL is_keyframe_nal(U8 u8NalType) {
    /* H.264: NAL type 5 = IDR slice */
    return (u8NalType == 5);
}

/* Find next start code (0x000001 or 0x00000001) in buffer.
 * Returns offset of start code, or -1 if not found. */
static S32 find_start_code(const U8 *pData, U32 u32Len, U32 u32StartOff) {
    if (u32Len < 3)
        return -1;
    for (U32 i = u32StartOff; i + 2 < u32Len; i++) {
        if (pData[i] == 0x00 && pData[i + 1] == 0x00) {
            if (pData[i + 2] == 0x01) {
                return (S32)i; /* 3-byte start code */
            }
            if (i + 3 < u32Len && pData[i + 2] == 0x00 && pData[i + 3] == 0x01) {
                return (S32)i; /* 4-byte start code */
            }
        }
    }
    return -1;
}

/* Scan all NAL units in ES data, return TRUE if any is a keyframe (IDR).
 * Also normalizes the data to ensure 4-byte start codes (0x00000001) for VDEC. */
static BOOL scan_es_for_keyframe(const U8 *pData, U32 u32Len) {
    S32 s32Off = find_start_code(pData, u32Len, 0);
    while (s32Off >= 0 && (U32)s32Off < u32Len) {
        /* Skip start code */
        U32 u32NalOff = s32Off;
        if ((U32)(s32Off + 2) < u32Len && pData[s32Off + 2] == 0x01) {
            u32NalOff += 3;
        } else if ((U32)(s32Off + 3) < u32Len && pData[s32Off + 2] == 0x00 && pData[s32Off + 3] == 0x01) {
            u32NalOff += 4;
        } else {
            break;
        }

        if (u32NalOff >= u32Len)
            break;

        U8 u8NalType = pData[u32NalOff] & 0x1F;
        if (is_keyframe_nal(u8NalType)) {
            return MPP_TRUE;
        }

        /* Find next start code */
        s32Off = find_start_code(pData, u32Len, u32NalOff + 1);
    }
    return MPP_FALSE;
}

/* Flush a complete PES into output buffer with keyframe detection */
static S32 flush_pes_to_output(TsDemuxer *pDemux, PidInfo *pPid) {
    if (!pPid->bPesStarted || pPid->u32PesLen == 0)
        return -1;

    U64 u64Pts;
    U32 u32HdrLen;
    if (parse_pes_header(pPid->au8PesBuf, pPid->u32PesLen, &u64Pts, &u32HdrLen) != 0) {
        return -1;
    }
    if (u32HdrLen >= pPid->u32PesLen)
        return -1;

    const U8 *pEsData = pPid->au8PesBuf + u32HdrLen;
    U32 u32EsLen = pPid->u32PesLen - u32HdrLen;

    /* Verify ES starts with a NAL start code */
    S32 s32StartOff = find_start_code(pEsData, u32EsLen, 0);
    if (s32StartOff < 0) {
        /* No NAL found - drop this PES */
        return -1;
    }

    /* Trim leading garbage before first start code */
    pEsData += s32StartOff;
    u32EsLen -= s32StartOff;

    if (u32EsLen > sizeof(pDemux->au8OutBuf)) {
        u32EsLen = sizeof(pDemux->au8OutBuf);
    }

    /* Copy entire ES data (all NAL units) to output buffer.
     * The data is already in Annex-B format with start codes. */
    memcpy(pDemux->au8OutBuf, pEsData, u32EsLen);
    pDemux->u32OutLen = u32EsLen;
    pDemux->u64OutPts = u64Pts;
    pDemux->bKeyFrame = scan_es_for_keyframe(pEsData, u32EsLen);
    pDemux->bPacketReady = MPP_TRUE;

    return 0;
}

static S32 process_ts_packet(TsDemuxer *pDemux, const U8 *pPacket) {
    /* Parse TS header */
    if (pPacket[0] != TS_SYNC_BYTE)
        return -1;

    U8 u8Tei = (pPacket[1] >> 7) & 0x01;
    U8 u8Pusi = (pPacket[1] >> 6) & 0x01;
    U16 u16Pid = ((pPacket[1] & 0x1F) << 8) | pPacket[2];
    U8 u8Afc = (pPacket[3] >> 4) & 0x03;

    if (u8Tei)
        return -1; /* Transport error */

    /* Calculate payload offset */
    U32 u32PayloadOff = 4;
    if (u8Afc == 2 || u8Afc == 3) {
        U8 u8AfLen = pPacket[4];
        u32PayloadOff = 5 + u8AfLen;
    }

    /* No payload */
    if (u8Afc == 0 || u8Afc == 2)
        return 0;
    if (u32PayloadOff >= TS_PACKET_SIZE)
        return 0;

    const U8 *pPayload = pPacket + u32PayloadOff;
    U32 u32PayloadLen = TS_PACKET_SIZE - u32PayloadOff;

    /* Handle special PIDs */
    if (u16Pid == 0x0000) {
        /* PAT */
        if (u8Pusi && u32PayloadLen > 1) {
            U8 u8PointerField = pPayload[0];
            if (u8PointerField + 1U < u32PayloadLen) {
                parse_pat(pDemux, pPayload + 1 + u8PointerField, u32PayloadLen - 1 - u8PointerField);
            }
        }
    } else if (pDemux->u16PmtPid != 0 && u16Pid == pDemux->u16PmtPid) {
        /* PMT */
        if (u8Pusi && u32PayloadLen > 1) {
            U8 u8PointerField = pPayload[0];
            if (u8PointerField + 1U < u32PayloadLen) {
                parse_pmt(pDemux, pPayload + 1 + u8PointerField, u32PayloadLen - 1 - u8PointerField);
            }
        }
    } else if (pDemux->u16VideoPid != 0 && u16Pid == pDemux->u16VideoPid) {
        /* Video PES */
        PidInfo *pPid = &pDemux->stVideoPid;

        if (u8Pusi) {
            /* Output previous PES if complete */
            flush_pes_to_output(pDemux, pPid);

            /* Start new PES */
            pPid->bPesStarted = MPP_TRUE;
            pPid->u32PesLen = 0;
        }

        if (pPid->bPesStarted) {
            if (pPid->u32PesLen + u32PayloadLen <= sizeof(pPid->au8PesBuf)) {
                memcpy(pPid->au8PesBuf + pPid->u32PesLen, pPayload, u32PayloadLen);
                pPid->u32PesLen += u32PayloadLen;
            } else {
                /* Buffer overflow - PES too large, reset */
                pPid->bPesStarted = MPP_FALSE;
                pPid->u32PesLen = 0;
            }
        }
    }

    return 0;
}

TsDemuxer *TsDemuxer_Create(VOID) { return (TsDemuxer *)calloc(1, sizeof(TsDemuxer)); }

VOID TsDemuxer_Destroy(TsDemuxer *pDemux) {
    if (pDemux) {
        TsDemuxer_Close(pDemux);
        free(pDemux);
    }
}

S32 TsDemuxer_Open(TsDemuxer *pDemux, const CHAR *pszPath) {
    if (!pDemux || !pszPath)
        return ERR_DEMUX_NULL_PTR;

    pDemux->pFile = fopen(pszPath, "rb");
    if (!pDemux->pFile) {
        return ERR_DEMUX_OPEN_FAIL;
    }

    return 0;
}

VOID TsDemuxer_Close(TsDemuxer *pDemux) {
    if (pDemux && pDemux->pFile) {
        fclose(pDemux->pFile);
        pDemux->pFile = NULL;
    }
}

S32 TsDemuxer_GetStreamInfo(TsDemuxer *pDemux, DemuxStreamInfo *pstInfo) {
    if (!pDemux || !pstInfo)
        return ERR_DEMUX_NULL_PTR;

    pstInfo->eCodecType = pDemux->eCodec;
    pstInfo->u32Width = pDemux->u32Width;
    pstInfo->u32Height = pDemux->u32Height;
    pstInfo->u32Fps = 25;

    return 0;
}

S32 TsDemuxer_ReadPacket(TsDemuxer *pDemux, DemuxPacket *pstPkt) {
    if (!pDemux || !pstPkt)
        return ERR_DEMUX_NULL_PTR;

    pDemux->bPacketReady = MPP_FALSE;

    while (!pDemux->bPacketReady) {
        U8 au8TsPacket[TS_PACKET_SIZE];

        if (pDemux->pFile) {
            if (fread(au8TsPacket, 1, TS_PACKET_SIZE, pDemux->pFile) != TS_PACKET_SIZE) {
                /* EOF: flush last buffered PES if any */
                PidInfo *pPid = &pDemux->stVideoPid;
                if (flush_pes_to_output(pDemux, pPid) == 0) {
                    pPid->bPesStarted = MPP_FALSE;
                    pPid->u32PesLen = 0;
                    pstPkt->pu8Data = pDemux->au8OutBuf;
                    pstPkt->u32Size = pDemux->u32OutLen;
                    pstPkt->bKeyFrame = pDemux->bKeyFrame;
                    pstPkt->eCodecType = pDemux->eCodec;
                    pstPkt->u64PTS = pDemux->u64OutPts * 100 / 9;
                    return 0;
                }
                return ERR_DEMUX_NO_STREAM; /* EOF */
            }
        } else {
            return ERR_DEMUX_NOT_STARTED;
        }

        process_ts_packet(pDemux, au8TsPacket);
    }

    pstPkt->pu8Data = pDemux->au8OutBuf;
    pstPkt->u32Size = pDemux->u32OutLen;
    pstPkt->bKeyFrame = pDemux->bKeyFrame;
    pstPkt->eCodecType = pDemux->eCodec;
    pstPkt->u64PTS = pDemux->u64OutPts * 100 / 9; /* 90kHz to us */

    return 0;
}

S32 TsDemuxer_Seek(TsDemuxer *pDemux, S64 s64PtsUs) {
    S64 bestPos = 0;
    U64 bestPtsUs = 0;
    U64 targetPtsUs;

    if (!pDemux || !pDemux->pFile)
        return ERR_DEMUX_NULL_PTR;

    if (s64PtsUs <= 0) {
        fseek(pDemux->pFile, 0, SEEK_SET);
        memset(&pDemux->stVideoPid, 0, sizeof(pDemux->stVideoPid));
        memset(&pDemux->stAudioPid, 0, sizeof(pDemux->stAudioPid));
        pDemux->bPacketReady = MPP_FALSE;
        pDemux->u32OutLen = 0;
        return 0;
    }

    targetPtsUs = (U64)s64PtsUs;

    /* MPEG-TS has no mandatory global index. Build a lightweight seek point by
     * scanning packets from the beginning and remembering the latest keyframe
     * PES whose PTS is <= target. This is robust for small/local files and keeps
     * decoder startup safe by seeking only to IDR-aligned PES boundaries. */
    fseek(pDemux->pFile, 0, SEEK_SET);
    memset(&pDemux->stVideoPid, 0, sizeof(pDemux->stVideoPid));
    memset(&pDemux->stAudioPid, 0, sizeof(pDemux->stAudioPid));
    pDemux->bPacketReady = MPP_FALSE;
    pDemux->u32OutLen = 0;

    while (1) {
        S64 packetPos = ftell(pDemux->pFile);
        DemuxPacket pkt;
        S32 ret;

        memset(&pkt, 0, sizeof(pkt));
        ret = TsDemuxer_ReadPacket(pDemux, &pkt);
        if (ret == ERR_DEMUX_NO_STREAM) {
            break;
        }
        if (ret != 0) {
            break;
        }

        if (pkt.bKeyFrame && pkt.u64PTS <= targetPtsUs) {
            bestPos = packetPos;
            bestPtsUs = pkt.u64PTS;
        }

        if (pkt.u64PTS >= targetPtsUs) {
            break;
        }
    }

    fseek(pDemux->pFile, bestPos, SEEK_SET);
    memset(&pDemux->stVideoPid, 0, sizeof(pDemux->stVideoPid));
    memset(&pDemux->stAudioPid, 0, sizeof(pDemux->stAudioPid));
    pDemux->bPacketReady = MPP_FALSE;
    pDemux->u32OutLen = 0;

    (void)bestPtsUs;
    return 0;
}

S32 TsDemuxer_FeedData(TsDemuxer *pDemux, const U8 *pu8Data, U32 u32Len) {
    if (!pDemux || !pu8Data)
        return ERR_DEMUX_NULL_PTR;

    /* Append to feed buffer */
    if (pDemux->u32FeedLen + u32Len > sizeof(pDemux->au8FeedBuf)) {
        /* Buffer full, discard old data */
        pDemux->u32FeedLen = 0;
    }

    memcpy(pDemux->au8FeedBuf + pDemux->u32FeedLen, pu8Data, u32Len);
    pDemux->u32FeedLen += u32Len;

    /* Process complete TS packets */
    while (pDemux->u32FeedLen >= TS_PACKET_SIZE) {
        /* Find sync byte */
        U32 u32SyncOff = 0;
        while (u32SyncOff < pDemux->u32FeedLen && pDemux->au8FeedBuf[u32SyncOff] != TS_SYNC_BYTE) {
            u32SyncOff++;
        }

        if (u32SyncOff > 0) {
            memmove(pDemux->au8FeedBuf, pDemux->au8FeedBuf + u32SyncOff, pDemux->u32FeedLen - u32SyncOff);
            pDemux->u32FeedLen -= u32SyncOff;
        }

        if (pDemux->u32FeedLen < TS_PACKET_SIZE)
            break;

        process_ts_packet(pDemux, pDemux->au8FeedBuf);

        memmove(pDemux->au8FeedBuf, pDemux->au8FeedBuf + TS_PACKET_SIZE, pDemux->u32FeedLen - TS_PACKET_SIZE);
        pDemux->u32FeedLen -= TS_PACKET_SIZE;
    }

    return 0;
}
