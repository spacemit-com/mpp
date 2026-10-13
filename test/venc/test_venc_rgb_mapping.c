/* Copyright 2026 SPACEMIT. All rights reserved. BSD-style license: LICENSE.
 * Exercise the encoder's real mapping table without opening any device. */
#include "../../al/vcodec/linlonv5v7/linlonv5v7_enc.c"

static int check_mapping(MppPixelFormat mpp, U32 fourcc) {
    if ((U32)get_linlonv5v7enc_codec_pixel_format(mpp) != fourcc ||
        get_linlonv5v7enc_mpp_pixel_format((S32)fourcc) != mpp) {
        fprintf(stderr, "FAIL: mapping format=%d fourcc=0x%x\n", mpp, fourcc);
        return 1;
    }
    return 0;
}

int main(void) {
    int failures = 0;
    /* Linux RGB format byte-order table, not a same-name round trip:
     * https://docs.kernel.org/userspace-api/media/v4l/pixfmt-rgb.html */
    failures += check_mapping(MPP_PIXEL_FORMAT_BGRA, V4L2_PIX_FMT_ABGR32);
    failures += check_mapping(MPP_PIXEL_FORMAT_ABGR, V4L2_PIX_FMT_BGRA32);
    failures += check_mapping(MPP_PIXEL_FORMAT_RGBA, V4L2_PIX_FMT_RGBA32);
    failures += check_mapping(MPP_PIXEL_FORMAT_ARGB, V4L2_PIX_FMT_ARGB32);
    failures += check_mapping(MPP_PIXEL_FORMAT_NV12, V4L2_PIX_FMT_NV12);
    failures += check_mapping(MPP_PIXEL_FORMAT_I420, V4L2_PIX_FMT_YUV420M);
    if (failures) return 1;
    puts("PASS: all four packed RGB byte orders and unchanged YUV mappings");
    return 0;
}
