/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 */
#ifndef LOG_H
#define LOG_H
#include <sys/syscall.h>
#include <unistd.h>
#include "type.h"

#ifndef gettid
#define gettid() syscall(SYS_gettid)
#endif
#define MPP_LOG_UNKNOWN 0
#define MPP_LOG_FATAL 1
#define MPP_LOG_ERROR 2
#define MPP_LOG_WARN 3
#define MPP_LOG_INFO 4
#define MPP_LOG_DEBUG 5
#define MPP_LOG_VERBOSE 6
#define MPP_LOG_SILENT 7

#ifdef __cplusplus
extern "C" {
#endif
int mpp_log_enabled(int level, const char *tag);
void mpp_log_write(int level, const char *tag, const char *file, const char *func,
    int line, const char *fmt, ...) __attribute__((format(printf, 6, 7)));
void _mpp_log_l(int level, const char *tag, const char *fmt, const char *func, ...)
    __attribute__((format(printf, 3, 5)));
void _mpp_log(const char *tag, const char *fmt, const char *func, ...)
    __attribute__((format(printf, 2, 4)));
/* Set the process baseline; a valid runtime file overrides it while present. */
void mpp_set_log_level(int level);
/* Return the effective level, refreshing the runtime file at most once a second. */
int mpp_get_log_level(void);
#ifdef __cplusplus
}
#endif

/* Check before evaluating printf arguments, including in Release builds. */
#define MPP_LOG(level, tag, fmt, ...) do { \
    if (mpp_log_enabled((level), (tag))) \
        mpp_log_write((level), (tag), __FILE__, __func__, __LINE__, (fmt), ##__VA_ARGS__); \
} while (0)

/* error/info/debug use their source paths to identify modules. */
#define error(fmt, ...) MPP_LOG(MPP_LOG_ERROR, __FILE__, fmt, ##__VA_ARGS__)
#define info(fmt, ...) MPP_LOG(MPP_LOG_INFO, __FILE__, fmt, ##__VA_ARGS__)
#define debug(fmt, ...) MPP_LOG(MPP_LOG_DEBUG, __FILE__, fmt, ##__VA_ARGS__)
#define mpp_logf(fmt, ...) MPP_LOG(MPP_LOG_FATAL, MODULE_TAG, fmt, ##__VA_ARGS__)
#define mpp_loge(fmt, ...) MPP_LOG(MPP_LOG_ERROR, MODULE_TAG, fmt, ##__VA_ARGS__)
#define mpp_logw(fmt, ...) MPP_LOG(MPP_LOG_WARN, MODULE_TAG, fmt, ##__VA_ARGS__)
#define mpp_logi(fmt, ...) MPP_LOG(MPP_LOG_INFO, MODULE_TAG, fmt, ##__VA_ARGS__)
#define mpp_logd(fmt, ...) MPP_LOG(MPP_LOG_DEBUG, MODULE_TAG, fmt, ##__VA_ARGS__)
#define mpp_logv(fmt, ...) MPP_LOG(MPP_LOG_VERBOSE, MODULE_TAG, fmt, ##__VA_ARGS__)
#endif
