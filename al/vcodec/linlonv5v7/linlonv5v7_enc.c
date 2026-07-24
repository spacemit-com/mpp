/*
 * Copyright 2022-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: video encode plugin for V4L2 codec standard interface.
 *               Consumes MPI public structs directly (VencChnAttr /
 *               VencChnStatus / VideoFrameInfo / StreamBufferInfo / VencCmd).
 */

#define ENABLE_DEBUG 1

#include <errno.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "al_interface_enc.h"
#include "linlonv5v7_codec.h"
#include "log.h"
#include "mvx-v4l2-controls.h"
#include "v4l2_utils.h"

#define MODULE_TAG "linlonv5v7_enc"

CODING_TYPE_MAPPING_DEFINE(Linlonv5v7Enc, S32)
static const ALLinlonv5v7EncCodingTypeMapping stALLinlonv5v7EncCodingTypeMapping[] = {
    {MPP_STREAM_CODEC_H263, V4L2_PIX_FMT_H263},
    {MPP_STREAM_CODEC_H264, V4L2_PIX_FMT_H264},
    {MPP_STREAM_CODEC_H264_MVC, V4L2_PIX_FMT_H264_MVC},
    {MPP_STREAM_CODEC_H264_NO_SC, V4L2_PIX_FMT_H264_NO_SC},
    {MPP_STREAM_CODEC_H265, V4L2_PIX_FMT_HEVC},
    {MPP_STREAM_CODEC_MJPEG, V4L2_PIX_FMT_JPEG},
    {MPP_STREAM_CODEC_JPEG, V4L2_PIX_FMT_JPEG},
    {MPP_STREAM_CODEC_VP8, V4L2_PIX_FMT_VP8},
    {MPP_STREAM_CODEC_VP9, V4L2_PIX_FMT_VP9},
    {MPP_STREAM_CODEC_AVS, V4L2_PIX_FMT_AVS},
    {MPP_STREAM_CODEC_AVS2, V4L2_PIX_FMT_AVS2},
    {MPP_STREAM_CODEC_MPEG1, V4L2_PIX_FMT_MPEG},
    {MPP_STREAM_CODEC_MPEG2, V4L2_PIX_FMT_MPEG2},
    {MPP_STREAM_CODEC_MPEG4, V4L2_PIX_FMT_MPEG4},
    {MPP_STREAM_CODEC_RV, V4L2_PIX_FMT_RV},
    {MPP_STREAM_CODEC_VC1, V4L2_PIX_FMT_VC1_ANNEX_G},
    {MPP_STREAM_CODEC_VC1_ANNEX_L, V4L2_PIX_FMT_VC1_ANNEX_L},
    {MPP_STREAM_CODEC_FWHT, V4L2_PIX_FMT_FWHT},
};
CODING_TYPE_MAPPING_CONVERT(Linlonv5v7Enc, linlonv5v7enc, S32)

PIXEL_FORMAT_MAPPING_DEFINE(Linlonv5v7Enc, S32)
static const ALLinlonv5v7EncPixelFormatMapping stALLinlonv5v7EncPixelFormatMapping[] = {
    {MPP_PIXEL_FORMAT_I420, V4L2_PIX_FMT_YUV420M},
    {MPP_PIXEL_FORMAT_NV12, V4L2_PIX_FMT_NV12},
    {MPP_PIXEL_FORMAT_NV21, V4L2_PIX_FMT_NV21},
    {MPP_PIXEL_FORMAT_YV12, V4L2_PIX_FMT_YVU420M},
    {MPP_PIXEL_FORMAT_UYVY, V4L2_PIX_FMT_UYVY},
    {MPP_PIXEL_FORMAT_YUYV, V4L2_PIX_FMT_YUYV},
    {MPP_PIXEL_FORMAT_AFBC_YUV420_8, V4L2_PIX_FMT_YUV420_AFBC_8},
    {MPP_PIXEL_FORMAT_AFBC_YUV420_10, V4L2_PIX_FMT_YUV420_AFBC_10},
    {MPP_PIXEL_FORMAT_AFBC_YUV422_8, V4L2_PIX_FMT_YUV422_AFBC_8},
    {MPP_PIXEL_FORMAT_AFBC_YUV422_10, V4L2_PIX_FMT_YUV422_AFBC_10},
    {MPP_PIXEL_FORMAT_RGBA, V4L2_PIX_FMT_RGBA32},
    {MPP_PIXEL_FORMAT_ARGB, V4L2_PIX_FMT_ARGB32},
    {MPP_PIXEL_FORMAT_ABGR, V4L2_PIX_FMT_ABGR32},
    {MPP_PIXEL_FORMAT_BGRA, V4L2_PIX_FMT_BGRA32},
};
PIXEL_FORMAT_MAPPING_CONVERT(Linlonv5v7Enc, linlonv5v7enc, S32)

typedef struct _ALLinlonv5v7EncContext ALLinlonv5v7EncContext;

struct _ALLinlonv5v7EncContext {
    ALEncBaseContext stAlEncBaseContext;
    VencChnAttr stAttr;             // channel attributes (copied at init)
    MppPixelFormat ePixelFormat;    // input frame format
    MppStreamCodecType eCodecType;  // output stream format

    Codec *stCodec;

    /***
     * for open video device, such as /dev/video0
     */
    U8 sDevicePath[20];
    S32 nVideoFd;

    /***
     * enum v4l2_buf_type
     * nInputType: always V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE
     * nOutputType: always V4L2_BUF_TYPE_VIDEO_CAPTURE
     */
    U32 nInputType;
    U32 nOutputType;

    /***
     * nInputFormatFourcc: V4L2_PIX_FMT_NV12, etc.
     * nOutputFormatFourcc: V4L2_PIX_FMT_H264, etc.
     */
    U32 nInputFormatFourcc;
    U32 nOutputFormatFourcc;

    /***
     * enum v4l2_memory
     * nInputMemType: always V4L2_MEMORY_DMABUF
     * nOutputMemType: always V4L2_MEMORY_MMAP
     */
    U32 nInputMemType;
    U32 nOutputMemType;

    /***
     * MPP_FALSE, meaning that open device node with O_NONBLOCK
     */
    BOOL bIsBlockMode;
    BOOL bIsInterlaced;

    /***
     * video width and height
     * 0x0 is not supported, because MPP do not know the size of YUV.
     */
    S32 nWidth;
    S32 nHeight;
    S32 nAlign;

    S32 nRotation;

    /***
     * EOS flag, default MPP_FALSE
     * when a frame with eos flag comes, bInputEos is set to MPP_TRUE.
     */
    BOOL bInputEos;
    BOOL bOutputEos;

    U32 nInputNextIdx;
    U32 nInputQueuedCount;
    U32 nInputCallbacksInFlight;

    AlEncCallbacks stCallbacks;
    pthread_t inputDoneTid;
    pthread_mutex_t inputLock;
    pthread_cond_t inputCond;
    S32 nInputWakeFd;
    BOOL bInputDoneRun;
    BOOL bInputDoneStarted;

    /***
     * SPS/PPS/VPS
     */
    U8 *pHeader;
    U32 nHeaderSize;
};

/***
 * V4L2_CID_MVE_VIDEO_FRAME_RATE
 *
 * Sets the frame rate in frames per second (FPS), represented in Q16 format,
 * that is, signed 15.16 fixed-point format. Frame rate values are limited to
 * between 1 and 256 frames per second.
 */
static void setEncoderFramerate(ALLinlonv5v7EncContext *context, U32 fps) {
    if (!getCsweo(context->stCodec)) {
        setEncFramerate(getOutputPort(context->stCodec), fps << 16);
    } else {
        setFps(context->stCodec, fps << 16);
    }
}

/***
 * V4L2_CID_MVE_VIDEO_P_FRAMES
 *
 * ENC_P_FRAMES sets the number of P frames between two I frames.
 */
static void setPFrames(ALLinlonv5v7EncContext *context, U32 pframes) {
    setEncPFrames(getOutputPort(context->stCodec), pframes);
}

static void setH264MinQP(ALLinlonv5v7EncContext *context, U32 minqp) {
    if (!getCsweo(context->stCodec)) {
        setH264EncMinQP(getOutputPort(context->stCodec), minqp);
    } else {
        setMinqp(context->stCodec, minqp);
    }
}

static void setH264MaxQP(ALLinlonv5v7EncContext *context, U32 maxqp) {
    if (!getCsweo(context->stCodec)) {
        setH264EncMaxQP(getOutputPort(context->stCodec), maxqp);
    } else {
        setMaxqp(context->stCodec, maxqp);
    }
}

static void setH264FixedQPI(ALLinlonv5v7EncContext *context, U32 fqp) {
    setH264EncFixedQPI(getOutputPort(context->stCodec), fqp);
}

static void setH264FixedQPP(ALLinlonv5v7EncContext *context, U32 fqp) {
    setH264EncFixedQPP(getOutputPort(context->stCodec), fqp);
}

static void setH264FixedQPB(ALLinlonv5v7EncContext *context, U32 fqp) {
    setH264EncFixedQPB(getOutputPort(context->stCodec), fqp);
}

static void setHEVCFixedQPI(ALLinlonv5v7EncContext *context, U32 fqp) {
    setHEVCEncFixedQPI(getOutputPort(context->stCodec), fqp);
}

static void setHEVCFixedQPP(ALLinlonv5v7EncContext *context, U32 fqp) {
    setHEVCEncFixedQPP(getOutputPort(context->stCodec), fqp);
}

static void setHEVCFixedQPB(ALLinlonv5v7EncContext *context, U32 fqp) {
    setHEVCEncFixedQPB(getOutputPort(context->stCodec), fqp);
}

static void setHEVCMinQP(ALLinlonv5v7EncContext *context, U32 minqp) {
    if (!getCsweo(context->stCodec)) {
        setHEVCEncMinQP(getOutputPort(context->stCodec), minqp);
    } else {
        setMinqp(context->stCodec, minqp);
    }
}

static void setHEVCMaxQP(ALLinlonv5v7EncContext *context, U32 maxqp) {
    if (!getCsweo(context->stCodec)) {
        setHEVCEncMaxQP(getOutputPort(context->stCodec), maxqp);
    } else {
        setMaxqp(context->stCodec, maxqp);
    }
}

static void setEncoderMirror(ALLinlonv5v7EncContext *context, S32 mirror) {
    setPortMirror(getInputPort(context->stCodec), mirror);
}

/***
 * The struct mve_buffer_param_rate_control is used to enable rate control, and
 * to set the target bitrate.
 *
 * MVE_OPT_RATE_CONTROL_MODE_OFF:
 * This sets fixed a QP mode, this is the default. The target_bitrate value is
 * ignored and the quantization values QP_I, QP_P and QP_B are used.
 *
 * MVE_OPT_RATE_CONTROL_MODE_VARIABLE:
 * This mode aims to match bitrate target_bitrate while maximizing visual
 * quality. Arm China recommends you use this mode.
 *
 * MVE_OPT_RATE_CONTROL_MODE_CONSTANT:
 * This mode aims to keep the output bitstream at a fixed bitrate.
 *
 * MVE_OPT_RATE_CONTROL_MODE_C_VARIABLE:
 * Argument: max_bitrate. This mode aims to constrain the maximum bitrate to a
 * desired max_bitrate, and in the meantime to match target_bitrate.
 */
static void setEncoderRateControl(
    ALLinlonv5v7EncContext *context, const char *rc, S32 target_bitrate, S32 maximum_bitrate
) {
    struct v4l2_rate_control v4l2_rc;
    memset(&v4l2_rc, 0, sizeof(v4l2_rc));
    if (strcmp(rc, "standard") == 0) {
        v4l2_rc.rc_type = V4L2_OPT_RATE_CONTROL_MODE_STANDARD;
    } else if (strcmp(rc, "constant") == 0) {
        v4l2_rc.rc_type = V4L2_OPT_RATE_CONTROL_MODE_CONSTANT;
    } else if (strcmp(rc, "variable") == 0) {
        v4l2_rc.rc_type = V4L2_OPT_RATE_CONTROL_MODE_VARIABLE;
    } else if (strcmp(rc, "cvbr") == 0) {
        v4l2_rc.rc_type = V4L2_OPT_RATE_CONTROL_MODE_C_VARIABLE;
    } else if (strcmp(rc, "off") == 0) {
        v4l2_rc.rc_type = V4L2_OPT_RATE_CONTROL_MODE_OFF;
    } else {
        v4l2_rc.rc_type = V4L2_OPT_RATE_CONTROL_MODE_OFF;
    }
    if (v4l2_rc.rc_type) {
        v4l2_rc.target_bitrate = target_bitrate;
    }
    if (v4l2_rc.rc_type == V4L2_OPT_RATE_CONTROL_MODE_C_VARIABLE) {
        v4l2_rc.maximum_bitrate = maximum_bitrate;
    }
    setRateControl(getOutputPort(context->stCodec), &v4l2_rc);
}

static void setEncoderRotation(ALLinlonv5v7EncContext *context, S32 rotation) {
    setPortRotation(getInputPort(context->stCodec), rotation);
}

static void setEncoderCropLeft(ALLinlonv5v7EncContext *context, S32 left) {
    setCropLeft(getInputPort(context->stCodec), left);
}

static void setEncoderCropRight(ALLinlonv5v7EncContext *context, S32 right) {
    setCropRight(getInputPort(context->stCodec), right);
}

static void setEncoderCropTop(ALLinlonv5v7EncContext *context, S32 top) {
    setCropTop(getInputPort(context->stCodec), top);
}

static void setEncoderCropBottom(ALLinlonv5v7EncContext *context, S32 bottom) {
    setCropBottom(getInputPort(context->stCodec), bottom);
}

/**
 * @brief Apply the initial rate-control state from the channel attributes.
 *        With FIXQP and all QP fields zero, the legacy plugin defaults are
 *        used (HEVC fixed QP I=35 / P=30, rate control off).
 */
static void applyInitialRateControl(ALLinlonv5v7EncContext *context, const VencChnAttr *pstAttr) {
    if (pstAttr->eRcMode == VENC_RC_MODE_FIXQP && pstAttr->u32IQp == 0 && pstAttr->u32PQp == 0 &&
        pstAttr->u32BQp == 0) {
        /* legacy defaults */
        setHEVCFixedQPI(context, 35);
        setHEVCFixedQPP(context, 30);
        setEncoderRateControl(context, "off", 0, 0);
        return;
    }

    const char *rcMode = "off";
    switch (pstAttr->eRcMode) {
        case VENC_RC_MODE_CBR:
            rcMode = "constant";
            break;
        case VENC_RC_MODE_VBR:
            rcMode = "variable";
            break;
        case VENC_RC_MODE_CVBR:
            rcMode = "cvbr";
            break;
        case VENC_RC_MODE_FIXQP:
        default:
            rcMode = "off";
            break;
    }
    setEncoderRateControl(context, rcMode, (S32)pstAttr->u32Bitrate, (S32)pstAttr->u32Bitrate);

    if (pstAttr->u32MinQp > 0 || pstAttr->u32MaxQp > 0) {
        if (context->eCodecType == MPP_STREAM_CODEC_H265) {
            setHEVCMinQP(context, pstAttr->u32MinQp);
            setHEVCMaxQP(context, pstAttr->u32MaxQp);
        } else {
            setH264MinQP(context, pstAttr->u32MinQp);
            setH264MaxQP(context, pstAttr->u32MaxQp);
        }
    }

    if (pstAttr->eRcMode == VENC_RC_MODE_FIXQP) {
        if (context->eCodecType == MPP_STREAM_CODEC_H265) {
            setHEVCFixedQPI(context, pstAttr->u32IQp);
            setHEVCFixedQPP(context, pstAttr->u32PQp);
            setHEVCFixedQPB(context, pstAttr->u32BQp);
        } else {
            setH264FixedQPI(context, pstAttr->u32IQp);
            setH264FixedQPP(context, pstAttr->u32PQp);
            setH264FixedQPB(context, pstAttr->u32BQp);
        }
    }
}

static void notifyInputBufferDone(ALLinlonv5v7EncContext *context, UL ulBufferId) {
    if (ulBufferId != 0 && context->stCallbacks.pfnInputBufferDone) {
        context->stCallbacks.pfnInputBufferDone(context->stCallbacks.pUserData, ulBufferId);
    }
}

static U32 collectInputBuffersLocked(
    ALLinlonv5v7EncContext *context,
    UL *pulBufferIds,
    U32 u32Capacity) {
    Port *input = getInputPort(context->stCodec);
    S32 numBufs = getBufNum(input);
    U32 count = 0;

    for (S32 i = 0; i < numBufs; ++i) {
        Buffer *buffer = getBuffer(input, i);
        UL ulBufferId = 0;

        setIsQueued(buffer, MPP_FALSE);
        if (takeInputBufferId(buffer, &ulBufferId)) {
            if (count < u32Capacity) {
                pulBufferIds[count++] = ulBufferId;
            } else {
                error("input token capacity %u is smaller than V4L2 buffer count %d", u32Capacity, numBufs);
            }
        }
    }

    context->nInputNextIdx = 0;
    context->nInputQueuedCount = 0;
    return count;
}

static void *inputBufferDoneTask(void *arg) {
    ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)arg;
    struct pollfd pollFds[2] = {
        {.fd = context->nVideoFd, .events = POLLOUT},
        {.fd = context->nInputWakeFd, .events = POLLIN},
    };

    while (MPP_TRUE) {
        pthread_mutex_lock(&context->inputLock);
        while (context->bInputDoneRun && context->nInputQueuedCount == 0) {
            pthread_cond_wait(&context->inputCond, &context->inputLock);
        }
        BOOL bRunning = context->bInputDoneRun;
        pthread_mutex_unlock(&context->inputLock);
        if (!bRunning)
            break;

        S32 ret;
        do {
            ret = poll(pollFds, 2, -1);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) {
            error("input completion poll failed: %s", strerror(errno));
            usleep(1000);
            continue;
        }

        if (pollFds[1].revents & POLLIN) {
            U64 value;
            ssize_t readSize = read(context->nInputWakeFd, &value, sizeof(value));
            if (readSize < 0 && errno != EAGAIN)
                error("failed to read input completion eventfd: %s", strerror(errno));
            pollFds[1].revents = 0;
        }

        pthread_mutex_lock(&context->inputLock);
        bRunning = context->bInputDoneRun;
        if (!bRunning || context->nInputQueuedCount == 0 || !(pollFds[0].revents & POLLOUT)) {
            BOOL bPollError = (pollFds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
            pthread_mutex_unlock(&context->inputLock);
            pollFds[0].revents = 0;
            if (bPollError)
                usleep(1000);
            continue;
        }

        Buffer *buffer = dequeueBuffer(getInputPort(context->stCodec));
        UL ulBufferId = 0;
        BOOL bTokenValid = MPP_FALSE;
        if (buffer) {
            setIsQueued(buffer, MPP_FALSE);
            if (context->nInputQueuedCount > 0)
                context->nInputQueuedCount--;
            bTokenValid = takeInputBufferId(buffer, &ulBufferId);
            if (bTokenValid)
                context->nInputCallbacksInFlight++;
        }
        pthread_mutex_unlock(&context->inputLock);
        pollFds[0].revents = 0;

        if (bTokenValid) {
            notifyInputBufferDone(context, ulBufferId);
            pthread_mutex_lock(&context->inputLock);
            context->nInputCallbacksInFlight--;
            pthread_cond_broadcast(&context->inputCond);
            pthread_mutex_unlock(&context->inputLock);
        }
    }

    return NULL;
}

static S32 startInputBufferDoneTask(ALLinlonv5v7EncContext *context) {
    context->nInputWakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (context->nInputWakeFd < 0) {
        error("failed to create input completion eventfd: %s", strerror(errno));
        return MPP_INIT_FAILED;
    }

    pthread_mutex_lock(&context->inputLock);
    context->bInputDoneRun = MPP_TRUE;
    pthread_mutex_unlock(&context->inputLock);

    if (pthread_create(&context->inputDoneTid, NULL, inputBufferDoneTask, context) != 0) {
        error("failed to create input completion thread");
        pthread_mutex_lock(&context->inputLock);
        context->bInputDoneRun = MPP_FALSE;
        pthread_mutex_unlock(&context->inputLock);
        close(context->nInputWakeFd);
        context->nInputWakeFd = -1;
        return MPP_INIT_FAILED;
    }

    context->bInputDoneStarted = MPP_TRUE;
    return MPP_OK;
}

static void stopInputBufferDoneTask(ALLinlonv5v7EncContext *context) {
    if (!context->bInputDoneStarted)
        return;

    pthread_mutex_lock(&context->inputLock);
    context->bInputDoneRun = MPP_FALSE;
    pthread_cond_broadcast(&context->inputCond);
    pthread_mutex_unlock(&context->inputLock);

    U64 value = 1;
    ssize_t writeSize = write(context->nInputWakeFd, &value, sizeof(value));
    if (writeSize < 0 && errno != EAGAIN)
        error("failed to wake input completion thread: %s", strerror(errno));
    pthread_join(context->inputDoneTid, NULL);
    close(context->nInputWakeFd);
    context->nInputWakeFd = -1;
    context->bInputDoneStarted = MPP_FALSE;
}

ALBaseContext *al_enc_create(void) {
    ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)malloc(sizeof(ALLinlonv5v7EncContext));
    if (!context) {
        error("can not malloc ALLinlonv5v7EncContext, please check! (%s)", strerror(errno));
        return NULL;
    }

    memset(context, 0, sizeof(ALLinlonv5v7EncContext));
    context->nVideoFd = -1;
    context->nInputWakeFd = -1;
    if (pthread_mutex_init(&context->inputLock, NULL) != 0) {
        free(context);
        return NULL;
    }
    if (pthread_cond_init(&context->inputCond, NULL) != 0) {
        pthread_mutex_destroy(&context->inputLock);
        free(context);
        return NULL;
    }

    return &(context->stAlEncBaseContext.stAlBaseContext);
}

S32 al_enc_init(ALBaseContext *ctx, const VencChnAttr *pstAttr, const AlEncCallbacks *pstCallbacks) {
    if (!ctx) {
        error("input para ALBaseContext is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    if (!pstAttr) {
        error("input para VencChnAttr is NULL, please check!");
        return MPP_NULL_POINTER;
    }
    if (!pstCallbacks || !pstCallbacks->pfnInputBufferDone) {
        error("input completion callback is NULL");
        return MPP_NULL_POINTER;
    }

    ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;

    context->stCallbacks = *pstCallbacks;
    context->stAttr = *pstAttr;
    context->ePixelFormat = pstAttr->eInputPixelFormat;
    context->eCodecType = pstAttr->eCodecType;
    context->nInputFormatFourcc = get_linlonv5v7enc_codec_pixel_format(context->ePixelFormat);
    context->nOutputFormatFourcc = get_linlonv5v7enc_codec_coding_type(context->eCodecType);
    context->bIsBlockMode = MPP_FALSE;
    context->nWidth = (S32)pstAttr->u32Width;
    context->nHeight = (S32)pstAttr->u32Height;
    context->nAlign = pstAttr->u32Align == 0 ? 1 : (S32)pstAttr->u32Align;
    context->nRotation = (S32)pstAttr->u32RotateDegree;
    context->nInputMemType = V4L2_MEMORY_DMABUF;
    context->nOutputMemType = V4L2_MEMORY_MMAP;
    context->nInputType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    context->nOutputType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    context->bInputEos = MPP_FALSE;
    context->bOutputEos = MPP_FALSE;
    context->nInputNextIdx = 0;
    context->nInputQueuedCount = 0;
    context->nInputCallbacksInFlight = 0;

    MppFrameBufferType eBufferType;
    switch (pstAttr->eFrameBufMode) {
        case VENC_FRAME_BUF_DMABUF_INTERNAL:
            eBufferType = MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL;
            break;
        case VENC_FRAME_BUF_NORMAL_INTERNAL:
            eBufferType = MPP_FRAME_BUFFERTYPE_NORMAL_INTERNAL;
            break;
        case VENC_FRAME_BUF_DMABUF_EXTERNAL:
            eBufferType = MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL;
            break;
        default:
            eBufferType = MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL;
            break;
    }

    debug(
        "input para check: foramt:0x%x output format:0x%x", context->nInputFormatFourcc, context->nOutputFormatFourcc);

    context->nVideoFd = find_v4l2_encoder(context->sDevicePath, context->nOutputFormatFourcc);

    if (-1 == context->nVideoFd) {
        error("can not find the v4l2 codec device, please check!");
        return MPP_OPEN_FAILED;
    }

    debug("video fd = %d, device path = '%s', rot:%d", context->nVideoFd, context->sDevicePath, context->nRotation);

    context->stCodec = createCodec(
        context->nVideoFd,
        context->nWidth,
        context->nHeight,
        context->nAlign,
        context->bIsInterlaced,
        context->nInputType,
        context->nOutputType,
        context->nInputFormatFourcc,
        context->nOutputFormatFourcc,
        context->nInputMemType,
        context->nOutputMemType,
        ENCODER_INPUT_BUF_NUM,
        ENCODER_OUTPUT_BUF_NUM,
        context->bIsBlockMode,
        eBufferType);
    if (!context->stCodec) {
        error("create Codec failed, please check!");
        close(context->nVideoFd);
        context->nVideoFd = -1;
        return MPP_INIT_FAILED;
    }

    // set some parameters on the stream level
    applyInitialRateControl(context, pstAttr);
    if (pstAttr->u32Gop > 0)
        setPFrames(context, pstAttr->u32Gop);
    if (pstAttr->u32FrameRate > 0)
        setEncoderFramerate(context, pstAttr->u32FrameRate);
    setEncoderRotation(context, context->nRotation);

    // setformat, allocate buffer, stream on
    S32 streamRet = stream(context->stCodec);
    if (streamRet != MPP_OK) {
        error("al_enc_init: stream() failed (ret=%d), tearing down encoder", streamRet);
        destoryCodec(context->stCodec);
        context->stCodec = NULL;
        close(context->nVideoFd);
        context->nVideoFd = -1;
        return streamRet;
    }

    S32 ret = startInputBufferDoneTask(context);
    if (ret != MPP_OK)
        return ret;

    debug("init finish");

    return MPP_OK;
}

S32 al_enc_set_para(ALBaseContext *ctx, VencCmd cmd, const void *pParam) {
    if (!ctx) {
        error("input para ALBaseContext is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;

    switch (cmd) {
        case VENC_CMD_SET_RATE_CONTROL: {
            const VencRcAttr *pRcAttr = (const VencRcAttr *)pParam;
            if (!pRcAttr)
                return MPP_NULL_POINTER;
            const char *rcMode = "off";
            S32 targetBitrate = (S32)pRcAttr->u32BitRate;
            S32 maxBitrate = (S32)pRcAttr->u32MaxBitRate;
            switch (pRcAttr->enRcMode) {
                case VENC_RC_MODE_CBR:
                    rcMode = "constant";
                    break;
                case VENC_RC_MODE_VBR:
                    rcMode = "variable";
                    break;
                case VENC_RC_MODE_CVBR:
                    rcMode = "cvbr";
                    break;
                case VENC_RC_MODE_FIXQP:
                default:
                    rcMode = "off";
                    break;
            }
            setEncoderRateControl(context, rcMode, targetBitrate, maxBitrate);
            if (pRcAttr->u32MinQp > 0 || pRcAttr->u32MaxQp > 0) {
                if (context->eCodecType == MPP_STREAM_CODEC_H265) {
                    setHEVCMinQP(context, pRcAttr->u32MinQp);
                    setHEVCMaxQP(context, pRcAttr->u32MaxQp);
                } else {
                    setH264MinQP(context, pRcAttr->u32MinQp);
                    setH264MaxQP(context, pRcAttr->u32MaxQp);
                }
            }
            if (pRcAttr->enRcMode == VENC_RC_MODE_FIXQP) {
                if (context->eCodecType == MPP_STREAM_CODEC_H265) {
                    setHEVCFixedQPI(context, pRcAttr->u32IQp);
                    setHEVCFixedQPP(context, pRcAttr->u32PQp);
                    setHEVCFixedQPB(context, pRcAttr->u32BQp);
                } else {
                    setH264FixedQPI(context, pRcAttr->u32IQp);
                    setH264FixedQPP(context, pRcAttr->u32PQp);
                    setH264FixedQPB(context, pRcAttr->u32BQp);
                }
            }
            break;
        }
        case VENC_CMD_SET_FRAME_RATE: {
            const VencFrameRateAttr *pFr = (const VencFrameRateAttr *)pParam;
            if (!pFr)
                return MPP_NULL_POINTER;
            setEncoderFramerate(context, pFr->u32FrameRate);
            break;
        }
        case VENC_CMD_SET_CROP: {
            const VencCropAttr *pCrop = (const VencCropAttr *)pParam;
            if (!pCrop)
                return MPP_NULL_POINTER;
            setEncoderCropLeft(context, pCrop->s32Left);
            setEncoderCropRight(context, pCrop->s32Right);
            setEncoderCropTop(context, pCrop->s32Top);
            setEncoderCropBottom(context, pCrop->s32Bottom);
            break;
        }
        case VENC_CMD_SET_FORCE_IDR: {
            struct v4l2_control control;
            memset(&control, 0, sizeof(control));
            control.id = V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME;
            control.value = 0;
            if (-1 == ioctl(context->nVideoFd, VIDIOC_S_CTRL, &control)) {
                error("Failed to force IDR frame");
            }
            break;
        }
        case VENC_CMD_SET_ROI: {
            const VencRoiAttr *pRoi = (const VencRoiAttr *)pParam;
            if (!pRoi)
                return MPP_NULL_POINTER;
            struct v4l2_mvx_roi_regions stRegions;
            memset(&stRegions, 0, sizeof(stRegions));
            stRegions.pic_index = pRoi->u32PicIndex;
            stRegions.qp_present = pRoi->u8QpPresent;
            stRegions.qp = pRoi->u8Qp;
            stRegions.roi_present = pRoi->u8RoiPresent;
            stRegions.num_roi = pRoi->u8NumRoi;
            U32 u32Num = pRoi->u8NumRoi;
            if (u32Num > V4L2_MVX_MAX_FRAME_REGIONS)
                u32Num = V4L2_MVX_MAX_FRAME_REGIONS;
            if (u32Num > MPP_MAX_FRAME_REGIONS)
                u32Num = MPP_MAX_FRAME_REGIONS;
            for (U32 i = 0; i < u32Num; i++) {
                stRegions.roi[i].mbx_left = pRoi->stRoi[i].u16MbxLeft;
                stRegions.roi[i].mbx_right = pRoi->stRoi[i].u16MbxRight;
                stRegions.roi[i].mby_top = pRoi->stRoi[i].u16MbyTop;
                stRegions.roi[i].mby_bottom = pRoi->stRoi[i].u16MbyBottom;
                stRegions.roi[i].qp_delta = pRoi->stRoi[i].s16QpDelta;
            }
            setRoiRegion(getOutputPort(context->stCodec), &stRegions);
            break;
        }
        case VENC_CMD_SET_MIRROR: {
            const VencMirrorAttr *pMirror = (const VencMirrorAttr *)pParam;
            if (!pMirror)
                return MPP_NULL_POINTER;
            setEncoderMirror(context, pMirror->s32Mirror);
            break;
        }
        case VENC_CMD_SET_SLICE: {
            const VencSliceAttr *pSlice = (const VencSliceAttr *)pParam;
            if (!pSlice)
                return MPP_NULL_POINTER;
            setEncSliceSpacing(getOutputPort(context->stCodec), pSlice->s32Spacing);
            break;
        }
        default:
            return MPP_OK;
    }
    return MPP_OK;
}

/**
 * @brief Wire one input frame's planes into a V4L2 buffer slot.
 */
static void setupInputFrame(ALLinlonv5v7EncContext *context, Buffer *buf, const VideoFrameInfo *pstFrame) {
    S32 frameId = (S32)pstFrame->u32Idx;
    const VideoFrame *pstV = &pstFrame->stVFrame;

    if (context->nInputMemType == V4L2_MEMORY_USERPTR) {
        if (context->ePixelFormat == MPP_PIXEL_FORMAT_NV12 || context->ePixelFormat == MPP_PIXEL_FORMAT_NV21) {
            setExternalUserPtrFrame(buf, (U8 *)pstV->ulPlaneVirAddr[0], (U8 *)pstV->ulPlaneVirAddr[1], NULL, frameId);
        } else if (context->ePixelFormat == MPP_PIXEL_FORMAT_I420) {
            setExternalUserPtrFrame(
                buf,
                (U8 *)pstV->ulPlaneVirAddr[0],
                (U8 *)pstV->ulPlaneVirAddr[1],
                (U8 *)pstV->ulPlaneVirAddr[2],
                frameId);
        } else if (
            context->ePixelFormat == MPP_PIXEL_FORMAT_RGBA || context->ePixelFormat == MPP_PIXEL_FORMAT_ARGB ||
            context->ePixelFormat == MPP_PIXEL_FORMAT_BGRA || context->ePixelFormat == MPP_PIXEL_FORMAT_ABGR ||
            context->ePixelFormat == MPP_PIXEL_FORMAT_YUYV || context->ePixelFormat == MPP_PIXEL_FORMAT_UYVY) {
            setExternalUserPtrFrame(buf, (U8 *)pstV->ulPlaneVirAddr[0], NULL, NULL, frameId);
        }
    } else if (context->nInputMemType == V4L2_MEMORY_DMABUF) {
        setExternalDmaBuf(buf, (S32)pstV->u32Fd[0], (U8 *)pstV->ulPlaneVirAddr[0], frameId);
    }
    setTimeStamp(buf, (S64)pstV->u64PTS);
}

S32 al_enc_send_input_frame(ALBaseContext *ctx, const VideoFrameInfo *pstFrame) {
    if (!ctx) {
        error("input para ALBaseContext is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    if (!pstFrame) {
        error("input para VideoFrameInfo is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;
    S32 ret = 0;

    BOOL bEosFlag = (pstFrame->stVFrame.u32FrameFlag & MPP_FRAME_FLAG_EOS) ? MPP_TRUE : MPP_FALSE;

    if (bEosFlag && pstFrame->stVFrame.u32PlaneNum > 0) {
        debug("eos flag of input frame with data is set, EOS is coming");
        context->bInputEos = MPP_TRUE;
    }

    if (bEosFlag && pstFrame->stVFrame.u32PlaneNum == 0) {
        debug("eos flag of input frame without data is set, EOS is coming");
        context->bInputEos = MPP_TRUE;

        // gstreamer last frame may only carry the eos flag, no memory
        sendEncStopCommand(getInputPort(context->stCodec));
        return MPP_OK;
    }

    pthread_mutex_lock(&context->inputLock);
    Port *input = getInputPort(context->stCodec);
    S32 numBufs = getBufNum(input);
    if (numBufs <= 0) {
        pthread_mutex_unlock(&context->inputLock);
        return MPP_CODER_NO_DATA;
    }

    Buffer *buffer = NULL;
    for (S32 i = 0; i < numBufs; ++i) {
        S32 bufIdx = (S32)((context->nInputNextIdx + (U32)i) % (U32)numBufs);
        Buffer *candidate = getBuffer(input, bufIdx);
        if (!getIsQueued(candidate)) {
            buffer = candidate;
            context->nInputNextIdx = ((U32)bufIdx + 1U) % (U32)numBufs;
            break;
        }
    }

    if (!buffer) {
        pthread_mutex_unlock(&context->inputLock);
        return MPP_CODER_NO_DATA;
    }

    setupInputFrame(context, buffer, pstFrame);
    setEndOfStream(buffer, context->bInputEos);
    setInputBufferId(buffer, pstFrame->ulBufferId);
    ret = queueBuffer(input, buffer);
    if (ret != MPP_OK) {
        clearInputBufferId(buffer);
        pthread_mutex_unlock(&context->inputLock);
        return ret;
    }

    setIsQueued(buffer, MPP_TRUE);
    context->nInputQueuedCount++;
    pthread_cond_signal(&context->inputCond);
    pthread_mutex_unlock(&context->inputLock);
    return MPP_OK;
}

S32 al_enc_request_output_stream(ALBaseContext *ctx, StreamBufferInfo *pstStream, U32 u32TimeoutMs) {
    if (!ctx) {
        error("input para ALBaseContext is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    if (!pstStream || !pstStream->pu8Addr) {
        error("input para StreamBufferInfo is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;
    S32 ret = 0;
    struct pollfd p = {.fd = context->nVideoFd, .events = POLLIN};
    U8 *out_ptr = (U8 *)pstStream->pu8Addr;
    U32 u32Capacity = pstStream->u32Size;

    S32 nTimeoutMs = (u32TimeoutMs == (U32)-1) ? -1 : (S32)u32TimeoutMs;
    ret = poll(&p, 1, nTimeoutMs);
    if (ret < 0) {
        error("poll returned error: %s", strerror(errno));
        return MPP_POLL_FAILED;
    }
    if (ret == 0 || !(p.revents & POLLIN) || (p.revents & POLLERR)) {
        return MPP_CODER_NO_DATA;
    }

    Buffer *buffer = dequeueBuffer(getOutputPort(context->stCodec));
    if (!buffer)
        return MPP_CODER_NO_DATA;
    struct v4l2_buffer *b = getV4l2Buffer(buffer);

    // stash SPS/PPS
    if ((b->flags & V4L2_BUF_FLAG_MVX_CODEC_CONFIG) == V4L2_BUF_FLAG_MVX_CODEC_CONFIG) {
        if (context->nHeaderSize == 0) {
            context->pHeader = malloc(b->bytesused);
            context->nHeaderSize = b->bytesused;
        } else if (context->nHeaderSize < b->bytesused) {
            void *temp = realloc(context->pHeader, b->bytesused);
            if (temp == NULL) {
                free(context->pHeader);
                context->pHeader = NULL;
                context->nHeaderSize = 0;
            }
            context->pHeader = temp;
            context->nHeaderSize = b->bytesused;
        }
        if (context->pHeader != NULL) {
            if (context->eCodecType == MPP_STREAM_CODEC_H265 || context->eCodecType == MPP_STREAM_CODEC_VP9) {
                memcpy(context->pHeader, getUserPtrForHevcAndVp9Encode(buffer, 0), b->bytesused);
            } else {
                memcpy(context->pHeader, getUserPtr(buffer, 0), b->bytesused);
            }
        }
        resetVendorFlags(buffer);
        queueBuffer(getOutputPort(context->stCodec), buffer);
        return MPP_CODER_NO_DATA;
    }

    BOOL bKeyFrame = (b->flags & V4L2_BUF_FLAG_KEYFRAME) ? MPP_TRUE : MPP_FALSE;
    U32 u32HeaderSize = (bKeyFrame && context->pHeader) ? context->nHeaderSize : 0;
    U32 u32PayloadSize = b->bytesused + u32HeaderSize;

    /* capacity check before any memcpy (the legacy plugin copied unchecked) */
    if (u32PayloadSize > u32Capacity) {
        error("output stream buffer too small: need %u, have %u", u32PayloadSize, u32Capacity);
        resetVendorFlags(buffer);
        queueBuffer(getOutputPort(context->stCodec), buffer);
        return MPP_OUT_OF_MEM;
    }

    // add SPS/PPS to IDR
    if (u32HeaderSize > 0) {
        memcpy(out_ptr, context->pHeader, u32HeaderSize);
        out_ptr += u32HeaderSize;
    }

    if (!V4L2_TYPE_IS_OUTPUT(b->type) && b->flags & V4L2_BUF_FLAG_LAST) {
        debug("Capture EOS.");
        context->bOutputEos = MPP_TRUE;
    }

    if (context->eCodecType == MPP_STREAM_CODEC_H265 || context->eCodecType == MPP_STREAM_CODEC_VP9) {
        memcpy(out_ptr, getUserPtrForHevcAndVp9Encode(buffer, 0), b->bytesused);
    } else {
        memcpy(out_ptr, getUserPtr(buffer, 0), b->bytesused);
    }

    pstStream->u32Size = u32PayloadSize;
    pstStream->u64PTS = (U64)(b->timestamp.tv_sec * 1000000 + b->timestamp.tv_usec);
    pstStream->bKeyFrame = bKeyFrame;
    pstStream->bEndOfStream = context->bOutputEos;
    pstStream->eCodecType = context->eCodecType;
    pstStream->u32Width = (U32)context->nWidth;
    pstStream->u32Height = (U32)context->nHeight;

    resetVendorFlags(buffer);
    queueBuffer(getOutputPort(context->stCodec), buffer);

    if (context->bOutputEos)
        return MPP_CODER_EOS;

    return MPP_OK;
}

S32 al_enc_get_status(ALBaseContext *ctx, VencChnStatus *pstStatus) {
    if (!ctx || !pstStatus) {
        error("input para is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;

    U32 u32Queued = 0;
    if (context->stCodec) {
        S32 nBufNum = getBufNum(getInputPort(context->stCodec));
        for (S32 i = 0; i < nBufNum; i++) {
            Buffer *buf = getBuffer(getInputPort(context->stCodec), i);
            if (buf && getIsQueued(buf))
                u32Queued++;
        }
    }
    pstStatus->u32LeftInputFrames = u32Queued;
    pstStatus->u32LeftOutputStreams = 0;
    pstStatus->u32Width = (U32)context->nWidth;
    pstStatus->u32Height = (U32)context->nHeight;
    pstStatus->bEndOfStream = context->bOutputEos;

    return MPP_OK;
}

S32 al_enc_flush(ALBaseContext *ctx) {
    if (!ctx)
        return MPP_NULL_POINTER;
    ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;
    UL aulBufferIds[ENCODER_INPUT_BUF_NUM];
    U32 u32BufferCount;

    debug("Flush start ========================================");
    pthread_mutex_lock(&context->inputLock);
    handleFlush(context->stCodec, MPP_FALSE);
    u32BufferCount = collectInputBuffersLocked(context, aulBufferIds, NUM_OF(aulBufferIds));
    while (context->nInputCallbacksInFlight > 0) {
        pthread_cond_wait(&context->inputCond, &context->inputLock);
    }
    pthread_mutex_unlock(&context->inputLock);

    for (U32 i = 0; i < u32BufferCount; ++i) {
        notifyInputBufferDone(context, aulBufferIds[i]);
    }
    debug("Flush finish ========================================");

    return MPP_OK;
}

void al_enc_destory(ALBaseContext *ctx) {
    if (!ctx)
        return;
    ALLinlonv5v7EncContext *context = (ALLinlonv5v7EncContext *)ctx;
    UL aulBufferIds[ENCODER_INPUT_BUF_NUM];
    U32 u32BufferCount = 0;

    debug("destory start");
    stopInputBufferDoneTask(context);
    if (context->stCodec) {
        enum v4l2_buf_type input_type = getV4l2BufType(getInputPort(context->stCodec));
        enum v4l2_buf_type output_type = getV4l2BufType(getOutputPort(context->stCodec));

        pthread_mutex_lock(&context->inputLock);
        mpp_v4l2_stream_off(context->nVideoFd, &input_type);
        mpp_v4l2_stream_off(context->nVideoFd, &output_type);
        u32BufferCount = collectInputBuffersLocked(context, aulBufferIds, NUM_OF(aulBufferIds));
        pthread_mutex_unlock(&context->inputLock);
        debug("stream off finish");

        for (U32 i = 0; i < u32BufferCount; ++i) {
            notifyInputBufferDone(context, aulBufferIds[i]);
        }

        destoryCodec(context->stCodec);
        context->stCodec = NULL;
        debug("destory codec finish");
    }
    if (context->nVideoFd >= 0) {
        close(context->nVideoFd);
        context->nVideoFd = -1;
    }
    if (context->pHeader) {
        free(context->pHeader);
        context->pHeader = NULL;
    }
    pthread_cond_destroy(&context->inputCond);
    pthread_mutex_destroy(&context->inputLock);
    free(context);
    context = NULL;
}
