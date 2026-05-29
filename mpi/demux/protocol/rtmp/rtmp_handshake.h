/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    rtmp_handshake.h
 * @Brief     :    RTMP handshake protocol implementation.
 *------------------------------------------------------------------------------
 */

#ifndef RTMP_HANDSHAKE_H
#define RTMP_HANDSHAKE_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTMP_HANDSHAKE_SIZE 1536
#define RTMP_VERSION 3

typedef enum _RtmpHandshakeState {
    RTMP_HS_INIT = 0,
    RTMP_HS_C0_SENT,
    RTMP_HS_C1_SENT,
    RTMP_HS_S0_RECV,
    RTMP_HS_S1_RECV,
    RTMP_HS_S2_RECV,
    RTMP_HS_C2_SENT,
    RTMP_HS_DONE
} RtmpHandshakeState;

typedef struct _RtmpHandshake {
    RtmpHandshakeState eState;
    U8 au8C1[RTMP_HANDSHAKE_SIZE];
    U8 au8S1[RTMP_HANDSHAKE_SIZE];
    U32 u32ClientTime;
    U32 u32ServerTime;
} RtmpHandshake;

/**
 * @brief  Initialize handshake context
 */
void RtmpHandshake_Init(RtmpHandshake *pHs);

/**
 * @brief  Generate C0+C1 packet
 * @return Total bytes written to buffer
 */
S32 RtmpHandshake_CreateC0C1(RtmpHandshake *pHs, U8 *pu8Buf, U32 u32BufSize);

/**
 * @brief  Process S0+S1+S2 response
 * @return 0 on success, -1 on error, 1 if need more data
 */
S32 RtmpHandshake_ProcessS0S1S2(RtmpHandshake *pHs, const U8 *pu8Data, U32 u32Len, U32 *pu32Consumed);

/**
 * @brief  Generate C2 packet
 * @return Bytes written to buffer
 */
S32 RtmpHandshake_CreateC2(RtmpHandshake *pHs, U8 *pu8Buf, U32 u32BufSize);

/**
 * @brief  Check if handshake is complete
 */
BOOL RtmpHandshake_IsDone(const RtmpHandshake *pHs);

#ifdef __cplusplus
}
#endif

#endif /* __RTMP_HANDSHAKE_H__ */
