/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_uvc_vdec.c
 * @Date      :    2026-4-19
 * @Brief     :    Integration test: UVC capture → VDEC decode → VENC encode → MUX RTSP.
 *                 Captures MJPEG from UVC, decodes to NV12, re-encodes to H.264,
 *                 pushes via RTSP. Runs continuously until Ctrl+C.
 *                 VENC task thread automatically sends encoded stream to MUX
 *                 via SYS_SendStream (bound VENC src → MUX sink).
 *
 *                 NOTE: Requires a real UVC camera. Skips if device missing.
 *
 * Run:
 *   ./test_uvc_vdec
 *   ./test_uvc_vdec /dev/video0
 *   ./test_uvc_vdec /dev/video0 rtsp://0.0.0.0:8554/live
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

#include "sys_api.h"
#include "uvc_api.h"
#include "vdec/vdec_api.h"
#include "venc/venc_api.h"
#include "mux/mux_api.h"
#include "vb_api.h"

/* ======================== Helpers ======================== */

#define TEST_PASS(name) printf("[PASS] %s\n", (name))
#define TEST_FAIL(name, msg)                      \
    do {                                          \
        printf("[FAIL] %s: %s\n", (name), (msg)); \
        exit(1);                                  \
    } while (0)
#define TEST_SKIP(name, msg)                      \
    do {                                          \
        printf("[SKIP] %s: %s\n", (name), (msg)); \
        return;                                   \
    } while (0)

static const char *g_devNode = "/dev/video13";
static const char *g_rtspUrl = "rtsp://10.0.90.125:8554/live";
static BOOL g_hasHw = MPP_FALSE;
static U32 g_warmUpCnt = 30;

static volatile S32 g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void check_hw(void) {
    g_hasHw = (access(g_devNode, F_OK) == 0) ? MPP_TRUE : MPP_FALSE;
}

/* ======================== Test: UVC → VDEC → VENC → file ======================== */

/* ======================== Pipeline: UVC → VDEC → VENC → MUX (RTSP) ======================== */

static void test_uvc_vdec_venc_mux_pipeline(void) {
    const char *name = "uvc_vdec_venc_mux_pipeline";
    S32 ret;

    if (!g_hasHw)
        TEST_SKIP(name, "no UVC device found");

    /* --- Init modules --- */
    ret = UVC_Init();
    assert(ret == 0);
    ret = VDEC_Init();
    assert(ret == 0);
    ret = VENC_Init();
    assert(ret == 0);
    ret = MUX_Init();
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
        UVC_DestroyDev(uvcDev);
        MUX_Exit();
        VENC_Exit();
        VDEC_Exit();
        UVC_Exit();
        TEST_SKIP(name, "UVC EnableDev failed (device not usable)");
    }

    /* --- UVC channel --- */
    UVC_CHN uvcChn = 0;
    UvcChnAttr uvcChnAttr;
    memset(&uvcChnAttr, 0, sizeof(uvcChnAttr));
    uvcChnAttr.u32Width = 1280;
    uvcChnAttr.u32Height = 720;
    uvcChnAttr.ePixelFormat = MPP_PIXEL_FORMAT_MJPEG;
    uvcChnAttr.u32Fps = 30;
    uvcChnAttr.u32Depth = 1;

    ret = UVC_SetChnAttr(uvcDev, uvcChn, &uvcChnAttr);
    assert(ret == 0);

    ret = UVC_EnableChn(uvcDev, uvcChn);
    if (ret != 0) {
        UVC_DisableDev(uvcDev);
        UVC_DestroyDev(uvcDev);
        MUX_Exit();
        VENC_Exit();
        VDEC_Exit();
        UVC_Exit();
        TEST_SKIP(name, "UVC EnableChn failed (MJPEG not supported)");
    }

    /* --- VDEC channel --- */
    S32 vdecChn = 0;
    VdecChnAttr vdecAttr;
    memset(&vdecAttr, 0, sizeof(vdecAttr));
    vdecAttr.eCodecType = MPP_STREAM_CODEC_MJPEG;
    vdecAttr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    vdecAttr.u32Width = 1280;
    vdecAttr.u32Height = 720;
    vdecAttr.u32Align = 16;
    ret = VDEC_CreateChn(vdecChn, &vdecAttr);
    assert(ret == 0);

    ret = VDEC_EnableChn(vdecChn);
    if (ret != 0) {
        VDEC_DestroyChn(vdecChn);
        UVC_DisableChn(uvcDev, uvcChn);
        UVC_DisableDev(uvcDev);
        UVC_DestroyDev(uvcDev);
        MUX_Exit();
        VENC_Exit();
        VDEC_Exit();
        UVC_Exit();
        TEST_SKIP(name, "VDEC EnableChn failed");
    }

    /* --- VENC channel --- */
    S32 vencChn = 0;
    VencChnAttr vencAttr;
    memset(&vencAttr, 0, sizeof(vencAttr));
    vencAttr.eCodecType = MPP_STREAM_CODEC_H264;
    vencAttr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    vencAttr.u32Width = 1280;
    vencAttr.u32Height = 720;
    vencAttr.u32Align = 16;
    vencAttr.u32Bitrate = 4000000;
    vencAttr.u32FrameRate = 30;
    vencAttr.u32Gop = 30;
    vencAttr.eFrameBufMode = VENC_FRAME_BUF_DMABUF_EXTERNAL;
    vencAttr.eRcMode = VENC_RC_MODE_CBR;

    ret = VENC_CreateChn(vencChn, &vencAttr);
    assert(ret == 0);

    ret = VENC_EnableChn(vencChn);
    if (ret != 0) {
        VENC_DestroyChn(vencChn);
        VDEC_DisableChn(vdecChn);
        VDEC_DestroyChn(vdecChn);
        UVC_DisableChn(uvcDev, uvcChn);
        UVC_DisableDev(uvcDev);
        UVC_DestroyDev(uvcDev);
        MUX_Exit();
        VENC_Exit();
        VDEC_Exit();
        UVC_Exit();
        TEST_SKIP(name, "VENC EnableChn failed");
    }

    /* --- MUX channel (RTSP output) --- */
    S32 muxChn = 0;
    MuxChnAttr muxAttr;
    memset(&muxAttr, 0, sizeof(muxAttr));
    muxAttr.eOutputType = MUX_OUTPUT_RTSP;
    muxAttr.stStreamAttr.eCodecType = MUX_CODEC_H264;
    muxAttr.stStreamAttr.u32Width = 1280;
    muxAttr.stStreamAttr.u32Height = 720;
    muxAttr.stStreamAttr.u32Fps = 30;
    muxAttr.stStreamAttr.u32BitrateKbps = 4000;
    snprintf(muxAttr.szUrl, sizeof(muxAttr.szUrl), "%s", g_rtspUrl);

    ret = MUX_CreateChn(muxChn, &muxAttr);
    assert(ret == 0);

    ret = MUX_StartChn(muxChn);
    if (ret != 0) {
        MUX_DestroyChn(muxChn);
        VENC_DisableChn(vencChn);
        VENC_DestroyChn(vencChn);
        VDEC_DisableChn(vdecChn);
        VDEC_DestroyChn(vdecChn);
        UVC_DisableChn(uvcDev, uvcChn);
        UVC_DisableDev(uvcDev);
        UVC_DestroyDev(uvcDev);
        MUX_Exit();
        VENC_Exit();
        VDEC_Exit();
        UVC_Exit();
        TEST_FAIL(name, "MUX_StartChn failed");
    }

    /* Manual forwarding: VENC_GetStream → MUX_SendPacket */

    /* --- Warm-up: discard initial UVC frames --- */
    printf("  [INFO] Discarding %u warm-up frames...\n", g_warmUpCnt);
    VideoFrameInfo uvcFrame;
    const S32 uvcTimeout = 3000;

    for (U32 i = 0; i < g_warmUpCnt; i++) {
        memset(&uvcFrame, 0, sizeof(uvcFrame));
        ret = UVC_GetFrame(uvcDev, uvcChn, &uvcFrame, uvcTimeout);
        if (ret == 0)
            UVC_ReleaseFrame(uvcDev, uvcChn, &uvcFrame);
    }

    /* --- Main loop: UVC → VDEC → VENC (continuous until Ctrl+C) --- */
    U32 u32Done = 0;
    const U32 vdecTimeout = 1000;

    printf("  [INFO] UVC→VDEC→VENC→MUX running, Ctrl+C to stop\n");
    printf("  [INFO] RTSP URL: %s\n", g_rtspUrl);

    while (g_running) {
        memset(&uvcFrame, 0, sizeof(uvcFrame));
        ret = UVC_GetFrame(uvcDev, uvcChn, &uvcFrame, uvcTimeout);
        if (ret != 0) {
            if (!g_running)
                break;
            continue;
        }

        if (uvcFrame.stVFrame.ulPlaneVirAddr[0] == 0 || uvcFrame.stVFrame.u32PlaneSizeValid[0] == 0) {
            UVC_ReleaseFrame(uvcDev, uvcChn, &uvcFrame);
            continue;
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
        if (ret != 0 && ret != ERR_VDEC_EOS) {
            UVC_ReleaseFrame(uvcDev, uvcChn, &uvcFrame);
            continue;
        }

        UVC_ReleaseFrame(uvcDev, uvcChn, &uvcFrame);

        /* Receive decoded NV12 frame */
        VideoFrameInfo decFrame;
        memset(&decFrame, 0, sizeof(decFrame));

        ret = VDEC_GetFrame(vdecChn, &decFrame, vdecTimeout);
        if (ret == ERR_VDEC_NO_FRAME)
            continue;
        if (ret == ERR_VDEC_EOS)
            break;
        if (ret != ERR_VDEC_OK)
            continue;

        /* Send decoded frame to VENC */
        decFrame.eFrameType = FRAME_TYPE_VENC;

        ret = VENC_SendFrame(vencChn, &decFrame, 0);
        if (ret != ERR_VENC_OK) {
            printf("  [WARN] VENC_SendFrame failed (ret=%d)\n", ret);
            VDEC_ReleaseFrame(vdecChn, decFrame.ulBufferId);
            continue;
        }

        VDEC_ReleaseFrame(vdecChn, decFrame.ulBufferId);

        /* Receive encoded stream and forward to MUX */
        StreamBufferInfo encStream;
        memset(&encStream, 0, sizeof(encStream));
        ret = VENC_GetStream(vencChn, &encStream, vdecTimeout);
        if (ret == ERR_VENC_OK) {
            MuxPacket muxPkt;
            memset(&muxPkt, 0, sizeof(muxPkt));
            muxPkt.pu8Data = encStream.pu8Addr;
            muxPkt.u32Size = encStream.u32Size;
            muxPkt.bKeyFrame = encStream.bKeyFrame;
            muxPkt.eCodecType = MUX_CODEC_H264;
            muxPkt.u64PTS = encStream.u64PTS;

            ret = MUX_SendPacket(muxChn, &muxPkt);
            if (ret != 0) {
                printf("  [WARN] MUX_SendPacket failed (ret=%d)\n", ret);
            }

            VENC_ReleaseStream(vencChn, &encStream);
            u32Done++;

            if (u32Done % 100 == 0) {
                printf("  [INFO] %u frames sent to RTSP\n", u32Done);
            }
        }
    }

    printf("\n  [INFO] Stopping... encoded %u frames total\n", u32Done);

    /* --- Teardown --- */
    MUX_StopChn(muxChn);
    MUX_DestroyChn(muxChn);

    VENC_DisableChn(vencChn);
    VENC_DestroyChn(vencChn);

    VDEC_DisableChn(vdecChn);
    VDEC_DestroyChn(vdecChn);

    UVC_DisableChn(uvcDev, uvcChn);
    UVC_DisableDev(uvcDev);
    UVC_DestroyDev(uvcDev);

    MUX_Exit();
    VENC_Exit();
    VDEC_Exit();
    UVC_Exit();

    if (u32Done > 0)
        TEST_PASS(name);
    else
        TEST_FAIL(name, "no frames through pipeline");
}

/* ======================== Main ======================== */

int main(int argc, char *argv[]) {
    if (argc > 1)
        g_devNode = argv[1];
    if (argc > 2)
        g_rtspUrl = argv[2];

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    check_hw();

    printf("=== UVC → VDEC → VENC → MUX (RTSP) Test ===\n");
    printf("Device node : %s (%s)\n", g_devNode, g_hasHw ? "found" : "not found");
    printf("RTSP URL    : %s\n\n", g_rtspUrl);

    S32 ret __attribute__((unused)) = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    test_uvc_vdec_venc_mux_pipeline();

    VB_Exit();
    SYS_Exit();

    printf("\n=== Test finished ===\n");
    return 0;
}
