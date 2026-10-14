/* Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd. MIT license: LICENSE.
 * Exercise actual input buffer recycling with scripted V4L2 ioctls. */
#include <stdarg.h>
#include <sys/ioctl.h>
static int test_ioctl(int fd, unsigned long request, ...);
#define ioctl test_ioctl
#include "../../al/vcodec/linlonv5v7/linlonv5v7_port.c"
#undef MODULE_TAG
#undef ENABLE_DEBUG
#include "../../al/vcodec/linlonv5v7/linlonv5v7_buffer.c"
#undef ioctl

#define REQUIRE(x)                                                                                                     \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                                       \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

static int dequeueError, queueError, queues;
static int test_ioctl(int fd, unsigned long request, ...) {
    va_list args;
    va_start(args, request);
    struct v4l2_buffer *buf = va_arg(args, void *);
    va_end(args);
    if (request == VIDIOC_S_MVX_ROI_REGIONS)
        return 0;
    if (request == VIDIOC_G_CROP) {
        memset(buf, 0, sizeof(struct v4l2_crop));
        return 0;
    }
    if (request == VIDIOC_DQBUF) {
        if (dequeueError) {
            errno = dequeueError;
            return -1;
        }
        *buf = (struct v4l2_buffer){
            .type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
            .memory = V4L2_MEMORY_MMAP,
            .length = 16,
        };
        return 0;
    }
    REQUIRE(request == VIDIOC_QBUF);
    ++queues;
    if (queueError) {
        errno = queueError;
        return -1;
    }
    REQUIRE(buf->bytesused == 3);
    return 0;
}

int main(void) {
    U8 storage[16] = {0};
    U8 data[] = {1, 2, 3};
    StreamBufferInfo stream = {.pu8Addr = data, .u32Size = sizeof(data), .u64PTS = 123};
    Buffer buffer = {
        .stBufArr = {.type = V4L2_BUF_TYPE_VIDEO_OUTPUT, .memory = V4L2_MEMORY_MMAP, .length = sizeof(storage)},
        .pUserPtr = {storage},
        .nMemType = V4L2_MEMORY_MMAP,
    };
    Port port = {
        .eBufType = V4L2_BUF_TYPE_VIDEO_OUTPUT,
        .nMemType = V4L2_MEMORY_MMAP,
        .ePortDirection = INPUT,
        .stBuf = {&buffer},
    };
    dequeueError = EAGAIN;
    REQUIRE(handleInputBuffer(&port, MPP_FALSE, &stream, MPP_STREAM_CODEC_H264) == MPP_DATAQUEUE_FULL);
    REQUIRE(queues == 0);
    dequeueError = EIO;
    REQUIRE(handleInputBuffer(&port, MPP_FALSE, &stream, MPP_STREAM_CODEC_H264) == MPP_IOCTL_FAILED);
    REQUIRE(queues == 0);
    dequeueError = 0;
    queueError = EIO;
    REQUIRE(handleInputBuffer(&port, MPP_FALSE, &stream, MPP_STREAM_CODEC_H264) == MPP_IOCTL_FAILED);
    REQUIRE(queues == 1);
    queueError = 0;
    REQUIRE(handleInputBuffer(&port, MPP_FALSE, &stream, MPP_STREAM_CODEC_H264) >= 0);
    REQUIRE(queues == 2 && memcmp(storage, data, sizeof(data)) == 0);
    puts("[PASS] empty/error DQBUF and failed/successful QBUF propagate "
        "correctly");
    return 0;
}
