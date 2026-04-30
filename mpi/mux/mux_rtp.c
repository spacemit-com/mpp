/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_rtp.c
 * @Date      :    2026-04-15
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    RTP packetizer for MUX RTSP server.
 *------------------------------------------------------------------------------
 */

#include "mux_rtsp_internal.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MUX_RTP_VERSION 2
#define MUX_NAL_TYPE_FU_A 28
#define MUX_HEVC_NAL_TYPE_AP 48
#define MUX_HEVC_NAL_TYPE_FU 49

static S32 mux_rtsp_send_all(S32 s32Fd, const U8 *pu8Data, U32 u32Len)
{
    U32 sent = 0;
    while (sent < u32Len) {
        ssize_t ret = send(s32Fd, pu8Data + sent, u32Len - sent, 0);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (ret == 0) {
            return -1;
        }
        sent += (U32)ret;
    }
    return 0;
}

static S32 mux_rtsp_send_rtp_raw(MuxRtspClient *pstClient, const U8 *pu8Pkt, U32 u32PktLen,
                                 BOOL bInterleaved, U8 u8Channel)
{
    if (bInterleaved) {
        U8 hdr[4];
        hdr[0] = '$';
        hdr[1] = u8Channel;
        hdr[2] = (U8)((u32PktLen >> 8) & 0xff);
        hdr[3] = (U8)(u32PktLen & 0xff);
        if (mux_rtsp_send_all(pstClient->s32RtspFd, hdr, sizeof(hdr)) != 0) {
            return -1;
        }
        if (mux_rtsp_send_all(pstClient->s32RtspFd, pu8Pkt, u32PktLen) != 0) {
            return -1;
        }
        return 0;
    }

    if (sendto(pstClient->s32RtpSock, pu8Pkt, u32PktLen, 0,
               (const struct sockaddr *)&pstClient->stClientRtpAddr,
               sizeof(pstClient->stClientRtpAddr)) < 0) {
        return -1;
    }
    return 0;
}

static U32 mux_rtp_build_header(U8 *pu8Hdr, U16 u16Seq, U32 u32Ts, U32 u32Ssrc, BOOL bMarker, U8 u8Pt)
{
    pu8Hdr[0] = (MUX_RTP_VERSION << 6);
    pu8Hdr[1] = (bMarker ? 0x80 : 0x00) | (u8Pt & 0x7f);
    pu8Hdr[2] = (U8)(u16Seq >> 8);
    pu8Hdr[3] = (U8)(u16Seq & 0xff);
    pu8Hdr[4] = (U8)(u32Ts >> 24);
    pu8Hdr[5] = (U8)(u32Ts >> 16);
    pu8Hdr[6] = (U8)(u32Ts >> 8);
    pu8Hdr[7] = (U8)(u32Ts & 0xff);
    pu8Hdr[8] = (U8)(u32Ssrc >> 24);
    pu8Hdr[9] = (U8)(u32Ssrc >> 16);
    pu8Hdr[10] = (U8)(u32Ssrc >> 8);
    pu8Hdr[11] = (U8)(u32Ssrc & 0xff);
    return 12;
}

static const U8 *mux_find_start_code(const U8 *pu8Data, U32 u32Size, U32 *pu32Prefix)
{
    if (!pu8Data || u32Size < 4) {
        return NULL;
    }

    for (U32 i = 0; i + 3 < u32Size; ++i) {
        if (pu8Data[i] == 0 && pu8Data[i + 1] == 0 && pu8Data[i + 2] == 1) {
            *pu32Prefix = 3;
            return pu8Data + i;
        }
        if (i + 4 < u32Size && pu8Data[i] == 0 && pu8Data[i + 1] == 0 && pu8Data[i + 2] == 0 && pu8Data[i + 3] == 1) {
            *pu32Prefix = 4;
            return pu8Data + i;
        }
    }
    return NULL;
}

static S32 mux_rtp_send_h264_nalu(MuxRtspServer *pstServer, MuxRtspClient *pstClient,
                                  const U8 *pu8Nalu, U32 u32NaluLen, U32 u32Ts, BOOL bMarker)
{
    U8 au8Pkt[1600];
    U32 hdrLen;

    if (u32NaluLen + 12 <= sizeof(au8Pkt) && u32NaluLen <= MUX_RTP_MAX_PAYLOAD) {
        hdrLen = mux_rtp_build_header(au8Pkt, pstServer->u16Seq++, u32Ts, pstServer->u32Ssrc, bMarker, 96);
        memcpy(au8Pkt + hdrLen, pu8Nalu, u32NaluLen);
        return mux_rtsp_send_rtp_raw(pstClient, au8Pkt, hdrLen + u32NaluLen,
                                     pstClient->bInterleaved, pstClient->u8RtpChannel);
    }

    {
        U8 fuIndicator = (pu8Nalu[0] & 0xe0) | MUX_NAL_TYPE_FU_A;
        U8 fuHeaderBase = pu8Nalu[0] & 0x1f;
        U32 offset = 1;
        BOOL bStart = MPP_TRUE;

        while (offset < u32NaluLen) {
            U32 chunk = u32NaluLen - offset;
            BOOL bEnd;
            if (chunk > (MUX_RTP_MAX_PAYLOAD - 2)) {
                chunk = MUX_RTP_MAX_PAYLOAD - 2;
            }
            bEnd = ((offset + chunk) >= u32NaluLen) ? MPP_TRUE : MPP_FALSE;

            hdrLen = mux_rtp_build_header(au8Pkt, pstServer->u16Seq++, u32Ts, pstServer->u32Ssrc, bEnd ? bMarker : MPP_FALSE, 96);
            au8Pkt[hdrLen + 0] = fuIndicator;
            au8Pkt[hdrLen + 1] = fuHeaderBase | (bStart ? 0x80 : 0x00) | (bEnd ? 0x40 : 0x00);
            memcpy(au8Pkt + hdrLen + 2, pu8Nalu + offset, chunk);

            if (mux_rtsp_send_rtp_raw(pstClient, au8Pkt, hdrLen + 2 + chunk,
                                      pstClient->bInterleaved, pstClient->u8RtpChannel) != 0) {
                return -1;
            }
            bStart = MPP_FALSE;
            offset += chunk;
        }
    }

    return 0;
}

static S32 mux_rtp_send_h265_nalu(MuxRtspServer *pstServer, MuxRtspClient *pstClient,
                                  const U8 *pu8Nalu, U32 u32NaluLen, U32 u32Ts, BOOL bMarker)
{
    U8 au8Pkt[1600];
    U32 hdrLen;

    if (u32NaluLen + 12 <= sizeof(au8Pkt) && u32NaluLen <= MUX_RTP_MAX_PAYLOAD) {
        hdrLen = mux_rtp_build_header(au8Pkt, pstServer->u16Seq++, u32Ts, pstServer->u32Ssrc, bMarker, 96);
        memcpy(au8Pkt + hdrLen, pu8Nalu, u32NaluLen);
        return mux_rtsp_send_rtp_raw(pstClient, au8Pkt, hdrLen + u32NaluLen,
                                     pstClient->bInterleaved, pstClient->u8RtpChannel);
    }

    {
        U8 nalHdr0 = pu8Nalu[0];
        U8 nalHdr1 = pu8Nalu[1];
        U8 fuIndicator0 = (nalHdr0 & 0x81) | (MUX_HEVC_NAL_TYPE_FU << 1);
        U8 fuIndicator1 = nalHdr1;
        U8 fuHeaderBase = (nalHdr0 >> 1) & 0x3f;
        U32 offset = 2;
        BOOL bStart = MPP_TRUE;

        while (offset < u32NaluLen) {
            U32 chunk = u32NaluLen - offset;
            BOOL bEnd;
            if (chunk > (MUX_RTP_MAX_PAYLOAD - 3)) {
                chunk = MUX_RTP_MAX_PAYLOAD - 3;
            }
            bEnd = ((offset + chunk) >= u32NaluLen) ? MPP_TRUE : MPP_FALSE;

            hdrLen = mux_rtp_build_header(au8Pkt, pstServer->u16Seq++, u32Ts, pstServer->u32Ssrc, bEnd ? bMarker : MPP_FALSE, 96);
            au8Pkt[hdrLen + 0] = fuIndicator0;
            au8Pkt[hdrLen + 1] = fuIndicator1;
            au8Pkt[hdrLen + 2] = fuHeaderBase | (bStart ? 0x80 : 0x00) | (bEnd ? 0x40 : 0x00);
            memcpy(au8Pkt + hdrLen + 3, pu8Nalu + offset, chunk);

            if (mux_rtsp_send_rtp_raw(pstClient, au8Pkt, hdrLen + 3 + chunk,
                                      pstClient->bInterleaved, pstClient->u8RtpChannel) != 0) {
                return -1;
            }
            bStart = MPP_FALSE;
            offset += chunk;
        }
    }

    return 0;
}

S32 mux_rtsp_send_h26x_annexb(MuxRtspServer *pstServer, MuxRtspClient *pstClient,
                              const MuxPacket *pstPkt)
{
    const U8 *cur = pstPkt->pu8Data;
    U32 left = pstPkt->u32Size;
    U32 rtpTs;

    if (!pstServer || !pstClient || !pstPkt || !pstPkt->pu8Data || pstPkt->u32Size == 0) {
        return -1;
    }

    rtpTs = (U32)((pstPkt->u64PTS * 90ULL) / 1000ULL);

    while (left > 4) {
        U32 prefix = 0;
        const U8 *sc = mux_find_start_code(cur, left, &prefix);
        const U8 *nextSc;
        U32 nextPrefix = 0;
        const U8 *nalu;
        U32 naluLen;
        U32 remainAfterSc;

        if (!sc) {
            break;
        }
        sc += prefix;
        remainAfterSc = (U32)(cur + left - sc);
        if (remainAfterSc == 0) {
            break;
        }

        nextSc = mux_find_start_code(sc, remainAfterSc, &nextPrefix);
        if (nextSc) {
            nalu = sc;
            naluLen = (U32)(nextSc - sc);
            cur = nextSc;
            left = (U32)(pstPkt->pu8Data + pstPkt->u32Size - cur);
        } else {
            nalu = sc;
            naluLen = remainAfterSc;
            left = 0;
        }

        if (naluLen == 0) {
            continue;
        }

        if (pstPkt->eCodecType == MUX_CODEC_H264) {
            if (mux_rtp_send_h264_nalu(pstServer, pstClient, nalu, naluLen, rtpTs, left == 0 ? MPP_TRUE : MPP_FALSE) != 0) {
                return -1;
            }
        } else if (pstPkt->eCodecType == MUX_CODEC_H265) {
            if (mux_rtp_send_h265_nalu(pstServer, pstClient, nalu, naluLen, rtpTs, left == 0 ? MPP_TRUE : MPP_FALSE) != 0) {
                return -1;
            }
        } else {
            return -1;
        }
    }

    return 0;
}

/*
 * Extract SPS/PPS/VPS from Annex-B bitstream and cache into MuxRtspServer.
 * Called on every keyframe so the server always has the latest parameter sets.
 */
VOID mux_rtsp_cache_param_sets(MuxRtspServer *pstServer, const MuxPacket *pstPkt)
{
    const U8 *cur;
    U32 left;

    if (!pstServer || !pstPkt || !pstPkt->pu8Data || pstPkt->u32Size == 0) {
        return;
    }

    cur = pstPkt->pu8Data;
    left = pstPkt->u32Size;

    while (left > 4) {
        U32 prefix = 0;
        const U8 *sc = mux_find_start_code(cur, left, &prefix);
        const U8 *nextSc;
        U32 nextPrefix = 0;
        const U8 *nalu;
        U32 naluLen;
        U32 remainAfterSc;

        if (!sc) {
            break;
        }
        sc += prefix;
        remainAfterSc = (U32)(cur + left - sc);
        if (remainAfterSc == 0) {
            break;
        }

        nextSc = mux_find_start_code(sc, remainAfterSc, &nextPrefix);
        if (nextSc) {
            nalu = sc;
            naluLen = (U32)(nextSc - sc);
            cur = nextSc;
            left = (U32)(pstPkt->pu8Data + pstPkt->u32Size - cur);
        } else {
            nalu = sc;
            naluLen = remainAfterSc;
            left = 0;
        }

        if (naluLen == 0 || naluLen > MUX_SPS_PPS_MAX_SIZE) {
            continue;
        }

        if (pstPkt->eCodecType == MUX_CODEC_H264) {
            U8 nalType = nalu[0] & 0x1f;
            if (nalType == 7) { /* SPS */
                memcpy(pstServer->au8Sps, nalu, naluLen);
                pstServer->u32SpsLen = naluLen;
            } else if (nalType == 8) { /* PPS */
                memcpy(pstServer->au8Pps, nalu, naluLen);
                pstServer->u32PpsLen = naluLen;
            }
        } else if (pstPkt->eCodecType == MUX_CODEC_H265) {
            U8 nalType = (nalu[0] >> 1) & 0x3f;
            if (nalType == 32) { /* VPS */
                memcpy(pstServer->au8Vps, nalu, naluLen);
                pstServer->u32VpsLen = naluLen;
            } else if (nalType == 33) { /* SPS */
                memcpy(pstServer->au8Sps, nalu, naluLen);
                pstServer->u32SpsLen = naluLen;
            } else if (nalType == 34) { /* PPS */
                memcpy(pstServer->au8Pps, nalu, naluLen);
                pstServer->u32PpsLen = naluLen;
            }
        }
    }
}
