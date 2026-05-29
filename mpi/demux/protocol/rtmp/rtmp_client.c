/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    rtmp_client.c
 * @Brief     :    Lightweight RTMP client implementation.
 *------------------------------------------------------------------------------
 */

#include "rtmp_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common/socket_utils.h"
#include "common/url_parser.h"

/* RTMP chunk stream IDs */
#define RTMP_CSID_PROTOCOL 2
#define RTMP_CSID_COMMAND 3
#define RTMP_CSID_VIDEO 6
#define RTMP_CSID_AUDIO 7

/* RTMP message types */
#define RTMP_MSG_SET_CHUNK_SIZE 1
#define RTMP_MSG_ABORT 2
#define RTMP_MSG_ACK 3
#define RTMP_MSG_USER_CONTROL 4
#define RTMP_MSG_ACK_WINDOW_SIZE 5
#define RTMP_MSG_SET_PEER_BANDWIDTH 6
#define RTMP_MSG_AUDIO 8
#define RTMP_MSG_VIDEO 9
#define RTMP_MSG_AMF3_COMMAND 17
#define RTMP_MSG_AMF0_COMMAND 20

#define RTMP_DEFAULT_CHUNK_SIZE 128
#define RTMP_MAX_CHUNK_SIZE 65536

typedef enum _RtmpState {
    RTMP_STATE_DISCONNECTED,
    RTMP_STATE_HANDSHAKE,
    RTMP_STATE_CONNECTED,
    RTMP_STATE_STREAMING
} RtmpState;

struct _RtmpClient {
    S32 s32Fd;
    UrlInfo stUrl;
    RtmpState eState;

    U32 u32ChunkSize;
    U32 u32WindowAckSize;
    U32 u32StreamId;

    /* Stream info */
    DemuxStreamInfo stStreamInfo;
    U8 au8ExtraData[1024];
    U32 u32ExtraDataLen;

    /* Read buffer */
    U8 au8ReadBuf[512 * 1024];
    U32 u32ReadPos;

    /* Chunk state for each CSID */
    struct {
        U32 u32Timestamp;
        U32 u32MsgLen;
        U8 u8MsgTypeId;
        U32 u32StreamId;
    } astChunkState[16];
};

/* RTMP handshake */
static S32 rtmp_handshake(RtmpClient *pClient) {
    U8 au8C0C1[1537];
    U8 au8S0S1S2[3073];
    U8 au8C2[1536];

    /* C0: version (3) */
    au8C0C1[0] = 0x03;

    /* C1: timestamp (4) + zero (4) + random (1528) */
    U32 timestamp = (U32)time(NULL);
    unsigned int seed = timestamp;
    au8C0C1[1] = (timestamp >> 24) & 0xFF;
    au8C0C1[2] = (timestamp >> 16) & 0xFF;
    au8C0C1[3] = (timestamp >> 8) & 0xFF;
    au8C0C1[4] = timestamp & 0xFF;
    memset(&au8C0C1[5], 0, 4);
    for (int i = 9; i < 1537; i++) {
        au8C0C1[i] = rand_r(&seed) & 0xFF;
    }

    /* Send C0 + C1 */
    if (Socket_SendAll(pClient->s32Fd, au8C0C1, 1537) < 0) {
        return -1;
    }

    /* Receive S0 + S1 + S2 */
    if (Socket_RecvExact(pClient->s32Fd, au8S0S1S2, 3073, 5000) != 0) {
        return -1;
    }

    /* Verify S0 */
    if (au8S0S1S2[0] != 0x03) {
        return -1;
    }

    /* C2 = S1 */
    memcpy(au8C2, &au8S0S1S2[1], 1536);

    /* Send C2 */
    if (Socket_SendAll(pClient->s32Fd, au8C2, 1536) < 0) {
        return -1;
    }

    return 0;
}

/* Send RTMP connect command (simplified) */
static S32 rtmp_send_connect(RtmpClient *pClient) {
    /* AMF0 encoded connect command - simplified */
    U8 au8Cmd[256];
    U32 u32Len = 0;

    /* String type marker */
    au8Cmd[u32Len++] = 0x02;
    /* String length (7 = "connect") */
    au8Cmd[u32Len++] = 0x00;
    au8Cmd[u32Len++] = 0x07;
    memcpy(&au8Cmd[u32Len], "connect", 7);
    u32Len += 7;

    /* Transaction ID (number) */
    au8Cmd[u32Len++] = 0x00; /* number type */
    memset(&au8Cmd[u32Len], 0, 8);
    au8Cmd[u32Len + 7] = 0x3F;
    au8Cmd[u32Len + 6] = 0xF0; /* 1.0 */
    u32Len += 8;

    /* TODO: Add proper AMF0 object with app, tcUrl, etc. */
    au8Cmd[u32Len++] = 0x05; /* null */

    /* Send as chunk */
    /* TODO: Implement proper chunk framing */

    return 0;
}

RtmpClient *RtmpClient_Create(VOID) {
    RtmpClient *pClient = (RtmpClient *)calloc(1, sizeof(RtmpClient));
    if (pClient) {
        pClient->s32Fd = -1;
        pClient->u32ChunkSize = RTMP_DEFAULT_CHUNK_SIZE;
        pClient->u32WindowAckSize = 2500000;
    }
    return pClient;
}

VOID RtmpClient_Destroy(RtmpClient *pClient) {
    if (pClient) {
        RtmpClient_Disconnect(pClient);
        free(pClient);
    }
}

S32 RtmpClient_Connect(RtmpClient *pClient, const CHAR *pszUrl, U32 u32TimeoutMs) {
    if (!pClient || !pszUrl)
        return ERR_DEMUX_NULL_PTR;

    /* Parse URL */
    if (Url_Parse(pszUrl, &pClient->stUrl) != 0) {
        return ERR_DEMUX_OPEN_FAIL;
    }

    /* TCP connect */
    pClient->s32Fd = Socket_TcpConnect(pClient->stUrl.szHost, pClient->stUrl.u16Port, u32TimeoutMs);
    if (pClient->s32Fd < 0) {
        return ERR_DEMUX_OPEN_FAIL;
    }

    /* Handshake */
    if (rtmp_handshake(pClient) != 0) {
        Socket_Close(pClient->s32Fd);
        pClient->s32Fd = -1;
        return ERR_DEMUX_OPEN_FAIL;
    }

    pClient->eState = RTMP_STATE_CONNECTED;

    /* Send connect command */
    if (rtmp_send_connect(pClient) != 0) {
        Socket_Close(pClient->s32Fd);
        pClient->s32Fd = -1;
        return ERR_DEMUX_OPEN_FAIL;
    }

    /* TODO: Handle connect response, createStream, play */

    pClient->eState = RTMP_STATE_STREAMING;

    return 0;
}

VOID RtmpClient_Disconnect(RtmpClient *pClient) {
    if (!pClient)
        return;

    if (pClient->s32Fd >= 0) {
        Socket_Close(pClient->s32Fd);
        pClient->s32Fd = -1;
    }

    pClient->eState = RTMP_STATE_DISCONNECTED;
}

S32 RtmpClient_GetStreamInfo(RtmpClient *pClient, DemuxStreamInfo *pstInfo) {
    if (!pClient || !pstInfo)
        return ERR_DEMUX_NULL_PTR;
    *pstInfo = pClient->stStreamInfo;
    return 0;
}

S32 RtmpClient_ReadPacket(RtmpClient *pClient, DemuxPacket *pstPkt) {
    if (!pClient || !pstPkt)
        return ERR_DEMUX_NULL_PTR;
    if (pClient->eState != RTMP_STATE_STREAMING)
        return ERR_DEMUX_NOT_STARTED;

    /* TODO: Read RTMP chunks, reassemble messages, extract video data */

    return ERR_DEMUX_NO_STREAM;
}

BOOL RtmpClient_IsConnected(RtmpClient *pClient) { return pClient && pClient->eState == RTMP_STATE_STREAMING; }
