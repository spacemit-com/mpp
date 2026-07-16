/*
 * Copyright 2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 */

#include "linlonv5v7_mjpeg.h"

#include <string.h>

#define JPEG_MARKER_PREFIX 0xff
#define JPEG_MARKER_SOI 0xd8
#define JPEG_MARKER_EOI 0xd9
#define JPEG_MARKER_SOS 0xda
#define JPEG_MARKER_DRI 0xdd
#define JPEG_MARKER_TEM 0x01
#define JPEG_MARKER_RST0 0xd0
#define JPEG_MARKER_RST7 0xd7

typedef struct {
    size_t start;
    size_t end;
    int is_zero_dri;
} JpegMarker;

static int marker_has_no_length(uint8_t marker) {
    return marker == JPEG_MARKER_SOI || marker == JPEG_MARKER_EOI || marker == JPEG_MARKER_TEM ||
        (marker >= JPEG_MARKER_RST0 && marker <= JPEG_MARKER_RST7);
}

static int next_header_marker(const uint8_t *src, size_t size, size_t *pos, JpegMarker *result) {
    if (*pos >= size)
        return 0;

    result->start = *pos;
    result->end = *pos;
    result->is_zero_dri = 0;
    if (src[*pos] != JPEG_MARKER_PREFIX)
        return -1;
    while (*pos < size && src[*pos] == JPEG_MARKER_PREFIX)
        (*pos)++;
    if (*pos == size || src[*pos] == 0)
        return -1;

    uint8_t marker = src[(*pos)++];
    if (marker == JPEG_MARKER_SOS || marker == JPEG_MARKER_EOI)
        return 0;
    if (marker_has_no_length(marker)) {
        result->end = *pos;
        return 1;
    }
    if (size - *pos < 2)
        return -1;

    size_t segment_size = ((size_t)src[*pos] << 8) | src[*pos + 1];
    if (segment_size < 2 || segment_size > size - *pos)
        return -1;
    result->end = *pos + segment_size;
    result->is_zero_dri = marker == JPEG_MARKER_DRI && segment_size == 4 &&
        src[*pos + 2] == 0 && src[*pos + 3] == 0;
    *pos = result->end;
    return 1;
}

static int scan_headers(const uint8_t *src, size_t size, int *has_zero_dri) {
    size_t pos = 2;
    JpegMarker marker;
    int ret;

    *has_zero_dri = 0;
    if (size < 2 || src[0] != JPEG_MARKER_PREFIX || src[1] != JPEG_MARKER_SOI)
        return -1;
    while ((ret = next_header_marker(src, size, &pos, &marker)) > 0) {
        if (marker.is_zero_dri)
            *has_zero_dri = 1;
    }
    return ret;
}

int linlon_mjpeg_remove_zero_dri(const uint8_t *src, size_t src_size, uint8_t *dst,
    size_t dst_capacity, size_t *dst_size) {
    int has_zero_dri;
    size_t pos = 2;
    size_t copied_from = 0;
    size_t written = 0;
    JpegMarker marker;
    int ret;

    if (!src || !dst || !dst_size || dst_capacity < src_size)
        return -1;
    if (scan_headers(src, src_size, &has_zero_dri) != 0 || !has_zero_dri) {
        memmove(dst, src, src_size);
        *dst_size = src_size;
        return 0;
    }

    while ((ret = next_header_marker(src, src_size, &pos, &marker)) > 0) {
        if (marker.is_zero_dri) {
            size_t chunk_size = marker.start - copied_from;
            memmove(dst + written, src + copied_from, chunk_size);
            written += chunk_size;
            copied_from = marker.end;
        }
    }
    if (ret < 0)
        return -1;

    memmove(dst + written, src + copied_from, src_size - copied_from);
    written += src_size - copied_from;
    *dst_size = written;
    return 0;
}
