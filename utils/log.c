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
#include <errno.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <time.h>

U32 mpp_debug = 0; /* Retained exported symbol for existing applications. */
static atomic_int log_level = MPP_LOG_INFO;

static const char *basename_of(const char *path) {
    if (!path) return "mpp";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int module_of(const char *tag) {
    if (!tag) return MPP_LOG_MOD_OTHER;
    for (int i = 0; i < MPP_LOG_MOD_COUNT; ++i)
        if (!strcasecmp(tag, mpp_log_module_names[i])) return i;
    /* File paths identify legacy macros; MODULE_TAG identifies logx callers. */
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
    int base = atomic_load_explicit(&log_level, memory_order_relaxed);
    if (base != MPP_LOG_SILENT && level <= base) return 1;
    return level <= mpp_log_subscription_level(module_of(tag));
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
    mpp_log_publish(level, module, text);
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
int mpp_get_log_level(void) {
    return atomic_load_explicit(&log_level, memory_order_relaxed);
}
