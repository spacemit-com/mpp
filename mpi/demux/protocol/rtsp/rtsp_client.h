/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    rtsp_client.h
 * @Brief     :    Lightweight RTSP client implementation.
 *------------------------------------------------------------------------------
 */

#ifndef RTSP_CLIENT_H
#define RTSP_CLIENT_H

#include "demux/demux_type.h"
#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _RtspClient RtspClient;

/**
 * @brief  Create RTSP client
 */
RtspClient *RtspClient_Create(VOID);

/**
 * @brief  Destroy RTSP client
 */
VOID RtspClient_Destroy(RtspClient *pClient);

/**
 * @brief  Connect to RTSP server
 * @param  pClient      Client handle
 * @param  pszUrl       RTSP URL
 * @param  bPreferTcp   Use RTP over TCP (interleaved)
 * @param  u32TimeoutMs Connection timeout
 * @return 0 success, error code on failure
 */
S32 RtspClient_Connect(RtspClient *pClient, const CHAR *pszUrl, BOOL bPreferTcp, U32 u32TimeoutMs);

/**
 * @brief  Disconnect from server
 */
VOID RtspClient_Disconnect(RtspClient *pClient);

/**
 * @brief  Get stream info
 */
S32 RtspClient_GetStreamInfo(RtspClient *pClient, DemuxStreamInfo *pstInfo);

/**
 * @brief  Read one packet (blocking)
 * @return 0 success, -1 EOF, error code on failure
 */
S32 RtspClient_ReadPacket(RtspClient *pClient, DemuxPacket *pstPkt);

/**
 * @brief  Check if connected
 */
BOOL RtspClient_IsConnected(RtspClient *pClient);

#ifdef __cplusplus
}
#endif

#endif /* __RTSP_CLIENT_H__ */
