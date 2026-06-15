/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    sample_vi_dual_stream.c
 * @Date      :    2026-05-20
 * @Brief     :    Sample: one VI sensor -> main + sub streams (dual encode).
 *
 *                 A very common surveillance / vision-edge layout: a single
 *                 camera feeds two simultaneous encoded streams at different
 *                 resolutions and codecs.
 *
 *                   VI dev0/chn0 (1920x1080) --bind--> VENC0 H.264  -> main.h264
 *                   VI dev0/chn1 (1280x720)  --bind--> VENC1 H.265  -> sub.h265
 *
 *                 The "main" stream is the high-quality archive feed; the
 *                 "sub" stream is a lighter feed for preview / AI inference.
 *                 Each VENC channel is drained by its own reader thread.
 *
 * Run:
 *   ./sample_vi_dual_stream                 # 300 frames, defaults
 *   ./sample_vi_dual_stream 600             # 600 frames
 *   ./sample_vi_dual_stream 600 main.h264 sub.h265
 *
 * Play: ffplay main.h264   /   ffplay sub.h265
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
#define NUM_STREAM 2
#define MAIN_WIDTH 1920
#define MAIN_HEIGHT 1080
#define SUB_WIDTH 1280
#define SUB_HEIGHT 720
#define MAIN_BITRATE_KBPS 4000
#define SUB_BITRATE_KBPS 1500
#define DEFAULT_FPS 30
#define DEFAULT_GOP 30
#define DEFAULT_FRAMES 300

typedef struct StreamDesc {
    const char *pszTag;
    S32 s32ViChn;
    S32 s32VencChn;
    U32 u32Width;
    U32 u32Height;
    MppStreamCodecType eCodec;
    U32 u32BitrateKbps;
    char szOutPath[128];
} StreamDesc;

typedef struct ReaderArg {
    const StreamDesc *pstDesc;
    FILE *fp;
    int frame_count;
    int saved;
    int error;
} ReaderArg;

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

static void *reader_thread(void *arg) {
    ReaderArg *ra = (ReaderArg *)arg;
    StreamBufferInfo stream;
    S32 ret;

    while (g_running && ra->saved < ra->frame_count) {
        memset(&stream, 0, sizeof(stream));
        ret = VENC_GetStream(ra->pstDesc->s32VencChn, &stream, 3000);
        if (ret != ERR_VENC_OK) {
            if (ret == ERR_VENC_NO_STREAM || ret == ERR_VENC_TIMEOUT) {
                continue;
            }
            fprintf(stderr, "[%s] VENC_GetStream error %d\n", ra->pstDesc->pszTag, ret);
            if (++ra->error > 10) {
                break;
            }
            continue;
        }

        if (fwrite(stream.pu8Addr, 1, stream.u32Size, ra->fp) != stream.u32Size) {
            fprintf(stderr, "[%s] short write\n", ra->pstDesc->pszTag);
        }
        VENC_ReleaseStream(ra->pstDesc->s32VencChn, &stream);
        ra->saved++;

        if (ra->saved % 30 == 0) {
            printf("  [%s] saved %d / %d\n", ra->pstDesc->pszTag, ra->saved, ra->frame_count);
        }
    }
    return NULL;
}

static S32 setup_vi_chn(const StreamDesc *pstDesc) {
    ViChnAttrS stChnAttr;
    S32 ret;

    memset(&stChnAttr, 0, sizeof(stChnAttr));
    stChnAttr.eChnType = VI_CHN_TYPE_PHYSICAL;
    stChnAttr.ePixelFormat = MPP_PIXEL_FORMAT_UYVY;
    stChnAttr.u32Width = pstDesc->u32Width;
    stChnAttr.u32Height = pstDesc->u32Height;
    stChnAttr.eStrideAlign = VI_STRIDE_ALIGN_DEFAULT;
    stChnAttr.u32Depth = 0; /* bind-only */

    ret = VI_SetChnAttr(VI_DEV_ID, pstDesc->s32ViChn, &stChnAttr);
    if (ret != 0) {
        fprintf(stderr, "VI_SetChnAttr(%s) failed: %d\n", pstDesc->pszTag, ret);
        return ret;
    }
    ret = VI_EnableChn(VI_DEV_ID, pstDesc->s32ViChn);
    if (ret != 0) {
        fprintf(stderr, "VI_EnableChn(%s) failed: %d\n", pstDesc->pszTag, ret);
    }
    return ret;
}

static S32 setup_venc_chn(const StreamDesc *pstDesc) {
    VencChnAttr stAttr;
    S32 ret;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eCodecType = pstDesc->eCodec;
    stAttr.eInputPixelFormat = MPP_PIXEL_FORMAT_UYVY;
    stAttr.u32Width = pstDesc->u32Width;
    stAttr.u32Height = pstDesc->u32Height;
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

static void usage(const char *prog) {
    printf("Usage: %s [frame_count] [main_out] [sub_out]\n", prog);
    printf("  frame_count  frames per stream (default %d)\n", DEFAULT_FRAMES);
    printf("  main_out     main H.264 file (default ./main.h264, %dx%d)\n", MAIN_WIDTH, MAIN_HEIGHT);
    printf("  sub_out      sub  H.265 file (default ./sub.h265,  %dx%d)\n", SUB_WIDTH, SUB_HEIGHT);
}

int main(int argc, char *argv[]) {
    int frame_count = DEFAULT_FRAMES;
    S32 ret;
    S32 i;
    S32 vi_enabled_cnt = 0;
    S32 venc_ready_cnt = 0;
    S32 bound_cnt = 0;
    StreamDesc astDesc[NUM_STREAM];
    MppNode astViNode[NUM_STREAM];
    MppNode astVencNode[NUM_STREAM];
    FILE *apFp[NUM_STREAM];
    pthread_t atid[NUM_STREAM];
    ReaderArg astReader[NUM_STREAM];

    memset(apFp, 0, sizeof(apFp));
    memset(atid, 0, sizeof(atid));
    memset(astReader, 0, sizeof(astReader));

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

    /* main stream */
    memset(astDesc, 0, sizeof(astDesc));
    astDesc[0].pszTag = "main";
    astDesc[0].s32ViChn = 0;
    astDesc[0].s32VencChn = 0;
    astDesc[0].u32Width = MAIN_WIDTH;
    astDesc[0].u32Height = MAIN_HEIGHT;
    astDesc[0].eCodec = MPP_STREAM_CODEC_H264;
    astDesc[0].u32BitrateKbps = MAIN_BITRATE_KBPS;
    snprintf(astDesc[0].szOutPath, sizeof(astDesc[0].szOutPath), "%s", (argc > 2) ? argv[2] : "./main.h264");

    /* sub stream */
    astDesc[1].pszTag = "sub";
    astDesc[1].s32ViChn = 1;
    astDesc[1].s32VencChn = 1;
    astDesc[1].u32Width = SUB_WIDTH;
    astDesc[1].u32Height = SUB_HEIGHT;
    astDesc[1].eCodec = MPP_STREAM_CODEC_H265;
    astDesc[1].u32BitrateKbps = SUB_BITRATE_KBPS;
    snprintf(astDesc[1].szOutPath, sizeof(astDesc[1].szOutPath), "%s", (argc > 3) ? argv[3] : "./sub.h265");

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Sample: VI Dual-Stream (main + sub) ===\n");
    for (i = 0; i < NUM_STREAM; i++) {
        printf("  %-4s: %ux%u %s @ %u kbps -> %s\n",
            astDesc[i].pszTag,
            astDesc[i].u32Width,
            astDesc[i].u32Height,
            codec_name(astDesc[i].eCodec),
            astDesc[i].u32BitrateKbps,
            astDesc[i].szOutPath);
    }
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

    /* ---- VI device ---- */
    ret = VI_Init();
    if (ret != 0) {
        fprintf(stderr, "VI_Init failed: %d\n", ret);
        goto cleanup_vb;
    }

    ViDevAttrS stDevAttr;
    memset(&stDevAttr, 0, sizeof(stDevAttr));
    stDevAttr.eWorkMode = VI_WORK_MODE_ONLINE;
    stDevAttr.u32Width = MAIN_WIDTH;
    stDevAttr.u32Height = MAIN_HEIGHT;
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

    for (i = 0; i < NUM_STREAM; i++) {
        if (setup_vi_chn(&astDesc[i]) != 0) {
            goto cleanup_vi_chns;
        }
        vi_enabled_cnt++;
        printf("VI chn%d enabled: %ux%u UYVY\n", astDesc[i].s32ViChn, astDesc[i].u32Width, astDesc[i].u32Height);
    }

    /* ---- VENC ---- */
    ret = VENC_Init();
    if (ret != 0) {
        fprintf(stderr, "VENC_Init failed: %d\n", ret);
        goto cleanup_vi_chns;
    }
    for (i = 0; i < NUM_STREAM; i++) {
        if (setup_venc_chn(&astDesc[i]) != 0) {
            goto cleanup_venc;
        }
        venc_ready_cnt++;
        printf("VENC chn%d enabled: %s %ux%u\n",
            astDesc[i].s32VencChn,
            codec_name(astDesc[i].eCodec),
            astDesc[i].u32Width,
            astDesc[i].u32Height);
    }

    /* ---- bind VI -> VENC ---- */
    for (i = 0; i < NUM_STREAM; i++) {
        astViNode[i] = (MppNode){MPP_ID_VI, VI_DEV_ID, astDesc[i].s32ViChn};
        astVencNode[i] = (MppNode){MPP_ID_VENC, 0, astDesc[i].s32VencChn};
        ret = SYS_Bind(&astViNode[i], &astVencNode[i]);
        if (ret != 0) {
            fprintf(stderr, "SYS_Bind(%s) failed: %d\n", astDesc[i].pszTag, ret);
            goto cleanup_bind;
        }
        bound_cnt++;
        printf("Bind: VI(dev=%d,chn=%d) -> VENC(chn=%d) [%s]\n",
            VI_DEV_ID,
            astDesc[i].s32ViChn,
            astDesc[i].s32VencChn,
            astDesc[i].pszTag);
    }

    /* ---- output files + reader threads ---- */
    for (i = 0; i < NUM_STREAM; i++) {
        apFp[i] = fopen(astDesc[i].szOutPath, "wb");
        if (apFp[i] == NULL) {
            fprintf(stderr, "fopen(%s) failed: %s\n", astDesc[i].szOutPath, strerror(errno));
            ret = -1;
            goto cleanup_threads;
        }
        astReader[i].pstDesc = &astDesc[i];
        astReader[i].fp = apFp[i];
        astReader[i].frame_count = frame_count;

        ret = pthread_create(&atid[i], NULL, reader_thread, &astReader[i]);
        if (ret != 0) {
            fprintf(stderr, "pthread_create(%s) failed: %d\n", astDesc[i].pszTag, ret);
            atid[i] = 0;
            goto cleanup_threads;
        }
        printf("Reader thread started [%s] -> %s\n", astDesc[i].pszTag, astDesc[i].szOutPath);
    }

    for (i = 0; i < NUM_STREAM; i++) {
        if (atid[i]) {
            pthread_join(atid[i], NULL);
            atid[i] = 0;
        }
    }

    printf("\nAll streams done:\n");
    ret = 0;
    for (i = 0; i < NUM_STREAM; i++) {
        printf("  %-4s: %d frames -> %s\n", astDesc[i].pszTag, astReader[i].saved, astDesc[i].szOutPath);
        if (astReader[i].saved == 0) {
            ret = 1;
        }
    }

cleanup_threads:
    g_running = 0;
    for (i = 0; i < NUM_STREAM; i++) {
        if (atid[i]) {
            pthread_join(atid[i], NULL);
        }
        if (apFp[i]) {
            fclose(apFp[i]);
        }
    }

cleanup_bind:
    for (i = 0; i < bound_cnt; i++) {
        SYS_UnBind(&astViNode[i], &astVencNode[i]);
    }

cleanup_venc:
    for (i = 0; i < venc_ready_cnt; i++) {
        VENC_DisableChn(astDesc[i].s32VencChn);
        VENC_DestroyChn(astDesc[i].s32VencChn);
    }
    VENC_Exit();

cleanup_vi_chns:
    for (i = 0; i < vi_enabled_cnt; i++) {
        VI_DisableChn(VI_DEV_ID, astDesc[i].s32ViChn);
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
