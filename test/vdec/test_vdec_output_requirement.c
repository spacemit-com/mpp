/* Copyright 2026 SPACEMIT. All rights reserved. BSD-style license: LICENSE.
 * Script REQBUFS/QUERYBUF; run actual allocation and output-layout reporting. */
#include <stdarg.h>
#include <sys/ioctl.h>
static int test_ioctl(int fd, unsigned long request, ...);
#define ioctl test_ioctl
#include "../../al/vcodec/linlonv5v7/linlonv5v7_port.c"
#undef ioctl

#define REQUIRE(x)                                                                                                     \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                                       \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)
struct _Buffer {
    struct v4l2_buffer v4l;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
};
static U32 granted, live, freed;
static int failQuery = -1;
static int test_ioctl(int fd, unsigned long request, ...) {
    (void)fd;
    va_list args;
    va_start(args, request);
    void *arg = va_arg(args, void *);
    va_end(args);
    if (request == VIDIOC_REQBUFS) {
        struct v4l2_requestbuffers *req = arg;
        if (req->count)
            req->count = granted;
        return 0;
    }
    REQUIRE(request == VIDIOC_QUERYBUF);
    struct v4l2_buffer *buf = arg;
    if ((int)buf->index == failQuery)
        return -1;
    buf->length = 2;
    buf->m.planes[0].length = 4096 + buf->index * 64;
    buf->m.planes[1].length = 2048;
    return 0;
}
Buffer *createBuffer(struct v4l2_buffer v4l, S32 fd, struct v4l2_format format, MppFrameBufferType type) {
    (void)fd;
    (void)format;
    (void)type;
    Buffer *buf = calloc(1, sizeof(*buf));
    REQUIRE(buf);
    buf->v4l = v4l;
    memcpy(buf->planes, v4l.m.planes, v4l.length * sizeof(*buf->planes));
    buf->v4l.m.planes = buf->planes;
    ++live;
    return buf;
}
void destoryBuffer(Buffer *buf) {
    REQUIRE(buf && live);
    --live;
    ++freed;
    free(buf);
}
struct v4l2_buffer *getV4l2Buffer(Buffer *buf) { return &buf->v4l; }
S32 getBytesUsed(struct v4l2_buffer *buf) {
    S32 count = 0;
    for (U32 i = 0; i < buf->length; ++i)
        count += buf->m.planes[i].bytesused;
    return count;
}
int main(void) {
    Port port = {0};
    port.eBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    port.nMemType = V4L2_MEMORY_DMABUF;
    port.stFormat.fmt.pix_mp = (struct v4l2_pix_format_mplane){
        .width = 64,
        .height = 64,
        .pixelformat = V4L2_PIX_FMT_NV12M,
        .num_planes = 2,
        .plane_fmt = {{.sizeimage = 4096, .bytesperline = 64}, {.sizeimage = 2048, .bytesperline = 64}},
    };
    AlDecOutputRequirement req;
    granted = 3;
    REQUIRE(allocateBuffers(&port, 12) == MPP_OK);
    REQUIRE(getBufNum(&port) == 3 && live == 3);
    REQUIRE(getOutputRequirement(&port, &req) == MPP_OK);
    REQUIRE(req.u32BufCnt == 3 && req.u32PlaneNum == 2);
    REQUIRE(req.au32Size[0] == 4224 && req.au32Size[1] == 2048);
    REQUIRE(req.au32Stride[0] == 64 && req.ePixelFormat == MPP_PIXEL_FORMAT_NV12);
    granted = 5;
    REQUIRE(allocateBuffers(&port, 2) == MPP_OK);
    REQUIRE(getBufNum(&port) == 5 && live == 5 && freed == 3);
    REQUIRE(getOutputRequirement(&port, &req) == MPP_OK && req.u32BufCnt == 5);
    granted = MAX_BUF_NUM + 1;
    REQUIRE(allocateBuffers(&port, 12) == MPP_CHECK_FAILED);
    REQUIRE(getBufNum(&port) == 0 && live == 0);
    granted = 4;
    failQuery = 2;
    REQUIRE(allocateBuffers(&port, 4) == MPP_IOCTL_FAILED);
    REQUIRE(live == 2 && getOutputRequirement(&port, &req) == MPP_CHECK_FAILED);
    freeBuffers(&port);
    freeBuffers(&port);
    REQUIRE(live == 0 && getBufNum(&port) == 0);
    puts("PASS: actual REQBUFS counts, padded plane sizes and partial allocation cleanup");
    return 0;
}
