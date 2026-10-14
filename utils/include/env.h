/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 */

#ifndef ENV_H
#define ENV_H

#include <sys/syscall.h>
#include <unistd.h>

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

S32 mpp_env_get_u32(const char *name, U32 *value, U32 default_value);
S32 mpp_env_get_str(const char *name, char **value, char *default_value);

S32 mpp_env_set_u32(const char *name, U32 value);
S32 mpp_env_set_str(const char *name, char *value);

#ifdef __cplusplus
}
#endif

#endif /*ENV_H*/
