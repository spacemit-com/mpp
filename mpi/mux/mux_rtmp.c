/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_rtmp.c
 * @Date      :    2026-06-11
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    RTMP publisher backend for MUX.
 *------------------------------------------------------------------------------
 */

#include "mux_rtmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "common/socket_utils.h"
#include "common/url_parser.h"
#include "mux_common.h"
#include "protocol/rtmp/amf0.h"
#include "protocol/rtmp/rtmp_handshake.h"

#define RTMP_DEFAULT_PORT 1935
#define RTMP_OUT_CHUNK_SIZE 4096
#define RTMP_CSID_CMD 3   /* command messages (AMF0) */
#define RTMP_CSID_VIDEO 6 /* video data */
#define RTMP_MSG_SET_CHUNK 0x01
#define RTMP_MSG_ABORT 0x02
#define RTMP_MSG_ACK 0x03
#define RTMP_MSG_WIN_ACK_SIZE 0x05
#define RTMP_MSG_SET_PEER_BW 0x06
#define RTMP_MSG_AMF0_CMD 0x14 /* 20 */
#define RTMP_MSG_VIDEO 0x09
#define RTMP_STREAM_ID 1
#define RTMP_SEND_TIMEOUT_MS 5000 /* cap blocking sends so a dead peer surfaces as an error */

#define FLV_CODEC_AVC 7
#define FLV_CODEC_HEVC 12 /* enhanced RTMP uses fourcc, but 12 widely accepted */

typedef struct _MuxRtmp {
    S32 s32Fd;
    MuxStreamAttr stStream;
    UrlInfo stUrl;
    CHAR szApp[128];
    CHAR szStream[256];
    CHAR szTcUrl[URL_PATH_LEN];
    BOOL bConnected;
    BOOL bSeqHdrSent;
    U32 u32TxnId;
    U64 u64BasePtsUs;
    BOOL bHaveBase;
    /* cached parameter sets for the sequence header */
    MuxParamSets stSets;
    U8 *pu8Scratch; /* assembly buffer for FLV tag body + AVCC conversion */
    U32 u32ScratchCap;
    /* Window Acknowledgement: server tells us how many bytes it expects us
     * to acknowledge after receiving. We also track our own TX bytes so we
     * can send Ack back to the server when its Window Ack Size requires it. */
    U32 u32WinAckSize;  /* server's Window Ack Size (0 = not set) */
    U64 u64BytesRecv;   /* cumulative bytes received from server (for ACK seqnum) */
    U64 u64BytesSent;   /* bytes sent since last ack (window sliding counter) */
} MuxRtmp;

/* ---- RTMP chunk writer (type 0 full header) ---- */

/* Send an Acknowledgement (msg type 3) to the server. Constructed inline
 * rather than via rtmp_send_chunks to avoid forward-reference and to keep
 * it minimal (fixed 16-byte packet: 12-byte chunk header + 4-byte body). */
static S32 rtmp_send_ack(MuxRtmp *pRtmp, U32 u32SeqNum) {
    U8 au8Pkt[16];
    /* chunk basic header: fmt=0, csid=2 */
    au8Pkt[0] = 0x02;
    /* timestamp = 0 */
    au8Pkt[1] = 0; au8Pkt[2] = 0; au8Pkt[3] = 0;
    /* message length = 4 */
    au8Pkt[4] = 0; au8Pkt[5] = 0; au8Pkt[6] = 4;
    /* message type = Acknowledgement (3) */
    au8Pkt[7] = RTMP_MSG_ACK;
    /* message stream id = 0 (little-endian) */
    au8Pkt[8] = 0; au8Pkt[9] = 0; au8Pkt[10] = 0; au8Pkt[11] = 0;
    /* body: sequence number (big-endian) */
    MuxPutBe32(au8Pkt + 12, u32SeqNum);
    if (Socket_SendAll(pRtmp->s32Fd, au8Pkt, 16) < 0) {
        return ERR_MUX_WRITE_FAIL;
    }
    return ERR_MUX_OK;
}
static S32 rtmp_send_chunks(MuxRtmp *pRtmp, U8 u8Csid, U32 u32Timestamp, U8 u8MsgType, U32 u32StreamId,
                            const U8 *pu8Body, U32 u32BodyLen) {
    U8 au8Hdr[16];
    U32 u32Off = 0;
    BOOL bFirst = MPP_TRUE;

    if (u32BodyLen == 0) {
        return ERR_MUX_OK;
    }

    while (u32Off < u32BodyLen) {
        U32 u32Remain = u32BodyLen - u32Off;
        U32 u32Chunk = (u32Remain < RTMP_OUT_CHUNK_SIZE) ? u32Remain : RTMP_OUT_CHUNK_SIZE;
        U32 u32HdrLen;

        if (bFirst) {
            /* fmt=0, full 11-byte message header */
            au8Hdr[0] = (U8)(0x00 | (u8Csid & 0x3F));
            au8Hdr[1] = (U8)((u32Timestamp >> 16) & 0xFF);
            au8Hdr[2] = (U8)((u32Timestamp >> 8) & 0xFF);
            au8Hdr[3] = (U8)(u32Timestamp & 0xFF);
            au8Hdr[4] = (U8)((u32BodyLen >> 16) & 0xFF);
            au8Hdr[5] = (U8)((u32BodyLen >> 8) & 0xFF);
            au8Hdr[6] = (U8)(u32BodyLen & 0xFF);
            au8Hdr[7] = u8MsgType;
            /* message stream id is little-endian */
            au8Hdr[8] = (U8)(u32StreamId & 0xFF);
            au8Hdr[9] = (U8)((u32StreamId >> 8) & 0xFF);
            au8Hdr[10] = (U8)((u32StreamId >> 16) & 0xFF);
            au8Hdr[11] = (U8)((u32StreamId >> 24) & 0xFF);
            u32HdrLen = 12;
            bFirst = MPP_FALSE;
        } else {
            /* fmt=3, no header, just continuation */
            au8Hdr[0] = (U8)(0xC0 | (u8Csid & 0x3F));
            u32HdrLen = 1;
        }

        if (Socket_SendAll(pRtmp->s32Fd, au8Hdr, u32HdrLen) < 0) {
            return ERR_MUX_WRITE_FAIL;
        }
        if (u32Chunk > 0 && Socket_SendAll(pRtmp->s32Fd, pu8Body + u32Off, u32Chunk) < 0) {
            return ERR_MUX_WRITE_FAIL;
        }
        u32Off += u32Chunk;
        pRtmp->u64BytesSent += u32HdrLen + u32Chunk;
    }

    /* Window Acknowledgement (Adobe RTMP spec §5.4.3): the ACK sequence
     * number must be cumulative bytes RECEIVED from the server.
     *
     * The full implementation covers two accumulation points:
     *   1. rtmp_drain() during handshake — blocking recv accumulates
     *      u64BytesRecv and parses control messages (line ~346).
     *   2. Here in rtmp_send_chunks() — non-blocking recv picks up any
     *      server control messages that arrived during media streaming,
     *      accumulates u64BytesRecv, and parses Window Ack Size updates.
     * The while-loop at the end emits one ACK per window period. */
    if (pRtmp->u32WinAckSize > 0 &&
        pRtmp->u64BytesSent >= (U64)pRtmp->u32WinAckSize) {
        /* Non-blocking poll: timeout=0 returns immediately if no data.
         * A return value <= 0 means no data available (EAGAIN/EWOULDBLOCK)
         * or a transient condition — this is normal and not an error.  We
         * simply skip the control-message scan and proceed to send the ACK.
         * Only a positive return indicates actual bytes received. */
        U8 au8Ctrl[512];
        S32 s32Rx = Socket_RecvTimeout(pRtmp->s32Fd, au8Ctrl, sizeof(au8Ctrl), 0);
        if (s32Rx > 0) {
            S32 j;
            pRtmp->u64BytesRecv += (U64)s32Rx;
            /* Scan for Window Ack Size / Set Peer BW control messages.
             * Each RTMP chunk on csid 2 with fmt=0 is exactly 12-byte
             * header + 4-byte payload = 16 bytes.  We use message-length
             * driven parsing: advance by the full message size (16) on
             * match, or by 1 byte on mismatch, so no message boundary
             * is ever skipped. */
            for (j = 0; j + 16 <= s32Rx; ) {
                if (au8Ctrl[j] != 0x02) {
                    j++;
                    continue;
                }
                if (au8Ctrl[j + 1] != 0 || au8Ctrl[j + 2] != 0 ||
                    au8Ctrl[j + 3] != 0) {
                    j++;
                    continue;
                }
                if (au8Ctrl[j + 4] != 0 || au8Ctrl[j + 5] != 0 ||
                    au8Ctrl[j + 6] != 4) {
                    j++;
                    continue;
                }
                if (au8Ctrl[j + 7] != RTMP_MSG_WIN_ACK_SIZE &&
                    au8Ctrl[j + 7] != RTMP_MSG_SET_PEER_BW) {
                    j++;
                    continue;
                }
                if (au8Ctrl[j + 8] != 0 || au8Ctrl[j + 9] != 0 ||
                    au8Ctrl[j + 10] != 0 || au8Ctrl[j + 11] != 0) {
                    j++;
                    continue;
                }
                {
                    U32 u32Val = ((U32)au8Ctrl[j + 12] << 24) |
                        ((U32)au8Ctrl[j + 13] << 16) |
                        ((U32)au8Ctrl[j + 14] << 8) |
                        (U32)au8Ctrl[j + 15];
                    if (u32Val > 0) {
                        pRtmp->u32WinAckSize = u32Val;
                    }
                }
                /* Advance past the full 16-byte control message. */
                j += 16;
            }
        }
        /* Loop to emit one ACK per window period.  If a single send
         * exceeds u32WinAckSize (e.g. a large I-frame > 2.5 MB), the RTMP
         * spec requires one Acknowledgement per window, not just one for
         * the entire burst.  The loop drains the counter one window at a
         * time until the remainder is below the threshold. */
        while (pRtmp->u64BytesSent >= (U64)pRtmp->u32WinAckSize) {
            rtmp_send_ack(pRtmp, (U32)pRtmp->u64BytesRecv);
            /* Count the 16-byte ACK packet itself as sent bytes. */
            pRtmp->u64BytesSent += 16;
            pRtmp->u64BytesSent -= (U64)pRtmp->u32WinAckSize;
        }
    }
    return ERR_MUX_OK;
}

/* Set outgoing chunk size (control message on chunk stream 2). */
static S32 rtmp_send_set_chunk_size(MuxRtmp *pRtmp, U32 u32Size) {
    U8 au8Body[4];
    MuxPutBe32(au8Body, u32Size);
    return rtmp_send_chunks(pRtmp, 2, 0, RTMP_MSG_SET_CHUNK, 0, au8Body, 4);
}

/* ---- handshake ---- */
static S32 rtmp_do_handshake(MuxRtmp *pRtmp) {
    RtmpHandshake stHs;
    U8 au8Buf[RTMP_HANDSHAKE_SIZE * 2 + 16];
    U8 au8S[RTMP_HANDSHAKE_SIZE * 2 + 1];
    S32 s32Len;
    U32 u32Consumed = 0;
    S32 ret;

    RtmpHandshake_Init(&stHs);
    s32Len = RtmpHandshake_CreateC0C1(&stHs, au8Buf, sizeof(au8Buf));
    if (s32Len <= 0 || Socket_SendAll(pRtmp->s32Fd, au8Buf, (U32)s32Len) < 0) {
        return ERR_MUX_WRITE_FAIL;
    }
    /* Expect S0+S1+S2 = 1 + 1536 + 1536. */
    if (Socket_RecvExact(pRtmp->s32Fd, au8S, 1 + RTMP_HANDSHAKE_SIZE * 2, 5000) != 0) {
        return ERR_MUX_WRITE_FAIL;
    }
    ret = RtmpHandshake_ProcessS0S1S2(&stHs, au8S, 1 + RTMP_HANDSHAKE_SIZE * 2, &u32Consumed);
    if (ret != 0) {
        return ERR_MUX_WRITE_FAIL;
    }
    s32Len = RtmpHandshake_CreateC2(&stHs, au8Buf, sizeof(au8Buf));
    if (s32Len <= 0 || Socket_SendAll(pRtmp->s32Fd, au8Buf, (U32)s32Len) < 0) {
        return ERR_MUX_WRITE_FAIL;
    }
    return ERR_MUX_OK;
}

/* ---- AMF0 command sequence ---- */
static S32 rtmp_send_connect(MuxRtmp *pRtmp) {
    U8 au8Body[1024];
    U32 u32Pos = 0;
    S32 n;
    Amf0Value stVal;

    n = Amf0_WriteString(au8Body + u32Pos, sizeof(au8Body) - u32Pos, "connect");
    u32Pos += (U32)n;
    n = Amf0_WriteNumber(au8Body + u32Pos, sizeof(au8Body) - u32Pos, (double)(++pRtmp->u32TxnId));
    u32Pos += (U32)n;
    /* command object */
    n = Amf0_WriteObjectStart(au8Body + u32Pos, sizeof(au8Body) - u32Pos);
    u32Pos += (U32)n;
    stVal.u8Type = AMF0_TYPE_STRING;
    stVal.uData.stString.pStr = pRtmp->szApp;
    stVal.uData.stString.u32Len = (U32)strlen(pRtmp->szApp);
    n = Amf0_WriteProperty(au8Body + u32Pos, sizeof(au8Body) - u32Pos, "app", &stVal);
    u32Pos += (U32)n;
    stVal.uData.stString.pStr = pRtmp->szTcUrl;
    stVal.uData.stString.u32Len = (U32)strlen(pRtmp->szTcUrl);
    n = Amf0_WriteProperty(au8Body + u32Pos, sizeof(au8Body) - u32Pos, "tcUrl", &stVal);
    u32Pos += (U32)n;
    stVal.u8Type = AMF0_TYPE_STRING;
    stVal.uData.stString.pStr = (char *)"FMLE/3.0 (compatible; mpp-mux)";
    stVal.uData.stString.u32Len = (U32)strlen(stVal.uData.stString.pStr);
    n = Amf0_WriteProperty(au8Body + u32Pos, sizeof(au8Body) - u32Pos, "flashVer", &stVal);
    u32Pos += (U32)n;
    n = Amf0_WriteObjectEnd(au8Body + u32Pos, sizeof(au8Body) - u32Pos);
    u32Pos += (U32)n;

    return rtmp_send_chunks(pRtmp, RTMP_CSID_CMD, 0, RTMP_MSG_AMF0_CMD, 0, au8Body, u32Pos);
}

static S32 rtmp_send_create_stream(MuxRtmp *pRtmp) {
    U8 au8Body[256];
    U32 u32Pos = 0;
    S32 n;

    n = Amf0_WriteString(au8Body + u32Pos, sizeof(au8Body) - u32Pos, "createStream");
    u32Pos += (U32)n;
    n = Amf0_WriteNumber(au8Body + u32Pos, sizeof(au8Body) - u32Pos, (double)(++pRtmp->u32TxnId));
    u32Pos += (U32)n;
    n = Amf0_WriteNull(au8Body + u32Pos, sizeof(au8Body) - u32Pos);
    u32Pos += (U32)n;
    return rtmp_send_chunks(pRtmp, RTMP_CSID_CMD, 0, RTMP_MSG_AMF0_CMD, 0, au8Body, u32Pos);
}

static S32 rtmp_send_publish(MuxRtmp *pRtmp) {
    U8 au8Body[512];
    U32 u32Pos = 0;
    S32 n;

    n = Amf0_WriteString(au8Body + u32Pos, sizeof(au8Body) - u32Pos, "publish");
    u32Pos += (U32)n;
    n = Amf0_WriteNumber(au8Body + u32Pos, sizeof(au8Body) - u32Pos, (double)(++pRtmp->u32TxnId));
    u32Pos += (U32)n;
    n = Amf0_WriteNull(au8Body + u32Pos, sizeof(au8Body) - u32Pos);
    u32Pos += (U32)n;
    n = Amf0_WriteString(au8Body + u32Pos, sizeof(au8Body) - u32Pos, pRtmp->szStream);
    u32Pos += (U32)n;
    n = Amf0_WriteString(au8Body + u32Pos, sizeof(au8Body) - u32Pos, "live");
    u32Pos += (U32)n;
    /* publish is sent on the message stream id obtained from createStream. */
    return rtmp_send_chunks(pRtmp, RTMP_CSID_CMD, 0, RTMP_MSG_AMF0_CMD, RTMP_STREAM_ID, au8Body, u32Pos);
}

/* Read whatever the server sends back (results / window ack) within the
 * timeout. We don't fully parse the AMF responses, but the returned byte
 * count lets callers distinguish "server acknowledged" from "silence", so a
 * publish that the server never confirmed is at least observable rather than
 * being reported as connected. Returns bytes read (>=0) or a negative error.
 *
 * Additionally, scan the received data for Window Acknowledgement Size (type
 * 5) and Set Peer Bandwidth (type 6) control messages — both are 4-byte
 * payloads on chunk stream 2. When detected, store the window size and send
 * an Acknowledgement back so strict servers (e.g. nginx-rtmp) don't stall. */
static S32 rtmp_drain(MuxRtmp *pRtmp, U32 u32TimeoutMs) {
    U8 au8Tmp[16384];
    S32 n = Socket_RecvTimeout(pRtmp->s32Fd, au8Tmp, sizeof(au8Tmp), u32TimeoutMs);
    /* Negative => poll/recv error: report so the caller can abort.
     * A return of 0 means the poll timed out with no data available — this is
     * normal during idle periods and does NOT indicate a broken connection.
     * Simply return 0 so the caller can continue. */
    if (n < 0) {
        return -1;
    }
    if (n == 0) {
        return 0;
    }
    /* Track cumulative received bytes for Window Acknowledgement (RTMP spec
     * §5.4.3: the sequence number in Ack is the total bytes received). */
    pRtmp->u64BytesRecv += (U64)n;
    /* Lightweight scan for control messages (chunk stream 2, fmt 0).
     *
     * IMPORTANT: This function is ONLY called during the RTMP handshake
     * phase (after connect / createStream / publish) where no media data
     * flows.  The received buffer therefore contains only protocol-level
     * control messages and AMF responses, never raw video payload.
     *
     * A type-0 chunk on csid 2 has basic header 0x02 (fmt=0, csid=2),
     * followed by 11 bytes of message header: timestamp(3) + msg_length(3)
     * + msg_type(1) + stream_id(4). Body is 4 bytes for Win Ack Size and
     * Set Peer BW. We validate ALL of the following to virtually eliminate
     * false positives:
     *   1. basic header == 0x02 (fmt=0, csid=2)
     *   2. timestamp == 0 (control msgs during handshake always have ts=0)
     *   3. message length == 4 (bytes [4..6] == 0x00 0x00 0x04)
     *   4. msg_type is RTMP_MSG_WIN_ACK_SIZE(5) or RTMP_MSG_SET_PEER_BW(6)
     *   5. stream_id == 0 (protocol control always on stream 0)
     * After a successful match, advance past the full 16-byte chunk
     * (1 basic + 11 header + 4 body) to avoid re-scanning the same bytes
     * and to align subsequent iterations to real chunk boundaries.
     *
     * Design note on cross-segment messages: control messages (Win Ack Size,
     * Set Peer BW) are exactly 16 bytes and always fit within a single TCP
     * segment during the handshake phase (typical MTU >= 1460 bytes).  If a
     * future server splits such a message across segments, it would not match
     * the validation constraints and would be silently ignored — the default
     * window size (2.5 MB) remains in effect, which is safe for streaming. */
    {
        /* Scan for Window Ack Size / Set Peer BW control messages using
         * message-length-driven parsing.  Each control chunk on csid 2
         * with fmt=0 is exactly 12-byte header + 4-byte payload = 16
         * bytes.  On match advance by 16; on mismatch advance by 1. */
        S32 i;
        for (i = 0; i + 16 <= n; ) {
            U8 u8MsgType;
            U32 u32Val;
            if (au8Tmp[i] != 0x02) {
                i++;
                continue;
            }
            /* Control messages during the handshake have timestamp 0
             * (big-endian 24-bit at offset 1..3).  Reject non-zero to
             * eliminate coincidental 0x02 bytes in AMF payload. */
            if (au8Tmp[i + 1] != 0x00 || au8Tmp[i + 2] != 0x00 ||
                au8Tmp[i + 3] != 0x00) {
                i++;
                continue;
            }
            /* Validate message length == 4 (big-endian 24-bit at offset 4). */
            if (au8Tmp[i + 4] != 0x00 || au8Tmp[i + 5] != 0x00 ||
                au8Tmp[i + 6] != 0x04) {
                i++;
                continue;
            }
            u8MsgType = au8Tmp[i + 7];
            if (u8MsgType != RTMP_MSG_WIN_ACK_SIZE &&
                u8MsgType != RTMP_MSG_SET_PEER_BW) {
                i++;
                continue;
            }
            /* Control messages use stream_id 0 (little-endian 32-bit at
             * offset 8..11).  Reject matches where stream_id != 0 to
             * eliminate false positives. */
            if (au8Tmp[i + 8] != 0x00 || au8Tmp[i + 9] != 0x00 ||
                au8Tmp[i + 10] != 0x00 || au8Tmp[i + 11] != 0x00) {
                i++;
                continue;
            }
            u32Val = ((U32)au8Tmp[i + 12] << 24) |
                    ((U32)au8Tmp[i + 13] << 16) |
                    ((U32)au8Tmp[i + 14] << 8) |
                    (U32)au8Tmp[i + 15];
            if (u32Val > 0) {
                pRtmp->u32WinAckSize = u32Val;
                /* Do NOT send Ack here.  The ack logic is unified in
                 * rtmp_send_chunks which tracks cumulative TX bytes and
                 * sends Ack when the threshold is exceeded.  Sending ack
                 * from both paths would produce duplicate/out-of-order
                 * acknowledgements that confuse strict servers. */
            }
            /* Advance past the full 16-byte control message. */
            i += 16;
        }
    }
    return n;
}

/* Find needle within the first u32Len bytes of haystack (data may contain
 * NULs, so plain strstr is unsafe). Returns MPP_TRUE if present. */
static BOOL rtmp_buf_contains(const U8 *pu8Buf, U32 u32Len, const CHAR *pszNeedle) {
    U32 u32NeedleLen = (U32)strlen(pszNeedle);
    U32 i;
    if (u32NeedleLen == 0 || u32Len < u32NeedleLen) {
        return MPP_FALSE;
    }
    for (i = 0; i + u32NeedleLen <= u32Len; ++i) {
        if (memcmp(pu8Buf + i, pszNeedle, u32NeedleLen) == 0) {
            return MPP_TRUE;
        }
    }
    return MPP_FALSE;
}

/* Wait for the server's response to publish and confirm it carries
 * onStatus(... "NetStream.Publish.Start"). The control responses to connect /
 * createStream may still be queued, so accumulate up to a few reads and scan
 * the raw bytes for the AMF0 status-code strings. We don't decode the full
 * chunk stream, but matching the level/code strings is enough to tell a real
 * publish acceptance from a rejection (e.g. NetStream.Publish.BadName) or
 * silence, which the previous "any bytes == success" check could not.
 * Returns ERR_MUX_OK on confirmed publish, ERR_MUX_OPEN_FAIL otherwise. */
static S32 rtmp_wait_publish_ack(MuxRtmp *pRtmp) {
    U8 au8Buf[16384];
    U32 u32Len = 0;
    S32 s32Try;

    for (s32Try = 0; s32Try < 10 && u32Len < sizeof(au8Buf); ++s32Try) {
        S32 n = Socket_RecvTimeout(pRtmp->s32Fd, au8Buf + u32Len, sizeof(au8Buf) - u32Len, 500);
        if (n > 0) {
            u32Len += (U32)n;
            if (rtmp_buf_contains(au8Buf, u32Len, "NetStream.Publish.Start")) {
                return ERR_MUX_OK;
            }
            /* Match only the specific AMF0 onStatus "code" strings a server
             * uses to reject a publish. The earlier bare "error" scan was too
             * broad: those five bytes can occur inside RTMP chunk headers or
             * window-ack control messages and would false-reject a healthy
             * stream. These dotted codes are long enough that a coincidental
             * raw-byte collision is implausible. Cross-read fragmentation is
             * already covered because we rescan the full accumulated buffer. */
            if (rtmp_buf_contains(au8Buf, u32Len, "NetStream.Publish.BadName") ||
                rtmp_buf_contains(au8Buf, u32Len, "NetStream.Publish.Denied") ||
                rtmp_buf_contains(au8Buf, u32Len, "NetConnection.Connect.Rejected") ||
                rtmp_buf_contains(au8Buf, u32Len, "NetConnection.Connect.Failed")) {
                MUX_LOGE("rtmp server rejected publish");
                return ERR_MUX_OPEN_FAIL;
            }
        } else if (n < 0) {
            break; /* socket error */
        }
    }
    MUX_LOGE("rtmp publish not acknowledged (recv=%u bytes)", u32Len);
    return ERR_MUX_OPEN_FAIL;
}

/* ---- FLV video tag bodies ----
 * FLV VIDEODATA: [FrameType(4)|CodecID(4)] [AVCPacketType(8)] [CTS(24)] [data]
 *   FrameType: 1=key, 2=inter; CodecID: 7=AVC, 12=HEVC
 *   AVCPacketType: 0=sequence header, 1=NALU
 * For the sequence header, data is the AVCDecoderConfigurationRecord /
 * HEVCDecoderConfigurationRecord. For NALUs, data is length-prefixed AVCC.
 */
static S32 rtmp_build_avcc_config(MuxRtmp *pRtmp, U8 *p, U32 u32Cap, U32 *pu32Len) {
    const MuxParamSets *pSets = &pRtmp->stSets;
    U32 u32Need = 6 + 2 + pSets->u32SpsLen + 1 + 2 + pSets->u32PpsLen;
    U32 u32Pos = 0;

    if (u32Need > u32Cap) {
        MUX_LOGE("avcc config overflow (need=%u cap=%u)", u32Need, u32Cap);
        return -1;
    }
    p[u32Pos++] = 1;
    p[u32Pos++] = pSets->u32SpsLen > 1 ? pSets->au8Sps[1] : 0x42;
    p[u32Pos++] = pSets->u32SpsLen > 2 ? pSets->au8Sps[2] : 0x00;
    p[u32Pos++] = pSets->u32SpsLen > 3 ? pSets->au8Sps[3] : 0x1E;
    p[u32Pos++] = 0xFF; /* lengthSizeMinusOne = 3 */
    p[u32Pos++] = 0xE1; /* numOfSPS = 1 */
    u32Pos += MuxPutBe16(p + u32Pos, (U16)pSets->u32SpsLen);
    memcpy(p + u32Pos, pSets->au8Sps, pSets->u32SpsLen);
    u32Pos += pSets->u32SpsLen;
    p[u32Pos++] = 1; /* numOfPPS */
    u32Pos += MuxPutBe16(p + u32Pos, (U16)pSets->u32PpsLen);
    memcpy(p + u32Pos, pSets->au8Pps, pSets->u32PpsLen);
    u32Pos += pSets->u32PpsLen;
    *pu32Len = u32Pos;
    return 0;
}

/* Build an HEVCDecoderConfigurationRecord (hvcC) carrying VPS/SPS/PPS. The
 * enhanced-RTMP HEVC sequence header expects exactly this record after the
 * FLV video tag header, so H265 must not reuse the H264 avcC layout. */
static S32 rtmp_build_hvcc_config(MuxRtmp *pRtmp, U8 *p, U32 u32Cap, U32 *pu32Len) {
    const MuxParamSets *pSets = &pRtmp->stSets;
    U8 au8Ptl[MUX_HEVC_PTL_LEN];
    BOOL bPtl;
    /* Fixed overhead: configVersion(1) + PTL(12) + fixed fields(9)
     *   + 3 arrays * (type(1) + count(2) + len(2)) = 15 -> total fixed ~37 */
    U32 u32Fixed = 1 + 12 + 9 + 3 * 5;
    U32 u32Need;
    U32 u32Pos = 0;

    /* VPS is mandatory for a valid hvcC record; reject early to avoid writing
     * a zero-length VPS array that confuses downstream decoders/servers. */
    if (pSets->u32VpsLen == 0 || pSets->u32SpsLen == 0 ||
        pSets->u32PpsLen == 0) {
        MUX_LOGE("hvcc: incomplete param sets (vps=%u sps=%u pps=%u)",
            pSets->u32VpsLen, pSets->u32SpsLen, pSets->u32PpsLen);
        return -1;
    }

    bPtl = (BOOL)(MuxHevcExtractPtl(pSets->au8Sps, pSets->u32SpsLen,
        au8Ptl) == 0);
    u32Need = u32Fixed + pSets->u32VpsLen + pSets->u32SpsLen +
        pSets->u32PpsLen;

    if (u32Need > u32Cap) {
        MUX_LOGE("hvcc config overflow (need=%u cap=%u)", u32Need, u32Cap);
        return -1;
    }

    p[u32Pos++] = 1;    /* configurationVersion */
    if (bPtl) {
        /* Write the full 12-byte PTL block in one shot, consistent with the
         * MP4 box_write_hvcc path that uses box_bytes(au8Ptl, PTL_LEN). */
        memcpy(p + u32Pos, au8Ptl, MUX_HEVC_PTL_LEN);
        u32Pos += MUX_HEVC_PTL_LEN;
    } else {
        p[u32Pos++] = 0x01;
        u32Pos += MuxPutBe32(p + u32Pos, 0x60000000);
        u32Pos += MuxPutBe32(p + u32Pos, 0x00000000);
        u32Pos += MuxPutBe16(p + u32Pos, 0x0000);
        p[u32Pos++] = 0x5A;
    }
    u32Pos += MuxPutBe16(p + u32Pos, 0xF000);
    p[u32Pos++] = 0xFC;
    p[u32Pos++] = 0xFD;
    p[u32Pos++] = 0xF8;
    p[u32Pos++] = 0xF8;
    u32Pos += MuxPutBe16(p + u32Pos, 0x0000);
    p[u32Pos++] = 0x0F;
    p[u32Pos++] = 3;
    /* VPS */
    p[u32Pos++] = 0xA0;
    u32Pos += MuxPutBe16(p + u32Pos, 1);
    u32Pos += MuxPutBe16(p + u32Pos, (U16)pSets->u32VpsLen);
    memcpy(p + u32Pos, pSets->au8Vps, pSets->u32VpsLen);
    u32Pos += pSets->u32VpsLen;
    /* SPS */
    p[u32Pos++] = 0xA1;
    u32Pos += MuxPutBe16(p + u32Pos, 1);
    u32Pos += MuxPutBe16(p + u32Pos, (U16)pSets->u32SpsLen);
    memcpy(p + u32Pos, pSets->au8Sps, pSets->u32SpsLen);
    u32Pos += pSets->u32SpsLen;
    /* PPS */
    p[u32Pos++] = 0xA2;
    u32Pos += MuxPutBe16(p + u32Pos, 1);
    u32Pos += MuxPutBe16(p + u32Pos, (U16)pSets->u32PpsLen);
    memcpy(p + u32Pos, pSets->au8Pps, pSets->u32PpsLen);
    u32Pos += pSets->u32PpsLen;
    *pu32Len = u32Pos;
    return 0;
}

/* Send the codec sequence header tag once, before the first NALU tag. */
static S32 rtmp_send_seq_header(MuxRtmp *pRtmp) {
    U8 *p = pRtmp->pu8Scratch;
    U32 u32Pos = 0;
    BOOL bHevc = (BOOL)(pRtmp->stStream.eCodecType == MUX_CODEC_H265);

    if (pRtmp->stSets.u32SpsLen == 0 || pRtmp->stSets.u32PpsLen == 0) {
        return ERR_MUX_NEED_KEYFRAME; /* parameter sets incomplete */
    }
    if (bHevc && pRtmp->stSets.u32VpsLen == 0) {
        return ERR_MUX_NEED_KEYFRAME; /* H265 needs VPS in the hvcC */
    }

    p[u32Pos++] = (U8)(0x10 | (bHevc ? FLV_CODEC_HEVC : FLV_CODEC_AVC)); /* key frame */
    p[u32Pos++] = 0x00;                                                  /* seq header */
    p[u32Pos++] = 0x00;                                                  /* CTS */
    p[u32Pos++] = 0x00;
    p[u32Pos++] = 0x00;
    /* H264 carries an avcC record, H265 a proper hvcC (VPS/SPS/PPS).
     * Both builders validate remaining capacity to prevent buffer overflow
     * from maliciously large or corrupt parameter sets. */
    {
        U32 u32CfgLen = 0;
        S32 s32Ret;
        if (bHevc) {
            s32Ret = rtmp_build_hvcc_config(pRtmp, p + u32Pos,
                pRtmp->u32ScratchCap - u32Pos, &u32CfgLen);
        } else {
            s32Ret = rtmp_build_avcc_config(pRtmp, p + u32Pos,
                pRtmp->u32ScratchCap - u32Pos, &u32CfgLen);
        }
        if (s32Ret != 0) {
            return ERR_MUX_WRITE_FAIL;
        }
        u32Pos += u32CfgLen;
    }

    if (rtmp_send_chunks(pRtmp, RTMP_CSID_VIDEO, 0, RTMP_MSG_VIDEO, RTMP_STREAM_ID, p, u32Pos) != ERR_MUX_OK) {
        return ERR_MUX_WRITE_FAIL;
    }
    pRtmp->bSeqHdrSent = MPP_TRUE;
    return ERR_MUX_OK;
}

MuxRtmp *MuxRtmp_Create(const CHAR *pszUrl, const MuxStreamAttr *pstStream) {
    MuxRtmp *pRtmp;
    const CHAR *pszSlash;
    U16 u16Port;

    if (!pszUrl || !pstStream) {
        return NULL;
    }
    pRtmp = (MuxRtmp *)calloc(1, sizeof(MuxRtmp));
    if (!pRtmp) {
        return NULL;
    }
    pRtmp->stStream = *pstStream;
    pRtmp->s32Fd = -1;
    pRtmp->u32ScratchCap = 1024 * 1024;
    pRtmp->pu8Scratch = (U8 *)malloc(pRtmp->u32ScratchCap);
    if (!pRtmp->pu8Scratch) {
        free(pRtmp);
        return NULL;
    }

    if (Url_Parse(pszUrl, &pRtmp->stUrl) != 0) {
        MUX_LOGE("rtmp url parse failed: %s", pszUrl);
        goto fail;
    }
    u16Port = pRtmp->stUrl.u16Port ? pRtmp->stUrl.u16Port : RTMP_DEFAULT_PORT;

    /* RTMP publish URLs use the form "/app/streamKey": only the first path
     * segment after the leading slash is treated as the app name, and the rest
     * becomes the stream key. Nested app paths are not supported here. */
    if (pRtmp->stUrl.szPath[0] != '/') {
        MUX_LOGE("rtmp url must be rtmp://host/app/streamKey: %s", pszUrl);
        goto fail;
    }
    pszSlash = strchr(pRtmp->stUrl.szPath + 1, '/');
    if (!pszSlash || pszSlash[1] == '\0') {
        MUX_LOGE("rtmp url must be rtmp://host/app/streamKey: %s", pszUrl);
        goto fail;
    }
    snprintf(pRtmp->szStream, sizeof(pRtmp->szStream), "%s", pszSlash + 1);
    {
        U32 u32AppLen = (U32)(pszSlash - (pRtmp->stUrl.szPath + 1));
        if (u32AppLen == 0) {
            MUX_LOGE("rtmp url has empty app: %s", pszUrl);
            goto fail;
        }
        if (u32AppLen >= sizeof(pRtmp->szApp)) {
            u32AppLen = sizeof(pRtmp->szApp) - 1;
        }
        memcpy(pRtmp->szApp, pRtmp->stUrl.szPath + 1, u32AppLen);
        pRtmp->szApp[u32AppLen] = '\0';
    }
    snprintf(pRtmp->szTcUrl, sizeof(pRtmp->szTcUrl), "rtmp://%s:%u/%s", pRtmp->stUrl.szHost, u16Port,
        pRtmp->szApp);

    pRtmp->s32Fd = Socket_TcpConnect(pRtmp->stUrl.szHost, u16Port, 5000);
    if (pRtmp->s32Fd < 0) {
        MUX_LOGE("rtmp connect %s:%u failed", pRtmp->stUrl.szHost, u16Port);
        goto fail;
    }
    /* Bound every send: without SO_SNDTIMEO a server that stops draining (or
     * dies without a FIN) would wedge Socket_SendAll in the kernel TCP
     * retransmit window for minutes, hanging the caller's media thread. A
     * finite timeout makes a stalled link surface as a write error instead. */
    Socket_SetSendTimeout(pRtmp->s32Fd, RTMP_SEND_TIMEOUT_MS);

    if (rtmp_do_handshake(pRtmp) != ERR_MUX_OK) {
        MUX_LOGE("rtmp handshake failed");
        goto fail;
    }
    if (rtmp_send_set_chunk_size(pRtmp, RTMP_OUT_CHUNK_SIZE) != ERR_MUX_OK) {
        MUX_LOGE("rtmp set chunk size failed");
        goto fail;
    }
    if (rtmp_send_connect(pRtmp) != ERR_MUX_OK) {
        goto fail;
    }
    /* A negative drain means the socket errored/closed (vs 0 = timeout with no
     * data yet). Bail early instead of pushing more commands into a dead
     * socket and only noticing at the publish-ack step. */
    if (rtmp_drain(pRtmp, 500) < 0) {
        MUX_LOGE("rtmp socket error after connect");
        goto fail;
    }
    if (rtmp_send_create_stream(pRtmp) != ERR_MUX_OK) {
        goto fail;
    }
    if (rtmp_drain(pRtmp, 500) < 0) {
        MUX_LOGE("rtmp socket error after createStream");
        goto fail;
    }
    if (rtmp_send_publish(pRtmp) != ERR_MUX_OK) {
        goto fail;
    }
    /* The server replies to publish with onStatus(NetStream.Publish.Start).
     * Confirm that status code before declaring the stream live; otherwise
     * media tags would be pushed into a stream the server never accepted
     * (rejected name, auth failure, or simply no response). */
    if (rtmp_wait_publish_ack(pRtmp) != ERR_MUX_OK) {
        goto fail;
    }
    pRtmp->bConnected = MPP_TRUE;
    MUX_LOGI("rtmp publish ready: %s/%s", pRtmp->szApp, pRtmp->szStream);
    return pRtmp;

fail:
    if (pRtmp->s32Fd >= 0) {
        Socket_Close(pRtmp->s32Fd);
    }
    free(pRtmp->pu8Scratch);
    free(pRtmp);
    return NULL;
}

S32 MuxRtmp_Write(MuxRtmp *pRtmp, const U8 *pu8Data, U32 u32Size, BOOL bKeyFrame, U64 u64PtsUs) {
    U8 *p;
    U32 u32Pos = 0;
    U32 u32AvccLen = 0;
    U32 u32Ts;
    BOOL bHevc;
    S32 ret;

    if (!pRtmp || !pRtmp->bConnected || !pu8Data || u32Size == 0) {
        return ERR_MUX_INVALID_ARG;
    }
    bHevc = (BOOL)(pRtmp->stStream.eCodecType == MUX_CODEC_H265);

    /* Refresh parameter sets and emit the sequence header on the first chance.
     * rtmp_send_seq_header returns ERR_MUX_NEED_KEYFRAME when parameter sets
     * are incomplete (waiting for SPS/PPS/VPS). In that case we silently drop
     * the current frame — the caller sees ERR_MUX_OK (no fatal error) but we
     * log once so the state is diagnosable. */
    MuxCollectParamSets(pu8Data, u32Size, pRtmp->stStream.eCodecType, &pRtmp->stSets);
    if (!pRtmp->bSeqHdrSent) {
        if (!bKeyFrame) {
            return ERR_MUX_OK;
        }
        ret = rtmp_send_seq_header(pRtmp);
        if (ret == ERR_MUX_NEED_KEYFRAME) {
            /* Parameter sets not yet complete; drop frame silently. */
            return ERR_MUX_OK;
        }
        if (ret != ERR_MUX_OK || !pRtmp->bSeqHdrSent) {
            return ret;
        }
    }

    if (!pRtmp->bHaveBase) {
        pRtmp->u64BasePtsUs = u64PtsUs;
        pRtmp->bHaveBase = MPP_TRUE;
    }
    u32Ts = (U32)((u64PtsUs - pRtmp->u64BasePtsUs) / 1000ULL); /* ms */

    p = pRtmp->pu8Scratch;
    p[u32Pos++] = (U8)((bKeyFrame ? 0x10 : 0x20) | (bHevc ? FLV_CODEC_HEVC : FLV_CODEC_AVC));
    p[u32Pos++] = 0x01; /* NALU */
    p[u32Pos++] = 0x00; /* CTS */
    p[u32Pos++] = 0x00;
    p[u32Pos++] = 0x00;

    /* Annex-B -> length-prefixed (AVCC/HVCC) after the 5-byte FLV video
     * header. Sequence headers already carry avcC/hvcC, so media tags must
     * contain only VCL NALs and must not duplicate VPS/SPS/PPS in-band. */
    ret = MuxAnnexBToAvccVcl(pu8Data, u32Size, pRtmp->stStream.eCodecType, p + u32Pos,
        pRtmp->u32ScratchCap - u32Pos, &u32AvccLen);
    if (ret != 0 || u32AvccLen == 0) {
        MUX_LOGW("rtmp annexb->avcc failed (size=%u)", u32Size);
        return ERR_MUX_OK;
    }
    u32Pos += u32AvccLen;

    return rtmp_send_chunks(pRtmp, RTMP_CSID_VIDEO, u32Ts, RTMP_MSG_VIDEO, RTMP_STREAM_ID, p, u32Pos);
}

VOID MuxRtmp_Destroy(MuxRtmp *pRtmp) {
    if (!pRtmp) {
        return;
    }
    if (pRtmp->s32Fd >= 0) {
        Socket_Close(pRtmp->s32Fd);
        pRtmp->s32Fd = -1;
    }
    if (pRtmp->pu8Scratch) {
        free(pRtmp->pu8Scratch);
        pRtmp->pu8Scratch = NULL;
    }
    free(pRtmp);
}


