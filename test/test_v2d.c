/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
*
* @File      :    v2d_vb_demo.c
* @Brief     :    Example: allocate source and destination buffers from VB,
*                 then run a V2D scale or format-convert job selected by CLI.
*
* Notes:
*   1. This is a demo executable, not a CTest case, because it depends on
*      `/dev/v2d_dev` and real V2D hardware.
*   2. Source buffer uses NV12 (`./vi_phy0_first_frame.yuv`) and destination
*      buffer also uses NV12.
*   3. The example intentionally uses VB_SetFrameInfo + VB_GetFrameInfo so the
*      V2D helper can obtain fd/stride/size metadata from VB.
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

#define DEMO_INPUT_FILE          "./vi_phy0_first_frame.yuv"
#define DEMO_INPUT_FILE_OVERLAY  "./v2d_overlay.bgra"
#define DEMO_OUTPUT_FILE_SCALE   "./v2d_case1_scale_out.yuv"
#define DEMO_OUTPUT_FILE_CONVERT "./v2d_case1_convert_out.bgra"
#define DEMO_OUTPUT_FILE_ROTATE  "./v2d_case1_rotate_out.yuv"
#define DEMO_OUTPUT_FILE_BLEND   "./v2d_case1_adv2layers_out.yuv"
#define DEFAULT_SRC_WIDTH   1920U
#define DEFAULT_SRC_HEIGHT  1080U
#define DEFAULT_DST_WIDTH   640U
#define DEFAULT_DST_HEIGHT  480U

typedef enum DEMO_MODE_E {
    DEMO_MODE_SCALE = 0,
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
    V2DArea foreground_area;
    MppPixelFormat dst_format;
} DEMO_CONFIG_S;

#define DEMO_LOG(fmt, ...)  printf("[v2d_vb_demo] " fmt "\n", ## __VA_ARGS__)
#define DEMO_FAIL(fmt, ...) do { printf("[v2d_vb_demo][FAIL] " fmt "\n", ## __VA_ARGS__); return -1; } while (0)

static void demo_print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s scale [input_file src_width src_height output_file dst_width dst_height]\n", prog);
    printf("  %s convert [input_file src_width src_height output_file]\n", prog);
    printf("  %s rotate [input_file src_width src_height output_file]\n", prog);
    printf("  %s adv2layers [background_nv12 foreground_bgra width height output_file [fg_x fg_y fg_w fg_h]]\n", prog);
    printf("\nscale defaults:\n");
    printf("  input_file  : %s\n", DEMO_INPUT_FILE);
    printf("  src_width   : %u\n", DEFAULT_SRC_WIDTH);
    printf("  src_height  : %u\n", DEFAULT_SRC_HEIGHT);
    printf("  output_file : %s\n", DEMO_OUTPUT_FILE_SCALE);
    printf("  dst_width   : %u\n", DEFAULT_DST_WIDTH);
    printf("  dst_height  : %u\n", DEFAULT_DST_HEIGHT);
    printf("\nconvert defaults:\n");
    printf("  input_file  : %s\n", DEMO_INPUT_FILE);
    printf("  src_width   : %u\n", DEFAULT_SRC_WIDTH);
    printf("  src_height  : %u\n", DEFAULT_SRC_HEIGHT);
    printf("  output_file : %s\n", DEMO_OUTPUT_FILE_CONVERT);
    printf("\nrotate defaults:\n");
    printf("  input_file  : %s\n", DEMO_INPUT_FILE);
    printf("  src_width   : %u\n", DEFAULT_SRC_WIDTH);
    printf("  src_height  : %u\n", DEFAULT_SRC_HEIGHT);
    printf("  output_file : %s\n", DEMO_OUTPUT_FILE_ROTATE);
    printf("\nadv2layers defaults:\n");
    printf("  background  : %s\n", DEMO_INPUT_FILE);
    printf("  foreground  : %s\n", DEMO_INPUT_FILE_OVERLAY);
    printf("  width       : %u\n", DEFAULT_SRC_WIDTH);
    printf("  height      : %u\n", DEFAULT_SRC_HEIGHT);
    printf("  output_file : %s\n", DEMO_OUTPUT_FILE_BLEND);
    printf("  fg_x        : %d\n", (int)(DEFAULT_SRC_WIDTH / 4U));
    printf("  fg_y        : %d\n", (int)(DEFAULT_SRC_HEIGHT / 4U));
    printf("  fg_w        : %u\n", DEFAULT_SRC_WIDTH / 2U);
    printf("  fg_h        : %u\n", DEFAULT_SRC_HEIGHT / 2U);
    printf("\nExample:\n");
    printf("  %s scale input.yuv 1920 1080 output.yuv 640 480\n", prog);
    printf("  %s convert input.yuv 1920 1080 output.bgra\n", prog);
    printf("  %s rotate input.yuv 1920 1080 output.yuv\n", prog);
    printf("  %s adv2layers bg.yuv fg.bgra 1920 1080 output.yuv\n", prog);
    printf("  %s adv2layers bg.yuv fg.bgra 1920 1080 output.yuv 100 200 320 180\n", prog);
}

static int demo_parse_mode(const char *mode_str, DEMO_MODE_E *mode)
{
    if (mode_str == NULL || mode == NULL) {
        return -1;
    }

    if (strcmp(mode_str, "scale") == 0) {
        *mode = DEMO_MODE_SCALE;
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

static int demo_parse_u32(const char *text, U32 *value)
{
    char *end_ptr = NULL;
    unsigned long parsed;

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

static void demo_set_default_config(DEMO_CONFIG_S *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->enMode = DEMO_MODE_SCALE;
    config->input_file = DEMO_INPUT_FILE;
    config->overlay_file = DEMO_INPUT_FILE_OVERLAY;
    config->output_file = DEMO_OUTPUT_FILE_SCALE;
    config->src_width = DEFAULT_SRC_WIDTH;
    config->src_height = DEFAULT_SRC_HEIGHT;
    config->dst_width = DEFAULT_DST_WIDTH;
    config->dst_height = DEFAULT_DST_HEIGHT;
    config->foreground_area.u16X = (U16)(DEFAULT_SRC_WIDTH / 4U);
    config->foreground_area.u16Y = (U16)(DEFAULT_SRC_HEIGHT / 4U);
    config->foreground_area.u16W = DEFAULT_SRC_WIDTH / 2U;
    config->foreground_area.u16H = DEFAULT_SRC_HEIGHT / 2U;
    config->dst_format = MPP_PIXEL_FORMAT_NV12;
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

    if (demo_parse_mode(argv[1], &config->enMode) != 0) {
        return -1;
    }

    if (config->enMode == DEMO_MODE_SCALE) {
        config->output_file = DEMO_OUTPUT_FILE_SCALE;
        config->dst_format = MPP_PIXEL_FORMAT_NV12;

        if (argc == 2) {
            return 0;
        }

        if (argc != 8) {
            return -1;
        }

        config->input_file = argv[2];
        config->output_file = argv[5];
        if (demo_parse_u32(argv[3], &config->src_width) != 0 ||
            demo_parse_u32(argv[4], &config->src_height) != 0 ||
            demo_parse_u32(argv[6], &config->dst_width) != 0 ||
            demo_parse_u32(argv[7], &config->dst_height) != 0) {
            return -1;
        }

        return 0;
    }

    if (config->enMode == DEMO_MODE_ADV2LAYERS) {
        U32 fg_x;
        U32 fg_y;
        U32 fg_w;
        U32 fg_h;

        config->overlay_file = DEMO_INPUT_FILE_OVERLAY;
        config->output_file = DEMO_OUTPUT_FILE_BLEND;
        config->dst_format = MPP_PIXEL_FORMAT_NV12;
        config->dst_width = config->src_width;
        config->dst_height = config->src_height;

        if (argc == 2) {
            return 0;
        }

        if ((argc != 7) && (argc != 11)) {
            return -1;
        }

        config->input_file = argv[2];
        config->overlay_file = argv[3];
        config->output_file = argv[6];
        if (demo_parse_u32(argv[4], &config->src_width) != 0 ||
            demo_parse_u32(argv[5], &config->src_height) != 0) {
            return -1;
        }

        config->dst_width = config->src_width;
        config->dst_height = config->src_height;
        config->foreground_area.u16X = (U16)(config->src_width / 4U);
        config->foreground_area.u16Y = (U16)(config->src_height / 4U);
        config->foreground_area.u16W = (U16)(config->src_width / 2U);
        config->foreground_area.u16H = (U16)(config->src_height / 2U);

        if (argc == 11) {
            if (demo_parse_u32(argv[7], &fg_x) != 0 ||
                demo_parse_u32(argv[8], &fg_y) != 0 ||
                demo_parse_u32(argv[9], &fg_w) != 0 ||
                demo_parse_u32(argv[10], &fg_h) != 0) {
                return -1;
            }

            config->foreground_area.u16X = (U16)fg_x;
            config->foreground_area.u16Y = (U16)fg_y;
            config->foreground_area.u16W = (U16)fg_w;
            config->foreground_area.u16H = (U16)fg_h;
        }

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

        if (argc != 6) {
            return -1;
        }

        config->input_file = argv[2];
        config->output_file = argv[5];
        if (demo_parse_u32(argv[3], &config->src_width) != 0 ||
            demo_parse_u32(argv[4], &config->src_height) != 0) {
            return -1;
        }

        config->dst_width = config->src_height;
        config->dst_height = config->src_width;
        return 0;
    }

    config->output_file = DEMO_OUTPUT_FILE_CONVERT;
    config->dst_format = MPP_PIXEL_FORMAT_BGRA;

    if (argc == 2) {
        config->dst_width = config->src_width;
        config->dst_height = config->src_height;
        return 0;
    }

    if (argc != 6) {
        return -1;
    }

    config->input_file = argv[2];
    config->output_file = argv[5];
    if (demo_parse_u32(argv[3], &config->src_width) != 0 ||
        demo_parse_u32(argv[4], &config->src_height) != 0) {
        return -1;
    }

    config->dst_width = config->src_width;
    config->dst_height = config->src_height;
    return 0;
}

static int demo_load_nv12_file(VideoFrameInfo *frame, const char *input_file)
{
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
    expected_size = (size_t)frame->stVFrame.u32PlaneSize[0] +
        (size_t)frame->stVFrame.u32PlaneSize[1];

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
        memcpy(plane1,
            base + frame->stVFrame.u32PlaneSize[0],
            frame->stVFrame.u32PlaneSize[1]);
    }

    return 0;
}

static int demo_load_packed_file(VideoFrameInfo *frame, const char *input_file)
{
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

static int demo_dump_nv12_file_as(const VideoFrameInfo *frame, const char *file_name)
{
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

    write_size = fwrite(plane0,
        1,
        frame->stVFrame.u32PlaneSize[0] + frame->stVFrame.u32PlaneSize[1],
        fp);
    fclose(fp);

    if (write_size != frame->stVFrame.u32PlaneSize[0] + frame->stVFrame.u32PlaneSize[1]) {
        DEMO_LOG("write output file size mismatch, got=%zu expect=%u",
            write_size,
            frame->stVFrame.u32PlaneSize[0] + frame->stVFrame.u32PlaneSize[1]);
        return -1;
    }

    return 0;
}

static int demo_dump_frame_file_as(const VideoFrameInfo *frame, const char *file_name)
{
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

static void demo_reset_nv12_frame(VideoFrameInfo *frame)
{
    void *plane0;
    void *plane1;

    if (frame == NULL){
        return;
    }

    plane0 = (void *)frame->stVFrame.ulPlaneVirAddr[0];
    plane1 = (void *)frame->stVFrame.ulPlaneVirAddr[1];

    if (plane0 != NULL){
        memset(plane0, 0, frame->stVFrame.u32PlaneSize[0]);
    }
    if (plane1 != NULL){
        memset(plane1, 0x80, frame->stVFrame.u32PlaneSize[1]);
    }
}

static int demo_prepare_pool(UL *pool_id, ModId mod_id, MppPixelFormat pixel_format,
    U32 width, U32 height)
{
    VbPoolCfg cfg;
    VideoFrameInfo frame_info;
    S32 ret;
    U32 stride;
    U32 y_size;
    U32 total_size;

    if (pixel_format == MPP_PIXEL_FORMAT_NV12) {
        stride = width;
        y_size = stride * height;
        total_size = y_size + (y_size / 2U);
    } else if (pixel_format == MPP_PIXEL_FORMAT_BGRA) {
        stride = width * 4U;
        y_size = stride * height;
        total_size = y_size;
    } else {
        DEMO_LOG("unsupported format in demo, format=%d", pixel_format);
        return -1;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.u32BufSize = total_size;
    cfg.u32BufCnt = 2;
    cfg.eModId = mod_id;
    cfg.eRemapMode = VBUF_REMAP_MODE_NOCACHE;

    *pool_id = VB_CreatePool(&cfg);
    if (*pool_id == 0UL) {
        DEMO_LOG("VB_CreatePool failed, mod=%d format=%d", mod_id, pixel_format);
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
    frame_info.stCommFrameInfo.eColorSpace = COLOR_SPACE_BT601;

    frame_info.stVFrame.u32PlaneNum = (pixel_format == MPP_PIXEL_FORMAT_NV12) ? 2U : 1U;
    frame_info.stVFrame.u32PlaneStride[0] = stride;
    frame_info.stVFrame.u32PlaneStride[1] = (pixel_format == MPP_PIXEL_FORMAT_NV12) ? stride : 0U;
    frame_info.stVFrame.u32PlaneSize[0] = y_size;
    frame_info.stVFrame.u32PlaneSize[1] = (pixel_format == MPP_PIXEL_FORMAT_NV12) ? (y_size / 2U) : 0U;
    frame_info.stVFrame.u32PlaneSizeValid[0] = y_size;
    frame_info.stVFrame.u32PlaneSizeValid[1] = (pixel_format == MPP_PIXEL_FORMAT_NV12) ? (y_size / 2U) : 0U;
    frame_info.stVFrame.u32TotalSize = total_size;

    ret = VB_SetFrameInfo(*pool_id, &frame_info);
    if (ret != 0) {
        DEMO_LOG("VB_SetFrameInfo failed, ret=%d", ret);
        VB_DestroyPool(*pool_id);
        *pool_id = 0UL;
        return -1;
    }

    return 0;
}

static int demo_prepare_frame_from_buffer(UL buffer, VideoFrameInfo *frame)
{
    S32 ret;

    if (buffer == 0UL || frame == NULL) {
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

static int demo_run_scale(const DEMO_CONFIG_S *config,
    const VideoFrameInfo *src_frame,
    VideoFrameInfo *dst_frame)
{
    V2DHandle handle = 0;
    S32 ret;

    if (config == NULL || src_frame == NULL || dst_frame == NULL) {
        return -1;
    }

    demo_reset_nv12_frame(dst_frame);

    ret = V2D_BeginJob(&handle);
    if (ret != 0) {
        return ret;
    }

    ret = V2D_ScaleFrame(handle, src_frame, dst_frame, V2D_CSC_MODE_BUTT);
    if (ret != 0) {
        V2D_CancelJob(handle);
        return ret;
    }

    ret = V2D_EndJob(handle);
    if (ret != 0) {
        return ret;
    }

    dst_frame->stVFrame.u32PlaneSizeValid[0] = dst_frame->stVFrame.u32PlaneSize[0];
    if (dst_frame->stVFrame.u32PlaneNum > 1U) {
        dst_frame->stVFrame.u32PlaneSizeValid[1] = dst_frame->stVFrame.u32PlaneSize[1];
    }

    return 0;
}

static int demo_run_convert(const DEMO_CONFIG_S *config,
    const VideoFrameInfo *src_frame,
    VideoFrameInfo *dst_frame)
{
    void *plane0;
    V2DHandle handle = 0;
    S32 ret;
    V2DCscMode csc_mode;

    if (src_frame == NULL || dst_frame == NULL) {
        return -1;
    }

    plane0 = (void *)dst_frame->stVFrame.ulPlaneVirAddr[0];
    if (plane0 == NULL) {
        return -1;
    }

    memset(plane0, 0, dst_frame->stVFrame.u32PlaneSize[0]);

    csc_mode = V2D_CSC_MODE_BT601WIDE_2_RGB;
    if ((config != NULL) && (src_frame->stCommFrameInfo.eColorSpace == COLOR_SPACE_BT601_LIMIT)) {
        csc_mode = V2D_CSC_MODE_BT601NARROW_2_RGB;
    }

    ret = V2D_BeginJob(&handle);
    if (ret != 0) {
        return ret;
    }

    ret = V2D_ConvertFrame(handle, src_frame, dst_frame, csc_mode);
    if (ret != 0) {
        V2D_CancelJob(handle);
        return ret;
    }

    ret = V2D_EndJob(handle);
    if (ret != 0) {
        return ret;
    }

    dst_frame->stVFrame.u32PlaneSizeValid[0] = dst_frame->stVFrame.u32PlaneSize[0];
    if (dst_frame->stVFrame.u32PlaneNum > 1U) {
        dst_frame->stVFrame.u32PlaneSizeValid[1] = dst_frame->stVFrame.u32PlaneSize[1];
    }

    return 0;
}

static int demo_run_adv2layers(const DEMO_CONFIG_S *config,
    const VideoFrameInfo *background_frame,
    const VideoFrameInfo *foreground_frame,
    VideoFrameInfo *dst_frame)
{
    V2DHandle handle = 0;
    S32 ret;

    if (config == NULL || background_frame == NULL || foreground_frame == NULL || dst_frame == NULL) {
        return -1;
    }

    demo_reset_nv12_frame(dst_frame);

    ret = V2D_BeginJob(&handle);
    if (ret != 0) {
        return ret;
    }

    ret = V2D_Adv2Layers(handle, background_frame, foreground_frame, &config->foreground_area, dst_frame);
    if (ret != 0) {
        V2D_CancelJob(handle);
        return ret;
    }

    ret = V2D_EndJob(handle);
    if (ret != 0) {
        return ret;
    }

    dst_frame->stVFrame.u32PlaneSizeValid[0] = dst_frame->stVFrame.u32PlaneSize[0];
    if (dst_frame->stVFrame.u32PlaneNum > 1U) {
        dst_frame->stVFrame.u32PlaneSizeValid[1] = dst_frame->stVFrame.u32PlaneSize[1];
    }

    return 0;
}

static int demo_run_rotate(const DEMO_CONFIG_S *config,
    const VideoFrameInfo *src_frame,
    VideoFrameInfo *dst_frame)
{
    V2DHandle handle = 0;
    S32 ret;

    if (config == NULL || src_frame == NULL || dst_frame == NULL) {
        return -1;
    }

    demo_reset_nv12_frame(dst_frame);

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

    dst_frame->stVFrame.u32PlaneSizeValid[0] = dst_frame->stVFrame.u32PlaneSize[0];
    if (dst_frame->stVFrame.u32PlaneNum > 1U) {
        dst_frame->stVFrame.u32PlaneSizeValid[1] = dst_frame->stVFrame.u32PlaneSize[1];
    }

    return 0;
}

int main(int argc, char *argv[])
{
    S32 ret;
    DEMO_CONFIG_S config;
    UL src_pool = 0UL;
    UL overlay_pool = 0UL;
    UL dst_pool = 0UL;
    UL src_buf = 0UL;
    UL overlay_buf = 0UL;
    UL dst_buf = 0UL;
    VideoFrameInfo src_frame;
    VideoFrameInfo overlay_frame;
    VideoFrameInfo dst_frame;
    memset(&src_frame, 0, sizeof(src_frame));
    memset(&overlay_frame, 0, sizeof(overlay_frame));
    memset(&dst_frame, 0, sizeof(dst_frame));

    if (demo_parse_args(argc, argv, &config) != 0) {
        demo_print_usage(argv[0]);
        return -1;
    }

    DEMO_LOG("Mode: %s",
        (config.enMode == DEMO_MODE_SCALE) ? "scale" :
        (config.enMode == DEMO_MODE_CONVERT) ? "convert" :
        (config.enMode == DEMO_MODE_ROTATE) ? "rotate" : "adv2layers");
    DEMO_LOG("Configuration: src=%ux%u dst=%ux%u", config.src_width, config.src_height, config.dst_width, config.dst_height);
    DEMO_LOG("Input: %s", config.input_file);
    if (config.enMode == DEMO_MODE_ADV2LAYERS) {
        DEMO_LOG("Overlay: %s", config.overlay_file);
        DEMO_LOG("Foreground area: x=%d y=%d w=%u h=%u",
            config.foreground_area.u16X,
            config.foreground_area.u16Y,
            config.foreground_area.u16W,
            config.foreground_area.u16H);
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

    if (demo_prepare_pool(&src_pool, MPP_ID_SYS, MPP_PIXEL_FORMAT_NV12,
        config.src_width, config.src_height) != 0) {
        goto EXIT;
    }
    if (config.enMode == DEMO_MODE_ADV2LAYERS) {
        if (demo_prepare_pool(&overlay_pool, MPP_ID_SYS, MPP_PIXEL_FORMAT_BGRA,
            config.src_width, config.src_height) != 0) {
            goto EXIT;
        }
    }
    if (demo_prepare_pool(&dst_pool, MPP_ID_SYS, config.dst_format,
        config.dst_width, config.dst_height) != 0) {
        goto EXIT;
    }

    src_buf = VB_GetBuffer(src_pool, 0);
    if (config.enMode == DEMO_MODE_ADV2LAYERS) {
        overlay_buf = VB_GetBuffer(overlay_pool, 0);
    }
    dst_buf = VB_GetBuffer(dst_pool, 0);
    if ((src_buf == 0UL) || (dst_buf == 0UL) ||
        ((config.enMode == DEMO_MODE_ADV2LAYERS) && (overlay_buf == 0UL))) {
        DEMO_LOG("VB_GetBuffer failed, src=0x%lx overlay=0x%lx dst=0x%lx", src_buf, overlay_buf, dst_buf);
        goto EXIT;
    }

    ret = demo_prepare_frame_from_buffer(src_buf, &src_frame);
    if (ret != 0) {
        DEMO_LOG("prepare src frame failed");
        goto EXIT;
    }

    ret = demo_prepare_frame_from_buffer(dst_buf, &dst_frame);
    if (ret != 0) {
        DEMO_LOG("prepare dst frame failed");
        goto EXIT;
    }

    if (config.enMode == DEMO_MODE_ADV2LAYERS) {
        ret = demo_prepare_frame_from_buffer(overlay_buf, &overlay_frame);
        if (ret != 0) {
            DEMO_LOG("prepare overlay frame failed");
            goto EXIT;
        }
    }

    ret = demo_load_nv12_file(&src_frame, config.input_file);
    if (ret != 0) {
        DEMO_LOG("load NV12 file failed: %s", config.input_file);
        goto EXIT;
    }

    if (config.enMode == DEMO_MODE_ADV2LAYERS) {
        ret = demo_load_packed_file(&overlay_frame, config.overlay_file);
        if (ret != 0) {
            DEMO_LOG("load overlay file failed: %s", config.overlay_file);
            goto EXIT;
        }
    }

    if (config.enMode == DEMO_MODE_SCALE) {
        ret = demo_run_scale(&config, &src_frame, &dst_frame);
        if (ret != 0) {
            DEMO_LOG("demo_run_scale failed, ret=%d", ret);
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

    DEMO_LOG("V2D %s experiment finished",
        (config.enMode == DEMO_MODE_SCALE) ? "scale" :
        (config.enMode == DEMO_MODE_CONVERT) ? "convert" :
        (config.enMode == DEMO_MODE_ROTATE) ? "rotate" : "adv2layers");
    DEMO_LOG("input file: %s", config.input_file);
    DEMO_LOG("output file: %s", config.output_file);
    DEMO_LOG("src=%ux%u dst=%ux%u", config.src_width, config.src_height, config.dst_width, config.dst_height);
    DEMO_LOG("src fd=%d dst fd=%d", (int)src_frame.stVFrame.u32Fd[0], (int)dst_frame.stVFrame.u32Fd[0]);

EXIT:
    if (src_buf != 0UL) {
        VB_ReleaseBuffer(src_buf);
    }
    if (dst_buf != 0UL) {
        VB_ReleaseBuffer(dst_buf);
    }
    if (overlay_buf != 0UL) {
        VB_ReleaseBuffer(overlay_buf);
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
