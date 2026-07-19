/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_rtsp_pull.c
 * @Date      :    2026-05-12
 * @Brief     :    Quick DEMUX RTSP pull verification.
 *
 * Run:
 *   ./test_rtsp_pull rtsp://192.168.1.100:554/live
 *   ./test_rtsp_pull rtsp://192.168.1.100:554/live 100
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "demux/demux_api.h"

#define TEST_PASS(name) printf("[PASS] %s\n", (name))
#define TEST_FAIL(name, msg)                      \
    do {                                          \
        printf("[FAIL] %s: %s\n", (name), (msg)); \
        return 1;                                 \
    } while (0)
#define TEST_SKIP(name, msg)                      \
    do {                                          \
        printf("[SKIP] %s: %s\n", (name), (msg)); \
        return 0;                                 \
    } while (0)

typedef struct RtspPullTestCtx {
    volatile S32 s32Running;
    U32 u32LimitPackets;
    U32 u32PacketCount;
    U32 u32KeyFrameCount;
    U64 u64TotalBytes;
    BOOL bSawKnownCodec;
} RtspPullTestCtx;

static RtspPullTestCtx g_stCtx = {
    .s32Running = 1,
    .u32LimitPackets = 60,
};

static VOID sig_handler(int sig) {
    (void)sig;
    g_stCtx.s32Running = 0;
}

static const CHAR *codec_name(DemuxCodecType eCodecType) {
    switch (eCodecType) {
        case DEMUX_CODEC_H264:
            return "H.264";
        case DEMUX_CODEC_H265:
            return "H.265";
        case DEMUX_CODEC_MJPEG:
            return "MJPEG";
        default:
            return "UNKNOWN";
    }
}

static S32 on_demux_packet(S32 s32ChnId, const DemuxPacket *pstPkt, VOID *pPriv) {
    RtspPullTestCtx *pstCtx = (RtspPullTestCtx *)pPriv;

    (void)s32ChnId;

    if (!pstCtx || !pstPkt) {
        return -1;
    }

    pstCtx->u32PacketCount++;
    pstCtx->u64TotalBytes += pstPkt->u32Size;
    if (pstPkt->bKeyFrame) {
        pstCtx->u32KeyFrameCount++;
    }
    if (pstPkt->eCodecType != DEMUX_CODEC_UNKNOWN) {
        pstCtx->bSawKnownCodec = MPP_TRUE;
    }

    if ((pstCtx->u32PacketCount % 20U) == 0U) {
        printf(
            "[INFO] packets=%u bytes=%" PRIu64 "codec=%s keyframes=%u\n",
            pstCtx->u32PacketCount,
            (uint64_t)pstCtx->u64TotalBytes,
            codec_name(pstPkt->eCodecType),
            pstCtx->u32KeyFrameCount);
    }

    if (pstCtx->u32PacketCount >= pstCtx->u32LimitPackets) {
        pstCtx->s32Running = 0;
    }

    return 1;
}

int main(int argc, char *argv[]) {
    const char *pszName = "rtsp_pull";
    const char *pszUrl;
    S32 ret;
    time_t tStart;
    DemuxChnAttr stAttr;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s <input_rtsp_url> [limit_packets]\n", argv[0]);
            printf("  example: %s rtsp://192.168.1.100:554/live 60\n", argv[0]);
            return 0;
        }
    }

    if (argc < 2) {
        TEST_SKIP(pszName, "missing input_rtsp_url");
    }

    pszUrl = argv[1];
    if (argc >= 3) {
        g_stCtx.u32LimitPackets = (U32)strtoul(argv[2], NULL, 10);
        if (g_stCtx.u32LimitPackets == 0) {
            g_stCtx.u32LimitPackets = 60;
        }
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eInputType = DEMUX_INPUT_RTSP;
    stAttr.bPreferTcp = MPP_TRUE;
    stAttr.bLowLatency = MPP_TRUE;
    stAttr.u32OpenTimeoutMs = 5000;
    stAttr.u32RwTimeoutMs = 5000;
    stAttr.u32ReconnectMs = 2000;
    stAttr.u32AnalyzeDurationMs = 1000;
    stAttr.u32ProbeSizeBytes = 512 * 1024;
    stAttr.bInjectPS = MPP_TRUE;
    stAttr.bEnableBindOutput = MPP_FALSE;
    stAttr.bEnableBindOutputSet = MPP_TRUE;
    snprintf(stAttr.szUrl, sizeof(stAttr.szUrl), "%s", pszUrl);

    ret = DEMUX_Init();
    if (ret != 0) {
        TEST_FAIL(pszName, "DEMUX_Init failed");
    }

    ret = DEMUX_CreateChn(0, &stAttr);
    if (ret != 0) {
        DEMUX_Exit();
        TEST_FAIL(pszName, "DEMUX_CreateChn failed");
    }

    ret = DEMUX_SetPacketCallback(0, on_demux_packet, &g_stCtx);
    if (ret != 0) {
        DEMUX_DestroyChn(0);
        DEMUX_Exit();
        TEST_FAIL(pszName, "DEMUX_SetPacketCallback failed");
    }

    ret = DEMUX_StartChn(0);
    if (ret != 0) {
        DEMUX_DestroyChn(0);
        DEMUX_Exit();
        TEST_FAIL(pszName, "DEMUX_StartChn failed");
    }

    printf("[INFO] pulling %s, target packets=%u\n", pszUrl, g_stCtx.u32LimitPackets);
    tStart = time(NULL);
    while (g_stCtx.s32Running) {
        if (time(NULL) - tStart > 20) {
            printf("[INFO] timeout waiting for packets\n");
            break;
        }
        sleep(1);
    }

    DEMUX_StopChn(0);
    DEMUX_DestroyChn(0);
    DEMUX_Exit();

    if (g_stCtx.u32PacketCount == 0) {
        TEST_FAIL(pszName, "no packets received");
    }
    if (!g_stCtx.bSawKnownCodec) {
        TEST_FAIL(pszName, "codec stayed UNKNOWN");
    }

    printf(
        "[INFO] final packets=%u keyframes=%u bytes=%" PRIu64 "\n",
        g_stCtx.u32PacketCount,
        g_stCtx.u32KeyFrameCount,
        (uint64_t)g_stCtx.u64TotalBytes);
    TEST_PASS(pszName);
    return 0;
}
