/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    rtmp_client.h
 * @Brief     :    Lightweight RTMP client implementation.
 *------------------------------------------------------------------------------
 */

#ifndef RTMP_CLIENT_H
#define RTMP_CLIENT_H

#include "demux/demux_type.h"
#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _RtmpClient RtmpClient;

RtmpClient *RtmpClient_Create(VOID);
VOID RtmpClient_Destroy(RtmpClient *pClient);
S32 RtmpClient_Connect(RtmpClient *pClient, const CHAR *pszUrl, U32 u32TimeoutMs);
VOID RtmpClient_Disconnect(RtmpClient *pClient);
S32 RtmpClient_GetStreamInfo(RtmpClient *pClient, DemuxStreamInfo *pstInfo);
S32 RtmpClient_ReadPacket(RtmpClient *pClient, DemuxPacket *pstPkt);
BOOL RtmpClient_IsConnected(RtmpClient *pClient);

#ifdef __cplusplus
}
#endif

#endif /* __RTMP_CLIENT_H__ */
