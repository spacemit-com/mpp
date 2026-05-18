/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_v2d_boarder_fill.c
 * @Brief     :    V2D_CopyMakeBorder demo: caller prepares an already-scaled
 *                 input image, then V2D fills the canvas and blits the image.
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys_api.h"
#include "vb_api.h"
#include "v2d_api.h"

#define DEMO_INPUT_FILE   "./input.yuv"
#define DEMO_OUTPUT_FILE  "./test_v2d_boarder_fill.yuv"
#define SCALED_WIDTH      640U
#define SCALED_HEIGHT     360U
#define DST_WIDTH         640U
#define DST_HEIGHT        640U

#define DEMO_LOG(fmt, ...)  printf("[test_v2d_boarder_fill] " fmt "\n", ##__VA_ARGS__)
#define DEMO_FAIL(fmt, ...) do { printf("[test_v2d_boarder_fill][FAIL] " fmt "\n", ##__VA_ARGS__); return -1; } while (0)

typedef struct DEMO_CONFIG_S {
    const char *scaled_file;
    const char *output_file;
    U32 scaled_width;
    U32 scaled_height;
    U32 dst_width;
    U32 dst_height;
    U32 border_y;
    U32 border_u;
    U32 border_v;
} DEMO_CONFIG_S;

static void demo_set_default_config(DEMO_CONFIG_S *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->scaled_file = DEMO_INPUT_FILE;
    config->output_file = DEMO_OUTPUT_FILE;
    config->scaled_width = SCALED_WIDTH;
    config->scaled_height = SCALED_HEIGHT;
    config->dst_width = DST_WIDTH;
    config->dst_height = DST_HEIGHT;
    config->border_y = 0U;
    config->border_u = 128U;
    config->border_v = 128U;
}

static int demo_parse_u32(const char *text, U32 *value)
{
    char *end_ptr = NULL;
    unsigned long parsed;

    if ((text == NULL) || (value == NULL) || (text[0] == '\0')) {
        return -1;
    }

    parsed = strtoul(text, &end_ptr, 10);
    if (*end_ptr != '\0') {
        return -1;
    }

    *value = (U32)parsed;
    return 0;
}

static void demo_print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s [scaled_nv12 scaled_width scaled_height output_nv12 dst_width dst_height [border_y border_u border_v]]\n", prog);
    printf("\nDefaults:\n");
    printf("  scaled_file : %s\n", DEMO_INPUT_FILE);
    printf("  scaled_width: %u\n", SCALED_WIDTH);
    printf("  scaled_height: %u\n", SCALED_HEIGHT);
    printf("  output_file : %s\n", DEMO_OUTPUT_FILE);
    printf("  dst_width   : %u\n", DST_WIDTH);
    printf("  dst_height  : %u\n", DST_HEIGHT);
    printf("  border_yuv  : %u %u %u\n", 0U, 128U, 128U);
    printf("\nBehavior:\n");
    printf("  This demo verifies border fill on a pre-scaled input image.\n");
    printf("  V2D only fills the destination canvas and copies the scaled image into the inner area.\n");
}

static int demo_parse_args(int argc, char *argv[], DEMO_CONFIG_S *config)
{
    if (config == NULL) {
        return -1;
    }

    demo_set_default_config(config);

    if (argc == 1) {
        return 0;
    }

    if ((argc != 7) && (argc != 10)) {
        return -1;
    }

    config->scaled_file = argv[1];
    config->output_file = argv[4];
    if ((demo_parse_u32(argv[2], &config->scaled_width) != 0) ||
        (demo_parse_u32(argv[3], &config->scaled_height) != 0) ||
        (demo_parse_u32(argv[5], &config->dst_width) != 0) ||
        (demo_parse_u32(argv[6], &config->dst_height) != 0)) {
        return -1;
    }

    if (argc == 10) {
        if ((demo_parse_u32(argv[7], &config->border_y) != 0) ||
            (demo_parse_u32(argv[8], &config->border_u) != 0) ||
            (demo_parse_u32(argv[9], &config->border_v) != 0)) {
            return -1;
        }

        if ((config->border_y > 255U) || (config->border_u > 255U) || (config->border_v > 255U)) {
            return -1;
        }
    }

    return 0;
}

static int demo_load_nv12_file(VideoFrameInfo *frame, const char *input_file)
{
    FILE *fp;
    size_t read_size;
    size_t expected_size;
    U8 *plane0;
    U8 *plane1;

    if ((frame == NULL) || (input_file == NULL)) {
        return -1;
    }

    plane0 = (U8 *)frame->stVFrame.ulPlaneVirAddr[0];
    plane1 = (U8 *)frame->stVFrame.ulPlaneVirAddr[1];
    if (plane0 == NULL) {
        return -1;
    }

    expected_size = (size_t)frame->stVFrame.u32PlaneSize[0] +
                    (size_t)frame->stVFrame.u32PlaneSize[1];

    fp = fopen(input_file, "rb");
    if (fp == NULL) {
        DEMO_LOG("Failed to open input file: %s", input_file);
        perror("fopen");
        return -1;
    }

    read_size = fread(plane0, 1, expected_size, fp);
    fclose(fp);

    if (read_size != expected_size) {
        DEMO_LOG("File size mismatch: read=%zu expected=%zu", read_size, expected_size);
        return -1;
    }

    if ((plane1 != NULL) && (plane1 != (plane0 + frame->stVFrame.u32PlaneSize[0]))) {
        memcpy(plane1,
               plane0 + frame->stVFrame.u32PlaneSize[0],
               frame->stVFrame.u32PlaneSize[1]);
    }

    return 0;
}

static int demo_dump_nv12_file(const VideoFrameInfo *frame, const char *output_file)
{
    FILE *fp;
    size_t write_size;
    size_t expected_size;
    const void *plane0;

    if ((frame == NULL) || (output_file == NULL)) {
        return -1;
    }

    plane0 = (const void *)frame->stVFrame.ulPlaneVirAddr[0];
    if (plane0 == NULL) {
        return -1;
    }

    expected_size = (size_t)frame->stVFrame.u32PlaneSize[0] +
                    (size_t)frame->stVFrame.u32PlaneSize[1];

    fp = fopen(output_file, "wb");
    if (fp == NULL) {
        DEMO_LOG("open output file failed: %s", output_file);
        return -1;
    }

    write_size = fwrite(plane0, 1, expected_size, fp);
    fclose(fp);

    if (write_size != expected_size) {
        DEMO_LOG("write output file size mismatch, got=%zu expect=%zu", write_size, expected_size);
        return -1;
    }

    return 0;
}

static void demo_reset_nv12_frame(VideoFrameInfo *frame)
{
    void *plane0;
    void *plane1;

    if (frame == NULL) {
        return;
    }

    plane0 = (void *)frame->stVFrame.ulPlaneVirAddr[0];
    plane1 = (void *)frame->stVFrame.ulPlaneVirAddr[1];

    if (plane0 != NULL) {
        memset(plane0, 0, frame->stVFrame.u32PlaneSize[0]);
    }

    if (plane1 != NULL) {
        memset(plane1, 0x80, frame->stVFrame.u32PlaneSize[1]);
    }
}

/*
 * Allocate an NV12 VB pool, grab one buffer from it, and fill out the
 * VideoFrameInfo so V2D can consume it directly (UVC-style flow).
 */
static int demo_prepare_pool(UL *pool_id, VideoFrameInfo *frame, ModId mod_id, U32 width, U32 height)
{
    VbPoolCfg cfg;
    UL buffer = 0UL;
    U32 stride;
    U32 y_size;
    U32 total_size;
    void *vir_addr = NULL;
    S32 dma_buf_fd = -1;
    S32 ret;

    if ((pool_id == NULL) || (frame == NULL)) {
        return -1;
    }

    stride = width;
    y_size = stride * height;
    total_size = y_size + (y_size / 2U);

    memset(&cfg, 0, sizeof(cfg));
    cfg.u32BufSize = total_size;
    cfg.u32BufCnt = 1U;
    cfg.eModId = mod_id;
    cfg.eRemapMode = VBUF_REMAP_MODE_NOCACHE;

    *pool_id = VB_CreatePool(&cfg);
    if (*pool_id == 0UL) {
        DEMO_LOG("VB_CreatePool failed, width=%u height=%u", width, height);
        return -1;
    }

    /* Acquire one buffer from the pool and resolve its fd / virtual address. */
    buffer = VB_GetBuffer(*pool_id, 0);
    if (buffer == 0UL) {
        DEMO_LOG("VB_GetBuffer failed");
        goto err_destroy_pool;
    }

    ret = VB_GetDmaBufFd(buffer, &dma_buf_fd);
    if ((ret != 0) || (dma_buf_fd < 0)) {
        DEMO_LOG("VB_GetDmaBufFd failed, ret=%d fd=%d", ret, dma_buf_fd);
        goto err_release_buf;
    }

    ret = VB_GetVirAddr(buffer, &vir_addr);
    if ((ret != 0) || (vir_addr == NULL)) {
        DEMO_LOG("VB_GetVirAddr failed, ret=%d", ret);
        goto err_release_buf;
    }

    /* Fill VideoFrameInfo in one shot (UVC-style). */
    memset(frame, 0, sizeof(*frame));
    frame->eFrameType = FRAME_TYPE_COMMON;
    frame->eModId = mod_id;
    frame->ulPoolId = *pool_id;
    frame->ulBufferId = buffer;

    frame->stCommFrameInfo.u32Width = width;
    frame->stCommFrameInfo.u32Height = height;
    frame->stCommFrameInfo.ePixelFormat = MPP_PIXEL_FORMAT_NV12;

    frame->stVFrame.u32PlaneNum = 2U;
    frame->stVFrame.u32PlaneStride[0] = stride;
    frame->stVFrame.u32PlaneStride[1] = stride;
    frame->stVFrame.u32PlaneSize[0] = y_size;
    frame->stVFrame.u32PlaneSize[1] = y_size / 2U;
    frame->stVFrame.u32PlaneSizeValid[0] = y_size;
    frame->stVFrame.u32PlaneSizeValid[1] = y_size / 2U;
    frame->stVFrame.u32TotalSize = total_size;
    frame->stVFrame.u32Fd[0] = (UL)dma_buf_fd;
    frame->stVFrame.u32Fd[1] = (UL)dma_buf_fd;
    frame->stVFrame.ulPlaneVirAddr[0] = (UL)vir_addr;
    frame->stVFrame.ulPlaneVirAddr[1] = (UL)vir_addr + y_size;

    return 0;

err_release_buf:
    VB_ReleaseBuffer(buffer);
err_destroy_pool:
    VB_DestroyPool(*pool_id);
    *pool_id = 0UL;
    return -1;
}

static int demo_run_border_fill(const DEMO_CONFIG_S *config,
                                const VideoFrameInfo *scaled_frame,
                                VideoFrameInfo *dst_frame)
{
    V2DHandle handle = 0;
    V2DFillColor border_color;
    U32 left;
    U32 right;
    U32 top;
    U32 bottom;
    S32 ret;

    if ((config == NULL) || (scaled_frame == NULL) || (dst_frame == NULL)) {
        return -1;
    }

    if ((config->scaled_width > config->dst_width) || (config->scaled_height > config->dst_height)) {
        DEMO_LOG("scaled image is larger than destination canvas");
        return -1;
    }

    left = (config->dst_width - config->scaled_width) / 2U;
    right = config->dst_width - config->scaled_width - left;
    top = (config->dst_height - config->scaled_height) / 2U;
    bottom = config->dst_height - config->scaled_height - top;

    if (((left | right | top | bottom) & 1U) != 0U) {
        DEMO_LOG("border size must be even for NV12/NV21, got top=%u bottom=%u left=%u right=%u",
                 top, bottom, left, right);
        return -1;
    }

    demo_reset_nv12_frame(dst_frame);

    border_color.enFormat = V2D_COLOR_FORMAT_NV12;
    border_color.u32ColorValue = (config->border_v << 16) |
                                 (config->border_u << 8) |
                                 config->border_y;

    ret = V2D_BeginJob(&handle);
    if (ret != 0) {
        return ret;
    }

    ret = V2D_BorderFill(handle,
                         scaled_frame,
                         dst_frame,
                         top,
                         bottom,
                         left,
                         right,
                         &border_color);
    if (ret != 0) {
        V2D_CancelJob(handle);
        return ret;
    }

    ret = V2D_EndJob(handle);
    if (ret != 0) {
        return ret;
    }

    DEMO_LOG("border(top=%u bottom=%u left=%u right=%u)", top, bottom, left, right);
    return 0;
}

int main(int argc, char *argv[])
{
    S32 ret;
    DEMO_CONFIG_S config;
    UL scaled_pool = 0UL;
    UL dst_pool = 0UL;
    VideoFrameInfo scaled_frame;
    VideoFrameInfo dst_frame;

    memset(&scaled_frame, 0, sizeof(scaled_frame));
    memset(&dst_frame, 0, sizeof(dst_frame));

    if (demo_parse_args(argc, argv, &config) != 0) {
        demo_print_usage(argv[0]);
        return -1;
    }

    DEMO_LOG("Scaled input: %s", config.scaled_file);
    DEMO_LOG("Output: %s", config.output_file);
    DEMO_LOG("Scaled source: %ux%u NV12", config.scaled_width, config.scaled_height);
    DEMO_LOG("Destination canvas: %ux%u NV12", config.dst_width, config.dst_height);
    DEMO_LOG("Border YUV: %u %u %u", config.border_y, config.border_u, config.border_v);

    ret = SYS_Init();
    if (ret != 0) {
        DEMO_FAIL("SYS_Init failed, ret=%d", ret);
    }

    ret = VB_Init();
    if (ret != 0) {
        SYS_Exit();
        DEMO_FAIL("VB_Init failed, ret=%d", ret);
    }

    if (demo_prepare_pool(&scaled_pool, &scaled_frame, MPP_ID_V2D, config.scaled_width, config.scaled_height) != 0) {
        goto EXIT;
    }

    if (demo_prepare_pool(&dst_pool, &dst_frame, MPP_ID_V2D, config.dst_width, config.dst_height) != 0) {
        goto EXIT;
    }

    ret = demo_load_nv12_file(&scaled_frame, config.scaled_file);
    if (ret != 0) {
        DEMO_LOG("load scaled NV12 file failed: %s", config.scaled_file);
        goto EXIT;
    }

    ret = demo_run_border_fill(&config, &scaled_frame, &dst_frame);
    if (ret != 0) {
        DEMO_LOG("demo_run_border_fill failed, ret=%d", ret);
        goto EXIT;
    }

    ret = demo_dump_nv12_file(&dst_frame, config.output_file);
    if (ret != 0) {
        DEMO_LOG("dump output file failed: %s", config.output_file);
        goto EXIT;
    }

    DEMO_LOG("V2D_CopyMakeBorder border fill finished");

EXIT:
    if (scaled_frame.ulBufferId != 0UL) {
        VB_ReleaseBuffer(scaled_frame.ulBufferId);
    }
    if (dst_frame.ulBufferId != 0UL) {
        VB_ReleaseBuffer(dst_frame.ulBufferId);
    }
    if (scaled_pool != 0UL) {
        VB_DestroyPool(scaled_pool);
    }
    if (dst_pool != 0UL) {
        VB_DestroyPool(dst_pool);
    }
    VB_Exit();
    SYS_Exit();
    return 0;
}