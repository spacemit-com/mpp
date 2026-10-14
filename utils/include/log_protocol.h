/* Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license in LICENSE. */
#ifndef LOG_PROTOCOL_H
#define LOG_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

/* Local Linux SOCK_SEQPACKET protocol. Each response is one UTF-8 text record.
 * Native byte order is intentional: both endpoints run on the same host. */
#define MPP_LOG_PROTOCOL_VERSION 1
#define MPP_LOG_RING_SIZE 512
#define MPP_LOG_TEXT_SIZE 1536
#define MPP_LOG_MAX_CLIENTS 8
#define MPP_LOG_READY "@mpp-log ready\n"
#define MPP_LOG_END "@mpp-log end\n"
#define MPP_LOG_SOCKET_PREFIX "mpp-log."

enum MppLogModule {
    MPP_LOG_MOD_SYS, MPP_LOG_MOD_VB, MPP_LOG_MOD_VI, MPP_LOG_MOD_VO,
    MPP_LOG_MOD_UVC, MPP_LOG_MOD_VDEC, MPP_LOG_MOD_VENC, MPP_LOG_MOD_DEMUX,
    MPP_LOG_MOD_MUX, MPP_LOG_MOD_V2D, MPP_LOG_MOD_AL, MPP_LOG_MOD_UTILS,
    MPP_LOG_MOD_CPP, MPP_LOG_MOD_OTHER, MPP_LOG_MOD_COUNT
};
#define MPP_LOG_ALL_MODULES ((1U << MPP_LOG_MOD_COUNT) - 1U)
static const char *const mpp_log_module_names[MPP_LOG_MOD_COUNT] = {
    "sys", "vb", "vi", "vo", "uvc", "vdec", "venc", "demux",
    "mux", "v2d", "al", "utils", "cpp", "other"
};
typedef struct MppLogRequest {
    uint32_t version;
    uint32_t level;
    uint32_t modules;
    uint32_t tail;
    uint32_t follow;
} MppLogRequest;

static inline socklen_t mpp_log_address(struct sockaddr_un *addr, unsigned uid, unsigned pid) {
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    snprintf(addr->sun_path + 1, sizeof(addr->sun_path) - 1,
        MPP_LOG_SOCKET_PREFIX "%u.%u", uid, pid);
    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + strlen(addr->sun_path + 1));
}
#endif
