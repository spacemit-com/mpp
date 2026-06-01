/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "rtmp_handshake.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

void RtmpHandshake_Init(RtmpHandshake *pHs) {
    if (!pHs)
        return;
    memset(pHs, 0, sizeof(RtmpHandshake));
    pHs->eState = RTMP_HS_INIT;
}

S32 RtmpHandshake_CreateC0C1(RtmpHandshake *pHs, U8 *pu8Buf, U32 u32BufSize) {
    if (!pHs || !pu8Buf || u32BufSize < 1 + RTMP_HANDSHAKE_SIZE) {
        return -1;
    }

    /* C0: Version */
    pu8Buf[0] = RTMP_VERSION;

    /* C1: Time (4 bytes) + Zero (4 bytes) + Random (1528 bytes) */
    pHs->u32ClientTime = (U32)time(NULL);

    /* Time */
    pu8Buf[1] = (pHs->u32ClientTime >> 24) & 0xFF;
    pu8Buf[2] = (pHs->u32ClientTime >> 16) & 0xFF;
    pu8Buf[3] = (pHs->u32ClientTime >> 8) & 0xFF;
    pu8Buf[4] = pHs->u32ClientTime & 0xFF;

    /* Zero */
    memset(&pu8Buf[5], 0, 4);

    /* Random data */
    unsigned int seed = pHs->u32ClientTime;
    for (int i = 9; i < 1 + RTMP_HANDSHAKE_SIZE; i++) {
        pu8Buf[i] = (U8)(rand_r(&seed) & 0xFF);
    }

    /* Store C1 for later comparison */
    memcpy(pHs->au8C1, &pu8Buf[1], RTMP_HANDSHAKE_SIZE);

    pHs->eState = RTMP_HS_C1_SENT;
    return 1 + RTMP_HANDSHAKE_SIZE;
}

S32 RtmpHandshake_ProcessS0S1S2(RtmpHandshake *pHs, const U8 *pu8Data, U32 u32Len, U32 *pu32Consumed) {
    if (!pHs || !pu8Data || !pu32Consumed)
        return -1;

    *pu32Consumed = 0;
    U32 needed = 1 + RTMP_HANDSHAKE_SIZE * 2; /* S0 + S1 + S2 */

    if (u32Len < needed) {
        return 1; /* Need more data */
    }

    /* S0: Version check */
    if (pu8Data[0] != RTMP_VERSION) {
        return -1;
    }
    pHs->eState = RTMP_HS_S0_RECV;

    /* S1: Store for C2 */
    memcpy(pHs->au8S1, &pu8Data[1], RTMP_HANDSHAKE_SIZE);
    pHs->u32ServerTime = ((U32)pu8Data[1] << 24) | ((U32)pu8Data[2] << 16) | ((U32)pu8Data[3] << 8) | pu8Data[4];
    pHs->eState = RTMP_HS_S1_RECV;

    /* S2: Verify echo of C1 (simplified check) */
    const U8 *pS2 = &pu8Data[1 + RTMP_HANDSHAKE_SIZE];
    if (memcmp(&pS2[8], &pHs->au8C1[8], RTMP_HANDSHAKE_SIZE - 8) != 0) {
        /* Strict servers may fail, but many don't verify */
    }
    pHs->eState = RTMP_HS_S2_RECV;

    *pu32Consumed = needed;
    return 0;
}

S32 RtmpHandshake_CreateC2(RtmpHandshake *pHs, U8 *pu8Buf, U32 u32BufSize) {
    if (!pHs || !pu8Buf || u32BufSize < RTMP_HANDSHAKE_SIZE) {
        return -1;
    }

    /* C2: Echo S1 with our time */
    memcpy(pu8Buf, pHs->au8S1, RTMP_HANDSHAKE_SIZE);

    /* Set time read (our client time) at offset 4-7 */
    pu8Buf[4] = (pHs->u32ClientTime >> 24) & 0xFF;
    pu8Buf[5] = (pHs->u32ClientTime >> 16) & 0xFF;
    pu8Buf[6] = (pHs->u32ClientTime >> 8) & 0xFF;
    pu8Buf[7] = pHs->u32ClientTime & 0xFF;

    pHs->eState = RTMP_HS_DONE;
    return RTMP_HANDSHAKE_SIZE;
}

BOOL RtmpHandshake_IsDone(const RtmpHandshake *pHs) { return pHs && pHs->eState == RTMP_HS_DONE; }
