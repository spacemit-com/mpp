/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_vi_venc_file.c
 * @Date      :    2026-04-30
 * @Brief     :    VI -> VENC -> file demo.
 *                 Capture frames from K1 VI physical channel, send them to the
 *                 encoder, and save the encoded bitstream to a file.
 *
 * Usage:
 *   ./test_vi_venc_file <output_file> [codec] [frames]
 *
 * Examples:
 *   ./test_vi_venc_file ./out.h264 h264 30
 *   ./test_vi_venc_file ./out.h265 h265 60
 *   ./test_vi_venc_file ./out.mjpg mjpeg 1
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sys_api.h"
#include "vb_api.h"
#include "vi_api.h"
#include "venc/venc_api.h"

#define DEMO_DEFAULT_DEV 0
#define DEMO_DEFAULT_PHY_CHN 0
#define DEMO_DEFAULT_TIMEOUT_MS 1000
#define DEMO_DEFAULT_FRAME_COUNT 30
#define DEMO_DEFAULT_WIDTH 1920U
#define DEMO_DEFAULT_HEIGHT 1080U
#define DEMO_DEFAULT_BITRATE 4000000U
#define DEMO_DEFAULT_FRAMERATE 30U
#define DEMO_DEFAULT_GOP 30U
#define DEMO_DEFAULT_ALIGN 16U
#define DEMO_DEFAULT_VENC_CHN 0
#define DEMO_STARTUP_WAIT_US (200 * 1000)
#define DEMO_RETRY_INTERVAL_US (33 * 1000)
#define DEMO_VENC_TIMEOUT_MS 3000

#define LOG_INFO(fmt, ...) printf("[INFO] " fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) printf("[WARN] " fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "[ERROR] " fmt, ##__VA_ARGS__)

typedef struct _ViVencDemoConfig {
    VI_DEV ViDev;
    VI_CHN ViChn;
    S32 vencChn;
    S32 s32TimeoutMs;
    U32 u32FrameCount;
    const char *pszOutputPath;
    MppStreamCodecType eCodecType;
    ViDevAttrS stDevAttr;
    ViChnAttrS stViChnAttr;
    VencChnAttr stVencAttr;
} ViVencDemoConfig;

static void usage(const char *prog) {
    fprintf(
        stderr,
        "Usage: %s <output_file> [codec] [frames]\n"
        "  codec : h264 | h265 | mjpeg (default: h264)\n"
        "  frames: number of frames to capture and encode (default: 30)\n",
        prog);
}

static MppStreamCodecType parse_codec(const char *codec) {
    if (codec == NULL || strcmp(codec, "h264") == 0)
        return MPP_STREAM_CODEC_H264;
    if (strcmp(codec, "h265") == 0)
        return MPP_STREAM_CODEC_H265;
    if (strcmp(codec, "mjpeg") == 0 || strcmp(codec, "jpeg") == 0)
        return MPP_STREAM_CODEC_MJPEG;
    return MPP_STREAM_CODEC_UNKNOWN;
}

static const char *codec_name(MppStreamCodecType codec) {
    switch (codec) {
        case MPP_STREAM_CODEC_H264:
            return "H.264";
        case MPP_STREAM_CODEC_H265:
            return "H.265";
        case MPP_STREAM_CODEC_MJPEG:
            return "MJPEG";
        default:
            return "UNKNOWN";
    }
}

static BOOL is_vi_no_frame_error(S32 ret) {
    return (ret == -4) ? MPP_TRUE : MPP_FALSE;
}

static void init_config(ViVencDemoConfig *pstCfg) {
    if (pstCfg == NULL)
        return;

    memset(pstCfg, 0, sizeof(*pstCfg));
    pstCfg->ViDev = DEMO_DEFAULT_DEV;
    pstCfg->ViChn = DEMO_DEFAULT_PHY_CHN;
    pstCfg->vencChn = DEMO_DEFAULT_VENC_CHN;
    pstCfg->s32TimeoutMs = DEMO_DEFAULT_TIMEOUT_MS;
    pstCfg->u32FrameCount = DEMO_DEFAULT_FRAME_COUNT;
    pstCfg->eCodecType = MPP_STREAM_CODEC_H264;

    pstCfg->stDevAttr.eWorkMode = VI_WORK_MODE_ONLINE;
    pstCfg->stDevAttr.u32Width = 3864;
    pstCfg->stDevAttr.u32Height = 2192;
    pstCfg->stDevAttr.u32MipiLaneNum = 4;
    pstCfg->stDevAttr.bCapture2Preview = MPP_FALSE;

    pstCfg->stViChnAttr.eChnType = VI_CHN_TYPE_PHYSICAL;
    pstCfg->stViChnAttr.ePixelFormat = MPP_PIXEL_FORMAT_NV12;
    pstCfg->stViChnAttr.u32Width = DEMO_DEFAULT_WIDTH;
    pstCfg->stViChnAttr.u32Height = DEMO_DEFAULT_HEIGHT;
    pstCfg->stViChnAttr.eStrideAlign = VI_STRIDE_ALIGN_16;

    pstCfg->stVencAttr.eCodecType = MPP_STREAM_CODEC_H264;
    pstCfg->stVencAttr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    pstCfg->stVencAttr.u32Width = DEMO_DEFAULT_WIDTH;
    pstCfg->stVencAttr.u32Height = DEMO_DEFAULT_HEIGHT;
    pstCfg->stVencAttr.u32Align = DEMO_DEFAULT_ALIGN;
    pstCfg->stVencAttr.u32Bitrate = DEMO_DEFAULT_BITRATE;
    pstCfg->stVencAttr.u32FrameRate = DEMO_DEFAULT_FRAMERATE;
    pstCfg->stVencAttr.u32Gop = DEMO_DEFAULT_GOP;
    pstCfg->stVencAttr.eRcMode = VENC_RC_MODE_CBR;
}

static S32 setup_vi(const ViVencDemoConfig *pstCfg) {
    S32 ret;

    ret = VI_Init();
    if (ret != 0) {
        LOG_ERROR("VI_Init failed: %d\n", ret);
        return ret;
    }

    ret = VI_SetDevAttr(pstCfg->ViDev, &pstCfg->stDevAttr);
    if (ret != 0) {
        LOG_ERROR("VI_SetDevAttr failed: %d\n", ret);
        return ret;
    }

    ret = VI_SetChnAttr(pstCfg->ViDev, pstCfg->ViChn, &pstCfg->stViChnAttr);
    if (ret != 0) {
        LOG_ERROR("VI_SetChnAttr failed: %d\n", ret);
        return ret;
    }

    ret = VI_EnableDev(pstCfg->ViDev);
    if (ret != 0) {
        LOG_ERROR("VI_EnableDev failed: %d\n", ret);
        return ret;
    }

    ret = VI_EnableChn(pstCfg->ViDev, pstCfg->ViChn);
    if (ret != 0) {
        LOG_ERROR("VI_EnableChn failed: %d\n", ret);
        return ret;
    }

    return 0;
}

static void teardown_vi(const ViVencDemoConfig *pstCfg) {
    if (pstCfg == NULL)
        return;

    (void)VI_DisableChn(pstCfg->ViDev, pstCfg->ViChn);
    (void)VI_DisableDev(pstCfg->ViDev);
    (void)VI_DeInit();
}

static S32 setup_venc(const ViVencDemoConfig *pstCfg) {
    S32 ret;

    ret = VENC_Init();
    if (ret != 0) {
        LOG_ERROR("VENC_Init failed: %d\n", ret);
        return ret;
    }

    ret = VENC_CreateChn(pstCfg->vencChn, &pstCfg->stVencAttr);
    if (ret != 0) {
        LOG_ERROR("VENC_CreateChn failed: %d\n", ret);
        return ret;
    }

    ret = VENC_EnableChn(pstCfg->vencChn);
    if (ret != 0) {
        LOG_ERROR("VENC_EnableChn failed: %d\n", ret);
        return ret;
    }

    return 0;
}

static void teardown_venc(const ViVencDemoConfig *pstCfg) {
    if (pstCfg == NULL)
        return;

    (void)VENC_DisableChn(pstCfg->vencChn);
    (void)VENC_DestroyChn(pstCfg->vencChn);
    (void)VENC_Exit();
}

int main(int argc, char *argv[]) {
    ViVencDemoConfig stCfg;
    const char *codec_arg = "h264";
    FILE *fout = NULL;
    S32 ret;
    U32 encoded = 0;
    BOOL sys_inited = MPP_FALSE;
    BOOL vb_inited = MPP_FALSE;
    BOOL vi_ready = MPP_FALSE;
    BOOL venc_ready = MPP_FALSE;

    init_config(&stCfg);

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    stCfg.pszOutputPath = argv[1];
    if (argc > 2)
        codec_arg = argv[2];
    if (argc > 3)
        stCfg.u32FrameCount = (U32)strtoul(argv[3], NULL, 10);
    if (stCfg.u32FrameCount == 0) {
        usage(argv[0]);
        return 1;
    }

    stCfg.eCodecType = parse_codec(codec_arg);
    if (stCfg.eCodecType == MPP_STREAM_CODEC_UNKNOWN) {
        LOG_ERROR("unsupported codec: %s\n", codec_arg);
        return 1;
    }
    stCfg.stVencAttr.eCodecType = stCfg.eCodecType;

    fout = fopen(stCfg.pszOutputPath, "wb");
    if (fout == NULL) {
        LOG_ERROR("fopen output %s failed: %s\n", stCfg.pszOutputPath, strerror(errno));
        return 1;
    }

    ret = SYS_Init();
    if (ret != 0) {
        LOG_ERROR("SYS_Init failed: %d\n", ret);
        goto fail;
    }
    sys_inited = MPP_TRUE;

    ret = VB_Init();
    if (ret != 0) {
        LOG_ERROR("VB_Init failed: %d\n", ret);
        goto fail;
    }
    vb_inited = MPP_TRUE;

    ret = setup_vi(&stCfg);
    if (ret != 0)
        goto fail;
    vi_ready = MPP_TRUE;

    ret = setup_venc(&stCfg);
    if (ret != 0)
        goto fail;
    venc_ready = MPP_TRUE;

    LOG_INFO(
        "VI->VENC demo start: dev=%d chn=%d %ux%u -> %s (%s), frames=%u\n",
        stCfg.ViDev,
        stCfg.ViChn,
        stCfg.stViChnAttr.u32Width,
        stCfg.stViChnAttr.u32Height,
        stCfg.pszOutputPath,
        codec_name(stCfg.eCodecType),
        stCfg.u32FrameCount);
    usleep(DEMO_STARTUP_WAIT_US); /* wait for VI stable; K1 may return -4 before first frame is ready */

    for (U32 i = 0; i < stCfg.u32FrameCount; ++i) {
        VideoFrameInfo stFrame;
        StreamBufferInfo stStream;

        memset(&stFrame, 0, sizeof(stFrame));
        ret = VI_GetChnFrame(stCfg.ViDev, stCfg.ViChn, &stFrame, stCfg.s32TimeoutMs);
        if (ret != 0) {
            if (is_vi_no_frame_error(ret) == MPP_TRUE)
                LOG_WARN("VI_GetChnFrame no frame yet at frame %u: %d\n", i, ret);
            else
                LOG_WARN("VI_GetChnFrame failed at frame %u: %d\n", i, ret);

            usleep(DEMO_RETRY_INTERVAL_US);
            continue;
        }

        stFrame.eFrameType = FRAME_TYPE_VENC;
        stFrame.eModId = MPP_ID_VENC;
        stFrame.stVencFrameInfo.stCommFrameInfo = stFrame.stViFrameInfo.stCommFrameInfo;
        stFrame.u32Idx = i;

        ret = VENC_SendFrame(stCfg.vencChn, &stFrame, DEMO_VENC_TIMEOUT_MS);
        if (ret != 0) {
            LOG_ERROR("VENC_SendFrame failed at frame %u: %d\n", i, ret);
            (void)VI_ReleaseChnFrame(stCfg.ViDev, stCfg.ViChn, &stFrame);
            goto fail;
        }

        memset(&stStream, 0, sizeof(stStream));
        ret = VENC_GetStream(stCfg.vencChn, &stStream, DEMO_VENC_TIMEOUT_MS);
        if (ret != 0) {
            LOG_ERROR("VENC_GetStream failed at frame %u: %d\n", i, ret);
            (void)VI_ReleaseChnFrame(stCfg.ViDev, stCfg.ViChn, &stFrame);
            goto fail;
        }

        if (fwrite(stStream.pu8Addr, 1, stStream.u32Size, fout) != stStream.u32Size) {
            LOG_ERROR("fwrite failed at frame %u\n", i);
            VENC_ReleaseStream(stCfg.vencChn, &stStream);
            (void)VI_ReleaseChnFrame(stCfg.ViDev, stCfg.ViChn, &stFrame);
            goto fail;
        }

        LOG_INFO(
            "encoded frame %u: size=%u key=%d pts=%" PRIu64 "\n",
            i,
            stStream.u32Size,
            stStream.bKeyFrame,
            (uint64_t)stStream.u64PTS);
        VENC_ReleaseStream(stCfg.vencChn, &stStream);

        ret = VI_ReleaseChnFrame(stCfg.ViDev, stCfg.ViChn, &stFrame);
        if (ret != 0) {
            LOG_ERROR("VI_ReleaseChnFrame failed at frame %u: %d\n", i, ret);
            goto fail;
        }

        encoded++;
        usleep(DEMO_RETRY_INTERVAL_US);
    }

    LOG_INFO("done, encoded %u frame(s) to %s\n", encoded, stCfg.pszOutputPath);

    if (venc_ready == MPP_TRUE)
        teardown_venc(&stCfg);
    if (vi_ready == MPP_TRUE)
        teardown_vi(&stCfg);
    if (fout != NULL)
        fclose(fout);

    /* keep same style as existing tests: avoid SYS/VB exit due to board-side instability */
    (void)vb_inited;
    (void)sys_inited;
    return 0;

fail:
    if (venc_ready == MPP_TRUE)
        teardown_venc(&stCfg);
    if (vi_ready == MPP_TRUE)
        teardown_vi(&stCfg);
    if (fout != NULL)
        fclose(fout);
    (void)vb_inited;
    (void)sys_inited;
    return 1;
}
