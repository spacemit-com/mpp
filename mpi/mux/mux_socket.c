/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *------------------------------------------------------------------------------
 */

#include "mux_socket.h"

#include <sys/socket.h>

ssize_t mux_socket_send_no_signal(
    S32 s32Fd, const VOID *pData, size_t uSize, S32 s32Flags) {
    return send(s32Fd, pData, uSize, s32Flags | MSG_NOSIGNAL);
}
