/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    test_demux_seek.c
 * @Brief     :    Test DEMUX low-level seek for MP4/TS files.
 *------------------------------------------------------------------------------
 */

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demux.h"

static S32 run_seek_test(const char *filename, S64 seek_us) {
    DemuxCtx *ctx;
    DemuxPacket pkt;
    S32 ret;
    U32 i;
    U32 packets = 0;

    ctx = Demux_Create(filename);
    if (!ctx) {
        fprintf(stderr, "[SEEK] Demux_Create failed: %s\n", filename);
        return -1;
    }

    ret = Demux_Open(ctx, MPP_TRUE, 5000);
    if (ret != 0) {
        fprintf(stderr, "[SEEK] Demux_Open failed: %d\n", ret);
        Demux_Destroy(ctx);
        return ret;
    }

    ret = Demux_Seek(ctx, seek_us);
    if (ret != 0) {
        fprintf(stderr, "[SEEK] Demux_Seek failed: %d\n", ret);
        Demux_Close(ctx);
        Demux_Destroy(ctx);
        return ret;
    }

    printf("[SEEK] file=%s target_us=%" PRId64 "\n", filename, (int64_t)seek_us);

    for (i = 0; i < 8; i++) {
        memset(&pkt, 0, sizeof(pkt));
        ret = Demux_ReadPacket(ctx, &pkt);
        if (ret == ERR_DEMUX_NO_STREAM) {
            break;
        }
        if (ret != 0) {
            fprintf(stderr, "[SEEK] Demux_ReadPacket failed: %d\n", ret);
            Demux_Close(ctx);
            Demux_Destroy(ctx);
            return ret;
        }

        packets++;
        printf("[SEEK] packet %u: pts=%" PRIu64 " size=%u key=%d codec=%d\n", packets, (uint64_t)pkt.u64PTS,
            pkt.u32Size, pkt.bKeyFrame, pkt.eCodecType);
    }

    Demux_Close(ctx);
    Demux_Destroy(ctx);

    if (packets == 0) {
        fprintf(stderr, "[SEEK] no packet after seek\n");
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    const char *filename;
    S64 seek_us = 5000000;
    S32 ret;

    if (argc < 2) {
        printf("Usage: %s <input_file> [seek_us]\n", argv[0]);
        printf("Example: %s ../test/assets/test_video.ts 5000000\n", argv[0]);
        return -1;
    }

    filename = argv[1];
    if (argc >= 3) {
        seek_us = (S64)atoll(argv[2]);
    }

    ret = run_seek_test(filename, seek_us);
    if (ret != 0) {
        fprintf(stderr, "[SEEK] Test FAILED\n");
        return ret;
    }

    printf("[SEEK] Test PASSED\n");
    return 0;
}
