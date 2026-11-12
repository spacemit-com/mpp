/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * Exercise real USERPTR plane setup and input submission without a device.
 */
#include "../../al/vcodec/linlonv5v7/linlonv5v7_buffer.c"
#undef MODULE_TAG
#include "../../al/vcodec/linlonv5v7/linlonv5v7_enc.c"

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
            exit(1); \
        } \
    } while (0)

static Buffer input_buffer;
static U8 pixels[3][8192];
static U8 next_pixels[3][8192];
static U32 queue_calls;
static U32 stop_calls;
static S32 queue_result;
static char fake_codec;
static char fake_port;

Port *getInputPort(Codec *codec) {
    REQUIRE(codec == (Codec *)&fake_codec);
    return (Port *)&fake_port;
}

S32 getBufNum(Port *port) {
    REQUIRE(port == (Port *)&fake_port);
    return 1;
}

Buffer *getBuffer(Port *port, S32 index) {
    REQUIRE(port == (Port *)&fake_port);
    REQUIRE(index == 0);
    return &input_buffer;
}

S32 queueBuffer(Port *port, Buffer *buffer) {
    REQUIRE(port == (Port *)&fake_port);
    REQUIRE(buffer == &input_buffer);
    queue_calls++;
    return queue_result;
}

void sendEncStopCommand(Port *port) {
    REQUIRE(port == (Port *)&fake_port);
    stop_calls++;
}

static void prepare_frame(ALLinlonv5v7EncContext *context, VideoFrameInfo *frame,
    MppPixelFormat format, U32 planes) {
    memset(&input_buffer, 0, sizeof(input_buffer));
    memset(frame, 0, sizeof(*frame));
    context->nInputMemType = V4L2_MEMORY_USERPTR;
    context->ePixelFormat = format;
    context->nInputNextIdx = 0;
    context->nInputQueuedCount = 0;
    context->bInputEos = MPP_FALSE;
    input_buffer.nMemType = V4L2_MEMORY_USERPTR;
    input_buffer.stBufArr.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    input_buffer.stBufArr.memory = V4L2_MEMORY_USERPTR;
    input_buffer.stBufArr.length = planes;
    input_buffer.stBufArr.m.planes = input_buffer.stBufPlanes;
    input_buffer.format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    input_buffer.format.fmt.pix_mp.num_planes = planes;
    frame->stCommFrameInfo.ePixelFormat = format;
    frame->stCommFrameInfo.u32Width = 64;
    frame->stCommFrameInfo.u32Height = 64;
    frame->stVFrame.u32PlaneNum = planes;
    frame->stVFrame.u64PTS = 1234567;
    frame->u32Idx = 7;
    for (U32 i = 0; i < planes; ++i) {
        U32 size = i == 0 ? 4096 : 2048;
        input_buffer.nPlaneLength[i] = size;
        input_buffer.stBufPlanes[i].length = size;
        input_buffer.format.fmt.pix_mp.plane_fmt[i].sizeimage = size;
        input_buffer.format.fmt.pix_mp.plane_fmt[i].bytesperline = 64;
        frame->stVFrame.ulPlaneVirAddr[i] = (UL)pixels[i];
        frame->stVFrame.u32PlaneSize[i] = size;
        frame->stVFrame.u32PlaneSizeValid[i] = size;
        frame->stVFrame.u32PlaneStride[i] = 64;
        frame->stVFrame.u32Fd[i] = (UL)-1;
    }
    queue_calls = 0;
    stop_calls = 0;
    queue_result = MPP_OK;
}

static void check_planes(ALLinlonv5v7EncContext *context, MppPixelFormat format, U32 planes) {
    VideoFrameInfo frame;
    prepare_frame(context, &frame, format, planes);
    REQUIRE(setupInputFrame(context, &input_buffer, &frame) == MPP_OK);
    REQUIRE(input_buffer.stBufArr.timestamp.tv_sec == 1);
    REQUIRE(input_buffer.stBufArr.timestamp.tv_usec == 234567);
    REQUIRE(getExtraId(&input_buffer) == 7);
    for (U32 i = 0; i < planes; ++i) {
        struct v4l2_plane *plane = &input_buffer.stBufPlanes[i];
        REQUIRE(getLength(&input_buffer, i) == (S32)frame.stVFrame.u32PlaneSize[i]);
        REQUIRE(plane->m.userptr == (uintptr_t)pixels[i]);
        REQUIRE(plane->length == frame.stVFrame.u32PlaneSize[i]);
        REQUIRE(plane->bytesused == frame.stVFrame.u32PlaneSizeValid[i]);
        REQUIRE(plane->data_offset == 0);
        REQUIRE(getUserPtr(&input_buffer, i) == pixels[i]);
        /* A recycled slot must not retain the old address or payload offset. */
        plane->data_offset = 99;
        plane->bytesused = 0;
        frame.stVFrame.ulPlaneVirAddr[i] = (UL)next_pixels[i];
    }
    REQUIRE(getLength(&input_buffer, planes) == 0);
    REQUIRE(setupInputFrame(context, &input_buffer, &frame) == MPP_OK);
    for (U32 i = 0; i < planes; ++i) {
        REQUIRE(input_buffer.stBufPlanes[i].m.userptr == (uintptr_t)next_pixels[i]);
        REQUIRE(input_buffer.stBufPlanes[i].data_offset == 0);
        REQUIRE(input_buffer.stBufPlanes[i].bytesused == frame.stVFrame.u32PlaneSizeValid[i]);
        REQUIRE(getUserPtr(&input_buffer, i) == next_pixels[i]);
    }
}

static void check_invalid_input(ALLinlonv5v7EncContext *context) {
    VideoFrameInfo frame;
    prepare_frame(context, &frame, MPP_PIXEL_FORMAT_NV12, 2);
    frame.stVFrame.u32PlaneNum = 1;
    REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) != MPP_OK);
    REQUIRE(queue_calls == 0);
    frame.stVFrame.u32PlaneNum = 4;
    REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) != MPP_OK);
    REQUIRE(queue_calls == 0);
    frame.stVFrame.u32PlaneNum = 2;
    for (U32 i = 0; i < 2; ++i) {
        frame.stVFrame.ulPlaneVirAddr[i] = 0;
        REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) != MPP_OK);
        REQUIRE(queue_calls == 0);
        frame.stVFrame.ulPlaneVirAddr[i] = (UL)pixels[i];
        frame.stVFrame.u32PlaneSize[i]--;
        REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) != MPP_OK);
        REQUIRE(queue_calls == 0);
        frame.stVFrame.u32PlaneSize[i]++;
        frame.stVFrame.u32PlaneStride[i]++;
        REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) != MPP_OK);
        REQUIRE(queue_calls == 0);
        frame.stVFrame.u32PlaneStride[i]--;
    }
    /* DQBUF metadata may shrink; the original QUERYBUF capacity still applies. */
    input_buffer.nPlaneLength[0] += 1024;
    input_buffer.stBufPlanes[0].length = 1024;
    REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) != MPP_OK);
    REQUIRE(queue_calls == 0);
    frame.stVFrame.u32PlaneSize[0] += 1024;
    /* Omitted stride uses the negotiated layout. Oversized storage is legal. */
    frame.stVFrame.u32PlaneStride[0] = 0;
    frame.stVFrame.u32PlaneSize[0] += 4096;
    REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) == MPP_OK);
    REQUIRE(queue_calls == 1);
}

static void check_submission(ALLinlonv5v7EncContext *context) {
    VideoFrameInfo frame;
    UL token = 0;
    prepare_frame(context, &frame, MPP_PIXEL_FORMAT_NV12, 2);
    frame.ulBufferId = 123;
    frame.stVFrame.u32FrameFlag = MPP_FRAME_FLAG_EOS;
    frame.stVFrame.ulPlaneVirAddr[0] = 0;
    REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) != MPP_OK);
    REQUIRE(!context->bInputEos);
    REQUIRE(queue_calls == 0);
    frame.stVFrame.ulPlaneVirAddr[0] = (UL)pixels[0];
    queue_result = MPP_IOCTL_FAILED;
    REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) == MPP_IOCTL_FAILED);
    REQUIRE(queue_calls == 1);
    REQUIRE(!context->bInputEos);
    frame.stVFrame.u32FrameFlag = 0;
    REQUIRE(!getIsQueued(&input_buffer));
    REQUIRE(context->nInputQueuedCount == 0);
    REQUIRE(!takeInputBufferId(&input_buffer, &token));
    queue_result = MPP_OK;
    REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) == MPP_OK);
    REQUIRE(queue_calls == 2);
    REQUIRE(getIsQueued(&input_buffer));
    REQUIRE(!(input_buffer.stBufArr.flags & V4L2_BUF_FLAG_LAST));
    REQUIRE(context->nInputQueuedCount == 1);
    REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) == MPP_CODER_NO_DATA);
    REQUIRE(queue_calls == 2);
    REQUIRE(takeInputBufferId(&input_buffer, &token));
    REQUIRE(token == 123);
    REQUIRE(!takeInputBufferId(&input_buffer, &token));

    /* Accepted EOS with pixels commits EOS state and marks the queued buffer. */
    prepare_frame(context, &frame, MPP_PIXEL_FORMAT_NV12, 2);
    frame.stVFrame.u32FrameFlag = MPP_FRAME_FLAG_EOS;
    REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) == MPP_OK);
    REQUIRE(queue_calls == 1 && context->bInputEos);
    REQUIRE(getIsQueued(&input_buffer));
    REQUIRE(input_buffer.stBufArr.flags & V4L2_BUF_FLAG_LAST);

    /* EOS without a payload must not require pointers or consume a slot. */
    prepare_frame(context, &frame, MPP_PIXEL_FORMAT_NV12, 2);
    memset(&frame, 0, sizeof(frame));
    frame.stVFrame.u32FrameFlag = MPP_FRAME_FLAG_EOS;
    REQUIRE(al_enc_send_input_frame((ALBaseContext *)context, &frame) == MPP_OK);
    REQUIRE(stop_calls == 1);
    REQUIRE(queue_calls == 0);
}

int main(void) {
    ALLinlonv5v7EncContext context;
    memset(&context, 0, sizeof(context));
    context.nVideoFd = -1;
    REQUIRE(pthread_mutex_init(&context.inputLock, NULL) == 0);
    REQUIRE(pthread_cond_init(&context.inputCond, NULL) == 0);
    context.stCodec = (Codec *)&fake_codec;
    check_planes(&context, MPP_PIXEL_FORMAT_NV12, 2);
    check_planes(&context, MPP_PIXEL_FORMAT_NV21, 2);
    check_planes(&context, MPP_PIXEL_FORMAT_I420, 3);
    check_planes(&context, MPP_PIXEL_FORMAT_YV12, 3);
    check_planes(&context, MPP_PIXEL_FORMAT_RGBA, 1);
    check_planes(&context, MPP_PIXEL_FORMAT_ARGB, 1);
    check_planes(&context, MPP_PIXEL_FORMAT_ABGR, 1);
    check_planes(&context, MPP_PIXEL_FORMAT_BGRA, 1);
    check_planes(&context, MPP_PIXEL_FORMAT_YUYV, 1);
    check_planes(&context, MPP_PIXEL_FORMAT_UYVY, 1);
    check_invalid_input(&context);
    check_submission(&context);
    pthread_cond_destroy(&context.inputCond);
    pthread_mutex_destroy(&context.inputLock);
    puts("PASS: USERPTR planes, slot reuse, capacity/stride checks, failed submission and EOS");
    return 0;
}
