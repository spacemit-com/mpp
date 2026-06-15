/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    sample_uvc_record_snap.c
 * @Date      :    2026-06-05
 * @Brief     :    Sample: UVC continuous H.264 recording + periodic JPEG snapshot.
 *
 *                 The classic "record while grabbing stills" use case (event
 *                 snapshots, AI keyframe capture, thumbnails), driven by a real
 *                 USB (UVC) camera:
 *
 *                   UVC(MJPEG) -> VDEC -> NV12 frame
 *                                          |--> VENC0 H.264  -> record.h264 (every frame)
 *                                          |--> VENC1 MJPEG  -> snap_XXXX.jpg (every N frames)
 *
 *                 The UVC module exposes a single capture channel per device,
 *                 so the camera's MJPEG is decoded once and the decoded NV12
 *                 frame is fanned out: always to the H.264 recorder, and -- on
 *                 every snap_interval-th frame -- to a fixed-QP MJPEG encoder
 *                 that writes a still JPEG.
 *
 * Run:
 *   ./sample_uvc_record_snap                       # 120 frames, snap every 30
 *   ./sample_uvc_record_snap 300 15                # 300 frames, snap every 15
 *   ./sample_uvc_record_snap 300 15 ./out          # output dir prefix ./out
 *   ./sample_uvc_record_snap 300 15 ./out /dev/video13
 *
 * Play:  ffplay record.h264   /   view snap_0000.jpg
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
#define REC_VENC_CHN 0
#define SNAP_VENC_CHN 1
#define CAP_WIDTH 1280
#define CAP_HEIGHT 720
#define REC_BITRATE_KBPS 4000
#define DEFAULT_FPS 30
#define DEFAULT_GOP 30
#define DEFAULT_FRAMES 120
#define DEFAULT_SNAP_INTERVAL 30
#define SNAP_JPEG_QP 80
#define WARMUP_FRAMES 2

static const char *g_devNode = "/dev/video13";
static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static S32 setup_venc_chn(S32 s32Chn, MppStreamCodecType eCodec) {
    VencChnAttr stAttr;
    S32 ret;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eCodecType = eCodec;
    stAttr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    stAttr.u32Width = CAP_WIDTH;
    stAttr.u32Height = CAP_HEIGHT;
    stAttr.u32FrameRate = DEFAULT_FPS;
    stAttr.u32Gop = DEFAULT_GOP;

    if (eCodec == MPP_STREAM_CODEC_MJPEG) {
        stAttr.eRcMode = VENC_RC_MODE_FIXQP;
        stAttr.u32IQp = SNAP_JPEG_QP;
        stAttr.u32PQp = SNAP_JPEG_QP;
    } else {
        stAttr.eRcMode = VENC_RC_MODE_CBR;
        stAttr.u32Bitrate = REC_BITRATE_KBPS * 1000;
    }

    ret = VENC_CreateChn(s32Chn, &stAttr);
    if (ret != 0) {
        fprintf(stderr, "VENC_CreateChn(chn%d) failed: %d\n", s32Chn, ret);
        return ret;
    }
    ret = VENC_EnableChn(s32Chn);
    if (ret != 0) {
        fprintf(stderr, "VENC_EnableChn(chn%d) failed: %d\n", s32Chn, ret);
        VENC_DestroyChn(s32Chn);
    }
    return ret;
}

/* Drain all ready H.264 frames on the recorder channel to the record file. */
static void drain_record(FILE *fp, int *pSaved) {
    StreamBufferInfo stream;
    S32 ret;

    while (1) {
        memset(&stream, 0, sizeof(stream));
        ret = VENC_GetStream(REC_VENC_CHN, &stream, 0);
        if (ret != ERR_VENC_OK) {
            break;
        }
        if (fwrite(stream.pu8Addr, 1, stream.u32Size, fp) != stream.u32Size) {
            fprintf(stderr, "[rec] short write\n");
        }
        VENC_ReleaseStream(REC_VENC_CHN, &stream);
        (*pSaved)++;
        if (*pSaved % 30 == 0) {
            printf("  [rec] recorded %d frames\n", *pSaved);
        }
    }
}

/* Pull one JPEG from the snapshot encoder and write it to disk. */
static S32 take_snapshot(const char *pszDir, int idx) {
    StreamBufferInfo stream;
    char path[200];
    FILE *fp;
    S32 ret;

    memset(&stream, 0, sizeof(stream));
    ret = VENC_GetStream(SNAP_VENC_CHN, &stream, 1000);
    if (ret != ERR_VENC_OK) {
        fprintf(stderr, "  [snap] VENC_GetStream error %d\n", ret);
        return ret;
    }

    snprintf(path, sizeof(path), "%s/snap_%04d.jpg", pszDir, idx);
    fp = fopen(path, "wb");
    if (fp != NULL) {
        if (fwrite(stream.pu8Addr, 1, stream.u32Size, fp) != stream.u32Size) {
            fprintf(stderr, "  [snap] short write %s\n", path);
        }
        fclose(fp);
        printf("  [snap] saved %s (%u bytes)\n", path, stream.u32Size);
    } else {
        fprintf(stderr, "  [snap] fopen(%s) failed: %s\n", path, strerror(errno));
    }
    VENC_ReleaseStream(SNAP_VENC_CHN, &stream);
    return 0;
}

static void usage(const char *prog) {
    printf("Usage: %s [frame_count] [snap_interval] [out_dir] [dev_node]\n", prog);
    printf("  frame_count    H.264 frames to record (default %d)\n", DEFAULT_FRAMES);
    printf("  snap_interval  take a JPEG every N frames (default %d)\n", DEFAULT_SNAP_INTERVAL);
    printf("  out_dir        output directory (default .)\n");
    printf("  dev_node       UVC device node (default %s)\n", g_devNode);
}

int main(int argc, char *argv[]) {
    int frame_count = DEFAULT_FRAMES;
    int snap_interval = DEFAULT_SNAP_INTERVAL;
    const char *pszDir = ".";
    char szRecPath[200];
    S32 ret;
    S32 uvc_dev_on = 0;
    S32 uvc_chn_on = 0;
    S32 vdec_on = 0;
    S32 venc_ready = 0; /* bitmask: 1=rec, 2=snap */
    int processed = 0;
    int recorded = 0;
    int snap_idx = 0;
    FILE *fpRec = NULL;
    VideoFrameInfo uvcFrame;
    VideoFrameInfo decFrame;
    StreamBufferInfo inStream;

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
    if (argc > 2) {
        snap_interval = atoi(argv[2]);
        if (snap_interval <= 0) {
            snap_interval = DEFAULT_SNAP_INTERVAL;
        }
    }
    if (argc > 3) {
        pszDir = argv[3];
    }
    if (argc > 4) {
        g_devNode = argv[4];
    }
    snprintf(szRecPath, sizeof(szRecPath), "%s/record.h264", pszDir);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Sample: UVC Record + JPEG Snapshot ===\n");
    printf("  Source : %s  %ux%u MJPEG\n", g_devNode, CAP_WIDTH, CAP_HEIGHT);
    printf("  Record : %ux%u H.264 %u kbps -> %s\n", CAP_WIDTH, CAP_HEIGHT, REC_BITRATE_KBPS, szRecPath);
    printf("  Snap   : MJPEG QP%d every %d frames -> %s/snap_*.jpg\n", SNAP_JPEG_QP, snap_interval, pszDir);
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

    /* ---- VENC: H.264 recorder + MJPEG snapshot ---- */
    ret = VENC_Init();
    if (ret != 0) {
        fprintf(stderr, "VENC_Init failed: %d\n", ret);
        goto cleanup_vdec;
    }
    if (setup_venc_chn(REC_VENC_CHN, MPP_STREAM_CODEC_H264) != 0) {
        goto cleanup_venc;
    }
    venc_ready |= 1;
    if (setup_venc_chn(SNAP_VENC_CHN, MPP_STREAM_CODEC_MJPEG) != 0) {
        goto cleanup_venc;
    }
    venc_ready |= 2;
    printf("VENC chn%d (H.264) + chn%d (MJPEG) enabled\n", REC_VENC_CHN, SNAP_VENC_CHN);

    fpRec = fopen(szRecPath, "wb");
    if (fpRec == NULL) {
        fprintf(stderr, "fopen(%s) failed: %s\n", szRecPath, strerror(errno));
        ret = -1;
        goto cleanup_venc;
    }

    /* ---- warm-up: discard first UVC frames ---- */
    {
        int w;
        for (w = 0; w < WARMUP_FRAMES; w++) {
            memset(&uvcFrame, 0, sizeof(uvcFrame));
            if (UVC_GetFrame(UVC_DEV_ID, UVC_CHN_ID, &uvcFrame, 3000) == 0) {
                UVC_ReleaseFrame(UVC_DEV_ID, UVC_CHN_ID, &uvcFrame);
            }
        }
    }

    /* ---- main loop: UVC -> VDEC -> record + periodic snapshot ---- */
    printf("Capturing... (Ctrl+C to stop)\n");
    while (g_running && recorded < frame_count) {
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

        /* always feed the recorder */
        ret = VENC_SendFrame(REC_VENC_CHN, &decFrame, 0);
        if (ret != 0) {
            fprintf(stderr, "[rec] VENC_SendFrame ret=%d\n", ret);
        }

        /* periodically feed the snapshot encoder */
        processed++;
        if ((processed % snap_interval) == 0) {
            ret = VENC_SendFrame(SNAP_VENC_CHN, &decFrame, 200);
            if (ret == ERR_VENC_OK) {
                take_snapshot(pszDir, snap_idx++);
            } else {
                fprintf(stderr, "  [snap] VENC_SendFrame error %d\n", ret);
            }
        }

        VDEC_ReleaseFrame(VDEC_CHN_ID, decFrame.ulBufferId);

        drain_record(fpRec, &recorded);
    }

    /* final drain */
    drain_record(fpRec, &recorded);

    printf("\nDone: recorded %d H.264 frames, captured %d JPEG snapshots\n", recorded, snap_idx);
    ret = (recorded > 0) ? 0 : 1;

cleanup_venc:
    if (fpRec) {
        fclose(fpRec);
        fpRec = NULL;
    }
    if (venc_ready & 2) {
        VENC_DisableChn(SNAP_VENC_CHN);
        VENC_DestroyChn(SNAP_VENC_CHN);
    }
    if (venc_ready & 1) {
        VENC_DisableChn(REC_VENC_CHN);
        VENC_DestroyChn(REC_VENC_CHN);
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
