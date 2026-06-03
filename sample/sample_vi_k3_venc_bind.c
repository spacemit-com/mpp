/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    sample_vi_k3_venc_bind.c
 * @Brief     :    Demo: K3 VI → VENC via SYS bind, save H.264 elementary stream.
 *
 *                 Set u32Depth=0 so VI runs in bind-only mode.
 *                 Call SYS_Bind to connect VI (dev 0, chn 0) to VENC (chn 0).
 *                 VI's push thread automatically delivers each captured frame
 *                 to VENC; the application only pulls the encoded stream.
 *
 *                 Usage: ./sample_vi_k3_venc_bind [frame_count]
 *                   default frame_count = 300  (~10 s @ 30 fps)
 *
 *                 Output: ./output.h264
 *                   Play:  ffplay output.h264
 *------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "sys/sys_api.h"
#include "sys/vb_api.h"
#include "vi/vi_api.h"
#include "vi/vi_type.h"
#include "venc/venc_api.h"
#include "venc/venc_type.h"

#define VI_DEV_ID      0
#define VI_CHN_ID      0
#define VENC_CHN_ID    0

#define DEFAULT_WIDTH  1280
#define DEFAULT_HEIGHT 720
#define DEFAULT_FRAMES 300
#define OUTPUT_PATH    "./output.h264"

static void usage(const char *prog) {
    printf("Usage: %s [frame_count]\n", prog);
    printf("  frame_count  number of encoded frames to save (default %d)\n", DEFAULT_FRAMES);
}

int main(int argc, char **argv) {
    int frame_count = DEFAULT_FRAMES;
    S32 ret = 0;

    if (argc > 1) {
        if (argv[1][0] == '-') { usage(argv[0]); return 0; }
        frame_count = atoi(argv[1]);
        if (frame_count <= 0) { usage(argv[0]); return 1; }
    }

    /* ------------------------------------------------------------------ */
    /* 1. System init                                                       */
    /* ------------------------------------------------------------------ */
    ret = SYS_Init();
    if (ret != 0) { fprintf(stderr, "SYS_Init failed: %d\n", ret); return 1; }

    ret = VB_Init();
    if (ret != 0) { fprintf(stderr, "VB_Init failed: %d\n", ret); goto err_sys; }

    /* ------------------------------------------------------------------ */
    /* 2. VI init and enable                                                */
    /* ------------------------------------------------------------------ */
    ret = VI_Init();
    if (ret != 0) { fprintf(stderr, "VI_Init failed: %d\n", ret); goto err_vb; }

    ViDevAttrS devAttr;
    memset(&devAttr, 0, sizeof(devAttr));
    devAttr.eWorkMode    = VI_WORK_MODE_ONLINE;
    devAttr.u32Width     = DEFAULT_WIDTH;
    devAttr.u32Height    = DEFAULT_HEIGHT;
    devAttr.u32MipiLaneNum = 4;
    devAttr.u32mbps      = 800;

    ret = VI_SetDevAttr(VI_DEV_ID, &devAttr);
    if (ret != 0) { fprintf(stderr, "VI_SetDevAttr failed: %d\n", ret); goto err_vi; }

    ret = VI_EnableDev(VI_DEV_ID);
    if (ret != 0) { fprintf(stderr, "VI_EnableDev failed: %d\n", ret); goto err_vi; }

    ViChnAttrS chnAttr;
    memset(&chnAttr, 0, sizeof(chnAttr));
    chnAttr.eChnType     = VI_CHN_TYPE_PHYSICAL;
    chnAttr.ePixelFormat = MPP_PIXEL_FORMAT_UYVY;
    chnAttr.u32Width     = DEFAULT_WIDTH;
    chnAttr.u32Height    = DEFAULT_HEIGHT;
    chnAttr.eStrideAlign = VI_STRIDE_ALIGN_DEFAULT;
    chnAttr.u32Depth     = 0;  /* bind-only: frames auto-pushed via SYS_SendFrame */

    ret = VI_SetChnAttr(VI_DEV_ID, VI_CHN_ID, &chnAttr);
    if (ret != 0) { fprintf(stderr, "VI_SetChnAttr failed: %d\n", ret); goto err_vi_dev; }

    ret = VI_EnableChn(VI_DEV_ID, VI_CHN_ID);
    if (ret != 0) { fprintf(stderr, "VI_EnableChn failed: %d\n", ret); goto err_vi_dev; }

    printf("VI enabled: %dx%d UYVY (bind-only mode)\n", DEFAULT_WIDTH, DEFAULT_HEIGHT);

    /* ------------------------------------------------------------------ */
    /* 3. VENC init and create channel                                      */
    /* ------------------------------------------------------------------ */
    ret = VENC_Init();
    if (ret != 0) { fprintf(stderr, "VENC_Init failed: %d\n", ret); goto err_vi_chn; }

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

    ret = VENC_CreateChn(VENC_CHN_ID, &vencAttr);
    if (ret != 0) { fprintf(stderr, "VENC_CreateChn failed: %d\n", ret); goto err_venc; }

    ret = VENC_EnableChn(VENC_CHN_ID);
    if (ret != 0) { fprintf(stderr, "VENC_EnableChn failed: %d\n", ret); goto err_venc_chn; }

    printf("VENC enabled: H.264 %dx%d 2Mbps\n", DEFAULT_WIDTH, DEFAULT_HEIGHT);

    /* ------------------------------------------------------------------ */
    /* 4. Establish SYS bind: VI (dev0,chn0) → VENC (chn0)                 */
    /*    From this point VI's push thread automatically delivers frames.   */
    /* ------------------------------------------------------------------ */
    MppNode viNode   = { MPP_ID_VI,   VI_DEV_ID, VI_CHN_ID  };
    MppNode vencNode = { MPP_ID_VENC, 0,         VENC_CHN_ID };

    ret = SYS_Bind(&viNode, &vencNode);
    if (ret != 0) { fprintf(stderr, "SYS_Bind failed: %d\n", ret); goto err_venc_enable; }

    printf("SYS bind established: VI(dev=%d,chn=%d) → VENC(chn=%d)\n",
            VI_DEV_ID, VI_CHN_ID, VENC_CHN_ID);

    /* ------------------------------------------------------------------ */
    /* 5. Pull encoded stream from VENC and write to file                   */
    /* ------------------------------------------------------------------ */
    FILE *fp = fopen(OUTPUT_PATH, "wb");
    if (fp == NULL) {
        fprintf(stderr, "fopen(%s) failed: %s\n", OUTPUT_PATH, strerror(errno));
        goto err_bind;
    }

    printf("Saving %d frames to %s ...\n", frame_count, OUTPUT_PATH);

    int saved = 0;
    while (saved < frame_count) {
        StreamBufferInfo stream;
        memset(&stream, 0, sizeof(stream));

        ret = VENC_GetStream(VENC_CHN_ID, &stream, 3000);
        if (ret != 0) {
            fprintf(stderr, "VENC_GetStream timeout/error on frame %d: %d\n", saved, ret);
            continue;
        }

        if (fwrite(stream.pu8Addr, 1, stream.u32Size, fp) != stream.u32Size)
            fprintf(stderr, "short write on frame %d\n", saved);

        VENC_ReleaseStream(VENC_CHN_ID, &stream);
        saved++;

        if (saved % 30 == 0)
            printf("  saved %d / %d frames\n", saved, frame_count);
    }

    fclose(fp);
    printf("Done. %d frames saved to %s\n", saved, OUTPUT_PATH);
    printf("Play with: ffplay %s\n", OUTPUT_PATH);

    /* ------------------------------------------------------------------ */
    /* 6. Cleanup (reverse order)                                           */
    /* ------------------------------------------------------------------ */
err_bind:
    SYS_UnBind(&viNode, &vencNode);
err_venc_enable:
    VENC_DisableChn(VENC_CHN_ID);
err_venc_chn:
    VENC_DestroyChn(VENC_CHN_ID);
err_venc:
    VENC_Exit();
err_vi_chn:
    VI_DisableChn(VI_DEV_ID, VI_CHN_ID);
err_vi_dev:
    VI_DisableDev(VI_DEV_ID);
err_vi:
    VI_DeInit();
err_vb:
    VB_Exit();
err_sys:
    SYS_Exit();

    return (ret == 0 && saved == frame_count) ? 0 : 1;
}
