/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_vdec.c
 * @Brief     :    VDEC tests: API checks; format / scale decode using
 *                 test/vdec/parse (PARSE_*) to split elementary streams into
 *                 access units, feed VDEC_SendStream, append NV12 to a file.
 *
 * Suites (-t): api | format | param | multi | all   (all = api+format+param)
 *
 *   --frames N     max decoded frames to save (0 = entire file). Default 0.
 *   --output PATH  NV12 (Y then UV) append per frame (format / param).
 *
 * Examples:
 *   ./test_vdec --test api
 *   ./test_vdec --test format -i clip.264 -W 1920 -H 1080 -c h264 \\
 *               --output out.nv12 --frames 100
 *   ./test_vdec --test param -i clip.264 -W 1920 -H 1080 -c h264 \\
 *               --scale-width 640 --scale-height 360 --rotate 90 \\
 *               --output out.nv12 --frames 100
 *   ./test_vdec --test multi --cases "h264|a.264|1920|1080;h264|b.264|1280|720" \\
 *               -o /tmp/m --frames 30
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "para.h"
#include "parse.h"
#include "sys_api.h"
#include "vb_api.h"
#include "vdec/vdec_api.h"

#define TEST_API   0x01u
#define TEST_FMT   0x02u
#define TEST_PARAM 0x04u
#define TEST_MULTI 0x08u

#define MAX_MEDIA_CASES 8

typedef struct {
    char path[512];
    U32 width;
    U32 height;
    MppStreamCodecType codec;
} MediaCase;

typedef struct {
    BOOL sys_ok;
    BOOL vb_ok;
    BOOL vdec_ok;
} MediaRuntime;

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  -t, --test <list>     api, format, param, multi, all "
            "(no multi in all)\n"
            "  -i, --input <path>    elementary stream\n"
            "  -W, --width <n>       nominal width (stream hint)\n"
            "  -H, --height <n>      nominal height (stream hint)\n"
            "  -c, --codec <name>    mjpeg | h264 | h265\n"
            "  -n, --frames <n>      max decoded frames to write (0 = all)\n"
            "      --cases <str>     codec|path|w|h;...\n"
            "  -o, --output <path>   NV12: format/param = one file; multi = "
            "name.ch00.nv12, ...\n"
            "      --scale-width <n>  --scale-height <n>  (param suite)\n"
            "      --rotate <deg>    0 | 90 | 180 | 270  (param suite)\n"
            "  -h, --help\n"
            "\n"
            "Examples:\n"
            "  --test api\n"
            "  --test format -i clip.264 -W 1920 -H 1080 -c h264 \\\n"
            "      -o out.nv12 -n 100\n"
            "  --test param -i in.264 -W 1920 -H 1080 -c h264 \\\n"
            "      --scale-width 640 --scale-height 360 -o scaled.nv12\n"
            "  --test param -i in.264 -W 1920 -H 1080 -c h264 \\\n"
            "      --rotate 90 -o rotated.nv12\n"
            "  --test multi --cases "
            "\"h264|a.264|1920|1080;h264|b.264|1280|720\" \\\n"
            "      -o /tmp/m.nv12 -n 30\n",
            prog);
}

static MppCodingType stream_to_coding(MppStreamCodecType t)
{
    switch (t) {
    case MPP_STREAM_CODEC_H264:
        return CODING_H264;
    case MPP_STREAM_CODEC_H265:
        return CODING_H265;
    case MPP_STREAM_CODEC_MJPEG:
        return CODING_MJPEG;
    default:
        return CODING_H264;
    }
}

static MppStreamCodecType parse_codec_name(const char *name)
{
    if (!name)
        return MPP_STREAM_CODEC_UNKNOWN;
    if (strcmp(name, "mjpeg") == 0 || strcmp(name, "jpeg") == 0)
        return MPP_STREAM_CODEC_MJPEG;
    if (strcmp(name, "h264") == 0 || strcmp(name, "avc") == 0)
        return MPP_STREAM_CODEC_H264;
    if (strcmp(name, "h265") == 0 || strcmp(name, "hevc") == 0)
        return MPP_STREAM_CODEC_H265;
    return MPP_STREAM_CODEC_UNKNOWN;
}

static const char *codec_name(MppStreamCodecType c)
{
    switch (c) {
    case MPP_STREAM_CODEC_MJPEG:
        return "mjpeg";
    case MPP_STREAM_CODEC_H264:
        return "h264";
    case MPP_STREAM_CODEC_H265:
        return "h265";
    default:
        return "unknown";
    }
}

static U32 parse_test_mask(const char *s)
{
    U32 m = 0;
    char *tmp = strdup(s);
    if (!tmp)
        return 0;
    for (char *tok = strtok(tmp, ","); tok; tok = strtok(NULL, ",")) {
        while (*tok == ' ' || *tok == '\t')
            ++tok;
        if (strcmp(tok, "api") == 0)
            m |= TEST_API;
        else if (strcmp(tok, "format") == 0)
            m |= TEST_FMT;
        else if (strcmp(tok, "param") == 0)
            m |= TEST_PARAM;
        else if (strcmp(tok, "multi") == 0)
            m |= TEST_MULTI;
        else if (strcmp(tok, "all") == 0)
            m |= TEST_API | TEST_FMT | TEST_PARAM;
        else {
            free(tmp);
            return 0;
        }
    }
    free(tmp);
    return m;
}

static int append_media_case(MediaCase *cases, int *n, const char *rec)
{
    char buf[768];
    char *p0, *p1, *p2, *p3;

    if (*n >= MAX_MEDIA_CASES) {
        fprintf(stderr, "too many media cases (max %d)\n", MAX_MEDIA_CASES);
        return -1;
    }
    if (strlen(rec) >= sizeof(buf)) {
        fprintf(stderr, "case string too long\n");
        return -1;
    }
    memcpy(buf, rec, strlen(rec) + 1);

    p0 = buf;
    p1 = strchr(p0, '|');
    if (!p1) {
        fprintf(stderr, "bad --cases field (need codec|path|w|h): %s\n", rec);
        return -1;
    }
    *p1++ = '\0';
    p2 = strchr(p1, '|');
    if (!p2) {
        fprintf(stderr, "bad --cases field: %s\n", rec);
        return -1;
    }
    *p2++ = '\0';
    p3 = strchr(p2, '|');
    if (!p3) {
        fprintf(stderr, "bad --cases field: %s\n", rec);
        return -1;
    }
    *p3++ = '\0';

    cases[*n].codec  = parse_codec_name(p0);
    cases[*n].width  = (U32)strtoul(p2, NULL, 10);
    cases[*n].height = (U32)strtoul(p3, NULL, 10);
    if (cases[*n].codec == MPP_STREAM_CODEC_UNKNOWN ||
        cases[*n].width == 0 || cases[*n].height == 0) {
        fprintf(stderr, "invalid case line: %s\n", rec);
        return -1;
    }
    if (strlen(p1) >= sizeof(cases[*n].path)) {
        fprintf(stderr, "path too long in case\n");
        return -1;
    }
    memcpy(cases[*n].path, p1, strlen(p1) + 1);
    (*n)++;
    return 0;
}

static int parse_cases_arg(MediaCase *cases, int *n, const char *arg)
{
    char *buf = strdup(arg);
    if (!buf)
        return -1;
    for (char *r = strtok(buf, ";"); r; r = strtok(NULL, ";")) {
        while (*r == ' ' || *r == '\t')
            ++r;
        if (*r == '\0')
            continue;
        if (append_media_case(cases, n, r) != 0) {
            free(buf);
            return -1;
        }
    }
    free(buf);
    return 0;
}

static U8 *read_whole_file(const char *path, long *size_out)
{
    FILE *fp = fopen(path, "rb");
    long sz;
    U8 *buf;

    if (!fp) {
        fprintf(stderr, "fopen failed %s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (sz = ftell(fp)) <= 0) {
        fclose(fp);
        fprintf(stderr, "bad file size: %s\n", path);
        return NULL;
    }
    rewind(fp);
    buf = (U8 *)malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        fprintf(stderr, "fread failed: %s\n", path);
        return NULL;
    }
    fclose(fp);
    *size_out = sz;
    return buf;
}

static void media_runtime_down(MediaRuntime *rt)
{
    if (rt->vdec_ok) {
        (void)VDEC_Exit();
        rt->vdec_ok = MPP_FALSE;
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

static int media_runtime_up(MediaRuntime *rt)
{
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
        media_runtime_down(rt);
        return -1;
    }
    rt->vb_ok = MPP_TRUE;
    ret = VDEC_Init();
    if (ret != 0) {
        fprintf(stderr, "VDEC_Init failed: %d\n", ret);
        media_runtime_down(rt);
        return -1;
    }
    rt->vdec_ok = MPP_TRUE;
    return 0;
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

/**
 * Advance source pointer after a successful PARSE_*_Parse (payload in frame_buf).
 */
static int advance_after_parse(MppStreamCodecType codec, U8 *p, S32 left,
                               const U8 *frame_buf, S32 frame_sz, U8 **next_p,
                               S32 *next_left)
{
    void *hit;
    S32 adv;

    if (!p || left <= 0 || frame_sz <= 0 || !frame_buf) {
        return -1;
    }
    if (frame_sz > left) {
        return -1;
    }

    hit = memmem(p, (size_t)left, frame_buf, (size_t)frame_sz);
    if (!hit) {
        if (frame_sz == left && memcmp(p, frame_buf, (size_t)frame_sz) == 0) {
            hit = p;
        } else {
            return -1;
        }
    }

    adv = frame_sz;
    if (codec == MPP_STREAM_CODEC_MJPEG) {
        U8 *q = (U8 *)hit + frame_sz;
        if (q + 1 < p + left && q[0] == 0xff && q[1] == 0xd9)
            adv += 2;
    }

    *next_p    = (U8 *)hit + adv;
    *next_left = left - (S32)(*next_p - p);
    if (*next_left < 0)
        *next_left = 0;
    return 0;
}

static int drain_available_frames(S32 chn, FILE *nv12_out, U32 *decoded_count,
                                 U32 max_frames, U32 *last_w, U32 *last_h)
{
    VideoFrameInfo frame;
    S32 ret;

    for (;;) {
        memset(&frame, 0, sizeof(frame));
        ret = VDEC_GetFrame(chn, &frame, 80);
        if (ret == ERR_VDEC_NO_FRAME || ret == ERR_VDEC_TIMEOUT)
            break;
        if (ret == ERR_VDEC_EOS) {
            break;
        }
        if (ret != 0) {
            fprintf(stderr, "VDEC_GetFrame: %d\n", ret);
            return -1;
        }

        *last_w = frame.stVdecFrameInfo.stCommFrameInfo.u32Width;
        *last_h = frame.stVdecFrameInfo.stCommFrameInfo.u32Height;

        if (nv12_out && save_nv12_frame(nv12_out, &frame) != 0) {
            fprintf(stderr, "save_nv12_frame failed\n");
            (void)VDEC_ReleaseFrame(chn, frame.ulBufferId);
            return -1;
        }
        (*decoded_count)++;
        ret = VDEC_ReleaseFrame(chn, frame.ulBufferId);
        if (ret != 0) {
            fprintf(stderr, "VDEC_ReleaseFrame: %d\n", ret);
            return -1;
        }

        if (max_frames > 0 && *decoded_count >= max_frames)
            break;
    }
    return 0;
}

static int send_eos_packet(S32 chn, MppStreamCodecType codec)
{
    static U8 dummy;
    StreamBufferInfo s;
    memset(&s, 0, sizeof(s));
    s.pu8Addr      = &dummy;
    s.u32Size      = 0;
    s.bKeyFrame    = MPP_FALSE;
    s.bEndOfStream = MPP_TRUE;
    s.eCodecType   = codec;
    s.u64PTS       = 0;
    return VDEC_SendStream(chn, &s, 3000);
}

static int decode_media_with_parse(S32 chn, const MediaCase *mc, U8 *file_buf,
                                   long file_sz, BOOL scale_en, U32 sc_w, U32 sc_h,
                                   U32 max_frames, FILE *nv12_out,
                                   U32 *out_decoded, U32 *out_fw, U32 *out_fh)
{
    MppParseContext *pctx = NULL;
    U8 *frame_buf = NULL;
    VdecChnAttr attr;
    U8 *p;
    S32 left;
    S32 is_first = 1;
    S32 frame_size = 0;
    S32 pret;
    U32 dec_count = 0;
    U32 last_w = 0, last_h = 0;
    U64 pts = 0;
    S32 ret;
    U8 *next_p;
    S32 next_left;
    S32 pass;

    memset(&attr, 0, sizeof(attr));
    attr.eCodecType         = mc->codec;
    attr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    attr.u32Align           = 0;
    attr.u32Width           = mc->width;
    attr.u32Height          = mc->height;
    attr.bIsFrameReordering = MPP_FALSE;
    attr.bDispErrorFrame    = MPP_FALSE;
    attr.stScale.bScaleEnable = scale_en;
    attr.stScale.u32Width     = sc_w;
    attr.stScale.u32Height    = sc_h;

    ret = VDEC_CreateChn(chn, &attr);
    if (ret != 0) {
        fprintf(stderr, "VDEC_CreateChn: %d\n", ret);
        return -1;
    }
    ret = VDEC_EnableChn(chn);
    if (ret != 0) {
        fprintf(stderr, "VDEC_EnableChn: %d\n", ret);
        (void)VDEC_DestroyChn(chn);
        return -1;
    }

    pctx = PARSE_Create(stream_to_coding(mc->codec));
    if (!pctx || !pctx->ops || !pctx->ops->init || !pctx->ops->parse) {
        fprintf(stderr, "PARSE_Create failed\n");
        goto err;
    }
    if (pctx->ops->init(pctx) != MPP_OK) {
        fprintf(stderr, "parse init failed\n");
        goto err;
    }

    frame_buf = (U8 *)malloc(STREAM_BUFFER_SIZE);
    if (!frame_buf) {
        fprintf(stderr, "malloc frame buf\n");
        goto err;
    }

    p    = file_buf;
    left = (S32)file_sz;

    while (left > 0) {
        if (max_frames > 0 && dec_count >= max_frames)
            break;

        pret = pctx->ops->parse(pctx, p, left, frame_buf, &frame_size, is_first);
        if (pret != 0) {
            if (pret == 1 && mc->codec == MPP_STREAM_CODEC_H265) {
                if (left <= 1)
                    break;
                p++;
                left--;
                continue;
            }
            if (pret == 2 && mc->codec == MPP_STREAM_CODEC_MJPEG)
                break;
            if (pret < 0 && mc->codec == MPP_STREAM_CODEC_H264)
                break;
            if (left <= 1)
                break;
            p++;
            left--;
            continue;
        }

        is_first = 0;

        if (frame_size <= 0 || frame_size > STREAM_BUFFER_SIZE) {
            fprintf(stderr, "parse: bad frame_size %d\n", frame_size);
            goto err;
        }

        if (advance_after_parse(mc->codec, p, left, frame_buf, frame_size,
                                &next_p, &next_left) != 0) {
            fprintf(stderr, "parse: could not align stream (size=%d)\n",
                    frame_size);
            goto err;
        }

        {
            StreamBufferInfo stream;
            memset(&stream, 0, sizeof(stream));
            stream.pu8Addr      = frame_buf;
            stream.u32Size      = (U32)frame_size;
            stream.bKeyFrame    = MPP_FALSE;
            stream.bEndOfStream = MPP_FALSE;
            stream.eCodecType   = mc->codec;
            stream.u64PTS       = pts++;
            stream.u32Width     = mc->width;
            stream.u32Height    = mc->height;

            ret = VDEC_SendStream(chn, &stream, 3000);
            if (ret != 0 && ret != ERR_VDEC_EOS) {
                fprintf(stderr, "VDEC_SendStream: %d\n", ret);
                goto err;
            }
        }

        if (drain_available_frames(chn, nv12_out, &dec_count, max_frames, &last_w,
                                   &last_h) != 0) {
            goto err;
        }

        if (max_frames > 0 && dec_count >= max_frames)
            break;

        p    = next_p;
        left = next_left;
    }

    (void)send_eos_packet(chn, mc->codec);

    for (pass = 0; pass < 64; ++pass) {
        U32 before = dec_count;
        if (drain_available_frames(chn, nv12_out, &dec_count, max_frames, &last_w,
                                   &last_h) != 0) {
            goto err;
        }
        if (max_frames > 0 && dec_count >= max_frames)
            break;
        if (dec_count == before) {
            usleep(5000);
            if (pass > 8 && dec_count == before)
                break;
        }
    }

    *out_decoded = dec_count;
    *out_fw      = last_w;
    *out_fh      = last_h;

    free(frame_buf);
    PARSE_Destory(pctx);
    ret = VDEC_DisableChn(chn);
    if (ret != 0)
        fprintf(stderr, "VDEC_DisableChn: %d\n", ret);
    ret = VDEC_DestroyChn(chn);
    if (ret != 0)
        fprintf(stderr, "VDEC_DestroyChn: %d\n", ret);
    printf("[decode] %s decoded_frames=%u last %ux%u\n", mc->path, dec_count,
           last_w, last_h);
    return 0;

err:
    free(frame_buf);
    PARSE_Destory(pctx);
    (void)VDEC_DisableChn(chn);
    (void)VDEC_DestroyChn(chn);
    return -1;
}

/**
 * Extended decode helper with rotation support.
 */
static int decode_media_with_parse_ex(S32 chn, const MediaCase *mc, U8 *file_buf,
                                      long file_sz, BOOL scale_en, U32 sc_w,
                                      U32 sc_h, U32 rotate_deg, U32 max_frames,
                                      FILE *nv12_out, U32 *out_decoded,
                                      U32 *out_fw, U32 *out_fh)
{
    MppParseContext *pctx = NULL;
    U8 *frame_buf = NULL;
    VdecChnAttr attr;
    U8 *p;
    S32 left;
    S32 is_first = 1;
    S32 frame_size = 0;
    S32 pret;
    U32 dec_count = 0;
    U32 last_w = 0, last_h = 0;
    U64 pts = 0;
    S32 ret;
    U8 *next_p;
    S32 next_left;
    S32 pass;

    memset(&attr, 0, sizeof(attr));
    attr.eCodecType         = mc->codec;
    attr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    attr.u32Align           = 0;
    attr.u32Width           = mc->width;
    attr.u32Height          = mc->height;
    attr.bIsFrameReordering = MPP_FALSE;
    attr.bDispErrorFrame    = MPP_FALSE;
    attr.u32RotateDegree    = rotate_deg;
    attr.stScale.bScaleEnable = scale_en;
    attr.stScale.u32Width     = sc_w;
    attr.stScale.u32Height    = sc_h;

    ret = VDEC_CreateChn(chn, &attr);
    if (ret != 0) {
        fprintf(stderr, "VDEC_CreateChn: %d\n", ret);
        return -1;
    }
    ret = VDEC_EnableChn(chn);
    if (ret != 0) {
        fprintf(stderr, "VDEC_EnableChn: %d\n", ret);
        (void)VDEC_DestroyChn(chn);
        return -1;
    }

    pctx = PARSE_Create(stream_to_coding(mc->codec));
    if (!pctx || !pctx->ops || !pctx->ops->init || !pctx->ops->parse) {
        fprintf(stderr, "PARSE_Create failed\n");
        goto err;
    }
    if (pctx->ops->init(pctx) != MPP_OK) {
        fprintf(stderr, "parse init failed\n");
        goto err;
    }

    frame_buf = (U8 *)malloc(STREAM_BUFFER_SIZE);
    if (!frame_buf) {
        fprintf(stderr, "malloc frame buf\n");
        goto err;
    }

    p    = file_buf;
    left = (S32)file_sz;

    while (left > 0) {
        if (max_frames > 0 && dec_count >= max_frames)
            break;

        pret = pctx->ops->parse(pctx, p, left, frame_buf, &frame_size, is_first);
        if (pret != 0) {
            if (pret == 1 && mc->codec == MPP_STREAM_CODEC_H265) {
                if (left <= 1)
                    break;
                p++;
                left--;
                continue;
            }
            if (pret == 2 && mc->codec == MPP_STREAM_CODEC_MJPEG)
                break;
            if (pret < 0 && mc->codec == MPP_STREAM_CODEC_H264)
                break;
            if (left <= 1)
                break;
            p++;
            left--;
            continue;
        }

        is_first = 0;

        if (frame_size <= 0 || frame_size > STREAM_BUFFER_SIZE) {
            fprintf(stderr, "parse: bad frame_size %d\n", frame_size);
            goto err;
        }

        if (advance_after_parse(mc->codec, p, left, frame_buf, frame_size,
                                &next_p, &next_left) != 0) {
            fprintf(stderr, "parse: could not align stream (size=%d)\n",
                    frame_size);
            goto err;
        }

        {
            StreamBufferInfo stream;
            memset(&stream, 0, sizeof(stream));
            stream.pu8Addr      = frame_buf;
            stream.u32Size      = (U32)frame_size;
            stream.bKeyFrame    = MPP_FALSE;
            stream.bEndOfStream = MPP_FALSE;
            stream.eCodecType   = mc->codec;
            stream.u64PTS       = pts++;
            stream.u32Width     = mc->width;
            stream.u32Height    = mc->height;

            ret = VDEC_SendStream(chn, &stream, 3000);
            if (ret != 0 && ret != ERR_VDEC_EOS) {
                fprintf(stderr, "VDEC_SendStream: %d\n", ret);
                goto err;
            }
        }

        if (drain_available_frames(chn, nv12_out, &dec_count, max_frames, &last_w,
                                   &last_h) != 0) {
            goto err;
        }

        if (max_frames > 0 && dec_count >= max_frames)
            break;

        p    = next_p;
        left = next_left;
    }

    (void)send_eos_packet(chn, mc->codec);

    for (pass = 0; pass < 64; ++pass) {
        U32 before = dec_count;
        if (drain_available_frames(chn, nv12_out, &dec_count, max_frames, &last_w,
                                   &last_h) != 0) {
            goto err;
        }
        if (max_frames > 0 && dec_count >= max_frames)
            break;
        if (dec_count == before) {
            usleep(5000);
            if (pass > 8 && dec_count == before)
                break;
        }
    }

    *out_decoded = dec_count;
    *out_fw      = last_w;
    *out_fh      = last_h;

    free(frame_buf);
    PARSE_Destory(pctx);
    ret = VDEC_DisableChn(chn);
    if (ret != 0)
        fprintf(stderr, "VDEC_DisableChn: %d\n", ret);
    ret = VDEC_DestroyChn(chn);
    if (ret != 0)
        fprintf(stderr, "VDEC_DestroyChn: %d\n", ret);
    printf("[decode_ex] %s decoded_frames=%u last %ux%u (rot=%u)\n", mc->path,
           dec_count, last_w, last_h, rotate_deg);
    return 0;

err:
    free(frame_buf);
    PARSE_Destory(pctx);
    (void)VDEC_DisableChn(chn);
    (void)VDEC_DestroyChn(chn);
    return -1;
}

static int make_chn_nv12_path(const char *base, S32 chn, char *out, size_t outsz)
{
    const char *dot = strrchr(base, '.');

    if (dot && dot != base) {
        if ((size_t)(dot - base) + strlen(dot) + 32 >= outsz)
            return -1;
        snprintf(out, outsz, "%.*s.ch%02d%s", (int)(dot - base), base, (int)chn,
                 dot);
    } else {
        snprintf(out, outsz, "%s.ch%02d.nv12", base, (int)chn);
    }
    return 0;
}

static int run_multichn_suite(const MediaCase *cases, int n_chn, U32 max_frames,
                              const char *nv12_base)
{
    MediaRuntime rt;
    typedef struct {
        U8 *file;
        long file_sz;
        MppParseContext *pctx;
        U8 *frame_buf;
        U8 *p;
        S32 left;
        S32 is_first;
        U64 pts;
        U32 dec_count;
        FILE *out;
        U32 lw, lh;
    } ChnSt;
    ChnSt st[MAX_MEDIA_CASES];
    S32 ch;
    int i;
    S32 ret;
    S32 pass;

    if (n_chn < 2) {
        fprintf(stderr,
                "multi suite: need at least 2 streams (--cases or repeated inputs)\n");
        return -1;
    }
    if (n_chn > MAX_MEDIA_CASES) {
        fprintf(stderr, "multi suite: too many channels\n");
        return -1;
    }

    memset(st, 0, sizeof(st));

    if (media_runtime_up(&rt) != 0)
        return -1;

    for (i = 0; i < n_chn; ++i) {
        st[i].file = read_whole_file(cases[i].path, &st[i].file_sz);
        if (!st[i].file) {
            goto fail_free;
        }
        st[i].p        = st[i].file;
        st[i].left     = (S32)st[i].file_sz;
        st[i].is_first = 1;
        st[i].pctx     = PARSE_Create(stream_to_coding(cases[i].codec));
        if (!st[i].pctx || !st[i].pctx->ops || !st[i].pctx->ops->init ||
            !st[i].pctx->ops->parse) {
            fprintf(stderr, "PARSE_Create failed ch %d\n", i);
            goto fail_free;
        }
        if (st[i].pctx->ops->init(st[i].pctx) != MPP_OK) {
            fprintf(stderr, "parse init failed ch %d\n", i);
            goto fail_free;
        }
        st[i].frame_buf = (U8 *)malloc(STREAM_BUFFER_SIZE);
        if (!st[i].frame_buf) {
            goto fail_free;
        }
        if (nv12_base) {
            char opath[768];
            if (make_chn_nv12_path(nv12_base, (S32)i, opath, sizeof(opath)) != 0) {
                fprintf(stderr, "output path too long\n");
                goto fail_free;
            }
            st[i].out = fopen(opath, "wb");
            if (!st[i].out) {
                fprintf(stderr, "fopen %s: %s\n", opath, strerror(errno));
                goto fail_free;
            }
            printf("[multi] ch%d -> %s\n", i, opath);
        }
    }

    for (ch = 0; ch < n_chn; ++ch) {
        VdecChnAttr attr;
        memset(&attr, 0, sizeof(attr));
        attr.eCodecType            = cases[ch].codec;
        attr.eOutputPixelFormat    = MPP_PIXEL_FORMAT_NV12;
        attr.u32Width              = cases[ch].width;
        attr.u32Height             = cases[ch].height;
        attr.bIsFrameReordering    = MPP_FALSE;
        attr.bDispErrorFrame       = MPP_FALSE;
        attr.stScale.bScaleEnable  = MPP_FALSE;

        ret = VDEC_CreateChn(ch, &attr);
        if (ret != 0) {
            fprintf(stderr, "VDEC_CreateChn %d: %d\n", (int)ch, ret);
            goto fail_teardown_chn;
        }
        ret = VDEC_EnableChn(ch);
        if (ret != 0) {
            fprintf(stderr, "VDEC_EnableChn %d: %d\n", (int)ch, ret);
            goto fail_teardown_chn;
        }
    }

    printf("[multi] round-robin %d channels\n", n_chn);

    for (;;) {
        int any_left = 0;
        int progress = 0;

        for (i = 0; i < n_chn; ++i) {
            MppStreamCodecType cdec = cases[i].codec;
            S32 frame_size = 0;
            S32 pret;
            U8 *next_p;
            S32 next_left;

            if (st[i].left <= 0)
                continue;
            any_left = 1;
            if (max_frames > 0 && st[i].dec_count >= max_frames)
                continue;

            pret = st[i].pctx->ops->parse(st[i].pctx, st[i].p, st[i].left,
                                         st[i].frame_buf, &frame_size,
                                         st[i].is_first);
            if (pret != 0) {
                if (pret == 1 && cdec == MPP_STREAM_CODEC_H265) {
                    if (st[i].left <= 1) {
                        st[i].left = 0;
                        continue;
                    }
                    st[i].p++;
                    st[i].left--;
                    progress = 1;
                    continue;
                }
                if (pret == 2 && cdec == MPP_STREAM_CODEC_MJPEG) {
                    st[i].left = 0;
                    continue;
                }
                if (pret < 0 && cdec == MPP_STREAM_CODEC_H264) {
                    st[i].left = 0;
                    continue;
                }
                if (st[i].left <= 1) {
                    st[i].left = 0;
                    continue;
                }
                st[i].p++;
                st[i].left--;
                progress = 1;
                continue;
            }

            st[i].is_first = 0;

            if (frame_size <= 0 || frame_size > STREAM_BUFFER_SIZE) {
                fprintf(stderr, "multi ch%d bad frame_size %d\n", i, frame_size);
                goto fail_teardown_chn;
            }

            if (advance_after_parse(cdec, st[i].p, st[i].left, st[i].frame_buf,
                                    frame_size, &next_p, &next_left) != 0) {
                fprintf(stderr, "multi ch%d advance failed\n", i);
                goto fail_teardown_chn;
            }

            {
                StreamBufferInfo stream;
                memset(&stream, 0, sizeof(stream));
                stream.pu8Addr      = st[i].frame_buf;
                stream.u32Size      = (U32)frame_size;
                stream.bKeyFrame    = MPP_FALSE;
                stream.bEndOfStream = MPP_FALSE;
                stream.eCodecType   = cdec;
                stream.u64PTS       = st[i].pts++;
                stream.u32Width     = cases[i].width;
                stream.u32Height    = cases[i].height;

                ret = VDEC_SendStream((S32)i, &stream, 3000);
                if (ret != 0 && ret != ERR_VDEC_EOS) {
                    fprintf(stderr, "VDEC_SendStream ch%d: %d\n", i, ret);
                    goto fail_teardown_chn;
                }
            }

            if (drain_available_frames((S32)i, st[i].out, &st[i].dec_count,
                                       max_frames, &st[i].lw, &st[i].lh) != 0) {
                goto fail_teardown_chn;
            }

            st[i].p    = next_p;
            st[i].left = next_left;
            progress   = 1;
        }

        if (!any_left)
            break;
        if (!progress)
            break;
    }

    for (ch = 0; ch < n_chn; ++ch) {
        (void)send_eos_packet(ch, cases[ch].codec);
        for (pass = 0; pass < 64; ++pass) {
            U32 before = st[ch].dec_count;
            if (drain_available_frames(ch, st[ch].out, &st[ch].dec_count,
                                       max_frames, &st[ch].lw, &st[ch].lh) !=
                0) {
                goto fail_teardown_chn;
            }
            if (max_frames > 0 && st[ch].dec_count >= max_frames)
                break;
            if (st[ch].dec_count == before) {
                usleep(5000);
                if (pass > 8 && st[ch].dec_count == before)
                    break;
            }
        }
    }

    for (ch = 0; ch < n_chn; ++ch) {
        printf("[multi] ch%u: %s frames=%u last %ux%u\n", (unsigned)ch,
               cases[ch].path, st[ch].dec_count, st[ch].lw, st[ch].lh);
        ret = VDEC_DisableChn(ch);
        if (ret != 0)
            fprintf(stderr, "VDEC_DisableChn %d: %d\n", (int)ch, ret);
        ret = VDEC_DestroyChn(ch);
        if (ret != 0)
            fprintf(stderr, "VDEC_DestroyChn %d: %d\n", (int)ch, ret);
    }

    for (i = 0; i < n_chn; ++i) {
        if (st[i].out)
            fclose(st[i].out);
        free(st[i].frame_buf);
        PARSE_Destory(st[i].pctx);
        free(st[i].file);
    }

    media_runtime_down(&rt);
    printf("[PASS] multi suite (%d channels)\n", n_chn);
    return 0;

fail_teardown_chn:
    for (ch = 0; ch < n_chn; ++ch) {
        (void)VDEC_DisableChn(ch);
        (void)VDEC_DestroyChn(ch);
    }
fail_free:
    for (i = 0; i < n_chn; ++i) {
        if (st[i].out)
            fclose(st[i].out);
        free(st[i].frame_buf);
        PARSE_Destory(st[i].pctx);
        free(st[i].file);
    }
    media_runtime_down(&rt);
    return -1;
}

static int run_format_suite(const MediaCase *cases, int n_case, U32 max_frames,
                            const char *nv12_path)
{
    MediaRuntime rt;
    FILE *out = NULL;
    int i;

    if (n_case == 0) {
        fprintf(stderr,
                "format suite: use --input/--width/--height/--codec or --cases\n");
        return -1;
    }
    if (media_runtime_up(&rt) != 0)
        return -1;

    if (nv12_path) {
        out = fopen(nv12_path, "wb");
        if (!out) {
            fprintf(stderr, "fopen output: %s\n", strerror(errno));
            media_runtime_down(&rt);
            return -1;
        }
    }

    for (i = 0; i < n_case; ++i) {
        long sz;
        U8 *buf = read_whole_file(cases[i].path, &sz);
        U32 dc, fw, fh;

        if (!buf) {
            if (out)
                fclose(out);
            media_runtime_down(&rt);
            return -1;
        }
        printf("[format] %s (%s)\n", cases[i].path, codec_name(cases[i].codec));
        if (decode_media_with_parse(0, &cases[i], buf, sz, MPP_FALSE, 0, 0,
                                    max_frames, out, &dc, &fw, &fh) != 0) {
            free(buf);
            if (out)
                fclose(out);
            media_runtime_down(&rt);
            return -1;
        }
        free(buf);
    }

    if (out)
        fclose(out);
    media_runtime_down(&rt);
    printf("[PASS] format suite (%d case(s))\n", n_case);
    return 0;
}

static int run_param_suite(const MediaCase *cases, int n_case, U32 sc_w, U32 sc_h,
                           U32 rotate_deg, U32 max_frames, const char *nv12_path)
{
    MediaRuntime rt;
    FILE *out = NULL;
    int i;
    BOOL has_scale = (sc_w > 0 && sc_h > 0) ? MPP_TRUE : MPP_FALSE;
    BOOL has_rot   = (rotate_deg == 90 || rotate_deg == 180 || rotate_deg == 270)
                         ? MPP_TRUE : MPP_FALSE;

    if (n_case == 0) {
        fprintf(stderr, "param suite: need media\n");
        return -1;
    }
    if (!has_scale && !has_rot) {
        fprintf(stderr,
                "param suite: need --scale-width/--scale-height and/or --rotate\n");
        return -1;
    }
    if (has_rot && rotate_deg != 0 && rotate_deg != 90 &&
        rotate_deg != 180 && rotate_deg != 270) {
        fprintf(stderr, "param suite: --rotate must be 0, 90, 180, or 270\n");
        return -1;
    }
    if (media_runtime_up(&rt) != 0)
        return -1;

    if (nv12_path) {
        out = fopen(nv12_path, "wb");
        if (!out) {
            fprintf(stderr, "fopen output: %s\n", strerror(errno));
            media_runtime_down(&rt);
            return -1;
        }
    }

    for (i = 0; i < n_case; ++i) {
        long sz;
        U8 *buf = read_whole_file(cases[i].path, &sz);
        U32 dc, fw, fh;

        if (!buf) {
            if (out)
                fclose(out);
            media_runtime_down(&rt);
            return -1;
        }
        printf("[param] %s scale=%s(%ux%u) rotate=%u\n", cases[i].path,
               has_scale ? "on" : "off", sc_w, sc_h, rotate_deg);
        if (decode_media_with_parse_ex(0, &cases[i], buf, sz, has_scale, sc_w,
                                       sc_h, rotate_deg, max_frames, out, &dc,
                                       &fw, &fh) != 0) {
            free(buf);
            if (out)
                fclose(out);
            media_runtime_down(&rt);
            return -1;
        }
        if (dc > 0 && has_scale) {
            if (fw + 16 < sc_w || fh + 16 < sc_h) {
                fprintf(stderr, "param(scale): %ux%u vs target %ux%u\n", fw, fh,
                        sc_w, sc_h);
                free(buf);
                if (out)
                    fclose(out);
                media_runtime_down(&rt);
                return -1;
            }
            if (fw > sc_w + 64 || fh > sc_h + 64) {
                fprintf(stderr, "param(scale): output much larger than target\n");
                free(buf);
                if (out)
                    fclose(out);
                media_runtime_down(&rt);
                return -1;
            }
        }
        free(buf);
    }

    if (out)
        fclose(out);
    media_runtime_down(&rt);
    printf("[PASS] param suite (%d case(s), scale=%s rot=%u)\n", n_case,
           has_scale ? "on" : "off", rotate_deg);
    return 0;
}

static int run_api_suite(void)
{
    VdecChnAttr attr;
    VdecChnStatus st;
    StreamBufferInfo stream;
    VideoFrameInfo frame;
    S32 r;

    printf("[api] error-path checks\n");

    r = VDEC_Exit();
    if (r != ERR_VDEC_NOT_INIT) {
        fprintf(stderr, "VDEC_Exit w/o init: got %d expect NOT_INIT\n", r);
        return -1;
    }

    r = VDEC_Init();
    if (r != 0) {
        fprintf(stderr, "VDEC_Init: %d\n", r);
        return -1;
    }

    r = VDEC_Init();
    if (r != ERR_VDEC_ALREADY_INIT) {
        fprintf(stderr, "double VDEC_Init: got %d expect ALREADY_INIT\n", r);
        (void)VDEC_Exit();
        return -1;
    }

    memset(&attr, 0, sizeof(attr));
    attr.eCodecType         = MPP_STREAM_CODEC_MJPEG;
    attr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    attr.u32Width           = 128;
    attr.u32Height          = 128;

    r = VDEC_CreateChn(-1, &attr);
    if (r != ERR_VDEC_INVALID_CHN) {
        fprintf(stderr, "CreateChn invalid id: %d\n", r);
        (void)VDEC_Exit();
        return -1;
    }
    r = VDEC_CreateChn(VDEC_MAX_CHN, &attr);
    if (r != ERR_VDEC_INVALID_CHN) {
        fprintf(stderr, "CreateChn max id: %d\n", r);
        (void)VDEC_Exit();
        return -1;
    }
    r = VDEC_CreateChn(0, NULL);
    if (r != ERR_VDEC_NULL_PTR) {
        fprintf(stderr, "CreateChn NULL attr: %d\n", r);
        (void)VDEC_Exit();
        return -1;
    }

    r = VDEC_DestroyChn(0);
    if (r != ERR_VDEC_INVALID_CHN) {
        fprintf(stderr, "DestroyChn no channel: %d\n", r);
        (void)VDEC_Exit();
        return -1;
    }

    r = VDEC_EnableChn(0);
    if (r != ERR_VDEC_INVALID_CHN) {
        fprintf(stderr, "EnableChn no channel: %d\n", r);
        (void)VDEC_Exit();
        return -1;
    }

    memset(&frame, 0, sizeof(frame));
    r = VDEC_GetFrame(0, &frame, 0);
    if (r != ERR_VDEC_NOT_STARTED) {
        fprintf(stderr, "GetFrame no channel: %d (expect NOT_STARTED)\n", r);
        (void)VDEC_Exit();
        return -1;
    }
    r = VDEC_GetFrame(0, NULL, 0);
    if (r != ERR_VDEC_NULL_PTR) {
        fprintf(stderr, "GetFrame NULL: %d\n", r);
        (void)VDEC_Exit();
        return -1;
    }

    r = VDEC_QueryStatus(0, NULL);
    if (r != ERR_VDEC_NULL_PTR) {
        fprintf(stderr, "QueryStatus NULL: %d\n", r);
        (void)VDEC_Exit();
        return -1;
    }
    r = VDEC_QueryStatus(0, &st);
    if (r != ERR_VDEC_INVALID_CHN) {
        fprintf(stderr, "QueryStatus no channel: %d\n", r);
        (void)VDEC_Exit();
        return -1;
    }

    memset(&stream, 0, sizeof(stream));
    r = VDEC_SendStream(0, &stream, 0);
    if (r != ERR_VDEC_NOT_STARTED) {
        fprintf(stderr, "SendStream no channel: %d (expect NOT_STARTED)\n", r);
        (void)VDEC_Exit();
        return -1;
    }
    r = VDEC_SendStream(0, NULL, 0);
    if (r != ERR_VDEC_NULL_PTR) {
        fprintf(stderr, "SendStream NULL: %d\n", r);
        (void)VDEC_Exit();
        return -1;
    }

    r = VDEC_ReleaseFrame(0, 0);
    if (r != ERR_VDEC_NULL_PTR) {
        fprintf(stderr, "ReleaseFrame 0: %d\n", r);
        (void)VDEC_Exit();
        return -1;
    }

    r = VDEC_CreateChn(0, &attr);
    if (r != 0) {
        fprintf(stderr, "CreateChn ok path: %d\n", r);
        (void)VDEC_Exit();
        return -1;
    }
    r = VDEC_DestroyChn(0);
    if (r != 0) {
        fprintf(stderr, "DestroyChn after create: %d\n", r);
        (void)VDEC_Exit();
        return -1;
    }

    (void)VDEC_Exit();
    r = VDEC_Exit();
    if (r != ERR_VDEC_NOT_INIT) {
        fprintf(stderr, "VDEC_Exit double: %d\n", r);
        return -1;
    }

    printf("[PASS] api suite\n");
    return 0;
}

int main(int argc, char *argv[])
{
    U32 test_mask             = TEST_API;
    MediaCase cases[MAX_MEDIA_CASES];
    int n_case                = 0;
    const char *inp           = NULL;
    U32 iw = 0, ih = 0;
    MppStreamCodecType icodec = MPP_STREAM_CODEC_UNKNOWN;
    U32 sc_w = 0, sc_h = 0;
    U32 rotate_deg            = 0;
    U32 max_frames            = 0;
    const char *nv12_path     = NULL;
    int opt;
    static struct option long_opts[] = {
        {"test", required_argument, NULL, 't'},
        {"input", required_argument, NULL, 'i'},
        {"width", required_argument, NULL, 'W'},
        {"height", required_argument, NULL, 'H'},
        {"codec", required_argument, NULL, 'c'},
        {"frames", required_argument, NULL, 'n'},
        {"cases", required_argument, NULL, 1000},
        {"scale-width", required_argument, NULL, 1001},
        {"scale-height", required_argument, NULL, 1002},
        {"rotate", required_argument, NULL, 1003},
        {"output", required_argument, NULL, 'o'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}};

    memset(cases, 0, sizeof(cases));

    while ((opt = getopt_long(argc, argv, "t:i:W:H:c:n:o:h", long_opts, NULL)) !=
           -1) {
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
        case 'i':
            inp = optarg;
            break;
        case 'W':
            iw = (U32)strtoul(optarg, NULL, 10);
            break;
        case 'H':
            ih = (U32)strtoul(optarg, NULL, 10);
            break;
        case 'c':
            icodec = parse_codec_name(optarg);
            break;
        case 'n':
            max_frames = (U32)strtoul(optarg, NULL, 10);
            break;
        case 'o':
            nv12_path = optarg;
            break;
        case 1000:
            if (parse_cases_arg(cases, &n_case, optarg) != 0)
                return 1;
            break;
        case 1001:
            sc_w = (U32)strtoul(optarg, NULL, 10);
            break;
        case 1002:
            sc_h = (U32)strtoul(optarg, NULL, 10);
            break;
        case 1003:
            rotate_deg = (U32)strtoul(optarg, NULL, 10);
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (inp && iw && ih && icodec != MPP_STREAM_CODEC_UNKNOWN) {
        if (n_case >= MAX_MEDIA_CASES) {
            fprintf(stderr, "too many cases\n");
            return 1;
        }
        memcpy(cases[n_case].path, inp, strlen(inp) + 1);
        cases[n_case].width  = iw;
        cases[n_case].height = ih;
        cases[n_case].codec  = icodec;
        n_case++;
    } else if (inp || iw || ih || icodec != MPP_STREAM_CODEC_UNKNOWN) {
        fprintf(stderr,
                "single-input mode needs --input, --width, --height, --codec\n");
        return 1;
    }

    if (test_mask & TEST_API) {
        if (run_api_suite() != 0)
            return 1;
    }
    if (test_mask & TEST_FMT) {
        if (run_format_suite(cases, n_case, max_frames, nv12_path) != 0)
            return 1;
    }
    if (test_mask & TEST_PARAM) {
        if (run_param_suite(cases, n_case, sc_w, sc_h, rotate_deg, max_frames,
                            nv12_path) != 0)
            return 1;
    }
    if (test_mask & TEST_MULTI) {
        if (run_multichn_suite(cases, n_case, max_frames, nv12_path) != 0)
            return 1;
    }

    return 0;
}
