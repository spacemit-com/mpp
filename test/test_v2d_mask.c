/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_v2d_mask.c
 * @Brief     :    Example: blend NV12 background with BGRA foreground by A8
 *                 mask via V2D_DrawMask, then dump NV12 output.
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

#define DEMO_BG_FILE      "./mask_bg_nv12.yuv"
#define DEMO_FG_FILE      "./mask_fg_bgra.raw"
#define DEMO_MASK_FILE    "./mask_a8.raw"
#define DEMO_OUTPUT_FILE  "./test_v2d_mask_out.yuv"
#define DEMO_WIDTH        1920U
#define DEMO_HEIGHT       1080U

#define DEMO_LOG(fmt, ...)  printf("[test_v2d_mask] " fmt "\n", ##__VA_ARGS__)
#define DEMO_FAIL(fmt, ...) do { printf("[test_v2d_mask][FAIL] " fmt "\n", ##__VA_ARGS__); return -1; } while (0)

typedef struct DEMO_CONFIG_S {
    const char *bg_file;
    const char *fg_file;
    const char *mask_file;
    const char *output_file;
    U32 width;
    U32 height;
} DEMO_CONFIG_S;

static void demo_set_default_config(DEMO_CONFIG_S *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->bg_file = DEMO_BG_FILE;
    config->fg_file = DEMO_FG_FILE;
    config->mask_file = DEMO_MASK_FILE;
    config->output_file = DEMO_OUTPUT_FILE;
    config->width = DEMO_WIDTH;
    config->height = DEMO_HEIGHT;
}

static int demo_parse_u32(const char *text, U32 *value)
{
    char *end_ptr = NULL;
    unsigned long parsed;

    if ((text == NULL) || (value == NULL) || (text[0] == '\0')) {
        return -1;
    }

    parsed = strtoul(text, &end_ptr, 10);
    if ((*end_ptr != '\0') || (parsed == 0UL)) {
        return -1;
    }

    *value = (U32)parsed;
    return 0;
}

static void demo_print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s [bg_nv12 fg_bgra mask_a8 output_nv12 width height]\n", prog);
    printf("\nDefaults:\n");
    printf("  bg_file    : %s\n", DEMO_BG_FILE);
    printf("  fg_file    : %s\n", DEMO_FG_FILE);
    printf("  mask_file  : %s\n", DEMO_MASK_FILE);
    printf("  output_file: %s\n", DEMO_OUTPUT_FILE);
    printf("  width      : %u\n", DEMO_WIDTH);
    printf("  height     : %u\n", DEMO_HEIGHT);
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

    if (argc != 7) {
        return -1;
    }

    config->bg_file = argv[1];
    config->fg_file = argv[2];
    config->mask_file = argv[3];
    config->output_file = argv[4];
    if ((demo_parse_u32(argv[5], &config->width) != 0) ||
        (demo_parse_u32(argv[6], &config->height) != 0)) {
        return -1;
    }

    return 0;
}

static int demo_prepare_pool(UL *pool_id,
                             ModId mod_id,
                             MppPixelFormat pixel_format,
                             U32 width,
                             U32 height)
{
    VbPoolCfg cfg;
    VideoFrameInfo frame_info;
    U32 stride;
    U32 y_size;
    U32 total_size;
    S32 ret;

    if (pool_id == NULL) {
        return -1;
    }

    if (pixel_format == MPP_PIXEL_FORMAT_NV12) {
        stride = width;
        y_size = stride * height;
        total_size = y_size + (y_size / 2U);
        frame_info.stVFrame.u32PlaneNum = 2U;
        frame_info.stVFrame.u32PlaneStride[0] = stride;
        frame_info.stVFrame.u32PlaneStride[1] = stride;
        frame_info.stVFrame.u32PlaneSize[0] = y_size;
        frame_info.stVFrame.u32PlaneSize[1] = y_size / 2U;
        frame_info.stVFrame.u32PlaneSizeValid[0] = y_size;
        frame_info.stVFrame.u32PlaneSizeValid[1] = y_size / 2U;
    } else if (pixel_format == MPP_PIXEL_FORMAT_BGRA) {
        stride = width * 4U;
        y_size = stride * height;
        total_size = y_size;
        frame_info.stVFrame.u32PlaneNum = 1U;
        frame_info.stVFrame.u32PlaneStride[0] = stride;
        frame_info.stVFrame.u32PlaneSize[0] = total_size;
        frame_info.stVFrame.u32PlaneSizeValid[0] = total_size;
    } else if (pixel_format == MPP_PIXEL_FORMAT_A8) {
        stride = width;
        y_size = stride * height;
        total_size = y_size;
        frame_info.stVFrame.u32PlaneNum = 1U;
        frame_info.stVFrame.u32PlaneStride[0] = stride;
        frame_info.stVFrame.u32PlaneSize[0] = total_size;
        frame_info.stVFrame.u32PlaneSizeValid[0] = total_size;
    } else {
        return -1;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.u32BufCnt = 1U;
    cfg.u32BufSize = total_size;
    cfg.eModId = mod_id;
    cfg.eRemapMode = VBUF_REMAP_MODE_NOCACHE;

    *pool_id = VB_CreatePool(&cfg);
    if (*pool_id == 0UL) {
        return -1;
    }

    memset(&frame_info, 0, sizeof(frame_info));
    frame_info.eFrameType = FRAME_TYPE_COMMON;
    frame_info.eModId = mod_id;
    frame_info.stCommFrameInfo.u32Width = width;
    frame_info.stCommFrameInfo.u32Height = height;
    frame_info.stCommFrameInfo.u32Align = 1U;
    frame_info.stCommFrameInfo.ePixelFormat = pixel_format;
    frame_info.stCommFrameInfo.eCompressMode = COMPRESS_MODE_NONE;
    frame_info.stCommFrameInfo.eColorSpace = COLOR_SPACE_BT709;
    frame_info.stVFrame.u32TotalSize = total_size;

    if (pixel_format == MPP_PIXEL_FORMAT_NV12) {
        frame_info.stVFrame.u32PlaneNum = 2U;
        frame_info.stVFrame.u32PlaneStride[0] = stride;
        frame_info.stVFrame.u32PlaneStride[1] = stride;
        frame_info.stVFrame.u32PlaneSize[0] = y_size;
        frame_info.stVFrame.u32PlaneSize[1] = y_size / 2U;
        frame_info.stVFrame.u32PlaneSizeValid[0] = y_size;
        frame_info.stVFrame.u32PlaneSizeValid[1] = y_size / 2U;
    } else {
        frame_info.stVFrame.u32PlaneNum = 1U;
        frame_info.stVFrame.u32PlaneStride[0] = stride;
        frame_info.stVFrame.u32PlaneSize[0] = total_size;
        frame_info.stVFrame.u32PlaneSizeValid[0] = total_size;
    }

    ret = VB_SetFrameInfo(*pool_id, &frame_info);
    if (ret != 0) {
        VB_DestroyPool(*pool_id);
        *pool_id = 0UL;
        return -1;
    }

    return 0;
}

static int demo_prepare_frame_from_buffer(UL buffer, VideoFrameInfo *frame)
{
    S32 ret;

    if ((buffer == 0UL) || (frame == NULL)) {
        return -1;
    }

    ret = VB_GetFrameInfo(buffer, frame);
    if (ret != 0) {
        return -1;
    }

    ret = VB_GetDmaBufFd(buffer, (S32 *)&frame->stVFrame.u32Fd[0]);
    if (ret != 0) {
        return -1;
    }

    ret = VB_GetVirAddr(buffer, (void **)&frame->stVFrame.ulPlaneVirAddr[0]);
    if (ret != 0) {
        return -1;
    }

    if (frame->stVFrame.u32PlaneNum > 1U) {
        frame->stVFrame.u32Fd[1] = frame->stVFrame.u32Fd[0];
        frame->stVFrame.ulPlaneVirAddr[1] =
            frame->stVFrame.ulPlaneVirAddr[0] + frame->stVFrame.u32PlaneSize[0];
    }

    return 0;
}

static int demo_load_packed_file(VideoFrameInfo *frame, const char *input_file)
{
    FILE *fp;
    size_t read_size;
    size_t expected_size;
    void *base;

    if ((frame == NULL) || (input_file == NULL) || (frame->stVFrame.ulPlaneVirAddr[0] == 0UL)) {
        return -1;
    }

    base = (void *)(uintptr_t)frame->stVFrame.ulPlaneVirAddr[0];
    expected_size = frame->stVFrame.u32PlaneSize[0];

    fp = fopen(input_file, "rb");
    if (fp == NULL) {
        DEMO_LOG("open input file failed: %s", input_file);
        return -1;
    }

    read_size = fread(base, 1, expected_size, fp);
    fclose(fp);

    if (read_size != expected_size) {
        DEMO_LOG("read input file size mismatch, got=%zu expect=%zu", read_size, expected_size);
        return -1;
    }

    frame->stVFrame.u32PlaneSizeValid[0] = frame->stVFrame.u32PlaneSize[0];
    return 0;
}

static int demo_load_nv12_file(VideoFrameInfo *frame, const char *input_file)
{
    FILE *fp;
    size_t read_size;
    size_t expected_size;
    U8 *base;

    if ((frame == NULL) || (input_file == NULL) || (frame->stVFrame.ulPlaneVirAddr[0] == 0UL)) {
        return -1;
    }

    base = (U8 *)(uintptr_t)frame->stVFrame.ulPlaneVirAddr[0];
    expected_size = (size_t)frame->stVFrame.u32PlaneSize[0] +
                    (size_t)frame->stVFrame.u32PlaneSize[1];

    fp = fopen(input_file, "rb");
    if (fp == NULL) {
        DEMO_LOG("open input file failed: %s", input_file);
        return -1;
    }

    read_size = fread(base, 1, expected_size, fp);
    fclose(fp);

    if (read_size != expected_size) {
        DEMO_LOG("read input file size mismatch, got=%zu expect=%zu", read_size, expected_size);
        return -1;
    }

    frame->stVFrame.u32PlaneSizeValid[0] = frame->stVFrame.u32PlaneSize[0];
    frame->stVFrame.u32PlaneSizeValid[1] = frame->stVFrame.u32PlaneSize[1];
    return 0;
}

static int demo_dump_nv12_file(const VideoFrameInfo *frame, const char *output_file)
{
    FILE *fp;
    size_t write_size;
    size_t expected_size;

    if ((frame == NULL) || (output_file == NULL) || (frame->stVFrame.ulPlaneVirAddr[0] == 0UL)) {
        return -1;
    }

    expected_size = frame->stVFrame.u32PlaneSize[0] + frame->stVFrame.u32PlaneSize[1];

    fp = fopen(output_file, "wb");
    if (fp == NULL) {
        DEMO_LOG("open output file failed: %s", output_file);
        return -1;
    }

    write_size = fwrite((const void *)(uintptr_t)frame->stVFrame.ulPlaneVirAddr[0], 1, expected_size, fp);
    fclose(fp);

    return (write_size == expected_size) ? 0 : -1;
}

int main(int argc, char *argv[])
{
    S32 ret;
    DEMO_CONFIG_S config;
    V2DHandle handle = 0;
    UL bg_pool = 0UL;
    UL fg_pool = 0UL;
    UL mask_pool = 0UL;
    UL dst_pool = 0UL;
    UL bg_buf = 0UL;
    UL fg_buf = 0UL;
    UL mask_buf = 0UL;
    UL dst_buf = 0UL;
    VideoFrameInfo bg_frame;
    VideoFrameInfo fg_frame;
    VideoFrameInfo mask_frame;
    VideoFrameInfo dst_frame;

    memset(&bg_frame, 0, sizeof(bg_frame));
    memset(&fg_frame, 0, sizeof(fg_frame));
    memset(&mask_frame, 0, sizeof(mask_frame));
    memset(&dst_frame, 0, sizeof(dst_frame));

    if (demo_parse_args(argc, argv, &config) != 0) {
        demo_print_usage(argv[0]);
        return -1;
    }

    DEMO_LOG("Background: %s", config.bg_file);
    DEMO_LOG("Foreground: %s", config.fg_file);
    DEMO_LOG("Mask: %s", config.mask_file);
    DEMO_LOG("Output: %s", config.output_file);
    DEMO_LOG("Resolution: %ux%u", config.width, config.height);

    ret = SYS_Init();
    if (ret != 0) {
        DEMO_FAIL("SYS_Init failed, ret=%d", ret);
    }

    ret = VB_Init();
    if (ret != 0) {
        SYS_Exit();
        DEMO_FAIL("VB_Init failed, ret=%d", ret);
    }

    if (demo_prepare_pool(&bg_pool, MPP_ID_SYS, MPP_PIXEL_FORMAT_NV12,
                          config.width, config.height) != 0) {
        goto EXIT;
    }

    if (demo_prepare_pool(&fg_pool, MPP_ID_SYS, MPP_PIXEL_FORMAT_BGRA,
                          config.width, config.height) != 0) {
        goto EXIT;
    }

    if (demo_prepare_pool(&mask_pool, MPP_ID_SYS, MPP_PIXEL_FORMAT_A8,
                          config.width, config.height) != 0) {
        goto EXIT;
    }

    if (demo_prepare_pool(&dst_pool, MPP_ID_SYS, MPP_PIXEL_FORMAT_NV12,
                          config.width, config.height) != 0) {
        goto EXIT;
    }

    bg_buf = VB_GetBuffer(bg_pool, 0);
    fg_buf = VB_GetBuffer(fg_pool, 0);
    mask_buf = VB_GetBuffer(mask_pool, 0);
    dst_buf = VB_GetBuffer(dst_pool, 0);
    if ((bg_buf == 0UL) || (fg_buf == 0UL) || (mask_buf == 0UL) || (dst_buf == 0UL)) {
        DEMO_LOG("VB_GetBuffer failed, bg=0x%lx fg=0x%lx mask=0x%lx dst=0x%lx",
                 bg_buf, fg_buf, mask_buf, dst_buf);
        goto EXIT;
    }

    if (demo_prepare_frame_from_buffer(bg_buf, &bg_frame) != 0) {
        DEMO_LOG("prepare bg frame failed");
        goto EXIT;
    }

    if (demo_prepare_frame_from_buffer(fg_buf, &fg_frame) != 0) {
        DEMO_LOG("prepare fg frame failed");
        goto EXIT;
    }

    if (demo_prepare_frame_from_buffer(mask_buf, &mask_frame) != 0) {
        DEMO_LOG("prepare mask frame failed");
        goto EXIT;
    }

    if (demo_prepare_frame_from_buffer(dst_buf, &dst_frame) != 0) {
        DEMO_LOG("prepare dst frame failed");
        goto EXIT;
    }

    if (demo_load_nv12_file(&bg_frame, config.bg_file) != 0) {
        DEMO_LOG("load bg file failed: %s", config.bg_file);
        goto EXIT;
    }

    if (demo_load_packed_file(&fg_frame, config.fg_file) != 0) {
        DEMO_LOG("load fg file failed: %s", config.fg_file);
        goto EXIT;
    }

    if (demo_load_packed_file(&mask_frame, config.mask_file) != 0) {
        DEMO_LOG("load mask file failed: %s", config.mask_file);
        goto EXIT;
    }

    memset((void *)(uintptr_t)dst_frame.stVFrame.ulPlaneVirAddr[0], 0,
           dst_frame.stVFrame.u32PlaneSize[0] + dst_frame.stVFrame.u32PlaneSize[1]);

    ret = V2D_BeginJob(&handle);
    if (ret == 0) {
        ret = V2D_DrawMask(handle,
                           &bg_frame,
                           NULL,
                           &fg_frame,
                           NULL,
                           &mask_frame,
                           NULL,
                           &dst_frame,
                           NULL);
        if (ret != 0) {
            V2D_CancelJob(handle);
        } else {
            ret = V2D_EndJob(handle);
        }
    }

    if (ret != 0) {
        DEMO_LOG("V2D_DrawMask failed, ret=%d", ret);
        goto EXIT;
    }

    dst_frame.stVFrame.u32PlaneSizeValid[0] = dst_frame.stVFrame.u32PlaneSize[0];
    dst_frame.stVFrame.u32PlaneSizeValid[1] = dst_frame.stVFrame.u32PlaneSize[1];

    if (demo_dump_nv12_file(&dst_frame, config.output_file) != 0) {
        DEMO_LOG("dump output failed: %s", config.output_file);
        goto EXIT;
    }

    DEMO_LOG("mask blend finished successfully");

EXIT:
    if (bg_buf != 0UL) {
        VB_ReleaseBuffer(bg_buf);
    }
    if (fg_buf != 0UL) {
        VB_ReleaseBuffer(fg_buf);
    }
    if (mask_buf != 0UL) {
        VB_ReleaseBuffer(mask_buf);
    }
    if (dst_buf != 0UL) {
        VB_ReleaseBuffer(dst_buf);
    }
    if (bg_pool != 0UL) {
        VB_DestroyPool(bg_pool);
    }
    if (fg_pool != 0UL) {
        VB_DestroyPool(fg_pool);
    }
    if (mask_pool != 0UL) {
        VB_DestroyPool(mask_pool);
    }
    if (dst_pool != 0UL) {
        VB_DestroyPool(dst_pool);
    }
    VB_Exit();
    SYS_Exit();
    return ret;
}