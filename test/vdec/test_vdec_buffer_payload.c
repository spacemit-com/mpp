/* Copyright 2026 SPACEMIT. All rights reserved. BSD-style license: LICENSE. */
#include <stdio.h>
#include <stdlib.h>

#include "linlonv5v7_buffer.h"

#define REQUIRE(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); abort(); } } while (0)

int main(void) {
    struct v4l2_plane planes[2] = {{0}, {.data_offset = 921600, .bytesused = 921600}};
    struct v4l2_buffer buf = {
        .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, .length = 2, .m.planes = planes,
    };
    /* Actual empty CAPTURE marker observed during 1280x720 -> 640x360. */
    REQUIRE(getBytesUsed(&buf) == 0);
    planes[0].bytesused = 921600;
    planes[1].bytesused = 1382400;
    REQUIRE(getBytesUsed(&buf) == 1382400);
    planes[0].bytesused = 0;
    planes[1].bytesused = 0; /* clearBytesUsed leaves the offset intact */
    REQUIRE(getBytesUsed(&buf) == 0);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.bytesused = 1234;
    REQUIRE(getBytesUsed(&buf) == 1234);
    puts("PASS: empty CAPTURE markers and multi-plane payload sizes");
    return 0;
}
