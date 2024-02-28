/***
 * @Copyright 2022-2023 SPACEMIT. All rights reserved.
 * @Use of this source code is governed by a BSD-style license
 * @that can be found in the LICENSE file.
 * @
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-01-12 14:37:30
 * @LastEditTime: 2024-01-12 14:38:39
 * @Description:
 */

#ifndef __MPP_ENV_H__
#define __MPP_ENV_H__

#include <sys/syscall.h>
#include <unistd.h>

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

S32 mpp_env_get_u32(const U8 *name, U32 *value, U32 default_value);
S32 mpp_env_get_str(const U8 *name, U8 **value, U8 *default_value);

S32 mpp_env_set_u32(const U8 *name, U32 value);
S32 mpp_env_set_str(const U8 *name, U8 *value);

#ifdef __cplusplus
}
#endif

#endif /*__MPP_ENV_H__*/