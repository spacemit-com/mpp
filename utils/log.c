/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "log.h"
#include "log_internal.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

U32 mpp_debug = 0;
/* UNKNOWN means the startup environment has not yet been read. An explicit API
 * setting takes precedence, even when it happens before the first log call. */
static atomic_int log_level = MPP_LOG_UNKNOWN;
/* The low three bits hold the file override; the rest hold the next check time
 * in monotonic milliseconds. One CAS claims each check, and another publishes
 * it only if no newer check has started. There is no lock to inherit at fork. */
static atomic_uint_fast64_t config_cache;
enum MppLogModule {
    MPP_LOG_MOD_SYS, MPP_LOG_MOD_VB, MPP_LOG_MOD_VI, MPP_LOG_MOD_VO,
    MPP_LOG_MOD_UVC, MPP_LOG_MOD_VDEC, MPP_LOG_MOD_VENC, MPP_LOG_MOD_DEMUX,
    MPP_LOG_MOD_MUX, MPP_LOG_MOD_V2D, MPP_LOG_MOD_AL, MPP_LOG_MOD_UTILS,
    MPP_LOG_MOD_CPP, MPP_LOG_MOD_OTHER, MPP_LOG_MOD_COUNT
};
static const char *const mpp_log_module_names[MPP_LOG_MOD_COUNT] = {
    "sys", "vb", "vi", "vo", "uvc", "vdec", "venc", "demux",
    "mux", "v2d", "al", "utils", "cpp", "other"
};

static const char *basename_of(const char *path) {
    if (!path) return "mpp";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int module_of(const char *tag) {
    if (!tag) return MPP_LOG_MOD_OTHER;
    for (int i = 0; i < MPP_LOG_MOD_COUNT; ++i)
        if (!strcasecmp(tag, mpp_log_module_names[i])) return i;
    /* Source paths and MODULE_TAG values identify the calling module. */
    if (strstr(tag, "linlon") || strstr(tag, "/al/vcodec/") || strstr(tag, "/al/module.c"))
        return MPP_LOG_MOD_AL;
    if (strstr(tag, "/al/vi/")) return MPP_LOG_MOD_VI;
    const char *name = basename_of(tag);
    if (!strncmp(name, "mpp_", 4)) name += 4;
    for (int i = 0; i < MPP_LOG_MOD_COUNT; ++i) {
        size_t n = strlen(mpp_log_module_names[i]);
        if (!strncasecmp(name, mpp_log_module_names[i], n) &&
            (name[n] == '.' || name[n] == '_' || name[n] == '\0')) return i;
    }
    if (!strncmp(name, "dma_", 4) || !strncmp(name, "shm", 3)) return MPP_LOG_MOD_SYS;
    if (strstr(tag, "/utils/") || strstr(name, "v4l2_utils") || !strcmp(name, "env"))
        return MPP_LOG_MOD_UTILS;
    return MPP_LOG_MOD_OTHER;
}

int mpp_log_enabled(int level, const char *tag) {
    if (level < MPP_LOG_FATAL || level > MPP_LOG_VERBOSE) return 0;
    (void)tag;
    int base = mpp_get_log_level();
    return base != MPP_LOG_SILENT && level <= base;
}

static void log_writev(int level, const char *tag, const char *file, const char *func,
    int line, const char *fmt, va_list args) {
    if (!fmt || !mpp_log_enabled(level, tag)) return;
    int saved_errno = errno;
    char message[1024];
    int count = vsnprintf(message, sizeof(message), fmt, args);
    if (count < 0) { errno = saved_errno; return; }
    if ((size_t)count >= sizeof(message)) {
        static const char suffix[] = " [truncated]";
        memcpy(message + sizeof(message) - sizeof(suffix), suffix, sizeof(suffix));
    }
    size_t len = strlen(message);
    while (len && (message[len - 1] == '\n' || message[len - 1] == '\r')) message[--len] = '\0';
    for (size_t i = 0; i < len; ++i)
        if (message[i] == '\n' || message[i] == '\r') message[i] = ' ';
    static const char *const levels[] = {"?", "FATAL", "ERROR", "WARN", "INFO", "DEBUG", "VERBOSE"};
    struct timespec now;
    struct tm tm;
    char stamp[32], text[MPP_LOG_TEXT_SIZE];
    clock_gettime(CLOCK_REALTIME, &now);
    localtime_r(&now.tv_sec, &tm);
    strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm);
    int module = module_of(tag);
    snprintf(text, sizeof(text), "%s.%03ld [%-7s] [%ld:%ld] [%s] %.96s:%d %.64s: %s\n",
        stamp, now.tv_nsec / 1000000, levels[level], (long)getpid(), (long)gettid(),
        mpp_log_module_names[module], basename_of(file), line,
        func ? func : "-", message);
    mpp_log_publish(level, text);
    errno = saved_errno;
}

void mpp_log_write(int level, const char *tag, const char *file, const char *func,
    int line, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    log_writev(level, tag, file, func, line, fmt, args);
    va_end(args);
}
void _mpp_log_l(int level, const char *tag, const char *fmt, const char *func, ...) {
    va_list args;
    va_start(args, func);
    log_writev(level, tag, tag, func, 0, fmt, args);
    va_end(args);
}
void _mpp_log(const char *tag, const char *fmt, const char *func, ...) {
    va_list args;
    va_start(args, func);
    log_writev(MPP_LOG_INFO, tag, tag, func, 0, fmt, args);
    va_end(args);
}
void mpp_set_log_level(int level) {
    if (level < MPP_LOG_FATAL || level > MPP_LOG_SILENT) level = MPP_LOG_INFO;
    atomic_store_explicit(&log_level, level, memory_order_relaxed);
}
static int parse_log_level(const char *value) {
    static const char *const names[] = {"fatal", "error", "warn", "info", "debug", "verbose", "silent"};
    if (!value) return MPP_LOG_UNKNOWN;
    for (int i = 0; i < 7; ++i)
        if (!strcasecmp(value, names[i])) return i + 1;
    if (value[0] >= '1' && value[0] <= '7' && value[1] == '\0')
        return value[0] - '0';
    return MPP_LOG_UNKNOWN;
}

static int read_runtime_level(void) {
    const char *path = secure_getenv("MPP_LOG_LEVEL_FILE");
    if (!path || path[0] != '/') path = "/run/mpp/log-level";
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? MPP_LOG_UNKNOWN : -1;
    struct stat st;
    char value[32];
    ssize_t count = -1;
    if (!fstat(fd, &st) && S_ISREG(st.st_mode)) count = read(fd, value, sizeof(value));
    close(fd);
    /* Ignore partial, malformed or non-regular files, keeping the last value. */
    if (count <= 0 || count >= (ssize_t)sizeof(value) || memchr(value, '\0', count)) return -1;
    value[count] = '\0';
    while (count && isspace((unsigned char)value[count - 1])) value[--count] = '\0';
    char *begin = value;
    while (isspace((unsigned char)*begin)) ++begin;
    int level = parse_log_level(begin);
    return level == MPP_LOG_UNKNOWN ? -1 : level;
}

static int runtime_log_level(void) {
    uint_fast64_t cached = atomic_load_explicit(&config_cache, memory_order_relaxed);
    struct timespec now;
    if (!clock_gettime(CLOCK_MONOTONIC, &now)) {
        uint_fast64_t ms = (uint_fast64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
        if (ms >= (cached >> 3)) {
            uint_fast64_t claimed = ((ms + 1000) << 3) | (cached & 7);
            if (atomic_compare_exchange_strong_explicit(&config_cache, &cached, claimed,
                memory_order_relaxed, memory_order_relaxed)) {
                int level = read_runtime_level();
                uint_fast64_t updated = (claimed & ~(uint_fast64_t)7) |
                    (level < 0 ? (claimed & 7) : (uint_fast64_t)level);
                atomic_compare_exchange_strong_explicit(&config_cache, &claimed, updated,
                    memory_order_relaxed, memory_order_relaxed);
            }
        }
    }
    return (int)(atomic_load_explicit(&config_cache, memory_order_relaxed) & 7);
}

int mpp_get_log_level(void) {
    int saved_errno = errno;
    int current = atomic_load_explicit(&log_level, memory_order_relaxed);
    if (current == MPP_LOG_UNKNOWN) {
        int configured = parse_log_level(secure_getenv("MPP_LOG_LEVEL"));
        if (configured == MPP_LOG_UNKNOWN) configured = MPP_LOG_INFO;
        atomic_compare_exchange_strong_explicit(&log_level, &current, configured,
            memory_order_relaxed, memory_order_relaxed);
    }
    int override = runtime_log_level();
    current = override ? override : atomic_load_explicit(&log_level, memory_order_relaxed);
    errno = saved_errno;
    return current;
}
