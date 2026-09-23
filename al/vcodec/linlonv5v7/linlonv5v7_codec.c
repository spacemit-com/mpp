/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Author: David(qiang.fu@spacemit.com)
 * @Date: 2023-09-26 19:28:42
 * @LastEditTime: 2024-04-09 17:57:06
 * @Description:
 */

#define ENABLE_DEBUG 1

#include "linlonv5v7_codec.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MODULE_TAG "linlonv5v7_codec"

/***
 *
 * When we create a decode or encode channel, a codec session will be created in
 * the init function, a codec include two ports(input port and output port),
 * port is responsible for managing buffer.
 *
 *
 *                +--------------------------+
 *                |                          |
 *                |        CODEC             |
 *                |                          |
 *                +---+-----------------+----+
 *                    |                 |
 *                    |                 |
 *                    |                 |
 *   +----------------v-----+       +---v------------------+
 *   |                      |       |                      |
 *   |      INPUT PORT      |       |     OUTPUT PORT      |
 *   |                      |       |                      |
 *   +----------+-----------+       +-----------+----------+
 *              |                               |
 *              |                               |
 *              |                               |
 * +--------+---v----+--------+   +--------+----v---+--------+
 * | BUFFER | BUFFER | BUFFER |   | BUFFER | BUFFER | BUFFER |
 * +--------+--------+--------+   +--------+--------+--------+
 *
 */

struct _Codec {
    /***
     * parameters copyed from dec or enc
     */
    char sDevicePath[20];
    S32 nVideoFd;
    BOOL bIsBlockMode;
    S32 nWidth;
    S32 nHeight;
    S32 nAlign;
    BOOL bIsInterlaced;
    U32 nInputFormatFourcc;
    U32 nOutputFormatFourcc;
    U32 nInputMemtype;   // V4L2_MEMORY_MMAP/V4L2_MEMORY_USERPTR/V4L2_MEMORY_DMABUF
    U32 nOutputMemtype;  // V4L2_MEMORY_MMAP/V4L2_MEMORY_USERPTR/V4L2_MEMORY_DMABUF
    U32 nInputBufferNum;
    U32 nOutputBufferNum;

    /***
     * context of input port(managing input buffers)
     */
    Port *stInputPort;

    /***
     * context of output port(managing output buffers)
     */
    Port *stOutputPort;

    /***
     * parameters always used for encoder
     */
    BOOL bCsweo;
    U32 nFps;
    U32 nBps;
    U32 nMinqp;
    U32 nMaxqp;
    U32 nFixedqp;

    /***
     * only for frame, not used for packet
     */
    MppFrameBufferType eBufferType;
};

Codec *createCodec(
    S32 fd,
    S32 width,
    S32 height,
    S32 align,
    BOOL isInterlaced,
    enum v4l2_buf_type inputType,
    enum v4l2_buf_type outputType,
    U32 input_format_fourcc,
    U32 output_format_fourcc,
    U32 input_memtype,
    U32 output_memtype,
    U32 input_buffer_num,
    U32 output_buffer_num,
    BOOL block,
    MppFrameBufferType buffer_type
) {
    Codec *codec_tmp = (Codec *)malloc(sizeof(Codec));
    if (!codec_tmp) {
        error("can not malloc Codec, please check! (%s)", strerror(errno));
        return NULL;
    }
    memset(codec_tmp, 0, sizeof(Codec));

    debug(
        "create a codec, width=%d height=%d align=%d inputtype=%d outputtype=%d "
        "inputformat=%x outputformat=%x inputbufnum=%d outputbufnum=%d",
        width,
        height,
        align,
        inputType,
        outputType,
        input_format_fourcc,
        output_format_fourcc,
        input_buffer_num,
        output_buffer_num);

    codec_tmp->nVideoFd = fd;
    codec_tmp->bIsBlockMode = block;
    codec_tmp->nInputFormatFourcc = input_format_fourcc;
    codec_tmp->nOutputFormatFourcc = output_format_fourcc;
    codec_tmp->nInputMemtype = input_memtype;
    codec_tmp->nOutputMemtype = output_memtype;
    codec_tmp->nInputBufferNum = input_buffer_num;
    codec_tmp->nOutputBufferNum = output_buffer_num;
    codec_tmp->eBufferType = buffer_type;
    codec_tmp->stInputPort =
        createPort(fd, inputType, input_format_fourcc, align, input_memtype, input_buffer_num, buffer_type);
    if (!codec_tmp->stInputPort) {
        error("create input port failed, please check!");
        free(codec_tmp);
        return NULL;
    }
    codec_tmp->stOutputPort =
        createPort(fd, outputType, output_format_fourcc, align, output_memtype, output_buffer_num, buffer_type);
    if (!codec_tmp->stOutputPort) {
        error("create output port failed, please check!");
        destoryPort(codec_tmp->stInputPort);
        codec_tmp->stInputPort = NULL;
        free(codec_tmp);
        return NULL;
    }

    codec_tmp->nWidth = width;
    codec_tmp->nHeight = height;
    codec_tmp->nAlign = align;
    codec_tmp->bIsInterlaced = isInterlaced;
    codec_tmp->bCsweo = MPP_FALSE;
    codec_tmp->nFps = 0;
    codec_tmp->nBps = 0;
    codec_tmp->nMinqp = 0;
    codec_tmp->nMaxqp = 0;
    codec_tmp->nFixedqp = 0;

    return codec_tmp;
}

void destoryCodec(Codec *codec) {
    if (!codec)
        return;

    debug("destory input port");
    destoryPort(codec->stInputPort);
    debug("destory output port");
    destoryPort(codec->stOutputPort);
    debug("free codec");
    free(codec);
}

Port *getInputPort(Codec *codec) {
    return codec->stInputPort;
}

Port *getOutputPort(Codec *codec) {
    return codec->stOutputPort;
}

BOOL getCsweo(Codec *codec) {
    return codec->bCsweo;
}

U32 getFps(Codec *codec) {
    return codec->nFps;
}

U32 getBps(Codec *codec) {
    return codec->nBps;
}

U32 getMinqp(Codec *codec) {
    return codec->nMinqp;
}

U32 getMaxqp(Codec *codec) {
    return codec->nMaxqp;
}

U32 getFixedqp(Codec *codec) {
    return codec->nFixedqp;
}

void setCsweo(Codec *codec, BOOL csweo) {
    codec->bCsweo = csweo;
}

void setFps(Codec *codec, U32 fps) {
    codec->nFps = fps;
}

void setBps(Codec *codec, U32 bps) {
    codec->nBps = bps;
}

void setMinqp(Codec *codec, U32 minqp) {
    codec->nMinqp = minqp;
}

void setMaxqp(Codec *codec, U32 maxqp) {
    codec->nMaxqp = maxqp;
}

void setFixedqp(Codec *codec, U32 fixedqp) {
    codec->nFixedqp = fixedqp;
}

S32 stream(Codec *codec) {
    /* Set NALU. */
    // setNALU(codec->stInputPort, NALU_FORMAT_START_CODES);
    if (isVPx(getFormatFourcc(codec->stInputPort))) {
        setNALU(codec->stInputPort, NALU_FORMAT_ONE_NALU_PER_BUFFER);
    }

    if (getFormatFourcc(codec->stInputPort) == V4L2_PIX_FMT_VC1_ANNEX_L) {
        setNALU(codec->stInputPort, NALU_FORMAT_ONE_NALU_PER_BUFFER);
    }
    /*
        if (input.io->getNaluFormat() == NALU_FORMAT_ONE_NALU_PER_BUFFER
                || input.io->getNaluFormat() == NALU_FORMAT_ONE_BYTE_LENGTH_FIELD
                || input.io->getNaluFormat() == NALU_FORMAT_TWO_BYTE_LENGTH_FIELD
                || input.io->getNaluFormat() ==
       NALU_FORMAT_FOUR_BYTE_LENGTH_FIELD){
            input.setNALU((NaluFormat)input.io->getNaluFormat());
        }
    */
    if ((getFormatFourcc(codec->stInputPort) == V4L2_PIX_FMT_VC1_ANNEX_L) ||
        (getFormatFourcc(codec->stInputPort) == V4L2_PIX_FMT_VC1_ANNEX_G)) {
        struct v4l2_control control;
        S32 profile = 0xff;
        /*
                switch (input.io->getProfile())
                {
                    case 0:
                    {
                        profile = 0;
                        break;
                    }
                    case 4:
                    {
                        profile = 1;
                        break;
                    }
                    case 12:
                    {
                        profile = 2;
                        break;
                    }
                    default:
                    {
                        throw Exception("Unsupported VC1 profile.");
                    }
                }
        */
        memset(&control, 0, sizeof(control));

        control.id = V4L2_CID_MVE_VIDEO_VC1_PROFILE;
        control.value = 2;  // profile;

        if (-1 == ioctl(codec->nVideoFd, VIDIOC_S_CTRL, &control)) {
            error("Failed to set profile=%u for fmt: %u .", profile, getFormatFourcc(codec->stInputPort));
        }
    }

    /* Add VPx file header. */
    if (isVPx(getFormatFourcc(codec->stOutputPort))) {
        setNALU(codec->stOutputPort, NALU_FORMAT_ONE_NALU_PER_BUFFER);
    }

    queryCapabilities(codec);
    /* enumerateFormats(); */
    enumerateFramesizes(codec, getFormatFourcc(codec->stOutputPort));
    setFormats(codec);
    subscribeEvents(codec);

    /* Propagate buffer-allocation failure (e.g. CMA exhausted) instead of
     * swallowing it. Without this, a codec would come up with zero buffers and
     * drop every frame forever, and init would still report success. */
    S32 ret = allocateCodecBuffers(codec);
    if (ret != MPP_OK) {
        error("stream: buffer allocation failed, ret=%d", ret);
        freeCodecBuffers(codec);
        return ret;
    }

    queueCodecBuffers(codec, MPP_FALSE);
    streamonCodec(codec);

    return MPP_OK;
}

BOOL isVPx(U32 format) {
    return format == V4L2_PIX_FMT_VP8 || format == V4L2_PIX_FMT_VP9;
}

BOOL isAFBC(U32 format) {
    switch (format) {
        case V4L2_PIX_FMT_YUV420_AFBC_8:
        case V4L2_PIX_FMT_YUV420_AFBC_10:
        case V4L2_PIX_FMT_YUV422_AFBC_8:
        case V4L2_PIX_FMT_YUV422_AFBC_10:
            return MPP_TRUE;
        default:
            return MPP_FALSE;
    }
}

void enumerateCodecFormats(Codec *codec) {
    enumerateFormats(codec->stInputPort);
    enumerateFormats(codec->stOutputPort);
}

void openDev(Codec *codec) {
    S32 flags = O_RDWR;

    if (!codec->bIsBlockMode) {
        flags |= O_NONBLOCK;
    }

    /* Open the video device in read/write mode. */
    codec->nVideoFd = open(codec->sDevicePath, flags);
    if (codec->nVideoFd < 0) {
        error("Failed to open device.");
    }
}

void closeDev(Codec *codec) {
    close(codec->nVideoFd);
    codec->nVideoFd = -1;
}

void queryCapabilities(Codec *codec) {
    struct v4l2_capability cap;
    S32 ret;

    /* Query capabilities. */
    ret = ioctl(codec->nVideoFd, VIDIOC_QUERYCAP, &cap);
    if (ret != 0) {
        error("Failed to query for capabilities");
    }

    if (0 == (cap.capabilities & (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE))) {
        error("Device is missing m2m support.");
    }
}

void enumerateFramesizes(Codec *codec, U32 format) {
    struct v4l2_frmsizeenum frmsize;

    frmsize.index = 0;
    frmsize.pixel_format = format;

    S32 ret = ioctl(codec->nVideoFd, VIDIOC_ENUM_FRAMESIZES, &frmsize);
    if (ret != 0) {
        error(
            "Failed to enumerate frame sizes. fd=%d format=%d ret=%d error=%s",
            codec->nVideoFd,
            format,
            ret,
            strerror(errno));
    }

    // debug("Enumerate frame size. index=%d pixel_format=%x", frmsize.index,
    //       frmsize.pixel_format);

    switch (frmsize.type) {
        case V4L2_FRMIVAL_TYPE_DISCRETE:
            break;
        case V4L2_FRMIVAL_TYPE_CONTINUOUS:
        case V4L2_FRMIVAL_TYPE_STEPWISE:
            // debug(
            //     "min_width=%d max_width=%d step_width=%d  min_height=%d "
            //     "max_height=%d step_height=%d",
            //     frmsize.stepwise.min_width, frmsize.stepwise.max_width,
            //     frmsize.stepwise.step_width, frmsize.stepwise.min_height,
            //     frmsize.stepwise.max_height, frmsize.stepwise.step_height);
            break;
        default:
            error("Unsupported enumerate frame size type. type=%d", frmsize.type);
    }
}

void setFormats(Codec *codec) {
    getTrySetFormat(
        codec->stInputPort, codec->nWidth, codec->nHeight, getFormatFourcc(codec->stInputPort), codec->bIsInterlaced);
    getTrySetFormat(
        codec->stOutputPort, codec->nWidth, codec->nHeight, getFormatFourcc(codec->stOutputPort), codec->bIsInterlaced);
}

struct v4l2_mvx_color_desc getColorDesc(Codec *codec) {
    struct v4l2_mvx_color_desc color;
    S32 ret = ioctl(codec->nVideoFd, VIDIOC_G_MVX_COLORDESC, &color);
    if (ret) {
        error("Failed to get color description!");
    }
    return color;
}

void subscribeEvents(Codec *codec) {
    subscribeEvent(codec, V4L2_EVENT_EOS);
    subscribeEvent(codec, V4L2_EVENT_SOURCE_CHANGE);
    subscribeEvent(codec, V4L2_EVENT_MVX_COLOR_DESC);
}

void subscribeEvent(Codec *codec, U32 event) {
    struct v4l2_event_subscription sub = {.type = event, .id = 0};
    S32 ret;

    ret = ioctl(codec->nVideoFd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    if (ret != 0) {
        error("Failed to subscribe for event.");
    }
}

void unsubscribeEvents(Codec *codec) {
    unsubscribeEvent(codec, V4L2_EVENT_ALL);
}

void unsubscribeEvent(Codec *codec, U32 event) {
    struct v4l2_event_subscription sub;
    S32 ret;

    sub.type = event;
    ret = ioctl(codec->nVideoFd, VIDIOC_UNSUBSCRIBE_EVENT, &sub);
    if (ret != 0) {
        error("Failed to unsubscribe for event.");
    }
}

S32 allocateCodecBuffers(Codec *codec) {
    S32 retIn = allocateBuffers(codec->stInputPort, codec->nInputBufferNum);
    if (retIn != MPP_OK) {
        error("allocateCodecBuffers: input allocation failed (ret=%d nBuf=%d requested=%u)",
            retIn, getBufNum(codec->stInputPort), codec->nInputBufferNum);
        return retIn;
    }

    S32 retOut = allocateBuffers(codec->stOutputPort, codec->nOutputBufferNum);

    /* A partial allocation (CMA exhausted) leaves nBufNum below the request, or
     * even 0. Treat that as failure: a codec with too few input/output buffers
     * either drops every frame (numBufs==0) or badly under-pipelines. Callers
     * must be able to fail init and retry/clean up rather than silently run a
     * zombie channel. */
    S32 inBufNum = getBufNum(codec->stInputPort);
    S32 outBufNum = getBufNum(codec->stOutputPort);
    if (retOut != MPP_OK) {
        error("allocateCodecBuffers failed (in ret=%d nBuf=%d, out ret=%d nBuf=%d)",
            retIn, inBufNum, retOut, outBufNum);
        return retOut;
    }
    if (inBufNum != (S32)codec->nInputBufferNum || outBufNum != (S32)codec->nOutputBufferNum) {
        error("allocateCodecBuffers: incomplete buffers (in %d/%u, out %d/%u)",
            inBufNum, codec->nInputBufferNum, outBufNum, codec->nOutputBufferNum);
        return MPP_INIT_FAILED;
    }
    return MPP_OK;
}

void freeCodecBuffers(Codec *codec) {
    freeBuffers(codec->stInputPort);
    freeBuffers(codec->stOutputPort);
}

void queueCodecBuffers(Codec *codec, BOOL eof) {
    // only queue output buffer, input buffer is not filled yet
    queueBuffers(codec->stOutputPort, eof);
    // queueBuffers(codec->stInputPort, eof);
}

void streamonCodec(Codec *codec) {
    streamon(codec->stInputPort);
    streamon(codec->stOutputPort);
}

void streamoffCodec(Codec *codec) {
    streamoff(codec->stInputPort);
    streamoff(codec->stOutputPort);
}

S32 handleEvent(Codec *codec) {
    /* Upper bound on events drained in a single call. The V4L2 event queue is
     * small, so this is never reached in normal operation; it only prevents an
     * abnormal, never-ending event stream from blocking the poll thread. */
#define MAX_DQEVENT_PER_DRAIN 64
    /* Upper bound on consecutive EINTR retries. A signal interrupting the
     * ioctl is transient and normally clears on the first retry; bounding it
     * guarantees the loop terminates even under a relentless signal storm. */
#define MAX_DQEVENT_EINTR_RETRY 16
    struct v4l2_event event;
    S32 ret;
    S32 eventCount = 0;
    S32 eintrRetry = 0;

    /* Drain ALL pending events to prevent event queue overflow.
     * V4L2 event queue has limited capacity; if not drained in time,
     * subsequent poll() may return POLLERR.
     * A hard upper bound guards against an unexpectedly endless stream of
     * events (e.g. a misbehaving driver or repeated EINTR) so this loop can
     * never block the poll thread indefinitely. */
    while (eventCount < MAX_DQEVENT_PER_DRAIN) {
        /*
         * Guard the dequeue with a zero-timeout poll. The device fd may have
         * been opened in BLOCKING mode (e.g. find_v4l2_decoder() opens it
         * without O_NONBLOCK), and on such an fd VIDIOC_DQEVENT blocks
         * indefinitely (wait_event_interruptible) once the event queue is
         * empty. That would pin this thread inside the ioctl forever, so the
         * poll thread can never observe bIsDestoryed and pthread_join() during
         * teardown would deadlock. Only dequeue while POLLPRI confirms a
         * pending event; break out as soon as the queue drains.
         */
        struct pollfd pe = {.fd = codec->nVideoFd, .events = POLLPRI};
        if (poll(&pe, 1, 0) <= 0 || !(pe.revents & POLLPRI)) {
            break; /* no more pending events */
        }
    ret = ioctl(codec->nVideoFd, VIDIOC_DQEVENT, &event);
    if (ret != 0) {
            /*
             * A signal can interrupt the ioctl (EINTR) even though events are
             * still queued; retry in that case instead of mistaking it for an
             * empty queue. The retry count is bounded separately so a
             * relentless signal storm can never spin this loop forever.
             */
            if (errno == EINTR && eintrRetry < MAX_DQEVENT_EINTR_RETRY) {
                eintrRetry++;
                continue;
            }
            if (eventCount == 0) {
        error("Failed to dequeue event, please check!");
        return MPP_IOCTL_FAILED;
    }
            break; /* No more pending events */
        }
        eintrRetry = 0; /* a successful dequeue resets the retry budget */
        eventCount++;

    if (event.type == V4L2_EVENT_MVX_COLOR_DESC) {
        // struct v4l2_mvx_color_desc color = getColorDesc(codec);
        // printColorDesc(color);
        error("V4L2_EVENT_MVX_COLOR_DESC event is not support yet, please check!");
    }

    if (event.type == V4L2_EVENT_SOURCE_CHANGE && (event.u.src_change.changes & V4L2_EVENT_SRC_CH_RESOLUTION)) {
        debug("get V4L2_EVENT_SOURCE_CHANGE event, do notify!");
        notifySourceChange(codec->stOutputPort);
    }

    if (event.type == V4L2_EVENT_EOS) {
            debug("V4L2_EVENT_EOS event received");
        }
    }

    return MPP_OK;
}

void handleFlush(Codec *codec, BOOL eof) {
    streamoff(codec->stInputPort);
    streamoff(codec->stOutputPort);

    /* For DMABUF_EXTERNAL mode: DO NOT reallocate buffers here!
     * The buffers are owned by MPI layer (VB pool), and reallocation
     * would cause MPI's stExtBuf[] to become out of sync with V4L2.
     * Just do streamoff/streamon, and let MPI layer re-queue buffers. */

    streamon(codec->stInputPort);
    // this sleep is used to fix a bug, ffplay on linux sometimes get a streamon
    // failed(Operation now in progress) error
    usleep(5000);
    streamon(codec->stOutputPort);

    /* Only queue internal buffers; external buffers queued by MPI layer */
    if (getPortBufferType(codec->stOutputPort) != MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL) {
    queueBuffers(codec->stOutputPort, MPP_FALSE);
    }
    // port->nFramesProcessed = 0;
}

S32 runPoll(Codec *codec, struct pollfd *p) {
    // debug("input:%d output:%d", codec->stInputPort->pending,
    //       codec->stOutputPort->pending);
    /*if (codec->stInputPort->pending > 0)
    {
        p->events |= POLLOUT;
    }

    if (codec->stOutputPort->pending > 0)
    {
        p->events |= POLLIN;
    }*/

    S32 ret = poll(p, 1, POLL_TIMEOUT);
    // debug("poll ret = %d p->revents=%x", ret, p->revents);

    if (ret < 0) {
        error("Poll returned error code.");
        return MPP_POLL_FAILED;
    }

    if (p->revents & POLLERR) {
        error("Poll returned error event.");
        return MPP_POLL_FAILED;
    }

    if (0 == ret) {
        // error("Queue and dequeue poll timed out.");
        return MPP_POLL_FAILED;
    }

    return MPP_OK;
}
