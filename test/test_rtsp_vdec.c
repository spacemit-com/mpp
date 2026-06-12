/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_rtsp_vdec.c
 * @Date      :    2026-06-08
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    RTSP pull + VDEC decode verification.
 *                 Uses SYS_Bind (DEMUX → VDEC) for automatic data flow.
 *
 * Run:
 *   ./test_rtsp_vdec rtsp://admin:123456@192.168.1.100:8554/stream
 *   ./test_rtsp_vdec rtsp://admin:123456@192.168.1.100:8554/stream 100 out.nv12
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
#include "sys/sys_api.h"
#include "sys/sys_type.h"
#include "sys/vb_api.h"
#include "vdec/vdec_api.h"

#define DEMUX_CHN 0
#define VDEC_CHN 0
#define RECV_TIMEOUT_MS 1000U
#define DEFAULT_DECODE_W 1920U
#define DEFAULT_DECODE_H 1088U

static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void save_nv12_frame(FILE *fp, const VideoFrameInfo *f) {
    if (!fp || !f || f->stVFrame.ulPlaneVirAddr[0] == 0) return;
    U32 w = f->stVdecFrameInfo.stCommFrameInfo.u32Width;
    U32 h = f->stVdecFrameInfo.stCommFrameInfo.u32Height;
    const U8 *y  = (const U8 *)(uintptr_t)f->stVFrame.ulPlaneVirAddr[0];
    U32 ys = f->stVFrame.u32PlaneStride[0];
    for (U32 i = 0; i < h; i++) fwrite(y + i * ys, 1, w, fp);
    if (f->stVFrame.u32PlaneNum > 1 && f->stVFrame.ulPlaneVirAddr[1] != 0) {
        const U8 *uv = (const U8 *)(uintptr_t)f->stVFrame.ulPlaneVirAddr[1];
        U32 us = f->stVFrame.u32PlaneStride[1];
        for (U32 i = 0; i < h / 2; i++) fwrite(uv + i * us, 1, w, fp);
    }
}

static MppStreamCodecType demux_to_mpp_codec(DemuxCodecType e) {
    switch (e) {
        case DEMUX_CODEC_H264:  return MPP_STREAM_CODEC_H264;
        case DEMUX_CODEC_H265:  return MPP_STREAM_CODEC_H265;
        case DEMUX_CODEC_MJPEG: return MPP_STREAM_CODEC_MJPEG;
        default:                return MPP_STREAM_CODEC_UNKNOWN;
    }
}

static S32 probe_packet_cb(S32 s32ChnId, const DemuxPacket *pstPkt, VOID *pPriv) {
    (void)s32ChnId;
    (void)pstPkt;
    (void)pPriv;
    return 1; /* Skip DEMUX internal SYS_SendStream during probing. */
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <rtsp_url> [max_frames] [output.nv12]\n", argv[0]);
        return 1;
    }
    U32 u32Limit = (argc >= 3) ? (U32)atoi(argv[2]) : 100;
    const char *pszOutput = (argc >= 4) ? argv[3] : "out.nv12";
    FILE *fpOut = fopen(pszOutput, "wb");
    if (!fpOut) {
        fprintf(stderr, "[FAIL] open %s\n", pszOutput);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    if (SYS_Init() != 0) {
        fprintf(stderr, "[FAIL] SYS_Init\n");
        return 1;
    }
    if (VB_Init() != 0) {
        fprintf(stderr, "[FAIL] VB_Init\n");
        SYS_Exit();
        return 1;
    }

    S32 ret;
    MppNode stSrc, stSink;
    DemuxStreamInfo stInfo;
    U32 u32Decoded = 0;

    /* ---- DEMUX ---- */
    DemuxChnAttr stDemuxAttr;
    memset(&stDemuxAttr, 0, sizeof(stDemuxAttr));
    stDemuxAttr.eInputType     = DEMUX_INPUT_RTSP;
    stDemuxAttr.bPreferTcp     = MPP_TRUE;
    stDemuxAttr.bLowLatency    = MPP_TRUE;
    stDemuxAttr.bInjectPS      = MPP_TRUE;
    stDemuxAttr.u32OpenTimeoutMs = 5000;
    stDemuxAttr.u32RwTimeoutMs   = 5000;
    stDemuxAttr.u32ReconnectMs   = 2000;
    snprintf(stDemuxAttr.szUrl, sizeof(stDemuxAttr.szUrl), "%s", argv[1]);

    if (DEMUX_Init() != 0) {
        fprintf(stderr, "[FAIL] DEMUX_Init\n");
        goto exit_vb;
    }
    if (DEMUX_CreateChn(DEMUX_CHN, &stDemuxAttr) != 0) {
        fprintf(stderr, "[FAIL] DEMUX_CreateChn\n");
        goto exit_demux;
    }

    /* ---- VDEC (create before bind, enable after) ---- */
    VdecChnAttr stVdecAttr;
    memset(&stVdecAttr, 0, sizeof(stVdecAttr));
    stVdecAttr.eCodecType          = MPP_STREAM_CODEC_H264; /* will be updated after stream info */
    stVdecAttr.eOutputPixelFormat  = MPP_PIXEL_FORMAT_NV12;
    stVdecAttr.u32Align            = 16;

    if (VDEC_Init() != 0) {
        fprintf(stderr, "[FAIL] VDEC_Init\n");
        goto destroy_demux;
    }

    /* ---- Probe stream info without feeding VDEC ---- */
    if (DEMUX_SetPacketCallback(DEMUX_CHN, probe_packet_cb, NULL) != 0) {
        fprintf(stderr, "[FAIL] DEMUX_SetPacketCallback(probe)\n");
        goto exit_vdec;
    }
    if (DEMUX_StartChn(DEMUX_CHN) != 0) {
        fprintf(stderr, "[FAIL] DEMUX_StartChn(probe)\n");
        goto exit_vdec;
    }

    memset(&stInfo, 0, sizeof(stInfo));
    for (int i = 0; i < 50; i++) {
        if (DEMUX_GetStreamInfo(DEMUX_CHN, &stInfo) == 0 && stInfo.u32Width > 0 && stInfo.u32Height > 0) {
            break;
        }
        usleep(200000);
    }
    DEMUX_StopChn(DEMUX_CHN);
    DEMUX_SetPacketCallback(DEMUX_CHN, NULL, NULL);
    if (stInfo.eCodecType == DEMUX_CODEC_UNKNOWN) {
        fprintf(stderr, "[FAIL] no stream codec info\n");
        goto exit_vdec;
    }
    if (stInfo.u32Width == 0 || stInfo.u32Height == 0) {
        fprintf(stderr, "[WARN] no stream resolution info, use default %ux%u\n", DEFAULT_DECODE_W, DEFAULT_DECODE_H);
        stInfo.u32Width = DEFAULT_DECODE_W;
        stInfo.u32Height = DEFAULT_DECODE_H;
    }

    printf("[INFO] stream: %ux%u fps=%u codec=%d\n",
            stInfo.u32Width, stInfo.u32Height, stInfo.u32Fps, stInfo.eCodecType);

    /* ---- Create/enable VDEC with correct codec/resolution ---- */
    stVdecAttr.eCodecType = demux_to_mpp_codec(stInfo.eCodecType);
    stVdecAttr.u32Width   = stInfo.u32Width;
    stVdecAttr.u32Height  = stInfo.u32Height;
    if (VDEC_CreateChn(VDEC_CHN, &stVdecAttr) != 0) {
        fprintf(stderr, "[FAIL] VDEC_CreateChn\n");
        goto exit_vdec;
    }
    if (VDEC_EnableChn(VDEC_CHN) != 0) {
        fprintf(stderr, "[FAIL] VDEC_EnableChn\n");
        goto destroy_vdec;
    }

    /* ---- Bind DEMUX → VDEC, then start DEMUX ---- */
    if (DEMUX_GetSrcNode(DEMUX_CHN, &stSrc) != 0) {
        fprintf(stderr, "[FAIL] DEMUX_GetSrcNode\n");
        goto disable_vdec;
    }
    stSink.eModId  = MPP_ID_VDEC;
    stSink.s32DevId = 0;
    stSink.s32ChnId = VDEC_CHN;
    if (SYS_Bind(&stSrc, &stSink) != 0) {
        fprintf(stderr, "[FAIL] SYS_Bind\n");
        goto disable_vdec;
    }
    if (DEMUX_StartChn(DEMUX_CHN) != 0) {
        fprintf(stderr, "[FAIL] DEMUX_StartChn\n");
        goto unbind;
    }

    printf("[INFO] decoding... (Ctrl+C to stop)\n");

    while (g_running) {
        VideoFrameInfo stFrame;
        memset(&stFrame, 0, sizeof(stFrame));
        ret = VDEC_GetFrame(VDEC_CHN, &stFrame, RECV_TIMEOUT_MS);
        if (ret == 0) {
            u32Decoded++;
            save_nv12_frame(fpOut, &stFrame);
            printf("\r[INFO] decoded=%u -> %s", u32Decoded, pszOutput);
            fflush(stdout);
            VDEC_ReleaseFrame(VDEC_CHN, stFrame.ulBufferId);
            if (u32Limit > 0 && u32Decoded >= u32Limit) break;
        } else if (ret == ERR_VDEC_EOS) {
            break;
        }
    }

    printf("\n%s decoded=%u saved to %s\n", u32Decoded > 0 ? "[PASS]" : "[FAIL]", u32Decoded, pszOutput);

    DEMUX_StopChn(DEMUX_CHN);
unbind:
    SYS_UnBind(&stSrc, &stSink);
disable_vdec:
    VDEC_DisableChn(VDEC_CHN);
destroy_vdec:
    VDEC_DestroyChn(VDEC_CHN);
exit_vdec:
    VDEC_Exit();
destroy_demux:
    DEMUX_DestroyChn(DEMUX_CHN);
exit_demux:
    DEMUX_Exit();
exit_vb:
    VB_Exit();
    SYS_Exit();
    if (fpOut) fclose(fpOut);
    return (u32Decoded > 0) ? 0 : 1;
}
