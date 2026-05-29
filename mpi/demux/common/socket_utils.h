/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    socket_utils.h
 * @Brief     :    Socket utility functions for network protocols.
 *------------------------------------------------------------------------------
 */

#ifndef SOCKET_UTILS_H
#define SOCKET_UTILS_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Create TCP socket and connect
 * @return Socket fd, -1 on failure
 */
S32 Socket_TcpConnect(const CHAR *pszHost, U16 u16Port, U32 u32TimeoutMs);

/**
 * @brief  Create UDP socket and bind to local port
 * @return Socket fd, -1 on failure
 */
S32 Socket_UdpBind(U16 u16LocalPort);

/**
 * @brief  Set socket non-blocking
 */
S32 Socket_SetNonBlocking(S32 s32Fd, BOOL bNonBlock);

/**
 * @brief  Set socket receive timeout
 */
S32 Socket_SetRecvTimeout(S32 s32Fd, U32 u32TimeoutMs);

/**
 * @brief  Set socket send timeout
 */
S32 Socket_SetSendTimeout(S32 s32Fd, U32 u32TimeoutMs);

/**
 * @brief  Set socket receive buffer size
 */
S32 Socket_SetRecvBufSize(S32 s32Fd, U32 u32Size);

/**
 * @brief  Send all data (blocking)
 * @return Number of bytes sent, -1 on error
 */
S32 Socket_SendAll(S32 s32Fd, const U8 *pu8Data, U32 u32Size);

/**
 * @brief  Receive data with timeout
 * @return Number of bytes received, 0 on timeout, -1 on error
 */
S32 Socket_RecvTimeout(S32 s32Fd, U8 *pu8Buf, U32 u32Size, U32 u32TimeoutMs);

/**
 * @brief  Read exact number of bytes
 * @return 0 on success, -1 on error
 */
S32 Socket_RecvExact(S32 s32Fd, U8 *pu8Buf, U32 u32Size, U32 u32TimeoutMs);

/**
 * @brief  Read line (CR-LF terminated)
 * @return Number of bytes read (including CR-LF), -1 on error
 */
S32 Socket_ReadLine(S32 s32Fd, CHAR *pszBuf, U32 u32MaxLen, U32 u32TimeoutMs);

/**
 * @brief  Close socket
 */
VOID Socket_Close(S32 s32Fd);

/**
 * @brief  Get local port of socket
 */
U16 Socket_GetLocalPort(S32 s32Fd);

/**
 * @brief  Resolve hostname to IP address
 * @return 0 on success, -1 on error
 */
S32 Socket_ResolveHost(const CHAR *pszHost, CHAR *pszIpOut, U32 u32IpBufLen);

#ifdef __cplusplus
}
#endif

#endif /* __SOCKET_UTILS_H__ */
