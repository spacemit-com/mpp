/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    sample_vi_k3_4venc_bind.c
 * @Brief     :    Demo: K3 VI 4-channel → 4×VENC via SYS bind, save 4 H.264 files.
 *
 *                 Device mapping:
 *                   dev0/chn0 → /dev/video0 → VENC chn0 → output_chn0.h264  (full rate, reference)
 *                   dev0/chn1 → /dev/video1 → VENC chn1 → output_chn1.h264  (1/2 rate)
 *                   dev0/chn2 → /dev/video2 → VENC chn2 → output_chn2.h264  (1/3 rate)
 *                   dev0/chn3 → /dev/video3 → VENC chn3 → output_chn3.h264  (full rate)
 *
 *                 chn0 is the reference channel. When it saves frame_count frames
 *                 all other channels stop and report their actual saved counts.
 *
 *                 Usage: ./sample_vi_k3_4venc_bind [frame_count]
 *                   default frame_count = 300  (~10 s @ 30 fps)
 *
 *                 Play:  ffplay output_chn0.h264
 *------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "sys/sys_api.h"
#include "sys/vb_api.h"
#include "vi/vi_api.h"
#include "vi/vi_type.h"
#include "venc/venc_api.h"
#include "venc/venc_type.h"

#define VI_DEV_ID      0
#define NUM_CHN        4
#define DEFAULT_WIDTH  1280
#define DEFAULT_HEIGHT 720
#define DEFAULT_FRAMES 300

/* Global stop flag: set by chn0 when it reaches frame_count */
static volatile int g_stop = 0;

typedef struct {
    int    venc_chn;
    FILE  *fp;
    int    frame_count;   /* only meaningful for trigger channel (chn0) */
    int    is_trigger;    /* 1 = chn0, drives g_stop */
    int    saved;
    int    error;
    double elapsed_s;
} ReaderArg;

static void *reader_thread(void *arg) {
    ReaderArg *ra = arg;
    StreamBufferInfo stream;
    S32 ret;
    struct timespec t0, t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    while (!g_stop) {
        memset(&stream, 0, sizeof(stream));
        ret = VENC_GetStream(ra->venc_chn, &stream, 100);  /* short timeout to check g_stop */
        if (ret != 0)
            continue;

        if (fwrite(stream.pu8Addr, 1, stream.u32Size, ra->fp) != stream.u32Size)
            fprintf(stderr, "[chn%d] short write on frame %d\n", ra->venc_chn, ra->saved);

        VENC_ReleaseStream(ra->venc_chn, &stream);
        ra->saved++;

        if (ra->saved % 30 == 0) {
            if (ra->is_trigger)
                printf("[chn0] saved %d / %d frames\n", ra->saved, ra->frame_count);
            else
                printf("[chn%d] saved %d frames so far\n", ra->venc_chn, ra->saved);
        }

        /* Trigger channel: stop all when frame_count reached */
        if (ra->is_trigger && ra->saved >= ra->frame_count) {
            g_stop = 1;
            break;
        }
    }

    /* Ensure other threads are unblocked if this thread exits for any reason */
    if (ra->is_trigger)
        g_stop = 1;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    ra->elapsed_s = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
    return NULL;
}

static void usage(const char *prog) {
    printf("Usage: %s [frame_count]\n", prog);
    printf("  frame_count  frames for chn0 (reference channel, default %d)\n", DEFAULT_FRAMES);
}

int main(int argc, char **argv) {
    int frame_count = DEFAULT_FRAMES;
    S32 ret = 0;
    int i;
    int vi_enabled_cnt  = 0;
    int venc_created_cnt = 0;
    int venc_enabled_cnt = 0;
    int bound_cnt       = 0;

    MppNode viNode[NUM_CHN];
    MppNode vencNode[NUM_CHN];
    FILE   *fps[NUM_CHN];
    pthread_t tids[NUM_CHN];
    ReaderArg reader_args[NUM_CHN];
    char outpath[64];

    memset(fps, 0, sizeof(fps));
    memset(tids, 0, sizeof(tids));
    memset(reader_args, 0, sizeof(reader_args));

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

    /* ------------------------------------------------------------------ */
    /* 1. System init                                                       */
    /* ------------------------------------------------------------------ */
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

    /* ------------------------------------------------------------------ */
    /* 2. VI init: one device, 4 channels                                   */
    /* ------------------------------------------------------------------ */
    ret = VI_Init();
    if (ret != 0) {
        fprintf(stderr, "VI_Init failed: %d\n", ret);
        goto cleanup_vb;
    }

    ViDevAttrS devAttr;
    memset(&devAttr, 0, sizeof(devAttr));
    devAttr.eWorkMode     = VI_WORK_MODE_ONLINE;
    devAttr.u32Width      = DEFAULT_WIDTH;
    devAttr.u32Height     = DEFAULT_HEIGHT;
    devAttr.u32MipiLaneNum = 4;
    devAttr.u32mbps       = 800;

    ret = VI_SetDevAttr(VI_DEV_ID, &devAttr);
    if (ret != 0) {
        fprintf(stderr, "VI_SetDevAttr failed: %d\n", ret);
        goto cleanup_vi;
    }

    ret = VI_EnableDev(VI_DEV_ID);
    if (ret != 0) {
        fprintf(stderr, "VI_EnableDev failed: %d\n", ret);
        goto cleanup_vi;
    }

    for (i = 0; i < NUM_CHN; i++) {
        ViChnAttrS chnAttr;
        memset(&chnAttr, 0, sizeof(chnAttr));
        chnAttr.eChnType     = VI_CHN_TYPE_PHYSICAL;
        chnAttr.ePixelFormat = MPP_PIXEL_FORMAT_UYVY;
        chnAttr.u32Width     = DEFAULT_WIDTH;
        chnAttr.u32Height    = DEFAULT_HEIGHT;
        chnAttr.eStrideAlign = VI_STRIDE_ALIGN_DEFAULT;
        chnAttr.u32Depth     = 0;  /* bind-only */

        ret = VI_SetChnAttr(VI_DEV_ID, i, &chnAttr);
        if (ret != 0) {
            fprintf(stderr, "VI_SetChnAttr(chn%d) failed: %d\n", i, ret);
            goto cleanup_vi_chns;
        }

        ret = VI_EnableChn(VI_DEV_ID, i);
        if (ret != 0) {
            fprintf(stderr, "VI_EnableChn(chn%d) failed: %d\n", i, ret);
            goto cleanup_vi_chns;
        }
        vi_enabled_cnt++;
        printf("VI chn%d enabled: /dev/video%d %dx%d UYVY\n",
                i, i, DEFAULT_WIDTH, DEFAULT_HEIGHT);
    }

    /* ------------------------------------------------------------------ */
    /* 2b. Frame rate control: chn1 half-rate, chn2 one-third rate         */
    /* ------------------------------------------------------------------ */
    {
        ViFrameRateCtrlS frc;

        /* chn1: output 1 out of every 2 input frames → ~15 fps */
        frc.u32InputFrameStep  = 2;
        frc.u32OutputFrameStep = 1;
        ret = VI_SetChnFrameRate(VI_DEV_ID, 1, &frc);
        if (ret != 0)
            fprintf(stderr, "VI_SetChnFrameRate(chn1) failed: %d\n", ret);
        else
            printf("VI chn1 frame rate: %u/%u (every %u-th frame)\n",
                frc.u32OutputFrameStep, frc.u32InputFrameStep, frc.u32InputFrameStep);

        /* chn2: output 1 out of every 3 input frames → ~10 fps */
        frc.u32InputFrameStep  = 3;
        frc.u32OutputFrameStep = 1;
        ret = VI_SetChnFrameRate(VI_DEV_ID, 2, &frc);
        if (ret != 0)
            fprintf(stderr, "VI_SetChnFrameRate(chn2) failed: %d\n", ret);
        else
            printf("VI chn2 frame rate: %u/%u (every %u-th frame)\n",
                frc.u32OutputFrameStep, frc.u32InputFrameStep, frc.u32InputFrameStep);
    }

    /* ------------------------------------------------------------------ */
    /* 3. VENC init: 4 channels, each H.264 CBR 2 Mbps                     */
    /* ------------------------------------------------------------------ */
    ret = VENC_Init();
    if (ret != 0) {
        fprintf(stderr, "VENC_Init failed: %d\n", ret);
        goto cleanup_vi_chns;
    }

    for (i = 0; i < NUM_CHN; i++) {
        VencChnAttr vencAttr;
        memset(&vencAttr, 0, sizeof(vencAttr));
        vencAttr.eCodecType        = MPP_STREAM_CODEC_H264;
        vencAttr.eInputPixelFormat = MPP_PIXEL_FORMAT_UYVY;
        vencAttr.u32Width          = DEFAULT_WIDTH;
        vencAttr.u32Height         = DEFAULT_HEIGHT;
        vencAttr.eRcMode           = VENC_RC_MODE_CBR;
        vencAttr.u32Bitrate        = 2000 * 1000;  /* 2 Mbps */
        vencAttr.u32FrameRate      = 30;
        vencAttr.u32Gop            = 30;

        ret = VENC_CreateChn(i, &vencAttr);
        if (ret != 0) {
            fprintf(stderr, "VENC_CreateChn(chn%d) failed: %d\n", i, ret);
            goto cleanup_venc;
        }
        venc_created_cnt++;

        ret = VENC_EnableChn(i);
        if (ret != 0) {
            fprintf(stderr, "VENC_EnableChn(chn%d) failed: %d\n", i, ret);
            goto cleanup_venc;
        }
        venc_enabled_cnt++;
        printf("VENC chn%d enabled: H.264 %dx%d 2Mbps\n", i, DEFAULT_WIDTH, DEFAULT_HEIGHT);
    }

    /* ------------------------------------------------------------------ */
    /* 4. SYS bind: VI(dev0, chnN) → VENC(chnN) for N=0..3                 */
    /* ------------------------------------------------------------------ */
    for (i = 0; i < NUM_CHN; i++) {
        viNode[i]   = (MppNode){ MPP_ID_VI,   VI_DEV_ID, i };
        vencNode[i] = (MppNode){ MPP_ID_VENC, 0,         i };

        ret = SYS_Bind(&viNode[i], &vencNode[i]);
        if (ret != 0) {
            fprintf(stderr, "SYS_Bind(chn%d) failed: %d\n", i, ret);
            goto cleanup_bind;
        }
        bound_cnt++;
        printf("Bind: VI(dev=%d,chn=%d) → VENC(chn=%d)\n", VI_DEV_ID, i, i);
    }

    /* ------------------------------------------------------------------ */
    /* 5. Open output files and launch one reader thread per channel        */
    /* ------------------------------------------------------------------ */
    for (i = 0; i < NUM_CHN; i++) {
        snprintf(outpath, sizeof(outpath), "./output_chn%d.h264", i);
        fps[i] = fopen(outpath, "wb");
        if (fps[i] == NULL) {
            fprintf(stderr, "fopen(%s) failed: %s\n", outpath, strerror(errno));
            ret = -1;
            goto cleanup_threads;
        }

        reader_args[i].venc_chn    = i;
        reader_args[i].fp          = fps[i];
        reader_args[i].frame_count = frame_count;
        reader_args[i].is_trigger  = (i == 0) ? 1 : 0;
        reader_args[i].saved       = 0;
        reader_args[i].error       = 0;

        ret = pthread_create(&tids[i], NULL, reader_thread, &reader_args[i]);
        if (ret != 0) {
            fprintf(stderr, "pthread_create(chn%d) failed: %d\n", i, ret);
            tids[i] = 0;
            goto cleanup_threads;
        }
        printf("Reader thread started for chn%d → %s\n", i, outpath);
    }

    /* ------------------------------------------------------------------ */
    /* 6. Wait for all reader threads to finish                             */
    /* ------------------------------------------------------------------ */
    for (i = 0; i < NUM_CHN; i++) {
        if (tids[i])
            pthread_join(tids[i], NULL);
    }

    printf("\nResults (chn0 is reference, others stopped when chn0 reached %d frames):\n",
        frame_count);
    printf("  %-6s  %-8s  %-10s  %-12s  %s\n",
        "chn", "frames", "elapsed(s)", "actual fps", "file");
    for (i = 0; i < NUM_CHN; i++) {
        snprintf(outpath, sizeof(outpath), "./output_chn%d.h264", i);
        double fps_actual = (reader_args[i].elapsed_s > 0.0)
            ? reader_args[i].saved / reader_args[i].elapsed_s : 0.0;
        printf("  chn%-3d  %-8d  %-10.2f  %-12.1f  %s%s\n",
            i, reader_args[i].saved, reader_args[i].elapsed_s, fps_actual, outpath,
            (i == 0) ? "  [reference]" : "");
    }

    /* Success only requires chn0 to hit its target */
    ret = (reader_args[0].saved >= frame_count) ? 0 : 1;

    /* ------------------------------------------------------------------ */
    /* 7. Cleanup (reverse order)                                           */
    /* ------------------------------------------------------------------ */
cleanup_threads:
    for (i = 0; i < NUM_CHN; i++) {
        if (fps[i]) fclose(fps[i]);
    }

cleanup_bind:
    for (i = 0; i < bound_cnt; i++)
        SYS_UnBind(&viNode[i], &vencNode[i]);

cleanup_venc:
    for (i = 0; i < venc_enabled_cnt; i++)
        VENC_DisableChn(i);
    for (i = 0; i < venc_created_cnt; i++)
        VENC_DestroyChn(i);
    VENC_Exit();

cleanup_vi_chns:
    for (i = 0; i < vi_enabled_cnt; i++)
        VI_DisableChn(VI_DEV_ID, i);
    VI_DisableDev(VI_DEV_ID);

cleanup_vi:
    VI_DeInit();

cleanup_vb:
    VB_Exit();

cleanup_sys:
    SYS_Exit();

    return ret;
}
