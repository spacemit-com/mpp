/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    rtp_session.h
 * @Brief     :    RTP session management for RTSP.
 *------------------------------------------------------------------------------
 */

#ifndef RTP_SESSION_H
#define RTP_SESSION_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _RtpSession RtpSession;

/**
 * @brief  Create RTP session
 * @param  bTcpInterleaved  TRUE for RTP over TCP, FALSE for UDP
 * @param  s32TcpFd         TCP socket (only used if bTcpInterleaved)
 */
RtpSession *RtpSession_Create(BOOL bTcpInterleaved, S32 s32TcpFd);

/**
 * @brief  Destroy RTP session
 */
VOID RtpSession_Destroy(RtpSession *pSession);

/**
 * @brief  Set UDP ports (for UDP mode)
 * @param  u16RtpPort   Local RTP port
 * @param  u16RtcpPort  Local RTCP port
 */
S32 RtpSession_SetUdpPorts(RtpSession *pSession, U16 u16RtpPort, U16 u16RtcpPort);

/**
 * @brief  Receive one RTP packet
 * @param  pu8Buf       Buffer to receive data
 * @param  u32BufSize   Buffer size
 * @param  pu32Len      Actual received length
 * @param  u32TimeoutMs Timeout in milliseconds
 * @return 0 success, -1 timeout, error code on failure
 */
S32 RtpSession_RecvPacket(RtpSession *pSession, U8 *pu8Buf, U32 u32BufSize, U32 *pu32Len, U32 u32TimeoutMs);

/**
 * @brief  Get SSRC of the session
 */
U32 RtpSession_GetSsrc(RtpSession *pSession);

#ifdef __cplusplus
}
#endif

#endif /* __RTP_SESSION_H__ */
