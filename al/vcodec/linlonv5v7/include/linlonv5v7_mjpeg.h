/*
 * Copyright 2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 */

#ifndef LINLONV5V7_MJPEG_H
#define LINLONV5V7_MJPEG_H

#include <stddef.h>
#include <stdint.h>

int linlon_mjpeg_remove_zero_dri(
    const uint8_t *src, size_t src_size, uint8_t *dst, size_t dst_capacity, size_t *dst_size
);

#endif
