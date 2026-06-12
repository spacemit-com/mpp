/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    sample_rtsp_relay_record.c
 * @Date      :    2026-05-20
 * @Brief     :    Sample: pull one RTSP stream, fan it out to TWO sinks at once
 *                 -- re-publish as a new RTSP service AND record to a local file.
 *
 *                 DEMUX (RTSP in) --callback--> +--> MUX (RTSP out)   [relay]
 *                                               +--> FILE (raw ES)    [record]
 *
 *                 No re-encoding: the compressed packets are copied straight
 *                 through, so this is a cheap "edge gateway / DVR" pattern --
 *                 view the camera remotely while keeping a local recording.
 *
 *                 The source may be either a live RTSP URL (rtsp://...) or a
 *                 local container file (.mp4/.ts/.flv). Both are demuxed the
 *                 same way, so the record path can be exercised offline.
 *
 *                 IMPORTANT design notes (see CLAUUDE.md / verified behavior):
 *                 - MUX only supports RTSP output, it CANNOT write files. The
 *                   local recording is therefore a raw annexb elementary stream
 *                   (.h264 / .h265), written with plain fwrite() from the
 *                   demux packet callback. Annexb already carries start codes,
 *                   so the file is directly playable / decodable.
 *                 - The demux delivers packets through a user callback. If the
 *                   callback returns 0, demux ALSO tries to SYS_SendStream to
 *                   its (here unbound) source node, which would stall the relay
 *                   ~0.5s per packet. We return non-zero from the callback to
 *                   skip that path -- the read thread ignores the return value.
 *
 * Run:
 *   ./sample_rtsp_relay_record rtsp://cam/live
 *   ./sample_rtsp_relay_record rtsp://cam/live --port 8554 --rec ./dvr
 *   ./sample_rtsp_relay_record rtsp://cam/live --frames 1000
 *   ./sample_rtsp_relay_record rtsp://cam/live --no-relay      (record only)
 *   ./sample_rtsp_relay_record rtsp://cam/live --no-record     (relay only)
 *   ./sample_rtsp_relay_record ./test_video.mp4 --no-relay     (file -> DVR)
 *
 * Relay URL printed at startup, e.g. rtsp://<board-ip>:8554/relay
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "demux/demux_api.h"
#include "demux/demux_type.h"
#include "mux/mux_api.h"
#include "mux/mux_type.h"
#include "sys/sys_api.h"

#define DEMUX_CHN 0
#define MUX_RELAY_CHN 0
#define DEFAULT_RTSP_PORT 8554
#define DEFAULT_REC_PATH "relay_record"

typedef struct RelayCtx {
    int max_frames;
    int forwarded;
    int relay_sent;
    int relay_err;
    int record_bytes;
    int relay_ready;
    FILE *fpRec;
} RelayCtx;

static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static MuxCodecType demux_to_mux_codec(DemuxCodecType eIn) {
    switch (eIn) {
        case DEMUX_CODEC_H264:
            return MUX_CODEC_H264;
        case DEMUX_CODEC_H265:
            return MUX_CODEC_H265;
        case DEMUX_CODEC_MJPEG:
            return MUX_CODEC_MJPEG;
        default:
            return MUX_CODEC_UNKNOWN;
    }
}

static const char *mux_codec_name(MuxCodecType eCodec) {
    switch (eCodec) {
        case MUX_CODEC_H264:
            return "H.264";
        case MUX_CODEC_H265:
            return "H.265";
        case MUX_CODEC_MJPEG:
            return "MJPEG";
        default:
            return "unknown";
    }
}

static int is_rtsp_url(const char *pszSrc) {
    return (strncmp(pszSrc, "rtsp://", 7) == 0) || (strncmp(pszSrc, "rtmp://", 7) == 0) ||
        (strncmp(pszSrc, "http://", 7) == 0);
}

static const char *codec_file_ext(DemuxCodecType eIn) {
    switch (eIn) {
        case DEMUX_CODEC_H264:
            return "h264";
        case DEMUX_CODEC_H265:
            return "h265";
        case DEMUX_CODEC_MJPEG:
            return "mjpeg";
        default:
            return "bin";
    }
}

/*
 * Forward each demux packet:
 *   - relay: push to the RTSP muxer (MUX_SendPacket);
 *   - record: write the raw annexb bytes straight to the file.
 * Returns non-zero so demux skips its internal bind-forward SYS_SendStream
 * (the source node is intentionally left unbound here).
 */
static S32 on_packet(S32 s32Chn, const DemuxPacket *pstPkt, VOID *pvUser) {
    RelayCtx *ctx = (RelayCtx *)pvUser;
    MuxPacket stMux;
    (void)s32Chn;

    if (!g_running) {
        return -1;
    }
    if (pstPkt == NULL || pstPkt->pu8Data == NULL || pstPkt->u32Size == 0) {
        return 1;
    }

    if (ctx->relay_ready) {
        memset(&stMux, 0, sizeof(stMux));
        stMux.pu8Data = pstPkt->pu8Data;
        stMux.u32Size = pstPkt->u32Size;
        stMux.bKeyFrame = pstPkt->bKeyFrame;
        stMux.eCodecType = demux_to_mux_codec(pstPkt->eCodecType);
        stMux.u64PTS = pstPkt->u64PTS;
        if (MUX_SendPacket(MUX_RELAY_CHN, &stMux) == ERR_MUX_OK) {
            ctx->relay_sent++;
        } else {
            ctx->relay_err++;
        }
    }

    if (ctx->fpRec != NULL) {
        size_t wr = fwrite(pstPkt->pu8Data, 1, pstPkt->u32Size, ctx->fpRec);
        ctx->record_bytes += (int)wr;
    }

    ctx->forwarded++;
    if (ctx->forwarded % 100 == 0) {
        printf("  forwarded %d pkts (relay=%d, record=%d bytes)\n",
            ctx->forwarded,
            ctx->relay_sent,
            ctx->record_bytes);
    }
    if (ctx->max_frames > 0 && ctx->forwarded >= ctx->max_frames) {
        g_running = 0;
        return -1;
    }
    /* Non-zero: tell demux NOT to also bind-forward this packet. */
    return 1;
}

static S32 build_relay_mux(const char *pszUrl, const DemuxStreamInfo *pstInfo) {
    MuxChnAttr stAttr;
    S32 ret;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eOutputType = MUX_OUTPUT_RTSP;
    snprintf(stAttr.szUrl, sizeof(stAttr.szUrl), "%s", pszUrl);
    stAttr.stStreamAttr.eCodecType = demux_to_mux_codec(pstInfo->eCodecType);
    stAttr.stStreamAttr.u32Width = pstInfo->u32Width;
    stAttr.stStreamAttr.u32Height = pstInfo->u32Height;
    stAttr.stStreamAttr.u32Fps = pstInfo->u32Fps > 0 ? pstInfo->u32Fps : 30;

    ret = MUX_CreateChn(MUX_RELAY_CHN, &stAttr);
    if (ret != ERR_MUX_OK) {
        fprintf(stderr, "MUX_CreateChn(relay) failed: %d\n", ret);
        return ret;
    }
    ret = MUX_StartChn(MUX_RELAY_CHN);
    if (ret != ERR_MUX_OK) {
        fprintf(stderr, "MUX_StartChn(relay) failed: %d\n", ret);
        MUX_DestroyChn(MUX_RELAY_CHN);
    }
    return ret;
}

static void usage(const char *prog) {
    printf("Usage: %s <source> [--port N] [--rec PATH] [--frames N] [--no-relay] [--no-record]\n", prog);
    printf("  <source>     RTSP url (rtsp://...) OR local container file (.mp4/.ts/.flv)\n");
    printf("  --port N     relay RTSP port (default %d)\n", DEFAULT_RTSP_PORT);
    printf("  --rec PATH   record file path WITHOUT extension; codec ext added\n");
    printf("               automatically, e.g. '%s' -> '%s.h264' (default)\n", DEFAULT_REC_PATH, DEFAULT_REC_PATH);
    printf("  --frames N   stop after N packets (default: run until Ctrl+C)\n");
    printf("  --no-relay   disable RTSP re-publish (record only)\n");
    printf("  --no-record  disable local recording (relay only)\n");
}

int main(int argc, char *argv[]) {
    const char *pszSrc = NULL;
    const char *pszRec = DEFAULT_REC_PATH;
    int port = DEFAULT_RTSP_PORT;
    int max_frames = 0;
    int want_relay = 1;
    int want_record = 1;
    char szRelayUrl[128];
    char szRecPath[300];
    int i;
    S32 ret;
    S32 demux_started = 0;
    DemuxChnAttr stDemuxAttr;
    DemuxStreamInfo stInfo;
    RelayCtx stCtx;

    memset(&stCtx, 0, sizeof(stCtx));

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--rec") == 0 && i + 1 < argc) {
            pszRec = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-relay") == 0) {
            want_relay = 0;
        } else if (strcmp(argv[i], "--no-record") == 0) {
            want_record = 0;
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            return 0;
        } else if (pszSrc == NULL) {
            pszSrc = argv[i];
        }
    }
    if (pszSrc == NULL) {
        usage(argv[0]);
        return 1;
    }
    if (!want_relay && !want_record) {
        fprintf(stderr, "Nothing to do: both relay and record disabled\n");
        return 1;
    }
    stCtx.max_frames = max_frames;
    snprintf(szRelayUrl, sizeof(szRelayUrl), "rtsp://0.0.0.0:%d/relay", port);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Sample: RTSP Relay + Record ===\n");
    printf("  Source : %s\n", pszSrc);
    if (want_relay) {
        printf("  Relay  : rtsp://<board-ip>:%d/relay\n", port);
    }
    if (want_record) {
        printf("  Record : %s.<codec>\n", pszRec);
    }
    printf("\n");

    ret = SYS_Init();
    if (ret != 0) {
        fprintf(stderr, "SYS_Init failed: %d\n", ret);
        return 1;
    }

    /* ---- DEMUX ---- */
    ret = DEMUX_Init();
    if (ret != ERR_DEMUX_OK) {
        fprintf(stderr, "DEMUX_Init failed: %d\n", ret);
        goto cleanup_sys;
    }

    memset(&stDemuxAttr, 0, sizeof(stDemuxAttr));
    stDemuxAttr.eInputType = is_rtsp_url(pszSrc) ? DEMUX_INPUT_RTSP : DEMUX_INPUT_FILE;
    snprintf(stDemuxAttr.szUrl, sizeof(stDemuxAttr.szUrl), "%s", pszSrc);
    stDemuxAttr.bPreferTcp = MPP_TRUE;
    stDemuxAttr.bLowLatency = MPP_TRUE;
    stDemuxAttr.u32OpenTimeoutMs = 5000;
    stDemuxAttr.u32RwTimeoutMs = 5000;

    ret = DEMUX_CreateChn(DEMUX_CHN, &stDemuxAttr);
    if (ret != ERR_DEMUX_OK) {
        fprintf(stderr, "DEMUX_CreateChn failed: %d\n", ret);
        goto cleanup_demux;
    }

    memset(&stInfo, 0, sizeof(stInfo));
    ret = DEMUX_GetStreamInfo(DEMUX_CHN, &stInfo);
    if (ret != ERR_DEMUX_OK) {
        fprintf(stderr, "DEMUX_GetStreamInfo failed: %d\n", ret);
        goto cleanup_demux_chn;
    }
    printf("Source stream: %s %ux%u @%ufps\n",
        mux_codec_name(demux_to_mux_codec(stInfo.eCodecType)),
        stInfo.u32Width,
        stInfo.u32Height,
        stInfo.u32Fps);

    /* ---- record FILE (raw elementary stream) ---- */
    if (want_record) {
        snprintf(szRecPath, sizeof(szRecPath), "%s.%s", pszRec, codec_file_ext(stInfo.eCodecType));
        stCtx.fpRec = fopen(szRecPath, "wb");
        if (stCtx.fpRec == NULL) {
            fprintf(stderr, "WARNING: cannot open record file '%s': %s\n", szRecPath, strerror(errno));
        } else {
            printf("Record file -> %s\n", szRecPath);
        }
    }

    /* ---- relay MUX (RTSP out only) ---- */
    if (want_relay) {
        ret = MUX_Init();
        if (ret != ERR_MUX_OK) {
            fprintf(stderr, "MUX_Init failed: %d\n", ret);
            goto cleanup_rec;
        }
        if (build_relay_mux(szRelayUrl, &stInfo) == ERR_MUX_OK) {
            stCtx.relay_ready = 1;
            printf("Relay MUX started -> %s\n", szRelayUrl);
        } else {
            fprintf(stderr, "WARNING: relay MUX disabled\n");
        }
    }

    if (!stCtx.relay_ready && stCtx.fpRec == NULL) {
        fprintf(stderr, "No output available, aborting\n");
        ret = -1;
        goto cleanup_mux;
    }

    /* ---- wire DEMUX callback and start ---- */
    ret = DEMUX_SetPacketCallback(DEMUX_CHN, on_packet, &stCtx);
    if (ret != ERR_DEMUX_OK) {
        fprintf(stderr, "DEMUX_SetPacketCallback failed: %d\n", ret);
        goto cleanup_mux;
    }
    ret = DEMUX_StartChn(DEMUX_CHN);
    if (ret != ERR_DEMUX_OK) {
        fprintf(stderr, "DEMUX_StartChn failed: %d\n", ret);
        goto cleanup_mux;
    }
    demux_started = 1;

    printf("\nRelaying... (Ctrl+C to stop)\n");
    while (g_running) {
        usleep(200 * 1000);
    }

    printf("\nDone: forwarded %d pkts (relay=%d sent / %d err, record=%d bytes)\n",
        stCtx.forwarded,
        stCtx.relay_sent,
        stCtx.relay_err,
        stCtx.record_bytes);
    ret = (stCtx.forwarded > 0) ? 0 : 1;

cleanup_mux:
    if (demux_started) {
        DEMUX_StopChn(DEMUX_CHN);
        demux_started = 0;
    }
    if (want_relay) {
        if (stCtx.relay_ready) {
            MUX_StopChn(MUX_RELAY_CHN);
            MUX_DestroyChn(MUX_RELAY_CHN);
        }
        MUX_Exit();
    }

cleanup_rec:
    if (stCtx.fpRec != NULL) {
        fclose(stCtx.fpRec);
        stCtx.fpRec = NULL;
    }

cleanup_demux_chn:
    if (demux_started) {
        DEMUX_StopChn(DEMUX_CHN);
    }
    DEMUX_DestroyChn(DEMUX_CHN);

cleanup_demux:
    DEMUX_Exit();

cleanup_sys:
    SYS_Exit();

    printf("\n=== Sample finished ===\n");
    return ret;
}
