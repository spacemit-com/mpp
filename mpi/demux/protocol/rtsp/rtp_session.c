/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "rtp_session.h"

#include <stdlib.h>
#include <string.h>

#include "common/socket_utils.h"

struct _RtpSession {
    BOOL bTcpInterleaved;
    S32 s32TcpFd;
    S32 s32RtpUdpFd;
    S32 s32RtcpUdpFd;
    U32 u32Ssrc;
    U16 u16Seq;
};

RtpSession *RtpSession_Create(BOOL bTcpInterleaved, S32 s32TcpFd) {
    RtpSession *pSession = (RtpSession *)calloc(1, sizeof(RtpSession));
    if (!pSession)
        return NULL;

    pSession->bTcpInterleaved = bTcpInterleaved;
    pSession->s32TcpFd = s32TcpFd;
    pSession->s32RtpUdpFd = -1;
    pSession->s32RtcpUdpFd = -1;

    return pSession;
}

VOID RtpSession_Destroy(RtpSession *pSession) {
    if (!pSession)
        return;

    if (pSession->s32RtpUdpFd >= 0) {
        Socket_Close(pSession->s32RtpUdpFd);
    }
    if (pSession->s32RtcpUdpFd >= 0) {
        Socket_Close(pSession->s32RtcpUdpFd);
    }

    free(pSession);
}

S32 RtpSession_SetUdpPorts(RtpSession *pSession, U16 u16RtpPort, U16 u16RtcpPort) {
    if (!pSession || pSession->bTcpInterleaved)
        return -1;

    pSession->s32RtpUdpFd = Socket_UdpBind(u16RtpPort);
    if (pSession->s32RtpUdpFd < 0)
        return -1;

    pSession->s32RtcpUdpFd = Socket_UdpBind(u16RtcpPort);
    if (pSession->s32RtcpUdpFd < 0) {
        Socket_Close(pSession->s32RtpUdpFd);
        pSession->s32RtpUdpFd = -1;
        return -1;
    }

    return 0;
}

S32 RtpSession_RecvPacket(RtpSession *pSession, U8 *pu8Buf, U32 u32BufSize, U32 *pu32Len, U32 u32TimeoutMs) {
    if (!pSession || !pu8Buf || !pu32Len)
        return -1;

    *pu32Len = 0;

    if (pSession->bTcpInterleaved) {
        /* RTP over TCP (interleaved)
         * Format: $<channel><length_16bit><rtp_data>
         */
        U8 au8Hdr[4];
        S32 n = Socket_RecvExact(pSession->s32TcpFd, au8Hdr, 4, u32TimeoutMs);
        if (n != 0)
            return -1;

        if (au8Hdr[0] != '$') {
            /* Not RTP interleaved, might be RTSP response */
            return -1;
        }

        U16 u16Len = (au8Hdr[2] << 8) | au8Hdr[3];
        if (u16Len > u32BufSize)
            return -1;

        n = Socket_RecvExact(pSession->s32TcpFd, pu8Buf, u16Len, u32TimeoutMs);
        if (n != 0)
            return -1;

        *pu32Len = u16Len;
        return 0;
    } else {
        /* RTP over UDP */
        S32 n = Socket_RecvTimeout(pSession->s32RtpUdpFd, pu8Buf, u32BufSize, u32TimeoutMs);
        if (n <= 0)
            return -1;

        *pu32Len = (U32)n;
        return 0;
    }
}

U32 RtpSession_GetSsrc(RtpSession *pSession) { return pSession ? pSession->u32Ssrc : 0; }
