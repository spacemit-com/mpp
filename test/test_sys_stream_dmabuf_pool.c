/*
 * Hardware-only regression for the SYS fixed compressed-input DMA-BUF pool.
 * It intentionally does not need a V4L2 decoder: verifying the lease ring in
 * isolation makes reuse and ownership failures deterministic.
 */

#include <stdio.h>
#include <string.h>

#include "sys/sys_api.h"

#define TEST_SLOT_SIZE (64 * 1024U)
#define TEST_SLOT_COUNT 2U

static int check(S32 ret, const char *operation) {
    if (ret == SYS_ERR_OK) {
        return 0;
    }
    fprintf(stderr, "%s failed: %d\n", operation, ret);
    return -1;
}

static StreamBufferInfo make_packet(const U8 *payload, U32 size, U64 pts) {
    StreamBufferInfo packet;

    memset(&packet, 0, sizeof(packet));
    packet.pu8Addr = payload;
    packet.u32Size = size;
    packet.eCodecType = MPP_STREAM_CODEC_MJPEG;
    packet.u64PTS = pts;
    packet.s32DmaBufFd = -1;
    return packet;
}

static S32 send_packet(const MppNode *source, const U8 *payload, U32 size, U64 pts) {
    StreamBufferInfo packet = make_packet(payload, size, pts);
    return SYS_SendStream(source, &packet);
}

int main(void) {
    const MppNode source = {.eModId = MPP_ID_UVC, .s32DevId = 31, .s32ChnId = 0};
    const MppNode sink = {.eModId = MPP_ID_VDEC, .s32DevId = 0, .s32ChnId = 31};
    const U8 packet_a[] = "packet-a";
    const U8 packet_b[] = "packet-b";
    const U8 packet_c[] = "packet-c";
    StreamBufferInfo lease_a;
    StreamBufferInfo lease_b;
    StreamBufferInfo lease_c;
    S32 fd_a;

    if (check(SYS_Init(), "SYS_Init") != 0) {
        return 1;
    }
    if (check(SYS_Bind(&source, &sink), "SYS_Bind") != 0) {
        return 1;
    }
    if (check(SYS_ConfigStreamDmaBufPool(&source, &sink, TEST_SLOT_SIZE, TEST_SLOT_COUNT), "pool config") != 0) {
        (void)SYS_UnBind(&source, &sink);
        (void)SYS_Exit();
        return 77; /* No usable CMA heap on a host is an expected skip. */
    }

    if (check(send_packet(&source, packet_a, sizeof(packet_a), 1), "send a") != 0 ||
        check(SYS_RecvStreamDmaBuf(&sink, &lease_a, 0), "receive a") != 0 ||
        lease_a.s32DmaBufFd < 0 || lease_a.u64DmaBufToken == 0 ||
        memcmp(lease_a.pu8Addr, packet_a, sizeof(packet_a)) != 0) {
        return 1;
    }
    fd_a = lease_a.s32DmaBufFd;

    if (check(send_packet(&source, packet_b, sizeof(packet_b), 2), "send b") != 0 ||
        check(SYS_RecvStreamDmaBuf(&sink, &lease_b, 0), "receive b") != 0 ||
        memcmp(lease_b.pu8Addr, packet_b, sizeof(packet_b)) != 0) {
        return 1;
    }

    if (send_packet(&source, packet_c, sizeof(packet_c), 3) != SYS_ERR_FULL) {
        fprintf(stderr, "pool accepted a packet while every slot was leased\n");
        return 1;
    }
    if (SYS_UnBind(&source, &sink) != SYS_ERR_BUSY) {
        fprintf(stderr, "unbind accepted while a DMA-BUF lease was active\n");
        return 1;
    }

    if (check(SYS_ReleaseStreamDmaBuf(lease_a.u64DmaBufToken), "release a") != 0 ||
        check(send_packet(&source, packet_c, sizeof(packet_c), 3), "send c") != 0 ||
        check(SYS_RecvStreamDmaBuf(&sink, &lease_c, 0), "receive c") != 0 || lease_c.s32DmaBufFd != fd_a ||
        memcmp(lease_c.pu8Addr, packet_c, sizeof(packet_c)) != 0) {
        return 1;
    }

    if (check(SYS_ReleaseStreamDmaBuf(lease_b.u64DmaBufToken), "release b") != 0 ||
        check(SYS_ReleaseStreamDmaBuf(lease_c.u64DmaBufToken), "release c") != 0 ||
        check(SYS_UnBind(&source, &sink), "SYS_UnBind") != 0 || check(SYS_Exit(), "SYS_Exit") != 0) {
        return 1;
    }

    printf("[PASS] fixed stream DMA-BUF pool reused fd=%d without hot-path allocation\n", fd_a);
    return 0;
}
