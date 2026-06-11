/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_rtsp_server.h
 * @Date      :    2026-04-15
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Internal RTSP server declarations for MUX module.
 *
 * Architecture (Shared RTSP Server):
 *   - Single global server listening on one port
 *   - Multiple streams registered with different paths (/live/0, /live/1, etc.)
 *   - Clients connect and request specific path, routed to correct stream
 *   - Standard MUX_* API unchanged, internal implementation handles sharing
 *------------------------------------------------------------------------------
 */

#ifndef MUX_RTSP_SERVER_H
#define MUX_RTSP_SERVER_H

#include <netinet/in.h>
#include <pthread.h>

#include "mux/mux_type.h"
#include "sys/sys_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define MUX_RTSP_MAX_CLIENTS 16
#define MUX_RTSP_SESSION_ID_LEN 32
#define MUX_RTSP_SDP_MAX_LEN 4096
#define MUX_RTSP_RECV_BUF_SIZE 4096
#define MUX_RTSP_SEND_BUF_SIZE 4096
#define MUX_SPS_PPS_MAX_SIZE 256
#define MUX_RTSP_MAX_STREAMS 32
#define RTP_PACKET_MAX_SIZE 1400
#define MUX_RTP_MAX_PAYLOAD 1400
#define RTP_MAX_TRACKS 32
#define MUX_RTSP_URL_MAX_LEN 256

/* Forward declaration */
typedef struct _MuxRtspStream MuxRtspStream;

typedef enum _MuxRtspClientState {
    MUX_RTSP_CLIENT_INIT = 0,
    MUX_RTSP_CLIENT_READY,
    MUX_RTSP_CLIENT_PLAYING,
} MuxRtspClientState;

/* Client connected to a specific stream */
typedef struct _MuxRtspClient {
    S32 s32Used;
    S32 s32RtspFd;
    struct sockaddr_in stPeerAddr;
    socklen_t socklen;
    MuxRtspClientState eState;
    BOOL bInterleaved;
    BOOL bNeedParamInject; /* Need to inject SPS/PPS before first frame */
    U8 u8RtpChannel;
    U8 u8RtcpChannel;
    U16 u16ClientRtpPort;
    U16 u16ClientRtcpPort;
    S32 s32RtpSock;
    S32 s32RtcpSock;
    struct sockaddr_in stClientRtpAddr;
    struct sockaddr_in stClientRtcpAddr;
    CHAR szSessionId[MUX_RTSP_SESSION_ID_LEN];
    CHAR szRecvBuf[MUX_RTSP_RECV_BUF_SIZE];
    U32 u32RecvLen;
    MuxRtspStream *pStream; /* Stream this client is subscribed to */
} MuxRtspClient;

/* Stream registered with the shared server (one per MuxChannel) */
struct _MuxRtspStream {
    S32 s32Used;
    S32 s32ChnId;
    CHAR szPath[128]; /* e.g., "/live/0" */
    MuxCodecType eCodecType;
    U32 u32Width;
    U32 u32Height;
    U32 u32Ssrc;
    U16 u16Seq;
    pthread_mutex_t lock; /* Per-stream lock for fine-grained locking */
    /* SPS/PPS/VPS cache */
    U8 au8Sps[MUX_SPS_PPS_MAX_SIZE];
    U32 u32SpsLen;
    U8 au8Pps[MUX_SPS_PPS_MAX_SIZE];
    U32 u32PpsLen;
    U8 au8Vps[MUX_SPS_PPS_MAX_SIZE];
    U32 u32VpsLen;
    /* Statistics */
    U64 u64TotalPkts;
    U64 u64TotalBytes;
};

/* Global shared RTSP server (singleton) */
typedef struct _MuxGlobalRtspServer {
    S32 s32Inited;
    S32 s32Running;
    S32 s32ListenFd;
    U16 u16Port;
    pthread_t tidAccept;
    pthread_mutex_t lock; /* Global lock for streams/clients arrays */
    MuxRtspStream astStreams[MUX_RTSP_MAX_STREAMS];
    MuxRtspClient astClients[MUX_RTSP_MAX_CLIENTS];
    U32 u32StreamCount;
    U32 u32RefCount; /* Reference count for init/deinit */
} MuxGlobalRtspServer;

/* Legacy structure for backward compatibility (unused fields) */
typedef struct _MuxRtspServer {
    S32 s32Running;  /* Deprecated: use global server */
    S32 s32ListenFd; /* Deprecated */
    U16 u16ListenPort;
    CHAR szPath[128];
    CHAR szHost[64];
    pthread_t tidAccept;         /* Deprecated */
    pthread_mutex_t lock;        /* Deprecated */
    MuxRtspClient astClients[8]; /* Deprecated */
    U32 u32Ssrc;
    U16 u16Seq;
    U8 au8Sps[MUX_SPS_PPS_MAX_SIZE];
    U32 u32SpsLen;
    U8 au8Pps[MUX_SPS_PPS_MAX_SIZE];
    U32 u32PpsLen;
    U8 au8Vps[MUX_SPS_PPS_MAX_SIZE];
    U32 u32VpsLen;
    U64 u64TotalPkts;
    U64 u64TotalBytes;
    U32 u32ActiveClients;
} MuxRtspServer;

typedef struct _MuxChannel {
    S32 s32Created;
    S32 s32State;
    S32 s32ChnId;
    S32 s32StopWorker;
    S32 s32WorkerAlive;
    pthread_t tidWorker;
    pthread_mutex_t lock;
    MuxChnAttr stAttr;
    MppNode stSinkNode;
    MuxRtspServer stRtspServer;
    /* Non-RTSP output backend context (set per eOutputType). Stored as void*
     * to keep this shared header free of backend-specific includes. The actual
     * type is determined by eOutputType: MuxFile* or MuxRtmp*. */
    void *pOutputCtx;
} MuxChannel;

S32 mux_rtsp_server_start(MuxChannel *pstChn);
VOID mux_rtsp_server_stop(MuxChannel *pstChn);
S32 mux_rtsp_server_send_packet(MuxChannel *pstChn, const MuxPacket *pstPkt);

/* RTP packetizer */
S32 mux_rtsp_send_h26x_annexb(MuxRtspServer *pstServer, MuxRtspClient *pstClient, const MuxPacket *pstPkt);
VOID mux_rtsp_cache_param_sets(MuxRtspServer *pstServer, const MuxPacket *pstPkt);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __MUX_RTSP_SERVER_H__ */
