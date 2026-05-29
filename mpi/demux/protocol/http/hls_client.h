/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    hls_client.h
 * @Brief     :    HLS (HTTP Live Streaming) client.
 *------------------------------------------------------------------------------
 */

#ifndef HLS_CLIENT_H
#define HLS_CLIENT_H

#include "demux/demux_type.h"
#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _HlsClient HlsClient;

HlsClient *HlsClient_Create(VOID);
VOID HlsClient_Destroy(HlsClient *pClient);
S32 HlsClient_Open(HlsClient *pClient, const CHAR *pszUrl, U32 u32TimeoutMs);
VOID HlsClient_Close(HlsClient *pClient);
S32 HlsClient_GetStreamInfo(HlsClient *pClient, DemuxStreamInfo *pstInfo);
S32 HlsClient_ReadPacket(HlsClient *pClient, DemuxPacket *pstPkt);

#ifdef __cplusplus
}
#endif

#endif /* __HLS_CLIENT_H__ */
