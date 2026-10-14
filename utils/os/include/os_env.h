/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 */

#ifndef OS_ENV_H
#define OS_ENV_H

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

S32 os_get_env_u32(const char *name, U32 *value, U32 default_value);
S32 os_get_env_str(const char *name, char **value, char *default_value);

S32 os_set_env_u32(const char *name, U32 value);
S32 os_set_env_str(const char *name, char *value);

#ifdef __cplusplus
}
#endif

#endif /*OS_ENV_H*/
