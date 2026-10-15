/* Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license in LICENSE. */
#ifndef LOG_INTERNAL_H
#define LOG_INTERNAL_H
#include "log_protocol.h"
int mpp_log_subscription_level(int module);
void mpp_log_publish(int level, int module, const char *text);
#endif
