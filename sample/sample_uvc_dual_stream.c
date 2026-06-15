/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    sample_uvc_dual_stream.c
 * @Date      :    2026-06-05
 * @Brief     :    Sample: one UVC camera -> main + sub streams (dual encode).
 *
 *                 A very common surveillance / vision-edge layout, here driven
 *                 by a real USB (UVC) camera instead of a CSI sensor:
 *
 *                   UVC(MJPEG) -> VDEC -> NV12 frame
 *                                          |--> VENC0 H.264 -> main.h264
 *                                          |--> VENC1 H.265 -> sub.h265
 *
 *                 The UVC module exposes a single capture channel per device,
 *                 so the "dual stream" is produced by decoding the camera's
 *                 MJPEG once and fanning the decoded NV12 frame out to two
 *                 encoders (the high-quality archive feed + a lighter feed for
 *                 preview / AI inference). Both encoders run at the camera's
 *                 native resolution; they differ in codec and bitrate.
 *
 * Run:
 *   ./sample_uvc_dual_stream                              # 120 frames, defaults
 *   ./sample_uvc_dual_stream 300                          # 300 frames
 *   ./sample_uvc_dual_stream 300 /dev/video13 main.h264 sub.h265
 *
 * Play: ffplay main.h264   /   ffplay sub.h265
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sys_api.h"
#include "vb_api.h"
#include "uvc_api.h"
#include "vdec/vdec_api.h"
#include "venc/venc_api.h"
#include "venc/venc_type.h"

#define UVC_DEV_ID 0
#define UVC_CHN_ID 0
#define VDEC_CHN_ID 0
#define MAIN_VENC_CHN 0
#define SUB_VENC_CHN 1
#define CAP_WIDTH 1280
#define CAP_HEIGHT 720
#define MAIN_BITRATE_KBPS 4000
#define SUB_BITRATE_KBPS 1500
#define DEFAULT_FPS 30
#define DEFAULT_GOP 30
#define DEFAULT_FRAMES 120
#define WARMUP_FRAMES 2

typedef struct StreamDesc {
    const char *pszTag;
    S32 s32VencChn;
    MppStreamCodecType eCodec;
    U32 u32BitrateKbps;
    FILE *fp;
    int saved;
    char szOutPath[128];
} StreamDesc;

static const char *g_devNode = "/dev/video13";
static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static const char *codec_name(MppStreamCodecType eCodec) {
    switch (eCodec) {
        case MPP_STREAM_CODEC_H264:
            return "H.264";
        case MPP_STREAM_CODEC_H265:
            return "H.265";
        default:
            return "?";
    }
}

static S32 setup_venc_chn(const StreamDesc *pstDesc) {
    VencChnAttr stAttr;
    S32 ret;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eCodecType = pstDesc->eCodec;
    stAttr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    stAttr.u32Width = CAP_WIDTH;
    stAttr.u32Height = CAP_HEIGHT;
    stAttr.eRcMode = VENC_RC_MODE_CBR;
    stAttr.u32Bitrate = pstDesc->u32BitrateKbps * 1000;
    stAttr.u32FrameRate = DEFAULT_FPS;
    stAttr.u32Gop = DEFAULT_GOP;

    ret = VENC_CreateChn(pstDesc->s32VencChn, &stAttr);
    if (ret != 0) {
        fprintf(stderr, "VENC_CreateChn(%s) failed: %d\n", pstDesc->pszTag, ret);
        return ret;
    }
    ret = VENC_EnableChn(pstDesc->s32VencChn);
    if (ret != 0) {
        fprintf(stderr, "VENC_EnableChn(%s) failed: %d\n", pstDesc->pszTag, ret);
        VENC_DestroyChn(pstDesc->s32VencChn);
    }
    return ret;
}

/* Drain whatever encoded frames are ready on one VENC channel to its file. */
static void drain_venc(StreamDesc *pstDesc) {
    StreamBufferInfo stream;
    S32 ret;

    while (1) {
        memset(&stream, 0, sizeof(stream));
        ret = VENC_GetStream(pstDesc->s32VencChn, &stream, 0);
        if (ret != ERR_VENC_OK) {
            break;
        }
        if (fwrite(stream.pu8Addr, 1, stream.u32Size, pstDesc->fp) != stream.u32Size) {
            fprintf(stderr, "[%s] short write\n", pstDesc->pszTag);
        }
        VENC_ReleaseStream(pstDesc->s32VencChn, &stream);
        pstDesc->saved++;
        if (pstDesc->saved % 30 == 0) {
            printf("  [%s] saved %d frames\n", pstDesc->pszTag, pstDesc->saved);
        }
    }
}

static void usage(const char *prog) {
    printf("Usage: %s [frame_count] [dev_node] [main_out] [sub_out]\n", prog);
    printf("  frame_count  UVC frames to process (default %d)\n", DEFAULT_FRAMES);
    printf("  dev_node     UVC device node (default %s)\n", g_devNode);
    printf("  main_out     main H.264 file (default ./main.h264)\n");
    printf("  sub_out      sub  H.265 file (default ./sub.h265)\n");
}

int main(int argc, char *argv[]) {
    int frame_count = DEFAULT_FRAMES;
    S32 ret;
    S32 i;
    S32 uvc_dev_on = 0;
    S32 uvc_chn_on = 0;
    S32 vdec_on = 0;
    S32 venc_ready_cnt = 0;
    int processed = 0;
    StreamDesc astDesc[2];
    VideoFrameInfo uvcFrame;
    VideoFrameInfo decFrame;
    StreamBufferInfo inStream;

    memset(astDesc, 0, sizeof(astDesc));

    if (argc > 1) {
        if (argv[1][0] == '-') {
            usage(argv[0]);
            return 0;
        }
        frame_count = atoi(argv[1]);
        if (frame_count <= 0) {
            usage(argv[0]);
            return 1;
        }
    }
    if (argc > 2) {
        g_devNode = argv[2];
    }

    astDesc[0].pszTag = "main";
    astDesc[0].s32VencChn = MAIN_VENC_CHN;
    astDesc[0].eCodec = MPP_STREAM_CODEC_H264;
    astDesc[0].u32BitrateKbps = MAIN_BITRATE_KBPS;
    snprintf(astDesc[0].szOutPath, sizeof(astDesc[0].szOutPath), "%s", (argc > 3) ? argv[3] : "./main.h264");

    astDesc[1].pszTag = "sub";
    astDesc[1].s32VencChn = SUB_VENC_CHN;
    astDesc[1].eCodec = MPP_STREAM_CODEC_H265;
    astDesc[1].u32BitrateKbps = SUB_BITRATE_KBPS;
    snprintf(astDesc[1].szOutPath, sizeof(astDesc[1].szOutPath), "%s", (argc > 4) ? argv[4] : "./sub.h265");

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Sample: UVC Dual-Stream (main + sub) ===\n");
    printf("  Source: %s  %ux%u MJPEG\n", g_devNode, CAP_WIDTH, CAP_HEIGHT);
    for (i = 0; i < 2; i++) {
        printf("  %-4s: %s @ %u kbps -> %s\n",
            astDesc[i].pszTag,
            codec_name(astDesc[i].eCodec),
            astDesc[i].u32BitrateKbps,
            astDesc[i].szOutPath);
    }
    printf("\n");

    if (access(g_devNode, F_OK) != 0) {
        fprintf(stderr, "UVC device %s not found\n", g_devNode);
        return 1;
    }

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

    /* ---- UVC device + channel (MJPEG capture) ---- */
    ret = UVC_Init();
    if (ret != 0) {
        fprintf(stderr, "UVC_Init failed: %d\n", ret);
        goto cleanup_vb;
    }

    UvcDevAttr stDevAttr;
    memset(&stDevAttr, 0, sizeof(stDevAttr));
    strncpy(stDevAttr.acDevNode, g_devNode, sizeof(stDevAttr.acDevNode) - 1);
    ret = UVC_CreateDev(UVC_DEV_ID, &stDevAttr);
    if (ret != 0) {
        fprintf(stderr, "UVC_CreateDev failed: %d\n", ret);
        goto cleanup_uvc_init;
    }
    uvc_dev_on = 1;

    ret = UVC_EnableDev(UVC_DEV_ID);
    if (ret != 0) {
        fprintf(stderr, "UVC_EnableDev failed: %d\n", ret);
        goto cleanup_uvc_dev;
    }

    UvcChnAttr stChnAttr;
    memset(&stChnAttr, 0, sizeof(stChnAttr));
    stChnAttr.u32Width = CAP_WIDTH;
    stChnAttr.u32Height = CAP_HEIGHT;
    stChnAttr.ePixelFormat = MPP_PIXEL_FORMAT_MJPEG;
    stChnAttr.u32Fps = DEFAULT_FPS;
    stChnAttr.u32Depth = 1;
    ret = UVC_SetChnAttr(UVC_DEV_ID, UVC_CHN_ID, &stChnAttr);
    if (ret != 0) {
        fprintf(stderr, "UVC_SetChnAttr failed: %d\n", ret);
        goto cleanup_uvc_dev;
    }
    ret = UVC_EnableChn(UVC_DEV_ID, UVC_CHN_ID);
    if (ret != 0) {
        fprintf(stderr, "UVC_EnableChn failed: %d\n", ret);
        goto cleanup_uvc_dev;
    }
    uvc_chn_on = 1;
    printf("UVC enabled: %s %ux%u MJPEG\n", g_devNode, CAP_WIDTH, CAP_HEIGHT);

    /* ---- VDEC: MJPEG -> NV12 ---- */
    ret = VDEC_Init();
    if (ret != 0) {
        fprintf(stderr, "VDEC_Init failed: %d\n", ret);
        goto cleanup_uvc_chn;
    }

    VdecChnAttr stVdecAttr;
    memset(&stVdecAttr, 0, sizeof(stVdecAttr));
    stVdecAttr.eCodecType = MPP_STREAM_CODEC_MJPEG;
    stVdecAttr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    stVdecAttr.u32Width = CAP_WIDTH;
    stVdecAttr.u32Height = CAP_HEIGHT;
    ret = VDEC_CreateChn(VDEC_CHN_ID, &stVdecAttr);
    if (ret != 0) {
        fprintf(stderr, "VDEC_CreateChn failed: %d\n", ret);
        goto cleanup_vdec_init;
    }
    ret = VDEC_EnableChn(VDEC_CHN_ID);
    if (ret != 0) {
        fprintf(stderr, "VDEC_EnableChn failed: %d\n", ret);
        VDEC_DestroyChn(VDEC_CHN_ID);
        goto cleanup_vdec_init;
    }
    vdec_on = 1;
    printf("VDEC enabled: MJPEG -> NV12 %ux%u\n", CAP_WIDTH, CAP_HEIGHT);

    /* ---- VENC (two encoders) ---- */
    ret = VENC_Init();
    if (ret != 0) {
        fprintf(stderr, "VENC_Init failed: %d\n", ret);
        goto cleanup_vdec;
    }
    for (i = 0; i < 2; i++) {
        if (setup_venc_chn(&astDesc[i]) != 0) {
            goto cleanup_venc;
        }
        venc_ready_cnt++;
        printf("VENC chn%d enabled: %s\n", astDesc[i].s32VencChn, codec_name(astDesc[i].eCodec));
    }

    /* ---- output files ---- */
    for (i = 0; i < 2; i++) {
        astDesc[i].fp = fopen(astDesc[i].szOutPath, "wb");
        if (astDesc[i].fp == NULL) {
            fprintf(stderr, "fopen(%s) failed: %s\n", astDesc[i].szOutPath, strerror(errno));
            ret = -1;
            goto cleanup_files;
        }
    }

    /* ---- warm-up: discard first UVC frames ---- */
    for (i = 0; i < WARMUP_FRAMES; i++) {
        memset(&uvcFrame, 0, sizeof(uvcFrame));
        if (UVC_GetFrame(UVC_DEV_ID, UVC_CHN_ID, &uvcFrame, 3000) == 0) {
            UVC_ReleaseFrame(UVC_DEV_ID, UVC_CHN_ID, &uvcFrame);
        }
    }

    /* ---- main loop: UVC -> VDEC -> fan-out to two VENC ---- */
    printf("Capturing... (Ctrl+C to stop)\n");
    while (g_running && processed < frame_count) {
        memset(&uvcFrame, 0, sizeof(uvcFrame));
        ret = UVC_GetFrame(UVC_DEV_ID, UVC_CHN_ID, &uvcFrame, 3000);
        if (ret != 0) {
            if (!g_running) {
                break;
            }
            continue;
        }
        if (uvcFrame.stVFrame.ulPlaneVirAddr[0] == 0 || uvcFrame.stVFrame.u32PlaneSizeValid[0] == 0) {
            UVC_ReleaseFrame(UVC_DEV_ID, UVC_CHN_ID, &uvcFrame);
            continue;
        }

        memset(&inStream, 0, sizeof(inStream));
        inStream.pu8Addr = (const U8 *)uvcFrame.stVFrame.ulPlaneVirAddr[0];
        inStream.u32Size = uvcFrame.stVFrame.u32PlaneSizeValid[0];
        inStream.eCodecType = MPP_STREAM_CODEC_MJPEG;
        inStream.bKeyFrame = MPP_TRUE;
        inStream.u64PTS = uvcFrame.stVFrame.u64PTS;
        ret = VDEC_SendStream(VDEC_CHN_ID, &inStream, 0);
        UVC_ReleaseFrame(UVC_DEV_ID, UVC_CHN_ID, &uvcFrame);
        if (ret != 0 && ret != ERR_VDEC_EOS) {
            continue;
        }

        memset(&decFrame, 0, sizeof(decFrame));
        ret = VDEC_GetFrame(VDEC_CHN_ID, &decFrame, 1000);
        if (ret == ERR_VDEC_NO_FRAME || ret == ERR_VDEC_TIMEOUT) {
            continue;
        }
        if (ret == ERR_VDEC_EOS) {
            break;
        }
        if (ret != ERR_VDEC_OK) {
            continue;
        }

        /* fan-out the same NV12 frame to both encoders */
        for (i = 0; i < 2; i++) {
            ret = VENC_SendFrame(astDesc[i].s32VencChn, &decFrame, 0);
            if (ret != 0) {
                fprintf(stderr, "[%s] VENC_SendFrame ret=%d\n", astDesc[i].pszTag, ret);
            }
        }
        VDEC_ReleaseFrame(VDEC_CHN_ID, decFrame.ulBufferId);
        processed++;

        for (i = 0; i < 2; i++) {
            drain_venc(&astDesc[i]);
        }
    }

    /* final drain */
    for (i = 0; i < 2; i++) {
        drain_venc(&astDesc[i]);
    }

    printf("\nAll streams done (%d frames processed):\n", processed);
    ret = 0;
    for (i = 0; i < 2; i++) {
        printf("  %-4s: %d frames -> %s\n", astDesc[i].pszTag, astDesc[i].saved, astDesc[i].szOutPath);
        if (astDesc[i].saved == 0) {
            ret = 1;
        }
    }

cleanup_files:
    for (i = 0; i < 2; i++) {
        if (astDesc[i].fp) {
            fclose(astDesc[i].fp);
            astDesc[i].fp = NULL;
        }
    }

cleanup_venc:
    for (i = 0; i < venc_ready_cnt; i++) {
        VENC_DisableChn(astDesc[i].s32VencChn);
        VENC_DestroyChn(astDesc[i].s32VencChn);
    }
    VENC_Exit();

cleanup_vdec:
    if (vdec_on) {
        VDEC_DisableChn(VDEC_CHN_ID);
        VDEC_DestroyChn(VDEC_CHN_ID);
    }

cleanup_vdec_init:
    VDEC_Exit();

cleanup_uvc_chn:
    if (uvc_chn_on) {
        UVC_DisableChn(UVC_DEV_ID, UVC_CHN_ID);
    }

cleanup_uvc_dev:
    if (uvc_dev_on) {
        UVC_DisableDev(UVC_DEV_ID);
        UVC_DestroyDev(UVC_DEV_ID);
    }

cleanup_uvc_init:
    UVC_Exit();

cleanup_vb:
    VB_Exit();

cleanup_sys:
    SYS_Exit();

    printf("\n=== Sample finished ===\n");
    return ret;
}
