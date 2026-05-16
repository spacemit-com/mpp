/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    test_vi_isp_cam_api.c
* @Date      :    2026-04-28
* @Brief     :    Standard ISP camera API demo based on MPP VI.
*
* This file intentionally keeps the public-facing camera API in the same source
* as the demo so integrators can copy the small isp_cam_* wrapper into their
* application or ROS2 node directly.
*------------------------------------------------------------------------------
*/

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sys_api.h"
#include "vb_api.h"
#include "vi_api.h"

#define ISP_CAM_DEFAULT_DEV          0
#define ISP_CAM_DEFAULT_CHN          0
#define ISP_CAM_SENSOR_WIDTH         3864
#define ISP_CAM_SENSOR_HEIGHT        2192
#define ISP_CAM_DEFAULT_WIDTH        1920
#define ISP_CAM_DEFAULT_HEIGHT       1080
#define ISP_CAM_DEFAULT_MIPI_LANES   4
#define ISP_CAM_DEFAULT_MBPS         800
#define ISP_CAM_DEFAULT_TIMEOUT_MS   1000
#define ISP_CAM_DEFAULT_FRAME_COUNT  30
#define ISP_CAM_MAX_PLANES           3

#define ISP_CAM_LOGE(fmt, ...) printf("[isp-cam][ERR] " fmt, ## __VA_ARGS__)
#define ISP_CAM_LOGI(fmt, ...) printf("[isp-cam][INFO] " fmt, ## __VA_ARGS__)
#define ISP_CAM_LOGD(fmt, ...) do { if (g_isp_cam_verbose) printf("[isp-cam][DBG] " fmt, ## __VA_ARGS__); } while (0)

static int g_isp_cam_verbose = 0;

typedef struct _IspCamConfig {
    VI_DEV viDev;
    VI_CHN viChn;
    U32 sensorWidth;
    U32 sensorHeight;
    U32 outputWidth;
    U32 outputHeight;
    U32 mipiLaneNum;
    U32 mbps;
    MppPixelFormat pixelFormat;
    S32 timeoutMs;
    BOOL enableMeta;
} IspCamConfig;

typedef struct _IspCamHandle {
    IspCamConfig cfg;
    BOOL sysInited;
    BOOL vbInited;
    BOOL viInited;
    BOOL devEnabled;
    BOOL chnEnabled;
} IspCamHandle;

typedef struct _IspCamFrame {
    VideoFrameInfo frame;
    ViFrameMetaInfo meta;
    BOOL hasMeta;
} IspCamFrame;

static const char *isp_cam_pixel_format_name(MppPixelFormat pixelFormat)
{
    switch (pixelFormat) {
    case MPP_PIXEL_FORMAT_NV12:
        return "NV12";
    case MPP_PIXEL_FORMAT_NV21:
        return "NV21";
    case MPP_PIXEL_FORMAT_RGB_BAYER_8BITS:
        return "BAYER8";
    case MPP_PIXEL_FORMAT_RGB_BAYER_10BITS:
        return "BAYER10";
    case MPP_PIXEL_FORMAT_RGB_BAYER_12BITS:
        return "BAYER12";
    case MPP_PIXEL_FORMAT_RGB_BAYER_14BITS:
        return "BAYER14";
    case MPP_PIXEL_FORMAT_RGB_BAYER_16BITS:
        return "BAYER16";
    case MPP_PIXEL_FORMAT_RGB_BAYER_20BITS:
        return "BAYER20";
    default:
        return "UNKNOWN";
    }
}

static void isp_cam_default_config(IspCamConfig *cfg)
{
    if (cfg == NULL) {
        return;
    }

    memset(cfg, 0, sizeof(*cfg));
    cfg->viDev = ISP_CAM_DEFAULT_DEV;
    cfg->viChn = ISP_CAM_DEFAULT_CHN;
    cfg->sensorWidth = ISP_CAM_SENSOR_WIDTH;
    cfg->sensorHeight = ISP_CAM_SENSOR_HEIGHT;
    cfg->outputWidth = ISP_CAM_DEFAULT_WIDTH;
    cfg->outputHeight = ISP_CAM_DEFAULT_HEIGHT;
    cfg->mipiLaneNum = ISP_CAM_DEFAULT_MIPI_LANES;
    cfg->mbps = ISP_CAM_DEFAULT_MBPS;
    cfg->pixelFormat = MPP_PIXEL_FORMAT_NV12;
    cfg->timeoutMs = ISP_CAM_DEFAULT_TIMEOUT_MS;
    cfg->enableMeta = MPP_TRUE;
}

static void isp_cam_dump_config(const IspCamConfig *cfg)
{
    if (cfg == NULL) {
        return;
    }

    ISP_CAM_LOGI("config: dev=%d chn=%d sensor=%ux%u output=%ux%u lanes=%u mbps=%u fmt=%s timeout=%dms meta=%s\n",
        cfg->viDev,
        cfg->viChn,
        cfg->sensorWidth,
        cfg->sensorHeight,
        cfg->outputWidth,
        cfg->outputHeight,
        cfg->mipiLaneNum,
        cfg->mbps,
        isp_cam_pixel_format_name(cfg->pixelFormat),
        cfg->timeoutMs,
        cfg->enableMeta ? "on" : "off");
}

static S32 isp_cam_open(IspCamHandle *cam, const IspCamConfig *cfg)
{
    S32 ret;
    ViDevAttrS devAttr;
    ViChnAttrS chnAttr;

    if (cam == NULL || cfg == NULL) {
        return -1;
    }

    memset(cam, 0, sizeof(*cam));
    cam->cfg = *cfg;

    ret = SYS_Init();
    if (ret != 0) {
        ISP_CAM_LOGE("SYS_Init failed, ret=%d\n", ret);
        return ret;
    }
    cam->sysInited = MPP_TRUE;

    ret = VB_Init();
    if (ret != 0) {
        ISP_CAM_LOGE("VB_Init failed, ret=%d\n", ret);
        goto fail;
    }
    cam->vbInited = MPP_TRUE;

    ret = VI_Init();
    if (ret != 0) {
        ISP_CAM_LOGE("VI_Init failed, ret=%d\n", ret);
        goto fail;
    }
    cam->viInited = MPP_TRUE;

    memset(&devAttr, 0, sizeof(devAttr));
    devAttr.eWorkMode = VI_WORK_MODE_ONLINE;
    devAttr.u32Width = cfg->sensorWidth;
    devAttr.u32Height = cfg->sensorHeight;
    devAttr.u32MipiLaneNum = cfg->mipiLaneNum;
    devAttr.u32mbps = cfg->mbps;
    devAttr.bCapture2Preview = MPP_FALSE;

    ret = VI_SetDevAttr(cfg->viDev, &devAttr);
    if (ret != 0) {
        ISP_CAM_LOGE("VI_SetDevAttr failed, ret=%d\n", ret);
        goto fail;
    }

    memset(&chnAttr, 0, sizeof(chnAttr));
    chnAttr.eChnType = VI_CHN_TYPE_PHYSICAL;
    chnAttr.ePixelFormat = cfg->pixelFormat;
    chnAttr.u32Width = cfg->outputWidth;
    chnAttr.u32Height = cfg->outputHeight;
    chnAttr.bMirror = MPP_FALSE;
    chnAttr.bFlip = MPP_FALSE;
    chnAttr.eRotateMode = VI_ROT_0;
    chnAttr.bCropEnable = MPP_FALSE;
    chnAttr.eStrideAlign = VI_STRIDE_ALIGN_DEFAULT;

    ret = VI_SetChnAttr(cfg->viDev, cfg->viChn, &chnAttr);
    if (ret != 0) {
        ISP_CAM_LOGE("VI_SetChnAttr failed, ret=%d\n", ret);
        goto fail;
    }

    isp_cam_dump_config(cfg);
    return 0;

fail:
    if (cam->viInited) {
        (void)VI_DeInit();
    }
    if (cam->vbInited) {
        (void)VB_Exit();
    }
    if (cam->sysInited) {
        (void)SYS_Exit();
    }
    memset(cam, 0, sizeof(*cam));
    return ret;
}

static S32 isp_cam_start(IspCamHandle *cam)
{
    S32 ret;

    if (cam == NULL || !cam->viInited) {
        return -1;
    }

    ret = VI_EnableDev(cam->cfg.viDev);
    if (ret != 0) {
        ISP_CAM_LOGE("VI_EnableDev failed, ret=%d\n", ret);
        return ret;
    }
    cam->devEnabled = MPP_TRUE;

    ret = VI_EnableChn(cam->cfg.viDev, cam->cfg.viChn);
    if (ret != 0) {
        ISP_CAM_LOGE("VI_EnableChn failed, ret=%d\n", ret);
        (void)VI_DisableDev(cam->cfg.viDev);
        cam->devEnabled = MPP_FALSE;
        return ret;
    }
    cam->chnEnabled = MPP_TRUE;

    ISP_CAM_LOGI("camera started\n");
    return 0;
}

static S32 isp_cam_get_frame(IspCamHandle *cam, IspCamFrame *outFrame, S32 timeoutMs)
{
    S32 ret;
    // U32 frameId;

    if (cam == NULL || outFrame == NULL || !cam->chnEnabled) {
        return -1;
    }

    memset(outFrame, 0, sizeof(*outFrame));
    ret = VI_GetChnFrame(cam->cfg.viDev, cam->cfg.viChn, &outFrame->frame, timeoutMs);
    if (ret != 0) {
        return ret;
    }

    // frameId = outFrame->frame.u32Idx;

    // if (cam->cfg.enableMeta == MPP_TRUE) {

    //     ret = VI_QueryFrameMeta(cam->cfg.viDev, cam->cfg.viChn, frameId, &outFrame->meta);
    //     if (ret == 0) {
    //         outFrame->hasMeta = MPP_TRUE;
    //     } else {
    //         ISP_CAM_LOGD("VI_QueryFrameMeta frameId=%u failed, ret=%d\n", frameId, ret);
    //     }
    // }

    return 0;
}

static S32 isp_cam_release_frame(IspCamHandle *cam, const IspCamFrame *frame)
{
    if (cam == NULL || frame == NULL || !cam->chnEnabled) {
        return -1;
    }

    return VI_ReleaseChnFrame(cam->cfg.viDev, cam->cfg.viChn, &frame->frame);
}

static void isp_cam_stop(IspCamHandle *cam)
{
    if (cam == NULL) {
        return;
    }

    if (cam->chnEnabled) {
        (void)VI_DisableChn(cam->cfg.viDev, cam->cfg.viChn);
        cam->chnEnabled = MPP_FALSE;
    }
    if (cam->devEnabled) {
        (void)VI_DisableDev(cam->cfg.viDev);
        cam->devEnabled = MPP_FALSE;
    }
    ISP_CAM_LOGI("camera stopped\n");
}

static void isp_cam_close(IspCamHandle *cam)
{
    if (cam == NULL) {
        return;
    }

    isp_cam_stop(cam);

    if (cam->viInited) {
        (void)VI_DeInit();
        cam->viInited = MPP_FALSE;
    }
    if (cam->vbInited) {
        (void)VB_Exit();
        cam->vbInited = MPP_FALSE;
    }
    if (cam->sysInited) {
        (void)SYS_Exit();
        cam->sysInited = MPP_FALSE;
    }
}

static void isp_cam_dump_frame(const IspCamFrame *frame)
{
    const VideoFrameInfo *vf;
    const CommonFrameInfo *common;

    if (frame == NULL) {
        return;
    }

    vf = &frame->frame;
    common = &vf->stViFrameInfo.stCommFrameInfo;
    ISP_CAM_LOGI("frame idx=%u pts=%" PRIu64 " size=%ux%u fmt=%s total=%u planes=%u fd0=%u vir0=%p valid0=%u\n",
        vf->u32Idx,
        (uint64_t)vf->stVFrame.u64PTS,
        common->u32Width,
        common->u32Height,
        isp_cam_pixel_format_name(common->ePixelFormat),
        vf->stVFrame.u32TotalSize,
        vf->stVFrame.u32PlaneNum,
        (unsigned int)vf->stVFrame.u32Fd[0],
        (void *)(uintptr_t)vf->stVFrame.ulPlaneVirAddr[0],
        vf->stVFrame.u32PlaneSizeValid[0]);

    if (frame->hasMeta == MPP_TRUE) {
        ISP_CAM_LOGI("meta frameId=%u expLine[0]=%u again[0]=%u dgain[0]=%u ispDgain[0]=%u ctemp=%u rgain=%u bgain=%u aeStable=%u awbStable=%u\n",
            frame->meta.u32FrameId,
            frame->meta.u32ExpLine[0],
            frame->meta.u32Again[0],
            frame->meta.u32Dgain[0],
            frame->meta.u32IspDgain[0],
            frame->meta.u32ColorTemp,
            frame->meta.u32RGain,
            frame->meta.u32BGain,
            frame->meta.u8AeStable,
            frame->meta.u8AwbStable);
    }
}

static S32 isp_cam_save_frame(const char *path, const IspCamFrame *frame)
{
    FILE *fp;
    U32 plane;

    if (path == NULL || frame == NULL) {
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        ISP_CAM_LOGE("fopen %s failed: %s\n", path, strerror(errno));
        return -1;
    }

    for (plane = 0; plane < frame->frame.stVFrame.u32PlaneNum && plane < ISP_CAM_MAX_PLANES; ++plane) {
        const void *addr = (const void *)(uintptr_t)frame->frame.stVFrame.ulPlaneVirAddr[plane];
        U32 size = frame->frame.stVFrame.u32PlaneSizeValid[plane];

        if (addr == NULL || size == 0) {
            continue;
        }
        if (fwrite(addr, 1, size, fp) != size) {
            ISP_CAM_LOGE("fwrite %s plane=%u size=%u failed\n", path, plane, size);
            (void)fclose(fp);
            return -1;
        }
    }

    (void)fclose(fp);
    ISP_CAM_LOGI("saved frame to %s\n", path);
    return 0;
}

static void isp_cam_print_usage(const char *prog)
{
    printf("Usage: %s [frame_count] [output_width] [output_height] [options]\n", prog);
    printf("\n");
    printf("Options:\n");
    printf("  --dev N             VI device id, default %d\n", ISP_CAM_DEFAULT_DEV);
    printf("  --chn N             VI channel id, default %d\n", ISP_CAM_DEFAULT_CHN);
    printf("  --sensor WxH        sensor size, default %ux%u\n", ISP_CAM_SENSOR_WIDTH, ISP_CAM_SENSOR_HEIGHT);
    printf("  --lanes N           MIPI lane number, default %d\n", ISP_CAM_DEFAULT_MIPI_LANES);
    printf("  --mbps N            MIPI speed, default %d\n", ISP_CAM_DEFAULT_MBPS);
    printf("  --timeout MS        get frame timeout, default %d\n", ISP_CAM_DEFAULT_TIMEOUT_MS);
    printf("  --no-meta           do not query ISP frame metadata\n");
    printf("  --save PATH         save the last frame, NV12/NV21 as raw yuv\n");
    printf("  -v, --verbose       print debug logs\n");
    printf("  -h, --help          show this help\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s 30 1920 1080 --save isp_cam_last.yuv\n", prog);
    printf("  %s 300 1280 720 --sensor 3864x2192 --lanes 4 --mbps 800\n", prog);
}

static int isp_cam_parse_size(const char *str, U32 *width, U32 *height)
{
    unsigned int w;
    unsigned int h;

    if (str == NULL || width == NULL || height == NULL) {
        return -1;
    }

    if (sscanf(str, "%ux%u", &w, &h) != 2 && sscanf(str, "%uX%u", &w, &h) != 2) {
        return -1;
    }
    if (w == 0 || h == 0) {
        return -1;
    }

    *width = (U32)w;
    *height = (U32)h;
    return 0;
}

static int isp_cam_parse_args(int argc, char **argv, IspCamConfig *cfg, U32 *frameCount, const char **savePath)
{
    int i;

    if (cfg == NULL || frameCount == NULL || savePath == NULL) {
        return -1;
    }

    isp_cam_default_config(cfg);
    *frameCount = ISP_CAM_DEFAULT_FRAME_COUNT;
    *savePath = NULL;

    if (argc > 1 && argv[1][0] != '-') {
        *frameCount = (U32)atoi(argv[1]);
        if (*frameCount == 0) {
            *frameCount = ISP_CAM_DEFAULT_FRAME_COUNT;
        }
    }
    if (argc > 2 && argv[2][0] != '-') {
        cfg->outputWidth = (U32)atoi(argv[2]);
    }
    if (argc > 3 && argv[3][0] != '-') {
        cfg->outputHeight = (U32)atoi(argv[3]);
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            return 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            g_isp_cam_verbose = 1;
        } else if (strcmp(argv[i], "--no-meta") == 0) {
            cfg->enableMeta = MPP_FALSE;
        } else if (strcmp(argv[i], "--dev") == 0 && i + 1 < argc) {
            cfg->viDev = (VI_DEV)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--chn") == 0 && i + 1 < argc) {
            cfg->viChn = (VI_CHN)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--sensor") == 0 && i + 1 < argc) {
            if (isp_cam_parse_size(argv[++i], &cfg->sensorWidth, &cfg->sensorHeight) != 0) {
                return -1;
            }
        } else if (strcmp(argv[i], "--lanes") == 0 && i + 1 < argc) {
            cfg->mipiLaneNum = (U32)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--mbps") == 0 && i + 1 < argc) {
            cfg->mbps = (U32)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc) {
            cfg->timeoutMs = (S32)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--save") == 0 && i + 1 < argc) {
            *savePath = argv[++i];
        }
    }

    if (cfg->outputWidth == 0 || cfg->outputHeight == 0 || cfg->sensorWidth == 0 || cfg->sensorHeight == 0) {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    IspCamConfig cfg;
    IspCamHandle cam;
    IspCamFrame frame;
    U32 frameCount;
    U32 i;
    S32 ret;
    const char *savePath;

    ret = isp_cam_parse_args(argc, argv, &cfg, &frameCount, &savePath);
    if (ret != 0) {
        isp_cam_print_usage(argv[0]);
        return ret > 0 ? 0 : ret;
    }

    ret = isp_cam_open(&cam, &cfg);
    if (ret != 0) {
        return ret;
    }

    ret = isp_cam_start(&cam);
    if (ret != 0) {
        isp_cam_close(&cam);
        return ret;
    }

    for (i = 0; i < frameCount; ++i) {
        memset(&frame, 0, sizeof(frame));
        ret = isp_cam_get_frame(&cam, &frame, cfg.timeoutMs);
        if (ret != 0) {
            if (ret == -4) {
                ISP_CAM_LOGI("frame %u/%u timeout, retry after 100ms\n", i + 1, frameCount);
                usleep(100 * 1000);
                continue;
            }
            ISP_CAM_LOGE("isp_cam_get_frame failed, ret=%d at frame=%u\n", ret, i);
            break;
        }

        isp_cam_dump_frame(&frame);
        if (savePath != NULL && i + 1 == frameCount) {
            (void)isp_cam_save_frame(savePath, &frame);
        }

        ret = isp_cam_release_frame(&cam, &frame);
        if (ret != 0) {
            ISP_CAM_LOGE("isp_cam_release_frame failed, ret=%d at frame=%u\n", ret, i);
            break;
        }
    }

    isp_cam_close(&cam);
    if (ret == 0) {
        ISP_CAM_LOGI("ISP camera API demo PASS\n");
    } else {
        ISP_CAM_LOGE("ISP camera API demo FAIL ret=%d\n", ret);
    }
    return ret;
}
