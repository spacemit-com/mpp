/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license in LICENSE.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "log.h"
#include "log_internal.h"
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <syslog.h>

static pthread_once_t setup_once = PTHREAD_ONCE_INIT;
static atomic_int syslog_fd = -1;
static atomic_uint_fast64_t dropped;
static struct sockaddr_un address;
static socklen_t address_length;

static void after_fork(void) {
    int fd = atomic_exchange_explicit(&syslog_fd, -1, memory_order_relaxed);
    if (fd >= 0) close(fd);
    atomic_store_explicit(&dropped, 0, memory_order_relaxed);
}

static void setup(void) {
    const char *path = secure_getenv("MPP_SYSLOG_SOCKET");
    if (!path || path[0] != '/' || strlen(path) >= sizeof(address.sun_path))
        path = "/dev/log";
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
    address_length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1);
    /* If registration fails, the inherited unconnected socket still works. */
    (void)pthread_atfork(NULL, NULL, after_fork);
}

static int get_socket(void) {
    int fd = atomic_load_explicit(&syslog_fd, memory_order_relaxed);
    if (fd >= 0) return fd;
    int candidate = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (candidate < 0) return -1;
    int expected = -1;
    if (atomic_compare_exchange_strong_explicit(&syslog_fd, &expected, candidate,
        memory_order_relaxed, memory_order_relaxed)) return candidate;
    close(candidate);
    return expected;
}

static int send_record(int fd, int level, const char *text) {
    static const int priorities[] = {0, LOG_CRIT, LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG, LOG_DEBUG};
    char wire[MPP_LOG_TEXT_SIZE + 64];
    /* Local BSD syslog; keep the application's openlog() settings untouched. */
    int size = snprintf(wire, sizeof(wire), "<%d>mpp[%ld]: %s",
        LOG_USER | priorities[level], (long)getpid(), text);
    if (size <= 0 || (size_t)size >= sizeof(wire)) return -1;
    if (wire[size - 1] == '\n') --size;
    return sendto(fd, wire, (size_t)size, MSG_DONTWAIT | MSG_NOSIGNAL,
        (struct sockaddr *)&address, address_length) == size ? 0 : -1;
}

void mpp_log_publish(int level, const char *text) {
    pthread_once(&setup_once, setup);
    int fd = get_socket();
    /* Concurrent producers send complete datagrams without a userspace send lock.
     * Resolve the pathname on every send so collector restarts recover naturally. */
    if (fd < 0 || send_record(fd, level, text) != 0) {
        atomic_fetch_add_explicit(&dropped, 1, memory_order_relaxed);
        return;
    }
    uint64_t lost = atomic_exchange_explicit(&dropped, 0, memory_order_relaxed);
    if (lost) {
        char notice[128];
        snprintf(notice, sizeof(notice), "syslog transport dropped %" PRIu64 " records", lost);
        if (send_record(fd, MPP_LOG_WARN, notice) != 0)
            atomic_fetch_add_explicit(&dropped, lost, memory_order_relaxed);
    }
}

__attribute__((destructor)) static void log_shutdown(void) {
    int fd = atomic_exchange_explicit(&syslog_fd, -1, memory_order_relaxed);
    if (fd >= 0) close(fd);
}
