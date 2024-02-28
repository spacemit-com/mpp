/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:11:03
 * @LastEditTime: 2023-02-01 11:05:12
 * @Description:
 */

#ifndef __MPP_OS_LOG_H__
#define __MPP_OS_LOG_H__

#include "stdarg.h"

typedef void (*os_log_callback)(const char *, const char *, va_list);

#ifdef __cplusplus
extern "C" {
#endif

void os_log_trace(const char *tag, const char *msg, va_list list);
void os_log_debug(const char *tag, const char *msg, va_list list);
void os_log_info(const char *tag, const char *msg, va_list list);
void os_log_warn(const char *tag, const char *msg, va_list list);
void os_log_error(const char *tag, const char *msg, va_list list);
void os_log_fatal(const char *tag, const char *msg, va_list list);

#ifdef __cplusplus
}
#endif

#endif /*__MPP_OS_LOG_H__*/
