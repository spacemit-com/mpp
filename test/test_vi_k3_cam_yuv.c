/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "sys_api.h"
#include "vb_api.h"
#include "vi_api.h"

#define K3_YUV_DEV             0
#define K3_YUV_CHN             0
#define K3_YUV_DEFAULT_WIDTH   1280
#define K3_YUV_DEFAULT_HEIGHT  720
#define K3_YUV_DEFAULT_LANES   4
#define K3_YUV_DEFAULT_MBPS    800
#define K3_YUV_TIMEOUT_MS      33
#define K3_YUV_FRAME_COUNT     100
#define K3_YUV_SAVE_PATH       "./frames.yuv"

static void dump_frame(const VideoFrameInfo *frame, U32 idx) {
    if (frame == NULL)
        return;

    printf(
        "frame[%u]: pool=%lu buf=%lu width=%u height=%u planes=%u ts=%" PRIu64 "\n",
        idx,
        frame->ulPoolId,
        frame->ulBufferId,
        frame->stCommFrameInfo.u32Width,
        frame->stCommFrameInfo.u32Height,
        frame->stVFrame.u32PlaneNum,
        (uint64_t)frame->stVFrame.u64PTS);
}

static int save_frame(FILE *fp, const VideoFrameInfo *frame, U32 idx) {
    const void *addr;
    U32 size_valid;
    U32 size_total;
    U32 size;
    U32 width  = frame->stCommFrameInfo.u32Width;
    U32 height = frame->stCommFrameInfo.u32Height;

    if (fp == NULL || frame == NULL)
        return -1;

    /* UYVY is single-plane: 2 bytes per pixel */
    addr       = (const void *)frame->stVFrame.ulPlaneVirAddr[0];
    size_valid = frame->stVFrame.u32PlaneSizeValid[0];
    size_total = frame->stVFrame.u32PlaneSize[0];

    if (size_valid > 0 && size_valid <= size_total) {
        size = size_valid;
    } else {
        /* UYVY: width * height * 2 bytes */
        size = width * height * 2U;
    }

    printf("  plane[0]: addr=%p size_valid=%u size_total=%u using=%u\n",
            addr, size_valid, size_total, size);

    if (addr == NULL || size == 0)
        return -1;

    if (fwrite(addr, 1, size, fp) != size) {
        printf("save_frame[%u]: short write: %s\n", idx, strerror(errno));
        return -1;
    }

    printf("saved frame[%u] (%u bytes)\n", idx, size);
    return 0;
}

int main(int argc, char **argv) {
    const char *dev = "/dev/video3";
    S32 ret;
    U32 i;
    ViDevAttrS devAttr;
    ViChnAttrS chnAttr;
    VideoFrameInfo frame;

    (void)argc;
    (void)argv;

    ret = SYS_Init();
    if (ret != 0) {
        printf("SYS_Init failed: %d\n", ret);
        return 1;
    }

    ret = VB_Init();
    if (ret != 0) {
        printf("VB_Init failed: %d\n", ret);
        (void)SYS_Exit();
        return 1;
    }

    ret = VI_Init();
    if (ret != 0) {
        printf("VI_Init failed: %d\n", ret);
        (void)VB_Exit();
        (void)SYS_Exit();
        return 1;
    }

    memset(&devAttr, 0, sizeof(devAttr));
    devAttr.eWorkMode    = VI_WORK_MODE_ONLINE;
    devAttr.u32Width     = K3_YUV_DEFAULT_WIDTH;
    devAttr.u32Height    = K3_YUV_DEFAULT_HEIGHT;
    devAttr.u32MipiLaneNum = K3_YUV_DEFAULT_LANES;
    devAttr.u32mbps      = K3_YUV_DEFAULT_MBPS;

    ret = VI_SetDevAttr(K3_YUV_DEV, &devAttr);
    if (ret != 0) {
        printf("VI_SetDevAttr failed: %d\n", ret);
        goto out;
    }

    memset(&chnAttr, 0, sizeof(chnAttr));
    chnAttr.eChnType     = VI_CHN_TYPE_PHYSICAL;
    chnAttr.ePixelFormat = MPP_PIXEL_FORMAT_UYVY;
    chnAttr.u32Width     = K3_YUV_DEFAULT_WIDTH;
    chnAttr.u32Height    = K3_YUV_DEFAULT_HEIGHT;
    chnAttr.eStrideAlign = VI_STRIDE_ALIGN_DEFAULT;
    chnAttr.u32Depth     = 1;
    ret = VI_SetChnAttr(K3_YUV_DEV, K3_YUV_CHN, &chnAttr);
    if (ret != 0) {
        printf("VI_SetChnAttr failed: %d\n", ret);
        goto out;
    }

    ret = VI_EnableDev(K3_YUV_DEV);
    if (ret != 0) {
        printf("VI_EnableDev failed: %d\n", ret);
        goto out;
    }

    ret = VI_EnableChn(K3_YUV_DEV, K3_YUV_CHN);
    if (ret != 0) {
        printf("VI_EnableChn failed: %d\n", ret);
        goto out;
    }

    printf("started UYVY capture from %s (%ux%u)\n",
            dev, K3_YUV_DEFAULT_WIDTH, K3_YUV_DEFAULT_HEIGHT);
    usleep(30000);

    FILE *save_fp = fopen(K3_YUV_SAVE_PATH, "wb");
    if (save_fp == NULL) {
        printf("fopen(%s) failed: %s\n", K3_YUV_SAVE_PATH, strerror(errno));
        ret = 1;
        goto out;
    }

    for (i = 0; i < K3_YUV_FRAME_COUNT; ++i) {
        memset(&frame, 0, sizeof(frame));
        ret = VI_GetChnFrame(K3_YUV_DEV, K3_YUV_CHN, &frame, K3_YUV_TIMEOUT_MS);
        if (ret != 0) {
            printf("VI_GetChnFrame failed on frame %u: %d\n", i, ret);
            break;
        }
        dump_frame(&frame, i);
        if (save_fp != NULL)
            (void)save_frame(save_fp, &frame, i);
        ret = VI_ReleaseChnFrame(K3_YUV_DEV, K3_YUV_CHN, &frame);
        if (ret != 0) {
            printf("VI_ReleaseChnFrame failed on frame %u: %d\n", i, ret);
            break;
        }
    }

    if (save_fp != NULL) {
        fclose(save_fp);
        printf("all frames saved to %s\n", K3_YUV_SAVE_PATH);
        printf("play with: ffplay -f rawvideo -pixel_format uyvy422 -video_size %dx%d %s\n",
                K3_YUV_DEFAULT_WIDTH, K3_YUV_DEFAULT_HEIGHT, K3_YUV_SAVE_PATH);
    }

out:
    (void)VI_DisableChn(K3_YUV_DEV, K3_YUV_CHN);
    (void)VI_DisableDev(K3_YUV_DEV);
    (void)VI_DeInit();
    (void)VB_Exit();
    (void)SYS_Exit();
    return (ret == 0) ? 0 : 1;
}
