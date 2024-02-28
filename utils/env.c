/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-01-12 14:40:03
 * @LastEditTime: 2024-01-12 14:41:29
 * @Description:
 */

#define MODULE_TAG "mpp_env"

#include "env.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "os_env.h"

#ifdef __cplusplus
extern "C" {
#endif

S32 mpp_env_get_u32(const U8 *name, U32 *value, U32 default_value) {
  return os_get_env_u32(name, value, default_value);
}

S32 mpp_env_get_str(const U8 *name, U8 **value, U8 *default_value) {
  return os_get_env_str(name, value, default_value);
}

S32 mpp_env_set_u32(const U8 *name, U32 value) {
  return os_set_env_u32(name, value);
}

S32 mpp_env_set_str(const U8 *name, U8 *value) {
  return os_set_env_str(name, value);
}

#ifdef __cplusplus
}
#endif