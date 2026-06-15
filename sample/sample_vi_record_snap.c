/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    sample_vi_record_snap.c
 * @Date      :    2026-05-20
 * @Brief     :    Sample: VI continuous H.264 recording + periodic JPEG snapshot.
 *
 *                 Mixes the two MPP data-movement styles in one pipeline:
 *
 *                   VI dev0/chn0 (1080p) --SYS_Bind--> VENC0 (H.264) -> record.h264
 *                       (framework auto-feeds the encoder, a reader thread drains it)
 *
 *                   VI dev0/chn1 (1080p, u32Depth>0) --manual GetChnFrame-->
 *                       every N frames -> VENC1 (MJPEG) -> snap_XXXX.jpg
 *
 *                 This is the classic "record while grabbing stills" use case
 *                 (event snapshots, AI keyframe capture, thumbnails), and shows
 *                 how a bind chain and a manual pull loop coexist on one sensor.
 *
 * Run:
 *   ./sample_vi_record_snap                    # 300 frames, snap every 30
 *   ./sample_vi_record_snap 600 15             # 600 frames, snap every 15
 *   ./sample_vi_record_snap 600 15 ./out       # output dir prefix ./out
 *
 * Play:  ffplay record.h264   /   view snap_0000.jpg
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sys/sys_api.h"
#include "sys/vb_api.h"
#include "venc/venc_api.h"
#include "venc/venc_type.h"
#include "vi/vi_api.h"
#include "vi/vi_type.h"

#define VI_DEV_ID 0
#define REC_VI_CHN 0
#define SNAP_VI_CHN 1
#define REC_VENC_CHN 0
#define SNAP_VENC_CHN 1
#define FRAME_WIDTH 1920
#define FRAME_HEIGHT 1080
#define REC_BITRATE_KBPS 4000
#define DEFAULT_FPS 30
#define DEFAULT_GOP 30
#define DEFAULT_FRAMES 300
#define DEFAULT_SNAP_INTERVAL 30
#define SNAP_JPEG_QP 80

typedef struct RecorderArg {
    int frame_count;
    int saved;
    int error;
    FILE *fp;
} RecorderArg;

static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Reader thread: drains the bound H.264 recording channel to a file. */
static void *recorder_thread(void *arg) {
    RecorderArg *ra = (RecorderArg *)arg;
    StreamBufferInfo stream;
    S32 ret;

    while (g_running && ra->saved < ra->frame_count) {
        memset(&stream, 0, sizeof(stream));
        ret = VENC_GetStream(REC_VENC_CHN, &stream, 3000);
        if (ret != ERR_VENC_OK) {
            if (ret == ERR_VENC_NO_STREAM || ret == ERR_VENC_TIMEOUT) {
                continue;
            }
            fprintf(stderr, "[rec] VENC_GetStream error %d\n", ret);
            if (++ra->error > 10) {
                break;
            }
            continue;
        }
        if (fwrite(stream.pu8Addr, 1, stream.u32Size, ra->fp) != stream.u32Size) {
            fprintf(stderr, "[rec] short write\n");
        }
        VENC_ReleaseStream(REC_VENC_CHN, &stream);
        ra->saved++;
        if (ra->saved % 30 == 0) {
            printf("  [rec] recorded %d / %d frames\n", ra->saved, ra->frame_count);
        }
    }
    return NULL;
}

/* Encode one VI frame as a JPEG and write it to disk. */
static S32 take_snapshot(VideoFrameInfo *pstFrame, const char *pszDir, int idx) {
    StreamBufferInfo stream;
    char path[160];
    FILE *fp;
    S32 ret;

    pstFrame->eFrameType = FRAME_TYPE_VENC;
    ret = VENC_SendFrame(SNAP_VENC_CHN, pstFrame, 200);
    if (ret != ERR_VENC_OK) {
        fprintf(stderr, "  [snap] VENC_SendFrame error %d\n", ret);
        return ret;
    }

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

static S32 setup_vi_chn(S32 s32Chn, U32 u32Depth) {
    ViChnAttrS stChnAttr;
    S32 ret;

    memset(&stChnAttr, 0, sizeof(stChnAttr));
    stChnAttr.eChnType = VI_CHN_TYPE_PHYSICAL;
    stChnAttr.ePixelFormat = MPP_PIXEL_FORMAT_UYVY;
    stChnAttr.u32Width = FRAME_WIDTH;
    stChnAttr.u32Height = FRAME_HEIGHT;
    stChnAttr.eStrideAlign = VI_STRIDE_ALIGN_DEFAULT;
    stChnAttr.u32Depth = u32Depth;

    ret = VI_SetChnAttr(VI_DEV_ID, s32Chn, &stChnAttr);
    if (ret != 0) {
        fprintf(stderr, "VI_SetChnAttr(chn%d) failed: %d\n", s32Chn, ret);
        return ret;
    }
    ret = VI_EnableChn(VI_DEV_ID, s32Chn);
    if (ret != 0) {
        fprintf(stderr, "VI_EnableChn(chn%d) failed: %d\n", s32Chn, ret);
    }
    return ret;
}

static S32 setup_venc_chn(S32 s32Chn, MppStreamCodecType eCodec) {
    VencChnAttr stAttr;
    S32 ret;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eCodecType = eCodec;
    stAttr.eInputPixelFormat = MPP_PIXEL_FORMAT_UYVY;
    stAttr.u32Width = FRAME_WIDTH;
    stAttr.u32Height = FRAME_HEIGHT;
    stAttr.u32FrameRate = DEFAULT_FPS;
    stAttr.u32Gop = DEFAULT_GOP;

    if (eCodec == MPP_STREAM_CODEC_MJPEG) {
        /* MJPEG: drive quality through fixed QP. */
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

static void usage(const char *prog) {
    printf("Usage: %s [frame_count] [snap_interval] [out_dir]\n", prog);
    printf("  frame_count    H.264 frames to record (default %d)\n", DEFAULT_FRAMES);
    printf("  snap_interval  take a JPEG every N frames (default %d)\n", DEFAULT_SNAP_INTERVAL);
    printf("  out_dir        output directory (default .)\n");
}

int main(int argc, char *argv[]) {
    int frame_count = DEFAULT_FRAMES;
    int snap_interval = DEFAULT_SNAP_INTERVAL;
    const char *pszDir = ".";
    char szRecPath[160];
    S32 ret;
    S32 vi_enabled_cnt = 0;
    S32 venc_ready = 0; /* bitmask: 1=rec, 2=snap */
    S32 bound = 0;
    int grabbed = 0;
    int snap_idx = 0;
    MppNode stViNode;
    MppNode stVencNode;
    FILE *fpRec = NULL;
    pthread_t tid = 0;
    RecorderArg stRec;

    memset(&stRec, 0, sizeof(stRec));

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
    snprintf(szRecPath, sizeof(szRecPath), "%s/record.h264", pszDir);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Sample: VI Record + JPEG Snapshot ===\n");
    printf("  Record : %ux%u H.264 %u kbps -> %s\n", FRAME_WIDTH, FRAME_HEIGHT, REC_BITRATE_KBPS, szRecPath);
    printf("  Snap   : %ux%u MJPEG QP%d every %d frames -> %s/snap_*.jpg\n",
        FRAME_WIDTH,
        FRAME_HEIGHT,
        SNAP_JPEG_QP,
        snap_interval,
        pszDir);
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

    /* ---- VI ---- */
    ret = VI_Init();
    if (ret != 0) {
        fprintf(stderr, "VI_Init failed: %d\n", ret);
        goto cleanup_vb;
    }

    ViDevAttrS stDevAttr;
    memset(&stDevAttr, 0, sizeof(stDevAttr));
    stDevAttr.eWorkMode = VI_WORK_MODE_ONLINE;
    stDevAttr.u32Width = FRAME_WIDTH;
    stDevAttr.u32Height = FRAME_HEIGHT;
    stDevAttr.u32MipiLaneNum = 4;
    stDevAttr.u32mbps = 800;

    ret = VI_SetDevAttr(VI_DEV_ID, &stDevAttr);
    if (ret != 0) {
        fprintf(stderr, "VI_SetDevAttr failed: %d\n", ret);
        goto cleanup_vi;
    }
    ret = VI_EnableDev(VI_DEV_ID);
    if (ret != 0) {
        fprintf(stderr, "VI_EnableDev failed: %d\n", ret);
        goto cleanup_vi;
    }

    if (setup_vi_chn(REC_VI_CHN, 0) != 0) { /* bind-only */
        goto cleanup_vi_chns;
    }
    vi_enabled_cnt++;
    if (setup_vi_chn(SNAP_VI_CHN, 2) != 0) { /* manual GetChnFrame */
        goto cleanup_vi_chns;
    }
    vi_enabled_cnt++;
    printf("VI chn%d (record, bind) + chn%d (snapshot, manual) enabled\n", REC_VI_CHN, SNAP_VI_CHN);

    /* ---- VENC ---- */
    ret = VENC_Init();
    if (ret != 0) {
        fprintf(stderr, "VENC_Init failed: %d\n", ret);
        goto cleanup_vi_chns;
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

    /* ---- bind record path only: VI chn0 -> VENC0 ---- */
    stViNode = (MppNode){MPP_ID_VI, VI_DEV_ID, REC_VI_CHN};
    stVencNode = (MppNode){MPP_ID_VENC, 0, REC_VENC_CHN};
    ret = SYS_Bind(&stViNode, &stVencNode);
    if (ret != 0) {
        fprintf(stderr, "SYS_Bind(record) failed: %d\n", ret);
        goto cleanup_venc;
    }
    bound = 1;
    printf("Bind: VI(dev=%d,chn=%d) -> VENC(chn=%d) [record]\n", VI_DEV_ID, REC_VI_CHN, REC_VENC_CHN);

    /* ---- recorder thread ---- */
    fpRec = fopen(szRecPath, "wb");
    if (fpRec == NULL) {
        fprintf(stderr, "fopen(%s) failed: %s\n", szRecPath, strerror(errno));
        ret = -1;
        goto cleanup_bind;
    }
    stRec.frame_count = frame_count;
    stRec.fp = fpRec;
    ret = pthread_create(&tid, NULL, recorder_thread, &stRec);
    if (ret != 0) {
        fprintf(stderr, "pthread_create(recorder) failed: %d\n", ret);
        tid = 0;
        goto cleanup_file;
    }

    /* ---- main loop: manual snapshot from VI chn1 ---- */
    printf("Capturing... (Ctrl+C to stop)\n");
    while (g_running && stRec.saved < frame_count) {
        VideoFrameInfo stFrame;
        memset(&stFrame, 0, sizeof(stFrame));
        ret = VI_GetChnFrame(VI_DEV_ID, SNAP_VI_CHN, &stFrame, 200);
        if (ret != 0) {
            continue;
        }
        grabbed++;
        if ((grabbed % snap_interval) == 0) {
            take_snapshot(&stFrame, pszDir, snap_idx++);
        }
        VI_ReleaseChnFrame(VI_DEV_ID, SNAP_VI_CHN, &stFrame);
    }

    if (tid) {
        pthread_join(tid, NULL);
        tid = 0;
    }

    printf("\nDone: recorded %d H.264 frames, captured %d JPEG snapshots\n", stRec.saved, snap_idx);
    ret = (stRec.saved > 0) ? 0 : 1;

cleanup_file:
    g_running = 0;
    if (tid) {
        pthread_join(tid, NULL);
    }
    if (fpRec) {
        fclose(fpRec);
    }

cleanup_bind:
    if (bound) {
        SYS_UnBind(&stViNode, &stVencNode);
    }

cleanup_venc:
    if (venc_ready & 2) {
        VENC_DisableChn(SNAP_VENC_CHN);
        VENC_DestroyChn(SNAP_VENC_CHN);
    }
    if (venc_ready & 1) {
        VENC_DisableChn(REC_VENC_CHN);
        VENC_DestroyChn(REC_VENC_CHN);
    }
    VENC_Exit();

cleanup_vi_chns:
    if (vi_enabled_cnt > 1) {
        VI_DisableChn(VI_DEV_ID, SNAP_VI_CHN);
    }
    if (vi_enabled_cnt > 0) {
        VI_DisableChn(VI_DEV_ID, REC_VI_CHN);
    }
    VI_DisableDev(VI_DEV_ID);

cleanup_vi:
    VI_DeInit();

cleanup_vb:
    VB_Exit();

cleanup_sys:
    SYS_Exit();

    printf("\n=== Sample finished ===\n");
    return ret;
}
