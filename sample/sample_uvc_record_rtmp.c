/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    sample_uvc_record_rtmp.c
 * @Date      :    2026-06-11
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Real-world pipeline: UVC capture -> VDEC -> VENC -> MUX,
 *                 recording to segmented files AND pushing to RTMP at the
 *                 same time from a single encoded H.264 stream.
 *
 *                 UVC(MJPEG) --> VDEC(MJPEG->NV12) --> VENC(NV12->H.264)
 *                                                          |
 *                                          +---------------+---------------+
 *                                          v                               v
 *                                  MUX chn0 (FILE record)         MUX chn1 (RTMP push)
 *                                  fMP4/TS segmented              rtmp://server/app/key
 *
 * The encoder output is fanned out to two independent MUX channels, so the
 * same live stream is durably recorded to disk (power-loss safe fMP4/TS with
 * size/duration segmentation) while also being published to an RTMP server.
 * Either output can be disabled from the command line.
 *
 * Run:
 *   ./sample_uvc_record_rtmp                       # record only, default device
 *   ./sample_uvc_record_rtmp --dev /dev/video13
 *   ./sample_uvc_record_rtmp --rec "/mnt/sd/cam_%Y%m%d_%H%M%S_%d.mp4" --ts \
 *                            --max-sec 60
 *   ./sample_uvc_record_rtmp --rtmp rtmp://127.0.0.1/live/cam
 *   ./sample_uvc_record_rtmp --rec "/tmp/cam_%d.mp4" --max-sec 30 \
 *                            --rtmp rtmp://127.0.0.1/live/cam
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sys_api.h"
#include "uvc_api.h"
#include "vb_api.h"
#include "vdec/vdec_api.h"
#include "venc/venc_api.h"
#include "mux/mux_api.h"

/* ======================== Config ======================== */
#define SAMPLE_WIDTH 1280
#define SAMPLE_HEIGHT 720
#define SAMPLE_FPS 30
#define SAMPLE_BITRATE 2000000 /* 2 Mbps */
#define SAMPLE_GOP 30
#define SAMPLE_WARMUP_COUNT 2

#define REC_CHN 0
#define RTMP_CHN 1

static const char *g_pszDev = "/dev/video13";
static const char *g_pszRecPattern = "/tmp/uvc_rec_%d.mp4";
static const char *g_pszRtmpUrl = NULL; /* NULL = RTMP disabled */
static int g_bRecord = 1;               /* file recording on by default */
static int g_bTs = 0;                   /* 0 = fMP4, 1 = TS */
static int g_u32MaxSec = 30;            /* per-file duration limit */
static int g_u32MaxMb = 0;              /* per-file size limit (0 = unlimited) */

static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Forward the encoded H.264 access unit to one MUX channel. */
static S32 mux_send(S32 s32Chn, const StreamBufferInfo *pstStream) {
    MuxPacket stPkt;

    if (!pstStream->pu8Addr || pstStream->u32Size == 0) {
        return ERR_MUX_INVALID_ARG;
    }
    memset(&stPkt, 0, sizeof(stPkt));
    stPkt.pu8Data = pstStream->pu8Addr;
    stPkt.u32Size = pstStream->u32Size;
    stPkt.bKeyFrame = pstStream->bKeyFrame;
    stPkt.eCodecType = MUX_CODEC_H264;
    stPkt.u64PTS = pstStream->u64PTS;
    return MUX_SendPacket(s32Chn, &stPkt);
}

/* Open the file-recording MUX channel (segmented fMP4/TS). */
static S32 open_record_chn(void) {
    MuxChnAttr stAttr;
    S32 ret;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eOutputType = MUX_OUTPUT_FILE;
    stAttr.stStreamAttr.eCodecType = MUX_CODEC_H264;
    stAttr.stStreamAttr.u32Width = SAMPLE_WIDTH;
    stAttr.stStreamAttr.u32Height = SAMPLE_HEIGHT;
    stAttr.stStreamAttr.u32Fps = SAMPLE_FPS;
    stAttr.stStreamAttr.u32BitrateKbps = SAMPLE_BITRATE / 1000;
    stAttr.stSegment.eFileFormat = g_bTs ? MUX_FILE_TS : MUX_FILE_FMP4;
    stAttr.stSegment.u32MaxDurationMs = (U32)g_u32MaxSec * 1000U;
    stAttr.stSegment.u32MaxSizeBytes = (U32)g_u32MaxMb * 1024U * 1024U;
    snprintf(stAttr.stSegment.szPattern, sizeof(stAttr.stSegment.szPattern), "%s", g_pszRecPattern);

    ret = MUX_CreateChn(REC_CHN, &stAttr);
    if (ret != ERR_MUX_OK) {
        printf("  [ERROR] MUX_CreateChn(record) failed (ret=%d)\n", ret);
        return ret;
    }
    ret = MUX_StartChn(REC_CHN);
    if (ret != ERR_MUX_OK) {
        printf("  [ERROR] MUX_StartChn(record) failed (ret=%d)\n", ret);
        MUX_DestroyChn(REC_CHN);
        return ret;
    }
    printf("  [INFO] recording -> '%s' (%s, max %ds/%dMiB seg)\n", g_pszRecPattern, g_bTs ? "TS" : "fMP4",
        g_u32MaxSec, g_u32MaxMb);
    return ERR_MUX_OK;
}

/* Open the RTMP push MUX channel. */
static S32 open_rtmp_chn(void) {
    MuxChnAttr stAttr;
    S32 ret;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eOutputType = MUX_OUTPUT_RTMP;
    stAttr.stStreamAttr.eCodecType = MUX_CODEC_H264;
    stAttr.stStreamAttr.u32Width = SAMPLE_WIDTH;
    stAttr.stStreamAttr.u32Height = SAMPLE_HEIGHT;
    stAttr.stStreamAttr.u32Fps = SAMPLE_FPS;
    stAttr.stStreamAttr.u32BitrateKbps = SAMPLE_BITRATE / 1000;
    snprintf(stAttr.szUrl, sizeof(stAttr.szUrl), "%s", g_pszRtmpUrl);

    ret = MUX_CreateChn(RTMP_CHN, &stAttr);
    if (ret != ERR_MUX_OK) {
        printf("  [ERROR] MUX_CreateChn(rtmp) failed (ret=%d)\n", ret);
        return ret;
    }
    ret = MUX_StartChn(RTMP_CHN);
    if (ret != ERR_MUX_OK) {
        printf("  [ERROR] MUX_StartChn(rtmp) failed (ret=%d)\n", ret);
        MUX_DestroyChn(RTMP_CHN);
        return ret;
    }
    printf("  [INFO] RTMP push -> '%s'\n", g_pszRtmpUrl);
    return ERR_MUX_OK;
}

/* Set up UVC -> VDEC -> VENC and fan the H.264 output to record + RTMP. */
static S32 run_pipeline(void) {
    UVC_DEV uvcDev = 0;
    UVC_CHN uvcChn = 0;
    S32 vdecChn = 0;
    S32 vencChn = 0;
    UvcDevAttr stDevAttr;
    UvcChnAttr stUvcChn;
    VdecChnAttr stVdec;
    VencChnAttr stVenc;
    VideoFrameInfo stUvcFrame;
    U32 u32Pkts = 0;
    U32 i;
    S32 ret;

    if (access(g_pszDev, F_OK) != 0) {
        printf("  [SKIP] UVC device %s not found\n", g_pszDev);
        return -1;
    }

    ret = UVC_Init();
    if (ret != 0) {
        return ret;
    }
    VDEC_Init();
    VENC_Init();
    MUX_Init();

    /* --- UVC device + channel (MJPEG) --- */
    memset(&stDevAttr, 0, sizeof(stDevAttr));
    strncpy(stDevAttr.acDevNode, g_pszDev, sizeof(stDevAttr.acDevNode) - 1);
    ret = UVC_CreateDev(uvcDev, &stDevAttr);
    if (ret != 0) {
        printf("  [ERROR] UVC_CreateDev failed (ret=%d)\n", ret);
        goto cleanup_modules;
    }
    if (UVC_EnableDev(uvcDev) != 0) {
        printf("  [ERROR] UVC_EnableDev failed\n");
        goto cleanup_dev;
    }

    memset(&stUvcChn, 0, sizeof(stUvcChn));
    stUvcChn.u32Width = SAMPLE_WIDTH;
    stUvcChn.u32Height = SAMPLE_HEIGHT;
    stUvcChn.ePixelFormat = MPP_PIXEL_FORMAT_MJPEG;
    stUvcChn.u32Fps = SAMPLE_FPS;
    stUvcChn.u32Depth = 1;
    UVC_SetChnAttr(uvcDev, uvcChn, &stUvcChn);
    if (UVC_EnableChn(uvcDev, uvcChn) != 0) {
        printf("  [ERROR] UVC_EnableChn failed\n");
        goto cleanup_dev;
    }

    /* --- VDEC: MJPEG -> NV12 --- */
    memset(&stVdec, 0, sizeof(stVdec));
    stVdec.eCodecType = MPP_STREAM_CODEC_MJPEG;
    stVdec.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    stVdec.u32Width = SAMPLE_WIDTH;
    stVdec.u32Height = SAMPLE_HEIGHT;
    VDEC_CreateChn(vdecChn, &stVdec);
    if (VDEC_EnableChn(vdecChn) != 0) {
        printf("  [ERROR] VDEC_EnableChn failed\n");
        VDEC_DestroyChn(vdecChn);
        goto cleanup_uvc_chn;
    }

    /* --- VENC: NV12 -> H.264 --- */
    memset(&stVenc, 0, sizeof(stVenc));
    stVenc.eCodecType = MPP_STREAM_CODEC_H264;
    stVenc.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    stVenc.u32Width = SAMPLE_WIDTH;
    stVenc.u32Height = SAMPLE_HEIGHT;
    stVenc.u32Bitrate = SAMPLE_BITRATE;
    stVenc.u32FrameRate = SAMPLE_FPS;
    stVenc.u32Gop = SAMPLE_GOP;
    stVenc.eRcMode = VENC_RC_MODE_CBR;
    VENC_CreateChn(vencChn, &stVenc);
    if (VENC_EnableChn(vencChn) != 0) {
        printf("  [ERROR] VENC_EnableChn failed\n");
        VENC_DestroyChn(vencChn);
        goto cleanup_vdec;
    }

    /* --- MUX outputs: record and/or RTMP --- */
    if (g_bRecord && open_record_chn() != ERR_MUX_OK) {
        goto cleanup_venc;
    }
    if (g_pszRtmpUrl && open_rtmp_chn() != ERR_MUX_OK) {
        if (g_bRecord) {
            MUX_StopChn(REC_CHN);
            MUX_DestroyChn(REC_CHN);
        }
        goto cleanup_venc;
    }

    printf("  [INFO] pipeline running, Ctrl+C to stop\n");

    /* Drop a couple of warm-up frames. */
    for (i = 0; i < SAMPLE_WARMUP_COUNT; i++) {
        memset(&stUvcFrame, 0, sizeof(stUvcFrame));
        if (UVC_GetFrame(uvcDev, uvcChn, &stUvcFrame, 3000) == 0) {
            UVC_ReleaseFrame(uvcDev, uvcChn, &stUvcFrame);
        }
    }

    while (g_running) {
        StreamBufferInfo stStream;
        VideoFrameInfo stDec;
        StreamBufferInfo stEnc;

        /* 1. UVC capture (MJPEG) */
        memset(&stUvcFrame, 0, sizeof(stUvcFrame));
        ret = UVC_GetFrame(uvcDev, uvcChn, &stUvcFrame, 3000);
        if (ret != 0) {
            continue;
        }
        if (stUvcFrame.stVFrame.ulPlaneVirAddr[0] == 0 || stUvcFrame.stVFrame.u32PlaneSizeValid[0] == 0) {
            UVC_ReleaseFrame(uvcDev, uvcChn, &stUvcFrame);
            continue;
        }

        /* 2. decode MJPEG -> NV12 */
        memset(&stStream, 0, sizeof(stStream));
        stStream.pu8Addr = (const U8 *)stUvcFrame.stVFrame.ulPlaneVirAddr[0];
        stStream.u32Size = stUvcFrame.stVFrame.u32PlaneSizeValid[0];
        stStream.eCodecType = MPP_STREAM_CODEC_MJPEG;
        stStream.bKeyFrame = MPP_TRUE;
        stStream.u64PTS = stUvcFrame.stVFrame.u64PTS;
        ret = VDEC_SendStream(vdecChn, &stStream, 0);
        UVC_ReleaseFrame(uvcDev, uvcChn, &stUvcFrame);
        if (ret != 0 && ret != ERR_VDEC_EOS) {
            continue;
        }

        memset(&stDec, 0, sizeof(stDec));
        ret = VDEC_GetFrame(vdecChn, &stDec, 1000);
        if (ret != ERR_VDEC_OK) {
            continue;
        }

        /* 3. encode NV12 -> H.264 */
        stDec.eFrameType = FRAME_TYPE_VENC;
        ret = VENC_SendFrame(vencChn, &stDec, 0);
        VDEC_ReleaseFrame(vdecChn, stDec.ulBufferId);
        if (ret != 0) {
            continue;
        }

        memset(&stEnc, 0, sizeof(stEnc));
        ret = VENC_GetStream(vencChn, &stEnc, 1000);
        if (ret != ERR_VENC_OK) {
            continue;
        }

        /* 4. fan out to record + RTMP */
        if (g_bRecord) {
            mux_send(REC_CHN, &stEnc);
        }
        if (g_pszRtmpUrl) {
            mux_send(RTMP_CHN, &stEnc);
        }
        VENC_ReleaseStream(vencChn, &stEnc);

        u32Pkts++;
        if (u32Pkts % 100 == 0) {
            MuxChnStat stStat;
            if (g_bRecord && MUX_GetChnStat(REC_CHN, &stStat) == 0) {
                printf("  [INFO] pkts=%u  file#=%u  cur='%s' (%" PRIu64 " B)\n", u32Pkts, stStat.u32FileCount,
                    stStat.szCurFile, (uint64_t)stStat.u64CurFileBytes);
            } else {
                printf("  [INFO] pkts=%u\n", u32Pkts);
            }
        }
    }

    printf("  [INFO] stopping after %u packets\n", u32Pkts);

    if (g_pszRtmpUrl) {
        MUX_StopChn(RTMP_CHN);
        MUX_DestroyChn(RTMP_CHN);
    }
    if (g_bRecord) {
        MUX_StopChn(REC_CHN);
        MUX_DestroyChn(REC_CHN);
    }

cleanup_venc:
    VENC_DisableChn(vencChn);
    VENC_DestroyChn(vencChn);
cleanup_vdec:
    VDEC_DisableChn(vdecChn);
    VDEC_DestroyChn(vdecChn);
cleanup_uvc_chn:
    UVC_DisableChn(uvcDev, uvcChn);
cleanup_dev:
    UVC_DisableDev(uvcDev);
    UVC_DestroyDev(uvcDev);
cleanup_modules:
    MUX_Exit();
    VENC_Exit();
    VDEC_Exit();
    UVC_Exit();
    return (S32)u32Pkts;
}

static void usage(const char *pszProg) {
    printf("Usage: %s [OPTIONS]\n\n", pszProg);
    printf("  UVC capture -> VDEC -> VENC(H.264) -> file recording + RTMP push.\n\n");
    printf("Options:\n");
    printf("  --dev <node>       UVC device node (default: %s)\n", g_pszDev);
    printf("  --rec <pattern>    record file pattern (default: %s)\n", g_pszRecPattern);
    printf("                     supports strftime (%%Y%%m%%d...) and %%d seq number\n");
    printf("  --ts               record as MPEG-TS instead of fMP4\n");
    printf("  --max-sec <n>      per-file duration limit in seconds (default 30)\n");
    printf("  --max-mb <n>       per-file size limit in MiB (0 = unlimited)\n");
    printf("  --no-record        disable file recording\n");
    printf("  --rtmp <url>       also push to RTMP server (e.g. rtmp://ip/live/cam)\n");
    printf("  -h, --help         this help\n\n");
    printf("Playback: ffplay <file> | ffplay rtmp://ip/live/cam\n");
}

int main(int argc, char *argv[]) {
    S32 ret;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dev") == 0 && i + 1 < argc) {
            g_pszDev = argv[++i];
        } else if (strcmp(argv[i], "--rec") == 0 && i + 1 < argc) {
            g_pszRecPattern = argv[++i];
            g_bRecord = 1;
        } else if (strcmp(argv[i], "--ts") == 0) {
            g_bTs = 1;
        } else if (strcmp(argv[i], "--max-sec") == 0 && i + 1 < argc) {
            g_u32MaxSec = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-mb") == 0 && i + 1 < argc) {
            g_u32MaxMb = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--no-record") == 0) {
            g_bRecord = 0;
        } else if (strcmp(argv[i], "--rtmp") == 0 && i + 1 < argc) {
            g_pszRtmpUrl = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            printf("Unknown arg: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!g_bRecord && !g_pszRtmpUrl) {
        printf("[ERROR] nothing to do: enable recording or --rtmp\n");
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Sample: UVC -> VDEC -> VENC -> record + RTMP ===\n");
    printf("  Device : %s\n", g_pszDev);
    printf("  Video  : %dx%d @ %d fps, %d bps H.264\n", SAMPLE_WIDTH, SAMPLE_HEIGHT, SAMPLE_FPS, SAMPLE_BITRATE);
    printf("  Record : %s\n", g_bRecord ? g_pszRecPattern : "(disabled)");
    printf("  RTMP   : %s\n\n", g_pszRtmpUrl ? g_pszRtmpUrl : "(disabled)");

    ret = SYS_Init();
    if (ret != 0) {
        printf("[ERROR] SYS_Init failed (ret=%d)\n", ret);
        return 1;
    }
    ret = VB_Init();
    if (ret != 0) {
        printf("[ERROR] VB_Init failed (ret=%d)\n", ret);
        SYS_Exit();
        return 1;
    }

    run_pipeline();

    VB_Exit();
    SYS_Exit();
    printf("\n=== Sample finished ===\n");
    return 0;
}



