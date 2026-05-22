/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 */

#include "os_env.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

#include "type.h"

#define ENV_BUF_SIZE_LINUX 1024

S32 os_get_env_u32(const char *name, U32 *value, U32 default_value) {
    char *ptr = getenv(name);
    if (NULL == ptr) {
        *value = default_value;
    } else {
        char *endptr;
        int base = (ptr[0] == '0' && ptr[1] == 'x') ? (16) : (10);
        errno = 0;
        *value = strtoul(ptr, &endptr, base);
        if (errno || (ptr == endptr)) {
            errno = 0;
            *value = default_value;
        }
    }
    return 0;
}

S32 os_get_env_str(const char *name, char **value, char *default_value) {
    *value = getenv(name);
    if (NULL == *value) {
        *value = default_value;
    }
    return 0;
}

S32 os_set_env_u32(const char *name, U32 value) {
    char buf[ENV_BUF_SIZE_LINUX];
    snprintf(buf, sizeof(buf) - 1, "%u", value);
    return setenv(name, buf, 1);
}

S32 os_set_env_str(const char *name, char *value) {
    return setenv(name, value, 1);
}
