#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "demux/demux_api.h"
#include "mux/mux_api.h"

static volatile S32 g_s32Running = 1;

static VOID sig_handler(int sig) {
    (void)sig;
    g_s32Running = 0;
}

static MuxCodecType g_eMuxCodec = MUX_CODEC_H264;

static S32 on_demux_packet(S32 s32ChnId, const DemuxPacket *pstPkt, VOID *pPriv) {
    MuxPacket stMuxPkt;

    (void)s32ChnId;
    (void)pPriv;

    memset(&stMuxPkt, 0, sizeof(stMuxPkt));
    stMuxPkt.pu8Data = pstPkt->pu8Data;
    stMuxPkt.u32Size = pstPkt->u32Size;
    stMuxPkt.bKeyFrame = pstPkt->bKeyFrame;
    stMuxPkt.u64PTS = pstPkt->u64PTS;

    switch (pstPkt->eCodecType) {
        case DEMUX_CODEC_H264:
            stMuxPkt.eCodecType = MUX_CODEC_H264;
            break;
        case DEMUX_CODEC_H265:
            stMuxPkt.eCodecType = MUX_CODEC_H265;
            break;
        case DEMUX_CODEC_MJPEG:
            stMuxPkt.eCodecType = MUX_CODEC_MJPEG;
            break;
        default:
            stMuxPkt.eCodecType = MUX_CODEC_UNKNOWN;
            break;
    }

    return MUX_SendPacket(0, &stMuxPkt);
}

int main(int argc, char *argv[]) {
    DemuxChnAttr stDemuxAttr;
    MuxChnAttr stMuxAttr;

    if (argc < 3) {
        fprintf(
            stderr,
            "usage: %s <input_rtsp_url> <output_rtsp_url>\n"
            "  example:\n"
            "    %s rtsp://192.168.1.100:554/live rtsp://0.0.0.0:8554/relay\n"
            "  then play with:\n"
            "    ffplay rtsp://127.0.0.1:8554/relay\n"
            "    vlc rtsp://127.0.0.1:8554/relay\n",
            argv[0],
            argv[0]);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    memset(&stDemuxAttr, 0, sizeof(stDemuxAttr));
    stDemuxAttr.eInputType = DEMUX_INPUT_RTSP;
    stDemuxAttr.bPreferTcp = MPP_TRUE;
    stDemuxAttr.bLowLatency = MPP_TRUE;
    stDemuxAttr.u32OpenTimeoutMs = 5000;
    stDemuxAttr.u32RwTimeoutMs = 5000;
    stDemuxAttr.u32ReconnectMs = 2000;
    stDemuxAttr.bInjectPS = MPP_TRUE;
    snprintf(stDemuxAttr.szUrl, sizeof(stDemuxAttr.szUrl), "%s", argv[1]);

    memset(&stMuxAttr, 0, sizeof(stMuxAttr));
    stMuxAttr.eOutputType = MUX_OUTPUT_RTSP;
    stMuxAttr.stStreamAttr.eCodecType = MUX_CODEC_H264;
    stMuxAttr.stStreamAttr.u32Width = 1920;
    stMuxAttr.stStreamAttr.u32Height = 1080;
    stMuxAttr.stStreamAttr.u32Fps = 25;
    stMuxAttr.stStreamAttr.u32BitrateKbps = 4096;
    snprintf(stMuxAttr.szUrl, sizeof(stMuxAttr.szUrl), "%s", argv[2]);

    if (DEMUX_Init() != 0 || MUX_Init() != 0) {
        fprintf(stderr, "init failed\n");
        return 1;
    }

    if (DEMUX_CreateChn(0, &stDemuxAttr) != 0) {
        fprintf(stderr, "DEMUX_CreateChn failed\n");
        goto cleanup;
    }

    if (MUX_CreateChn(0, &stMuxAttr) != 0) {
        fprintf(stderr, "MUX_CreateChn failed\n");
        goto cleanup;
    }

    if (MUX_StartChn(0) != 0) {
        fprintf(stderr, "MUX_StartChn failed\n");
        goto cleanup;
    }

    if (DEMUX_SetPacketCallback(0, on_demux_packet, NULL) != 0) {
        fprintf(stderr, "DEMUX_SetPacketCallback failed\n");
        goto cleanup;
    }

    if (DEMUX_StartChn(0) != 0) {
        fprintf(stderr, "DEMUX_StartChn failed\n");
        goto cleanup;
    }

    printf("running... input=%s output=%s\n", argv[1], argv[2]);
    printf("play with: ffplay %s\n", argv[2]);

    while (g_s32Running) {
        MuxChnStat stStat;
        if (MUX_GetChnStat(0, &stStat) == 0) {
            printf(
                "\r[RTSP] clients=%u  pkts=%" PRIu64 "bytes=%" PRIu64,
                stStat.u32ActiveClients,
                (uint64_t)stStat.u64TotalPkts,
                (uint64_t)stStat.u64TotalBytes);
            fflush(stdout);
        }
        sleep(2);
    }

    printf("\nshutting down...\n");

cleanup:
    DEMUX_StopChn(0);
    DEMUX_DestroyChn(0);
    MUX_StopChn(0);
    MUX_DestroyChn(0);
    MUX_Exit();
    DEMUX_Exit();
    printf("done.\n");
    return 0;
}
