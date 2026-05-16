/*
* Copyright 2022-2023 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*/

#ifndef OS_LOG_H
#define OS_LOG_H

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

#endif /* OS_LOG_H */
