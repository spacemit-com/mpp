/* Copyright 2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 * Exercise the MP4 sample reader without hardware or downloaded media.
 * Include the implementation to inject realloc failure and inspect reuse. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned allocation_calls;
static int fail_allocation;

static void *test_realloc(void *ptr, size_t size) {
    ++allocation_calls;
    return fail_allocation ? NULL : realloc(ptr, size);
}

#define realloc test_realloc
#include "../mpi/demux/container/mp4/mp4_demuxer.c"
#undef realloc

#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x)) {                                                                                          \
            fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                                                  \
            exit(1);                                                                                         \
        }                                                                                                    \
    } while (0)
#define CHECK_EQ(a, b) CHECK((a) == (b))

static Mp4Demuxer *make_reader(const U32 *sizes, U32 count, U8 **expected) {
    Mp4Demuxer *reader = Mp4Demuxer_Create();
    CHECK(reader != NULL);
    reader->pFile = tmpfile();
    CHECK(reader->pFile != NULL);
    TrackInfo *track = &reader->stVideoTrack;
    track->pstSamples = (SampleEntry *)calloc(count, sizeof(SampleEntry));
    CHECK(track->pstSamples != NULL);
    track->u32SampleCount = count;
    track->u32Timescale = 30;
    track->u64Duration = count;
    track->u32Width = 4000;
    track->u32Height = 1200;
    for (U32 i = 0; i < count; ++i) {
        CHECK(sizes[i] >= 4);
        expected[i] = (U8 *)malloc(sizes[i]);
        CHECK(expected[i] != NULL);
        for (U32 j = 0; j < sizes[i]; ++j)
            expected[i][j] = (U8)(j * 17U + i);
        expected[i][0] = 0xff;
        expected[i][1] = 0xd8;
        expected[i][sizes[i] - 2] = 0xff;
        expected[i][sizes[i] - 1] = 0xd9;
        track->pstSamples[i].u64Offset = (U64)ftell(reader->pFile);
        track->pstSamples[i].u32Size = sizes[i];
        CHECK_EQ(fwrite(expected[i], 1, sizes[i], reader->pFile), sizes[i]);
    }
    CHECK_EQ(fflush(reader->pFile), 0);
    return reader;
}

static void test_growth_seek_and_close(void) {
    const U32 sizes[] = {16, 524288, 524289, 649848, 1114788, 16};
    const U32 count = sizeof(sizes) / sizeof(sizes[0]);
    U8 *expected[sizeof(sizes) / sizeof(sizes[0])];
    Mp4Demuxer *reader = make_reader(sizes, count, expected);
    DemuxPacket packet = {0};
    allocation_calls = 0;
    for (U32 i = 0; i < count; ++i) {
        CHECK_EQ(Mp4Demuxer_ReadPacket(reader, &packet), ERR_DEMUX_OK);
        CHECK_EQ(packet.u32Size, sizes[i]);
        CHECK_EQ(memcmp(packet.pu8Data, expected[i], sizes[i]), 0);
        CHECK_EQ(packet.eCodecType, DEMUX_CODEC_MJPEG);
        CHECK_EQ(packet.u64PTS, (U64)i * 33333);
        CHECK_EQ(packet.u32Width, 4000);
        CHECK_EQ(packet.u32Height, 1200);
    }
    CHECK_EQ(allocation_calls, 3);
    CHECK_EQ(Mp4Demuxer_ReadPacket(reader, &packet), ERR_DEMUX_NO_STREAM);
    CHECK_EQ(Mp4Demuxer_Seek(reader, 0), ERR_DEMUX_OK);
    CHECK_EQ(Mp4Demuxer_ReadPacket(reader, &packet), ERR_DEMUX_OK);
    CHECK_EQ(memcmp(packet.pu8Data, expected[0], sizes[0]), 0);
    CHECK_EQ(allocation_calls, 3);
    Mp4Demuxer_Close(reader);
    CHECK(reader->pu8ReadBuf == NULL);
    CHECK_EQ(reader->u32ReadBufSize, 0);
    Mp4Demuxer_Close(reader);
    Mp4Demuxer_Destroy(reader);
    for (U32 i = 0; i < count; ++i)
        free(expected[i]);
}

static void test_allocation_failure_and_limits(void) {
    const U32 sizes[] = {16, 524289};
    U8 *expected[2];
    Mp4Demuxer *reader = make_reader(sizes, 2, expected);
    DemuxPacket packet = {0};
    fail_allocation = 1;
    CHECK_EQ(Mp4Demuxer_ReadPacket(reader, &packet), ERR_DEMUX_NOMEM);
    CHECK_EQ(reader->stVideoTrack.u32CurrentSample, 0);
    fail_allocation = 0;
    CHECK_EQ(Mp4Demuxer_ReadPacket(reader, &packet), ERR_DEMUX_OK);
    U8 *original = reader->pu8ReadBuf;
    U32 capacity = reader->u32ReadBufSize;
    fail_allocation = 1;
    CHECK_EQ(Mp4Demuxer_ReadPacket(reader, &packet), ERR_DEMUX_NOMEM);
    CHECK_EQ(reader->stVideoTrack.u32CurrentSample, 1);
    CHECK(reader->pu8ReadBuf == original);
    CHECK_EQ(reader->u32ReadBufSize, capacity);
    CHECK_EQ(memcmp(reader->pu8ReadBuf, expected[0], sizes[0]), 0);
    fail_allocation = 0;
    CHECK_EQ(Mp4Demuxer_ReadPacket(reader, &packet), ERR_DEMUX_OK);
    CHECK_EQ(memcmp(packet.pu8Data, expected[1], sizes[1]), 0);
    allocation_calls = 0;
    CHECK_EQ(ensure_read_buffer(reader, MP4_MAX_SAMPLE_SIZE + 1), ERR_DEMUX_UNSUPPORTED);
    CHECK_EQ(ensure_read_buffer(reader, 0xffffffffU), ERR_DEMUX_UNSUPPORTED);
    CHECK_EQ(allocation_calls, 0);
    CHECK_EQ(ensure_read_buffer(reader, MP4_MAX_SAMPLE_SIZE), ERR_DEMUX_OK);
    CHECK_EQ(reader->u32ReadBufSize, MP4_MAX_SAMPLE_SIZE);
    Mp4Demuxer_Destroy(reader);
    free(expected[0]);
    free(expected[1]);
}

static void test_non_mjpeg_and_short_read(void) {
    const U32 sizes[] = {8, 524289};
    U8 *expected[2];
    Mp4Demuxer *reader = make_reader(sizes, 2, expected);
    const U8 avcc[] = {0, 0, 0, 4, 0x65, 1, 2, 3};
    const U8 annexb[] = {0, 0, 0, 1, 0x65, 1, 2, 3};
    CHECK_EQ(fseek(reader->pFile, 0, SEEK_SET), 0);
    CHECK_EQ(fwrite(avcc, 1, sizeof(avcc), reader->pFile), sizeof(avcc));
    CHECK_EQ(fwrite(avcc, 1, sizeof(avcc), reader->pFile), sizeof(avcc));
    CHECK_EQ(fflush(reader->pFile), 0);
    reader->stVideoTrack.eCodec = DEMUX_CODEC_H264;
    DemuxPacket packet = {0};
    CHECK_EQ(Mp4Demuxer_ReadPacket(reader, &packet), ERR_DEMUX_OK);
    CHECK_EQ(packet.eCodecType, DEMUX_CODEC_H264);
    CHECK_EQ(packet.u32Size, sizeof(annexb));
    CHECK_EQ(memcmp(packet.pu8Data, annexb, sizeof(annexb)), 0);
    CHECK_EQ(Mp4Demuxer_ReadPacket(reader, &packet), ERR_DEMUX_UNSUPPORTED);
    CHECK_EQ(reader->stVideoTrack.u32CurrentSample, 1);
    reader->stVideoTrack.pstSamples[1].u32Size += 1;
    CHECK_EQ(Mp4Demuxer_ReadPacket(reader, &packet), ERR_DEMUX_NO_STREAM);
    CHECK_EQ(reader->stVideoTrack.u32CurrentSample, 1);
    Mp4Demuxer_Destroy(reader);
    free(expected[0]);
    free(expected[1]);
}

int main(void) {
    test_growth_seek_and_close();
    test_allocation_failure_and_limits();
    test_non_mjpeg_and_short_read();
    puts("PASS: large MP4 MJPEG samples, reuse, seek, OOM, bounds, close and H.264");
    return 0;
}
