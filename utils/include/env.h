/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 */

#ifndef __MPP_ENV_H__
#define __MPP_ENV_H__

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

#endif /*__MPP_ENV_H__*/
