/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    v2d_vb_demo.c
 * @Brief     :    Example: allocate source and destination buffers from VB,
 *                 then run a V2D resize or format-convert job selected by CLI.
 *
 * Notes:
 *   1. This is a demo executable, not a CTest case, because it depends on
 *      `/dev/v2d_dev` and real V2D hardware.
 *   2. Source buffer uses NV12 (`./vi_phy0_first_frame.yuv`) and destination
 *      buffer also uses NV12.
 *   3. The example follows the UVC-style flow: VB_CreatePool + VB_GetBuffer +
 *      VB_GetDmaBufFd + VB_GetVirAddr right after pool creation, and fills
 *      VideoFrameInfo inline. It deliberately avoids VB_SetFrameInfo /
 *      VB_GetFrameInfo, since the V2D driver only needs fd/stride/size and the
 *      width/height/format kept in stCommFrameInfo.
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sys_api.h"
#include "vb_api.h"
#include "v2d_api.h"

#define DEMO_INPUT_FILE "./vi_phy0_first_frame.yuv"
#define DEMO_INPUT_FILE_OVERLAY "./v2d_overlay.bgra"
#define DEMO_OUTPUT_FILE_RESIZE "./v2d_case1_resize_out.yuv"
#define DEMO_OUTPUT_FILE_CONVERT "./v2d_case1_convert_out.bgra"
#define DEMO_OUTPUT_FILE_ROTATE "./v2d_case1_rotate_out.yuv"
#define DEMO_OUTPUT_FILE_BLEND "./v2d_case1_adv2layers_out.yuv"
#define DEFAULT_SRC_WIDTH 1920U
#define DEFAULT_SRC_HEIGHT 1080U
#define DEFAULT_DST_WIDTH 640U
#define DEFAULT_DST_HEIGHT 480U

typedef enum DEMO_MODE_E {
    DEMO_MODE_RESIZE = 0,
    DEMO_MODE_CONVERT,
    DEMO_MODE_ROTATE,
    DEMO_MODE_ADV2LAYERS,
} DEMO_MODE_E;

typedef struct DEMO_CONFIG_S {
    DEMO_MODE_E enMode;
    const char *input_file;
    const char *overlay_file;
    const char *output_file;
    U32 src_width;
    U32 src_height;
    U32 dst_width;
    U32 dst_height;
    MppPixelFormat src_format;
    MppPixelFormat dst_format;
    MppPixelFormat overlay_format;
} DEMO_CONFIG_S;

#define DEMO_LOG(fmt, ...) printf("[v2d_vb_demo] " fmt "\n", ##__VA_ARGS__)
#define DEMO_FAIL(fmt, ...)                                     \
    do {                                                        \
        printf("[v2d_vb_demo][FAIL] " fmt "\n", ##__VA_ARGS__); \
        return -1;                                              \
    } while (0)

static const char *demo_pixel_format_name(MppPixelFormat pixel_format) {
    switch (pixel_format) {
        case MPP_PIXEL_FORMAT_NV12:
            return "nv12";
        case MPP_PIXEL_FORMAT_NV21:
            return "nv21";
        case MPP_PIXEL_FORMAT_BGRA:
            return "bgra";
        case MPP_PIXEL_FORMAT_RGBA:
            return "rgba";
        case MPP_PIXEL_FORMAT_ARGB:
            return "argb";
        case MPP_PIXEL_FORMAT_ABGR:
            return "abgr";
        case MPP_PIXEL_FORMAT_RGB_888:
            return "rgb888";
        case MPP_PIXEL_FORMAT_BGR_888:
            return "bgr888";
        case MPP_PIXEL_FORMAT_RGB_565:
            return "rgb565";
        case MPP_PIXEL_FORMAT_BGR_565:
            return "bgr565";
        case MPP_PIXEL_FORMAT_A8:
            return "a8";
        default:
            return "unknown";
    }
}

static int demo_parse_pixel_format(const char *text, MppPixelFormat *pixel_format) {
    if ((text == NULL) || (pixel_format == NULL)) {
        return -1;
    }

    if ((strcmp(text, "nv12") == 0) || (strcmp(text, "NV12") == 0)) {
        *pixel_format = MPP_PIXEL_FORMAT_NV12;
        return 0;
    }

    if ((strcmp(text, "nv21") == 0) || (strcmp(text, "NV21") == 0)) {
        *pixel_format = MPP_PIXEL_FORMAT_NV21;
        return 0;
    }

    if ((strcmp(text, "bgra") == 0) || (strcmp(text, "BGRA") == 0)) {
        *pixel_format = MPP_PIXEL_FORMAT_BGRA;
        return 0;
    }

    if ((strcmp(text, "rgba") == 0) || (strcmp(text, "RGBA") == 0)) {
        *pixel_format = MPP_PIXEL_FORMAT_RGBA;
        return 0;
    }

    if ((strcmp(text, "argb") == 0) || (strcmp(text, "ARGB") == 0)) {
        *pixel_format = MPP_PIXEL_FORMAT_ARGB;
        return 0;
    }

    if ((strcmp(text, "abgr") == 0) || (strcmp(text, "ABGR") == 0)) {
        *pixel_format = MPP_PIXEL_FORMAT_ABGR;
        return 0;
    }

    if ((strcmp(text, "rgb888") == 0) || (strcmp(text, "RGB888") == 0)) {
        *pixel_format = MPP_PIXEL_FORMAT_RGB_888;
        return 0;
    }

    if ((strcmp(text, "bgr888") == 0) || (strcmp(text, "BGR888") == 0)) {
        *pixel_format = MPP_PIXEL_FORMAT_BGR_888;
        return 0;
    }

    if ((strcmp(text, "rgb565") == 0) || (strcmp(text, "RGB565") == 0)) {
        *pixel_format = MPP_PIXEL_FORMAT_RGB_565;
        return 0;
    }

    if ((strcmp(text, "bgr565") == 0) || (strcmp(text, "BGR565") == 0)) {
        *pixel_format = MPP_PIXEL_FORMAT_BGR_565;
        return 0;
    }

    if ((strcmp(text, "a8") == 0) || (strcmp(text, "A8") == 0)) {
        *pixel_format = MPP_PIXEL_FORMAT_A8;
        return 0;
    }

    return -1;
}

static void demo_print_usage(const char *prog) {
    printf("Usage:\n");
    printf(
        "  %s resize [input_file src_width src_height src_format output_file dst_width dst_height dst_format]\n", prog);
    printf("  %s convert [input_file src_width src_height src_format output_file dst_format]\n", prog);
    printf("  %s rotate [input_file src_width src_height src_format output_file dst_format]\n", prog);
    printf(
        "  %s adv2layers [background_file foreground_file width height background_format foreground_format output_file "
        "dst_format]\n",
        prog);
    printf("\nSupported formats: nv12 nv21 bgra rgba argb abgr rgb888 bgr888 rgb565 bgr565 a8\n");
    printf("\nresize defaults:\n");
    printf("  input_file  : %s\n", DEMO_INPUT_FILE);
    printf("  src_width   : %u\n", DEFAULT_SRC_WIDTH);
    printf("  src_height  : %u\n", DEFAULT_SRC_HEIGHT);
    printf("  src_format  : nv12\n");
    printf("  output_file : %s\n", DEMO_OUTPUT_FILE_RESIZE);
    printf("  dst_width   : %u\n", DEFAULT_DST_WIDTH);
    printf("  dst_height  : %u\n", DEFAULT_DST_HEIGHT);
    printf("  dst_format  : nv12\n");
    printf("\nconvert defaults:\n");
    printf("  input_file  : %s\n", DEMO_INPUT_FILE);
    printf("  src_width   : %u\n", DEFAULT_SRC_WIDTH);
    printf("  src_height  : %u\n", DEFAULT_SRC_HEIGHT);
    printf("  src_format  : nv12\n");
    printf("  output_file : %s\n", DEMO_OUTPUT_FILE_CONVERT);
    printf("  dst_format  : bgra\n");
    printf("\nrotate defaults:\n");
    printf("  input_file  : %s\n", DEMO_INPUT_FILE);
    printf("  src_width   : %u\n", DEFAULT_SRC_WIDTH);
    printf("  src_height  : %u\n", DEFAULT_SRC_HEIGHT);
    printf("  src_format  : nv12\n");
    printf("  output_file : %s\n", DEMO_OUTPUT_FILE_ROTATE);
    printf("  dst_format  : nv12\n");
    printf("\nadv2layers defaults:\n");
    printf("  background  : %s\n", DEMO_INPUT_FILE);
    printf("  foreground  : %s\n", DEMO_INPUT_FILE_OVERLAY);
    printf("  width       : %u\n", DEFAULT_SRC_WIDTH);
    printf("  height      : %u\n", DEFAULT_SRC_HEIGHT);
    printf("  bg_format   : nv12\n");
    printf("  fg_format   : bgra\n");
    printf("  output_file : %s\n", DEMO_OUTPUT_FILE_BLEND);
    printf("  dst_format  : nv12\n");
    printf("\nExample:\n");
    printf("  %s resize input.yuv 1920 1080 nv12 output.yuv 640 480 nv12\n", prog);
    printf("  %s convert input.yuv 1920 1080 nv12 output.bgra bgra\n", prog);
    printf("  %s rotate input.yuv 1920 1080 nv12 output.rgba rgba\n", prog);
    printf("  %s adv2layers bg.yuv fg.bgra 1920 1080 nv12 bgra output.yuv nv12\n", prog);
}

static int demo_parse_mode(const char *mode_str, DEMO_MODE_E *mode) {
    if (mode_str == NULL || mode == NULL) {
        return -1;
    }

    if (strcmp(mode_str, "resize") == 0) {
        *mode = DEMO_MODE_RESIZE;
        return 0;
    }

    if (strcmp(mode_str, "convert") == 0) {
        *mode = DEMO_MODE_CONVERT;
        return 0;
    }

    if (strcmp(mode_str, "rotate") == 0) {
        *mode = DEMO_MODE_ROTATE;
        return 0;
    }

    if (strcmp(mode_str, "adv2layers") == 0) {
        *mode = DEMO_MODE_ADV2LAYERS;
        return 0;
    }

    return -1;
}

static int demo_parse_u32(const char *text, U32 *value) {
    char *end_ptr = NULL;
    uint64_t parsed;

    if (text == NULL || value == NULL || text[0] == '\0') {
        return -1;
    }

    parsed = strtoul(text, &end_ptr, 10);
    if (*end_ptr != '\0' || parsed == 0UL) {
        return -1;
    }

    *value = (U32)parsed;
    return 0;
}

static void demo_set_default_config(DEMO_CONFIG_S *config) {
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->enMode = DEMO_MODE_RESIZE;
    config->input_file = DEMO_INPUT_FILE;
    config->overlay_file = DEMO_INPUT_FILE_OVERLAY;
    config->output_file = DEMO_OUTPUT_FILE_RESIZE;
    config->src_width = DEFAULT_SRC_WIDTH;
    config->src_height = DEFAULT_SRC_HEIGHT;
    config->dst_width = DEFAULT_DST_WIDTH;
    config->dst_height = DEFAULT_DST_HEIGHT;
    config->src_format = MPP_PIXEL_FORMAT_NV12;
    config->dst_format = MPP_PIXEL_FORMAT_NV12;
    config->overlay_format = MPP_PIXEL_FORMAT_BGRA;
}

static int demo_parse_args(int argc, char *argv[], DEMO_CONFIG_S *config) {
    if (config == NULL) {
        return -1;
    }

    demo_set_default_config(config);

    if (argc == 1) {
        return 0;
    }

    if (demo_parse_mode(argv[1], &config->enMode) != 0) {
        return -1;
    }

    if (config->enMode == DEMO_MODE_RESIZE) {
        config->output_file = DEMO_OUTPUT_FILE_RESIZE;
        config->dst_format = MPP_PIXEL_FORMAT_NV12;

        if (argc == 2) {
            return 0;
        }

        if (argc != 10) {
            return -1;
        }

        config->input_file = argv[2];
        config->output_file = argv[6];
        if (demo_parse_u32(argv[3], &config->src_width) != 0 || demo_parse_u32(argv[4], &config->src_height) != 0 ||
            demo_parse_pixel_format(argv[5], &config->src_format) != 0 ||
            demo_parse_u32(argv[7], &config->dst_width) != 0 || demo_parse_u32(argv[8], &config->dst_height) != 0 ||
            demo_parse_pixel_format(argv[9], &config->dst_format) != 0) {
            return -1;
        }

        return 0;
    }

    if (config->enMode == DEMO_MODE_ADV2LAYERS) {
        config->overlay_file = DEMO_INPUT_FILE_OVERLAY;
        config->output_file = DEMO_OUTPUT_FILE_BLEND;
        config->src_format = MPP_PIXEL_FORMAT_NV12;
        config->overlay_format = MPP_PIXEL_FORMAT_BGRA;
        config->dst_format = MPP_PIXEL_FORMAT_NV12;
        config->dst_width = config->src_width;
        config->dst_height = config->src_height;

        if (argc == 2) {
            return 0;
        }

        if (argc != 10) {
            return -1;
        }

        config->input_file = argv[2];
        config->overlay_file = argv[3];
        config->output_file = argv[8];
        if (demo_parse_u32(argv[4], &config->src_width) != 0 || demo_parse_u32(argv[5], &config->src_height) != 0 ||
            demo_parse_pixel_format(argv[6], &config->src_format) != 0 ||
            demo_parse_pixel_format(argv[7], &config->overlay_format) != 0 ||
            demo_parse_pixel_format(argv[9], &config->dst_format) != 0) {
            return -1;
        }

        config->dst_width = config->src_width;
        config->dst_height = config->src_height;
        return 0;
    }

    if (config->enMode == DEMO_MODE_ROTATE) {
        config->output_file = DEMO_OUTPUT_FILE_ROTATE;
        config->dst_format = MPP_PIXEL_FORMAT_NV12;
        config->dst_width = config->src_height;
        config->dst_height = config->src_width;

        if (argc == 2) {
            return 0;
        }

        if (argc != 8) {
            return -1;
        }

        config->input_file = argv[2];
        config->output_file = argv[6];
        if (demo_parse_u32(argv[3], &config->src_width) != 0 || demo_parse_u32(argv[4], &config->src_height) != 0 ||
            demo_parse_pixel_format(argv[5], &config->src_format) != 0 ||
            demo_parse_pixel_format(argv[7], &config->dst_format) != 0) {
            return -1;
        }

        config->dst_width = config->src_height;
        config->dst_height = config->src_width;
        return 0;
    }

    config->output_file = DEMO_OUTPUT_FILE_CONVERT;
    config->src_format = MPP_PIXEL_FORMAT_NV12;
    config->dst_format = MPP_PIXEL_FORMAT_BGRA;

    if (argc == 2) {
        config->dst_width = config->src_width;
        config->dst_height = config->src_height;
        return 0;
    }

    if (argc != 8) {
        return -1;
    }

    config->input_file = argv[2];
    config->output_file = argv[6];
    if (demo_parse_u32(argv[3], &config->src_width) != 0 || demo_parse_u32(argv[4], &config->src_height) != 0 ||
        demo_parse_pixel_format(argv[5], &config->src_format) != 0 ||
        demo_parse_pixel_format(argv[7], &config->dst_format) != 0) {
        return -1;
    }

    config->dst_width = config->src_width;
    config->dst_height = config->src_height;
    return 0;
}

static int demo_load_yuv420sp_file(VideoFrameInfo *frame, const char *input_file) {
    FILE *fp;
    size_t read_size;
    size_t expected_size;
    U8 *base;
    U8 *plane0;
    U8 *plane1;

    if (frame == NULL) {
        return -1;
    }

    plane0 = (U8 *)frame->stVFrame.ulPlaneVirAddr[0];
    plane1 = (U8 *)frame->stVFrame.ulPlaneVirAddr[1];
    if (plane0 == NULL) {
        return -1;
    }

    base = plane0;
    expected_size = (size_t)frame->stVFrame.u32PlaneSize[0] + (size_t)frame->stVFrame.u32PlaneSize[1];

    fp = fopen(input_file, "rb");
    if (fp == NULL) {
        DEMO_LOG("Failed to open input file: %s (check if file exists)", input_file);
        perror("fopen");
        return -1;
    }

    read_size = fread(base, 1, expected_size, fp);
    fclose(fp);

    if (read_size != expected_size) {
        DEMO_LOG("File size mismatch: read=%zu bytes, expected=%zu bytes", read_size, expected_size);
        DEMO_LOG("Make sure the input file matches the specified dimensions");
        return -1;
    }

    if (plane1 != (plane0 + frame->stVFrame.u32PlaneSize[0])) {
        memcpy(plane1, base + frame->stVFrame.u32PlaneSize[0], frame->stVFrame.u32PlaneSize[1]);
    }

    return 0;
}

static int demo_load_packed_file(VideoFrameInfo *frame, const char *input_file) {
    FILE *fp;
    size_t read_size;
    size_t expected_size = 0;
    U32 i;
    void *plane0;

    if ((frame == NULL) || (input_file == NULL)) {
        return -1;
    }

    plane0 = (void *)frame->stVFrame.ulPlaneVirAddr[0];
    if (plane0 == NULL) {
        return -1;
    }

    for (i = 0; i < frame->stVFrame.u32PlaneNum; ++i) {
        expected_size += frame->stVFrame.u32PlaneSize[i];
    }

    fp = fopen(input_file, "rb");
    if (fp == NULL) {
        DEMO_LOG("Failed to open input file: %s", input_file);
        perror("fopen");
        return -1;
    }

    read_size = fread(plane0, 1, expected_size, fp);
    fclose(fp);

    if (read_size != expected_size) {
        DEMO_LOG("File size mismatch: read=%zu bytes, expected=%zu bytes", read_size, expected_size);
        return -1;
    }

    return 0;
}

static int demo_dump_nv12_file_as(const VideoFrameInfo *frame, const char *file_name) {
    FILE *fp;
    size_t write_size;
    const void *plane0;

    if ((frame == NULL) || (file_name == NULL)) {
        return -1;
    }

    plane0 = (const void *)frame->stVFrame.ulPlaneVirAddr[0];
    if (plane0 == NULL) {
        return -1;
    }

    fp = fopen(file_name, "wb");
    if (fp == NULL) {
        DEMO_LOG("open output file failed: %s", file_name);
        return -1;
    }

    write_size = fwrite(plane0, 1, frame->stVFrame.u32PlaneSize[0] + frame->stVFrame.u32PlaneSize[1], fp);
    fclose(fp);

    if (write_size != frame->stVFrame.u32PlaneSize[0] + frame->stVFrame.u32PlaneSize[1]) {
        DEMO_LOG(
            "write output file size mismatch, got=%zu expect=%u",
            write_size,
            frame->stVFrame.u32PlaneSize[0] + frame->stVFrame.u32PlaneSize[1]);
        return -1;
    }

    return 0;
}

static int demo_dump_frame_file_as(const VideoFrameInfo *frame, const char *file_name) {
    FILE *fp;
    size_t write_size;
    size_t expected_size = 0;
    U32 i;
    const void *plane0;

    if ((frame == NULL) || (file_name == NULL)) {
        return -1;
    }

    plane0 = (const void *)frame->stVFrame.ulPlaneVirAddr[0];
    if (plane0 == NULL) {
        return -1;
    }

    for (i = 0; i < frame->stVFrame.u32PlaneNum; ++i) {
        expected_size += frame->stVFrame.u32PlaneSize[i];
    }

    fp = fopen(file_name, "wb");
    if (fp == NULL) {
        DEMO_LOG("open output file failed: %s", file_name);
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

static int demo_is_yuv420sp_format(MppPixelFormat pixel_format) {
    return ((pixel_format == MPP_PIXEL_FORMAT_NV12) || (pixel_format == MPP_PIXEL_FORMAT_NV21)) ? 1 : 0;
}

static U32 demo_get_packed_bpp(MppPixelFormat pixel_format) {
    switch (pixel_format) {
        case MPP_PIXEL_FORMAT_BGRA:
        case MPP_PIXEL_FORMAT_RGBA:
        case MPP_PIXEL_FORMAT_ARGB:
        case MPP_PIXEL_FORMAT_ABGR:
            return 4U;
        case MPP_PIXEL_FORMAT_RGB_888:
        case MPP_PIXEL_FORMAT_BGR_888:
            return 3U;
        case MPP_PIXEL_FORMAT_RGB_565:
        case MPP_PIXEL_FORMAT_BGR_565:
            return 2U;
        case MPP_PIXEL_FORMAT_A8:
            return 1U;
        default:
            return 0U;
    }
}

static void demo_reset_frame(VideoFrameInfo *frame) {
    void *plane0;
    void *plane1;

    if (frame == NULL)
        return;

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
 * Allocate a VB pool, grab one buffer from it, and fill out the
 * VideoFrameInfo so V2D can consume it directly.
 *
 * Mirrors the UVC-style flow (VB_GetBuffer + VB_GetDmaBufFd + VB_GetVirAddr
 * right after VB_CreatePool). V2D internally only needs:
 *   - stVFrame.u32Fd[0]            (DMA-BUF fd, the hardware handle)
 *   - stVFrame.u32PlaneStride[0]   (row stride)
 *   - stVFrame.u32PlaneSize[0]     (UV plane offset for NV12/NV21)
 *   - stVFrame.u32PlaneNum         (only checked != 0)
 *   - stCommFrameInfo.{u32Width,u32Height,ePixelFormat}
 * Everything else (Align/CompressMode/ColorSpace/PlaneSizeValid/TotalSize/...)
 * is filled for completeness and CPU-side load/dump helpers, but is not read
 * by the V2D driver itself.
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

    if (demo_is_yuv420sp_format(pixel_format)) {
        stride = width;
        y_size = stride * height;
        total_size = y_size + (y_size / 2U);
        plane_num = 2U;
    } else {
        U32 bpp = demo_get_packed_bpp(pixel_format);

        if (bpp == 0U) {
            DEMO_LOG("unsupported format in demo, format=%d", pixel_format);
            return -1;
        }

        stride = width * bpp;
        y_size = stride * height;
        total_size = y_size;
        plane_num = 1U;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.u32BufSize = total_size;
    cfg.u32BufCnt = 1; /* single-shot demo: one buffer per pool, bound to `frame` */
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

static int demo_run_resize(const DEMO_CONFIG_S *config, const VideoFrameInfo *src_frame, VideoFrameInfo *dst_frame) {
    V2DHandle handle = 0;
    S32 ret;
    struct timespec t0, t1;

    if (config == NULL || src_frame == NULL || dst_frame == NULL) {
        return -1;
    }

    demo_reset_frame(dst_frame);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    ret = V2D_BeginJob(&handle);
    if (ret != 0) {
        return ret;
    }

    ret = V2D_ResizeFrame(handle, src_frame, dst_frame);
    if (ret != 0) {
        V2D_CancelJob(handle);
        return ret;
    }

    ret = V2D_EndJob(handle);
    if (ret != 0) {
        return ret;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("[MPP_PERF] module=V2D op=resize cost_us=%.0f frame=0\n",
        (double)(t1.tv_sec - t0.tv_sec) * 1000000.0 + (double)(t1.tv_nsec - t0.tv_nsec) / 1000.0);

    return 0;
}

static int demo_run_convert(const DEMO_CONFIG_S *config, const VideoFrameInfo *src_frame, VideoFrameInfo *dst_frame) {
    void *plane0;
    V2DHandle handle = 0;
    S32 ret;
    if (src_frame == NULL || dst_frame == NULL) {
        return -1;
    }

    plane0 = (void *)dst_frame->stVFrame.ulPlaneVirAddr[0];
    if (plane0 == NULL) {
        return -1;
    }

    memset(plane0, 0, dst_frame->stVFrame.u32PlaneSize[0]);
    if (dst_frame->stVFrame.u32PlaneNum > 1U) {
        memset((void *)dst_frame->stVFrame.ulPlaneVirAddr[1], 0x80, dst_frame->stVFrame.u32PlaneSize[1]);
    }

    {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    ret = V2D_BeginJob(&handle);
    if (ret != 0) {
        return ret;
    }

    ret = V2D_ConvertFrame(handle, src_frame, dst_frame);
    if (ret != 0) {
        V2D_CancelJob(handle);
        return ret;
    }

    ret = V2D_EndJob(handle);
    if (ret != 0) {
        return ret;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("[MPP_PERF] module=V2D op=convert cost_us=%.0f frame=0\n",
        (double)(t1.tv_sec - t0.tv_sec) * 1000000.0 + (double)(t1.tv_nsec - t0.tv_nsec) / 1000.0);
    }

    return 0;
}

static int demo_run_adv2layers(
    const DEMO_CONFIG_S *config,
    const VideoFrameInfo *background_frame,
    const VideoFrameInfo *foreground_frame,
    VideoFrameInfo *dst_frame
) {
    V2DHandle handle = 0;
    S32 ret;

    if (config == NULL || background_frame == NULL || foreground_frame == NULL || dst_frame == NULL) {
        return -1;
    }

    demo_reset_frame(dst_frame);

    {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    ret = V2D_BeginJob(&handle);
    if (ret != 0) {
        return ret;
    }

    ret = V2D_Adv2Layers(handle, background_frame, foreground_frame, dst_frame);
    if (ret != 0) {
        V2D_CancelJob(handle);
        return ret;
    }

    ret = V2D_EndJob(handle);
    if (ret != 0) {
        return ret;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("[MPP_PERF] module=V2D op=blend cost_us=%.0f frame=0\n",
        (double)(t1.tv_sec - t0.tv_sec) * 1000000.0 + (double)(t1.tv_nsec - t0.tv_nsec) / 1000.0);
    }

    return 0;
}

static int demo_run_rotate(const DEMO_CONFIG_S *config, const VideoFrameInfo *src_frame, VideoFrameInfo *dst_frame) {
    V2DHandle handle = 0;
    S32 ret;

    if (config == NULL || src_frame == NULL || dst_frame == NULL) {
        return -1;
    }

    demo_reset_frame(dst_frame);

    {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    ret = V2D_BeginJob(&handle);
    if (ret != 0) {
        return ret;
    }

    ret = V2D_RotateFrame(handle, src_frame, dst_frame, V2D_ROT_90);
    if (ret != 0) {
        V2D_CancelJob(handle);
        return ret;
    }

    ret = V2D_EndJob(handle);
    if (ret != 0) {
        return ret;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("[MPP_PERF] module=V2D op=rotate cost_us=%.0f frame=0\n",
        (double)(t1.tv_sec - t0.tv_sec) * 1000000.0 + (double)(t1.tv_nsec - t0.tv_nsec) / 1000.0);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    S32 ret;
    DEMO_CONFIG_S config;
    UL src_pool = 0UL;
    UL overlay_pool = 0UL;
    UL dst_pool = 0UL;
    VideoFrameInfo src_frame;
    VideoFrameInfo overlay_frame;
    VideoFrameInfo dst_frame;
    memset(&src_frame, 0, sizeof(src_frame));
    memset(&overlay_frame, 0, sizeof(overlay_frame));
    memset(&dst_frame, 0, sizeof(dst_frame));

    {
        int i;
        for (i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
                demo_print_usage(argv[0]);
                return 0;
            }
        }
    }

    if (demo_parse_args(argc, argv, &config) != 0) {
        demo_print_usage(argv[0]);
        return -1;
    }

    DEMO_LOG(
        "Mode: %s",
        (config.enMode == DEMO_MODE_RESIZE)        ? "resize"
            : (config.enMode == DEMO_MODE_CONVERT) ? "convert"
            : (config.enMode == DEMO_MODE_ROTATE)  ? "rotate"
                                                    : "adv2layers");
    DEMO_LOG(
        "Configuration: src=%ux%u(%s) dst=%ux%u(%s)",
        config.src_width,
        config.src_height,
        demo_pixel_format_name(config.src_format),
        config.dst_width,
        config.dst_height,
        demo_pixel_format_name(config.dst_format));
    DEMO_LOG("Input: %s", config.input_file);
    if (config.enMode == DEMO_MODE_ADV2LAYERS) {
        DEMO_LOG("Overlay: %s", config.overlay_file);
    }
    DEMO_LOG("Output: %s", config.output_file);

    ret = SYS_Init();
    if (ret != 0) {
        DEMO_FAIL("SYS_Init failed, ret=%d", ret);
    }

    ret = VB_Init();
    if (ret != 0) {
        SYS_Exit();
        DEMO_FAIL("VB_Init failed, ret=%d", ret);
    }

    if (demo_prepare_pool(&src_pool, &src_frame, MPP_ID_V2D, config.src_format, config.src_width, config.src_height) !=
        0) {
        goto EXIT;
    }
    if (config.enMode == DEMO_MODE_ADV2LAYERS) {
        if (demo_prepare_pool(
                &overlay_pool, &overlay_frame, MPP_ID_V2D,
                config.overlay_format, config.src_width, config.src_height) != 0) {
            goto EXIT;
        }
    }
    if (demo_prepare_pool(&dst_pool, &dst_frame, MPP_ID_V2D, config.dst_format, config.dst_width, config.dst_height) !=
        0) {
        goto EXIT;
    }

    if (demo_is_yuv420sp_format(config.src_format)) {
        ret = demo_load_yuv420sp_file(&src_frame, config.input_file);
        if (ret != 0) {
            DEMO_LOG("load yuv file failed: %s", config.input_file);
            goto EXIT;
        }
    } else {
        ret = demo_load_packed_file(&src_frame, config.input_file);
        if (ret != 0) {
            DEMO_LOG("load packed file failed: %s", config.input_file);
            goto EXIT;
        }
    }

    if (config.enMode == DEMO_MODE_ADV2LAYERS) {
        ret = demo_load_packed_file(&overlay_frame, config.overlay_file);
        if (ret != 0) {
            DEMO_LOG("load overlay file failed: %s", config.overlay_file);
            goto EXIT;
        }
    }

    if (config.enMode == DEMO_MODE_RESIZE) {
        ret = demo_run_resize(&config, &src_frame, &dst_frame);
        if (ret != 0) {
            DEMO_LOG("demo_run_resize failed, ret=%d", ret);
            goto EXIT;
        }
    } else if (config.enMode == DEMO_MODE_CONVERT) {
        ret = demo_run_convert(&config, &src_frame, &dst_frame);
        if (ret != 0) {
            DEMO_LOG("demo_run_convert failed, ret=%d", ret);
            goto EXIT;
        }
    } else if (config.enMode == DEMO_MODE_ROTATE) {
        ret = demo_run_rotate(&config, &src_frame, &dst_frame);
        if (ret != 0) {
            DEMO_LOG("demo_run_rotate failed, ret=%d", ret);
            goto EXIT;
        }
    } else {
        ret = demo_run_adv2layers(&config, &src_frame, &overlay_frame, &dst_frame);
        if (ret != 0) {
            DEMO_LOG("demo_run_adv2layers failed, ret=%d", ret);
            goto EXIT;
        }
    }

    ret = demo_dump_frame_file_as(&dst_frame, config.output_file);
    if (ret != 0) {
        DEMO_LOG("dump output file failed: %s", config.output_file);
        goto EXIT;
    }

    DEMO_LOG(
        "V2D %s experiment finished",
        (config.enMode == DEMO_MODE_RESIZE)        ? "resize"
            : (config.enMode == DEMO_MODE_CONVERT) ? "convert"
            : (config.enMode == DEMO_MODE_ROTATE)  ? "rotate"
                                                    : "adv2layers");
    DEMO_LOG("input file: %s", config.input_file);
    DEMO_LOG("output file: %s", config.output_file);
    DEMO_LOG(
        "src=%ux%u(%s) dst=%ux%u(%s)",
        config.src_width,
        config.src_height,
        demo_pixel_format_name(config.src_format),
        config.dst_width,
        config.dst_height,
        demo_pixel_format_name(config.dst_format));
    DEMO_LOG("src fd=%d dst fd=%d", (int)src_frame.stVFrame.u32Fd[0], (int)dst_frame.stVFrame.u32Fd[0]);

EXIT:
    if (src_frame.ulBufferId != 0UL) {
        VB_ReleaseBuffer(src_frame.ulBufferId);
    }
    if (dst_frame.ulBufferId != 0UL) {
        VB_ReleaseBuffer(dst_frame.ulBufferId);
    }
    if (overlay_frame.ulBufferId != 0UL) {
        VB_ReleaseBuffer(overlay_frame.ulBufferId);
    }
    if (src_pool != 0UL) {
        VB_DestroyPool(src_pool);
    }
    if (overlay_pool != 0UL) {
        VB_DestroyPool(overlay_pool);
    }
    if (dst_pool != 0UL) {
        VB_DestroyPool(dst_pool);
    }
    VB_Exit();
    SYS_Exit();
    return 0;
}
