/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    demux.h
 * @Date      :    2026-05-09
 * @Brief     :    Demux implementation.
 *                 Supports RTSP/RTMP/HTTP-FLV/HLS/MP4/TS/FLV.
 *------------------------------------------------------------------------------
 */

#ifndef DEMUX_H
#define DEMUX_H

#include "demux/demux_type.h"
#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== Protocol Types ======================== */

typedef enum _DemuxProtocol {
    DEMUX_PROTO_RTSP = 0,  // rtsp://
    DEMUX_PROTO_RTMP,  // rtmp://
    DEMUX_PROTO_HTTP_FLV,  // http://...flv
    DEMUX_PROTO_HLS,  // http://...m3u8
    DEMUX_PROTO_FILE_MP4,  // *.mp4
    DEMUX_PROTO_FILE_TS,  // *.ts
    DEMUX_PROTO_FILE_FLV,  // *.flv
    DEMUX_PROTO_UNKNOWN
} DemuxProtocol;

/* ======================== Context (Low-level API) ======================== */

typedef struct _DemuxCtx DemuxCtx;

/**
 * @brief  Detect protocol from URL
 */
DemuxProtocol Demux_DetectProtocol(const CHAR *pszUrl);

/**
 * @brief  Check if protocol is supported (compiled in)
 */
BOOL Demux_IsSupported(DemuxProtocol eProto);

/**
 * @brief  Create demux context
 * @param  pszUrl  Input URL or file path
 * @return Context pointer, NULL on failure
 */
DemuxCtx *Demux_Create(const CHAR *pszUrl);

/**
 * @brief  Destroy context
 */
VOID Demux_Destroy(DemuxCtx *pCtx);

/**
 * @brief  Open and connect (blocking)
 * @param  pCtx          Context
 * @param  bPreferTcp    Use TCP for RTSP
 * @param  u32TimeoutMs  Connection timeout
 * @return 0 success, error code on failure
 */
S32 Demux_Open(DemuxCtx *pCtx, BOOL bPreferTcp, U32 u32TimeoutMs);

/**
 * @brief  Close connection
 */
VOID Demux_Close(DemuxCtx *pCtx);

/**
 * @brief  Get stream info (after open)
 */
S32 Demux_GetStreamInfoCtx(DemuxCtx *pCtx, DemuxStreamInfo *pstInfo);

/**
 * @brief  Read one packet (blocking)
 * @param  pCtx     Context
 * @param  pstPkt   Output packet (data pointer valid until next call)
 * @return 0 success, ERR_DEMUX_NO_STREAM on EOF, error code on failure
 */
S32 Demux_ReadPacket(DemuxCtx *pCtx, DemuxPacket *pstPkt);

/**
 * @brief  Seek (for file sources only)
 * @param  pCtx      Context
 * @param  s64PtsUs  Target PTS in microseconds
 * @return 0 success, error code on failure
 */
S32 Demux_Seek(DemuxCtx *pCtx, S64 s64PtsUs);

/**
 * @brief  Get duration (for file sources only)
 * @return Duration in microseconds, 0 for live streams
 */
S64 Demux_GetDuration(DemuxCtx *pCtx);

/* ============================================================================
 * Channel-based API (High-level, recommended)
 *
 * Features:
 *   - Auto-reconnect on disconnect
 *   - Thread-safe channel management
 *   - Callback mode + SYS_Bind support
 *   - Statistics tracking
 * ============================================================================ */

/**
 * @brief  Initialize DEMUX module (global resources)
 *         Must be called before other channel APIs.
 * @return 0 on success, error code on failure
 */
S32 DEMUX_Init(VOID);

/**
 * @brief  Deinitialize DEMUX module
 *         All channels must be destroyed first.
 * @return 0 on success, error code on failure
 */
S32 DEMUX_Exit(VOID);

/**
 * @brief  Create DEMUX channel
 * @param  s32ChnId  Channel ID [0, DEMUX_MAX_CHN)
 * @param  pstAttr   Channel attributes
 * @return 0 on success, error code on failure
 */
S32 DEMUX_CreateChn(S32 s32ChnId, const DemuxChnAttr *pstAttr);

/**
 * @brief  Destroy DEMUX channel
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 DEMUX_DestroyChn(S32 s32ChnId);

/**
 * @brief  Start channel (begin streaming with auto-reconnect)
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 DEMUX_StartChn(S32 s32ChnId);

/**
 * @brief  Stop channel
 * @param  s32ChnId  Channel ID
 * @return 0 on success, error code on failure
 */
S32 DEMUX_StopChn(S32 s32ChnId);

/**
 * @brief  Get stream info (valid after connection established)
 * @param  s32ChnId  Channel ID
 * @param  pstInfo   Output stream info
 * @return 0 on success, error code on failure
 */
S32 DEMUX_GetStreamInfo(S32 s32ChnId, DemuxStreamInfo *pstInfo);

/**
 * @brief  Set packet callback (manual mode)
 *         In bind mode, data flows automatically via SYS_SendStream.
 * @param  s32ChnId  Channel ID
 * @param  pfnCb     Callback function, NULL to cancel
 * @param  pPriv     User private data
 * @return 0 on success, error code on failure
 */
S32 DEMUX_SetPacketCallback(S32 s32ChnId, DemuxPacketCallback pfnCb, VOID *pPriv);

/**
 * @brief  Get DEMUX source node for SYS_Bind (compressed domain binding)
 * @param  s32ChnId  Channel ID
 * @param  pstNode   Output node
 * @return 0 on success, error code on failure
 */
S32 DEMUX_GetSrcNode(S32 s32ChnId, MppNode *pstNode);

#ifdef __cplusplus
}
#endif

#endif /* __DEMUX_H__ */
