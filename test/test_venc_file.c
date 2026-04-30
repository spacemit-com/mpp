/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_venc_file.c
 * @Date      :    2026-04-30
 * @Brief     :    File-based VENC single-module test.
 *                 Read one NV12 frame from file, encode to H.264 or MJPEG,
 *                 then write encoded stream to output file.
 *
 * Usage:
 *   ./test_venc_file <input_nv12> <width> <height> <output_file> [codec] [frames]
 *
 * Examples:
 *   ./test_venc_file ./test/assets/vi_phy0_last_frame.yuv 1920 1080 ./out.h264 h264 1
 *   ./test_venc_file ./test/assets/vi_phy0_last_frame.yuv 1920 1080 ./out.jpg mjpeg 1
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys_api.h"
#include "vb_api.h"
#include "venc/venc_api.h"

#define TEST_ALIGN      16U
#define TEST_BITRATE    4000000U
#define TEST_FRAMERATE  30U
#define TEST_GOP        30U

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <input_nv12> <width> <height> <output_file> [codec] [frames]\n"
            "  codec : h264 | h265 | mjpeg (default: h264)\n"
            "  frames: number of times to feed the same frame (default: 1)\n",
            prog);
}

static U32 align_up(U32 value, U32 align)
{
    return (value + align - 1U) / align * align;
}

static MppStreamCodecType parse_codec(const char *codec)
{
    if (codec == NULL || strcmp(codec, "h264") == 0)
        return MPP_STREAM_CODEC_H264;
    if (strcmp(codec, "h265") == 0)
        return MPP_STREAM_CODEC_H265;
    if (strcmp(codec, "mjpeg") == 0 || strcmp(codec, "jpeg") == 0)
        return MPP_STREAM_CODEC_MJPEG;
    return MPP_STREAM_CODEC_UNKNOWN;
}

static const char *codec_name(MppStreamCodecType codec)
{
    switch (codec) {
    case MPP_STREAM_CODEC_H264: return "H.264";
    case MPP_STREAM_CODEC_H265: return "H.265";
    case MPP_STREAM_CODEC_MJPEG: return "MJPEG";
    default: return "UNKNOWN";
    }
}

static int load_nv12_frame(FILE *fp, U8 *dst, U32 width, U32 height, U32 stride)
{
    U32 y;
    const U32 y_rows = height;
    const U32 uv_rows = height / 2U;
    const U32 row_bytes = width;
    U8 *base = dst;

    for (y = 0; y < y_rows; ++y) {
        if (fread(base + (size_t)y * stride, 1, row_bytes, fp) != row_bytes)
            return -1;
    }

    base += (size_t)stride * height;
    for (y = 0; y < uv_rows; ++y) {
        if (fread(base + (size_t)y * stride, 1, row_bytes, fp) != row_bytes)
            return -1;
    }

    return 0;
}

int main(int argc, char *argv[])
{
    const char *input_path;
    const char *output_path;
    const char *codec_arg = "h264";
    FILE *fin = NULL;
    FILE *fout = NULL;
    MppStreamCodecType codec;
    U32 width, height, frames = 1;
    U32 stride, y_size, uv_size, total_size;
    S32 ret;
    UL pool = 0;
    UL buffer = 0;
    void *vir_addr = NULL;
    S32 dma_fd = -1;
    VencChnAttr venc_attr;
    VbPoolCfg pool_cfg;
    VideoFrameInfo frame_info;
    StreamBufferInfo stream;
    U32 encoded = 0;
    S32 venc_chn = 0;
    BOOL sys_inited = MPP_FALSE;
    BOOL vb_inited = MPP_FALSE;
    BOOL venc_inited = MPP_FALSE;
    BOOL chn_created = MPP_FALSE;
    BOOL chn_enabled = MPP_FALSE;

    if (argc < 5) {
        usage(argv[0]);
        return 1;
    }

    input_path = argv[1];
    width = (U32)strtoul(argv[2], NULL, 10);
    height = (U32)strtoul(argv[3], NULL, 10);
    output_path = argv[4];
    if (argc > 5)
        codec_arg = argv[5];
    if (argc > 6)
        frames = (U32)strtoul(argv[6], NULL, 10);
    if (width == 0 || height == 0 || frames == 0) {
        usage(argv[0]);
        return 1;
    }

    codec = parse_codec(codec_arg);
    if (codec == MPP_STREAM_CODEC_UNKNOWN) {
        fprintf(stderr, "Unsupported codec: %s\n", codec_arg);
        return 1;
    }

    fin = fopen(input_path, "rb");
    if (!fin) {
        fprintf(stderr, "fopen input failed: %s\n", strerror(errno));
        return 1;
    }

    fout = fopen(output_path, "wb");
    if (!fout) {
        fprintf(stderr, "fopen output failed: %s\n", strerror(errno));
        fclose(fin);
        return 1;
    }

    stride = align_up(width, TEST_ALIGN);
    y_size = stride * height;
    uv_size = stride * (height / 2U);
    total_size = y_size + uv_size;

    ret = SYS_Init();
    if (ret != 0) {
        fprintf(stderr, "SYS_Init failed: %d\n", ret);
        goto fail;
    }
    sys_inited = MPP_TRUE;
    ret = VB_Init();
    if (ret != 0) {
        fprintf(stderr, "VB_Init failed: %d\n", ret);
        goto fail_sys;
    }
    vb_inited = MPP_TRUE;
    ret = VENC_Init();
    if (ret != 0) {
        fprintf(stderr, "VENC_Init failed: %d\n", ret);
        goto fail_vb;
    }
    venc_inited = MPP_TRUE;

    memset(&pool_cfg, 0, sizeof(pool_cfg));
    pool_cfg.u32BufCnt = 2;
    pool_cfg.u32BufSize = total_size;
    pool_cfg.eModId = MPP_ID_VENC;
    pool_cfg.eRemapMode = VBUF_REMAP_MODE_NOCACHE;
    pool = VB_CreatePool(&pool_cfg);
    if (pool == 0 || pool == (UL)-1) {
        fprintf(stderr, "VB_CreatePool failed\n");
        goto fail_venc;
    }

    memset(&frame_info, 0, sizeof(frame_info));
    frame_info.eFrameType = FRAME_TYPE_VENC;
    frame_info.eModId = MPP_ID_VENC;
    frame_info.ulPoolId = pool;
    frame_info.stVencFrameInfo.stCommFrameInfo.u32Width = width;
    frame_info.stVencFrameInfo.stCommFrameInfo.u32Height = height;
    frame_info.stVencFrameInfo.stCommFrameInfo.u32Align = TEST_ALIGN;
    frame_info.stVencFrameInfo.stCommFrameInfo.ePixelFormat = MPP_PIXEL_FORMAT_NV12;
    frame_info.stVencFrameInfo.stCommFrameInfo.eCompressMode = COMPRESS_MODE_NONE;
    frame_info.stVencFrameInfo.stCommFrameInfo.eColorSpace = COLOR_SPACE_BT709;
    frame_info.stVFrame.u32PlaneNum = 2;
    frame_info.stVFrame.u32PlaneStride[0] = stride;
    frame_info.stVFrame.u32PlaneStride[1] = stride;
    frame_info.stVFrame.u32PlaneSize[0] = y_size;
    frame_info.stVFrame.u32PlaneSize[1] = uv_size;
    frame_info.stVFrame.u32PlaneSizeValid[0] = y_size;
    frame_info.stVFrame.u32PlaneSizeValid[1] = uv_size;
    frame_info.stVFrame.u32TotalSize = total_size;

    ret = VB_SetFrameInfo(pool, &frame_info);
    if (ret != 0) {
        fprintf(stderr, "VB_SetFrameInfo failed: %d\n", ret);
        goto fail_pool;
    }

    buffer = VB_GetBuffer(pool, 0);
    if (buffer == 0 || buffer == (UL)-1) {
        fprintf(stderr, "VB_GetBuffer failed\n");
        goto fail_pool;
    }

    ret = VB_GetVirAddr(buffer, &vir_addr);
    if (ret != 0 || vir_addr == NULL) {
        fprintf(stderr, "VB_GetVirAddr failed: %d\n", ret);
        goto fail_buffer;
    }
    ret = VB_GetDmaBufFd(buffer, &dma_fd);
    if (ret != 0) {
        fprintf(stderr, "VB_GetDmaBufFd failed: %d\n", ret);
        goto fail_buffer;
    }

    if (load_nv12_frame(fin, (U8 *)vir_addr, width, height, stride) != 0) {
        fprintf(stderr, "load_nv12_frame failed, please check input file/size\n");
        goto fail_buffer;
    }

    memset(&frame_info, 0, sizeof(frame_info));
    ret = VB_GetFrameInfo(buffer, &frame_info);
    if (ret != 0) {
        fprintf(stderr, "VB_GetFrameInfo failed: %d\n", ret);
        goto fail_buffer;
    }

    frame_info.eFrameType = FRAME_TYPE_VENC;
    frame_info.eModId = MPP_ID_VENC;
    frame_info.ulPoolId = pool;
    frame_info.ulBufferId = buffer;
    frame_info.u32Idx = 0;
    frame_info.stVencFrameInfo.stCommFrameInfo = frame_info.stCommFrameInfo;
    frame_info.stVFrame.u32PlaneNum = 2;
    frame_info.stVFrame.u32PlaneStride[0] = stride;
    frame_info.stVFrame.u32PlaneStride[1] = stride;
    frame_info.stVFrame.u32PlaneSize[0] = y_size;
    frame_info.stVFrame.u32PlaneSize[1] = uv_size;
    frame_info.stVFrame.u32PlaneSizeValid[0] = y_size;
    frame_info.stVFrame.u32PlaneSizeValid[1] = uv_size;
    frame_info.stVFrame.u32TotalSize = total_size;
    frame_info.stVFrame.u32Fd[0] = (UL)dma_fd;
    frame_info.stVFrame.u32Fd[1] = (UL)dma_fd;
    frame_info.stVFrame.ulPlaneVirAddr[0] = (UL)(uintptr_t)vir_addr;
    frame_info.stVFrame.ulPlaneVirAddr[1] = (UL)((uintptr_t)vir_addr + y_size);

    memset(&venc_attr, 0, sizeof(venc_attr));
    venc_attr.eCodecType = codec;
    venc_attr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    venc_attr.u32Width = width;
    venc_attr.u32Height = height;
    venc_attr.u32Align = TEST_ALIGN;
    venc_attr.u32Bitrate = TEST_BITRATE;
    venc_attr.u32FrameRate = TEST_FRAMERATE;
    venc_attr.u32Gop = TEST_GOP;
    venc_attr.eRcMode = VENC_RC_MODE_CBR;

    ret = VENC_CreateChn(venc_chn, &venc_attr);
    if (ret != 0) {
        fprintf(stderr, "VENC_CreateChn failed: %d\n", ret);
        goto fail_buffer;
    }
    chn_created = MPP_TRUE;
    ret = VENC_EnableChn(venc_chn);
    if (ret != 0) {
        fprintf(stderr, "VENC_EnableChn failed: %d\n", ret);
        goto fail_chn;
    }
    chn_enabled = MPP_TRUE;

    printf("[INFO] VENC file test: %s %ux%u -> %s (%s), frames=%u\n",
           input_path, width, height, output_path, codec_name(codec), frames);

    for (U32 i = 0; i < frames; ++i) {
        frame_info.u32Idx = i;
        frame_info.stVFrame.u64PTS = (U64)i * 1000000ULL / TEST_FRAMERATE;

        ret = VENC_SendFrame(venc_chn, &frame_info, 3000);
        if (ret != 0) {
            fprintf(stderr, "VENC_SendFrame failed at frame %u: %d\n", i, ret);
            goto fail_enabled;
        }

        memset(&stream, 0, sizeof(stream));
        ret = VENC_GetStream(venc_chn, &stream, 3000);
        if (ret != 0) {
            fprintf(stderr, "VENC_GetStream failed at frame %u: %d\n", i, ret);
            goto fail_enabled;
        }

        if (fwrite(stream.pu8Addr, 1, stream.u32Size, fout) != stream.u32Size) {
            fprintf(stderr, "fwrite failed at frame %u\n", i);
            VENC_ReleaseStream(venc_chn, &stream);
            goto fail_enabled;
        }

        printf("[INFO] encoded frame %u: size=%u key=%d pts=%llu\n",
               i, stream.u32Size, stream.bKeyFrame,
               (unsigned long long)stream.u64PTS);
        VENC_ReleaseStream(venc_chn, &stream);
        encoded++;
    }

    printf("[INFO] done, encoded %u frame(s)\n", encoded);

    /* NOTE:
     * Board-side SYS_Exit/VB_Exit cleanup path is currently unstable and may
     * trigger SIGBUS even for reference programs. To prioritize functional
     * codec verification, intentionally skip full teardown here.
     */
    fclose(fin);
    fclose(fout);
    return 0;

fail_enabled:
fail_chn:
fail_buffer:
fail_pool:
fail_venc:
fail_vb:
fail_sys:
    (void)chn_enabled;
    (void)chn_created;
    (void)venc_inited;
    (void)vb_inited;
    (void)sys_inited;
fail:
    if (fin)
        fclose(fin);
    if (fout)
        fclose(fout);
    return 1;
}
