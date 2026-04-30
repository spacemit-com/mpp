/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    sample_uvc_vdec_venc_rtsp.c
 * @Date      :    2026-04-28
 * @Brief     :    Sample: UVC capture → VDEC decode → VENC encode → RTSP (MUX).
 *
 *                 Two modes:
 *                   manual (default) – same pull as sample_uvc_vdec_venc.c, but
 *                                      H.264 goes to MUX_SendPacket (RTSP server)
 *                   bind   (--bind)  – SYS_Bind(UVC→VDEC, VDEC→VENC),
 *                                      VENC_GetStream → MUX_SendPacket
 *
 * Run:
 *   ./sample_uvc_vdec_venc_rtsp
 *   ./sample_uvc_vdec_venc_rtsp --bind
 *   ./sample_uvc_vdec_venc_rtsp /dev/video0 rtsp://0.0.0.0:8554/live
 *   ./sample_uvc_vdec_venc_rtsp --bind /dev/video13 rtsp://0.0.0.0:8554/live
 *
 * Play (example): ffplay rtsp://127.0.0.1:8554/live
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

/* ======================== Config ======================== */

#define SAMPLE_WIDTH          1280
#define SAMPLE_HEIGHT         720
#define SAMPLE_FPS            30
#define SAMPLE_BITRATE        2000000 /* 2 Mbps */
#define SAMPLE_GOP            30
#define SAMPLE_WARMUP_COUNT   2

static const char *g_devNode   = "/dev/video13";
static const char *g_rtspUrl   = "rtsp://10.0.90.148:8554/live";
static S32         g_bindMode  = 0;

static volatile S32 g_running = 1;

static void sig_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

static S32 mux_send_enc_stream(S32 muxChn, const StreamBufferInfo *pstStream)
{
    MuxPacket muxPkt;

    if (!pstStream->pu8Addr || pstStream->u32Size == 0)
        return -1;

    memset(&muxPkt, 0, sizeof(muxPkt));
    muxPkt.pu8Data    = pstStream->pu8Addr;
    muxPkt.u32Size    = pstStream->u32Size;
    muxPkt.bKeyFrame  = pstStream->bKeyFrame;
    muxPkt.eCodecType = MUX_CODEC_H264;
    muxPkt.u64PTS     = pstStream->u64PTS;
    return MUX_SendPacket(muxChn, &muxPkt);
}

static S32 run_manual(void)
{
    S32 ret;
    S32 muxChn = 0;

    if (access(g_devNode, F_OK) != 0) {
        printf("  [SKIP] UVC device %s not found\n", g_devNode);
        return -1;
    }

    ret = UVC_Init();
    assert(ret == 0);
    ret = VDEC_Init();
    assert(ret == 0);
    ret = VENC_Init();
    assert(ret == 0);
    ret = MUX_Init();
    assert(ret == 0);

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

    MuxChnAttr muxAttr;
    memset(&muxAttr, 0, sizeof(muxAttr));
    muxAttr.eOutputType = MUX_OUTPUT_RTSP;
    muxAttr.stStreamAttr.eCodecType     = MUX_CODEC_H264;
    muxAttr.stStreamAttr.u32Width       = SAMPLE_WIDTH;
    muxAttr.stStreamAttr.u32Height      = SAMPLE_HEIGHT;
    muxAttr.stStreamAttr.u32Fps          = SAMPLE_FPS;
    muxAttr.stStreamAttr.u32BitrateKbps = SAMPLE_BITRATE / 1000;
    snprintf(muxAttr.szUrl, sizeof(muxAttr.szUrl), "%s", g_rtspUrl);

    ret = MUX_CreateChn(muxChn, &muxAttr);
    assert(ret == 0);

    ret = MUX_StartChn(muxChn);
    if (ret != 0) {
        printf("  [ERROR] MUX_StartChn failed (ret=%d)\n", ret);
        MUX_DestroyChn(muxChn);
        goto manual_cleanup_venc;
    }

    printf("  [INFO] RTSP: %s (Ctrl+C to stop)\n", g_rtspUrl);

    VideoFrameInfo uvcFrame;
    printf("  [INFO] Discarding %u warm-up frames...\n", SAMPLE_WARMUP_COUNT);
    for (U32 i = 0; i < SAMPLE_WARMUP_COUNT; i++) {
        memset(&uvcFrame, 0, sizeof(uvcFrame));
        ret = UVC_GetFrame(uvcDev, uvcChn, &uvcFrame, 3000);
        if (ret == 0)
            UVC_ReleaseFrame(uvcDev, uvcChn, &uvcFrame);
    }

    U32 u32MuxPkts = 0;

    while (g_running) {
        memset(&uvcFrame, 0, sizeof(uvcFrame));
        ret = UVC_GetFrame(uvcDev, uvcChn, &uvcFrame, 3000);
        if (ret != 0) {
            if (!g_running)
                break;
            continue;
        }

        if (uvcFrame.stVFrame.ulPlaneVirAddr[0] == 0 ||
            uvcFrame.stVFrame.u32PlaneSizeValid[0] == 0) {
            UVC_ReleaseFrame(uvcDev, uvcChn, &uvcFrame);
            continue;
        }

        StreamBufferInfo stream;
        memset(&stream, 0, sizeof(stream));
        stream.pu8Addr      = (const U8 *)uvcFrame.stVFrame.ulPlaneVirAddr[0];
        stream.u32Size      = uvcFrame.stVFrame.u32PlaneSizeValid[0];
        stream.eCodecType   = MPP_STREAM_CODEC_MJPEG;
        stream.bKeyFrame    = MPP_TRUE;
        stream.bEndOfStream = MPP_FALSE;
        stream.u64PTS       = uvcFrame.stVFrame.u64PTS;

        ret = VDEC_SendStream(vdecChn, &stream, 0);
        UVC_ReleaseFrame(uvcDev, uvcChn, &uvcFrame);

        if (ret != 0 && ret != ERR_VDEC_EOS)
            continue;

        VideoFrameInfo decFrame;
        memset(&decFrame, 0, sizeof(decFrame));
        ret = VDEC_GetFrame(vdecChn, &decFrame, 1000);
        if (ret == ERR_VDEC_NO_FRAME || ret == ERR_VDEC_TIMEOUT)
            continue;
        if (ret == ERR_VDEC_EOS)
            break;
        if (ret != ERR_VDEC_OK)
            continue;

        decFrame.eFrameType = FRAME_TYPE_VENC;
        ret = VENC_SendFrame(vencChn, &decFrame, 0);
        VDEC_ReleaseFrame(vdecChn, decFrame.ulBufferId);

        if (ret != 0)
            continue;

        StreamBufferInfo encStream;
        memset(&encStream, 0, sizeof(encStream));
        ret = VENC_GetStream(vencChn, &encStream, 1000);
        if (ret == ERR_VENC_NO_STREAM || ret == ERR_VENC_TIMEOUT)
            continue;
        if (ret == ERR_VENC_EOS)
            break;
        if (ret != ERR_VENC_OK)
            continue;

        ret = mux_send_enc_stream(muxChn, &encStream);
        if (ret != 0)
            printf("  [WARN] MUX_SendPacket ret=%d\n", ret);

        VENC_ReleaseStream(vencChn, &encStream);
        u32MuxPkts++;
        if (u32MuxPkts % 100 == 0) {
            MuxChnStat st;
            if (MUX_GetChnStat(muxChn, &st) == 0) {
                printf("  [INFO] mux pkts=%u  clients=%u\n",
                       u32MuxPkts, st.u32ActiveClients);
            }
        }
    }

    printf("  [INFO] Stopping after %u packets muxed\n", u32MuxPkts);

    MUX_StopChn(muxChn);
    MUX_DestroyChn(muxChn);

manual_cleanup_venc:
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

    MUX_Exit();
    VENC_Exit();
    VDEC_Exit();
    UVC_Exit();

    return (S32)u32MuxPkts;
}

static S32 run_bind(void)
{
    S32 ret;
    S32 muxChn = 0;

    if (access(g_devNode, F_OK) != 0) {
        printf("  [SKIP] UVC device %s not found\n", g_devNode);
        return -1;
    }

    ret = UVC_Init();
    assert(ret == 0);
    ret = VDEC_Init();
    assert(ret == 0);
    ret = VENC_Init();
    assert(ret == 0);
    ret = MUX_Init();
    assert(ret == 0);

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

    MppNode stUvcNode   = { .eModId = MPP_ID_UVC,  .s32DevId = uvcDev, .s32ChnId = uvcChn };
    MppNode stVdecNode = { .eModId = MPP_ID_VDEC, .s32DevId = 0,      .s32ChnId = vdecChn };
    MppNode stVencNode = { .eModId = MPP_ID_VENC, .s32DevId = 0,      .s32ChnId = vencChn };

    ret = SYS_Bind(&stUvcNode, &stVdecNode);
    if (ret != 0) {
        printf("  [ERROR] SYS_Bind UVC→VDEC failed (ret=%d)\n", ret);
        goto bind_cleanup_venc;
    }

    ret = SYS_Bind(&stVdecNode, &stVencNode);
    if (ret != 0) {
        printf("  [ERROR] SYS_Bind VDEC→VENC failed (ret=%d)\n", ret);
        SYS_UnBind(&stUvcNode, &stVdecNode);
        goto bind_cleanup_venc;
    }

    printf("  [INFO] SYS_Bind: UVC → VDEC → VENC OK\n");

    MuxChnAttr muxAttr;
    memset(&muxAttr, 0, sizeof(muxAttr));
    muxAttr.eOutputType = MUX_OUTPUT_RTSP;
    muxAttr.stStreamAttr.eCodecType     = MUX_CODEC_H264;
    muxAttr.stStreamAttr.u32Width       = SAMPLE_WIDTH;
    muxAttr.stStreamAttr.u32Height      = SAMPLE_HEIGHT;
    muxAttr.stStreamAttr.u32Fps          = SAMPLE_FPS;
    muxAttr.stStreamAttr.u32BitrateKbps = SAMPLE_BITRATE / 1000;
    snprintf(muxAttr.szUrl, sizeof(muxAttr.szUrl), "%s", g_rtspUrl);

    ret = MUX_CreateChn(muxChn, &muxAttr);
    assert(ret == 0);

    ret = MUX_StartChn(muxChn);
    if (ret != 0) {
        printf("  [ERROR] MUX_StartChn failed (ret=%d)\n", ret);
        MUX_DestroyChn(muxChn);
        SYS_UnBind(&stVdecNode, &stVencNode);
        SYS_UnBind(&stUvcNode, &stVdecNode);
        goto bind_cleanup_venc;
    }

    printf("  [INFO] RTSP: %s (Ctrl+C to stop)\n", g_rtspUrl);

    U32 u32MuxPkts = 0;

    while (g_running) {
        StreamBufferInfo encStream;
        memset(&encStream, 0, sizeof(encStream));

        ret = VENC_GetStream(vencChn, &encStream, 3000);
        if (ret == ERR_VENC_NO_STREAM || ret == ERR_VENC_TIMEOUT)
            continue;
        if (ret == ERR_VENC_EOS)
            break;
        if (ret != ERR_VENC_OK) {
            printf("  [WARN] VENC_GetStream ret=%d\n", ret);
            continue;
        }

        ret = mux_send_enc_stream(muxChn, &encStream);
        if (ret != 0)
            printf("  [WARN] MUX_SendPacket ret=%d\n", ret);

        VENC_ReleaseStream(vencChn, &encStream);
        u32MuxPkts++;
        if (u32MuxPkts % 100 == 0) {
            MuxChnStat st;
            if (MUX_GetChnStat(muxChn, &st) == 0) {
                printf("  [INFO] mux pkts=%u  clients=%u\n",
                       u32MuxPkts, st.u32ActiveClients);
            }
        }
    }

    printf("  [INFO] Stopping after %u packets muxed\n", u32MuxPkts);

    MUX_StopChn(muxChn);
    MUX_DestroyChn(muxChn);

    SYS_UnBind(&stVdecNode, &stVencNode);
    SYS_UnBind(&stUvcNode, &stVdecNode);

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

    MUX_Exit();
    VENC_Exit();
    VDEC_Exit();
    UVC_Exit();

    return 0;
}

int main(int argc, char *argv[])
{
    S32 argIdx = 1;

    while (argIdx < argc && argv[argIdx][0] == '-') {
        if (strcmp(argv[argIdx], "--bind") == 0) {
            g_bindMode = 1;
        } else if (strcmp(argv[argIdx], "--help") == 0 ||
                   strcmp(argv[argIdx], "-h") == 0) {
            printf("Usage: %s [OPTIONS] [devNode] [rtspUrl]\n\n"
                   "  UVC capture -> VDEC -> VENC -> RTSP (Annex-B H.264).\n\n"
                   "Options:\n"
                   "  --bind    Use SYS_Bind (UVC→VDEC→VENC).\n"
                   "  -h        This help.\n\n"
                   "Positional:\n"
                   "  devNode   UVC device (default: %s)\n"
                   "  rtspUrl   MUX listen URL (default: %s)\n\n"
                   "Example playback:\n"
                   "  ffplay rtsp://127.0.0.1:8554/live\n",
                   argv[0], g_devNode, g_rtspUrl);
            return 0;
        }
        argIdx++;
    }
    if (argIdx < argc)
        g_devNode = argv[argIdx++];
    if (argIdx < argc)
        g_rtspUrl = argv[argIdx++];

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Sample: UVC → VDEC → VENC → RTSP (%s) ===\n",
           g_bindMode ? "bind" : "manual");
    printf("  Device : %s\n", g_devNode);
    printf("  RTSP   : %s\n", g_rtspUrl);
    printf("  Video  : %ux%u @ %u fps, %u bps H.264\n\n",
           SAMPLE_WIDTH, SAMPLE_HEIGHT, SAMPLE_FPS, SAMPLE_BITRATE);

    S32 ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    g_bindMode ? run_bind() : run_manual();

    VB_Exit();
    SYS_Exit();

    printf("\n=== Sample finished ===\n");
    return 0;
}
