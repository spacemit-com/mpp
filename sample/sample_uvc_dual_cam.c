/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    sample_uvc_dual_cam.c
 * @Date      :    2026-06-05
 * @Brief     :    Sample: TWO UVC cameras captured & encoded in parallel.
 *
 *                 A multi-camera vision-edge layout. Two independent USB (UVC)
 *                 webcams are opened at the same time, each running its own
 *                 capture -> decode -> encode pipeline:
 *
 *                   cam0 /dev/video13 : UVC(MJPEG) -> VDEC0 -> VENC0 H.264 -> cam0.h264
 *                   cam1 /dev/video15 : UVC(MJPEG) -> VDEC1 -> VENC1 H.264 -> cam1.h264
 *
 *                 Each camera owns a dedicated UVC device index, VDEC channel
 *                 and VENC channel, so the two streams are fully isolated and
 *                 run concurrently. The main loop round-robins between the two
 *                 cameras: grab one MJPEG frame from each, decode it to NV12,
 *                 feed the encoder and drain the encoded bitstream to file.
 *
 *                 This is the natural "two cameras plugged in" scenario, e.g.
 *                 stereo capture, front/rear views, or two angles for an AI
 *                 inference node.
 *
 *                 NOTE on USB bandwidth: when both cameras sit on the same
 *                 USB 2.0 (480 Mbps) bus, the isochronous bandwidth budget is
 *                 shared. Two high-resolution MJPEG streams can exceed it and
 *                 the second STREAMON then fails with "No space left on
 *                 device". The default capture size is therefore 640x480,
 *                 which lets two cameras stream concurrently on one USB 2.0
 *                 bus. Raise it (e.g. 1280x720) only if the cameras are on
 *                 separate buses or a USB 3.0 hub.
 *
 * Run:
 *   ./sample_uvc_dual_cam                                  # 120 frames/cam, 640x480
 *   ./sample_uvc_dual_cam 300                              # 300 frames/cam
 *   ./sample_uvc_dual_cam 300 /dev/video13 /dev/video15 cam0.h264 cam1.h264
 *   ./sample_uvc_dual_cam 300 /dev/video13 /dev/video15 cam0.h264 cam1.h264 1280 720
 *
 * Play: ffplay cam0.h264   /   ffplay cam1.h264
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

#define CAM_COUNT 2
#define UVC_CHN_ID 0
#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480
#define DEFAULT_BITRATE_KBPS 4000
#define DEFAULT_FPS 30
#define DEFAULT_GOP 30
#define DEFAULT_FRAMES 120
#define WARMUP_FRAMES 2

/* Per-camera pipeline context: one UVC device + one VDEC chn + one VENC chn. */
typedef struct CamCtx {
    const char *pszTag;
    const char *pszDevNode;
    S32 s32UvcDev;
    S32 s32VdecChn;
    S32 s32VencChn;
    FILE *fp;
    char szOutPath[128];
    int processed;
    int saved;
    int uvc_dev_on;
    int uvc_chn_on;
    int vdec_on;
    int venc_on;
    int finished;
} CamCtx;

static volatile int g_running = 1;
static U32 g_width = DEFAULT_WIDTH;
static U32 g_height = DEFAULT_HEIGHT;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Bring up one camera's full UVC -> VDEC -> VENC chain. */
static S32 cam_open(CamCtx *pstCam) {
    UvcDevAttr stDevAttr;
    UvcChnAttr stChnAttr;
    VdecChnAttr stVdecAttr;
    VencChnAttr stVencAttr;
    S32 ret;

    if (access(pstCam->pszDevNode, F_OK) != 0) {
        fprintf(stderr, "[%s] device %s not found\n", pstCam->pszTag, pstCam->pszDevNode);
        return -1;
    }

    /* ---- UVC device + channel (MJPEG capture) ---- */
    memset(&stDevAttr, 0, sizeof(stDevAttr));
    strncpy(stDevAttr.acDevNode, pstCam->pszDevNode, sizeof(stDevAttr.acDevNode) - 1);
    ret = UVC_CreateDev(pstCam->s32UvcDev, &stDevAttr);
    if (ret != 0) {
        fprintf(stderr, "[%s] UVC_CreateDev failed: %d\n", pstCam->pszTag, ret);
        return ret;
    }
    pstCam->uvc_dev_on = 1;

    ret = UVC_EnableDev(pstCam->s32UvcDev);
    if (ret != 0) {
        fprintf(stderr, "[%s] UVC_EnableDev failed: %d\n", pstCam->pszTag, ret);
        return ret;
    }

    memset(&stChnAttr, 0, sizeof(stChnAttr));
    stChnAttr.u32Width = g_width;
    stChnAttr.u32Height = g_height;
    stChnAttr.ePixelFormat = MPP_PIXEL_FORMAT_MJPEG;
    stChnAttr.u32Fps = DEFAULT_FPS;
    stChnAttr.u32Depth = 1;
    ret = UVC_SetChnAttr(pstCam->s32UvcDev, UVC_CHN_ID, &stChnAttr);
    if (ret != 0) {
        fprintf(stderr, "[%s] UVC_SetChnAttr failed: %d\n", pstCam->pszTag, ret);
        return ret;
    }
    ret = UVC_EnableChn(pstCam->s32UvcDev, UVC_CHN_ID);
    if (ret != 0) {
        fprintf(stderr, "[%s] UVC_EnableChn failed: %d\n", pstCam->pszTag, ret);
        return ret;
    }
    pstCam->uvc_chn_on = 1;

    /* ---- VDEC: MJPEG -> NV12 ---- */
    memset(&stVdecAttr, 0, sizeof(stVdecAttr));
    stVdecAttr.eCodecType = MPP_STREAM_CODEC_MJPEG;
    stVdecAttr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    stVdecAttr.u32Width = g_width;
    stVdecAttr.u32Height = g_height;
    ret = VDEC_CreateChn(pstCam->s32VdecChn, &stVdecAttr);
    if (ret != 0) {
        fprintf(stderr, "[%s] VDEC_CreateChn failed: %d\n", pstCam->pszTag, ret);
        return ret;
    }
    ret = VDEC_EnableChn(pstCam->s32VdecChn);
    if (ret != 0) {
        fprintf(stderr, "[%s] VDEC_EnableChn failed: %d\n", pstCam->pszTag, ret);
        VDEC_DestroyChn(pstCam->s32VdecChn);
        return ret;
    }
    pstCam->vdec_on = 1;

    /* ---- VENC: NV12 -> H.264 ---- */
    memset(&stVencAttr, 0, sizeof(stVencAttr));
    stVencAttr.eCodecType = MPP_STREAM_CODEC_H264;
    stVencAttr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    stVencAttr.u32Width = g_width;
    stVencAttr.u32Height = g_height;
    stVencAttr.eRcMode = VENC_RC_MODE_CBR;
    stVencAttr.u32Bitrate = DEFAULT_BITRATE_KBPS * 1000;
    stVencAttr.u32FrameRate = DEFAULT_FPS;
    stVencAttr.u32Gop = DEFAULT_GOP;
    ret = VENC_CreateChn(pstCam->s32VencChn, &stVencAttr);
    if (ret != 0) {
        fprintf(stderr, "[%s] VENC_CreateChn failed: %d\n", pstCam->pszTag, ret);
        return ret;
    }
    ret = VENC_EnableChn(pstCam->s32VencChn);
    if (ret != 0) {
        fprintf(stderr, "[%s] VENC_EnableChn failed: %d\n", pstCam->pszTag, ret);
        VENC_DestroyChn(pstCam->s32VencChn);
        return ret;
    }
    pstCam->venc_on = 1;

    pstCam->fp = fopen(pstCam->szOutPath, "wb");
    if (pstCam->fp == NULL) {
        fprintf(stderr, "[%s] fopen(%s) failed: %s\n", pstCam->pszTag, pstCam->szOutPath, strerror(errno));
        return -1;
    }

    printf("[%s] %s %ux%u MJPEG -> VDEC%d -> VENC%d H.264 -> %s\n",
        pstCam->pszTag, pstCam->pszDevNode, g_width, g_height,
        pstCam->s32VdecChn, pstCam->s32VencChn, pstCam->szOutPath);
    return 0;
}

/* Discard a few startup frames so the camera settles before encoding. */
static void cam_warmup(CamCtx *pstCam) {
    VideoFrameInfo uvcFrame;
    S32 i;

    for (i = 0; i < WARMUP_FRAMES; i++) {
        memset(&uvcFrame, 0, sizeof(uvcFrame));
        if (UVC_GetFrame(pstCam->s32UvcDev, UVC_CHN_ID, &uvcFrame, 3000) == 0) {
            UVC_ReleaseFrame(pstCam->s32UvcDev, UVC_CHN_ID, &uvcFrame);
        }
    }
}

/* Drain all ready encoded frames on this camera's VENC channel to file. */
static void cam_drain(CamCtx *pstCam) {
    StreamBufferInfo stream;
    S32 ret;

    while (1) {
        memset(&stream, 0, sizeof(stream));
        ret = VENC_GetStream(pstCam->s32VencChn, &stream, 0);
        if (ret != ERR_VENC_OK) {
            break;
        }
        if (fwrite(stream.pu8Addr, 1, stream.u32Size, pstCam->fp) != stream.u32Size) {
            fprintf(stderr, "[%s] short write\n", pstCam->pszTag);
        }
        VENC_ReleaseStream(pstCam->s32VencChn, &stream);
        pstCam->saved++;
        if (pstCam->saved % 30 == 0) {
            printf("  [%s] saved %d frames\n", pstCam->pszTag, pstCam->saved);
        }
    }
}

/* Capture one MJPEG frame from a camera, decode it and feed its encoder. */
static void cam_step(CamCtx *pstCam, int frame_count) {
    VideoFrameInfo uvcFrame;
    VideoFrameInfo decFrame;
    StreamBufferInfo inStream;
    S32 ret;

    if (pstCam->finished || pstCam->processed >= frame_count) {
        pstCam->finished = 1;
        return;
    }

    memset(&uvcFrame, 0, sizeof(uvcFrame));
    ret = UVC_GetFrame(pstCam->s32UvcDev, UVC_CHN_ID, &uvcFrame, 1000);
    if (ret != 0) {
        return;
    }
    if (uvcFrame.stVFrame.ulPlaneVirAddr[0] == 0 || uvcFrame.stVFrame.u32PlaneSizeValid[0] == 0) {
        UVC_ReleaseFrame(pstCam->s32UvcDev, UVC_CHN_ID, &uvcFrame);
        return;
    }

    memset(&inStream, 0, sizeof(inStream));
    inStream.pu8Addr = (const U8 *)uvcFrame.stVFrame.ulPlaneVirAddr[0];
    inStream.u32Size = uvcFrame.stVFrame.u32PlaneSizeValid[0];
    inStream.eCodecType = MPP_STREAM_CODEC_MJPEG;
    inStream.bKeyFrame = MPP_TRUE;
    inStream.u64PTS = uvcFrame.stVFrame.u64PTS;
    ret = VDEC_SendStream(pstCam->s32VdecChn, &inStream, 0);
    UVC_ReleaseFrame(pstCam->s32UvcDev, UVC_CHN_ID, &uvcFrame);
    if (ret != 0 && ret != ERR_VDEC_EOS) {
        return;
    }

    memset(&decFrame, 0, sizeof(decFrame));
    ret = VDEC_GetFrame(pstCam->s32VdecChn, &decFrame, 1000);
    if (ret != ERR_VDEC_OK) {
        return;
    }

    ret = VENC_SendFrame(pstCam->s32VencChn, &decFrame, 0);
    if (ret != 0) {
        fprintf(stderr, "[%s] VENC_SendFrame ret=%d\n", pstCam->pszTag, ret);
    }
    VDEC_ReleaseFrame(pstCam->s32VdecChn, decFrame.ulBufferId);
    pstCam->processed++;

    cam_drain(pstCam);
}

/* Tear down one camera's chain (safe to call partially-initialized). */
static void cam_close(CamCtx *pstCam) {
    if (pstCam->fp) {
        fclose(pstCam->fp);
        pstCam->fp = NULL;
    }
    if (pstCam->venc_on) {
        VENC_DisableChn(pstCam->s32VencChn);
        VENC_DestroyChn(pstCam->s32VencChn);
        pstCam->venc_on = 0;
    }
    if (pstCam->vdec_on) {
        VDEC_DisableChn(pstCam->s32VdecChn);
        VDEC_DestroyChn(pstCam->s32VdecChn);
        pstCam->vdec_on = 0;
    }
    if (pstCam->uvc_chn_on) {
        UVC_DisableChn(pstCam->s32UvcDev, UVC_CHN_ID);
        pstCam->uvc_chn_on = 0;
    }
    if (pstCam->uvc_dev_on) {
        UVC_DisableDev(pstCam->s32UvcDev);
        UVC_DestroyDev(pstCam->s32UvcDev);
        pstCam->uvc_dev_on = 0;
    }
}

static void usage(const char *prog) {
    printf("Usage: %s [frame_count] [dev0] [dev1] [out0] [out1] [width] [height]\n", prog);
    printf("  frame_count  frames per camera (default %d)\n", DEFAULT_FRAMES);
    printf("  dev0         camera 0 node (default /dev/video13)\n");
    printf("  dev1         camera 1 node (default /dev/video15)\n");
    printf("  out0         camera 0 H.264 file (default ./cam0.h264)\n");
    printf("  out1         camera 1 H.264 file (default ./cam1.h264)\n");
    printf("  width        capture width  (default %d)\n", DEFAULT_WIDTH);
    printf("  height       capture height (default %d)\n", DEFAULT_HEIGHT);
    printf("  NOTE: two cams on one USB 2.0 bus share bandwidth; keep 640x480\n");
    printf("        unless they are on separate buses / a USB 3.0 hub.\n");
}

int main(int argc, char *argv[]) {
    int frame_count = DEFAULT_FRAMES;
    CamCtx astCam[CAM_COUNT];
    S32 ret;
    S32 i;
    S32 opened = 0;
    int all_done;

    memset(astCam, 0, sizeof(astCam));

    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (argv[1][0] == '-') {
            usage(argv[0]);
            return 1;
        }
        frame_count = atoi(argv[1]);
        if (frame_count <= 0) {
            usage(argv[0]);
            return 1;
        }
    }

    astCam[0].pszTag = "cam0";
    astCam[0].pszDevNode = (argc > 2) ? argv[2] : "/dev/video13";
    astCam[0].s32UvcDev = 0;
    astCam[0].s32VdecChn = 0;
    astCam[0].s32VencChn = 0;
    snprintf(astCam[0].szOutPath, sizeof(astCam[0].szOutPath), "%s", (argc > 4) ? argv[4] : "./cam0.h264");

    astCam[1].pszTag = "cam1";
    astCam[1].pszDevNode = (argc > 3) ? argv[3] : "/dev/video15";
    astCam[1].s32UvcDev = 1;
    astCam[1].s32VdecChn = 1;
    astCam[1].s32VencChn = 1;
    snprintf(astCam[1].szOutPath, sizeof(astCam[1].szOutPath), "%s", (argc > 5) ? argv[5] : "./cam1.h264");

    if (argc > 6) {
        g_width = (U32)atoi(argv[6]);
    }
    if (argc > 7) {
        g_height = (U32)atoi(argv[7]);
    }
    if (g_width == 0 || g_height == 0) {
        usage(argv[0]);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Sample: Dual UVC Camera Parallel Capture ===\n");
    printf("  cam0: %s -> %s\n", astCam[0].pszDevNode, astCam[0].szOutPath);
    printf("  cam1: %s -> %s\n", astCam[1].pszDevNode, astCam[1].szOutPath);
    printf("  %d frames/cam, %ux%u MJPEG -> H.264\n\n", frame_count, g_width, g_height);

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
    ret = UVC_Init();
    if (ret != 0) {
        fprintf(stderr, "UVC_Init failed: %d\n", ret);
        goto cleanup_vb;
    }
    ret = VDEC_Init();
    if (ret != 0) {
        fprintf(stderr, "VDEC_Init failed: %d\n", ret);
        goto cleanup_uvc;
    }
    ret = VENC_Init();
    if (ret != 0) {
        fprintf(stderr, "VENC_Init failed: %d\n", ret);
        goto cleanup_vdec;
    }

    for (i = 0; i < CAM_COUNT; i++) {
        if (cam_open(&astCam[i]) != 0) {
            ret = 1;
            goto cleanup_cams;
        }
        opened++;
    }

    for (i = 0; i < CAM_COUNT; i++) {
        cam_warmup(&astCam[i]);
    }

    /* ---- main loop: round-robin grab from both cameras ---- */
    printf("\nCapturing from both cameras... (Ctrl+C to stop)\n");
    while (g_running) {
        all_done = 1;
        for (i = 0; i < CAM_COUNT; i++) {
            cam_step(&astCam[i], frame_count);
            if (!astCam[i].finished) {
                all_done = 0;
            }
        }
        if (all_done) {
            break;
        }
    }

    for (i = 0; i < CAM_COUNT; i++) {
        cam_drain(&astCam[i]);
    }

    printf("\nAll cameras done:\n");
    ret = 0;
    for (i = 0; i < CAM_COUNT; i++) {
        printf("  %s: %d frames captured, %d frames encoded -> %s\n",
            astCam[i].pszTag, astCam[i].processed, astCam[i].saved, astCam[i].szOutPath);
        if (astCam[i].saved == 0) {
            ret = 1;
        }
    }

cleanup_cams:
    for (i = 0; i < opened; i++) {
        cam_close(&astCam[i]);
    }
    VENC_Exit();

cleanup_vdec:
    VDEC_Exit();

cleanup_uvc:
    UVC_Exit();

cleanup_vb:
    VB_Exit();

cleanup_sys:
    SYS_Exit();

    printf("\n=== Sample finished ===\n");
    return ret;
}
