/* Copyright 2026 SPACEMIT. All rights reserved. BSD-style license: LICENSE.
 * Check actual getTrySetFormat requests with scripted ioctl; no hardware. */
#include <stdarg.h>
#include <sys/ioctl.h>
static int test_ioctl(int fd, unsigned long request, ...);
#define ioctl test_ioctl
#include "../../al/vcodec/linlonv5v7/linlonv5v7_port.c"
#undef ioctl

#define REQUIRE(x)                                               \
    do {                                                         \
        if (!(x)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); \
            exit(1);                                             \
        }                                                        \
    } while (0)

static U32 expected_fourcc, expected_width, expected_height, expected_planes;
static U32 expected_stride[3], expected_size[3];
static U32 calls, driver_padding;

static int test_ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    struct v4l2_format* fmt = va_arg(args, struct v4l2_format*);
    va_end(args);
    REQUIRE(fd == 17);
    REQUIRE(fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
    static const unsigned long sequence[] = {VIDIOC_G_FMT, VIDIOC_TRY_FMT, VIDIOC_S_FMT};
    REQUIRE(calls < 3 && request == sequence[calls]);
    ++calls;
    if (request == VIDIOC_G_FMT) return 0; /* Seeded stale format, intentionally. */
    struct v4l2_pix_format_mplane* f = &fmt->fmt.pix_mp;
    REQUIRE(f->pixelformat == expected_fourcc);
    REQUIRE(f->width == expected_width && f->height == expected_height);
    REQUIRE(f->num_planes == expected_planes);
    for (U32 i = 0; i < 3; ++i) {
        REQUIRE(f->plane_fmt[i].bytesperline == expected_stride[i]);
        REQUIRE(f->plane_fmt[i].sizeimage == expected_size[i]);
    }
    if (request == VIDIOC_S_FMT && driver_padding) {
        f->plane_fmt[0].bytesperline += driver_padding;
        f->plane_fmt[0].sizeimage = f->plane_fmt[0].bytesperline * expected_height;
    }
    return 0;
}

static void check_layout(U32 fourcc, U32 width, U32 height, U32 bytes_per_pixel, BOOL nv12, U32 padding) {
    Port port = {0};
    port.nVideoFd = 17;
    port.eBufType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    port.nAlign = 16;
    port.stFormat.fmt.pix_mp.num_planes = 3;
    /* A previous planar format must not leak its plane layout into packed RGB. */
    for (U32 i = 0; i < 3; ++i) {
        port.stFormat.fmt.pix_mp.plane_fmt[i].bytesperline = 123;
        port.stFormat.fmt.pix_mp.plane_fmt[i].sizeimage = 456;
    }
    expected_fourcc = fourcc;
    expected_width = width;
    expected_height = height;
    expected_planes = nv12 ? 2 : 1;
    /* Deliberately independent of ST_ALIGN_UP in the implementation. */
    expected_stride[0] = ((width * bytes_per_pixel + 15) / 16) * 16;
    expected_stride[1] = nv12 ? expected_stride[0] : 0;
    expected_stride[2] = 0;
    expected_size[0] = expected_stride[0] * height;
    expected_size[1] = nv12 ? expected_size[0] / 2 : 0;
    expected_size[2] = 0;
    calls = 0;
    driver_padding = padding;
    getTrySetFormat(&port, width, height, fourcc, 0);
    REQUIRE(calls == 3);
    REQUIRE(port.stFormat.fmt.pix_mp.num_planes == expected_planes);
    REQUIRE(port.stFormat.fmt.pix_mp.plane_fmt[0].bytesperline == expected_stride[0] + padding);
    REQUIRE(port.stFormat.fmt.pix_mp.plane_fmt[0].sizeimage == (expected_stride[0] + padding) * height);
}

int main(void) {
    const U32 formats[] = {V4L2_PIX_FMT_RGBA32, V4L2_PIX_FMT_ARGB32, V4L2_PIX_FMT_BGRA32, V4L2_PIX_FMT_ABGR32};
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        check_layout(formats[i], 1920, 1080, 4, 0, 0);
        check_layout(formats[i], 1918, 1079, 4, 0, 0);
        check_layout(formats[i], 640, 480, 4, 0, 64);
    }
    check_layout(V4L2_PIX_FMT_NV12, 640, 480, 1, 1, 0);
    check_layout(V4L2_PIX_FMT_YUYV, 640, 480, 2, 0, 0);
    puts("PASS: RGB plane count, aligned stride, size, stale planes and driver padding; YUV unchanged");
    return 0;
}
