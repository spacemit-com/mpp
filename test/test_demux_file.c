/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    test_demux_file.c
 * @Date      :    2026-05-28
 * @Brief     :    Test DEMUX file demuxing (MP4/TS) with VDEC binding.
 *                 Features:
 *                 - MP4 and TS file demuxing
 *                 - Bind DEMUX → VDEC for automatic decoding
 *                 - Save decoded frames to YUV file
 *------------------------------------------------------------------------------
 */

#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "demux/demux_api.h"
#include "sys/sys_api.h"
#include "sys/vb_api.h"
#include "vdec/vdec_api.h"

/* ======================== Configuration ======================== */

#define TEST_DEMUX_CHN 0
#define TEST_VDEC_CHN 0
#define MAX_FRAMES 250 /* Max frames to decode and save */

/* ======================== Global Variables ======================== */

static volatile sig_atomic_t g_bExit = 0;

/* ======================== Signal Handler ======================== */

static void signal_handler(int sig) {
    printf("\n[TEST] Caught signal %d, exiting...\n", sig);
    fflush(stdout);
    g_bExit = 1;
}

/* ======================== Helper Functions ======================== */

static S32 save_nv12_frame(FILE *fp, const VideoFrameInfo *frame) {
    U32 width, height, stride;
    const U8 *base;

    if (!fp || !frame || frame->stVFrame.ulPlaneVirAddr[0] == 0)
        return -1;

    width = frame->stVdecFrameInfo.stCommFrameInfo.u32Width;
    height = frame->stVdecFrameInfo.stCommFrameInfo.u32Height;
    stride = frame->stVFrame.u32PlaneStride[0];

    /* Write Y plane */
    base = (const U8 *)(uintptr_t)frame->stVFrame.ulPlaneVirAddr[0];
    for (U32 i = 0; i < height; i++) {
        fwrite(base + i * stride, 1, width, fp);
    }

    /* Write UV plane (NV12) */
    if (frame->stVFrame.u32PlaneNum > 1 && frame->stVFrame.ulPlaneVirAddr[1] != 0) {
        const U8 *uv = (const U8 *)(uintptr_t)frame->stVFrame.ulPlaneVirAddr[1];
        U32 uvStride = frame->stVFrame.u32PlaneStride[1];
        for (U32 i = 0; i < height / 2; i++) {
            fwrite(uv + i * uvStride, 1, width, fp);
        }
    }

    return 0;
}

static U64 get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (U64)tv.tv_sec * 1000000 + tv.tv_usec;
}

/* ======================== Test Functions ======================== */

static S32 test_demux_vdec_bind(const char *filename, const char *output_yuv) {
    S32 ret;
    DemuxChnAttr stDemuxAttr;
    VdecChnAttr stVdecAttr;
    DemuxStreamInfo stInfo;
    MppNode stDemuxSrc, stVdecSink;
    U64 u64StartTime, u64ElapsedUs;
    U32 u32FrameCount = 0;
    FILE *fpOutput = NULL;

    printf("\n========== DEMUX → VDEC Binding Test ==========\n");
    printf("[TEST] Input: %s\n", filename);
    printf("[TEST] Output: %s\n", output_yuv);

    fpOutput = fopen(output_yuv, "wb");
    if (!fpOutput) {
        fprintf(stderr, "[TEST] Failed to open output file\n");
        return -1;
    }

    /* Initialize modules */
    ret = SYS_Init();
    if (ret != 0) {
        fprintf(stderr, "[TEST] SYS_Init failed: %d\n", ret);
        fclose(fpOutput);
        return ret;
    }

    ret = VB_Init();
    if (ret != 0) {
        fprintf(stderr, "[TEST] VB_Init failed: %d\n", ret);
        SYS_Exit();
        fclose(fpOutput);
        return ret;
    }

    ret = DEMUX_Init();
    if (ret != 0) {
        fprintf(stderr, "[TEST] DEMUX_Init failed: %d\n", ret);
        VB_Exit();
        SYS_Exit();
        fclose(fpOutput);
        return ret;
    }

    ret = VDEC_Init();
    if (ret != 0) {
        fprintf(stderr, "[TEST] VDEC_Init failed: %d\n", ret);
        DEMUX_Exit();
        VB_Exit();
        SYS_Exit();
        fclose(fpOutput);
        return ret;
    }

    /* Create DEMUX channel */
    memset(&stDemuxAttr, 0, sizeof(stDemuxAttr));
    stDemuxAttr.eInputType = DEMUX_INPUT_FILE;
    snprintf(stDemuxAttr.szUrl, sizeof(stDemuxAttr.szUrl), "%s", filename);
    stDemuxAttr.u32OpenTimeoutMs = 5000;
    stDemuxAttr.u32RwTimeoutMs = 5000;
    stDemuxAttr.bInjectPS = MPP_TRUE;

    ret = DEMUX_CreateChn(TEST_DEMUX_CHN, &stDemuxAttr);
    if (ret != 0) {
        fprintf(stderr, "[TEST] DEMUX_CreateChn failed: %d\n", ret);
        goto cleanup;
    }

    /* Create VDEC channel */
    memset(&stVdecAttr, 0, sizeof(stVdecAttr));
    stVdecAttr.eCodecType = MPP_STREAM_CODEC_H264;
    stVdecAttr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    stVdecAttr.u32Width = 640;
    stVdecAttr.u32Height = 480;
    stVdecAttr.u32Align = 16;

    ret = VDEC_CreateChn(TEST_VDEC_CHN, &stVdecAttr);
    if (ret != 0) {
        fprintf(stderr, "[TEST] VDEC_CreateChn failed: %d\n", ret);
        goto cleanup;
    }

    ret = VDEC_EnableChn(TEST_VDEC_CHN);
    if (ret != 0) {
        fprintf(stderr, "[TEST] VDEC_EnableChn failed: %d\n", ret);
        goto cleanup;
    }

    /* Bind DEMUX → VDEC */
    ret = DEMUX_GetSrcNode(TEST_DEMUX_CHN, &stDemuxSrc);
    if (ret != 0) {
        fprintf(stderr, "[TEST] DEMUX_GetSrcNode failed: %d\n", ret);
        goto cleanup;
    }

    stVdecSink.eModId = MPP_ID_VDEC;
    stVdecSink.s32DevId = 0;
    stVdecSink.s32ChnId = TEST_VDEC_CHN;

    ret = SYS_Bind(&stDemuxSrc, &stVdecSink);
    if (ret != 0) {
        fprintf(stderr, "[TEST] SYS_Bind failed: %d\n", ret);
        goto cleanup;
    }
    printf("[TEST] Bound DEMUX[%d] → VDEC[%d]\n", TEST_DEMUX_CHN, TEST_VDEC_CHN);

    /* Start DEMUX channel */
    ret = DEMUX_StartChn(TEST_DEMUX_CHN);
    if (ret != 0) {
        fprintf(stderr, "[TEST] DEMUX_StartChn failed: %d\n", ret);
        goto cleanup;
    }

    /* Do not sleep here: in bind mode DEMUX starts feeding immediately.
     * Sleeping before receiving frames can fill the small stream queue and
     * drop TS PES packets, which causes long-GOP decode failures. */
    ret = DEMUX_GetStreamInfo(TEST_DEMUX_CHN, &stInfo);
    if (ret == 0) {
        printf("[TEST] Stream Info:\n");
        printf("  Codec: %d\n", stInfo.eCodecType);
        printf("  Resolution: %ux%u\n", stInfo.u32Width, stInfo.u32Height);
        printf("  FPS: %u\n", stInfo.u32Fps);
    }

    /* Receive and save decoded frames */
    printf("[TEST] Decoding frames (max %d)...\n", MAX_FRAMES);
    u64StartTime = get_time_us();

    while (!g_bExit && u32FrameCount < MAX_FRAMES) {
        VideoFrameInfo stFrame;

        memset(&stFrame, 0, sizeof(stFrame));
        ret = VDEC_GetFrame(TEST_VDEC_CHN, &stFrame, 1000);

        if (ret == 0) {
            u32FrameCount++;

            /* Save frame to file */
            save_nv12_frame(fpOutput, &stFrame);

            if (u32FrameCount % 25 == 0 || u32FrameCount == 1) {
                printf("[TEST] Frame %u: %ux%u, PTS=%" PRIu64 "\n", u32FrameCount,
                    stFrame.stVdecFrameInfo.stCommFrameInfo.u32Width, stFrame.stVdecFrameInfo.stCommFrameInfo.u32Height,
                    (uint64_t)stFrame.stVFrame.u64PTS);
            }

            VDEC_ReleaseFrame(TEST_VDEC_CHN, stFrame.ulBufferId);
        } else if (ret == ERR_VDEC_NO_FRAME || ret == ERR_VDEC_TIMEOUT) {
            /* Timeout, continue waiting */
            usleep(10000);
        } else if (ret == ERR_VDEC_EOS) {
            printf("[TEST] End of stream\n");
            break;
        } else {
            fprintf(stderr, "[TEST] VDEC_GetFrame failed: %d\n", ret);
            break;
        }
    }

    u64ElapsedUs = get_time_us() - u64StartTime;
    printf("[TEST] Decoded %u frames in %.2f seconds (%.2f fps)\n", u32FrameCount, u64ElapsedUs / 1000000.0,
        u32FrameCount * 1000000.0 / u64ElapsedUs);

cleanup:
    /* Stop channels */
    DEMUX_StopChn(TEST_DEMUX_CHN);
    VDEC_DisableChn(TEST_VDEC_CHN);

    /* Unbind */
    SYS_UnBind(&stDemuxSrc, &stVdecSink);

    /* Destroy channels */
    VDEC_DestroyChn(TEST_VDEC_CHN);
    DEMUX_DestroyChn(TEST_DEMUX_CHN);

    /* Exit modules */
    VDEC_Exit();
    DEMUX_Exit();
    VB_Exit();
    SYS_Exit();

    if (fpOutput) {
        fclose(fpOutput);
    }

    printf("[TEST] Test completed, decoded %u frames\n", u32FrameCount);
    return (u32FrameCount > 0) ? 0 : -1;
}

/* ======================== Main ======================== */

static void print_usage(const char *prog) {
    printf("Usage: %s <input_file> [output_yuv]\n", prog);
    printf("  input_file: MP4 or TS file to demux\n");
    printf("  output_yuv: Output YUV file (default: test_demux_output.yuv)\n");
    printf("\nExample:\n");
    printf("  %s ../test/assets/test_video.mp4\n", prog);
    printf("  %s ../test/assets/test_video.ts output.yuv\n", prog);
}

int main(int argc, char *argv[]) {
    S32 ret;
    int i;
    const char *input_file = NULL;
    const char *output_file = "test_demux_output.yuv";

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    printf("\n");
    printf("========================================\n");
    printf("  MPP DEMUX File Test (MP4/TS)\n");
    printf("========================================\n");

    /* Parse arguments */
    if (argc < 2) {
        print_usage(argv[0]);
        return -1;
    }

    input_file = argv[1];
    if (argc >= 3) {
        output_file = argv[2];
    }

    /* Setup signal handler */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Run test */
    printf("\n[TEST] Input file: %s\n", input_file);
    printf("[TEST] Output file: %s\n", output_file);

    ret = test_demux_vdec_bind(input_file, output_file);
    if (ret != 0) {
        fprintf(stderr, "[TEST] Test FAILED\n");
        return ret;
    }

    printf("\n========================================\n");
    printf("  Test PASSED!\n");
    printf("========================================\n\n");

    return 0;
}
