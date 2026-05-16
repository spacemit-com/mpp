/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
*
* @File      :    v2d_draw_demo.c
* @Brief     :    Example: load one NV12 frame from file, create one V2D job,
*                 draw a rectangle, then submit by EndJob and dump result.
*
* Notes:
*   1. This demo shows the standard BeginJob -> Draw -> EndJob flow.
*   2. Current V2D_DrawLine implementation still uses CPU backend internally,
*      but external usage is unified as V2D job mode.
*   3. Current V2D_DrawLine implementation only supports NV12/NV21 and
*      line_width == 1, so this demo uses NV12 input/output.
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

#define DEMO_INPUT_FILE   "./input_nv12.yuv"
#define DEMO_OUTPUT_FILE  "./v2d_draw_out.yuv"
#define DEMO_WIDTH        1920U
#define DEMO_HEIGHT       1080U

#define DEMO_LOG(fmt, ...)  printf("[v2d_draw_demo] " fmt "\n", ## __VA_ARGS__)
#define DEMO_FAIL(fmt, ...) do { printf("[v2d_draw_demo][FAIL] " fmt "\n", ## __VA_ARGS__); return -1; } while (0)

typedef struct DEMO_CONFIG_S {
    const char *input_file;
    const char *output_file;
    U32 width;
    U32 height;
    U32 rect_x;
    U32 rect_y;
    U32 rect_w;
    U32 rect_h;
    U32 yuv_color;
} DEMO_CONFIG_S;

static void demo_print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s [input_file width height output_file rect_x rect_y rect_w rect_h [yuv_hex]]\n", prog);
    printf("\nDefaults:\n");
    printf("  input_file : %s\n", DEMO_INPUT_FILE);
    printf("  width      : %u\n", DEMO_WIDTH);
    printf("  height     : %u\n", DEMO_HEIGHT);
    printf("  output_file: %s\n", DEMO_OUTPUT_FILE);
    printf("  rect_x     : %u\n", DEMO_WIDTH / 4U);
    printf("  rect_y     : %u\n", DEMO_HEIGHT / 4U);
    printf("  rect_w     : %u\n", DEMO_WIDTH / 2U);
    printf("  rect_h     : %u\n", DEMO_HEIGHT / 2U);
    printf("  yuv_hex    : 0x00VVUUYY, e.g. 0x00FF4C52\n");
    printf("\nExample:\n");
    printf("  %s in.yuv 1920 1080 out.yuv 100 100 400 300 0x00FF4C52\n", prog);
}

static int demo_parse_u32(const char *text, U32 *value)
{
    char *end_ptr = NULL;
    unsigned long parsed;

    if ((text == NULL) || (value == NULL) || (text[0] == '\0')) {
        return -1;
    }

    parsed = strtoul(text, &end_ptr, 0);
    if ((*end_ptr != '\0') || (parsed > 0xffffffffUL)) {
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
    config->input_file = DEMO_INPUT_FILE;
    config->output_file = DEMO_OUTPUT_FILE;
    config->width = DEMO_WIDTH;
    config->height = DEMO_HEIGHT;
    config->rect_x = DEMO_WIDTH / 4U;
    config->rect_y = DEMO_HEIGHT / 4U;
    config->rect_w = DEMO_WIDTH / 2U;
    config->rect_h = DEMO_HEIGHT / 2U;
    config->yuv_color = 0x00FF4C52U;
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

    if ((argc != 9) && (argc != 10)) {
        return -1;
    }

    config->input_file = argv[1];
    config->output_file = argv[4];

    if ((demo_parse_u32(argv[2], &config->width) != 0) ||
        (demo_parse_u32(argv[3], &config->height) != 0) ||
        (demo_parse_u32(argv[5], &config->rect_x) != 0) ||
        (demo_parse_u32(argv[6], &config->rect_y) != 0) ||
        (demo_parse_u32(argv[7], &config->rect_w) != 0) ||
        (demo_parse_u32(argv[8], &config->rect_h) != 0)) {
        return -1;
    }

    if (argc == 10) {
        if (demo_parse_u32(argv[9], &config->yuv_color) != 0) {
            return -1;
        }
    }

    if ((config->width == 0U) || (config->height == 0U) ||
        (config->rect_w == 0U) || (config->rect_h == 0U)) {
        return -1;
    }

    if ((config->rect_x >= config->width) || (config->rect_y >= config->height)) {
        return -1;
    }

    if ((config->rect_x + config->rect_w) > config->width ||
        (config->rect_y + config->rect_h) > config->height) {
        return -1;
    }

    return 0;
}

static int demo_prepare_nv12_pool(UL *pool_id, U32 width, U32 height)
{
    VbPoolCfg cfg;
    VideoFrameInfo frame_info;
    U32 stride;
    U32 total_size;
    S32 ret;

    if (pool_id == NULL) {
        return -1;
    }

    stride = width;
    total_size = (stride * height * 3U) / 2U;

    memset(&cfg, 0, sizeof(cfg));
    cfg.u32BufSize = total_size;
    cfg.u32BufCnt = 2;
    cfg.eModId = MPP_ID_SYS;
    cfg.eRemapMode = VBUF_REMAP_MODE_NOCACHE;

    *pool_id = VB_CreatePool(&cfg);
    if (*pool_id == 0UL) {
        DEMO_LOG("VB_CreatePool failed");
        return -1;
    }

    memset(&frame_info, 0, sizeof(frame_info));
    frame_info.eFrameType = FRAME_TYPE_COMMON;
    frame_info.eModId = MPP_ID_SYS;
    frame_info.stCommFrameInfo.u32Width = width;
    frame_info.stCommFrameInfo.u32Height = height;
    frame_info.stCommFrameInfo.u32Align = 1U;
    frame_info.stCommFrameInfo.ePixelFormat = MPP_PIXEL_FORMAT_NV12;
    frame_info.stCommFrameInfo.eCompressMode = COMPRESS_MODE_NONE;
    frame_info.stCommFrameInfo.eColorSpace = COLOR_SPACE_BT601;
    frame_info.stVFrame.u32PlaneNum = 2U;
    frame_info.stVFrame.u32PlaneStride[0] = stride;
    frame_info.stVFrame.u32PlaneStride[1] = stride;
    frame_info.stVFrame.u32PlaneSize[0] = stride * height;
    frame_info.stVFrame.u32PlaneSize[1] = (stride * height) / 2U;
    frame_info.stVFrame.u32PlaneSizeValid[0] = frame_info.stVFrame.u32PlaneSize[0];
    frame_info.stVFrame.u32PlaneSizeValid[1] = frame_info.stVFrame.u32PlaneSize[1];
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

    return 0;
}

static int demo_load_nv12_file(VideoFrameInfo *frame, const char *input_file)
{
    FILE *fp;
    size_t read_size;
    size_t expected_size;

    if ((frame == NULL) || (input_file == NULL) || (frame->stVFrame.ulPlaneVirAddr[0] == 0UL)) {
        return -1;
    }

    expected_size = frame->stVFrame.u32PlaneSize[0] + frame->stVFrame.u32PlaneSize[1];

    fp = fopen(input_file, "rb");
    if (fp == NULL) {
        DEMO_LOG("open input file failed: %s", input_file);
        return -1;
    }

    read_size = fread((void *)frame->stVFrame.ulPlaneVirAddr[0], 1, expected_size, fp);
    fclose(fp);

    if (read_size != expected_size) {
        DEMO_LOG("read input file size mismatch, got=%zu expect=%zu", read_size, expected_size);
        return -1;
    }

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

    write_size = fwrite((const void *)frame->stVFrame.ulPlaneVirAddr[0], 1, expected_size, fp);
    fclose(fp);

    if (write_size != expected_size) {
        DEMO_LOG("write output file size mismatch, got=%zu expect=%zu", write_size, expected_size);
        return -1;
    }

    return 0;
}

static int demo_draw_rect(VideoFrameInfo *frame, const DEMO_CONFIG_S *config)
{
    V2DHandle handle = 0;
    V2DArea rect;
    V2DFillColor color;
    int ret;

    if ((frame == NULL) || (config == NULL)) {
        return -1;
    }

    memset(&rect, 0, sizeof(rect));
    rect.u16X = (uint16_t)config->rect_x;
    rect.u16Y = (uint16_t)config->rect_y;
    rect.u16W = (uint16_t)config->rect_w;
    rect.u16H = (uint16_t)config->rect_h;

    memset(&color, 0, sizeof(color));
    color.u32ColorValue = config->yuv_color;
    color.enFormat = V2D_COLOR_FORMAT_NV12;

    ret = V2D_BeginJob(&handle);
    if (ret != 0) {
        return -1;
    }

    ret = V2D_DrawRect(handle, frame, &rect, &color, 1U);
    if (ret != 0) {
        V2D_CancelJob(handle);
        return -1;
    }

    ret = V2D_EndJob(handle);
    if (ret != 0) {
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    S32 ret;
    UL pool = 0UL;
    UL buffer = 0UL;
    VideoFrameInfo frame;
    DEMO_CONFIG_S config;

    memset(&frame, 0, sizeof(frame));

    if (demo_parse_args(argc, argv, &config) != 0) {
        demo_print_usage(argv[0]);
        return -1;
    }

    ret = SYS_Init();
    if (ret != 0) {
        DEMO_FAIL("SYS_Init failed, ret=%d", ret);
    }

    ret = VB_Init();
    if (ret != 0) {
        SYS_Exit();
        DEMO_FAIL("VB_Init failed, ret=%d", ret);
    }

    if (demo_prepare_nv12_pool(&pool, config.width, config.height) != 0) {
        goto EXIT;
    }

    buffer = VB_GetBuffer(pool, 0);
    if (buffer == 0UL) {
        DEMO_LOG("VB_GetBuffer failed");
        goto EXIT;
    }

    if (demo_prepare_frame_from_buffer(buffer, &frame) != 0) {
        DEMO_LOG("prepare frame from buffer failed");
        goto EXIT;
    }

    if (frame.stVFrame.u32PlaneNum > 1U) {
        frame.stVFrame.u32Fd[1] = frame.stVFrame.u32Fd[0];
        frame.stVFrame.ulPlaneVirAddr[1] =
            frame.stVFrame.ulPlaneVirAddr[0] + frame.stVFrame.u32PlaneSize[0];
    }

    if (demo_load_nv12_file(&frame, config.input_file) != 0) {
        goto EXIT;
    }

    if (demo_draw_rect(&frame, &config) != 0) {
        DEMO_LOG("draw rectangle failed");
        goto EXIT;
    }

    if (demo_dump_nv12_file(&frame, config.output_file) != 0) {
        goto EXIT;
    }

    DEMO_LOG("draw done: input=%s output=%s rect=(%u,%u,%u,%u) yuv=0x%08x",
        config.input_file,
        config.output_file,
        config.rect_x,
        config.rect_y,
        config.rect_w,
        config.rect_h,
        config.yuv_color);

    VB_ReleaseBuffer(buffer);
    VB_DestroyPool(pool);
    VB_Exit();
    SYS_Exit();
    return 0;

EXIT:
    if (buffer != 0UL) {
        VB_ReleaseBuffer(buffer);
    }
    if (pool != 0UL) {
        VB_DestroyPool(pool);
    }
    VB_Exit();
    SYS_Exit();
    return -1;
}
