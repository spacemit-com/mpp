/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    sample_mux_record.c
 * @Date      :    2026-06-11
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    MUX recording / RTMP push demo.
 *
 * Reads a raw Annex-B H.264/H.265 elementary stream from a file, splits it
 * into access units, and feeds them into a MUX channel configured for either
 * file recording (fragmented MP4 / TS with size or duration segmentation) or
 * RTMP push streaming.
 *
 * Usage:
 *   sample_mux_record <input.h264|.h265> file <out_pattern> [mp4|ts] [opts]
 *   sample_mux_record <input.h264|.h265> rtmp <rtmp://host/app/key> [opts]
 *
 *   opts (file mode):
 *     --h265                 input is H.265 (default H.264)
 *     --fps <n>              frame rate, default 30
 *     --max-sec <n>          per-file max duration in seconds (0 = unlimited)
 *     --max-mb <n>           per-file max size in MiB (0 = unlimited)
 *     --frag-ms <n>          fMP4 fragment duration in ms (0 = per GOP)
 *
 * Examples:
 *   # Record to 10-second fragmented-MP4 segments (power-loss safe):
 *   sample_mux_record in.h264 file "/mnt/sd/rec_%Y%m%d_%H%M%S.mp4" mp4 --max-sec 10
 *   # Record to 50 MiB TS segments:
 *   sample_mux_record in.h264 file "/mnt/sd/rec_%d.ts" ts --max-mb 50
 *   # Push to RTMP:
 *   sample_mux_record in.h264 rtmp rtmp://127.0.0.1/live/stream
 *------------------------------------------------------------------------------
 */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "mux/mux_api.h"
#include "sys/sys_api.h"

static volatile int g_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    g_running = 0;
}

/* Find the next Annex-B start code (00 00 01 / 00 00 00 01) at or after off.
 * Returns the offset of the start code, or -1 if none. */
static int64_t find_start_code(const unsigned char *p, int64_t len, int64_t off, int *pPrefix) {
    int64_t i;
    for (i = off; i + 3 < len; ++i) {
        if (p[i] == 0x00 && p[i + 1] == 0x00) {
            if (p[i + 2] == 0x01) {
                *pPrefix = 3;
                return i;
            }
            if (p[i + 2] == 0x00 && p[i + 3] == 0x01) {
                *pPrefix = 4;
                return i;
            }
        }
    }
    return -1;
}

/* NAL type extraction for key-frame detection. */
static int is_key_frame(const unsigned char *pNal, int64_t nalLen, int bH265) {
    if (nalLen < 1) {
        return 0;
    }
    if (bH265) {
        int type = (pNal[0] >> 1) & 0x3F;
        /* BLA/IDR/CRA range 16..23 are IRAP (key) frames. */
        return (type >= 16 && type <= 23) ? 1 : 0;
    } else {
        int type = pNal[0] & 0x1F;
        return (type == 5) ? 1 : 0; /* IDR */
    }
}

/* Returns 1 if the NAL is a VCL (coded slice) unit, which marks picture data. */
static int is_vcl(const unsigned char *pNal, int64_t nalLen, int bH265) {
    if (nalLen < 1) {
        return 0;
    }
    if (bH265) {
        int type = (pNal[0] >> 1) & 0x3F;
        return (type <= 31) ? 1 : 0; /* 0..31 are VCL in HEVC */
    } else {
        int type = pNal[0] & 0x1F;
        return (type >= 1 && type <= 5) ? 1 : 0;
    }
}

int main(int argc, char **argv) {
    const char *pszInput;
    const char *pszMode;
    const char *pszTarget;
    int bH265 = 0;
    int u32Fps = 30;
    int maxSec = 0;
    int maxMb = 0;
    int fragMs = 0;
    unsigned char *pBuf = NULL;
    int64_t fileLen = 0;
    int64_t auStart = -1;
    int64_t pos = 0;
    int prefix = 0;
    int auIsKey = 0;
    int auHasVcl = 0;
    uint64_t ptsUs = 0;
    uint64_t ptsStepUs;
    FILE *fp;
    MuxChnAttr stAttr;
    MuxPacket stPkt;
    S32 ret;
    S32 chn = 0;
    int i;

    if (argc < 4) {
        printf("Usage:\n");
        printf("  %s <input.h264|.h265> file <out_pattern> [mp4|ts] [opts]\n", argv[0]);
        printf("  %s <input.h264|.h265> rtmp <rtmp://host/app/key> [opts]\n", argv[0]);
        printf("  opts: --h265 --fps N --max-sec N --max-mb N --frag-ms N\n");
        return 1;
    }
    pszInput = argv[1];
    pszMode = argv[2];
    pszTarget = argv[3];

    for (i = 4; i < argc; ++i) {
        if (strcmp(argv[i], "--h265") == 0) {
            bH265 = 1;
        } else if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc) {
            u32Fps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-sec") == 0 && i + 1 < argc) {
            maxSec = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-mb") == 0 && i + 1 < argc) {
            maxMb = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--frag-ms") == 0 && i + 1 < argc) {
            fragMs = atoi(argv[++i]);
        } else if (strcmp(argv[i], "mp4") == 0 || strcmp(argv[i], "ts") == 0) {
            /* format token handled below */
        }
    }
    if (u32Fps <= 0) {
        u32Fps = 30;
    }
    ptsStepUs = 1000000ULL / (uint64_t)u32Fps;

    /* Load the whole elementary stream into memory (demo convenience). */
    fp = fopen(pszInput, "rb");
    if (!fp) {
        printf("[ERROR] open input '%s' failed\n", pszInput);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    fileLen = (int64_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fileLen <= 0) {
        printf("[ERROR] empty input\n");
        fclose(fp);
        return 1;
    }
    pBuf = (unsigned char *)malloc((size_t)fileLen);
    if (!pBuf || fread(pBuf, 1, (size_t)fileLen, fp) != (size_t)fileLen) {
        printf("[ERROR] read input failed\n");
        free(pBuf);
        fclose(fp);
        return 1;
    }
    fclose(fp);

    signal(SIGINT, sig_handler);

    /* Configure the MUX channel. */
    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.stStreamAttr.eCodecType = bH265 ? MUX_CODEC_H265 : MUX_CODEC_H264;
    stAttr.stStreamAttr.u32Fps = (U32)u32Fps;
    if (strcmp(pszMode, "rtmp") == 0) {
        stAttr.eOutputType = MUX_OUTPUT_RTMP;
        snprintf(stAttr.szUrl, sizeof(stAttr.szUrl), "%s", pszTarget);
    } else {
        stAttr.eOutputType = MUX_OUTPUT_FILE;
        stAttr.stSegment.eFileFormat = MUX_FILE_FMP4;
        for (i = 4; i < argc; ++i) {
            if (strcmp(argv[i], "ts") == 0) {
                stAttr.stSegment.eFileFormat = MUX_FILE_TS;
            }
        }
        snprintf(stAttr.stSegment.szPattern, sizeof(stAttr.stSegment.szPattern), "%s", pszTarget);
        stAttr.stSegment.u32MaxDurationMs = (U32)maxSec * 1000U;
        stAttr.stSegment.u32MaxSizeBytes = (U32)maxMb * 1024U * 1024U;
        stAttr.stSegment.u32FragDurationMs = (U32)fragMs;
    }

    if (SYS_Init() != 0) {
        printf("[ERROR] SYS_Init failed\n");
        free(pBuf);
        return 1;
    }
    if (MUX_Init() != ERR_MUX_OK) {
        printf("[ERROR] MUX_Init failed\n");
        SYS_Exit();
        free(pBuf);
        return 1;
    }
    ret = MUX_CreateChn(chn, &stAttr);
    if (ret != ERR_MUX_OK) {
        printf("[ERROR] MUX_CreateChn failed (ret=%d)\n", ret);
        MUX_Exit();
        SYS_Exit();
        free(pBuf);
        return 1;
    }
    ret = MUX_StartChn(chn);
    if (ret != ERR_MUX_OK) {
        printf("[ERROR] MUX_StartChn failed (ret=%d)\n", ret);
        MUX_DestroyChn(chn);
        MUX_Exit();
        SYS_Exit();
        free(pBuf);
        return 1;
    }
    printf("[INFO] streaming '%s' (%s) -> %s '%s'\n", pszInput, bH265 ? "H265" : "H264", pszMode, pszTarget);

    /* Walk the stream, assemble access units, and send each AU as one packet.
     * An AU boundary is the start of a new VCL slice after we have already
     * collected a VCL slice for the current picture. */
    pos = find_start_code(pBuf, fileLen, 0, &prefix);
    auStart = pos;
    while (pos >= 0 && g_running) {
        int64_t nalStart = pos + prefix;
        int nextPrefix = 0;
        int64_t next = find_start_code(pBuf, fileLen, nalStart, &nextPrefix);
        int64_t nalLen = (next >= 0 ? next : fileLen) - nalStart;
        int bVcl = is_vcl(pBuf + nalStart, nalLen, bH265);

        if (bVcl && auHasVcl) {
            /* Emit the accumulated access unit [auStart, pos). */
            memset(&stPkt, 0, sizeof(stPkt));
            stPkt.pu8Data = pBuf + auStart;
            stPkt.u32Size = (U32)(pos - auStart);
            stPkt.bKeyFrame = (BOOL)auIsKey;
            stPkt.eCodecType = stAttr.stStreamAttr.eCodecType;
            stPkt.u64PTS = ptsUs;
            ret = MUX_SendPacket(chn, &stPkt);
            if (ret != ERR_MUX_OK) {
                printf("[WARN] MUX_SendPacket ret=%d\n", ret);
            }
            ptsUs += ptsStepUs;
            auStart = pos;
            auIsKey = 0;
            auHasVcl = 0;
        }
        if (bVcl) {
            auHasVcl = 1;
            if (is_key_frame(pBuf + nalStart, nalLen, bH265)) {
                auIsKey = 1;
            }
        }
        pos = next;
        prefix = nextPrefix;
    }

    /* Flush the final access unit. */
    if (auStart >= 0 && auHasVcl && g_running) {
        memset(&stPkt, 0, sizeof(stPkt));
        stPkt.pu8Data = pBuf + auStart;
        stPkt.u32Size = (U32)(fileLen - auStart);
        stPkt.bKeyFrame = (BOOL)auIsKey;
        stPkt.eCodecType = stAttr.stStreamAttr.eCodecType;
        stPkt.u64PTS = ptsUs;
        MUX_SendPacket(chn, &stPkt);
    }

    printf("[INFO] done, stopping channel\n");
    MUX_StopChn(chn);
    MUX_DestroyChn(chn);
    MUX_Exit();
    SYS_Exit();
    free(pBuf);
    return 0;
}

