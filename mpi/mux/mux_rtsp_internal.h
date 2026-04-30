/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_rtsp_internal.h
 * @Date      :    2026-04-15
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Internal RTSP server declarations for MUX module.
 *------------------------------------------------------------------------------
 */

#ifndef __MUX_RTSP_INTERNAL_H__
#define __MUX_RTSP_INTERNAL_H__

#include "mux/mux_type.h"
#include "sys/sys_type.h"

#include <netinet/in.h>
#include <pthread.h>

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define MUX_RTSP_MAX_SESSIONS     8
#define MUX_RTSP_RECV_BUF_SIZE    4096
#define MUX_RTSP_SEND_BUF_SIZE    4096
#define MUX_RTP_MAX_PAYLOAD       1400
#define MUX_RTSP_SESSION_ID_LEN   32
#define MUX_SPS_PPS_MAX_SIZE      256

typedef enum _MuxRtspClientState {
    MUX_RTSP_CLIENT_INIT = 0,
    MUX_RTSP_CLIENT_READY,
    MUX_RTSP_CLIENT_PLAYING,
} MuxRtspClientState;

typedef struct _MuxRtspClient {
    S32                 s32Used;
    S32                 s32RtspFd;
    struct sockaddr_in  stPeerAddr;
    socklen_t           socklen;
    MuxRtspClientState  eState;
    BOOL                bInterleaved;
    U8                  u8RtpChannel;
    U8                  u8RtcpChannel;
    U16                 u16ClientRtpPort;
    U16                 u16ClientRtcpPort;
    S32                 s32RtpSock;
    S32                 s32RtcpSock;
    struct sockaddr_in  stClientRtpAddr;
    struct sockaddr_in  stClientRtcpAddr;
    CHAR                szSessionId[MUX_RTSP_SESSION_ID_LEN];
    CHAR                szRecvBuf[MUX_RTSP_RECV_BUF_SIZE];
    U32                 u32RecvLen;
} MuxRtspClient;

typedef struct _MuxRtspServer {
    S32                s32Running;
    S32                s32ListenFd;
    U16                u16ListenPort;
    CHAR               szPath[128];
    CHAR               szHost[64];
    pthread_t          tidAccept;
    pthread_mutex_t    lock;
    MuxRtspClient      astClients[MUX_RTSP_MAX_SESSIONS];
    U32                u32Ssrc;
    U16                u16Seq;
    /* SPS/PPS cache for SDP and keyframe pre-injection */
    U8                 au8Sps[MUX_SPS_PPS_MAX_SIZE];
    U32                u32SpsLen;
    U8                 au8Pps[MUX_SPS_PPS_MAX_SIZE];
    U32                u32PpsLen;
    U8                 au8Vps[MUX_SPS_PPS_MAX_SIZE];
    U32                u32VpsLen;
    /* statistics */
    U64                u64TotalPkts;
    U64                u64TotalBytes;
    U32                u32ActiveClients;
} MuxRtspServer;

/* forward declare AVFormatContext / AVStream to avoid pulling ffmpeg headers */
struct AVFormatContext;
struct AVStream;

typedef struct _MuxChannel {
    S32              s32Created;
    S32              s32State;
    S32              s32ChnId;
    S32              s32StopWorker;
    S32              s32WorkerAlive;
    pthread_t        tidWorker;
    pthread_mutex_t  lock;
    MuxChnAttr       stAttr;
    MppNode          stSinkNode;
    struct AVFormatContext *pstFmt;
    struct AVStream  *pstStream;
    S32              s32HeaderWritten;
    MuxRtspServer    stRtspServer;
} MuxChannel;

S32 mux_rtsp_server_start(MuxChannel *pstChn);
VOID mux_rtsp_server_stop(MuxChannel *pstChn);
S32 mux_rtsp_server_send_packet(MuxChannel *pstChn, const MuxPacket *pstPkt);

/* RTP packetizer */
S32 mux_rtsp_send_h26x_annexb(MuxRtspServer *pstServer, MuxRtspClient *pstClient,
                               const MuxPacket *pstPkt);
VOID mux_rtsp_cache_param_sets(MuxRtspServer *pstServer, const MuxPacket *pstPkt);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __MUX_RTSP_INTERNAL_H__ */
