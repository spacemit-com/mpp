/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_vdec_file.c
 * @Date      :    2026-04-30
 * @Brief     :    File-based VDEC single-module test.
 *                 Read one compressed JPEG/H.264 elementary stream from file,
 *                 decode it, then write the first decoded frame to NV12 file.
 *
 * Usage:
 *   ./test_vdec_file <input_stream> <width> <height> <output_nv12> [codec]
 *
 * Examples:
 *   ./test_vdec_file ./test/assets/1920x1080.jpg 1920 1080 ./dec_out_nv12.yuv mjpeg
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
#include "vdec/vdec_api.h"

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <input_stream> <width> <height> <output_nv12> [codec]\n"
            "  codec : mjpeg | h264 | h265 (default: mjpeg)\n",
            prog);
}

static MppStreamCodecType parse_codec(const char *codec)
{
    if (codec == NULL || strcmp(codec, "mjpeg") == 0 || strcmp(codec, "jpeg") == 0)
        return MPP_STREAM_CODEC_MJPEG;
    if (strcmp(codec, "h264") == 0)
        return MPP_STREAM_CODEC_H264;
    if (strcmp(codec, "h265") == 0)
        return MPP_STREAM_CODEC_H265;
    return MPP_STREAM_CODEC_UNKNOWN;
}

static int save_nv12_frame(FILE *fp, const VideoFrameInfo *frame)
{
    const U8 *base;
    U32 width, height, stride;
    U32 y;

    if (frame == NULL || frame->stVFrame.ulPlaneVirAddr[0] == 0)
        return -1;

    width = frame->stVdecFrameInfo.stCommFrameInfo.u32Width;
    height = frame->stVdecFrameInfo.stCommFrameInfo.u32Height;
    stride = frame->stVFrame.u32PlaneStride[0];
    if (stride == 0)
        stride = width;

    base = (const U8 *)(uintptr_t)frame->stVFrame.ulPlaneVirAddr[0];
    for (y = 0; y < height; ++y) {
        if (fwrite(base + (size_t)y * stride, 1, width, fp) != width)
            return -1;
    }

    if (frame->stVFrame.u32PlaneNum > 1 && frame->stVFrame.ulPlaneVirAddr[1] != 0) {
        const U8 *uv = (const U8 *)(uintptr_t)frame->stVFrame.ulPlaneVirAddr[1];
        for (y = 0; y < height / 2U; ++y) {
            if (fwrite(uv + (size_t)y * stride, 1, width, fp) != width)
                return -1;
        }
    } else {
        const U8 *uv = base + (size_t)stride * height;
        for (y = 0; y < height / 2U; ++y) {
            if (fwrite(uv + (size_t)y * stride, 1, width, fp) != width)
                return -1;
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    const char *input_path;
    const char *output_path;
    const char *codec_arg = "mjpeg";
    MppStreamCodecType codec;
    U32 width, height;
    FILE *fin = NULL;
    FILE *fout = NULL;
    U8 *stream_buf = NULL;
    long file_size;
    StreamBufferInfo stream;
    VideoFrameInfo frame;
    VdecChnAttr attr;
    S32 ret;
    S32 chn = 0;
    int rc = 1;
    BOOL sys_inited = MPP_FALSE;
    BOOL vb_inited = MPP_FALSE;
    BOOL vdec_inited = MPP_FALSE;
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

    if (width == 0 || height == 0) {
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
        goto out;
    }
    fout = fopen(output_path, "wb");
    if (!fout) {
        fprintf(stderr, "fopen output failed: %s\n", strerror(errno));
        goto out;
    }

    if (fseek(fin, 0, SEEK_END) != 0) {
        fprintf(stderr, "fseek end failed\n");
        goto out;
    }
    file_size = ftell(fin);
    if (file_size <= 0) {
        fprintf(stderr, "invalid input file size\n");
        goto out;
    }
    rewind(fin);

    stream_buf = (U8 *)malloc((size_t)file_size);
    if (!stream_buf) {
        fprintf(stderr, "malloc stream buf failed\n");
        goto out;
    }
    if (fread(stream_buf, 1, (size_t)file_size, fin) != (size_t)file_size) {
        fprintf(stderr, "fread input failed\n");
        goto out;
    }

    ret = SYS_Init();
    if (ret != 0) {
        fprintf(stderr, "SYS_Init failed: %d\n", ret);
        goto out;
    }
    sys_inited = MPP_TRUE;
    ret = VB_Init();
    if (ret != 0) {
        fprintf(stderr, "VB_Init failed: %d\n", ret);
        goto out_sys;
    }
    vb_inited = MPP_TRUE;
    ret = VDEC_Init();
    if (ret != 0) {
        fprintf(stderr, "VDEC_Init failed: %d\n", ret);
        goto out_vb;
    }
    vdec_inited = MPP_TRUE;

    memset(&attr, 0, sizeof(attr));
    attr.eCodecType = codec;
    attr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    attr.u32Align = 0;
    attr.u32Width = width;
    attr.u32Height = height;
    attr.bIsFrameReordering = MPP_FALSE;
    attr.bDispErrorFrame = MPP_FALSE;
    attr.stScale.bScaleEnable = MPP_TRUE;
    attr.stScale.u32Width = 320;
    attr.stScale.u32Height = 320;

    ret = VDEC_CreateChn(chn, &attr);
    if (ret != 0) {
        fprintf(stderr, "VDEC_CreateChn failed: %d\n", ret);
        goto out_vdec;
    }
    chn_created = MPP_TRUE;
    ret = VDEC_EnableChn(chn);
    if (ret != 0) {
        fprintf(stderr, "VDEC_EnableChn failed: %d\n", ret);
        goto out_chn;
    }
    chn_enabled = MPP_TRUE;

    memset(&stream, 0, sizeof(stream));
    stream.pu8Addr = stream_buf;
    stream.u32Size = (U32)file_size;
    stream.bKeyFrame = MPP_TRUE;
    stream.bEndOfStream = MPP_FALSE;
    stream.eCodecType = codec;
    stream.u64PTS = 0;
    stream.u32Width = width;
    stream.u32Height = height;

    printf("[INFO] VDEC file test: %s -> %s, codec=%s, size=%ld\n",
           input_path, output_path, codec_arg, file_size);

    ret = VDEC_SendStream(chn, &stream, 3000);
    if (ret != 0) {
        fprintf(stderr, "VDEC_SendStream failed: %d\n", ret);
        goto out_enabled;
    }

    memset(&frame, 0, sizeof(frame));
    ret = VDEC_GetFrame(chn, &frame, 5000);
    if (ret != 0) {
        fprintf(stderr, "VDEC_GetFrame failed: %d\n", ret);
        goto out_enabled;
    }

    printf("[INFO] decoded frame: %ux%u planes=%u stride=%u pts=%llu\n",
           frame.stVdecFrameInfo.stCommFrameInfo.u32Width,
           frame.stVdecFrameInfo.stCommFrameInfo.u32Height,
           frame.stVFrame.u32PlaneNum,
           frame.stVFrame.u32PlaneStride[0],
           (unsigned long long)frame.stVFrame.u64PTS);

    if (save_nv12_frame(fout, &frame) != 0) {
        fprintf(stderr, "save_nv12_frame failed\n");
        VDEC_ReleaseFrame(chn, frame.ulBufferId);
        goto out_enabled;
    }

    ret = VDEC_ReleaseFrame(chn, frame.ulBufferId);
    if (ret != 0) {
        fprintf(stderr, "VDEC_ReleaseFrame failed: %d\n", ret);
        goto out_enabled;
    }

    rc = 0;

out_enabled:
out_chn:
out_vdec:
out_vb:
out_sys:
    (void)chn_enabled;
    (void)chn_created;
    (void)vdec_inited;
    (void)vb_inited;
    (void)sys_inited;
out:
    if (stream_buf)
        free(stream_buf);
    if (fin)
        fclose(fin);
    if (fout)
        fclose(fout);
    return rc;
}
