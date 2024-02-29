/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:11:03
 * @LastEditTime: 2023-02-02 14:48:41
 * @Description:
 */

#ifndef __MPP_LOG_H__
#define __MPP_LOG_H__

#include <sys/syscall.h>
#include <unistd.h>

#include "type.h"

#define gettid() syscall(SYS_gettid)

#define USE_PRINTF 1

#if USE_PRINTF
#define error(fmt, ...)                                                       \
  printf("[MPP-ERROR] %ld:%s:%d " fmt "\n", gettid(), __FUNCTION__, __LINE__, \
         ##__VA_ARGS__)
// fprintf(stderr, "%ld:%s:%d " fmt "\n", gettid(), __FUNCTION__, __LINE__,
// ##__VA_ARGS__)
#define info(fmt, ...)                                                       \
  printf("[MPP-INFO] %ld:%s:%d " fmt "\n", gettid(), __FUNCTION__, __LINE__, \
         ##__VA_ARGS__)
// fprintf(stderr, "%ld:%s:%d " fmt "\n", gettid(), __FUNCTION__, __LINE__,
// ##__VA_ARGS__)
#if ENABLE_DEBUG
#define debug(fmt, ...)                                                       \
  printf("[MPP-DEBUG] %ld:%s:%d " fmt "\n", gettid(), __FUNCTION__, __LINE__, \
         ##__VA_ARGS__)
// fprintf(stderr, "%ld:%s:%d " fmt "\n", gettid(), __FUNCTION__, __LINE__,
// ##__VA_ARGS__)
#define debug_pre(fmt, ...)                                                  \
  printf("[MPP-DEBUG] %ld:%s:%d " fmt " ", gettid(), __FUNCTION__, __LINE__, \
         ##__VA_ARGS__)
// fprintf(stderr, "%ld:%s:%d " fmt " ", gettid(), __FUNCTION__, __LINE__,
// ##__VA_ARGS__)
#define debug_mid(fmt, ...) printf(" " fmt " ", ##__VA_ARGS__)
// fprintf(stderr, " " fmt " ", ##__VA_ARGS__)
#define debug_after(fmt, ...) printf(" " fmt "\n", ##__VA_ARGS__)
// fprintf(stderr, " " fmt "\n", ##__VA_ARGS__)
#else
#define debug(fmt, ...) \
  do {                  \
  } while (0)
#endif
#else
#define error(fmt, ...) mpp_loge(fmt, ##__VA_ARGS__)
#define info(fmt, ...) mpp_logi(fmt, ##__VA_ARGS__)
#if ENABLE_DEBUG
#define debug(fmt, ...) mpp_logw(fmt, ##__VA_ARGS__)
#else
#define debug(fmt, ...) \
  do {                  \
  } while (0)
#endif
#endif

#define MPP_LOG_UNKNOWN 0  // internal use only
#define MPP_LOG_FATAL 1    // fatal error on aborting
#define MPP_LOG_ERROR 2    // error log on unrecoverable failures
#define MPP_LOG_WARN 3     // warning log on recoverable failures
#define MPP_LOG_INFO 4     // Informational log
#define MPP_LOG_DEBUG 5    // Debug log
#define MPP_LOG_VERBOSE 6  // Verbose log
#define MPP_LOG_SILENT 7   // internal use only

#define mpp_logf(fmt, ...) \
  _mpp_log_l(MPP_LOG_FATAL, MODULE_TAG, fmt, NULL, ##__VA_ARGS__)
#define mpp_loge(fmt, ...) \
  _mpp_log_l(MPP_LOG_ERROR, MODULE_TAG, fmt, NULL, ##__VA_ARGS__)
#define mpp_logw(fmt, ...) \
  _mpp_log_l(MPP_LOG_WARN, MODULE_TAG, fmt, NULL, ##__VA_ARGS__)
#define mpp_logi(fmt, ...) \
  _mpp_log_l(MPP_LOG_INFO, MODULE_TAG, fmt, NULL, ##__VA_ARGS__)
#define mpp_logd(fmt, ...) \
  _mpp_log_l(MPP_LOG_DEBUG, MODULE_TAG, fmt, NULL, ##__VA_ARGS__)
#define mpp_logv(fmt, ...) \
  _mpp_log_l(MPP_LOG_VERBOSE, MODULE_TAG, fmt, NULL, ##__VA_ARGS__)

/*
 * mpp runtime log system usage:
 * mpp_err is for error status message, it will print for sure.
 * mpp_log is for important message like open/close/reset/flush, it will print
 * too.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @description:
 * @param {int} level
 * @param {char} *tag
 * @param {char} *fmt
 * @param {char} *func
 * @return {*}
 */
void _mpp_log_l(int level, const char *tag, const char *fmt, const char *func,
                ...);

/**
 * @description:
 * @param {int} level
 * @return {*}
 */
void mpp_set_log_level(int level);

/**
 * @description:
 * @return {*}
 */
int mpp_get_log_level(void);

/* deprecated function */
/**
 * @description:
 * @param {char} *tag
 * @param {char} *fmt
 * @param {char} *func
 * @return {*}
 */
void _mpp_log(const char *tag, const char *fmt, const char *func, ...);

#ifdef __cplusplus
}
#endif

#endif /*__MPP_LOG_H__*/
