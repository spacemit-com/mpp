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

#define DEMO_BG_FILE "./mask_bg_nv12.yuv"
#define DEMO_FG_FILE "./mask_fg_bgra.raw"
#define DEMO_MASK_FILE "./mask_a8.raw"
#define DEMO_OUTPUT_FILE "./test_v2d_mask_out.yuv"
#define DEMO_WIDTH 1920U
#define DEMO_HEIGHT 1080U

#define DEMO_LOG(fmt, ...) printf("[test_v2d_mask] " fmt "\n", ##__VA_ARGS__)
#define DEMO_FAIL(fmt, ...)                                       \
    do {                                                          \
        printf("[test_v2d_mask][FAIL] " fmt "\n", ##__VA_ARGS__); \
        return -1;                                                \
    } while (0)

typedef struct DEMO_CONFIG_S {
    const char *bg_file;
    const char *fg_file;
    const char *mask_file;
    const char *output_file;
    U32 width;
    U32 height;
} DEMO_CONFIG_S;

static void demo_set_default_config(DEMO_CONFIG_S *config) {
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

static int demo_parse_u32(const char *text, U32 *value) {
    char *end_ptr = NULL;
    uint64_t parsed;

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

static void demo_print_usage(const char *prog) {
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

static int demo_parse_args(int argc, char *argv[], DEMO_CONFIG_S *config) {
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
    if ((demo_parse_u32(argv[5], &config->width) != 0) || (demo_parse_u32(argv[6], &config->height) != 0)) {
        return -1;
    }

    return 0;
}

/*
 * Allocate a VB pool, grab one buffer from it, and fill out the
 * VideoFrameInfo so V2D can consume it directly (UVC-style flow).
 */
static int demo_prepare_pool(
    UL *pool_id, VideoFrameInfo *frame, ModId mod_id, MppPixelFormat pixel_format, U32 width, U32 height
) {
    VbPoolCfg cfg;
    UL buffer = 0UL;
    S32 ret;
    U32 stride;
    U32 y_size;
    U32 total_size;
    U32 plane_num;
    void *vir_addr = NULL;
    S32 dma_buf_fd = -1;

    if ((pool_id == NULL) || (frame == NULL)) {
        return -1;
    }

    if (pixel_format == MPP_PIXEL_FORMAT_NV12) {
        stride = width;
        y_size = stride * height;
        total_size = y_size + (y_size / 2U);
        plane_num = 2U;
    } else if (pixel_format == MPP_PIXEL_FORMAT_BGRA) {
        stride = width * 4U;
        y_size = stride * height;
        total_size = y_size;
        plane_num = 1U;
    } else if (pixel_format == MPP_PIXEL_FORMAT_A8) {
        stride = width;
        y_size = stride * height;
        total_size = y_size;
        plane_num = 1U;
    } else {
        DEMO_LOG("unsupported format in demo, format=%d", pixel_format);
        return -1;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.u32BufCnt = 1U; /* single-shot demo: one buffer per pool, bound to `frame` */
    cfg.u32BufSize = total_size;
    cfg.eModId = mod_id;
    cfg.eRemapMode = VBUF_REMAP_MODE_NOCACHE;

    *pool_id = VB_CreatePool(&cfg);
    if (*pool_id == 0UL) {
        DEMO_LOG("VB_CreatePool failed, mod=%d format=%d", mod_id, pixel_format);
        return -1;
    }

    /* Acquire one buffer from the pool and resolve its fd / virtual address. */
    buffer = VB_GetBuffer(*pool_id, 0);
    if (buffer == 0UL) {
        DEMO_LOG("VB_GetBuffer failed, mod=%d", mod_id);
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
    frame->stCommFrameInfo.ePixelFormat = pixel_format;

    frame->stVFrame.u32PlaneNum = plane_num;
    frame->stVFrame.u32PlaneStride[0] = stride;
    frame->stVFrame.u32PlaneSize[0] = y_size;
    frame->stVFrame.u32PlaneSizeValid[0] = y_size;
    frame->stVFrame.u32TotalSize = total_size;
    frame->stVFrame.u32Fd[0] = (UL)dma_buf_fd;
    frame->stVFrame.ulPlaneVirAddr[0] = (UL)vir_addr;

    if (plane_num > 1U) {
        frame->stVFrame.u32PlaneStride[1] = stride;
        frame->stVFrame.u32PlaneSize[1] = y_size / 2U;
        frame->stVFrame.u32PlaneSizeValid[1] = y_size / 2U;
        frame->stVFrame.u32Fd[1] = (UL)dma_buf_fd;
        frame->stVFrame.ulPlaneVirAddr[1] = (UL)vir_addr + y_size;
    }

    return 0;

err_release_buf:
    VB_ReleaseBuffer(buffer);
err_destroy_pool:
    VB_DestroyPool(*pool_id);
    *pool_id = 0UL;
    return -1;
}

static int demo_load_packed_file(VideoFrameInfo *frame, const char *input_file) {
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

    return 0;
}

static int demo_load_nv12_file(VideoFrameInfo *frame, const char *input_file) {
    FILE *fp;
    size_t read_size;
    size_t expected_size;
    U8 *base;

    if ((frame == NULL) || (input_file == NULL) || (frame->stVFrame.ulPlaneVirAddr[0] == 0UL)) {
        return -1;
    }

    base = (U8 *)(uintptr_t)frame->stVFrame.ulPlaneVirAddr[0];
    expected_size = (size_t)frame->stVFrame.u32PlaneSize[0] + (size_t)frame->stVFrame.u32PlaneSize[1];

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

    return 0;
}

static int demo_dump_nv12_file(const VideoFrameInfo *frame, const char *output_file) {
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

int main(int argc, char *argv[]) {
    S32 ret;
    DEMO_CONFIG_S config;
    V2DHandle handle = 0;
    UL bg_pool = 0UL;
    UL fg_pool = 0UL;
    UL mask_pool = 0UL;
    UL dst_pool = 0UL;
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

    if (demo_prepare_pool(&bg_pool, &bg_frame, MPP_ID_V2D, MPP_PIXEL_FORMAT_NV12, config.width, config.height) != 0) {
        goto EXIT;
    }

    if (demo_prepare_pool(&fg_pool, &fg_frame, MPP_ID_V2D, MPP_PIXEL_FORMAT_BGRA, config.width, config.height) != 0) {
        goto EXIT;
    }

    if (demo_prepare_pool(&mask_pool, &mask_frame, MPP_ID_V2D, MPP_PIXEL_FORMAT_A8, config.width, config.height) != 0) {
        goto EXIT;
    }

    if (demo_prepare_pool(&dst_pool, &dst_frame, MPP_ID_V2D, MPP_PIXEL_FORMAT_NV12, config.width, config.height) != 0) {
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

    ret = V2D_BeginJob(&handle);
    if (ret == 0) {
        ret = V2D_DrawMask(handle, &bg_frame, &fg_frame, &mask_frame, &dst_frame);
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

    if (demo_dump_nv12_file(&dst_frame, config.output_file) != 0) {
        DEMO_LOG("dump output failed: %s", config.output_file);
        goto EXIT;
    }

    DEMO_LOG("mask blend finished successfully");

EXIT:
    if (bg_frame.ulBufferId != 0UL) {
        VB_ReleaseBuffer(bg_frame.ulBufferId);
    }
    if (fg_frame.ulBufferId != 0UL) {
        VB_ReleaseBuffer(fg_frame.ulBufferId);
    }
    if (mask_frame.ulBufferId != 0UL) {
        VB_ReleaseBuffer(mask_frame.ulBufferId);
    }
    if (dst_frame.ulBufferId != 0UL) {
        VB_ReleaseBuffer(dst_frame.ulBufferId);
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
