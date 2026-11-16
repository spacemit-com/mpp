/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * Hardware regression: encode distinct anonymous mappings, without input VB or
 * dma-buf fds. Unmap each input immediately after SendFrameSync returns.
 * Usage: test_venc_userptr_hw h264|h265|mjpeg output.es [nv12|i420|yv12|rgba]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "para.h"
#include "sys/sys_api.h"
#include "sys/vb_api.h"
#include "venc/venc_api.h"

#define WIDTH 640U
#define HEIGHT 480U
#define FRAME_COUNT 24U

static VideoFrameInfo input;

static int free_frame(void) {
    int result = 0;
    for (U32 plane = 0; plane < input.stVFrame.u32PlaneNum; ++plane) {
        UL addr = input.stVFrame.ulPlaneVirAddr[plane];
        if (addr) {
            if (munmap((void *)addr, input.stVFrame.u32PlaneSize[plane]) != 0) {
                perror("munmap input");
                result = -1;
            } else {
                input.stVFrame.ulPlaneVirAddr[plane] = 0;
            }
        }
    }
    return result;
}

static U64 monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (U64)ts.tv_sec * 1000 + (U64)ts.tv_nsec / 1000000;
}

static int prepare_frame(MppPixelFormat format, U32 i) {
    U32 page = (U32)sysconf(_SC_PAGESIZE);
    U32 planes = format == MPP_PIXEL_FORMAT_RGBA ? 1 : format == MPP_PIXEL_FORMAT_NV12 ? 2 : 3;
    VideoFrameInfo *frame = &input;
    memset(frame, 0, sizeof(*frame));
    frame->eFrameType = FRAME_TYPE_VENC;
    frame->eModId = MPP_ID_VENC;
    frame->u32Idx = i;
    frame->stCommFrameInfo.u32Width = WIDTH;
    frame->stCommFrameInfo.u32Height = HEIGHT;
    frame->stCommFrameInfo.u32Align = 16;
    frame->stCommFrameInfo.ePixelFormat = format;
    frame->stVFrame.u32PlaneNum = planes;
    frame->stVFrame.u64PTS = (U64)i * 33333;
    for (U32 j = 0; j < planes; ++j) {
        U32 stride = format == MPP_PIXEL_FORMAT_RGBA ? WIDTH * 4 : j && planes == 3 ? WIDTH / 2 : WIDTH;
        U32 size = stride * (j ? HEIGHT / 2 : HEIGHT);
        U32 allocation = ((size + page - 1) / page) * page;
        U8 *data = mmap(NULL, allocation, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (data == MAP_FAILED)
            return -1;
        frame->stVFrame.ulPlaneVirAddr[j] = (UL)data;
        frame->stVFrame.u32PlaneStride[j] = stride;
        frame->stVFrame.u32PlaneSize[j] = allocation;
        frame->stVFrame.u32PlaneSizeValid[j] = size;
        frame->stVFrame.u32Fd[j] = (UL)-1;
        memset(data, j ? 128 : 32 + i * 7, allocation);
    }
    return 0;
}

static int drain(FILE *output, U32 timeout, U32 *packets, BOOL *eos) {
    StreamBufferInfo stream;
    memset(&stream, 0, sizeof(stream));
    S32 ret = VENC_GetStream(0, &stream, timeout);
    if (ret == ERR_VENC_NO_STREAM || ret == ERR_VENC_TIMEOUT)
        return 0;
    if (ret == ERR_VENC_EOS) {
        *eos = MPP_TRUE;
        return 0;
    }
    if (ret != ERR_VENC_OK) {
        fprintf(stderr, "GetStream failed: %d\n", ret);
        return -1;
    }
    int failed = fwrite(stream.pu8Addr, 1, stream.u32Size, output) != stream.u32Size;
    if (stream.u32Size)
        ++*packets;
    if (VENC_ReleaseStream(0, &stream) != ERR_VENC_OK)
        failed = 1;
    return failed ? -1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: %s h264|h265|mjpeg output.es [nv12|i420|yv12|rgba]\n", argv[0]);
        return 2;
    }
    VencChnAttr attr = {0};
    if (strcmp(argv[1], "h264") == 0)
        attr.eCodecType = MPP_STREAM_CODEC_H264;
    else if (strcmp(argv[1], "h265") == 0)
        attr.eCodecType = MPP_STREAM_CODEC_H265;
    else if (strcmp(argv[1], "mjpeg") == 0)
        attr.eCodecType = MPP_STREAM_CODEC_MJPEG;
    else
        return 2;
    attr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    if (argc == 4 && strcmp(argv[3], "i420") == 0)
        attr.eInputPixelFormat = MPP_PIXEL_FORMAT_I420;
    else if (argc == 4 && strcmp(argv[3], "yv12") == 0)
        attr.eInputPixelFormat = MPP_PIXEL_FORMAT_YV12;
    else if (argc == 4 && strcmp(argv[3], "rgba") == 0)
        attr.eInputPixelFormat = MPP_PIXEL_FORMAT_RGBA;
    else if (argc == 4 && strcmp(argv[3], "nv12") != 0)
        return 2;
    attr.eFrameBufMode = VENC_FRAME_BUF_USERPTR_EXTERNAL;
    attr.u32Width = WIDTH;
    attr.u32Height = HEIGHT;
    attr.u32Align = 16;
    attr.u32FrameRate = 30;
    attr.u32Gop = 12;
    attr.u32Bitrate = 1000000;
    attr.eRcMode = VENC_RC_MODE_CBR;
    int result = 1;
    BOOL sys = MPP_FALSE, vb = MPP_FALSE, venc = MPP_FALSE;
    BOOL created = MPP_FALSE, enabled = MPP_FALSE, eos = MPP_FALSE;
    U32 packets = 0;
    U32 submitted = 0;
    FILE *output = fopen(argv[2], "wb");
    if (!output)
        goto cleanup;
    if (SYS_Init() != 0)
        goto cleanup;
    sys = MPP_TRUE;
    if (VB_Init() != 0)
        goto cleanup;
    vb = MPP_TRUE;
    if (VENC_Init() != 0)
        goto cleanup;
    venc = MPP_TRUE;
    if (VENC_CreateChn(0, &attr) != ERR_VENC_OK)
        goto cleanup;
    created = MPP_TRUE;
    S32 ret = VENC_EnableChn(0);
    if (ret != ERR_VENC_OK) {
        fprintf(stderr, "EnableChn failed: %d\n", ret);
        goto cleanup;
    }
    enabled = MPP_TRUE;
    U64 deadline = monotonic_ms() + 30000;
    for (U32 i = 0; i < FRAME_COUNT; ++i) {
        if (prepare_frame(attr.eInputPixelFormat, i) != 0)
            goto cleanup;
        ret = VENC_SendFrameSync(0, &input);
        if (free_frame() != 0)
            goto cleanup;
        if (ret != ERR_VENC_OK) {
            fprintf(stderr, "SendFrameSync %u failed: %d\n", i, ret);
            goto cleanup;
        }
        submitted++;
        while (packets < submitted && monotonic_ms() < deadline) {
            if (drain(output, 100, &packets, &eos) != 0)
                goto cleanup;
        }
        if (packets != submitted) {
            fprintf(stderr, "Missing output after input %u\n", i);
            goto cleanup;
        }
    }
    VideoFrameInfo end = {0};
    end.stVFrame.u32FrameFlag = MPP_FRAME_FLAG_EOS;
    if (VENC_SendFrameSync(0, &end) != ERR_VENC_OK)
        goto cleanup;
    while (!eos && monotonic_ms() < deadline) {
        if (drain(output, 100, &packets, &eos) != 0)
            goto cleanup;
    }
    if (!eos || packets != FRAME_COUNT) {
        fprintf(stderr, "Incomplete output: packets=%u expected=%u eos=%d\n", packets, FRAME_COUNT, eos);
        goto cleanup;
    }
    result = 0;

cleanup:
    if (enabled) {
        S32 ret = VENC_DisableChn(0);
        if (ret != ERR_VENC_OK) {
            fprintf(stderr, "DisableChn failed: %d\n", ret);
            result = 1;
        }
    }
    if (created) {
        S32 ret = VENC_DestroyChn(0);
        if (ret != ERR_VENC_OK) {
            fprintf(stderr, "DestroyChn failed: %d\n", ret);
            result = 1;
        }
    }
    if (free_frame() != 0)
        result = 1;
    if (venc && VENC_Exit() != ERR_VENC_OK)
        result = 1;
    if (vb && VB_Exit() != 0)
        result = 1;
    if (sys && SYS_Exit() != 0)
        result = 1;
    if (output && fclose(output) != 0)
        result = 1;
    if (result == 0) {
        printf("PASS: USERPTR sync %s %s, %u frames, %u inputs unmapped before GetStream, no input VB or dma-buf fd\n",
            argv[1], argc == 4 ? argv[3] : "nv12", packets, submitted);
    }
    return result;
}
