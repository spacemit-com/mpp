/***
 * @Copyright 2022-2023 SPACEMIT. All rights reserved.
 * @Use of this source code is governed by a BSD-style license
 * @that can be found in the LICENSE file.
 * @
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2024-01-12 14:33:55
 * @LastEditTime: 2024-01-12 14:48:40
 * @Description:
 */

#ifndef __OS_ENV_H__
#define __OS_ENV_H__

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

S32 os_get_env_u32(const char *name, U32 *value, U32 default_value);
S32 os_get_env_str(const char *name, const char **value,
                   const char *default_value);

S32 os_set_env_u32(const char *name, U32 value);
S32 os_set_env_str(const char *name, char *value);

#ifdef __cplusplus
}
#endif

#endif /*__OS_ENV_H__*/
