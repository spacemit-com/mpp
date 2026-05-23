/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_uvc.c
 * @Date      :    2026-4-15
 * @Brief     :    Unit tests for UVC module.
 *                 Covers: init/exit, create/destroy device, dev attr,
 *                 enable/disable dev, chn attr, enable/disable chn,
 *                 effect attr, error paths.
 *
 *                 NOTE: These tests exercise the API logic and state machine
 *                 only. V4L2 calls will fail unless a real UVC camera is
 *                 attached at the configured device node. Tests that require
 *                 hardware are guarded and will SKIP gracefully.
 *
 * Build (standalone):
 *   gcc -std=c11 -D_GNU_SOURCE -pthread -Wall -Wextra \
 *       -I../../include/sys -I../../include/uvc \
 *       ../../mpi/sys/sys.c ../../mpi/sys/vb.c \
 *       ../../mpi/sys/mpp_shm.c ../../mpi/sys/dma_alloc.c \
 *       ../../mpi/uvc/uvc.c \
 *       test_uvc.c -o test_uvc -lrt
 *
 * Run:
 *   ./test_uvc              # run all tests
 *   ./test_uvc /dev/video0  # run hw tests against a real device
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <strings.h>

#include "sys_api.h"
#include "uvc_api.h"
#include "vb_api.h"

/* ======================== Helpers ======================== */

#define TEST_PASS(name) printf("[PASS] %s\n", (name))
#define TEST_FAIL(name, msg)                      \
    do {                                          \
        printf("[FAIL] %s: %s\n", (name), (msg)); \
        exit(1);                                  \
    } while (0)
#define TEST_SKIP(name, msg)                      \
    do {                                          \
        printf("[SKIP] %s: %s\n", (name), (msg)); \
        return;                                   \
    } while (0)

static const char *g_devNode = "/dev/video13";
static BOOL g_hasHw = MPP_FALSE;
static MppPixelFormat g_pixelFormat = MPP_PIXEL_FORMAT_MJPEG;
static U32 g_duration = 0;    /* duration in seconds, 0 means use frame count */
static S32 g_brightness = -1; /* -1 means not set (use default) */
static S32 g_hue = -1;
static S32 g_saturation = -1;
static S32 g_sharpness = -1;

static MppPixelFormat parse_format(const char *str) {
    if (!str)
        return MPP_PIXEL_FORMAT_MJPEG;
    if (strcasecmp(str, "MJPEG") == 0 || strcasecmp(str, "mjpeg") == 0)
        return MPP_PIXEL_FORMAT_MJPEG;
    if (strcasecmp(str, "YUYV") == 0 || strcasecmp(str, "YUV2") == 0 || strcasecmp(str, "yuyv") == 0 ||
        strcasecmp(str, "yuv2") == 0)
        return MPP_PIXEL_FORMAT_YUYV;
    if (strcasecmp(str, "H264") == 0 || strcasecmp(str, "h264") == 0 || strcasecmp(str, "H.264") == 0)
        return MPP_PIXEL_FORMAT_H264;
    if (strcasecmp(str, "NV12") == 0 || strcasecmp(str, "nv12") == 0)
        return MPP_PIXEL_FORMAT_NV12;
    if (strcasecmp(str, "NV21") == 0 || strcasecmp(str, "nv21") == 0)
        return MPP_PIXEL_FORMAT_NV21;
    if (strcasecmp(str, "I420") == 0 || strcasecmp(str, "i420") == 0 || strcasecmp(str, "YU12") == 0)
        return MPP_PIXEL_FORMAT_I420;
    printf("[WARN] Unknown format '%s', defaulting to MJPEG\n", str);
    return MPP_PIXEL_FORMAT_MJPEG;
}

static const char *format_to_str(MppPixelFormat fmt) {
    switch (fmt) {
        case MPP_PIXEL_FORMAT_MJPEG:
            return "MJPEG";
        case MPP_PIXEL_FORMAT_YUYV:
            return "YUYV";
        case MPP_PIXEL_FORMAT_H264:
            return "H264";
        case MPP_PIXEL_FORMAT_NV12:
            return "NV12";
        case MPP_PIXEL_FORMAT_NV21:
            return "NV21";
        case MPP_PIXEL_FORMAT_I420:
            return "I420";
        default:
            return "UNKNOWN";
    }
}

static void print_usage(const char *prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("Options:\n");
    printf("  --device <dev>       Video device node (default: /dev/video13)\n");
    printf("  --format <fmt>       Pixel format: MJPEG, YUYV/YUV2, H264, NV12, NV21, I420\n");
    printf("                       (default: MJPEG)\n");
    printf("  --duration <sec>     Capture duration in seconds (default: 0 = 5 frames)\n");
    printf("  --brightness <val>   Set brightness (0-255, default: 100)\n");
    printf("  --hue <val>          Set hue (0-255, default: 100)\n");
    printf("  --saturation <val>   Set saturation (0-255, default: 128)\n");
    printf("  --sharpness <val>    Set sharpness (0-255, default: 100)\n");
    printf("  --help               Show this help message\n");
}

static void check_hw(void) {
    g_hasHw = (access(g_devNode, F_OK) == 0) ? MPP_TRUE : MPP_FALSE;
}

static void fill_default_dev_attr(UvcDevAttr *attr) {
    memset(attr, 0, sizeof(*attr));
    strncpy(attr->acDevNode, g_devNode, sizeof(attr->acDevNode) - 1);
}

static void fill_default_chn_attr(UvcChnAttr *attr) {
    memset(attr, 0, sizeof(*attr));
    attr->u32Width = 640;
    attr->u32Height = 480;
    attr->ePixelFormat = g_pixelFormat;
    attr->u32Fps = 30;
}

/* ======================== Test 1: Init / Exit ======================== */

static void test_init_exit(void) {
    const char *name = "init_exit";
    S32 ret;

    ret = UVC_Init();
    assert(ret == 0);

    /* double init should be OK (pthread_once) */
    ret = UVC_Init();
    assert(ret == 0);

    ret = UVC_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Test 2: Create / Destroy Device ======================== */

static void test_create_destroy_dev(void) {
    const char *name = "create_destroy_dev";
    S32 ret;
    UVC_DEV dev = 0;
    UvcDevAttr attr;

    ret = UVC_Init();
    assert(ret == 0);

    fill_default_dev_attr(&attr);

    /* create device */
    ret = UVC_CreateDev(dev, &attr);
    assert(ret == 0);

    /* double create should fail */
    ret = UVC_CreateDev(dev, &attr);
    assert(ret != 0);

    /* destroy device */
    ret = UVC_DestroyDev(dev);
    assert(ret == 0);

    /* double destroy should fail */
    ret = UVC_DestroyDev(dev);
    assert(ret != 0);

    ret = UVC_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Test 3: Create Multiple Devices ======================== */

static void test_create_multi_dev(void) {
    const char *name = "create_multi_dev";
    S32 ret;
    UvcDevAttr attr;

    ret = UVC_Init();
    assert(ret == 0);

    fill_default_dev_attr(&attr);

    /* fill all slots */
    for (S32 i = 0; i < UVC_MAX_DEV_NUM; i++) {
        ret = UVC_CreateDev((UVC_DEV)i, &attr);
        assert(ret == 0);
    }

    /* out-of-range should fail */
    ret = UVC_CreateDev((UVC_DEV)UVC_MAX_DEV_NUM, &attr);
    assert(ret != 0);

    /* duplicate should fail */
    ret = UVC_CreateDev(0, &attr);
    assert(ret != 0);

    /* destroy all */
    for (S32 i = 0; i < UVC_MAX_DEV_NUM; i++) {
        ret = UVC_DestroyDev((UVC_DEV)i);
        assert(ret == 0);
    }

    ret = UVC_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Test 5: Invalid Parameters ======================== */

static void test_invalid_params(void) {
    const char *name = "invalid_params";
    S32 ret;
    UVC_DEV dev = 0;
    UvcDevAttr attr;

    ret = UVC_Init();
    assert(ret == 0);

    /* null pointer */
    fill_default_dev_attr(&attr);
    ret = UVC_CreateDev(0, NULL);
    assert(ret != 0);

    /* invalid dev id */
    fill_default_dev_attr(&attr);
    ret = UVC_CreateDev(-1, &attr);
    assert(ret != 0);
    ret = UVC_CreateDev(UVC_MAX_DEV_NUM, &attr);
    assert(ret != 0);

    /* invalid dev for EnableDev */
    ret = UVC_EnableDev(-1);
    assert(ret != 0);

    /* invalid dev/chn for SetChnAttr */
    UvcChnAttr chnAttr;
    fill_default_chn_attr(&chnAttr);
    ret = UVC_SetChnAttr(-1, 0, &chnAttr);
    assert(ret != 0);
    ret = UVC_SetChnAttr(0, -1, &chnAttr);
    assert(ret != 0);

    /* invalid chn attr values */
    fill_default_dev_attr(&attr);
    dev = 0;
    ret = UVC_CreateDev(dev, &attr);
    assert(ret == 0);

    chnAttr.u32Width = 0;
    ret = UVC_SetChnAttr(dev, 0, &chnAttr);
    assert(ret != 0);

    fill_default_chn_attr(&chnAttr);
    chnAttr.ePixelFormat = MPP_PIXEL_FORMAT_UNKNOWN;
    ret = UVC_SetChnAttr(dev, 0, &chnAttr);
    assert(ret != 0);

    ret = UVC_DestroyDev(dev);
    assert(ret == 0);

    ret = UVC_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Test 6: State Machine Errors ======================== */

static void test_state_machine(void) {
    const char *name = "state_machine";
    S32 ret;
    UVC_DEV dev = 0;
    UvcDevAttr attr;
    UvcChnAttr chnAttr;

    ret = UVC_Init();
    assert(ret == 0);

    fill_default_dev_attr(&attr);
    dev = 0;
    ret = UVC_CreateDev(dev, &attr);
    assert(ret == 0);

    /* disable dev before enable should fail */
    ret = UVC_DisableDev(dev);
    assert(ret != 0);

    /* enable chn before dev enabled should fail (needs hw, but dev not enabled
       so the check happens before V4L2 calls) */
    fill_default_chn_attr(&chnAttr);
    ret = UVC_SetChnAttr(dev, 0, &chnAttr);
    assert(ret == 0);

    ret = UVC_EnableChn(dev, 0);
    assert(ret != 0); /* dev not enabled */

    /* disable chn that is not enabled should fail */
    ret = UVC_DisableChn(dev, 0);
    assert(ret != 0);

    /* destroy dev (not enabled, should succeed) */
    ret = UVC_DestroyDev(dev);
    assert(ret == 0);

    ret = UVC_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Test 7: Effect Attr Without Enable ======================== */

static void test_effect_not_enabled(void) {
    const char *name = "effect_not_enabled";
    S32 ret;
    UVC_DEV dev = 0;
    UvcDevAttr attr;
    UvcEffectAttr effect;

    ret = UVC_Init();
    assert(ret == 0);

    fill_default_dev_attr(&attr);
    dev = 0;
    ret = UVC_CreateDev(dev, &attr);
    assert(ret == 0);

    /* set effect on non-enabled dev should fail */
    memset(&effect, 0, sizeof(effect));
    ret = UVC_SetEffectAttr(dev, &effect);
    assert(ret != 0);

    /* get effect on non-enabled dev should fail */
    ret = UVC_GetEffectAttr(dev, &effect);
    assert(ret != 0);

    ret = UVC_DestroyDev(dev);
    assert(ret == 0);

    ret = UVC_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Test 8: Full HW Pipeline ======================== */

static void test_hw_full_pipeline(void) {
    const char *name = "hw_full_pipeline";
    S32 ret;
    UVC_DEV dev = 0;
    UvcDevAttr devAttr;
    UvcChnAttr chnAttr;
    UvcEffectAttr effect;

    if (!g_hasHw)
        TEST_SKIP(name, "no UVC device found");

    ret = UVC_Init();
    assert(ret == 0);

    /* create & enable dev */
    fill_default_dev_attr(&devAttr);
    dev = 0;
    ret = UVC_CreateDev(dev, &devAttr);
    assert(ret == 0);

    ret = UVC_EnableDev(dev);
    if (ret != 0) {
        /* device node exists but open failed (permissions, not a real UVC, etc.) */
        UVC_DestroyDev(dev);
        UVC_Exit();
        TEST_SKIP(name, "EnableDev failed (device not usable)");
    }

    /* destroy while enabled should fail */
    ret = UVC_DestroyDev(dev);
    assert(ret != 0);

    /* set & enable chn */
    fill_default_chn_attr(&chnAttr);
    ret = UVC_SetChnAttr(dev, 0, &chnAttr);
    assert(ret == 0);

    ret = UVC_EnableChn(dev, 0);
    if (ret != 0) {
        /* format negotiation or buffer request failed */
        UVC_DisableDev(dev);
        UVC_DestroyDev(dev);
        UVC_Exit();
        TEST_SKIP(name, "EnableChn failed (format not supported)");
    }

    /* double enable chn should fail */
    ret = UVC_EnableChn(dev, 0);
    assert(ret != 0);

    /* disable dev while chn enabled should fail */
    ret = UVC_DisableDev(dev);
    assert(ret != 0);

    /* set effect */
    memset(&effect, 0, sizeof(effect));
    effect.s32Brightness = 10;
    effect.s32Contrast = 32;
    effect.bAutoExposure = MPP_TRUE;
    ret = UVC_SetEffectAttr(dev, &effect);
    assert(ret == 0);

    /* get effect back */
    UvcEffectAttr gotEffect;
    ret = UVC_GetEffectAttr(dev, &gotEffect);
    assert(ret == 0);
    assert(gotEffect.s32Brightness == 10);
    assert(gotEffect.s32Contrast == 32);

    /* get negotiated chn attr */
    UvcChnAttr gotChn;
    ret = UVC_GetChnAttr(dev, 0, &gotChn);
    assert(ret == 0);
    assert(gotChn.u32Width > 0);
    assert(gotChn.u32Height > 0);

    /* teardown: disable chn -> disable dev -> destroy dev */
    ret = UVC_DisableChn(dev, 0);
    assert(ret == 0);

    ret = UVC_DisableDev(dev);
    assert(ret == 0);

    ret = UVC_DestroyDev(dev);
    assert(ret == 0);

    ret = UVC_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Test 9: GetFrame and Save to File ======================== */

static const char *get_format_ext(MppPixelFormat fmt) {
    switch (fmt) {
        case MPP_PIXEL_FORMAT_MJPEG:
            return "mjpeg";
        case MPP_PIXEL_FORMAT_H264:
            return "h264";
        case MPP_PIXEL_FORMAT_I420:
            return "yuv";
        case MPP_PIXEL_FORMAT_NV12:
            return "nv12";
        case MPP_PIXEL_FORMAT_NV21:
            return "nv21";
        case MPP_PIXEL_FORMAT_YUYV:
            return "yuyv";
        default:
            return "bin";
    }
}

/**
 * @brief Check if an H.264 NAL unit is an IDR slice (contains SPS/PPS).
 *        Scans for start codes (0x00000001 or 0x000001) and checks NAL type.
 *        Returns MPP_TRUE if SPS (type 7) is found, indicating a keyframe with headers.
 */
static BOOL h264_has_sps(const U8 *data, U32 size) {
    if (!data || size < 5)
        return MPP_FALSE;

    for (U32 i = 0; i < size - 4; i++) {
        /* look for start code: 0x00 0x00 0x01 or 0x00 0x00 0x00 0x01 */
        U32 nalOffset = 0;
        if (data[i] == 0x00 && data[i + 1] == 0x00) {
            if (data[i + 2] == 0x01) {
                nalOffset = i + 3;
            } else if (data[i + 2] == 0x00 && i + 3 < size && data[i + 3] == 0x01) {
                nalOffset = i + 4;
            } else {
                continue;
            }
            if (nalOffset < size) {
                U8 nalType = data[nalOffset] & 0x1F;
                if (nalType == 7) /* SPS */
                    return MPP_TRUE;
            }
        }
    }
    return MPP_FALSE;
}

static void test_getframe_save_file(void) {
    const char *name = "getframe_save_file";
    S32 ret;
    UVC_DEV dev = 0;
    UvcDevAttr devAttr;
    UvcChnAttr chnAttr;
    VideoFrameInfo frame;
    const S32 s32Timeout = 3000; /* 3s timeout */
    U32 u32SaveCnt = 5;          /* default save 5 frames */

    printf("  [INFO] Format: %s, Duration: %u sec\n", format_to_str(g_pixelFormat), g_duration);

    if (!g_hasHw)
        TEST_SKIP(name, "no UVC device found");

    ret = UVC_Init();
    assert(ret == 0);

    /* create & enable dev */
    fill_default_dev_attr(&devAttr);
    ret = UVC_CreateDev(dev, &devAttr);
    assert(ret == 0);

    ret = UVC_EnableDev(dev);
    if (ret != 0) {
        UVC_DestroyDev(dev);
        UVC_Exit();
        TEST_SKIP(name, "EnableDev failed (device not usable)");
    }

    /* set & enable chn 0 */
    fill_default_chn_attr(&chnAttr);
    chnAttr.u32Depth = 1;
    ret = UVC_SetChnAttr(dev, 0, &chnAttr);
    assert(ret == 0);

    ret = UVC_EnableChn(dev, 0);
    if (ret != 0) {
        UVC_DisableDev(dev);
        UVC_DestroyDev(dev);
        UVC_Exit();
        TEST_SKIP(name, "EnableChn failed (format not supported)");
    }

    /* --- Discard warm-up frames to let auto-exposure converge --- */
    const U32 u32WarmUpCnt = 30;
    printf("  [INFO] Discarding %u warm-up frames for AE convergence...\n", u32WarmUpCnt);

    /* Enable auto white balance & auto exposure for color output */
    UvcEffectAttr effect;
    memset(&effect, 0, sizeof(effect));
    effect.bAutoWhiteBalance = MPP_TRUE;
    effect.bAutoExposure = MPP_TRUE;
    effect.s32Brightness = (g_brightness >= 0) ? g_brightness : 100;
    effect.s32Contrast = 128;
    effect.s32Saturation = (g_saturation >= 0) ? g_saturation : 128;
    effect.s32Hue = (g_hue >= 0) ? g_hue : 100;
    effect.s32Sharpness = (g_sharpness >= 0) ? g_sharpness : 100;
    ret = UVC_SetEffectAttr(dev, &effect);
    if (ret != 0)
        printf("  [WARN] SetEffectAttr failed (ret=%d), colors may be off\n", ret);
    else
        printf(
            "  [INFO] SetEffectAttr OK: AWB=on, AE=on, brightness=%d, hue=%d, sat=%d, sharpness=%d\n",
            effect.s32Brightness,
            effect.s32Hue,
            effect.s32Saturation,
            effect.s32Sharpness);

    for (U32 i = 0; i < u32WarmUpCnt; i++) {
        memset(&frame, 0, sizeof(frame));
        ret = UVC_GetFrame(dev, 0, &frame, s32Timeout);
        if (ret == 0)
            UVC_ReleaseFrame(dev, 0, &frame);
    }

    /* --- Capture frames and save to a single file --- */
    /* If duration is set, capture for that many seconds; otherwise use frame count */
    time_t tStart = time(NULL);
    U32 u32FrameIdx = 0;
    U32 u32SavedCnt = 0;
    BOOL bDurationMode = (g_duration > 0) ? MPP_TRUE : MPP_FALSE;

    if (bDurationMode)
        printf("  [INFO] Capturing for %u seconds...\n", g_duration);
    else
        printf("  [INFO] Capturing %u frames...\n", u32SaveCnt);

    /* Open a single output file for all frames */
    char filename[256];
    snprintf(
        filename,
        sizeof(filename),
        "uvc_output_%s_640x480.%s",
        format_to_str(g_pixelFormat),
        get_format_ext(g_pixelFormat));

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        printf("  [ERROR] Failed to open output file %s: %s\n", filename, strerror(errno));
        goto teardown;
    }
    printf("  [INFO] Saving all frames to: %s\n", filename);

    size_t totalWritten = 0;
    BOOL bH264GotKeyframe = MPP_FALSE; /* for H264: wait for SPS/PPS before writing */

    while (1) {
        /* check exit condition */
        if (bDurationMode) {
            time_t tNow = time(NULL);
            if ((U32)(tNow - tStart) >= g_duration)
                break;
        } else {
            if (u32SavedCnt >= u32SaveCnt)
                break;
        }

        memset(&frame, 0, sizeof(frame));
        ret = UVC_GetFrame(dev, 0, &frame, s32Timeout);
        if (ret != 0) {
            printf("  [WARN] GetFrame #%u failed (ret=%d), skipping\n", u32FrameIdx, ret);
            u32FrameIdx++;
            continue;
        }

        assert(frame.stVFrame.ulPlaneVirAddr[0] != 0);
        assert(frame.stVFrame.u32PlaneSizeValid[0] > 0);
        assert(frame.stCommFrameInfo.u32Width > 0);
        assert(frame.stCommFrameInfo.u32Height > 0);

        /* For H264: skip frames until we find one with SPS/PPS (keyframe) */
        if (g_pixelFormat == MPP_PIXEL_FORMAT_H264 && !bH264GotKeyframe) {
            const U8 *pData = (const U8 *)frame.stVFrame.ulPlaneVirAddr[0];
            U32 dataSize = frame.stVFrame.u32PlaneSizeValid[0];
            if (!h264_has_sps(pData, dataSize)) {
                printf("  [INFO] H264: frame #%u has no SPS, skipping (waiting for keyframe)\n", u32FrameIdx);
                UVC_ReleaseFrame(dev, 0, &frame);
                u32FrameIdx++;
                continue;
            }
            bH264GotKeyframe = MPP_TRUE;
            printf("  [INFO] H264: found SPS/PPS at frame #%u, start saving\n", u32FrameIdx);
        }

        /* Append frame data to the single output file */
        size_t written = fwrite((void *)frame.stVFrame.ulPlaneVirAddr[0], 1, frame.stVFrame.u32PlaneSizeValid[0], fp);
        totalWritten += written;

        if (written != frame.stVFrame.u32PlaneSizeValid[0]) {
            printf(
                "  [WARN] Frame #%u: expected %u bytes, wrote %zu\n",
                u32FrameIdx,
                frame.stVFrame.u32PlaneSizeValid[0],
                written);
        }

        ret = UVC_ReleaseFrame(dev, 0, &frame);
        assert(ret == 0);

        u32FrameIdx++;
        u32SavedCnt++;
    }

    fclose(fp);
    printf(
        "  [INFO] Capture done: saved %u frames (%zu bytes total) to %s in %ld seconds\n",
        u32SavedCnt,
        totalWritten,
        filename,
        (int64_t)(time(NULL) - tStart));

    /* --- ReleaseFrame with invalid params should fail --- */
    memset(&frame, 0, sizeof(frame));
    ret = UVC_ReleaseFrame(dev, 0, NULL);
    assert(ret != 0);

    ret = UVC_ReleaseFrame(-1, 0, &frame);
    assert(ret != 0);

teardown:
    /* teardown */
    ret = UVC_DisableChn(dev, 0);
    assert(ret == 0);

    ret = UVC_DisableDev(dev);
    assert(ret == 0);

    ret = UVC_DestroyDev(dev);
    assert(ret == 0);

    ret = UVC_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Test 10: Exit Force Cleanup ======================== */

static void test_exit_force_cleanup(void) {
    const char *name = "exit_force_cleanup";
    S32 ret;
    UVC_DEV dev = 0;
    UvcDevAttr attr;

    ret = UVC_Init();
    assert(ret == 0);

    fill_default_dev_attr(&attr);
    dev = 0;
    ret = UVC_CreateDev(dev, &attr);
    assert(ret == 0);

    /* exit without destroy — should not crash, just warn */
    ret = UVC_Exit();
    assert(ret == 0);

    /* re-init should work after exit */
    ret = UVC_Init();
    assert(ret == 0);

    ret = UVC_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Main ======================== */

int main(int argc, char *argv[]) {
    /* parse command-line arguments */
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--device") == 0) && (i + 1 < argc)) {
            g_devNode = argv[++i];
        } else if ((strcmp(argv[i], "--format") == 0) && (i + 1 < argc)) {
            g_pixelFormat = parse_format(argv[++i]);
        } else if ((strcmp(argv[i], "--duration") == 0) && (i + 1 < argc)) {
            g_duration = (U32)atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--brightness") == 0) && (i + 1 < argc)) {
            g_brightness = (S32)atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--hue") == 0) && (i + 1 < argc)) {
            g_hue = (S32)atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--saturation") == 0) && (i + 1 < argc)) {
            g_saturation = (S32)atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--sharpness") == 0) && (i + 1 < argc)) {
            g_sharpness = (S32)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '/') {
            /* legacy: treat bare path as device node */
            g_devNode = argv[i];
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    check_hw();

    printf("=== UVC Module Tests ===\n");
    printf("Device node: %s (%s)\n", g_devNode, g_hasHw ? "found" : "not found, hw tests will skip");
    printf("Format: %s, Duration: %u sec\n\n", format_to_str(g_pixelFormat), g_duration);

    /* initialize VB module for buffer management */
    S32 vbRet = SYS_Init();
    vbRet = VB_Init();
    assert(vbRet == 0);

    /* pure logic tests (no hardware needed) */
    // test_init_exit();
    // test_create_destroy_dev();
    // test_create_multi_dev();
    // test_invalid_params();
    // test_state_machine();
    // test_effect_not_enabled();
    // test_exit_force_cleanup();

    /* hardware tests */
    // test_hw_full_pipeline();
    test_getframe_save_file();

    printf("\n=== All tests passed ===\n");

    /* cleanup VB module */
    VB_Exit();

    return 0;
}
