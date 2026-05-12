/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    sample_rtsp_pull.c
 * @Date      :    2026-05-12
 * @Brief     :    Sample: pull Annex-B packets from an RTSP source with DEMUX.
 *
 * Run:
 *   ./sample_rtsp_pull rtsp://192.168.1.100:554/live
 *   ./sample_rtsp_pull rtsp://192.168.1.100:554/live 200
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "demux/demux_api.h"

typedef struct SampleStats {
    volatile S32 s32Running;
    U32          u32LimitPackets;
    U32          u32PacketCount;
    U32          u32KeyFrameCount;
    U64          u64TotalBytes;
    BOOL         bStreamInfoPrinted;
} SampleStats;

static SampleStats g_stStats = {
    .s32Running = 1,
};

static const CHAR *codec_name(DemuxCodecType eCodecType)
{
    switch (eCodecType) {
        case DEMUX_CODEC_H264:  return "H.264";
        case DEMUX_CODEC_H265:  return "H.265";
        case DEMUX_CODEC_MJPEG: return "MJPEG";
        default:                return "UNKNOWN";
    }
}

static VOID sig_handler(int sig)
{
    (void)sig;
    g_stStats.s32Running = 0;
}

static VOID print_stream_info_once(S32 s32ChnId, const DemuxPacket *pstPkt)
{
    DemuxStreamInfo stInfo;

    if (g_stStats.bStreamInfoPrinted) {
        return;
    }

    memset(&stInfo, 0, sizeof(stInfo));
    if (DEMUX_GetStreamInfo(s32ChnId, &stInfo) == 0) {
        printf("[INFO] stream codec=%s, %ux%u @ %u fps\n",
               codec_name(stInfo.eCodecType),
               stInfo.u32Width,
               stInfo.u32Height,
               stInfo.u32Fps);
    } else {
        printf("[INFO] stream codec=%s, %ux%u\n",
               codec_name(pstPkt->eCodecType),
               pstPkt->u32Width,
               pstPkt->u32Height);
    }

    g_stStats.bStreamInfoPrinted = MPP_TRUE;
}

static S32 on_demux_packet(S32 s32ChnId, const DemuxPacket *pstPkt, VOID *pPriv)
{
    SampleStats *pstStats = (SampleStats *)pPriv;

    if (!pstPkt || !pstStats) {
        return -1;
    }

    print_stream_info_once(s32ChnId, pstPkt);

    pstStats->u32PacketCount++;
    pstStats->u64TotalBytes += pstPkt->u32Size;
    if (pstPkt->bKeyFrame) {
        pstStats->u32KeyFrameCount++;
    }

    if ((pstStats->u32PacketCount % 30U) == 1U) {
        printf("[PKT ] #%u codec=%s size=%u key=%d pts=%llu us total=%llu bytes\n",
               pstStats->u32PacketCount,
               codec_name(pstPkt->eCodecType),
               pstPkt->u32Size,
               pstPkt->bKeyFrame,
               (unsigned long long)pstPkt->u64PTS,
               (unsigned long long)pstStats->u64TotalBytes);
    }

    if (pstStats->u32LimitPackets > 0 && pstStats->u32PacketCount >= pstStats->u32LimitPackets) {
        pstStats->s32Running = 0;
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    S32 ret;
    DemuxChnAttr stAttr;
    const char *pszUrl;

    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <input_rtsp_url> [limit_packets]\n"
                "  example:\n"
                "    %s rtsp://192.168.1.100:554/live\n"
                "    %s rtsp://192.168.1.100:554/live 200\n",
                argv[0], argv[0], argv[0]);
        return 1;
    }

    pszUrl = argv[1];
    if (argc >= 3) {
        g_stStats.u32LimitPackets = (U32)strtoul(argv[2], NULL, 10);
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
    snprintf(stAttr.szUrl, sizeof(stAttr.szUrl), "%s", pszUrl);

    ret = DEMUX_Init();
    if (ret != 0) {
        fprintf(stderr, "DEMUX_Init failed: %d\n", ret);
        return 1;
    }

    ret = DEMUX_CreateChn(0, &stAttr);
    if (ret != 0) {
        fprintf(stderr, "DEMUX_CreateChn failed: %d\n", ret);
        DEMUX_Exit();
        return 1;
    }

    ret = DEMUX_SetPacketCallback(0, on_demux_packet, &g_stStats);
    if (ret != 0) {
        fprintf(stderr, "DEMUX_SetPacketCallback failed: %d\n", ret);
        DEMUX_DestroyChn(0);
        DEMUX_Exit();
        return 1;
    }

    ret = DEMUX_StartChn(0);
    if (ret != 0) {
        fprintf(stderr, "DEMUX_StartChn failed: %d\n", ret);
        DEMUX_DestroyChn(0);
        DEMUX_Exit();
        return 1;
    }

    printf("[INFO] pulling RTSP: %s\n", pszUrl);
    if (g_stStats.u32LimitPackets > 0) {
        printf("[INFO] packet limit: %u\n", g_stStats.u32LimitPackets);
    } else {
        printf("[INFO] packet limit: unlimited, stop with Ctrl+C\n");
    }

    while (g_stStats.s32Running) {
        sleep(1);
    }

    printf("[DONE] packets=%u keyframes=%u total_bytes=%llu\n",
           g_stStats.u32PacketCount,
           g_stStats.u32KeyFrameCount,
           (unsigned long long)g_stStats.u64TotalBytes);

    DEMUX_StopChn(0);
    DEMUX_DestroyChn(0);
    DEMUX_Exit();
    return 0;
}
