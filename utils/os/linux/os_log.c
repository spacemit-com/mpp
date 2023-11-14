/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-01-13 18:11:10
 * @LastEditTime: 2023-02-02 15:26:37
 * @Description:
 */

#include "os_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>

#include "type.h"

#define LINE_SZ 1024

void os_log_trace(const char *tag, const char *msg, va_list list) {
  char line[LINE_SZ] = {0};
  snprintf(line, sizeof(line) - 1, "%s: %s", tag, msg);
  vsyslog(LOG_NOTICE, line, list);
}

void os_log_debug(const char *tag, const char *msg, va_list list) {
  char line[LINE_SZ] = {0};
  snprintf(line, sizeof(line) - 1, "%s: %s", tag, msg);
  vsyslog(LOG_DEBUG, line, list);
}

void os_log_info(const char *tag, const char *msg, va_list list) {
  char line[LINE_SZ] = {0};
  snprintf(line, sizeof(line) - 1, "%s: %s", tag, msg);
  vsyslog(LOG_INFO, line, list);
}

void os_log_warn(const char *tag, const char *msg, va_list list) {
  char line[LINE_SZ] = {0};
  snprintf(line, sizeof(line) - 1, "%s: %s", tag, msg);
  vsyslog(LOG_WARNING, line, list);
}

void os_log_error(const char *tag, const char *msg, va_list list) {
  char line[LINE_SZ] = {0};
  snprintf(line, sizeof(line) - 1, "%s: %s", tag, msg);
  vsyslog(LOG_ERR, line, list);
}

void os_log_fatal(const char *tag, const char *msg, va_list list) {
  char line[LINE_SZ] = {0};
  snprintf(line, sizeof(line) - 1, "%s: %s", tag, msg);
  vsyslog(LOG_CRIT, line, list);
}
