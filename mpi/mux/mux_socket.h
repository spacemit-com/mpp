/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *------------------------------------------------------------------------------
 */

#ifndef MUX_SOCKET_H
#define MUX_SOCKET_H

#include <stddef.h>
#include <sys/types.h>

#include "sys/type.h"

__attribute__((visibility("hidden"))) ssize_t mux_socket_send_no_signal(
    S32 s32Fd, const VOID *pData, size_t uSize, S32 s32Flags);

#endif /* MUX_SOCKET_H */
