/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    sample_file_transcode.c
 * @Date      :    2026-05-20
 * @Brief     :    Sample: container/RTSP transcode pipeline.
 *
 *                 Reads a container file (mp4/ts/flv) or an RTSP stream,
 *                 decodes it, then re-encodes to the requested codec/bitrate.
 *                 The compressed input side always uses compressed-domain bind
 *                 because the DEMUX module is bind-first (once started it pushes
 *                 packets straight to the bound VDEC):
 *
 *                     DEMUX --SYS_Bind--> VDEC --SYS_Bind--> VENC
 *
 *                 The output side depends on the destination:
 *                   - file (default) : pull encoded NAL units with
 *                                      VENC_GetStream and fwrite them to disk
 *                                      (the MUX module only does RTSP, not file)
 *                   - rtsp://...      : SYS_Bind(VENC -> MUX) and let the RTSP
 *                                      server re-publish the encoded stream
 *
 * Run:
 *   ./sample_file_transcode in.mp4 out.h264
 *   ./sample_file_transcode in.ts out.h265 --codec h265 --bitrate 3000
 *   ./sample_file_transcode in.mp4 rtsp://0.0.0.0:8554/live --codec h264
 *
 * Note: input must be a container (MP4/TS/FLV) or an rtsp:// url; the DEMUX
 *       module does not parse raw elementary .264/.265 annex-b files.
 * Play (RTSP out): ffplay rtsp://127.0.0.1:8554/live
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
#include <time.h>
#include <unistd.h>

#include "demux/demux_api.h"
#include "mux/mux_api.h"
#include "sys/sys_api.h"
#include "sys/vb_api.h"
#include "vdec/vdec_api.h"
#include "venc/venc_api.h"

/* ======================== Config ======================== */

#define TRANSCODE_DEMUX_CHN 0
#define TRANSCODE_VDEC_CHN 0
#define TRANSCODE_VENC_CHN 0
#define TRANSCODE_MUX_CHN 0
#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
#define DEFAULT_FPS 30
#define DEFAULT_BITRATE_KBPS 4000
#define DEFAULT_GOP 30

typedef struct TranscodeCfg {
    const char *pszInput;  /* input container file or rtsp url */
    const char *pszOutput; /* output file path or rtsp url */
    MppStreamCodecType eOutCodec;
    U32 u32BitrateKbps;
    U32 u32MaxFrames; /* 0 = unlimited */
    BOOL bRtspOut;
} TranscodeCfg;

static volatile S32 g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* ---------- small mapping helpers ---------- */

static MppStreamCodecType demux_to_stream_codec(DemuxCodecType eType) {
    switch (eType) {
        case DEMUX_CODEC_H264:
            return MPP_STREAM_CODEC_H264;
        case DEMUX_CODEC_H265:
            return MPP_STREAM_CODEC_H265;
        case DEMUX_CODEC_MJPEG:
            return MPP_STREAM_CODEC_MJPEG;
        default:
            return MPP_STREAM_CODEC_UNKNOWN;
    }
}

static MuxCodecType stream_to_mux_codec(MppStreamCodecType eType) {
    switch (eType) {
        case MPP_STREAM_CODEC_H264:
            return MUX_CODEC_H264;
        case MPP_STREAM_CODEC_H265:
            return MUX_CODEC_H265;
        case MPP_STREAM_CODEC_MJPEG:
            return MUX_CODEC_MJPEG;
        default:
            return MUX_CODEC_UNKNOWN;
    }
}

static BOOL is_rtsp_url(const char *pszStr) {
    return (pszStr != NULL && strncmp(pszStr, "rtsp://", 7) == 0) ? MPP_TRUE : MPP_FALSE;
}

static const char *stream_codec_name(MppStreamCodecType eType) {
    switch (eType) {
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

/* ======================== builders ======================== */

static S32 build_demux(const TranscodeCfg *pstCfg) {
    DemuxChnAttr stAttr;
    S32 ret;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eInputType = is_rtsp_url(pstCfg->pszInput) ? DEMUX_INPUT_RTSP : DEMUX_INPUT_FILE;
    stAttr.bPreferTcp = MPP_TRUE;
    stAttr.bLowLatency = MPP_TRUE;
    stAttr.u32OpenTimeoutMs = 5000;
    stAttr.u32RwTimeoutMs = 5000;
    stAttr.u32ReconnectMs = 2000;
    stAttr.u32AnalyzeDurationMs = 1000;
    stAttr.u32ProbeSizeBytes = 512 * 1024;
    stAttr.bInjectPS = MPP_TRUE;
    snprintf(stAttr.szUrl, sizeof(stAttr.szUrl), "%s", pstCfg->pszInput);

    ret = DEMUX_CreateChn(TRANSCODE_DEMUX_CHN, &stAttr);
    if (ret != 0) {
        fprintf(stderr, "DEMUX_CreateChn failed: %d\n", ret);
    }
    return ret;
}

static S32 build_vdec(MppStreamCodecType eInCodec, U32 u32Width, U32 u32Height) {
    VdecChnAttr stAttr;
    S32 ret;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eCodecType = eInCodec;
    stAttr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    stAttr.u32Align = 16;
    stAttr.u32Width = (u32Width > 0) ? u32Width : DEFAULT_WIDTH;
    stAttr.u32Height = (u32Height > 0) ? u32Height : DEFAULT_HEIGHT;

    ret = VDEC_CreateChn(TRANSCODE_VDEC_CHN, &stAttr);
    if (ret != 0) {
        fprintf(stderr, "VDEC_CreateChn failed: %d\n", ret);
        return ret;
    }
    ret = VDEC_EnableChn(TRANSCODE_VDEC_CHN);
    if (ret != 0) {
        fprintf(stderr, "VDEC_EnableChn failed: %d\n", ret);
        VDEC_DestroyChn(TRANSCODE_VDEC_CHN);
    }
    return ret;
}

static S32 build_venc(const TranscodeCfg *pstCfg, U32 u32Width, U32 u32Height) {
    VencChnAttr stAttr;
    S32 ret;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eCodecType = pstCfg->eOutCodec;
    stAttr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    stAttr.u32Width = (u32Width > 0) ? u32Width : DEFAULT_WIDTH;
    stAttr.u32Height = (u32Height > 0) ? u32Height : DEFAULT_HEIGHT;
    stAttr.u32Bitrate = pstCfg->u32BitrateKbps * 1000;
    stAttr.u32FrameRate = DEFAULT_FPS;
    stAttr.u32Gop = DEFAULT_GOP;
    stAttr.eRcMode = VENC_RC_MODE_CBR;

    ret = VENC_CreateChn(TRANSCODE_VENC_CHN, &stAttr);
    if (ret != 0) {
        fprintf(stderr, "VENC_CreateChn failed: %d\n", ret);
        return ret;
    }
    ret = VENC_EnableChn(TRANSCODE_VENC_CHN);
    if (ret != 0) {
        fprintf(stderr, "VENC_EnableChn failed: %d\n", ret);
        VENC_DestroyChn(TRANSCODE_VENC_CHN);
    }
    return ret;
}

static S32 build_mux(const TranscodeCfg *pstCfg, U32 u32Width, U32 u32Height) {
    MuxChnAttr stAttr;
    S32 ret;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eOutputType = MUX_OUTPUT_RTSP;
    stAttr.stStreamAttr.eCodecType = stream_to_mux_codec(pstCfg->eOutCodec);
    stAttr.stStreamAttr.u32Width = u32Width;
    stAttr.stStreamAttr.u32Height = u32Height;
    stAttr.stStreamAttr.u32Fps = DEFAULT_FPS;
    stAttr.stStreamAttr.u32BitrateKbps = pstCfg->u32BitrateKbps;
    stAttr.bPreferTcp = MPP_TRUE;
    snprintf(stAttr.szUrl, sizeof(stAttr.szUrl), "%s", pstCfg->pszOutput);

    ret = MUX_CreateChn(TRANSCODE_MUX_CHN, &stAttr);
    if (ret != 0) {
        fprintf(stderr, "MUX_CreateChn failed: %d\n", ret);
        return ret;
    }
    ret = MUX_StartChn(TRANSCODE_MUX_CHN);
    if (ret != 0) {
        fprintf(stderr, "MUX_StartChn failed: %d\n", ret);
        MUX_DestroyChn(TRANSCODE_MUX_CHN);
    }
    return ret;
}

/* ======================== transcode ======================== */

static S32 run_transcode(const TranscodeCfg *pstCfg) {
    DemuxStreamInfo stInfo;
    MppStreamCodecType eInCodec = MPP_STREAM_CODEC_H264;
    U32 u32Width = DEFAULT_WIDTH;
    U32 u32Height = DEFAULT_HEIGHT;
    MppNode stDemuxNode;
    MppNode stVdecNode;
    MppNode stVencNode;
    MppNode stMuxNode;
    S32 s32BoundDv = 0;
    S32 s32BoundVv = 0;
    S32 s32BoundVm = 0;
    S32 s32MuxReady = 0;
    FILE *fpOut = NULL;
    U32 u32Frames = 0;
    S32 ret;

    ret = DEMUX_Init();
    if (ret != 0) {
        fprintf(stderr, "DEMUX_Init failed: %d\n", ret);
        return -1;
    }
    ret = VDEC_Init();
    if (ret != 0) {
        fprintf(stderr, "VDEC_Init failed: %d\n", ret);
        goto cleanup_demux_init;
    }
    ret = VENC_Init();
    if (ret != 0) {
        fprintf(stderr, "VENC_Init failed: %d\n", ret);
        goto cleanup_vdec_init;
    }
    if (pstCfg->bRtspOut) {
        ret = MUX_Init();
        if (ret != 0) {
            fprintf(stderr, "MUX_Init failed: %d\n", ret);
            goto cleanup_venc_init;
        }
    }

    if (build_demux(pstCfg) != 0) {
        goto cleanup_init;
    }

    /* Resolve real codec/resolution from the container before building VDEC. */
    memset(&stInfo, 0, sizeof(stInfo));
    if (DEMUX_GetStreamInfo(TRANSCODE_DEMUX_CHN, &stInfo) == 0) {
        if (stInfo.eCodecType != DEMUX_CODEC_UNKNOWN) {
            eInCodec = demux_to_stream_codec(stInfo.eCodecType);
        }
        if (stInfo.u32Width > 0 && stInfo.u32Height > 0) {
            u32Width = stInfo.u32Width;
            u32Height = stInfo.u32Height;
        }
    }
    printf("  [INFO] input %s : %ux%u codec=%s\n", pstCfg->pszInput, u32Width, u32Height, stream_codec_name(eInCodec));

    if (build_vdec(eInCodec, u32Width, u32Height) != 0) {
        goto cleanup_demux;
    }
    if (build_venc(pstCfg, u32Width, u32Height) != 0) {
        goto cleanup_vdec;
    }

    /* Connect DEMUX -> VDEC -> VENC (compressed input is always bind). */
    memset(&stDemuxNode, 0, sizeof(stDemuxNode));
    if (DEMUX_GetSrcNode(TRANSCODE_DEMUX_CHN, &stDemuxNode) != 0) {
        fprintf(stderr, "DEMUX_GetSrcNode failed\n");
        goto cleanup_venc;
    }
    stVdecNode = (MppNode){MPP_ID_VDEC, 0, TRANSCODE_VDEC_CHN};
    stVencNode = (MppNode){MPP_ID_VENC, 0, TRANSCODE_VENC_CHN};

    ret = SYS_Bind(&stDemuxNode, &stVdecNode);
    if (ret != 0) {
        fprintf(stderr, "SYS_Bind DEMUX->VDEC failed: %d\n", ret);
        goto cleanup_venc;
    }
    s32BoundDv = 1;
    ret = SYS_Bind(&stVdecNode, &stVencNode);
    if (ret != 0) {
        fprintf(stderr, "SYS_Bind VDEC->VENC failed: %d\n", ret);
        goto cleanup_bind;
    }
    s32BoundVv = 1;

    /* Output side: RTSP -> bind VENC->MUX; file -> pull + fwrite. */
    if (pstCfg->bRtspOut) {
        if (build_mux(pstCfg, u32Width, u32Height) != 0) {
            goto cleanup_bind;
        }
        s32MuxReady = 1;
        memset(&stMuxNode, 0, sizeof(stMuxNode));
        if (MUX_GetSinkNode(TRANSCODE_MUX_CHN, &stMuxNode) != 0) {
            fprintf(stderr, "MUX_GetSinkNode failed\n");
            goto cleanup_mux;
        }
        ret = SYS_Bind(&stVencNode, &stMuxNode);
        if (ret != 0) {
            fprintf(stderr, "SYS_Bind VENC->MUX failed: %d\n", ret);
            goto cleanup_mux;
        }
        s32BoundVm = 1;
    } else {
        fpOut = fopen(pstCfg->pszOutput, "wb");
        if (!fpOut) {
            fprintf(stderr, "fopen(%s) failed\n", pstCfg->pszOutput);
            goto cleanup_bind;
        }
    }

    ret = DEMUX_StartChn(TRANSCODE_DEMUX_CHN);
    if (ret != 0) {
        fprintf(stderr, "DEMUX_StartChn failed: %d\n", ret);
        goto cleanup_out;
    }

    printf("  [INFO] transcoding %s -> %s (%s @ %u kbps)\n", pstCfg->pszInput, pstCfg->pszOutput,
        stream_codec_name(pstCfg->eOutCodec), pstCfg->u32BitrateKbps);

    if (pstCfg->bRtspOut) {
        /* MUX pulls from VENC automatically; just report stats. */
        U32 u32Loops = 0;
        while (g_running) {
            MuxChnStat stStat;
            sleep(1);
            u32Loops++;
            if (MUX_GetChnStat(TRANSCODE_MUX_CHN, &stStat) == 0) {
                printf("  [INFO] t=%us pkts=%" PRIu64 " bytes=%" PRIu64 " clients=%u\n", u32Loops,
                    (uint64_t)stStat.u64TotalPkts, (uint64_t)stStat.u64TotalBytes, stStat.u32ActiveClients);
            }
            if (pstCfg->u32MaxFrames > 0 && u32Loops >= pstCfg->u32MaxFrames) {
                break;
            }
        }
    } else {
        U32 u32Idle = 0;
        struct timespec tFirst, tLast;
        int bHaveFirst = 0;
        while (g_running) {
            StreamBufferInfo stStream;
            memset(&stStream, 0, sizeof(stStream));
            ret = VENC_GetStream(TRANSCODE_VENC_CHN, &stStream, 500);
            if (ret == ERR_VENC_OK) {
                u32Idle = 0;
                if (stStream.pu8Addr && stStream.u32Size > 0) {
                    if (!bHaveFirst) {
                        clock_gettime(CLOCK_MONOTONIC, &tFirst);
                        bHaveFirst = 1;
                    }
                    clock_gettime(CLOCK_MONOTONIC, &tLast);
                    fwrite(stStream.pu8Addr, 1, stStream.u32Size, fpOut);
                    u32Frames++;
                    if (u32Frames % 30 == 0) {
                        printf("  [INFO] transcoded %u frames\n", u32Frames);
                    }
                }
                VENC_ReleaseStream(TRANSCODE_VENC_CHN, &stStream);
                if (pstCfg->u32MaxFrames > 0 && u32Frames >= pstCfg->u32MaxFrames) {
                    break;
                }
            } else if (ret == ERR_VENC_EOS) {
                break;
            } else {
                /* file input drains after EOF: bail out once idle. */
                if (++u32Idle > 30 && u32Frames > 0) {
                    break;
                }
            }
        }
        printf("  [DONE] transcoded %u frames\n", u32Frames);
        /* Steady-state pipeline throughput: from first to last output frame,
         * excluding init / v4l2 probe / teardown / trailing idle drain. */
        if (bHaveFirst && u32Frames > 1) {
            double dWindowUs = (double)(tLast.tv_sec - tFirst.tv_sec) * 1000000.0 +
                (double)(tLast.tv_nsec - tFirst.tv_nsec) / 1000.0;
            if (dWindowUs > 0.0) {
                printf("[MPP_PERF] metric=frames value=%u unit=frames\n", u32Frames);
                printf("[MPP_PERF] metric=fps value=%.3f unit=fps\n",
                    (double)(u32Frames - 1) * 1000000.0 / dWindowUs);
            }
        }
    }

    DEMUX_StopChn(TRANSCODE_DEMUX_CHN);

cleanup_out:
    if (fpOut) {
        fclose(fpOut);
        fpOut = NULL;
    }
cleanup_mux:
    if (s32BoundVm) {
        SYS_UnBind(&stVencNode, &stMuxNode);
    }
    if (s32MuxReady) {
        MUX_StopChn(TRANSCODE_MUX_CHN);
        MUX_DestroyChn(TRANSCODE_MUX_CHN);
    }
cleanup_bind:
    if (s32BoundVv) {
        SYS_UnBind(&stVdecNode, &stVencNode);
    }
    if (s32BoundDv) {
        SYS_UnBind(&stDemuxNode, &stVdecNode);
    }
cleanup_venc:
    VENC_DisableChn(TRANSCODE_VENC_CHN);
    VENC_DestroyChn(TRANSCODE_VENC_CHN);
cleanup_vdec:
    VDEC_DisableChn(TRANSCODE_VDEC_CHN);
    VDEC_DestroyChn(TRANSCODE_VDEC_CHN);
cleanup_demux:
    DEMUX_DestroyChn(TRANSCODE_DEMUX_CHN);
cleanup_init:
    if (pstCfg->bRtspOut) {
        MUX_Exit();
    }
cleanup_venc_init:
    VENC_Exit();
cleanup_vdec_init:
    VDEC_Exit();
cleanup_demux_init:
    DEMUX_Exit();
    return (u32Frames > 0 || pstCfg->bRtspOut) ? 0 : -1;
}

/* ======================== CLI ======================== */

static void usage(const char *prog) {
    printf(
        "Usage: %s <input> <output> [OPTIONS]\n\n"
        "  Transcode a container/RTSP stream: DEMUX -> VDEC -> VENC -> file/RTSP.\n\n"
        "Positional:\n"
        "  input       input container file (mp4/ts/flv) or rtsp:// url\n"
        "  output      output file or rtsp:// url\n\n"
        "Options:\n"
        "  --codec <h264|h265>   output codec (default h264)\n"
        "  --bitrate <kbps>      output bitrate in kbps (default %d)\n"
        "  --frames <n>          stop after n frames (file) / seconds (rtsp), 0 = run forever\n"
        "  -h, --help            show this help\n\n"
        "Examples:\n"
        "  %s in.mp4 out.h265 --codec h265 --bitrate 3000\n"
        "  %s in.ts rtsp://0.0.0.0:8554/live --codec h264\n",
        prog,
        DEFAULT_BITRATE_KBPS,
        prog,
        prog);
}

int main(int argc, char *argv[]) {
    TranscodeCfg stCfg;
    S32 i;
    S32 ret;

    memset(&stCfg, 0, sizeof(stCfg));
    stCfg.eOutCodec = MPP_STREAM_CODEC_H264;
    stCfg.u32BitrateKbps = DEFAULT_BITRATE_KBPS;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--codec") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "h265") == 0 || strcmp(argv[i], "hevc") == 0) {
                stCfg.eOutCodec = MPP_STREAM_CODEC_H265;
            } else {
                stCfg.eOutCodec = MPP_STREAM_CODEC_H264;
            }
        } else if (strcmp(argv[i], "--bitrate") == 0 && i + 1 < argc) {
            stCfg.u32BitrateKbps = (U32)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            stCfg.u32MaxFrames = (U32)atoi(argv[++i]);
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        } else if (!stCfg.pszInput) {
            stCfg.pszInput = argv[i];
        } else if (!stCfg.pszOutput) {
            stCfg.pszOutput = argv[i];
        }
    }

    if (!stCfg.pszInput || !stCfg.pszOutput) {
        usage(argv[0]);
        return 1;
    }
    stCfg.bRtspOut = is_rtsp_url(stCfg.pszOutput);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("=== Sample: Container/RTSP Transcode ===\n");
    printf("  Input  : %s\n", stCfg.pszInput);
    printf("  Output : %s (%s)\n", stCfg.pszOutput, stCfg.bRtspOut ? "RTSP" : "FILE");
    printf("  Codec  : %s @ %u kbps\n\n", stream_codec_name(stCfg.eOutCodec), stCfg.u32BitrateKbps);

    ret = SYS_Init();
    if (ret != 0) {
        fprintf(stderr, "SYS_Init failed: %d\n", ret);
        return 1;
    }
    ret = VB_Init();
    if (ret != 0) {
        fprintf(stderr, "VB_Init failed: %d\n", ret);
        SYS_Exit();
        return 1;
    }

    ret = run_transcode(&stCfg);

    VB_Exit();
    SYS_Exit();

    printf("\n=== Sample finished (ret=%d) ===\n", ret);
    return (ret == 0) ? 0 : 1;
}
