/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_venc.c
 * @Brief     :    VENC tests: API checks; encode synthetic NV12 frames to a
 *                 compressed elementary stream (H.264 / H.265 / MJPEG).
 *                 Input frames are generated in-process (no external YUV
 *                 needed) using a VB pool, so the test is self-contained.
 *
 * Suites (-t): api | format | param | multi | all   (all = api+format+param)
 *
 *   -W, --width N      input frame width  (default 1280)
 *   -H, --height N     input frame height (default 720)
 *   -c, --codec NAME   h264 | h265 | mjpeg (format suite)
 *   -n, --frames N     number of frames to encode. Default 30.
 *   -i, --input PATH   raw NV12 input file (packed, no header).
 *   -o, --output PATH  write the encoded elementary stream to PATH.
 *       --bitrate N    target bitrate in bps (param suite). Default 4000000.
 *       --gop N        GOP size (param suite). Default 30.
 *       --rc NAME      fixqp | cbr | vbr | cvbr (param suite). Default cbr.
 *       --rotate DEG   0 | 90 | 180 | 270 (param suite).
 *
 * Examples:
 *   ./test_venc --test api
 *   ./test_venc --test format -W 1280 -H 720 -c h264 -n 60 -o out.h264
 *   ./test_venc --test format -i input.nv12 -W 1280 -H 720 -c h264 -o out.h264
 *   ./test_venc --test param -W 1920 -H 1080 --rc vbr --bitrate 8000000 \
 *               --gop 60 -o out.h265 -c h265 -n 100
 *   ./test_venc --test multi -n 30 -o /tmp/m
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "sys_api.h"
#include "vb_api.h"
#include "venc/venc_api.h"

#define TEST_API 0x01u
#define TEST_FMT 0x02u
#define TEST_PARAM 0x04u
#define TEST_MULTI 0x08u

#define DEF_WIDTH 1280U
#define DEF_HEIGHT 720U
#define DEF_FRAMES 30U
#define DEF_BITRATE 4000000U
#define DEF_FRAMERATE 30U
#define DEF_GOP 30U
#define DEF_ALIGN 16U
#define DEF_POOL_BUFS 4U
#define VENC_SEND_TIMEOUT_MS 1000U
#define VENC_FLUSH_TIMEOUT_MS 50U   /* blocking wait during final flush */
#define VENC_DRAIN_POLL_MS 0U         /* non-blocking poll after each frame */
#define VENC_DRAIN_BACKOFF_MS 100U    /* short wait when send fails (backpressure) */
#define VENC_SEND_RETRIES 10U         /* max retries when encoder is full */
#define MAX_VENC_CASES 4

typedef struct {
    BOOL sys_ok;
    BOOL vb_ok;
    BOOL venc_ok;
} VencRuntime;

static MppStreamCodecType parse_codec_name(const char *name) {
    if (!name)
        return MPP_STREAM_CODEC_UNKNOWN;
    if (strcmp(name, "h264") == 0 || strcmp(name, "avc") == 0)
        return MPP_STREAM_CODEC_H264;
    if (strcmp(name, "h265") == 0 || strcmp(name, "hevc") == 0)
        return MPP_STREAM_CODEC_H265;
    if (strcmp(name, "mjpeg") == 0 || strcmp(name, "jpeg") == 0)
        return MPP_STREAM_CODEC_MJPEG;
    return MPP_STREAM_CODEC_UNKNOWN;
}

static const char *codec_name(MppStreamCodecType c) {
    switch (c) {
        case MPP_STREAM_CODEC_H264:
            return "h264";
        case MPP_STREAM_CODEC_H265:
            return "h265";
        case MPP_STREAM_CODEC_MJPEG:
            return "mjpeg";
        default:
            return "unknown";
    }
}

static const char *codec_ext(MppStreamCodecType c) {
    switch (c) {
        case MPP_STREAM_CODEC_H264:
            return "h264";
        case MPP_STREAM_CODEC_H265:
            return "h265";
        case MPP_STREAM_CODEC_MJPEG:
            return "mjpeg";
        default:
            return "bin";
    }
}

static VencRcMode parse_rc_name(const char *name) {
    if (!name || strcmp(name, "cbr") == 0)
        return VENC_RC_MODE_CBR;
    if (strcmp(name, "fixqp") == 0)
        return VENC_RC_MODE_FIXQP;
    if (strcmp(name, "vbr") == 0)
        return VENC_RC_MODE_VBR;
    if (strcmp(name, "cvbr") == 0)
        return VENC_RC_MODE_CVBR;
    return VENC_RC_MODE_MAX;
}

static const char *rc_name(VencRcMode m) {
    switch (m) {
        case VENC_RC_MODE_FIXQP:
            return "fixqp";
        case VENC_RC_MODE_CBR:
            return "cbr";
        case VENC_RC_MODE_VBR:
            return "vbr";
        case VENC_RC_MODE_CVBR:
            return "cvbr";
        default:
            return "unknown";
    }
}

static void print_usage(const char *prog) {
    fprintf(
        stderr,
        "Usage: %s [options]\n"
        "  -t, --test <list>   api, format, param, multi, all "
        "(no multi in all)\n"
        "  -W, --width <n>     input frame width (default %u)\n"
        "  -H, --height <n>    input frame height (default %u)\n"
        "  -c, --codec <name>  h264 | h265 | mjpeg (format/param)\n"
        "  -n, --frames <n>    frames to encode (default %u)\n"
        "  -i, --input <path>  raw NV12 input file (packed, no header)\n"
        "  -o, --output <path> elementary stream output; multi appends "
        ".chNN.<ext>\n"
        "      --channels <n>  number of channels for multi suite (default 2, "
        "max %u)\n"
        "      --bitrate <n>   target bitrate bps (param, default %u)\n"
        "      --gop <n>       GOP size (param, default %u)\n"
        "      --rc <name>     fixqp | cbr | vbr | cvbr (param, default cbr)\n"
        "      --rotate <deg>  0 | 90 | 180 | 270 (param)\n"
        "      --crop L,T,R,B  crop region (param)\n"
        "      --force-idr     force IDR at mid-stream (param)\n"
        "      --framerate <n> set frame rate dynamically (param)\n"
        "  -h, --help\n"
        "\n"
        "Examples:\n"
        "  --test api\n"
        "  --test format -W 1280 -H 720 -c h264 -n 60 -o out.h264\n"
        "  --test format -i input.nv12 -W 1280 -H 720 -c h264 -o out.h264\n"
        "  --test param -W 1920 -H 1080 -c h265 --rc vbr --bitrate 8000000 -o out.h265\n"
        "  --test param --force-idr --crop 0,0,120,68 -c h264 -n 100\n"
        "  --test multi -n 30 -o /tmp/m\n",
        prog,
        MAX_VENC_CASES,
        DEF_WIDTH,
        DEF_HEIGHT,
        DEF_FRAMES,
        DEF_BITRATE,
        DEF_GOP);
}

static U32 parse_test_mask(const char *s) {
    U32 m = 0;
    char *tmp = strdup(s);
    char *saveptr = NULL;
    if (!tmp)
        return 0;
    for (char *tok = strtok_r(tmp, ",", &saveptr); tok; tok = strtok_r(NULL, ",", &saveptr)) {
        while (*tok == ' ' || *tok == '\t')
            ++tok;
        if (strcmp(tok, "api") == 0) {
            m |= TEST_API;
        } else if (strcmp(tok, "format") == 0) {
            m |= TEST_FMT;
        } else if (strcmp(tok, "param") == 0) {
            m |= TEST_PARAM;
        } else if (strcmp(tok, "multi") == 0) {
            m |= TEST_MULTI;
        } else if (strcmp(tok, "all") == 0) {
            m |= TEST_API | TEST_FMT | TEST_PARAM;
        } else {
            free(tmp);
            return 0;
        }
    }
    free(tmp);
    return m;
}

static void venc_runtime_down(VencRuntime *rt) {
    if (rt->venc_ok) {
        (void)VENC_Exit();
        rt->venc_ok = MPP_FALSE;
    }
    if (rt->vb_ok) {
        (void)VB_Exit();
        rt->vb_ok = MPP_FALSE;
    }
    if (rt->sys_ok) {
        (void)SYS_Exit();
        rt->sys_ok = MPP_FALSE;
    }
}

static int venc_runtime_up(VencRuntime *rt) {
    S32 ret;

    memset(rt, 0, sizeof(*rt));
    ret = SYS_Init();
    if (ret != 0) {
        fprintf(stderr, "SYS_Init failed: %d\n", ret);
        return -1;
    }
    rt->sys_ok = MPP_TRUE;
    ret = VB_Init();
    if (ret != 0) {
        fprintf(stderr, "VB_Init failed: %d\n", ret);
        venc_runtime_down(rt);
        return -1;
    }
    rt->vb_ok = MPP_TRUE;
    ret = VENC_Init();
    if (ret != 0) {
        fprintf(stderr, "VENC_Init failed: %d\n", ret);
        venc_runtime_down(rt);
        return -1;
    }
    rt->venc_ok = MPP_TRUE;
    return 0;
}

/* ====================== Synthetic NV12 frame source ====================== */

typedef struct {
    UL ulPool;
    U32 u32Width;
    U32 u32Height;
    U32 u32Align;
    U32 u32Stride;
} SynthSource;

static U32 align_up(U32 v, U32 a) {
    if (a == 0)
        return v;
    return (v + a - 1U) & ~(a - 1U);
}

/**
 * Create a VB pool sized for one NV12 frame of width x height.
 */
static int synth_source_create(SynthSource *src, U32 width, U32 height, U32 align) {
    VideoFrameInfo info;
    VbPoolCfg cfg;
    S32 bufSize;

    memset(src, 0, sizeof(*src));
    src->u32Width = width;
    src->u32Height = height;
    src->u32Align = align ? align : DEF_ALIGN;
    src->u32Stride = align_up(width, src->u32Align);

    memset(&info, 0, sizeof(info));
    info.stCommFrameInfo.u32Width = width;
    info.stCommFrameInfo.u32Height = height;
    info.stCommFrameInfo.u32Align = src->u32Align;
    info.stCommFrameInfo.ePixelFormat = MPP_PIXEL_FORMAT_NV12;

    bufSize = VB_GetPicBufferSize(&info);
    if (bufSize <= 0) {
        fprintf(stderr, "VB_GetPicBufferSize failed for %ux%u\n", width, height);
        return -1;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.u32BufSize = (U32)bufSize;
    cfg.u32BufCnt = DEF_POOL_BUFS;
    cfg.eModId = MPP_ID_VENC;
    cfg.eRemapMode = VBUF_REMAP_MODE_CACHED;

    src->ulPool = VB_CreatePool(&cfg);
    if (src->ulPool == 0) {
        fprintf(stderr, "VB_CreatePool failed (size=%u cnt=%u)\n", cfg.u32BufSize, cfg.u32BufCnt);
        return -1;
    }
    return 0;
}

static void synth_source_destroy(SynthSource *src) {
    if (src->ulPool) {
        (void)VB_DestroyPool(src->ulPool);
        src->ulPool = 0;
    }
}

/**
 * Paint a moving NV12 pattern so consecutive frames differ (lets the encoder
 * emit P frames). Y is a diagonal gradient shifted by frame index; UV is a
 * slow colour sweep.
 */
static void synth_paint_nv12(U8 *y_base, U8 *uv_base, U32 w, U32 h, U32 stride, U32 idx) {
    U32 x, row;

    for (row = 0; row < h; ++row) {
        U8 *line = y_base + (size_t)row * stride;
        for (x = 0; x < w; ++x)
            line[x] = (U8)((x + row + idx * 4U) & 0xFFU);
    }
    for (row = 0; row < h / 2U; ++row) {
        U8 *line = uv_base + (size_t)row * stride;
        for (x = 0; x + 1U < w; x += 2U) {
            line[x] = (U8)((128 + (S32)idx * 2 + (S32)x) & 0xFF);     /* U */
            line[x + 1U] = (U8)((128 - (S32)idx * 2 + (S32)row) & 0xFF); /* V */
        }
    }
}

/**
 * Acquire a VB buffer, paint a synthetic NV12 frame, and fill pstFrame so it
 * can be passed straight to VENC_SendFrame. On success the caller owns ulBuff
 * and must release it via VB_ReleaseBuffer after the frame is consumed.
 */
static int synth_fill_frame(const SynthSource *src, U32 idx, VideoFrameInfo *pstFrame, UL *pulBuff) {
    UL ulBuff;
    void *pVir = NULL;
    S32 fd = -1;
    U32 ySize;

    ulBuff = VB_GetBuffer(src->ulPool, 1000);
    if (ulBuff == 0) {
        fprintf(stderr, "VB_GetBuffer failed (idx=%u)\n", idx);
        return -1;
    }
    if (VB_GetVirAddr(ulBuff, &pVir) != 0 || pVir == NULL) {
        fprintf(stderr, "VB_GetVirAddr failed (idx=%u)\n", idx);
        (void)VB_ReleaseBuffer(ulBuff);
        return -1;
    }
    (void)VB_GetDmaBufFd(ulBuff, &fd);

    ySize = src->u32Stride * src->u32Height;
    synth_paint_nv12((U8 *)pVir, (U8 *)pVir + ySize, src->u32Width, src->u32Height, src->u32Stride, idx);

    memset(pstFrame, 0, sizeof(*pstFrame));
    pstFrame->eFrameType = FRAME_TYPE_VENC;
    pstFrame->eModId = MPP_ID_VENC;
    pstFrame->u32Idx = idx;
    pstFrame->ulPoolId = src->ulPool;
    pstFrame->ulBufferId = ulBuff;
    pstFrame->stVencFrameInfo.stCommFrameInfo.u32Width = src->u32Width;
    pstFrame->stVencFrameInfo.stCommFrameInfo.u32Height = src->u32Height;
    pstFrame->stVencFrameInfo.stCommFrameInfo.u32Align = src->u32Align;
    pstFrame->stVencFrameInfo.stCommFrameInfo.ePixelFormat = MPP_PIXEL_FORMAT_NV12;

    pstFrame->stVFrame.u32PlaneNum = 2;
    pstFrame->stVFrame.u32PlaneStride[0] = src->u32Stride;
    pstFrame->stVFrame.u32PlaneStride[1] = src->u32Stride;
    pstFrame->stVFrame.u32PlaneSize[0] = ySize;
    pstFrame->stVFrame.u32PlaneSize[1] = ySize / 2U;
    pstFrame->stVFrame.ulPlaneVirAddr[0] = (UL)pVir;
    pstFrame->stVFrame.ulPlaneVirAddr[1] = (UL)((U8 *)pVir + ySize);
    if (fd > 0) {
        pstFrame->stVFrame.u32Fd[0] = (UL)fd;
        pstFrame->stVFrame.u32Fd[1] = (UL)fd;
    }
    pstFrame->stVFrame.u64PTS = (U64)idx;

    *pulBuff = ulBuff;
    return 0;
}

/* ====================== File-based NV12 frame source ====================== */

typedef struct {
    FILE *fp;
    UL ulPool;
    U32 u32Width;
    U32 u32Height;
    U32 u32Align;
    U32 u32Stride;
    U32 u32FrameSize; /* packed NV12 frame size: w*h*3/2 */
    U32 u32TotalFrames;
} FileSource;

/**
 * Create a file-based frame source. Opens the raw NV12 file and creates a VB
 * pool. The file must contain packed NV12 frames (no header, no stride padding).
 */
static int file_source_create(FileSource *fs, const char *path, U32 width, U32 height, U32 align) {
    VideoFrameInfo info;
    VbPoolCfg cfg;
    S32 bufSize;
    int64_t fileSize;

    memset(fs, 0, sizeof(*fs));
    fs->u32Width = width;
    fs->u32Height = height;
    fs->u32Align = align ? align : DEF_ALIGN;
    fs->u32Stride = align_up(width, fs->u32Align);
    fs->u32FrameSize = width * height * 3U / 2U;

    fs->fp = fopen(path, "rb");
    if (!fs->fp) {
        fprintf(stderr, "fopen input %s: %s\n", path, strerror(errno));
        return -1;
    }

    /* Determine total frames in file. */
    if (fseek(fs->fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "fseek %s: %s\n", path, strerror(errno));
        fclose(fs->fp);
        fs->fp = NULL;
        return -1;
    }
    fileSize = (int64_t)ftell(fs->fp);
    fseek(fs->fp, 0, SEEK_SET);
    if (fileSize <= 0 || fs->u32FrameSize == 0) {
        fprintf(stderr, "invalid input file or frame size\n");
        fclose(fs->fp);
        fs->fp = NULL;
        return -1;
    }
    fs->u32TotalFrames = (U32)(fileSize / (int64_t)fs->u32FrameSize);
    if (fs->u32TotalFrames == 0) {
        fprintf(stderr, "input file too small for one %ux%u NV12 frame\n", width, height);
        fclose(fs->fp);
        fs->fp = NULL;
        return -1;
    }
    printf("  [file] %s: %u frames available (%ux%u NV12)\n", path, fs->u32TotalFrames, width, height);

    memset(&info, 0, sizeof(info));
    info.stCommFrameInfo.u32Width = width;
    info.stCommFrameInfo.u32Height = height;
    info.stCommFrameInfo.u32Align = fs->u32Align;
    info.stCommFrameInfo.ePixelFormat = MPP_PIXEL_FORMAT_NV12;

    bufSize = VB_GetPicBufferSize(&info);
    if (bufSize <= 0) {
        fprintf(stderr, "VB_GetPicBufferSize failed for %ux%u\n", width, height);
        fclose(fs->fp);
        fs->fp = NULL;
        return -1;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.u32BufSize = (U32)bufSize;
    cfg.u32BufCnt = DEF_POOL_BUFS;
    cfg.eModId = MPP_ID_VENC;
    cfg.eRemapMode = VBUF_REMAP_MODE_CACHED;

    fs->ulPool = VB_CreatePool(&cfg);
    if (fs->ulPool == 0) {
        fprintf(stderr, "VB_CreatePool failed (file source)\n");
        fclose(fs->fp);
        fs->fp = NULL;
        return -1;
    }
    return 0;
}

static void file_source_destroy(FileSource *fs) {
    if (fs->ulPool) {
        (void)VB_DestroyPool(fs->ulPool);
        fs->ulPool = 0;
    }
    if (fs->fp) {
        fclose(fs->fp);
        fs->fp = NULL;
    }
}

/**
 * Read one NV12 frame from the file into a VB buffer. Handles stride alignment:
 * if stride > width, reads line-by-line and pads each row.
 * Returns 0 on success, -1 on EOF or error.
 */
static int file_fill_frame(const FileSource *fs, U32 idx, VideoFrameInfo *pstFrame, UL *pulBuff) {
    UL ulBuff;
    void *pVir = NULL;
    S32 fd = -1;
    U32 ySize;
    U32 row;

    if (idx >= fs->u32TotalFrames)
        return -1; /* EOF */

    ulBuff = VB_GetBuffer(fs->ulPool, 1000);
    if (ulBuff == 0) {
        fprintf(stderr, "VB_GetBuffer failed (file, idx=%u)\n", idx);
        return -1;
    }
    if (VB_GetVirAddr(ulBuff, &pVir) != 0 || pVir == NULL) {
        fprintf(stderr, "VB_GetVirAddr failed (file, idx=%u)\n", idx);
        (void)VB_ReleaseBuffer(ulBuff);
        return -1;
    }
    (void)VB_GetDmaBufFd(ulBuff, &fd);

    ySize = fs->u32Stride * fs->u32Height;

    /* Read Y plane. */
    if (fs->u32Stride == fs->u32Width) {
        if (fread(pVir, 1, (size_t)fs->u32Width * fs->u32Height, fs->fp)
            != (size_t)fs->u32Width * fs->u32Height) {
            fprintf(stderr, "fread Y plane failed (idx=%u)\n", idx);
            (void)VB_ReleaseBuffer(ulBuff);
            return -1;
        }
    } else {
        U8 *dst = (U8 *)pVir;
        for (row = 0; row < fs->u32Height; ++row) {
            if (fread(dst, 1, fs->u32Width, fs->fp) != fs->u32Width) {
                fprintf(stderr, "fread Y row %u failed (idx=%u)\n", row, idx);
                (void)VB_ReleaseBuffer(ulBuff);
                return -1;
            }
            /* Zero-fill stride padding. */
            if (fs->u32Stride > fs->u32Width)
                memset(dst + fs->u32Width, 0, fs->u32Stride - fs->u32Width);
            dst += fs->u32Stride;
        }
    }

    /* Read UV plane. */
    {
        U8 *uvBase = (U8 *)pVir + ySize;
        U32 uvHeight = fs->u32Height / 2U;
        if (fs->u32Stride == fs->u32Width) {
            if (fread(uvBase, 1, (size_t)fs->u32Width * uvHeight, fs->fp)
                != (size_t)fs->u32Width * uvHeight) {
                fprintf(stderr, "fread UV plane failed (idx=%u)\n", idx);
                (void)VB_ReleaseBuffer(ulBuff);
                return -1;
            }
        } else {
            for (row = 0; row < uvHeight; ++row) {
                if (fread(uvBase, 1, fs->u32Width, fs->fp) != fs->u32Width) {
                    fprintf(stderr, "fread UV row %u failed (idx=%u)\n", row, idx);
                    (void)VB_ReleaseBuffer(ulBuff);
                    return -1;
                }
                if (fs->u32Stride > fs->u32Width)
                    memset(uvBase + fs->u32Width, 0, fs->u32Stride - fs->u32Width);
                uvBase += fs->u32Stride;
            }
        }
    }

    memset(pstFrame, 0, sizeof(*pstFrame));
    pstFrame->eFrameType = FRAME_TYPE_VENC;
    pstFrame->eModId = MPP_ID_VENC;
    pstFrame->u32Idx = idx;
    pstFrame->ulPoolId = fs->ulPool;
    pstFrame->ulBufferId = ulBuff;
    pstFrame->stVencFrameInfo.stCommFrameInfo.u32Width = fs->u32Width;
    pstFrame->stVencFrameInfo.stCommFrameInfo.u32Height = fs->u32Height;
    pstFrame->stVencFrameInfo.stCommFrameInfo.u32Align = fs->u32Align;
    pstFrame->stVencFrameInfo.stCommFrameInfo.ePixelFormat = MPP_PIXEL_FORMAT_NV12;

    pstFrame->stVFrame.u32PlaneNum = 2;
    pstFrame->stVFrame.u32PlaneStride[0] = fs->u32Stride;
    pstFrame->stVFrame.u32PlaneStride[1] = fs->u32Stride;
    pstFrame->stVFrame.u32PlaneSize[0] = ySize;
    pstFrame->stVFrame.u32PlaneSize[1] = ySize / 2U;
    pstFrame->stVFrame.ulPlaneVirAddr[0] = (UL)pVir;
    pstFrame->stVFrame.ulPlaneVirAddr[1] = (UL)((U8 *)pVir + ySize);
    if (fd > 0) {
        pstFrame->stVFrame.u32Fd[0] = (UL)fd;
        pstFrame->stVFrame.u32Fd[1] = (UL)fd;
    }
    pstFrame->stVFrame.u64PTS = (U64)idx;

    *pulBuff = ulBuff;
    return 0;
}

/* ====================== Encode core ====================== */

typedef struct {
    MppStreamCodecType eCodec;
    U32 u32Width;
    U32 u32Height;
    U32 u32Bitrate;
    U32 u32FrameRate;
    U32 u32Gop;
    U32 u32Align;
    U32 u32RotateDegree;
    VencRcMode eRcMode;
    U32 u32IQp;
    U32 u32PQp;
    BOOL bSetRc;        /**< user specified --rc/--bitrate, call VENC_SetRateControl */
    BOOL bSetFrameRate; /**< user specified --framerate, call VENC_SetFrameRate */
    BOOL bForceIDR;     /**< user specified --force-idr, call VENC_SetForceIDR */
    BOOL bSetCrop;      /**< user specified --crop, call VENC_SetCropAttr */
    VencCropAttr stCrop;
} VencCaseCfg;

static void venc_case_defaults(VencCaseCfg *cfg, MppStreamCodecType codec, U32 w, U32 h) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->eCodec = codec;
    cfg->u32Width = w;
    cfg->u32Height = h;
    cfg->u32Bitrate = DEF_BITRATE;
    cfg->u32FrameRate = DEF_FRAMERATE;
    cfg->u32Gop = DEF_GOP;
    cfg->u32Align = DEF_ALIGN;
    cfg->u32RotateDegree = 0;
    cfg->eRcMode = VENC_RC_MODE_CBR;
}

static void fill_chn_attr(VencChnAttr *attr, const VencCaseCfg *cfg) {
    memset(attr, 0, sizeof(*attr));
    attr->eCodecType = cfg->eCodec;
    attr->eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    attr->u32Width = cfg->u32Width;
    attr->u32Height = cfg->u32Height;
    attr->u32Align = cfg->u32Align;
    attr->u32Bitrate = cfg->u32Bitrate;
    attr->u32FrameRate = cfg->u32FrameRate;
    attr->u32Gop = cfg->u32Gop;
    attr->u32RotateDegree = cfg->u32RotateDegree;
    attr->eRcMode = cfg->eRcMode;
    attr->u32IQp = cfg->u32IQp;
    attr->u32PQp = cfg->u32PQp;
    attr->eFrameBufMode = VENC_FRAME_BUF_DMABUF_EXTERNAL;
}

/**
 * Pull any ready encoded streams, write them to fout (optional), count them.
 * Returns 0 on success (including "no stream available"), -1 on hard error.
 */
static int drain_streams(S32 chn, FILE *fout, U32 *enc_count, U32 *key_count, U32 budget, U32 timeout_ms) {
    U32 pulled = 0;

    for (;;) {
        StreamBufferInfo stream;
        S32 ret;

        memset(&stream, 0, sizeof(stream));
        ret = VENC_GetStream(chn, &stream, timeout_ms);
        if (ret == ERR_VENC_NO_STREAM || ret == ERR_VENC_TIMEOUT || ret == ERR_VENC_EOS)
            break;
        if (ret != ERR_VENC_OK) {
            fprintf(stderr, "VENC_GetStream chn %d: %d\n", chn, ret);
            return -1;
        }

        if (fout && stream.pu8Addr && stream.u32Size > 0) {
            if (fwrite(stream.pu8Addr, 1, stream.u32Size, fout) != stream.u32Size) {
                fprintf(stderr, "fwrite stream failed (chn %d)\n", chn);
                (void)VENC_ReleaseStream(chn, &stream);
                return -1;
            }
        }
        if (stream.u32Size > 0)
            (*enc_count)++;
        if (stream.bKeyFrame)
            (*key_count)++;

        ret = VENC_ReleaseStream(chn, &stream);
        if (ret != ERR_VENC_OK) {
            fprintf(stderr, "VENC_ReleaseStream chn %d: %d\n", chn, ret);
            return -1;
        }

        if (budget && ++pulled >= budget)
            break;
    }
    return 0;
}

/**
 * Full encode of `frames` synthetic NV12 frames on `chn` to `fout` (optional).
 * Returns 0 on success; the encoded/key-frame counts are reported via outputs.
 */
static double ts_ms(struct timespec *a, struct timespec *b) {
    return (double)(b->tv_sec - a->tv_sec) * 1000.0 +
            (double)(b->tv_nsec - a->tv_nsec) / 1000000.0;
}

static int encode_synth(S32 chn, const VencCaseCfg *cfg, U32 frames, FILE *fout,
                        const char *input_file, U32 *out_enc, U32 *out_key) {
    SynthSource src;
    FileSource fsrc;
    BOOL use_file = (input_file != NULL) ? MPP_TRUE : MPP_FALSE;
    VencChnAttr attr;
    U32 enc_count = 0;
    U32 key_count = 0;
    U32 i;
    S32 ret;
    int rc = -1;
    struct timespec t0, t1, t2, t3, t4, t5, t_start, t_end;
    double total_fill_ms = 0, total_send_ms = 0, total_drain_ms = 0;
    double max_fill_ms = 0, max_send_ms = 0, max_drain_ms = 0;
    double flush_ms = 0;

    memset(&src, 0, sizeof(src));
    memset(&fsrc, 0, sizeof(fsrc));

    clock_gettime(CLOCK_MONOTONIC, &t_start);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (use_file) {
        if (file_source_create(&fsrc, input_file, cfg->u32Width, cfg->u32Height, cfg->u32Align) != 0)
            return -1;
        /* Clamp frame count to available frames in file. */
        if (frames > fsrc.u32TotalFrames) {
            printf("  [file] clamping frames %u -> %u (file EOF)\n", frames, fsrc.u32TotalFrames);
            frames = fsrc.u32TotalFrames;
        }
    } else {
        if (synth_source_create(&src, cfg->u32Width, cfg->u32Height, cfg->u32Align) != 0)
            return -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  [time] %s: %.2f ms\n", use_file ? "file_source_create" : "synth_source_create", ts_ms(&t0, &t1));

    fill_chn_attr(&attr, cfg);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    ret = VENC_CreateChn(chn, &attr);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  [time] VENC_CreateChn: %.2f ms\n", ts_ms(&t0, &t1));
    if (ret != ERR_VENC_OK) {
        fprintf(stderr, "VENC_CreateChn chn %d: %d\n", chn, ret);
        if (use_file)
            file_source_destroy(&fsrc);
        else
            synth_source_destroy(&src);
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &t0);
    ret = VENC_EnableChn(chn);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  [time] VENC_EnableChn: %.2f ms\n", ts_ms(&t0, &t1));
    if (ret != ERR_VENC_OK) {
        fprintf(stderr, "VENC_EnableChn chn %d: %d\n", chn, ret);
        (void)VENC_DestroyChn(chn);
        if (use_file)
            file_source_destroy(&fsrc);
        else
            synth_source_destroy(&src);
        return -1;
    }

    /* Apply dynamic parameters after channel is started (only if user specified). */
    if (cfg->bSetRc) {
        VencRcAttr rcAttr;
        memset(&rcAttr, 0, sizeof(rcAttr));
        rcAttr.enRcMode = cfg->eRcMode;
        rcAttr.u32BitRate = cfg->u32Bitrate;
        rcAttr.u32MaxBitRate = cfg->u32Bitrate;
        ret = VENC_SetRateControl(chn, &rcAttr);
        if (ret != ERR_VENC_OK) {
            fprintf(stderr, "VENC_SetRateControl chn %d: %d\n", chn, ret);
            (void)VENC_DisableChn(chn);
            (void)VENC_DestroyChn(chn);
            if (use_file)
                file_source_destroy(&fsrc);
            else
                synth_source_destroy(&src);
            return -1;
        }
        printf("  [param] SetRateControl: rc=%s bitrate=%u\n", rc_name(cfg->eRcMode), cfg->u32Bitrate);
    }
    if (cfg->bSetFrameRate) {
        ret = VENC_SetFrameRate(chn, (S32)cfg->u32FrameRate);
        if (ret != ERR_VENC_OK) {
            fprintf(stderr, "VENC_SetFrameRate chn %d: %d\n", chn, ret);
        } else {
            printf("  [param] SetFrameRate: %u\n", cfg->u32FrameRate);
        }
    }
    if (cfg->bSetCrop) {
        VencCropAttr cropAttr = cfg->stCrop;
        ret = VENC_SetCropAttr(chn, &cropAttr);
        if (ret != ERR_VENC_OK) {
            fprintf(stderr, "VENC_SetCropAttr chn %d: %d\n", chn, ret);
        } else {
            printf("  [param] SetCropAttr: left=%d top=%d right=%d bottom=%d\n",
                    cropAttr.s32Left, cropAttr.s32Top, cropAttr.s32Right, cropAttr.s32Bottom);
        }
    }

    for (i = 0; i < frames; ++i) {
        VideoFrameInfo frame;
        UL ulBuff = 0;
        double dt;

        /* Force IDR at mid-stream if user specified --force-idr. */
        if (cfg->bForceIDR && i == frames / 2U) {
            ret = VENC_SetForceIDR(chn);
            if (ret != ERR_VENC_OK)
                fprintf(stderr, "  [warn] VENC_SetForceIDR chn %d: %d\n", chn, ret);
            else
                printf("  [param] SetForceIDR at frame %u\n", i);
        }

        clock_gettime(CLOCK_MONOTONIC, &t2);
        if (use_file) {
            if (file_fill_frame(&fsrc, i, &frame, &ulBuff) != 0)
                goto done;
        } else {
            if (synth_fill_frame(&src, i, &frame, &ulBuff) != 0)
                goto done;
        }
        clock_gettime(CLOCK_MONOTONIC, &t3);
        dt = ts_ms(&t2, &t3);
        total_fill_ms += dt;
        if (dt > max_fill_ms) max_fill_ms = dt;

        clock_gettime(CLOCK_MONOTONIC, &t3);
        ret = VENC_SendFrame(chn, &frame, VENC_SEND_TIMEOUT_MS);
        if (ret != ERR_VENC_OK) {
            /* Encoder input queue full — drain output and retry. */
            U32 retry;
            for (retry = 0; retry < VENC_SEND_RETRIES; ++retry) {
                if (drain_streams(chn, fout, &enc_count, &key_count, 0, VENC_DRAIN_BACKOFF_MS) != 0) {
                    (void)VB_ReleaseBuffer(ulBuff);
                    goto done;
                }
                ret = VENC_SendFrame(chn, &frame, VENC_SEND_TIMEOUT_MS);
                if (ret == ERR_VENC_OK)
                    break;
            }
            if (ret != ERR_VENC_OK) {
                fprintf(stderr, "VENC_SendFrame chn %d frame %u: %d (after %u retries)\n",
                        chn, i, ret, VENC_SEND_RETRIES);
                (void)VB_ReleaseBuffer(ulBuff);
                goto done;
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &t4);
        dt = ts_ms(&t3, &t4);
        total_send_ms += dt;
        if (dt > max_send_ms) max_send_ms = dt;
        (void)VB_ReleaseBuffer(ulBuff);

        clock_gettime(CLOCK_MONOTONIC, &t4);
        if (drain_streams(chn, fout, &enc_count, &key_count, 0, 0) != 0)
            goto done;
        clock_gettime(CLOCK_MONOTONIC, &t5);
        dt = ts_ms(&t4, &t5);
        total_drain_ms += dt;
        if (dt > max_drain_ms) max_drain_ms = dt;

        if (i < 5 || (i % 10 == 0)) {
            printf("  [time] frame %3u: fill=%.2f send=%.2f drain=%.2f ms\n",
                    i, ts_ms(&t2, &t3), ts_ms(&t3, &t4), ts_ms(&t4, &t5));
        }
    }

    /* Drain any frames still in flight. */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 64U; ++i) {
        U32 before = enc_count;
        if (drain_streams(chn, fout, &enc_count, &key_count, 0, VENC_FLUSH_TIMEOUT_MS) != 0)
            goto done;
        if (enc_count == before) {
            usleep(5000);
            if (i > 8U && enc_count == before)
                break;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    flush_ms = ts_ms(&t0, &t1);

    rc = 0;

done:
    (void)VENC_DisableChn(chn);
    (void)VENC_DestroyChn(chn);
    if (use_file)
        file_source_destroy(&fsrc);
    else
        synth_source_destroy(&src);

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    printf("  [time] === encode_synth summary (chn %d, %u frames) ===\n", chn, frames);
    printf("  [time]   fill_frame : total=%.2f ms, avg=%.2f ms, max=%.2f ms\n",
            total_fill_ms, frames ? total_fill_ms / frames : 0, max_fill_ms);
    printf("  [time]   SendFrame  : total=%.2f ms, avg=%.2f ms, max=%.2f ms\n",
            total_send_ms, frames ? total_send_ms / frames : 0, max_send_ms);
    printf("  [time]   drain      : total=%.2f ms, avg=%.2f ms, max=%.2f ms\n",
            total_drain_ms, frames ? total_drain_ms / frames : 0, max_drain_ms);
    printf("  [time]   flush+drain: %.2f ms\n", flush_ms);
    printf("  [time]   total      : %.2f ms\n", ts_ms(&t_start, &t_end));

    if (out_enc)
        *out_enc = enc_count;
    if (out_key)
        *out_key = key_count;
    return rc;
}

/* ====================== api suite ====================== */

static int run_api_suite(void) {
    VencChnAttr attr;
    VencChnStatus st;
    StreamBufferInfo stream;
    VideoFrameInfo frame;
    S32 r;

    printf("[api] error-path checks\n");

    r = VENC_Exit();
    if (r != ERR_VENC_NOT_INIT) {
        fprintf(stderr, "VENC_Exit w/o init: got %d expect NOT_INIT\n", r);
        return -1;
    }

    r = VENC_Init();
    if (r != ERR_VENC_OK) {
        fprintf(stderr, "VENC_Init: %d\n", r);
        return -1;
    }

    r = VENC_Init();
    if (r != ERR_VENC_ALREADY_INIT) {
        fprintf(stderr, "double VENC_Init: got %d expect ALREADY_INIT\n", r);
        (void)VENC_Exit();
        return -1;
    }

    memset(&attr, 0, sizeof(attr));
    attr.eCodecType = MPP_STREAM_CODEC_H264;
    attr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    attr.u32Width = 128;
    attr.u32Height = 128;
    attr.u32Align = DEF_ALIGN;
    attr.eFrameBufMode = VENC_FRAME_BUF_DMABUF_EXTERNAL;
    attr.eRcMode = VENC_RC_MODE_CBR;

    r = VENC_CreateChn(-1, &attr);
    if (r != ERR_VENC_INVALID_CHN) {
        fprintf(stderr, "CreateChn invalid id: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }
    r = VENC_CreateChn(VENC_MAX_CHN, &attr);
    if (r != ERR_VENC_INVALID_CHN) {
        fprintf(stderr, "CreateChn max id: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }
    r = VENC_CreateChn(0, NULL);
    if (r != ERR_VENC_NULL_PTR) {
        fprintf(stderr, "CreateChn NULL attr: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }

    r = VENC_DestroyChn(0);
    if (r != ERR_VENC_INVALID_CHN) {
        fprintf(stderr, "DestroyChn no channel: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }
    r = VENC_EnableChn(0);
    if (r != ERR_VENC_INVALID_CHN) {
        fprintf(stderr, "EnableChn no channel: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }
    r = VENC_DisableChn(0);
    if (r != ERR_VENC_INVALID_CHN) {
        fprintf(stderr, "DisableChn no channel: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }

    memset(&frame, 0, sizeof(frame));
    r = VENC_SendFrame(0, &frame, 0);
    if (r != ERR_VENC_NOT_STARTED) {
        fprintf(stderr, "SendFrame no channel: %d (expect NOT_STARTED)\n", r);
        (void)VENC_Exit();
        return -1;
    }
    r = VENC_SendFrame(0, NULL, 0);
    if (r != ERR_VENC_NULL_PTR) {
        fprintf(stderr, "SendFrame NULL: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }

    memset(&stream, 0, sizeof(stream));
    r = VENC_GetStream(0, &stream, 0);
    if (r != ERR_VENC_NOT_STARTED) {
        fprintf(stderr, "GetStream no channel: %d (expect NOT_STARTED)\n", r);
        (void)VENC_Exit();
        return -1;
    }
    r = VENC_GetStream(0, NULL, 0);
    if (r != ERR_VENC_NULL_PTR) {
        fprintf(stderr, "GetStream NULL: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }
    r = VENC_ReleaseStream(0, NULL);
    if (r != ERR_VENC_NULL_PTR) {
        fprintf(stderr, "ReleaseStream NULL: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }

    r = VENC_QueryStatus(0, NULL);
    if (r != ERR_VENC_NULL_PTR) {
        fprintf(stderr, "QueryStatus NULL: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }
    r = VENC_QueryStatus(0, &st);
    if (r != ERR_VENC_INVALID_CHN) {
        fprintf(stderr, "QueryStatus no channel: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }

    r = VENC_Flush(0);
    if (r != ERR_VENC_NOT_STARTED) {
        fprintf(stderr, "Flush no channel: %d (expect NOT_STARTED)\n", r);
        (void)VENC_Exit();
        return -1;
    }
    r = VENC_Reset(0);
    if (r != ERR_VENC_NOT_STARTED) {
        fprintf(stderr, "Reset no channel: %d (expect NOT_STARTED)\n", r);
        (void)VENC_Exit();
        return -1;
    }

    /* Happy path: create -> query -> destroy. */
    r = VENC_CreateChn(0, &attr);
    if (r != ERR_VENC_OK) {
        fprintf(stderr, "CreateChn ok path: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }
    r = VENC_QueryStatus(0, &st);
    if (r != ERR_VENC_OK || st.u32Width != attr.u32Width || st.u32Height != attr.u32Height) {
        fprintf(stderr, "QueryStatus created chn: ret=%d %ux%u\n", r, st.u32Width, st.u32Height);
        (void)VENC_DestroyChn(0);
        (void)VENC_Exit();
        return -1;
    }
    /* Cannot create the same channel twice. */
    r = VENC_CreateChn(0, &attr);
    if (r != ERR_VENC_BUSY) {
        fprintf(stderr, "CreateChn busy: got %d expect BUSY\n", r);
        (void)VENC_DestroyChn(0);
        (void)VENC_Exit();
        return -1;
    }
    r = VENC_DestroyChn(0);
    if (r != ERR_VENC_OK) {
        fprintf(stderr, "DestroyChn after create: %d\n", r);
        (void)VENC_Exit();
        return -1;
    }

    (void)VENC_Exit();
    r = VENC_Exit();
    if (r != ERR_VENC_NOT_INIT) {
        fprintf(stderr, "VENC_Exit double: %d\n", r);
        return -1;
    }

    printf("[PASS] api suite\n");
    return 0;
}

/* ====================== format suite ====================== */

/**
 * Encode `frames` synthetic frames with `codec` on channel `chn`, writing to
 * "<prefix>.<ext>" when prefix is set. Validates that bytes were produced and
 * at least one key frame appeared.
 */
static int format_one(S32 chn, MppStreamCodecType codec, U32 w, U32 h, U32 frames,
                        const char *prefix, const char *input_file) {
    VencCaseCfg cfg;
    FILE *fout = NULL;
    U32 enc = 0, key = 0;
    int rc;

    venc_case_defaults(&cfg, codec, w, h);
    if (codec == MPP_STREAM_CODEC_MJPEG)
        cfg.eRcMode = VENC_RC_MODE_FIXQP;

    if (prefix) {
        char path[512];
        snprintf(path, sizeof(path), "%s.%s", prefix, codec_ext(codec));
        fout = fopen(path, "wb");
        if (!fout) {
            fprintf(stderr, "fopen %s: %s\n", path, strerror(errno));
            return -1;
        }
    }

    printf("[format] %s %ux%u %u frame(s)%s\n", codec_name(codec), w, h, frames,
            input_file ? " (file input)" : "");
    rc = encode_synth(chn, &cfg, frames, fout, input_file, &enc, &key);
    if (fout)
        fclose(fout);
    if (rc != 0)
        return -1;

    if (enc == 0) {
        fprintf(stderr, "[format] %s produced no stream\n", codec_name(codec));
        return -1;
    }
    printf("[format] %s: %u stream(s), %u key frame(s)\n", codec_name(codec), enc, key);
    return 0;
}

static int run_format_suite(MppStreamCodecType only_codec, U32 w, U32 h, U32 frames,
                            const char *prefix, const char *input_file) {
    VencRuntime rt;
    int rc = 0;
    struct timespec ts_init0, ts_init1;

    if (w == 0)
        w = DEF_WIDTH;
    if (h == 0)
        h = DEF_HEIGHT;
    if (frames == 0)
        frames = DEF_FRAMES;

    clock_gettime(CLOCK_MONOTONIC, &ts_init0);
    if (venc_runtime_up(&rt) != 0)
        return -1;
    clock_gettime(CLOCK_MONOTONIC, &ts_init1);
    printf("  [time] runtime_up (SYS+VB+VENC init): %.2f ms\n", ts_ms(&ts_init0, &ts_init1));

    if (only_codec != MPP_STREAM_CODEC_UNKNOWN) {
        rc = format_one(0, only_codec, w, h, frames, prefix, input_file);
    } else {
        static const MppStreamCodecType codecs[] = {
            MPP_STREAM_CODEC_H264,
            MPP_STREAM_CODEC_H265,
            MPP_STREAM_CODEC_MJPEG,
        };
        size_t i;
        for (i = 0; i < sizeof(codecs) / sizeof(codecs[0]); ++i) {
            if (format_one(0, codecs[i], w, h, frames, prefix, input_file) != 0) {
                rc = -1;
                break;
            }
        }
    }

    venc_runtime_down(&rt);
    if (rc != 0)
        return -1;
    printf("[PASS] format suite\n");
    return 0;
}

/* ====================== param suite ====================== */

static int param_one(const char *label, const VencCaseCfg *cfg, U32 frames, FILE *fout,
                        const char *input_file) {
    U32 enc = 0, key = 0;

    printf("[param] %s: %s %ux%u rc=%s bitrate=%u gop=%u rotate=%u\n",
            label, codec_name(cfg->eCodec), cfg->u32Width, cfg->u32Height,
            rc_name(cfg->eRcMode), cfg->u32Bitrate, cfg->u32Gop, cfg->u32RotateDegree);
    if (encode_synth(0, cfg, frames, fout, input_file, &enc, &key) != 0)
        return -1;
    if (enc == 0) {
        fprintf(stderr, "[param] %s produced no stream\n", label);
        return -1;
    }
    printf("[param] %s: %u stream(s), %u key frame(s)\n", label, enc, key);
    return 0;
}

/**
 * Dynamic control: start a channel, send some frames, then change frame rate,
 * rate control, and force an IDR mid-stream. Verifies the calls succeed and the
 * encoder keeps producing output.
 */
static int param_dynamic(MppStreamCodecType codec, U32 w, U32 h, U32 frames) {
    SynthSource src;
    VencChnAttr attr;
    VencCaseCfg cfg;
    VencRcAttr rc;
    U32 enc = 0, key = 0;
    U32 i;
    S32 ret;
    int result = -1;

    venc_case_defaults(&cfg, codec, w, h);
    if (synth_source_create(&src, w, h, cfg.u32Align) != 0)
        return -1;
    fill_chn_attr(&attr, &cfg);

    if (VENC_CreateChn(0, &attr) != ERR_VENC_OK) {
        synth_source_destroy(&src);
        return -1;
    }
    if (VENC_EnableChn(0) != ERR_VENC_OK) {
        (void)VENC_DestroyChn(0);
        synth_source_destroy(&src);
        return -1;
    }

    for (i = 0; i < frames; ++i) {
        VideoFrameInfo frame;
        UL ulBuff = 0;

        if (i == frames / 3U) {
            ret = VENC_SetFrameRate(0, (S32)(DEF_FRAMERATE / 2U));
            if (ret != ERR_VENC_OK) {
                fprintf(stderr, "VENC_SetFrameRate: %d\n", ret);
                goto done;
            }
            memset(&rc, 0, sizeof(rc));
            rc.enRcMode = VENC_RC_MODE_VBR;
            rc.u32BitRate = DEF_BITRATE / 2U;
            rc.u32MaxBitRate = DEF_BITRATE;
            ret = VENC_SetRateControl(0, &rc);
            if (ret != ERR_VENC_OK) {
                fprintf(stderr, "VENC_SetRateControl: %d\n", ret);
                goto done;
            }
        }
        if (i == (frames * 2U) / 3U) {
            ret = VENC_SetForceIDR(0);
            if (ret != ERR_VENC_OK) {
                fprintf(stderr, "VENC_SetForceIDR: %d\n", ret);
                goto done;
            }
        }

        if (synth_fill_frame(&src, i, &frame, &ulBuff) != 0)
            goto done;
        ret = VENC_SendFrame(0, &frame, VENC_SEND_TIMEOUT_MS);
        if (ret != ERR_VENC_OK) {
            U32 retry;
            for (retry = 0; retry < VENC_SEND_RETRIES; ++retry) {
                if (drain_streams(0, NULL, &enc, &key, 0, VENC_DRAIN_BACKOFF_MS) != 0) {
                    (void)VB_ReleaseBuffer(ulBuff);
                    goto done;
                }
                ret = VENC_SendFrame(0, &frame, VENC_SEND_TIMEOUT_MS);
                if (ret == ERR_VENC_OK)
                    break;
            }
            if (ret != ERR_VENC_OK) {
                fprintf(stderr, "SendFrame (dynamic) frame %u: %d (after %u retries)\n",
                        i, ret, VENC_SEND_RETRIES);
                (void)VB_ReleaseBuffer(ulBuff);
                goto done;
            }
        }
        (void)VB_ReleaseBuffer(ulBuff);
        if (drain_streams(0, NULL, &enc, &key, 0, 0) != 0)
            goto done;
    }

    for (i = 0; i < 32U; ++i) {
        U32 before = enc;
        if (drain_streams(0, NULL, &enc, &key, 0, VENC_FLUSH_TIMEOUT_MS) != 0)
            goto done;
        if (enc == before) {
            usleep(5000);
            if (i > 8U)
                break;
        }
    }

    if (enc == 0) {
        fprintf(stderr, "[param] dynamic produced no stream\n");
        goto done;
    }
    printf("[param] dynamic: %u stream(s), %u key frame(s)\n", enc, key);
    result = 0;

done:
    (void)VENC_DisableChn(0);
    (void)VENC_DestroyChn(0);
    synth_source_destroy(&src);
    return result;
}

static int run_param_suite(MppStreamCodecType only_codec, U32 w, U32 h, U32 frames,
                            U32 rotate_deg, U32 bitrate, U32 gop, VencRcMode rc_mode,
                            U32 framerate, BOOL force_idr, BOOL set_crop,
                            const VencCropAttr *crop,
                            const char *output, const char *input_file) {
    VencRuntime rt;
    VencCaseCfg cfg;
    MppStreamCodecType codec;
    int rc = 0;
    BOOL user_mode;

    if (w == 0)
        w = DEF_WIDTH;
    if (h == 0)
        h = DEF_HEIGHT;
    if (frames == 0)
        frames = DEF_FRAMES;
    codec = (only_codec != MPP_STREAM_CODEC_UNKNOWN) ? only_codec : MPP_STREAM_CODEC_H264;

    /* If the user specified custom params or output, run only the user case. */
    user_mode = (bitrate || gop || rotate_deg || output || framerate ||
                    force_idr || set_crop ||
                    rc_mode != VENC_RC_MODE_MAX) ? MPP_TRUE : MPP_FALSE;

    if (venc_runtime_up(&rt) != 0)
        return -1;

    if (user_mode) {
        FILE *fout = NULL;
        venc_case_defaults(&cfg, codec, w, h);
        if (bitrate)
            cfg.u32Bitrate = bitrate;
        if (gop)
            cfg.u32Gop = gop;
        if (rc_mode != VENC_RC_MODE_MAX) {
            cfg.eRcMode = rc_mode;
            cfg.bSetRc = MPP_TRUE;
        }
        if (bitrate) {
            cfg.bSetRc = MPP_TRUE;
        }
        if (rotate_deg)
            cfg.u32RotateDegree = rotate_deg;
        if (framerate) {
            cfg.u32FrameRate = framerate;
            cfg.bSetFrameRate = MPP_TRUE;
        }
        if (force_idr)
            cfg.bForceIDR = MPP_TRUE;
        if (set_crop) {
            cfg.bSetCrop = MPP_TRUE;
            cfg.stCrop = *crop;
        }
        if (output) {
            fout = fopen(output, "wb");
            if (!fout) {
                fprintf(stderr, "fopen %s: %s\n", output, strerror(errno));
                rc = -1;
                goto out;
            }
        }
        if (param_one("user", &cfg, frames, fout, input_file) != 0)
            rc = -1;
        if (fout)
            fclose(fout);
        goto out;
    }

    /* Rate-control modes. */
    venc_case_defaults(&cfg, codec, w, h);
    cfg.eRcMode = VENC_RC_MODE_CBR;
    if (param_one("rc-cbr", &cfg, frames, NULL, input_file) != 0) {
        rc = -1;
        goto out;
    }

    cfg.eRcMode = VENC_RC_MODE_VBR;
    if (param_one("rc-vbr", &cfg, frames, NULL, input_file) != 0) {
        rc = -1;
        goto out;
    }

    cfg.eRcMode = VENC_RC_MODE_FIXQP;
    cfg.u32IQp = 30;
    cfg.u32PQp = 32;
    if (param_one("rc-fixqp", &cfg, frames, NULL, input_file) != 0) {
        rc = -1;
        goto out;
    }

    /* Bitrate + GOP variations (back to CBR). */
    venc_case_defaults(&cfg, codec, w, h);
    cfg.u32Bitrate = DEF_BITRATE * 2U;
    cfg.u32Gop = 15;
    if (param_one("bitrate-gop", &cfg, frames, NULL, input_file) != 0) {
        rc = -1;
        goto out;
    }

    /* Rotation. */
    venc_case_defaults(&cfg, codec, w, h);
    cfg.u32RotateDegree = 90;
    if (param_one("rotate", &cfg, frames, NULL, input_file) != 0) {
        rc = -1;
        goto out;
    }

    /* Dynamic mid-stream control. */
    if (param_dynamic(codec, w, h, frames) != 0) {
        rc = -1;
        goto out;
    }

out:
    venc_runtime_down(&rt);
    if (rc != 0)
        return -1;
    printf("[PASS] param suite\n");
    return 0;
}

/* ====================== multi-channel suite ====================== */

typedef struct {
    S32 s32ChnId;
    SynthSource src;
    VencCaseCfg cfg;
    FILE *fout;
    U32 enc;
    U32 key;
    BOOL active;
} MultiChn;

static void multi_chn_close(MultiChn *mc) {
    if (mc->active) {
        (void)VENC_DisableChn(mc->s32ChnId);
        (void)VENC_DestroyChn(mc->s32ChnId);
        mc->active = MPP_FALSE;
    }
    synth_source_destroy(&mc->src);
    if (mc->fout) {
        fclose(mc->fout);
        mc->fout = NULL;
    }
}

static int multi_chn_open(MultiChn *mc, S32 chn, const VencCaseCfg *cfg, const char *prefix) {
    VencChnAttr attr;

    memset(mc, 0, sizeof(*mc));
    mc->s32ChnId = chn;
    mc->cfg = *cfg;

    if (synth_source_create(&mc->src, cfg->u32Width, cfg->u32Height, cfg->u32Align) != 0)
        return -1;

    fill_chn_attr(&attr, cfg);
    if (VENC_CreateChn(chn, &attr) != ERR_VENC_OK) {
        fprintf(stderr, "multi: CreateChn %d failed\n", chn);
        synth_source_destroy(&mc->src);
        return -1;
    }
    if (VENC_EnableChn(chn) != ERR_VENC_OK) {
        fprintf(stderr, "multi: EnableChn %d failed\n", chn);
        (void)VENC_DestroyChn(chn);
        synth_source_destroy(&mc->src);
        return -1;
    }
    mc->active = MPP_TRUE;

    if (prefix) {
        char path[512];
        snprintf(path, sizeof(path), "%s.ch%02d.%s", prefix, chn, codec_ext(cfg->eCodec));
        mc->fout = fopen(path, "wb");
        if (!mc->fout) {
            fprintf(stderr, "multi: fopen %s: %s\n", path, strerror(errno));
            multi_chn_close(mc);
            return -1;
        }
    }
    return 0;
}

static int run_multichn_suite(U32 frames, const char *prefix, int n) {
    VencRuntime rt;
    MultiChn chns[MAX_VENC_CASES];
    int opened = 0;
    int i;
    U32 f;
    int rc = -1;
    static const MppStreamCodecType codecs[] = {
        MPP_STREAM_CODEC_H264, MPP_STREAM_CODEC_H265,
        MPP_STREAM_CODEC_H264, MPP_STREAM_CODEC_MJPEG,
    };
    static const U32 widths[] = {1280U, 640U, 1920U, 320U};
    static const U32 heights[] = {720U, 360U, 1080U, 240U};

    if (frames == 0)
        frames = DEF_FRAMES;
    if (n <= 0)
        n = 2;
    if (n > MAX_VENC_CASES)
        n = MAX_VENC_CASES;
    memset(chns, 0, sizeof(chns));

    if (venc_runtime_up(&rt) != 0)
        return -1;

    for (i = 0; i < n; ++i) {
        VencCaseCfg cfg;
        venc_case_defaults(&cfg, codecs[i], widths[i], heights[i]);
        if (codecs[i] == MPP_STREAM_CODEC_MJPEG)
            cfg.eRcMode = VENC_RC_MODE_FIXQP;
        if (multi_chn_open(&chns[i], i, &cfg, prefix) != 0)
            goto done;
        opened++;
        printf("[multi] chn %d: %s %ux%u\n", i, codec_name(codecs[i]), widths[i], heights[i]);
    }

    /* Round-robin: send one frame to each channel, then drain each. */
    for (f = 0; f < frames; ++f) {
        for (i = 0; i < opened; ++i) {
            VideoFrameInfo frame;
            UL ulBuff = 0;
            S32 ret;

            if (synth_fill_frame(&chns[i].src, f, &frame, &ulBuff) != 0)
                goto done;
            ret = VENC_SendFrame(chns[i].s32ChnId, &frame, VENC_SEND_TIMEOUT_MS);
            if (ret != ERR_VENC_OK) {
                /* Backpressure: drain output and retry. */
                U32 retry;
                for (retry = 0; retry < VENC_SEND_RETRIES; ++retry) {
                    if (drain_streams(chns[i].s32ChnId, chns[i].fout,
                                        &chns[i].enc, &chns[i].key, 0, VENC_DRAIN_BACKOFF_MS) != 0) {
                        (void)VB_ReleaseBuffer(ulBuff);
                        goto done;
                    }
                    ret = VENC_SendFrame(chns[i].s32ChnId, &frame, VENC_SEND_TIMEOUT_MS);
                    if (ret == ERR_VENC_OK)
                        break;
                }
                if (ret != ERR_VENC_OK) {
                    fprintf(stderr, "multi: SendFrame chn %d frame %u: %d (after %u retries)\n",
                            i, f, ret, VENC_SEND_RETRIES);
                    (void)VB_ReleaseBuffer(ulBuff);
                    goto done;
                }
            }
            (void)VB_ReleaseBuffer(ulBuff);
        }
        for (i = 0; i < opened; ++i) {
            if (drain_streams(chns[i].s32ChnId, chns[i].fout, &chns[i].enc, &chns[i].key, 0, 0) != 0)
                goto done;
        }
    }

    /* Drain remaining streams from every channel. */
    for (f = 0; f < 32U; ++f) {
        U32 progressed = 0;
        for (i = 0; i < opened; ++i) {
            U32 before = chns[i].enc;
            if (drain_streams(chns[i].s32ChnId, chns[i].fout, &chns[i].enc, &chns[i].key,
                                0, VENC_FLUSH_TIMEOUT_MS) != 0)
                goto done;
            if (chns[i].enc != before)
                progressed++;
        }
        if (progressed == 0) {
            usleep(5000);
            if (f > 8U)
                break;
        }
    }

    rc = 0;
    for (i = 0; i < opened; ++i) {
        printf("[multi] chn %d: %u stream(s), %u key frame(s)\n", i, chns[i].enc, chns[i].key);
        if (chns[i].enc == 0) {
            fprintf(stderr, "[multi] chn %d produced no stream\n", i);
            rc = -1;
        }
    }

done:
    for (i = 0; i < opened; ++i)
        multi_chn_close(&chns[i]);
    venc_runtime_down(&rt);
    if (rc != 0)
        return -1;
    printf("[PASS] multi-channel suite\n");
    return 0;
}

/* ====================== main ====================== */

int main(int argc, char *argv[]) {
    U32 test_mask = TEST_API;
    U32 w = 0, h = 0, frames = 0;
    U32 bitrate = 0, gop = 0, rotate_deg = 0, framerate = 0;
    int channels = 2;
    BOOL force_idr = MPP_FALSE;
    BOOL set_crop = MPP_FALSE;
    VencCropAttr crop = {0, 0, 0, 0};
    MppStreamCodecType codec = MPP_STREAM_CODEC_UNKNOWN;
    VencRcMode rc_mode = VENC_RC_MODE_MAX;
    const char *output = NULL;
    const char *input = NULL;
    int opt;
    static struct option long_opts[] = {
        {"test", required_argument, NULL, 't'},
        {"width", required_argument, NULL, 'W'},
        {"height", required_argument, NULL, 'H'},
        {"codec", required_argument, NULL, 'c'},
        {"frames", required_argument, NULL, 'n'},
        {"input", required_argument, NULL, 'i'},
        {"output", required_argument, NULL, 'o'},
        {"bitrate", required_argument, NULL, 1000},
        {"gop", required_argument, NULL, 1001},
        {"rc", required_argument, NULL, 1002},
        {"rotate", required_argument, NULL, 1003},
        {"channels", required_argument, NULL, 1004},
        {"crop", required_argument, NULL, 1005},
        {"force-idr", no_argument, NULL, 1006},
        {"framerate", required_argument, NULL, 1007},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    while ((opt = getopt_long(argc, argv, "t:W:H:c:n:i:o:h", long_opts, NULL)) != -1) {
        switch (opt) {
            case 't': {
                U32 m = parse_test_mask(optarg);
                if (m == 0) {
                    print_usage(argv[0]);
                    return 1;
                }
                test_mask = m;
                break;
            }
            case 'W':
                w = (U32)strtoul(optarg, NULL, 10);
                break;
            case 'H':
                h = (U32)strtoul(optarg, NULL, 10);
                break;
            case 'c':
                codec = parse_codec_name(optarg);
                if (codec == MPP_STREAM_CODEC_UNKNOWN) {
                    fprintf(stderr, "unknown codec: %s\n", optarg);
                    return 1;
                }
                break;
            case 'n':
                frames = (U32)strtoul(optarg, NULL, 10);
                break;
            case 'i':
                input = optarg;
                break;
            case 'o':
                output = optarg;
                break;
            case 1000:
                bitrate = (U32)strtoul(optarg, NULL, 10);
                break;
            case 1001:
                gop = (U32)strtoul(optarg, NULL, 10);
                break;
            case 1002:
                rc_mode = parse_rc_name(optarg);
                if (rc_mode == VENC_RC_MODE_MAX) {
                    fprintf(stderr, "unknown rc mode: %s\n", optarg);
                    return 1;
                }
                break;
            case 1003:
                rotate_deg = (U32)strtoul(optarg, NULL, 10);
                break;
            case 1004:
                channels = (int)strtol(optarg, NULL, 10);
                if (channels <= 0 || channels > MAX_VENC_CASES) {
                    fprintf(stderr, "channels must be 1..%d\n", MAX_VENC_CASES);
                    return 1;
                }
                break;
            case 1005: {
                /* --crop L,T,R,B */
                int n = sscanf(optarg, "%d,%d,%d,%d",
                                &crop.s32Left, &crop.s32Top, &crop.s32Right, &crop.s32Bottom);
                if (n != 4) {
                    fprintf(stderr, "--crop requires L,T,R,B (e.g. 0,0,120,68)\n");
                    return 1;
                }
                set_crop = MPP_TRUE;
                break;
            }
            case 1006:
                force_idr = MPP_TRUE;
                break;
            case 1007:
                framerate = (U32)strtoul(optarg, NULL, 10);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (test_mask & TEST_API) {
        if (run_api_suite() != 0)
            return 1;
    }
    if (test_mask & TEST_FMT) {
        if (run_format_suite(codec, w, h, frames, output, input) != 0)
            return 1;
    }
    if (test_mask & TEST_PARAM) {
        if (run_param_suite(codec, w, h, frames, rotate_deg, bitrate, gop, rc_mode,
                            framerate, force_idr, set_crop, &crop, output, input) != 0)
            return 1;
    }
    if (test_mask & TEST_MULTI) {
        if (run_multichn_suite(frames, output, channels) != 0)
            return 1;
    }

    printf("[DONE] all selected suites passed\n");
    return 0;
}
