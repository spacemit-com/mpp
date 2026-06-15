/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    sample_multi_decode.c
 * @Date      :    2026-05-20
 * @Brief     :    Sample: N-channel parallel decode (the "video wall" backend).
 *
 *                 For each input source a worker thread runs an independent
 *                 pipeline using compressed-domain bind:
 *
 *                     DEMUX (mp4/ts/flv file or RTSP) --SYS_Bind--> VDEC
 *
 *                 DEMUX is a bind-first module: once started it pushes packets
 *                 straight into the bound VDEC channel (SYS_SendStream), so the
 *                 worker only has to pull decoded NV12 frames with VDEC_GetFrame
 *                 and count them. Inputs can be mixed (some local container
 *                 files, some live RTSP cameras) and every channel decodes
 *                 concurrently on its own VDEC channel. This is the typical
 *                 front-end of an NVR / multi-camera AI ingest where the frames
 *                 would then go to inference or a compositor.
 *
 * Run:
 *   ./sample_multi_decode in0.mp4 in1.ts
 *   ./sample_multi_decode rtsp://camA/live rtsp://camB/live file.mp4
 *   ./sample_multi_decode --frames 200 a.mp4 b.ts
 *
 * Note: DEMUX consumes containers (MP4/TS/FLV) and RTSP/RTMP/HLS URLs.
 *       Raw elementary streams (.264/.265 annex-b) are not container inputs.
 *
 * Up to MAX_CHN channels.
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "demux/demux_api.h"
#include "demux/demux_type.h"
#include "sys/sys_api.h"
#include "sys/vb_api.h"
#include "vdec/vdec_api.h"
#include "vdec/vdec_type.h"

#define MAX_CHN 8
#define MAX_DECODE_W 1920
#define MAX_DECODE_H 1088

typedef struct ChnCtx {
    int index;
    const char *url;
    int is_rtsp;
    int max_frames;
    int decoded;    /* frames pulled from VDEC */
    int bound;      /* DEMUX src bound to VDEC sink */
    int started;    /* DEMUX started */
    int vdec_ready; /* VDEC channel created+enabled */
    int eos;
    MppNode stDemuxSrc;
    MppNode stVdecSink;
    pthread_t tid;
} ChnCtx;

static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static int is_rtsp_url(const char *url) {
    return (strncmp(url, "rtsp://", 7) == 0) ? 1 : 0;
}

static MppStreamCodecType demux_to_stream_codec(DemuxCodecType eIn) {
    switch (eIn) {
        case DEMUX_CODEC_H264:
            return MPP_STREAM_CODEC_H264;
        case DEMUX_CODEC_H265:
            return MPP_STREAM_CODEC_H265;
        case DEMUX_CODEC_MJPEG:
            return MPP_STREAM_CODEC_MJPEG;
        default:
            return MPP_STREAM_CODEC_UNKNOWN;
    }
}

static S32 build_vdec(ChnCtx *ctx, const DemuxStreamInfo *pstInfo) {
    VdecChnAttr stAttr;
    S32 ret;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eCodecType = demux_to_stream_codec(pstInfo->eCodecType);
    stAttr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    stAttr.u32Align = 16;
    stAttr.u32Width = (pstInfo->u32Width > 0) ? pstInfo->u32Width : MAX_DECODE_W;
    stAttr.u32Height = (pstInfo->u32Height > 0) ? pstInfo->u32Height : MAX_DECODE_H;

    ret = VDEC_CreateChn(ctx->index, &stAttr);
    if (ret != ERR_VDEC_OK) {
        fprintf(stderr, "[chn%d] VDEC_CreateChn failed: %d\n", ctx->index, ret);
        return ret;
    }
    ret = VDEC_EnableChn(ctx->index);
    if (ret != ERR_VDEC_OK) {
        fprintf(stderr, "[chn%d] VDEC_EnableChn failed: %d\n", ctx->index, ret);
        VDEC_DestroyChn(ctx->index);
    }
    return ret;
}

/* One worker per input source: DEMUX --bind--> VDEC, then drain frames. */
static void *channel_thread(void *arg) {
    ChnCtx *ctx = (ChnCtx *)arg;
    DemuxChnAttr stDemuxAttr;
    DemuxStreamInfo stInfo;
    VideoFrameInfo stFrame;
    S32 ret;
    int idle = 0;

    memset(&stDemuxAttr, 0, sizeof(stDemuxAttr));
    stDemuxAttr.eInputType = ctx->is_rtsp ? DEMUX_INPUT_RTSP : DEMUX_INPUT_FILE;
    snprintf(stDemuxAttr.szUrl, sizeof(stDemuxAttr.szUrl), "%s", ctx->url);
    stDemuxAttr.bPreferTcp = MPP_TRUE;
    stDemuxAttr.bInjectPS = MPP_TRUE;
    stDemuxAttr.u32OpenTimeoutMs = 5000;
    stDemuxAttr.u32RwTimeoutMs = 5000;

    ret = DEMUX_CreateChn(ctx->index, &stDemuxAttr);
    if (ret != ERR_DEMUX_OK) {
        fprintf(stderr, "[chn%d] DEMUX_CreateChn(%s) failed: %d\n", ctx->index, ctx->url, ret);
        return NULL;
    }

    memset(&stInfo, 0, sizeof(stInfo));
    ret = DEMUX_GetStreamInfo(ctx->index, &stInfo);
    if (ret != ERR_DEMUX_OK) {
        fprintf(stderr, "[chn%d] DEMUX_GetStreamInfo failed: %d\n", ctx->index, ret);
        goto out_demux;
    }
    printf("[chn%d] %s : %ux%u @%ufps codec=%d\n", ctx->index, ctx->url, stInfo.u32Width, stInfo.u32Height,
        stInfo.u32Fps, stInfo.eCodecType);

    if (build_vdec(ctx, &stInfo) != ERR_VDEC_OK) {
        goto out_demux;
    }
    ctx->vdec_ready = 1;

    /* Compressed-domain bind: DEMUX feeds the VDEC automatically once started. */
    ret = DEMUX_GetSrcNode(ctx->index, &ctx->stDemuxSrc);
    if (ret != ERR_DEMUX_OK) {
        fprintf(stderr, "[chn%d] DEMUX_GetSrcNode failed: %d\n", ctx->index, ret);
        goto out_vdec;
    }
    ctx->stVdecSink = (MppNode){MPP_ID_VDEC, 0, ctx->index};
    ret = SYS_Bind(&ctx->stDemuxSrc, &ctx->stVdecSink);
    if (ret != 0) {
        fprintf(stderr, "[chn%d] SYS_Bind failed: %d\n", ctx->index, ret);
        goto out_vdec;
    }
    ctx->bound = 1;

    ret = DEMUX_StartChn(ctx->index);
    if (ret != ERR_DEMUX_OK) {
        fprintf(stderr, "[chn%d] DEMUX_StartChn failed: %d\n", ctx->index, ret);
        goto out_unbind;
    }
    ctx->started = 1;

    /* Pull decoded frames until EOS or target reached. */
    while (g_running) {
        memset(&stFrame, 0, sizeof(stFrame));
        ret = VDEC_GetFrame(ctx->index, &stFrame, 200);
        if (ret == ERR_VDEC_OK) {
            ctx->decoded++;
            idle = 0;
            if (ctx->decoded % 60 == 0) {
                printf("[chn%d] decoded %d frames\n", ctx->index, ctx->decoded);
            }
            VDEC_ReleaseFrame(ctx->index, stFrame.ulBufferId);
            if (ctx->max_frames > 0 && ctx->decoded >= ctx->max_frames) {
                break;
            }
        } else if (ret == ERR_VDEC_EOS) {
            ctx->eos = 1;
            break;
        } else {
            /* NO_FRAME / TIMEOUT: file inputs drain after EOS; bail when idle. */
            if (++idle > 50 && ctx->decoded > 0 && !ctx->is_rtsp) {
                break;
            }
        }
    }

out_unbind:
    if (ctx->started) {
        DEMUX_StopChn(ctx->index);
        ctx->started = 0;
    }
    if (ctx->bound) {
        SYS_UnBind(&ctx->stDemuxSrc, &ctx->stVdecSink);
        ctx->bound = 0;
    }
out_vdec:
    if (ctx->vdec_ready) {
        VDEC_DisableChn(ctx->index);
        VDEC_DestroyChn(ctx->index);
        ctx->vdec_ready = 0;
    }
out_demux:
    DEMUX_DestroyChn(ctx->index);
    printf("[chn%d] finished: decoded=%d%s\n", ctx->index, ctx->decoded, ctx->eos ? " (eos)" : "");
    return NULL;
}

static void usage(const char *prog) {
    printf("Usage: %s [--frames N] <input0> [input1] ... (max %d)\n", prog, MAX_CHN);
    printf("  inputs may be container files (mp4/ts/flv) or rtsp:// URLs (mixed allowed)\n");
    printf("  --frames N   per-channel decode cap (default: until EOS)\n");
}

int main(int argc, char *argv[]) {
    ChnCtx ctx[MAX_CHN];
    int nchn = 0;
    int max_frames = 0;
    int i;
    int total_decoded = 0;
    S32 ret;

    memset(ctx, 0, sizeof(ctx));

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            max_frames = atoi(argv[++i]);
        } else if (argv[i][0] == '-') {
            usage(argv[0]);
            return 1;
        } else if (nchn < MAX_CHN) {
            ctx[nchn].index = nchn;
            ctx[nchn].url = argv[i];
            ctx[nchn].is_rtsp = is_rtsp_url(argv[i]);
            nchn++;
        } else {
            fprintf(stderr, "Too many inputs, max %d\n", MAX_CHN);
        }
    }
    if (nchn == 0) {
        usage(argv[0]);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Sample: %d-Channel Parallel Decode ===\n", nchn);
    for (i = 0; i < nchn; i++) {
        ctx[i].max_frames = max_frames;
        printf("  chn%d: %s (%s)\n", i, ctx[i].url, ctx[i].is_rtsp ? "RTSP" : "FILE");
    }
    printf("\n");

    ret = SYS_Init();
    if (ret != 0) {
        fprintf(stderr, "SYS_Init failed: %d\n", ret);
        return 1;
    }
    ret = VB_Init();
    if (ret != 0) {
        fprintf(stderr, "VB_Init failed: %d\n", ret);
        goto cleanup_sys;
    }
    ret = DEMUX_Init();
    if (ret != ERR_DEMUX_OK) {
        fprintf(stderr, "DEMUX_Init failed: %d\n", ret);
        goto cleanup_vb;
    }
    ret = VDEC_Init();
    if (ret != ERR_VDEC_OK) {
        fprintf(stderr, "VDEC_Init failed: %d\n", ret);
        goto cleanup_demux;
    }

    for (i = 0; i < nchn; i++) {
        if (pthread_create(&ctx[i].tid, NULL, channel_thread, &ctx[i]) != 0) {
            fprintf(stderr, "pthread_create(chn%d) failed\n", i);
            ctx[i].tid = 0;
        }
    }

    printf("Decoding... (Ctrl+C to stop)\n");
    for (i = 0; i < nchn; i++) {
        if (ctx[i].tid) {
            pthread_join(ctx[i].tid, NULL);
        }
    }

    printf("\n=== Summary ===\n");
    for (i = 0; i < nchn; i++) {
        printf("  chn%d: decoded=%d\n", i, ctx[i].decoded);
        total_decoded += ctx[i].decoded;
    }
    printf("  total decoded frames: %d\n", total_decoded);
    ret = (total_decoded > 0) ? 0 : 1;

    VDEC_Exit();
cleanup_demux:
    DEMUX_Exit();
cleanup_vb:
    VB_Exit();
cleanup_sys:
    SYS_Exit();

    printf("\n=== Sample finished ===\n");
    return ret;
}
