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

#define K3_CAM_DEV 0
#define K3_CAM_CHN 0
#define K3_CAM_DEFAULT_WIDTH 1920
#define K3_CAM_DEFAULT_HEIGHT 1080
#define K3_CAM_DEFAULT_LANES 4
#define K3_CAM_DEFAULT_MBPS 800
#define K3_CAM_TIMEOUT_MS 33
#define K3_CAM_FRAME_COUNT 100
#define K3_CAM_SAVE_PATH "./frames.raw"

static void dump_frame(const VideoFrameInfo *frame, U32 idx) {
    if (frame == NULL)
        return;

    printf(
        "frame[%u]: pool=%lu buf=%lu width=%u planes=%u ts=%" PRIu64 "\n",
        idx,
        frame->ulPoolId,
        frame->ulBufferId,
        frame->stCommFrameInfo.u32Width,
        frame->stVFrame.u32PlaneNum,
        (uint64_t)frame->stVFrame.u64PTS);
}

static int save_frame(FILE *fp, const VideoFrameInfo *frame, U32 idx) {
    U32 i;
    U32 planes;
    U32 total_written = 0;

    if (fp == NULL || frame == NULL)
        return -1;

    planes = frame->stVFrame.u32PlaneNum;
    if (planes > FRAME_MAX_PLANE)
        planes = FRAME_MAX_PLANE;

    for (i = 0; i < planes; ++i) {
        const void *addr = (const void *)frame->stVFrame.ulPlaneVirAddr[i];
        U32 size_valid = frame->stVFrame.u32PlaneSizeValid[i];
        U32 size_total = frame->stVFrame.u32PlaneSize[i];
        U32 width = frame->stCommFrameInfo.u32Width;
        U32 height = frame->stCommFrameInfo.u32Height;
        U32 size;

        /* Use the valid size (bytesused from driver) if available and reasonable.
         * For 10-bit Bayer format, the actual packed size should be:
         * width * height * 10 / 8 = 2,592,000 bytes for 1920x1080 */
        if (size_valid > 0 && size_valid <= size_total) {
            size = size_valid;
        } else {
            /* Fallback: calculate correct size for 10-bit Bayer packed format */
            size = (width * height * 10 + 7) / 8;
        }

        printf("  plane[%u]: addr=%p size_valid=%u size_total=%u using=%u\n", i, addr, size_valid, size_total, size);

        if (addr == NULL || size == 0)
            continue;

        if (fwrite(addr, 1, size, fp) != size) {
            printf("save_frame[%u]: short write at plane %u: %s\n", idx, i, strerror(errno));
            return -1;
        }
        total_written += size;
    }

    printf("saved frame[%u] (%u bytes)\n", idx, total_written);
    return 0;
}

int main(int argc, char **argv) {
    const char *dev = "/dev/video8";
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
    devAttr.eWorkMode = VI_WORK_MODE_ONLINE;
    devAttr.u32Width = K3_CAM_DEFAULT_WIDTH;
    devAttr.u32Height = K3_CAM_DEFAULT_HEIGHT;
    devAttr.u32MipiLaneNum = K3_CAM_DEFAULT_LANES;
    devAttr.u32mbps = K3_CAM_DEFAULT_MBPS;

    ret = VI_SetDevAttr(K3_CAM_DEV, &devAttr);
    if (ret != 0) {
        printf("VI_SetDevAttr failed: %d\n", ret);
        goto out;
    }

    memset(&chnAttr, 0, sizeof(chnAttr));
    chnAttr.eChnType = VI_CHN_TYPE_PHYSICAL;
    /* Use MIPI CSI-2 RAW10 packed (5 bytes per 4 pixels) so the VB pool
     * size matches V4L2 sizeimage exactly: width*5/4*height. */
    chnAttr.ePixelFormat = MPP_PIXEL_FORMAT_RGB_BAYER_10BITS_PACKED;
    chnAttr.u32Width = K3_CAM_DEFAULT_WIDTH;
    chnAttr.u32Height = K3_CAM_DEFAULT_HEIGHT;
    chnAttr.eStrideAlign = VI_STRIDE_ALIGN_DEFAULT;

    ret = VI_SetChnAttr(K3_CAM_DEV, K3_CAM_CHN, &chnAttr);
    if (ret != 0) {
        printf("VI_SetChnAttr failed: %d\n", ret);
        goto out;
    }

    ret = VI_EnableDev(K3_CAM_DEV);
    if (ret != 0) {
        printf("VI_EnableDev failed: %d\n", ret);
        goto out;
    }

    ret = VI_EnableChn(K3_CAM_DEV, K3_CAM_CHN);
    if (ret != 0) {
        printf("VI_EnableChn failed: %d\n", ret);
        goto out;
    }

    printf("started capture path for %s\n", dev);
    usleep(30000);  // Allow some time for the camera to start streaming

    FILE *save_fp = fopen(K3_CAM_SAVE_PATH, "wb");
    if (save_fp == NULL) {
        printf("fopen(%s) failed: %s\n", K3_CAM_SAVE_PATH, strerror(errno));
    }

    for (i = 0; i < K3_CAM_FRAME_COUNT; ++i) {
        memset(&frame, 0, sizeof(frame));
        ret = VI_GetChnFrame(K3_CAM_DEV, K3_CAM_CHN, &frame, K3_CAM_TIMEOUT_MS);
        if (ret != 0) {
            printf("VI_GetChnFrame failed on frame %u: %d\n", i, ret);
            break;
        }
        dump_frame(&frame, i);
        // if (save_fp != NULL)
        //     (void)save_frame(save_fp, &frame, i);
        ret = VI_ReleaseChnFrame(K3_CAM_DEV, K3_CAM_CHN, &frame);
        if (ret != 0) {
            printf("VI_ReleaseChnFrame failed on frame %u: %d\n", i, ret);
            break;
        }
    }

    if (save_fp != NULL) {
        fclose(save_fp);
        printf("all frames saved to %s\n", K3_CAM_SAVE_PATH);
    }

out:
    (void)VI_DisableChn(K3_CAM_DEV, K3_CAM_CHN);
    (void)VI_DisableDev(K3_CAM_DEV);
    (void)VI_DeInit();
    (void)VB_Exit();
    (void)SYS_Exit();
    return (ret == 0) ? 0 : 1;
}
