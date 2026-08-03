#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*
 * K3 hardware probe for external dma-buf ownership during H.264 resolution
 * changes. It holds decoded frames across source-change events and verifies
 * that the decoder neither reuses nor modifies those buffers.
 */

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

#define HELD_FRAME_COUNT 4
#define SEND_RETRIES 500

typedef struct {
    VideoFrameInfo frame;
    uint64_t checksum;
} HeldFrame;

typedef struct {
    HeldFrame held[HELD_FRAME_COUNT];
    U32 held_width;
    U32 held_height;
    int held_count;
    int decoded_count;
    int changed_count;
    int duplicate_count;
} ProbeState;

static U8 *read_file(const char *path, S32 *size) {
    FILE *fp = fopen(path, "rb");
    U8 *data = NULL;
    long length;

    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0 || (length = ftell(fp)) <= 0 || length > INT32_MAX ||
        fseek(fp, 0, SEEK_SET) != 0)
        goto out;
    data = (U8 *)malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, fp) != (size_t)length) {
        free(data);
        data = NULL;
        goto out;
    }
    *size = (S32)length;
out:
    fclose(fp);
    return data;
}

static uint64_t frame_checksum(const VideoFrameInfo *frame) {
    const U8 *base = (const U8 *)(uintptr_t)frame->stVFrame.ulPlaneVirAddr[0];
    U32 width = frame->stVdecFrameInfo.stCommFrameInfo.u32Width;
    U32 height = frame->stVdecFrameInfo.stCommFrameInfo.u32Height;
    U32 stride = frame->stVFrame.u32PlaneStride[0];
    uint64_t hash = UINT64_C(1469598103934665603);

    if (!base)
        return 0;
    if (!stride)
        stride = width;
    for (U32 y = 0; y < height; ++y) {
        for (U32 x = 0; x < width; ++x) {
            hash ^= base[(size_t)y * stride + x];
            hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

static int held_index(const ProbeState *state, UL buffer_id) {
    for (int i = 0; i < state->held_count; ++i) {
        if (state->held[i].frame.ulBufferId == buffer_id)
            return i;
    }
    return -1;
}

static S32 send_with_retry(S32 chn, const U8 *data, U32 size, U64 pts, BOOL eos) {
    StreamBufferInfo stream;

    memset(&stream, 0, sizeof(stream));
    stream.pu8Addr = data;
    stream.u32Size = size;
    stream.bKeyFrame = MPP_FALSE;
    stream.bEndOfStream = eos;
    stream.eCodecType = MPP_STREAM_CODEC_H264;
    stream.u64PTS = pts;
    for (int attempt = 0; attempt < SEND_RETRIES; ++attempt) {
        S32 ret = VDEC_SendStream(chn, &stream, 0);
        if (ret == ERR_VDEC_OK || ret == ERR_VDEC_EOS)
            return ERR_VDEC_OK;
        if (ret != MPP_POLL_FAILED)
            return ret;
        usleep(2000);
    }
    return MPP_POLL_FAILED;
}

static int process_frame(S32 chn, ProbeState *state, VideoFrameInfo *frame) {
    U32 width = frame->stVdecFrameInfo.stCommFrameInfo.u32Width;
    U32 height = frame->stVdecFrameInfo.stCommFrameInfo.u32Height;
    int index = held_index(state, frame->ulBufferId);

    state->decoded_count++;
    if (state->held_count == 0) {
        state->held_width = width;
        state->held_height = height;
    }
    if (width == state->held_width && height == state->held_height &&
        state->held_count < HELD_FRAME_COUNT) {
        HeldFrame *held = &state->held[state->held_count++];
        held->frame = *frame;
        held->checksum = frame_checksum(frame);
        printf("[HOLD] n=%d id=0x%lx checksum=0x%lx\n", state->held_count - 1,
                frame->ulBufferId, held->checksum);
        return 0;
    }

    if (width != state->held_width || height != state->held_height) {
        state->changed_count++;
        if (index >= 0) {
            fprintf(stderr, "[DUPLICATE] small frame id=0x%lx is still held as frame %d\n",
                    frame->ulBufferId, index);
            state->duplicate_count++;
        }
    }
    return VDEC_ReleaseFrame(chn, frame->ulBufferId);
}

static int drain_frames(S32 chn, ProbeState *state, U32 first_timeout_ms) {
    U32 timeout = first_timeout_ms;

    for (;;) {
        VideoFrameInfo frame;
        memset(&frame, 0, sizeof(frame));
        S32 ret = VDEC_GetFrame(chn, &frame, timeout);
        if (ret == ERR_VDEC_NO_FRAME || ret == ERR_VDEC_TIMEOUT)
            return 0;
        if (ret == ERR_VDEC_EOS)
            return 1;
        if (ret != ERR_VDEC_OK) {
            fprintf(stderr, "VDEC_GetFrame failed: %d\n", ret);
            return -1;
        }
        if (process_frame(chn, state, &frame) != 0)
            return -1;
        timeout = 0;
    }
}

static int advance_packet(U8 *source, S32 remaining, const U8 *packet, S32 packet_size,
        U8 **next, S32 *next_remaining) {
    U8 *match = (U8 *)memmem(source, (size_t)remaining, packet, (size_t)packet_size);
    if (!match)
        return -1;
    *next = match + packet_size;
    *next_remaining = remaining - (S32)(*next - source);
    return *next_remaining < 0 ? -1 : 0;
}

int main(int argc, char **argv) {
    U8 *file_data = NULL, *packet = NULL;
    S32 file_size = 0, remaining, packet_size = 0;
    U8 *cursor, *next;
    S32 next_remaining;
    MppParseContext *parser = NULL;
    ProbeState state;
    VdecChnAttr attr;
    U64 pts = 0;
    int first = 1, mutated_count = 0, result = 1;
    int sys_ok = 0, vb_ok = 0, vdec_ok = 0, chn_created = 0, chn_enabled = 0;

    if (argc != 2 && argc != 4) {
        fprintf(stderr, "usage: %s ALTERNATING_RESOLUTION.h264 [INITIAL_WIDTH INITIAL_HEIGHT]\n", argv[0]);
        return 2;
    }
    memset(&state, 0, sizeof(state));
    file_data = read_file(argv[1], &file_size);
    packet = (U8 *)malloc(STREAM_BUFFER_SIZE);
    if (!file_data || !packet)
        goto out;

    if (SYS_Init() != 0)
        goto out;
    sys_ok = 1;
    if (VB_Init() != 0)
        goto out;
    vb_ok = 1;
    if (VDEC_Init() != 0)
        goto out;
    vdec_ok = 1;

    memset(&attr, 0, sizeof(attr));
    attr.eCodecType = MPP_STREAM_CODEC_H264;
    attr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    attr.u32Width = argc == 4 ? (U32)strtoul(argv[2], NULL, 10) : 1280;
    attr.u32Height = argc == 4 ? (U32)strtoul(argv[3], NULL, 10) : 720;
    attr.u32Align = 16;
    attr.u32BufCnt = 12;
    if (VDEC_CreateChn(0, &attr) != 0)
        goto out;
    chn_created = 1;
    if (VDEC_EnableChn(0) != 0)
        goto out;
    chn_enabled = 1;

    parser = PARSE_Create(CODING_H264);
    if (!parser || !parser->ops || parser->ops->init(parser) != MPP_OK)
        goto out;
    cursor = file_data;
    remaining = file_size;
    while (remaining > 0) {
        S32 parse_ret = parser->ops->parse(parser, cursor, remaining, packet, &packet_size, first);
        if (parse_ret != 0 || packet_size <= 0)
            break;
        first = 0;
        if (advance_packet(cursor, remaining, packet, packet_size, &next, &next_remaining) != 0)
            goto out;
        if (send_with_retry(0, packet, (U32)packet_size, pts++, MPP_FALSE) != ERR_VDEC_OK)
            goto out;
        if (drain_frames(0, &state, 20) != 0)
            goto out;
        cursor = next;
        remaining = next_remaining;
    }

    if (send_with_retry(0, packet, 0, pts, MPP_TRUE) != ERR_VDEC_OK)
        goto out;
    for (int i = 0; i < 50; ++i) {
        int drain_ret = drain_frames(0, &state, 100);
        if (drain_ret < 0)
            goto out;
        if (drain_ret > 0)
            break;
        usleep(5000);
    }

    for (int i = 0; i < state.held_count; ++i) {
        uint64_t checksum = frame_checksum(&state.held[i].frame);
        if (checksum != state.held[i].checksum) {
            fprintf(stderr, "[MUTATED] n=%d id=0x%lx before=0x%lx after=0x%lx\n", i,
                    state.held[i].frame.ulBufferId, state.held[i].checksum, checksum);
            mutated_count++;
        }
    }
    printf("[SUMMARY] decoded=%d changed=%d held=%d duplicate_held=%d mutated_held=%d\n",
            state.decoded_count, state.changed_count, state.held_count,
            state.duplicate_count, mutated_count);
    if (state.held_count != HELD_FRAME_COUNT || state.changed_count == 0) {
        fprintf(stderr, "probe did not exercise the required output states\n");
        goto out;
    }
    result = state.duplicate_count || mutated_count ? 1 : 0;
    if (result == 0) {
        S32 ret = VDEC_DisableChn(0);
        if (ret != ERR_VDEC_OK) {
            fprintf(stderr, "VDEC_DisableChn with held frames failed: %d\n", ret);
            result = 1;
            goto out;
        }
        chn_enabled = 0;

        ret = VDEC_DestroyChn(0);
        if (ret != ERR_VDEC_BUSY) {
            fprintf(stderr, "VDEC_DestroyChn with held frames returned %d, expected %d\n",
                    ret, ERR_VDEC_BUSY);
            result = 1;
            goto out;
        }

        for (int i = 0; i < state.held_count; ++i) {
            if (VDEC_ReleaseFrame(0, state.held[i].frame.ulBufferId) != ERR_VDEC_OK) {
                fprintf(stderr, "release held frame %d after disable failed\n", i);
                result = 1;
                goto out;
            }
            state.held[i].frame.ulBufferId = 0;
        }
        state.held_count = 0;

        ret = VDEC_DestroyChn(0);
        if (ret != ERR_VDEC_OK) {
            fprintf(stderr, "VDEC_DestroyChn after releasing held frames failed: %d\n", ret);
            result = 1;
            goto out;
        }
        chn_created = 0;
        printf("[LIFECYCLE] held frames survived disable and blocked destroy until release\n");
    }

out:
    for (int i = 0; i < state.held_count; ++i) {
        if (state.held[i].frame.ulBufferId != 0)
            (void)VDEC_ReleaseFrame(0, state.held[i].frame.ulBufferId);
    }
    if (parser)
        PARSE_Destory(parser);
    if (chn_enabled)
        (void)VDEC_DisableChn(0);
    if (chn_created)
        (void)VDEC_DestroyChn(0);
    if (vdec_ok)
        (void)VDEC_Exit();
    if (vb_ok)
        (void)VB_Exit();
    if (sys_ok)
        (void)SYS_Exit();
    free(packet);
    free(file_data);
    return result;
}
