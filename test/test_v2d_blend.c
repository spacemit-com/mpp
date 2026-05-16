/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
*
* @File      :    test_v2d_blend.c
* @Brief     :    V2D AddBlendTask demo: convert 1080P NV12 to 480P RGBA and
*                 rotate 90 degrees in one job.
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

#define DEMO_INPUT_FILE   "./vi_phy0_first_frame.yuv"
#define DEMO_OUTPUT_FILE  "./test_v2d_blend_out.rgba"
#define SRC_WIDTH         1920U
#define SRC_HEIGHT        1080U
#define DST_WIDTH         640U
#define DST_HEIGHT        480U

#define DEMO_LOG(fmt, ...)  printf("[test_v2d_blend] " fmt "\n", ## __VA_ARGS__)
#define DEMO_FAIL(fmt, ...) do { printf("[test_v2d_blend][FAIL] " fmt "\n", ## __VA_ARGS__); return -1; } while (0)

typedef struct DEMO_CONFIG_S {
    const char *input_file;
    const char *output_file;
    U32 src_width;
    U32 src_height;
    U32 dst_width;
    U32 dst_height;
} DEMO_CONFIG_S;

static void demo_set_default_config(DEMO_CONFIG_S *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->input_file = DEMO_INPUT_FILE;
    config->output_file = DEMO_OUTPUT_FILE;
    config->src_width = SRC_WIDTH;
    config->src_height = SRC_HEIGHT;
    config->dst_width = DST_WIDTH;
    config->dst_height = DST_HEIGHT;
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

static void demo_print_usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s [input_nv12 src_width src_height output_rgba dst_width dst_height]\n", prog);
    printf("\nDefaults:\n");
    printf("  input_file  : %s\n", DEMO_INPUT_FILE);
    printf("  src_width   : %u\n", SRC_WIDTH);
    printf("  src_height  : %u\n", SRC_HEIGHT);
    printf("  output_file : %s\n", DEMO_OUTPUT_FILE);
    printf("  dst_width   : %u\n", DST_WIDTH);
    printf("  dst_height  : %u\n", DST_HEIGHT);
    printf("\nBehavior:\n");
    printf("  Convert NV12 to RGBA, scale to 640x480, rotate 90 degrees via V2D_AddBlendTask.\n");
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

    config->input_file = argv[1];
    config->output_file = argv[4];
    if (demo_parse_u32(argv[2], &config->src_width) != 0 ||
        demo_parse_u32(argv[3], &config->src_height) != 0 ||
        demo_parse_u32(argv[5], &config->dst_width) != 0 ||
        demo_parse_u32(argv[6], &config->dst_height) != 0) {
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
    U32 total_size;
    U32 y_size;
    S32 ret;

    if (pool_id == NULL) {
        return -1;
    }

    memset(&cfg, 0, sizeof(cfg));
    memset(&frame_info, 0, sizeof(frame_info));

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
    } else if (pixel_format == MPP_PIXEL_FORMAT_RGBA) {
        stride = width * 4U;
        total_size = stride * height;
        frame_info.stVFrame.u32PlaneNum = 1U;
        frame_info.stVFrame.u32PlaneStride[0] = stride;
        frame_info.stVFrame.u32PlaneSize[0] = total_size;
        frame_info.stVFrame.u32PlaneSizeValid[0] = total_size;
    } else {
        return -1;
    }

    cfg.u32BufCnt = 1U;
    cfg.u32BufSize = total_size;
    cfg.eModId = mod_id;
    cfg.eRemapMode = VBUF_REMAP_MODE_NOCACHE;

    *pool_id = VB_CreatePool(&cfg);
    if (*pool_id == 0UL) {
        return -1;
    }

    frame_info.eFrameType = FRAME_TYPE_COMMON;
    frame_info.eModId = mod_id;
    frame_info.stCommFrameInfo.u32Width = width;
    frame_info.stCommFrameInfo.u32Height = height;
    frame_info.stCommFrameInfo.u32Align = 1U;
    frame_info.stCommFrameInfo.ePixelFormat = pixel_format;
    frame_info.stCommFrameInfo.eCompressMode = COMPRESS_MODE_NONE;
    frame_info.stCommFrameInfo.eColorSpace = COLOR_SPACE_BT709;
    frame_info.stVFrame.u32TotalSize = total_size;

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
        DEMO_LOG("Failed to open input file: %s", input_file);
        return -1;
    }

    read_size = fread(base, 1, expected_size, fp);
    fclose(fp);

    if (read_size != expected_size) {
        DEMO_LOG("File size mismatch: read=%zu expected=%zu", read_size, expected_size);
        return -1;
    }

    if (frame->stVFrame.ulPlaneVirAddr[1] !=
        (frame->stVFrame.ulPlaneVirAddr[0] + frame->stVFrame.u32PlaneSize[0])) {
        memcpy((void *)(uintptr_t)frame->stVFrame.ulPlaneVirAddr[1],
            base + frame->stVFrame.u32PlaneSize[0],
            frame->stVFrame.u32PlaneSize[1]);
    }

    frame->stVFrame.u32PlaneSizeValid[0] = frame->stVFrame.u32PlaneSize[0];
    frame->stVFrame.u32PlaneSizeValid[1] = frame->stVFrame.u32PlaneSize[1];
    return 0;
}

static int demo_dump_rgba_file(const VideoFrameInfo *frame, const char *output_file)
{
    FILE *fp;
    size_t write_size;
    size_t expected_size;
    const U8 *base;

    if ((frame == NULL) || (output_file == NULL) || (frame->stVFrame.ulPlaneVirAddr[0] == 0UL)) {
        return -1;
    }

    fp = fopen(output_file, "wb");
    if (fp == NULL) {
        return -1;
    }

    base = (const U8 *)(uintptr_t)frame->stVFrame.ulPlaneVirAddr[0];
    expected_size = frame->stVFrame.u32PlaneSize[0];
    write_size = fwrite(base, 1, expected_size, fp);
    fclose(fp);

    return (write_size == expected_size) ? 0 : -1;
}

int main(int argc, char *argv[])
{
    S32 ret;
    DEMO_CONFIG_S config;
    V2DHandle handle = 0;
    UL src_pool = 0UL;
    UL dst_pool = 0UL;
    UL src_buf = 0UL;
    UL dst_buf = 0UL;
    V2DArea src_rect;
    V2DArea dst_rect;
    V2DBlendConf blend_conf;
    VideoFrameInfo src_frame;
    VideoFrameInfo dst_frame;

    memset(&src_frame, 0, sizeof(src_frame));
    memset(&dst_frame, 0, sizeof(dst_frame));

    if (demo_parse_args(argc, argv, &config) != 0) {
        demo_print_usage(argv[0]);
        return -1;
    }

    DEMO_LOG("Input: %s", config.input_file);
    DEMO_LOG("Output: %s", config.output_file);
    DEMO_LOG("Source: %ux%u NV12", config.src_width, config.src_height);
    DEMO_LOG("Destination: %ux%u RGBA, rotate=90", config.dst_width, config.dst_height);

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

    if (demo_prepare_pool(&dst_pool, MPP_ID_SYS, MPP_PIXEL_FORMAT_RGBA,
        config.dst_width, config.dst_height) != 0) {
        goto EXIT;
    }

    src_buf = VB_GetBuffer(src_pool, 0);
    dst_buf = VB_GetBuffer(dst_pool, 0);
    if ((src_buf == 0UL) || (dst_buf == 0UL)) {
        DEMO_LOG("VB_GetBuffer failed, src=0x%lx dst=0x%lx", src_buf, dst_buf);
        goto EXIT;
    }

    if (demo_prepare_frame_from_buffer(src_buf, &src_frame) != 0) {
        DEMO_LOG("prepare src frame failed");
        goto EXIT;
    }

    if (demo_prepare_frame_from_buffer(dst_buf, &dst_frame) != 0) {
        DEMO_LOG("prepare dst frame failed");
        goto EXIT;
    }

    if (demo_load_nv12_file(&src_frame, config.input_file) != 0) {
        DEMO_LOG("load NV12 file failed: %s", config.input_file);
        goto EXIT;
    }

    memset((void *)(uintptr_t)dst_frame.stVFrame.ulPlaneVirAddr[0], 0, dst_frame.stVFrame.u32PlaneSize[0]);

    memset(&src_rect, 0, sizeof(src_rect));
    src_rect.u16X = 0;
    src_rect.u16Y = 0;
    src_rect.u16W = (U16)config.src_width;
    src_rect.u16H = (U16)config.src_height;

    memset(&dst_rect, 0, sizeof(dst_rect));
    dst_rect.u16X = 0;
    dst_rect.u16Y = 0;
    dst_rect.u16W = (U16)config.dst_width;
    dst_rect.u16H = (U16)config.dst_height;

    memset(&blend_conf, 0, sizeof(blend_conf));
    blend_conf.stBlendLayer[0].stBlendArea = dst_rect;

    ret = V2D_BeginJob(&handle);
    if (ret == 0) {
        ret = V2D_AddBlendTask(handle,
            &src_frame,
            &src_rect,
            NULL,
            NULL,
            NULL,
            NULL,
            &dst_frame,
            &dst_rect,
            &blend_conf,
            V2D_ROT_0,
            V2D_ROT_90,
            V2D_CSC_MODE_BUTT,
            V2D_CSC_MODE_BT709NARROW_2_RGB,
            NULL,
            V2D_NO_DITHER);
        if (ret != 0) {
            V2D_CancelJob(handle);
        } else {
            ret = V2D_EndJob(handle);
        }
    }
    if (ret != 0) {
        DEMO_LOG("V2D_AddBlendTask failed, ret=%d", ret);
        goto EXIT;
    }

    dst_frame.stVFrame.u32PlaneSizeValid[0] = dst_frame.stVFrame.u32PlaneSize[0];
    if (dst_frame.stVFrame.u32PlaneNum > 1U) {
        dst_frame.stVFrame.u32PlaneSizeValid[1] = dst_frame.stVFrame.u32PlaneSize[1];
    }

    if (demo_dump_rgba_file(&dst_frame, config.output_file) != 0) {
        DEMO_LOG("dump RGBA file failed: %s", config.output_file);
        goto EXIT;
    }

    DEMO_LOG("transform finished successfully");

EXIT:
    if (src_buf != 0UL) {
        VB_ReleaseBuffer(src_buf);
    }
    if (dst_buf != 0UL) {
        VB_ReleaseBuffer(dst_buf);
    }
    if (src_pool != 0UL) {
        VB_DestroyPool(src_pool);
    }
    if (dst_pool != 0UL) {
        VB_DestroyPool(dst_pool);
    }
    VB_Exit();
    SYS_Exit();
    return (ret == 0) ? 0 : -1;
}
