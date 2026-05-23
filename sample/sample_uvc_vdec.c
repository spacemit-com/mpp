/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    sample_uvc_vdec.c
 * @Date      :    2026-04-24
 * @Brief     :    Sample: UVC capture → VDEC decode → save NV12 frames.
 *
 *                 Two modes:
 *                   manual (default) – UVC_GetFrame → VDEC_SendStream → VDEC_GetFrame
 *                   bind   (--bind)  – SYS_Bind(UVC→VDEC), data flows automatically
 *
 * Run:
 *   ./sample_uvc_vdec                              # manual mode
 *   ./sample_uvc_vdec --bind                       # bind mode
 *   ./sample_uvc_vdec /dev/video0 ./output         # manual, custom dev & dir
 *   ./sample_uvc_vdec --bind /dev/video0 ./output  # bind, custom dev & dir
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>

#include "sys_api.h"
#include "uvc_api.h"
#include "vdec/vdec_api.h"
#include "vb_api.h"

/* ======================== Config ======================== */

#define SAMPLE_WIDTH 1280
#define SAMPLE_HEIGHT 720
#define SAMPLE_FPS 30
#define SAMPLE_SAVE_COUNT 20  /* number of decoded frames to save */
#define SAMPLE_WARMUP_COUNT 2 /* discard initial frames (manual mode) */

static const char *g_devNode = "/dev/video0";
static const char *g_outDir = "./nv12_output";
static S32 g_bindMode = 0; /* 0 = manual, 1 = bind */

static volatile S32 g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* ======================== Save NV12 Frame ======================== */

/**
 * @brief  Save one NV12 frame to a raw file.
 *         File name: <outDir>/frame_NNNN_WxH.nv12
 */
static S32 save_nv12_frame(const VideoFrameInfo *pFrame, U32 u32Idx, const char *outDir) {
    U32 w = pFrame->stVdecFrameInfo.stCommFrameInfo.u32Width;
    U32 h = pFrame->stVdecFrameInfo.stCommFrameInfo.u32Height;
    U32 stride = pFrame->stVFrame.u32PlaneStride[0];
    if (stride == 0)
        stride = w;

    char path[512];
    snprintf(path, sizeof(path), "%s/frame_%04u_%ux%u.nv12", outDir, u32Idx, w, h);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        printf("  [ERROR] fopen(%s) failed: %s\n", path, strerror(errno));
        return -1;
    }

    /* NV12: Y plane + UV plane (interleaved) */
    U32 nPlanes = pFrame->stVFrame.u32PlaneNum;
    for (U32 i = 0; i < nPlanes && i < FRAME_MAX_PLANE; i++) {
        const void *pData = (const void *)pFrame->stVFrame.ulPlaneVirAddr[i];
        U32 size = pFrame->stVFrame.u32PlaneSizeValid[i];
        if (!pData || size == 0)
            continue;
        fwrite(pData, 1, size, fp);
    }

    fclose(fp);
    printf("  [SAVE] %s (stride=%u)\n", path, stride);
    return 0;
}

/* ======================== Save MJPEG Frame ======================== */

static void save_mjpeg_frame(const VideoFrameInfo *pFrame, U32 u32Idx, const char *outDir) {
    const void *pData = (const void *)pFrame->stVFrame.ulPlaneVirAddr[0];
    U32 size = pFrame->stVFrame.u32PlaneSizeValid[0];
    if (!pData || size == 0)
        return;

    char path[512];
    snprintf(path, sizeof(path), "%s/uvc_%04u.mjpeg", outDir, u32Idx);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        printf("  [ERROR] fopen(%s) failed: %s\n", path, strerror(errno));
        return;
    }
    fwrite(pData, 1, size, fp);
    fclose(fp);
    printf("  [UVC ] %s (%u bytes)\n", path, size);
}

/* ======================== Main Pipeline (manual mode) ======================== */

static S32 run_uvc_vdec_manual(void) {
    S32 ret;

    /* Check device exists */
    if (access(g_devNode, F_OK) != 0) {
        printf("  [SKIP] UVC device %s not found\n", g_devNode);
        return -1;
    }

    /* Create output directory */
    mkdir(g_outDir, 0755);

    /* --- Init modules --- */
    ret = UVC_Init();
    assert(ret == 0);
    ret = VDEC_Init();
    assert(ret == 0);

    /* --- UVC device --- */
    UVC_DEV uvcDev = 0;
    UvcDevAttr devAttr;
    memset(&devAttr, 0, sizeof(devAttr));
    strncpy(devAttr.acDevNode, g_devNode, sizeof(devAttr.acDevNode) - 1);

    ret = UVC_CreateDev(uvcDev, &devAttr);
    assert(ret == 0);

    ret = UVC_EnableDev(uvcDev);
    if (ret != 0) {
        printf("  [ERROR] UVC_EnableDev failed (ret=%d)\n", ret);
        UVC_DestroyDev(uvcDev);
        VDEC_Exit();
        UVC_Exit();
        return -1;
    }

    /* --- UVC channel --- */
    UVC_CHN uvcChn = 0;
    UvcChnAttr uvcChnAttr;
    memset(&uvcChnAttr, 0, sizeof(uvcChnAttr));
    uvcChnAttr.u32Width = SAMPLE_WIDTH;
    uvcChnAttr.u32Height = SAMPLE_HEIGHT;
    uvcChnAttr.ePixelFormat = MPP_PIXEL_FORMAT_MJPEG;
    uvcChnAttr.u32Fps = SAMPLE_FPS;
    uvcChnAttr.u32Depth = 1;

    ret = UVC_SetChnAttr(uvcDev, uvcChn, &uvcChnAttr);
    assert(ret == 0);

    ret = UVC_EnableChn(uvcDev, uvcChn);
    if (ret != 0) {
        printf("  [ERROR] UVC_EnableChn failed (ret=%d)\n", ret);
        goto teardown_uvc_dev;
    }

    /* --- VDEC channel --- */
    S32 vdecChn = 0;
    VdecChnAttr vdecAttr;
    memset(&vdecAttr, 0, sizeof(vdecAttr));
    vdecAttr.eCodecType = MPP_STREAM_CODEC_MJPEG;
    vdecAttr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    vdecAttr.u32Width = SAMPLE_WIDTH;
    vdecAttr.u32Height = SAMPLE_HEIGHT;

    ret = VDEC_CreateChn(vdecChn, &vdecAttr);
    assert(ret == 0);

    ret = VDEC_EnableChn(vdecChn);
    if (ret != 0) {
        printf("  [ERROR] VDEC_EnableChn failed (ret=%d)\n", ret);
        VDEC_DestroyChn(vdecChn);
        goto teardown_uvc_chn;
    }

    /* --- Warm-up: discard initial UVC frames --- */
    printf("  [INFO] Discarding %u warm-up frames...\n", SAMPLE_WARMUP_COUNT);
    VideoFrameInfo uvcFrame;
    for (U32 i = 0; i < SAMPLE_WARMUP_COUNT; i++) {
        memset(&uvcFrame, 0, sizeof(uvcFrame));
        ret = UVC_GetFrame(uvcDev, uvcChn, &uvcFrame, 3000);
        if (ret == 0)
            UVC_ReleaseFrame(uvcDev, uvcChn, &uvcFrame);
    }

    /* --- Main loop: UVC → VDEC → save NV12 --- */
    U32 u32Saved = 0;
    U32 u32UvcSaved = 0;
    printf("  [INFO] Capturing and decoding, will save %u frames to %s\n", SAMPLE_SAVE_COUNT, g_outDir);

    while (g_running && u32Saved < SAMPLE_SAVE_COUNT) {
        /* Get MJPEG frame from UVC */
        memset(&uvcFrame, 0, sizeof(uvcFrame));
        ret = UVC_GetFrame(uvcDev, uvcChn, &uvcFrame, 3000);
        if (ret != 0) {
            if (!g_running)
                break;
            continue;
        }

        if (uvcFrame.stVFrame.ulPlaneVirAddr[0] == 0 || uvcFrame.stVFrame.u32PlaneSizeValid[0] == 0) {
            UVC_ReleaseFrame(uvcDev, uvcChn, &uvcFrame);
            continue;
        }

        /* Save raw MJPEG from UVC */
        if (u32UvcSaved < SAMPLE_SAVE_COUNT) {
            save_mjpeg_frame(&uvcFrame, u32UvcSaved, g_outDir);
            u32UvcSaved++;
        }

        /* Send MJPEG to VDEC */
        StreamBufferInfo stream;
        memset(&stream, 0, sizeof(stream));
        stream.pu8Addr = (const U8 *)uvcFrame.stVFrame.ulPlaneVirAddr[0];
        stream.u32Size = uvcFrame.stVFrame.u32PlaneSizeValid[0];
        stream.eCodecType = MPP_STREAM_CODEC_MJPEG;
        stream.bKeyFrame = MPP_TRUE;
        stream.bEndOfStream = MPP_FALSE;
        stream.u64PTS = uvcFrame.stVFrame.u64PTS;

        ret = VDEC_SendStream(vdecChn, &stream, 0);
        printf("  [DBG ] VDEC_SendStream ret=%d size=%u\n", ret, stream.u32Size);
        UVC_ReleaseFrame(uvcDev, uvcChn, &uvcFrame);

        if (ret != 0 && ret != ERR_VDEC_EOS)
            continue;

        /* Receive decoded NV12 frame */
        VideoFrameInfo decFrame;
        memset(&decFrame, 0, sizeof(decFrame));

        ret = VDEC_GetFrame(vdecChn, &decFrame, 1000);
        printf(
            "  [DBG ] VDEC_GetFrame ret=%d bufferId=%lu w=%u h=%u planes=%u"
            " virAddr[0]=%lu sizeValid[0]=%u\n",
            ret,
            (uintptr_t)decFrame.ulBufferId,
            decFrame.stVdecFrameInfo.stCommFrameInfo.u32Width,
            decFrame.stVdecFrameInfo.stCommFrameInfo.u32Height,
            decFrame.stVFrame.u32PlaneNum,
            (uintptr_t)decFrame.stVFrame.ulPlaneVirAddr[0],
            decFrame.stVFrame.u32PlaneSizeValid[0]);
        if (ret == ERR_VDEC_NO_FRAME || ret == ERR_VDEC_TIMEOUT)
            continue;
        if (ret == ERR_VDEC_EOS)
            break;
        if (ret != ERR_VDEC_OK)
            continue;

        /* Save decoded frame to file */
        save_nv12_frame(&decFrame, u32Saved, g_outDir);
        u32Saved++;
        printf("  [DBG ] u32Saved=%u\n", u32Saved);

        /* Release VB buffer back to decoder */
        VDEC_ReleaseFrame(vdecChn, decFrame.ulBufferId);
    }

    printf("  [INFO] Saved %u / %u frames\n", u32Saved, SAMPLE_SAVE_COUNT);

    /* --- Teardown --- */
    VDEC_DisableChn(vdecChn);
    VDEC_DestroyChn(vdecChn);

teardown_uvc_chn:
    UVC_DisableChn(uvcDev, uvcChn);

teardown_uvc_dev:
    UVC_DisableDev(uvcDev);
    UVC_DestroyDev(uvcDev);

    VDEC_Exit();
    UVC_Exit();

    return (S32)u32Saved;
}

/* ======================== Main Pipeline (bind mode) ======================== */

static S32 run_uvc_vdec_bind(void) {
    S32 ret;

    if (access(g_devNode, F_OK) != 0) {
        printf("  [SKIP] UVC device %s not found\n", g_devNode);
        return -1;
    }

    mkdir(g_outDir, 0755);

    /* --- Init modules --- */
    ret = UVC_Init();
    assert(ret == 0);
    ret = VDEC_Init();
    assert(ret == 0);

    /* --- UVC device --- */
    UVC_DEV uvcDev = 0;
    UvcDevAttr devAttr;
    memset(&devAttr, 0, sizeof(devAttr));
    strncpy(devAttr.acDevNode, g_devNode, sizeof(devAttr.acDevNode) - 1);

    ret = UVC_CreateDev(uvcDev, &devAttr);
    assert(ret == 0);

    ret = UVC_EnableDev(uvcDev);
    if (ret != 0) {
        printf("  [ERROR] UVC_EnableDev failed (ret=%d)\n", ret);
        UVC_DestroyDev(uvcDev);
        VDEC_Exit();
        UVC_Exit();
        return -1;
    }

    /* --- UVC channel (depth=0: data flows only via bind) --- */
    UVC_CHN uvcChn = 0;
    UvcChnAttr uvcChnAttr;
    memset(&uvcChnAttr, 0, sizeof(uvcChnAttr));
    uvcChnAttr.u32Width = SAMPLE_WIDTH;
    uvcChnAttr.u32Height = SAMPLE_HEIGHT;
    uvcChnAttr.ePixelFormat = MPP_PIXEL_FORMAT_MJPEG;
    uvcChnAttr.u32Fps = SAMPLE_FPS;
    uvcChnAttr.u32Depth = 0;

    ret = UVC_SetChnAttr(uvcDev, uvcChn, &uvcChnAttr);
    assert(ret == 0);

    ret = UVC_EnableChn(uvcDev, uvcChn);
    if (ret != 0) {
        printf("  [ERROR] UVC_EnableChn failed (ret=%d)\n", ret);
        goto bind_teardown_uvc_dev;
    }

    /* --- VDEC channel --- */
    S32 vdecChn = 0;
    VdecChnAttr vdecAttr;
    memset(&vdecAttr, 0, sizeof(vdecAttr));
    vdecAttr.eCodecType = MPP_STREAM_CODEC_MJPEG;
    vdecAttr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    vdecAttr.u32Width = SAMPLE_WIDTH;
    vdecAttr.u32Height = SAMPLE_HEIGHT;

    ret = VDEC_CreateChn(vdecChn, &vdecAttr);
    assert(ret == 0);

    ret = VDEC_EnableChn(vdecChn);
    if (ret != 0) {
        printf("  [ERROR] VDEC_EnableChn failed (ret=%d)\n", ret);
        VDEC_DestroyChn(vdecChn);
        goto bind_teardown_uvc_chn;
    }

    /* --- Bind: UVC(src) → VDEC(sink) --- */
    MppNode stSrc = {.eModId = MPP_ID_UVC, .s32DevId = uvcDev, .s32ChnId = uvcChn};
    MppNode stSink = {.eModId = MPP_ID_VDEC, .s32DevId = 0, .s32ChnId = vdecChn};

    ret = SYS_Bind(&stSrc, &stSink);
    if (ret != 0) {
        printf("  [ERROR] SYS_Bind UVC→VDEC failed (ret=%d)\n", ret);
        VDEC_DisableChn(vdecChn);
        VDEC_DestroyChn(vdecChn);
        goto bind_teardown_uvc_chn;
    }
    printf("  [INFO] SYS_Bind: UVC(dev=%d,chn=%d) → VDEC(chn=%d) OK\n", uvcDev, uvcChn, vdecChn);

    /* --- Main loop: read decoded frames from VDEC --- */
    U32 u32Saved = 0;
    printf("  [INFO] Waiting for decoded frames (bind mode), will save %u to %s\n", SAMPLE_SAVE_COUNT, g_outDir);

    while (g_running && u32Saved < SAMPLE_SAVE_COUNT) {
        VideoFrameInfo decFrame;
        memset(&decFrame, 0, sizeof(decFrame));

        ret = VDEC_GetFrame(vdecChn, &decFrame, 3000);
        if (ret == ERR_VDEC_NO_FRAME || ret == ERR_VDEC_TIMEOUT) {
            printf("  [DBG ] VDEC_GetFrame timeout, retrying...\n");
            continue;
        }
        if (ret == ERR_VDEC_EOS) {
            printf("  [INFO] EOS received\n");
            break;
        }
        if (ret != ERR_VDEC_OK) {
            printf("  [WARN] VDEC_GetFrame ret=%d\n", ret);
            continue;
        }

        printf(
            "  [DBG ] decoded frame: w=%u h=%u planes=%u pts=%" PRIu64 "\n",
            decFrame.stVdecFrameInfo.stCommFrameInfo.u32Width,
            decFrame.stVdecFrameInfo.stCommFrameInfo.u32Height,
            decFrame.stVFrame.u32PlaneNum,
            (uint64_t)decFrame.stVFrame.u64PTS);

        save_nv12_frame(&decFrame, u32Saved, g_outDir);
        u32Saved++;

        VDEC_ReleaseFrame(vdecChn, decFrame.ulBufferId);
    }

    printf("  [INFO] Saved %u / %u frames\n", u32Saved, SAMPLE_SAVE_COUNT);

    /* --- Teardown (reverse order) --- */
    SYS_UnBind(&stSrc, &stSink);
    printf("  [INFO] SYS_UnBind done\n");

    VDEC_DisableChn(vdecChn);
    VDEC_DestroyChn(vdecChn);

bind_teardown_uvc_chn:
    UVC_DisableChn(uvcDev, uvcChn);

bind_teardown_uvc_dev:
    UVC_DisableDev(uvcDev);
    UVC_DestroyDev(uvcDev);

    VDEC_Exit();
    UVC_Exit();

    return (S32)u32Saved;
}

/* ======================== Main ======================== */

int main(int argc, char *argv[]) {
    /* Parse args: [--bind] [--help] [devNode] [outDir] */
    S32 argIdx = 1;
    while (argIdx < argc && argv[argIdx][0] == '-') {
        if (strcmp(argv[argIdx], "--bind") == 0) {
            g_bindMode = 1;
        } else if (strcmp(argv[argIdx], "--help") == 0 || strcmp(argv[argIdx], "-h") == 0) {
            printf(
                "Usage: %s [OPTIONS] [devNode] [outDir]\n\n"
                "  UVC capture -> VDEC decode -> save NV12 frames.\n\n"
                "Options:\n"
                "  --bind    Use SYS_Bind mode (UVC->VDEC automatic stream delivery).\n"
                "            Default is manual mode (UVC_GetFrame -> VDEC_SendStream).\n"
                "  -h,--help Show this help message.\n\n"
                "Positional:\n"
                "  devNode   UVC device node  (default: %s)\n"
                "  outDir    Output directory  (default: %s)\n\n"
                "Examples:\n"
                "  %s\n"
                "  %s --bind\n"
                "  %s --bind /dev/video0 ./output\n",
                argv[0],
                g_devNode,
                g_outDir,
                argv[0],
                argv[0],
                argv[0]);
            return 0;
        }
        argIdx++;
    }
    if (argIdx < argc)
        g_devNode = argv[argIdx++];
    if (argIdx < argc)
        g_outDir = argv[argIdx++];

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Sample: UVC → %s → VDEC → Save NV12 ===\n", g_bindMode ? "Bind" : "Manual");
    printf("  Device : %s\n", g_devNode);
    printf("  Output : %s\n", g_outDir);
    printf("  Frames : %u\n\n", SAMPLE_SAVE_COUNT);

    S32 ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    S32 saved = g_bindMode ? run_uvc_vdec_bind() : run_uvc_vdec_manual();

    VB_Exit();
    SYS_Exit();

    printf("\n=== Sample finished (%d frames saved) ===\n", saved);
    return (saved > 0) ? 0 : 1;
}
