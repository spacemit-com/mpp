/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    sample_uvc_vdec_venc.c
 * @Date      :    2026-04-24
 * @Brief     :    Sample: UVC capture → VDEC decode → VENC encode → save H.264.
 *
 *                 Two modes:
 *                   manual (default) – UVC_GetFrame → VDEC_SendStream →
 *                                      VDEC_GetFrame → VENC_SendFrame →
 *                                      VENC_GetStream → write file
 *                   bind   (--bind)  – SYS_Bind(UVC→VDEC, VDEC→VENC),
 *                                      only VENC_GetStream is called manually
 *
 * Run:
 *   ./sample_uvc_vdec_venc                              # manual mode
 *   ./sample_uvc_vdec_venc --bind                       # bind mode
 *   ./sample_uvc_vdec_venc /dev/video0 ./out.h264       # manual, custom args
 *   ./sample_uvc_vdec_venc --bind /dev/video0 ./out.h264
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
#include "venc/venc_api.h"
#include "vb_api.h"

/* ======================== Config ======================== */

#define SAMPLE_WIDTH        1280
#define SAMPLE_HEIGHT       720
#define SAMPLE_FPS          30
#define SAMPLE_BITRATE      2000000     /* 2 Mbps */
#define SAMPLE_GOP          30
#define SAMPLE_SAVE_COUNT   60          /* encoded frames to save */
#define SAMPLE_WARMUP_COUNT 2           /* warm-up frames to discard (manual mode) */

static const char *g_devNode  = "/dev/video13";
static const char *g_outFile  = "./output.h264";
static S32         g_bindMode = 0;

static volatile S32 g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/* ======================== Write H.264 Stream ======================== */

static S32 write_stream(FILE *fp, const StreamBufferInfo *pstStream, U32 u32Idx)
{
    if (!pstStream->pu8Addr || pstStream->u32Size == 0)
        return -1;

    size_t written = fwrite(pstStream->pu8Addr, 1, pstStream->u32Size, fp);
    if (written != pstStream->u32Size) {
        printf("  [ERROR] fwrite: wrote %zu / %u bytes\n", written, pstStream->u32Size);
        return -1;
    }

    printf("  [SAVE] frame %04u: %u bytes  key=%d  pts=%llu\n",
           u32Idx, pstStream->u32Size, pstStream->bKeyFrame,
           (unsigned long long)pstStream->u64PTS);
    return 0;
}

/* ======================== Manual Pipeline ======================== */

static S32 run_manual(void)
{
    S32 ret;

    if (access(g_devNode, F_OK) != 0) {
        printf("  [SKIP] UVC device %s not found\n", g_devNode);
        return -1;
    }

    FILE *fp = fopen(g_outFile, "wb");
    if (!fp) {
        printf("  [ERROR] fopen(%s) failed: %s\n", g_outFile, strerror(errno));
        return -1;
    }

    /* --- Init modules --- */
    ret = UVC_Init();
    assert(ret == 0);
    ret = VDEC_Init();
    assert(ret == 0);
    ret = VENC_Init();
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
        goto manual_cleanup_uvc_dev;
    }

    /* --- UVC channel --- */
    UVC_CHN uvcChn = 0;
    UvcChnAttr uvcChnAttr;
    memset(&uvcChnAttr, 0, sizeof(uvcChnAttr));
    uvcChnAttr.u32Width     = SAMPLE_WIDTH;
    uvcChnAttr.u32Height    = SAMPLE_HEIGHT;
    uvcChnAttr.ePixelFormat = MPP_PIXEL_FORMAT_MJPEG;
    uvcChnAttr.u32Fps       = SAMPLE_FPS;
    uvcChnAttr.u32Depth     = 1;

    ret = UVC_SetChnAttr(uvcDev, uvcChn, &uvcChnAttr);
    assert(ret == 0);

    ret = UVC_EnableChn(uvcDev, uvcChn);
    if (ret != 0) {
        printf("  [ERROR] UVC_EnableChn failed (ret=%d)\n", ret);
        goto manual_cleanup_uvc_dev;
    }

    /* --- VDEC channel: MJPEG → NV12 --- */
    S32 vdecChn = 0;
    VdecChnAttr vdecAttr;
    memset(&vdecAttr, 0, sizeof(vdecAttr));
    vdecAttr.eCodecType         = MPP_STREAM_CODEC_MJPEG;
    vdecAttr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    vdecAttr.u32Width           = SAMPLE_WIDTH;
    vdecAttr.u32Height          = SAMPLE_HEIGHT;

    ret = VDEC_CreateChn(vdecChn, &vdecAttr);
    assert(ret == 0);

    ret = VDEC_EnableChn(vdecChn);
    if (ret != 0) {
        printf("  [ERROR] VDEC_EnableChn failed (ret=%d)\n", ret);
        VDEC_DestroyChn(vdecChn);
        goto manual_cleanup_uvc_chn;
    }

    /* --- VENC channel: NV12 → H.264 --- */
    S32 vencChn = 0;
    VencChnAttr vencAttr;
    memset(&vencAttr, 0, sizeof(vencAttr));
    vencAttr.eCodecType        = MPP_STREAM_CODEC_H264;
    vencAttr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    vencAttr.u32Width          = SAMPLE_WIDTH;
    vencAttr.u32Height         = SAMPLE_HEIGHT;
    vencAttr.u32Bitrate        = SAMPLE_BITRATE;
    vencAttr.u32FrameRate      = SAMPLE_FPS;
    vencAttr.u32Gop            = SAMPLE_GOP;
    vencAttr.eRcMode           = VENC_RC_MODE_CBR;

    ret = VENC_CreateChn(vencChn, &vencAttr);
    assert(ret == 0);

    ret = VENC_EnableChn(vencChn);
    if (ret != 0) {
        printf("  [ERROR] VENC_EnableChn failed (ret=%d)\n", ret);
        VENC_DestroyChn(vencChn);
        goto manual_cleanup_vdec;
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

    /* --- Main loop: UVC → VDEC → VENC → file --- */
    U32 u32Saved = 0;
    printf("  [INFO] Pipeline running (manual), saving %u frames to %s\n",
           SAMPLE_SAVE_COUNT, g_outFile);

    while (g_running && u32Saved < SAMPLE_SAVE_COUNT) {

        /* 1. Get MJPEG frame from UVC */
        memset(&uvcFrame, 0, sizeof(uvcFrame));
        ret = UVC_GetFrame(uvcDev, uvcChn, &uvcFrame, 3000);
        if (ret != 0) {
            if (!g_running) break;
            continue;
        }

        if (uvcFrame.stVFrame.ulPlaneVirAddr[0] == 0 ||
            uvcFrame.stVFrame.u32PlaneSizeValid[0] == 0) {
            UVC_ReleaseFrame(uvcDev, uvcChn, &uvcFrame);
            continue;
        }

        /* 2. Send MJPEG to VDEC */
        StreamBufferInfo stream;
        memset(&stream, 0, sizeof(stream));
        stream.pu8Addr      = (const U8 *)uvcFrame.stVFrame.ulPlaneVirAddr[0];
        stream.u32Size      = uvcFrame.stVFrame.u32PlaneSizeValid[0];
        stream.eCodecType   = MPP_STREAM_CODEC_MJPEG;
        stream.bKeyFrame    = MPP_TRUE;
        stream.bEndOfStream = MPP_FALSE;
        stream.u64PTS       = uvcFrame.stVFrame.u64PTS;

        ret = VDEC_SendStream(vdecChn, &stream, 0);
        printf("  [DBG ] VDEC_SendStream ret=%d size=%u\n", ret, stream.u32Size);
        UVC_ReleaseFrame(uvcDev, uvcChn, &uvcFrame);

        if (ret != 0 && ret != ERR_VDEC_EOS)
            continue;

        /* 3. Get decoded NV12 frame from VDEC */
        VideoFrameInfo decFrame;
        memset(&decFrame, 0, sizeof(decFrame));
        ret = VDEC_GetFrame(vdecChn, &decFrame, 1000);
        printf("  [DBG ] VDEC_GetFrame ret=%d bufferId=%lu w=%u h=%u planes=%u"
               " virAddr[0]=%lu sizeValid[0]=%u\n",
               ret,
               (unsigned long)decFrame.ulBufferId,
               decFrame.stVdecFrameInfo.stCommFrameInfo.u32Width,
               decFrame.stVdecFrameInfo.stCommFrameInfo.u32Height,
               decFrame.stVFrame.u32PlaneNum,
               (unsigned long)decFrame.stVFrame.ulPlaneVirAddr[0],
               decFrame.stVFrame.u32PlaneSizeValid[0]);
        if (ret == ERR_VDEC_NO_FRAME || ret == ERR_VDEC_TIMEOUT)
            continue;
        if (ret == ERR_VDEC_EOS)
            break;
        if (ret != ERR_VDEC_OK) {
            printf("  [WARN] VDEC_GetFrame ret=%d\n", ret);
            continue;
        }

        /* 4. Send NV12 frame to VENC */
        ret = VENC_SendFrame(vencChn, &decFrame, 0);
        printf("  [DBG ] VENC_SendFrame ret=%d bufferId=%lu\n", ret, (unsigned long)decFrame.ulBufferId);

        /* Release decoded frame after VENC has consumed it */
        VDEC_ReleaseFrame(vdecChn, decFrame.ulBufferId);

        if (ret != 0) {
            printf("  [WARN] VENC_SendFrame ret=%d\n", ret);
            continue;
        }

        /* 5. Receive encoded H.264 stream from VENC */
        StreamBufferInfo encStream;
        memset(&encStream, 0, sizeof(encStream));
        ret = VENC_GetStream(vencChn, &encStream, 1000);
        printf("  [DBG ] VENC_GetStream ret=%d size=%u\n", ret, encStream.u32Size);
        if (ret == ERR_VENC_NO_STREAM || ret == ERR_VENC_TIMEOUT)
            continue;
        if (ret == ERR_VENC_EOS)
            break;
        if (ret != ERR_VENC_OK) {
            printf("  [WARN] VENC_GetStream ret=%d\n", ret);
            continue;
        }

        /* 6. Write encoded stream to file */
        write_stream(fp, &encStream, u32Saved);
        u32Saved++;

        VENC_ReleaseStream(vencChn, &encStream);
    }

    printf("  [INFO] Saved %u / %u frames\n", u32Saved, SAMPLE_SAVE_COUNT);

    /* --- Teardown --- */
    VENC_DisableChn(vencChn);
    VENC_DestroyChn(vencChn);

manual_cleanup_vdec:
    VDEC_DisableChn(vdecChn);
    VDEC_DestroyChn(vdecChn);

manual_cleanup_uvc_chn:
    UVC_DisableChn(uvcDev, uvcChn);

manual_cleanup_uvc_dev:
    UVC_DisableDev(uvcDev);
    UVC_DestroyDev(uvcDev);

    VENC_Exit();
    VDEC_Exit();
    UVC_Exit();

    fclose(fp);
    return (S32)u32Saved;
}

/* ======================== Bind Pipeline ======================== */

static S32 run_bind(void)
{
    S32 ret;

    if (access(g_devNode, F_OK) != 0) {
        printf("  [SKIP] UVC device %s not found\n", g_devNode);
        return -1;
    }

    FILE *fp = fopen(g_outFile, "wb");
    if (!fp) {
        printf("  [ERROR] fopen(%s) failed: %s\n", g_outFile, strerror(errno));
        return -1;
    }

    /* --- Init modules --- */
    ret = UVC_Init();
    assert(ret == 0);
    ret = VDEC_Init();
    assert(ret == 0);
    ret = VENC_Init();
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
        goto bind_cleanup_uvc_dev;
    }

    /* --- UVC channel (depth=0: data flows only via bind) --- */
    UVC_CHN uvcChn = 0;
    UvcChnAttr uvcChnAttr;
    memset(&uvcChnAttr, 0, sizeof(uvcChnAttr));
    uvcChnAttr.u32Width     = SAMPLE_WIDTH;
    uvcChnAttr.u32Height    = SAMPLE_HEIGHT;
    uvcChnAttr.ePixelFormat = MPP_PIXEL_FORMAT_MJPEG;
    uvcChnAttr.u32Fps       = SAMPLE_FPS;
    uvcChnAttr.u32Depth     = 0;

    ret = UVC_SetChnAttr(uvcDev, uvcChn, &uvcChnAttr);
    assert(ret == 0);

    ret = UVC_EnableChn(uvcDev, uvcChn);
    if (ret != 0) {
        printf("  [ERROR] UVC_EnableChn failed (ret=%d)\n", ret);
        goto bind_cleanup_uvc_dev;
    }

    /* --- VDEC channel: MJPEG → NV12 --- */
    S32 vdecChn = 0;
    VdecChnAttr vdecAttr;
    memset(&vdecAttr, 0, sizeof(vdecAttr));
    vdecAttr.eCodecType         = MPP_STREAM_CODEC_MJPEG;
    vdecAttr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    vdecAttr.u32Width           = SAMPLE_WIDTH;
    vdecAttr.u32Height          = SAMPLE_HEIGHT;

    ret = VDEC_CreateChn(vdecChn, &vdecAttr);
    assert(ret == 0);

    ret = VDEC_EnableChn(vdecChn);
    if (ret != 0) {
        printf("  [ERROR] VDEC_EnableChn failed (ret=%d)\n", ret);
        VDEC_DestroyChn(vdecChn);
        goto bind_cleanup_uvc_chn;
    }

    /* --- VENC channel: NV12 → H.264 --- */
    S32 vencChn = 0;
    VencChnAttr vencAttr;
    memset(&vencAttr, 0, sizeof(vencAttr));
    vencAttr.eCodecType        = MPP_STREAM_CODEC_H264;
    vencAttr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    vencAttr.u32Width          = SAMPLE_WIDTH;
    vencAttr.u32Height         = SAMPLE_HEIGHT;
    vencAttr.u32Bitrate        = SAMPLE_BITRATE;
    vencAttr.u32FrameRate      = SAMPLE_FPS;
    vencAttr.u32Gop            = SAMPLE_GOP;
    vencAttr.eRcMode           = VENC_RC_MODE_CBR;

    ret = VENC_CreateChn(vencChn, &vencAttr);
    assert(ret == 0);

    ret = VENC_EnableChn(vencChn);
    if (ret != 0) {
        printf("  [ERROR] VENC_EnableChn failed (ret=%d)\n", ret);
        VENC_DestroyChn(vencChn);
        goto bind_cleanup_vdec;
    }

    /* --- Bind: UVC → VDEC → VENC --- */
    MppNode stUvcSrc  = { .eModId = MPP_ID_UVC,  .s32DevId = uvcDev, .s32ChnId = uvcChn };
    MppNode stVdecSink = { .eModId = MPP_ID_VDEC, .s32DevId = 0,      .s32ChnId = vdecChn };
    MppNode stVdecSrc  = { .eModId = MPP_ID_VDEC, .s32DevId = 0,      .s32ChnId = vdecChn };
    MppNode stVencSink = { .eModId = MPP_ID_VENC, .s32DevId = 0,      .s32ChnId = vencChn };

    ret = SYS_Bind(&stUvcSrc, &stVdecSink);
    if (ret != 0) {
        printf("  [ERROR] SYS_Bind UVC→VDEC failed (ret=%d)\n", ret);
        goto bind_cleanup_venc;
    }
    printf("  [INFO] SYS_Bind: UVC(dev=%d,chn=%d) → VDEC(chn=%d) OK\n",
           uvcDev, uvcChn, vdecChn);

    ret = SYS_Bind(&stVdecSrc, &stVencSink);
    if (ret != 0) {
        printf("  [ERROR] SYS_Bind VDEC→VENC failed (ret=%d)\n", ret);
        SYS_UnBind(&stUvcSrc, &stVdecSink);
        goto bind_cleanup_venc;
    }
    printf("  [INFO] SYS_Bind: VDEC(chn=%d) → VENC(chn=%d) OK\n",
           vdecChn, vencChn);

    /* --- Main loop: read encoded H.264 from VENC --- */
    U32 u32Saved = 0;
    printf("  [INFO] Pipeline running (bind), saving %u frames to %s\n",
           SAMPLE_SAVE_COUNT, g_outFile);

    while (g_running && u32Saved < SAMPLE_SAVE_COUNT) {
        StreamBufferInfo encStream;
        memset(&encStream, 0, sizeof(encStream));

        ret = VENC_GetStream(vencChn, &encStream, 3000);
        if (ret == ERR_VENC_NO_STREAM || ret == ERR_VENC_TIMEOUT) {
            printf("  [DBG ] VENC_GetStream timeout, retrying...\n");
            continue;
        }
        if (ret == ERR_VENC_EOS) {
            printf("  [INFO] EOS received\n");
            break;
        }
        if (ret != ERR_VENC_OK) {
            printf("  [WARN] VENC_GetStream ret=%d\n", ret);
            continue;
        }

        write_stream(fp, &encStream, u32Saved);
        u32Saved++;

        VENC_ReleaseStream(vencChn, &encStream);
    }

    printf("  [INFO] Saved %u / %u frames\n", u32Saved, SAMPLE_SAVE_COUNT);

    /* --- Teardown (reverse bind order) --- */
    SYS_UnBind(&stVdecSrc, &stVencSink);
    SYS_UnBind(&stUvcSrc, &stVdecSink);
    printf("  [INFO] SYS_UnBind done\n");

bind_cleanup_venc:
    VENC_DisableChn(vencChn);
    VENC_DestroyChn(vencChn);

bind_cleanup_vdec:
    VDEC_DisableChn(vdecChn);
    VDEC_DestroyChn(vdecChn);

bind_cleanup_uvc_chn:
    UVC_DisableChn(uvcDev, uvcChn);

bind_cleanup_uvc_dev:
    UVC_DisableDev(uvcDev);
    UVC_DestroyDev(uvcDev);

    VENC_Exit();
    VDEC_Exit();
    UVC_Exit();

    fclose(fp);
    return (S32)u32Saved;
}

/* ======================== Main ======================== */

int main(int argc, char *argv[])
{
    S32 argIdx = 1;
    while (argIdx < argc && argv[argIdx][0] == '-') {
        if (strcmp(argv[argIdx], "--bind") == 0) {
            g_bindMode = 1;
        } else if (strcmp(argv[argIdx], "--help") == 0 ||
                   strcmp(argv[argIdx], "-h") == 0) {
            printf("Usage: %s [OPTIONS] [devNode] [outFile]\n\n"
                   "  UVC capture -> VDEC decode -> VENC encode -> save H.264.\n\n"
                   "Options:\n"
                   "  --bind    Use SYS_Bind mode (UVC->VDEC->VENC automatic delivery).\n"
                   "            Default is manual mode.\n"
                   "  -h,--help Show this help message.\n\n"
                   "Positional:\n"
                   "  devNode   UVC device node  (default: %s)\n"
                   "  outFile   Output H.264 file (default: %s)\n\n"
                   "Examples:\n"
                   "  %s\n"
                   "  %s --bind\n"
                   "  %s --bind /dev/video0 ./out.h264\n",
                   argv[0], g_devNode, g_outFile,
                   argv[0], argv[0], argv[0]);
            return 0;
        }
        argIdx++;
    }
    if (argIdx < argc)
        g_devNode = argv[argIdx++];
    if (argIdx < argc)
        g_outFile = argv[argIdx++];

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Sample: UVC → VDEC → VENC (%s) → H.264 ===\n",
           g_bindMode ? "Bind" : "Manual");
    printf("  Device : %s\n", g_devNode);
    printf("  Output : %s\n", g_outFile);
    printf("  Size   : %ux%u @ %u fps\n", SAMPLE_WIDTH, SAMPLE_HEIGHT, SAMPLE_FPS);
    printf("  Bitrate: %u bps\n", SAMPLE_BITRATE);
    printf("  Frames : %u\n\n", SAMPLE_SAVE_COUNT);

    S32 ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    S32 saved = g_bindMode ? run_bind() : run_manual();

    VB_Exit();
    SYS_Exit();

    printf("\n=== Sample finished (%d frames saved) ===\n", saved);
    return (saved > 0) ? 0 : 1;
}
