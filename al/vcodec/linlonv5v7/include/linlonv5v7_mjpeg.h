/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 */

#ifndef LINLONV5V7_MJPEG_H
#define LINLONV5V7_MJPEG_H

#include <stddef.h>
#include <stdint.h>

/* dst may equal src; successful in-place normalization is guaranteed. */
int linlon_mjpeg_remove_zero_dri(const uint8_t *src, size_t src_size, uint8_t *dst,
    size_t dst_capacity, size_t *dst_size);

#endif
