/* Copyright 2026 SPACEMIT. All rights reserved. BSD-style license.
 * File-only packet integrity/lifecycle test; no hardware or CMA allocation. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/mem.h>
#include <libavutil/sha.h>

#include "demux.h"
#include "container/mkv/mkv_demuxer.h"

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                             \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)

int main(int argc, char **argv) {
    DemuxPacket packet;
    DemuxStreamInfo info;
    CHECK(Demux_DetectProtocol("camera.MKV") == DEMUX_PROTO_FILE_MKV);
    CHECK(Demux_DetectProtocol("file:///path/video.mkv") == DEMUX_PROTO_FILE_MKV);
    CHECK(Demux_IsSupported(DEMUX_PROTO_FILE_MKV));
    CHECK(MkvDemuxer_Open(NULL, "x", 0) == ERR_DEMUX_NULL_PTR);
    CHECK(MkvDemuxer_ReadPacket(NULL, &packet) == ERR_DEMUX_NULL_PTR);
    MkvDemuxer *empty = MkvDemuxer_Create();
    CHECK(empty != NULL);
    CHECK(MkvDemuxer_ReadPacket(empty, &packet) == ERR_DEMUX_NOT_STARTED);
    CHECK(MkvDemuxer_GetStreamInfo(empty, &info) == ERR_DEMUX_NOT_STARTED);
    CHECK(MkvDemuxer_Seek(empty, 0) == ERR_DEMUX_NOT_STARTED);
    CHECK(MkvDemuxer_GetDuration(empty) == 0);
    CHECK(MkvDemuxer_Open(empty, "/", 1) == ERR_DEMUX_OPEN_FAIL);
    CHECK(MkvDemuxer_Open(empty, "/nonexistent/mpp-test.mkv", 1) == ERR_DEMUX_OPEN_FAIL);
    MkvDemuxer_Close(empty);
    MkvDemuxer_Close(empty);
    MkvDemuxer_Destroy(empty);
    MkvDemuxer_Destroy(NULL);
    if (argc < 3 || argc > 5) {
        fprintf(stderr, "Usage: %s INPUT OUTPUT.csv [OUTPUT.es] [SEEK_US]\n", argv[0]);
        return 2;
    }
    DemuxCtx *ctx = Demux_Create(argv[1]);
    CHECK(ctx != NULL);
    CHECK(Demux_Open(ctx, MPP_FALSE, 5000) == ERR_DEMUX_OK);
    CHECK(Demux_GetStreamInfoCtx(ctx, &info) == ERR_DEMUX_OK);
    CHECK(info.u32Width > 0 && info.u32Height > 0);
    fprintf(stderr, "INFO width=%u height=%u fps=%u codec=%d duration=%" PRId64 "\n", info.u32Width,
            info.u32Height, info.u32Fps, info.eCodecType, (int64_t)Demux_GetDuration(ctx));
    if (argc == 5) {
        CHECK(Demux_Seek(ctx, strtoll(argv[4], NULL, 10)) == ERR_DEMUX_OK);
    }
    FILE *ledger = fopen(argv[2], "w");
    FILE *dump = argc >= 4 ? fopen(argv[3], "wb") : NULL;
    CHECK(ledger != NULL && (argc < 4 || dump != NULL));
    struct AVSHA *sha = av_sha_alloc();
    CHECK(sha != NULL);
    unsigned count = 0;
    S32 ret;
    U64 first_pts = 0;
    U32 first_size = 0;
    U8 first_hash[32];
    fprintf(ledger, "index,pts_us,size,keyframe,sha256\n");
    while ((ret = Demux_ReadPacket(ctx, &packet)) == ERR_DEMUX_OK) {
        U8 hash[32];
        CHECK(packet.pu8Data != NULL && packet.u32Size > 0);
        if (Demux_DetectProtocol(argv[1]) == DEMUX_PROTO_FILE_MKV) {
            CHECK(packet.eCodecType == info.eCodecType);
        }
        CHECK(av_sha_init(sha, 256) == 0);
        av_sha_update(sha, packet.pu8Data, packet.u32Size);
        av_sha_final(sha, hash);
        if (count == 0) {
            first_pts = packet.u64PTS;
            first_size = packet.u32Size;
            memcpy(first_hash, hash, sizeof(hash));
        }
        fprintf(ledger, "%u,%" PRIu64 ",%u,%d,", count, (uint64_t)packet.u64PTS, packet.u32Size,
                packet.bKeyFrame);
        for (unsigned i = 0; i < sizeof(hash); ++i) {
            fprintf(ledger, "%02x", hash[i]);
        }
        fprintf(ledger, "\n");
        if (dump) {
            CHECK(fwrite(packet.pu8Data, 1, packet.u32Size, dump) == packet.u32Size);
        }
        ++count;
    }
    CHECK(ret == ERR_DEMUX_NO_STREAM);
    CHECK(count > 0);
    CHECK(Demux_ReadPacket(ctx, &packet) == ERR_DEMUX_NO_STREAM);
    CHECK(Demux_Seek(ctx, (S64)first_pts) == ERR_DEMUX_OK);
    CHECK(Demux_ReadPacket(ctx, &packet) == ERR_DEMUX_OK);
    CHECK(packet.u64PTS <= first_pts);
    if (argc != 5) {
        CHECK(packet.u64PTS == first_pts && packet.u32Size == first_size);
        U8 hash[32];
        CHECK(av_sha_init(sha, 256) == 0);
        av_sha_update(sha, packet.pu8Data, packet.u32Size);
        av_sha_final(sha, hash);
        CHECK(memcmp(hash, first_hash, sizeof(hash)) == 0);
    }
    if (Demux_DetectProtocol(argv[1]) == DEMUX_PROTO_FILE_MKV) {
        Demux_Close(ctx);
        Demux_Close(ctx);
        CHECK(Demux_Open(ctx, MPP_FALSE, 5000) == ERR_DEMUX_OK);
        CHECK(Demux_ReadPacket(ctx, &packet) == ERR_DEMUX_OK);
    }
    Demux_Destroy(ctx);
    av_free(sha);
    CHECK(fclose(ledger) == 0);
    if (dump) {
        CHECK(fclose(dump) == 0);
    }
    fprintf(stderr, "PASS packets=%u (EOF, seek; MKV also exercises reopen/close)\n", count);
    return 0;
}
