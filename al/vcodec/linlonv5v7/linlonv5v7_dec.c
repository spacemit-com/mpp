/*
 * Copyright 2022-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: video decode plugin for V4L2 codec interface.
 *               Consumes MPI public structs directly (VdecChnAttr /
 *               VdecChnStatus / VideoFrameInfo / StreamBufferInfo).
 */

#define ENABLE_DEBUG 1

#include <errno.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "al_interface_dec.h"
#include "linlonv5v7_codec.h"
#include "log.h"
#include "mvx-v4l2-controls.h"
#include "v4l2_utils.h"

#define MODULE_TAG "linlonv5v7_dec"

CODING_TYPE_MAPPING_DEFINE(Linlonv5v7Dec, S32)
static const ALLinlonv5v7DecCodingTypeMapping stALLinlonv5v7DecCodingTypeMapping[] = {
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
CODING_TYPE_MAPPING_CONVERT(Linlonv5v7Dec, linlonv5v7dec, S32)

PIXEL_FORMAT_MAPPING_DEFINE(Linlonv5v7Dec, S32)
static const ALLinlonv5v7DecPixelFormatMapping stALLinlonv5v7DecPixelFormatMapping[] = {
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
};
PIXEL_FORMAT_MAPPING_CONVERT(Linlonv5v7Dec, linlonv5v7dec, S32)

typedef struct _ALLinlonv5v7DecContext ALLinlonv5v7DecContext;

struct _ALLinlonv5v7DecContext {
    ALDecBaseContext stAlDecBaseContext;
    VdecChnAttr stAttr;             // channel attributes (copied at init)
    MppStreamCodecType eCodecType;  // input stream format
    MppPixelFormat ePixelFormat;    // output frame format

    Codec *stCodec;

    /***
     * for open video device, such as /dev/video0
     */
    U8 sDevicePath[20];
    S32 nVideoFd;

    /***
     * enum v4l2_buf_type
     * nInputType: always V4L2_BUF_TYPE_VIDEO_OUTPUT
     * nOutputType: always V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
     */
    U32 nInputType;
    U32 nOutputType;

    /***
     * nInputFormatFourcc: V4L2_PIX_FMT_H264, etc.
     * nOutputFormatFourcc: V4L2_PIX_FMT_NV12, etc.
     */
    U32 nInputFormatFourcc;
    U32 nOutputFormatFourcc;

    /***
     * enum v4l2_memory
     * nInputMemType: always V4L2_MEMORY_MMAP
     * nOutputMemType: always V4L2_MEMORY_DMABUF (external dma-buf capture)
     */
    U32 nInputMemType;
    U32 nOutputMemType;

    U32 nInputBufferNum;
    U32 nOutputBufferNum;

    /***
     * MPP_FALSE, meaning that open device node with O_NONBLOCK
     */
    BOOL bIsBlockMode;
    BOOL bIsInterlaced;

    /***
     * video width and height (rotation-adjusted, updated on resolution change)
     * 0x0 is supported, driver can parse real width and height and return by
     * V4L2_EVENT_SOURCE_CHANGE.
     */
    S32 nWidth;
    S32 nHeight;
    S32 nAlign;

    S32 nRotation;
    S32 nScale;

    S32 nNaluFmt;

    /***
     * EOS flag, default MPP_FALSE
     * when a packet with length=0 or with eos=1 comes, bInputEos is set to
     * MPP_TRUE. bEosReached is set when the capture side delivered EOS.
     */
    BOOL bInputEos;
    BOOL bEosReached;

    pthread_t pollthread;
    BOOL bPollThreadCreated;

    /***
     * default MPP_FLASE
     *
     * when al_dec_destory is called, bIsDestoryed will be set to MPP_TRUE, some
     * threads stop and some resources recycle.
     *
     * Accessed from both the destroy thread (writer) and the poll thread
     * (reader). 'volatile' alone does not provide inter-thread ordering or
     * atomicity, so this is a C11 atomic to give well-defined, race-free
     * publication of the shutdown request.
     */
    atomic_bool bIsDestoryed;

    /***
     * num of input buffer in driver
     * default 0, also 0 after flush
     */
    U32 nInputQueuedNum;
    S32 nInputQueueLeftNum;

    /***
     * capture buffer ownership bookkeeping: MPP_TRUE while a buffer is queued
     * inside the decoder, MPP_FALSE while it is held by the caller. Kept
     * private to the plugin and surfaced via al_dec_get_status.
     */
    BOOL bIsBufferInDecoder[MAX_OUTPUT_BUF_NUM];

    S64 nEosPts;
};

static void setDecoderInterlaced(ALLinlonv5v7DecContext *context, BOOL interlaced) {
    setPortInterlaced(getOutputPort(context->stCodec), interlaced);
}

static void setDecoderRotation(ALLinlonv5v7DecContext *context, S32 rotation) {
    setPortRotation(getOutputPort(context->stCodec), rotation);
}

static void setDecoderDownScale(ALLinlonv5v7DecContext *context, S32 scale) {
    setPortDownScale(getOutputPort(context->stCodec), scale);
}

/***
 * VIDIOC_S_MVX_DSL_FRAME
 *
 * struct v4l2_mvx_dsl_frame
 * {
 *   uint32_t width;
 *   uint32_t height;
 * };
 */
static void setDecoderDSLFrame(ALLinlonv5v7DecContext *context, S32 width, S32 height) {
    setDSLFrame(getOutputPort(context->stCodec), width, height);
}

static S32 checkInputParameters(MppStreamCodecType type, MppPixelFormat format) {
    if (type != MPP_STREAM_CODEC_H264 && type != MPP_STREAM_CODEC_H265 && type != MPP_STREAM_CODEC_MJPEG &&
        type != MPP_STREAM_CODEC_VP8 && type != MPP_STREAM_CODEC_VP9 && type != MPP_STREAM_CODEC_MPEG2 &&
        type != MPP_STREAM_CODEC_MPEG4) {
        error("not support this coding type (%d)!", type);
        return MPP_NOT_SUPPORTED_FORMAT;
    }

    if (format != MPP_PIXEL_FORMAT_I420 && format != MPP_PIXEL_FORMAT_NV12 && format != MPP_PIXEL_FORMAT_NV21) {
        error("not support this format (%d)!", format);
        return MPP_NOT_SUPPORTED_FORMAT;
    }

    return MPP_OK;
}

/***
 * pthread for poll event, need usleep, or CPU usage will soar, biubiu.
 */
void *runpoll(void *private_data) {
    ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)private_data;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    while (1) {
        if (atomic_load(&context->bIsDestoryed))
            break;

        struct pollfd p = {.fd = context->nVideoFd, .events = POLLPRI};
        S32 ret = poll(&p, 1, POLL_TIMEOUT);

        if (atomic_load(&context->bIsDestoryed))
            break;

        if (ret < 0) {
            error("Poll returned error code.");
        }

        if (p.revents & POLLERR) {
            error("Poll returned error event.");
        }

        if ((p.revents & POLLPRI) && !atomic_load(&context->bIsDestoryed)) {
            handleEvent(context->stCodec);
        }

        usleep(10000);
        pthread_testcancel();
    }

    pthread_exit(NULL);
}

ALBaseContext *al_dec_create(void) {
    ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)malloc(sizeof(ALLinlonv5v7DecContext));
    if (!context) {
        error("can not malloc ALLinlonv5v7DecContext, please check! (%s)", strerror(errno));
        return NULL;
    }

    memset(context, 0, sizeof(ALLinlonv5v7DecContext));

    debug("init create");

    return &(context->stAlDecBaseContext.stAlBaseContext);
}

S32 al_dec_init(ALBaseContext *ctx, const VdecChnAttr *pstAttr, AlDecBufferRequirement *pstReq) {
    if (!ctx) {
        error("input para ALBaseContext is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    if (!pstAttr || !pstReq) {
        error("input para VdecChnAttr/AlDecBufferRequirement is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    S32 ret = 0;

    ret = checkInputParameters(pstAttr->eCodecType, pstAttr->eOutputPixelFormat);
    if (ret) {
        error("not support this format, please check!");
        return MPP_NOT_SUPPORTED_FORMAT;
    }

    ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;

    context->stAttr = *pstAttr;
    context->eCodecType = pstAttr->eCodecType;
    context->ePixelFormat = pstAttr->eOutputPixelFormat;
    context->nInputFormatFourcc = get_linlonv5v7dec_codec_coding_type(context->eCodecType);
    context->nOutputFormatFourcc = get_linlonv5v7dec_codec_pixel_format(context->ePixelFormat);
    context->bIsBlockMode = MPP_FALSE;
    context->nWidth = (S32)pstAttr->u32Width;
    context->nHeight = (S32)pstAttr->u32Height;
    context->nAlign = pstAttr->u32Align == 0 ? 1 : (S32)pstAttr->u32Align;
    context->bIsInterlaced = pstAttr->bIsInterlaced;
    context->nRotation = (S32)pstAttr->u32RotateDegree;
    context->nScale = 0;
    context->nInputMemType = V4L2_MEMORY_MMAP;
    /* capture side always runs on external dma-bufs supplied by the caller */
    context->nOutputMemType = V4L2_MEMORY_DMABUF;
    context->nInputType = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    context->nOutputType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    context->bInputEos = MPP_FALSE;
    context->bEosReached = MPP_FALSE;
    atomic_store(&context->bIsDestoryed, false);
    context->nInputQueuedNum = 0;
    context->nEosPts = -1;
    context->bPollThreadCreated = MPP_FALSE;

    /* caller-desired buffer counts; 0 means plugin default, and the port
     * layer can track at most MAX_INPUT/OUTPUT_BUF_NUM buffers */
    context->nInputBufferNum = pstReq->u32InputBufNum ? pstReq->u32InputBufNum : DECODER_INPUT_BUF_NUM;
    if (context->nInputBufferNum > MAX_INPUT_BUF_NUM) {
        error("input buffer num %d exceeds max %d, clamped", context->nInputBufferNum, MAX_INPUT_BUF_NUM);
        context->nInputBufferNum = MAX_INPUT_BUF_NUM;
    }
    context->nOutputBufferNum = pstReq->u32OutputBufNum ? pstReq->u32OutputBufNum : DECODER_OUTPUT_BUF_NUM;
    if (context->nOutputBufferNum > MAX_OUTPUT_BUF_NUM) {
        error("output buffer num %d exceeds max %d, clamped", context->nOutputBufferNum, MAX_OUTPUT_BUF_NUM);
        context->nOutputBufferNum = MAX_OUTPUT_BUF_NUM;
    }

    debug(
        "input para check: foramt:0x%x output format:0x%x input buffer num:%d "
        "output buffer num:%d",
        context->nInputFormatFourcc,
        context->nOutputFormatFourcc,
        context->nInputBufferNum,
        context->nOutputBufferNum);

    for (S32 i = 0; i < MAX_OUTPUT_BUF_NUM; i++)
        context->bIsBufferInDecoder[i] = MPP_TRUE;

    context->nVideoFd = find_v4l2_decoder(context->sDevicePath, context->nInputFormatFourcc);
    if (-1 == context->nVideoFd) {
        error("can not find and open the v4l2 codec device, please check!");
        return MPP_OPEN_FAILED;
    }

    debug("video fd = %d, device path = '%s'", context->nVideoFd, context->sDevicePath);

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
        context->nInputBufferNum,
        context->nOutputBufferNum,
        context->bIsBlockMode,
        MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL);
    if (!context->stCodec) {
        error("create Codec failed, please check!");
        return MPP_INIT_FAILED;
    }

    // set some parameters on the stream level
    setDecoderInterlaced(context, context->bIsInterlaced);
    setDecoderRotation(context, context->nRotation);
    setDecoderDownScale(context, context->nScale);
    if (pstAttr->stScale.bScaleEnable) {
        setDecoderDSLFrame(context, (S32)pstAttr->stScale.u32Width, (S32)pstAttr->stScale.u32Height);
    } else {
        setDecoderDSLFrame(context, 0, 0);
    }

    // setformat, allocate buffer, stream on
    stream(context->stCodec);

    // pthread for handle event or something
    ret = pthread_create(&context->pollthread, NULL, runpoll, (void *)context);
    if (ret == 0) {
        context->bPollThreadCreated = MPP_TRUE;
    } else {
        /*
         * Without the poll thread the decoder can never dequeue frames, so
         * initialization has effectively failed. Tear down what we already set
         * up (stream off, destroy codec, close fd) and report the failure
         * instead of returning MPP_OK and leaving the caller with a dead
         * decoder.
         */
        error("create poll thread failed (%s)", strerror(ret));
        enum v4l2_buf_type input_type = getV4l2BufType(getInputPort(context->stCodec));
        enum v4l2_buf_type output_type = getV4l2BufType(getOutputPort(context->stCodec));
        mpp_v4l2_stream_off(context->nVideoFd, &input_type);
        mpp_v4l2_stream_off(context->nVideoFd, &output_type);
        destoryCodec(context->stCodec);
        context->stCodec = NULL;
        close(context->nVideoFd);
        context->nVideoFd = -1;
        return MPP_INIT_FAILED;
    }

    context->nInputQueueLeftNum = getBufNum(getInputPort(context->stCodec));
    context->nInputBufferNum = (U32)getBufNum(getInputPort(context->stCodec));
    context->nOutputBufferNum = (U32)getBufNum(getOutputPort(context->stCodec));

    pstReq->u32InputBufNum = context->nInputBufferNum;
    pstReq->u32OutputBufNum = context->nOutputBufferNum;

    debug("init finish");

    return MPP_OK;
}

S32 al_dec_get_status(ALBaseContext *ctx, VdecChnStatus *pstStatus) {
    if (!ctx || !pstStatus) {
        error("input para is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;

    struct pollfd p = {.fd = context->nVideoFd, .events = POLLOUT};
    S32 ret = poll(&p, 1, POLL_TIMEOUT);

    if (ret < 0) {
        error("Poll returned error code.");
    }

    if (p.revents & POLLERR) {
        usleep(2000);
        error("Poll returned error event.");
    }

    // used for update nInputQueueLeftNum, APP use it to decide whether send
    // packet to MPP.
    if ((p.revents & POLLOUT) && !context->nInputQueueLeftNum) {
        context->nInputQueueLeftNum = 1;
    }

    pstStatus->u32LeftStreamFrames = context->nInputQueueLeftNum > 0 ? (U32)context->nInputQueueLeftNum : 0;

    U32 u32Held = 0;
    S32 nBufNum = context->stCodec ? getBufNum(getOutputPort(context->stCodec)) : 0;
    for (S32 i = 0; i < nBufNum && i < MAX_OUTPUT_BUF_NUM; i++) {
        if (!context->bIsBufferInDecoder[i])
            u32Held++;
    }
    pstStatus->u32LeftDecodedFrames = u32Held;
    pstStatus->u32Width = (U32)context->nWidth;
    pstStatus->u32Height = (U32)context->nHeight;
    pstStatus->bEndOfStream = context->bEosReached;

    return MPP_OK;
}

S32 al_dec_decode(ALBaseContext *ctx, const StreamBufferInfo *pstStream) {
    if (!ctx) {
        error("input para ALBaseContext is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    if (!pstStream) {
        error("input para StreamBufferInfo is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;
    S32 ret = 0;
    struct pollfd p = {.fd = context->nVideoFd, .events = POLLOUT};

    if (!pstStream->u32Size) {
        debug("length of input packet is 0, EOS is coming, pts(%ld)", (S64)pstStream->u64PTS);
        context->bInputEos = MPP_TRUE;
        context->nEosPts = (S64)pstStream->u64PTS;
    }

    if (pstStream->bEndOfStream) {
        debug("eos flag of input packet is set, EOS is coming(%ld)", (S64)pstStream->u64PTS);
        context->bInputEos = MPP_TRUE;
        context->nEosPts = (S64)pstStream->u64PTS;
    }

    if (unlikely(context->nInputQueuedNum < (U32)getBufNum(getInputPort(context->stCodec)))) {
        Buffer *buf = getBuffer(getInputPort(context->stCodec), context->nInputQueuedNum);
        memcpy(getUserPtr(buf, 0), pstStream->pu8Addr, pstStream->u32Size);
        struct v4l2_buffer *b = getV4l2Buffer(buf);
        b->bytesused = pstStream->u32Size;
        setEndOfFrame(buf, MPP_TRUE);
        setEndOfStream(buf, MPP_FALSE);
        setTimeStamp(buf, (S64)pstStream->u64PTS);
        ret = queueBuffer(getInputPort(context->stCodec), buf);
        if (ret) {
            error("queueBuffer failed, should not failed, please check!");
            return ret;
        }
        context->nInputQueuedNum++;
        context->nInputQueueLeftNum--;
    } else {
        ret = runPoll(context->stCodec, &p);
        if (MPP_OK == ret && p.revents & POLLOUT) {
            ret = handleInputBuffer(getInputPort(context->stCodec), context->bInputEos, pstStream);
            if (ret < 0) {
                error("handleInputBuffer failed, should not failed, please check!");
                return ret;
            }
            context->nInputQueueLeftNum--;
        } else {
            return MPP_POLL_FAILED;
        }
    }
    return MPP_OK;
}

S32 al_dec_request_output_frame(ALBaseContext *ctx, VideoFrameInfo *pstFrame, U32 u32TimeoutMs) {
    if (!ctx || !pstFrame) {
        error("input para is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;
    S32 ret = 0;

    struct pollfd p = {.fd = context->nVideoFd, .events = POLLIN};

    /* Use caller-specified timeout for poll */
    S32 nTimeoutMs = (u32TimeoutMs == (U32)-1) ? -1 : (S32)u32TimeoutMs;
    ret = poll(&p, 1, nTimeoutMs);
    if (ret < 0) {
        error("poll returned error: %s", strerror(errno));
        return MPP_POLL_FAILED;
    }
    if (p.revents & POLLERR) {
        /* POLLERR from V4L2 typically means no buffers are queued.
         * This is NOT a fatal error - just means we need to wait for
         * buffers to be recycled back to the decoder. Return NO_DATA
         * instead of POLL_FAILED to allow recovery. */
        return MPP_CODER_NO_DATA;
    }
    if (ret == 0 || !(p.revents & POLLIN)) {
        return MPP_CODER_NO_DATA;
    }

    memset(pstFrame, 0, sizeof(*pstFrame));
    pstFrame->eFrameType = FRAME_TYPE_VDEC;
    pstFrame->eModId = MPP_ID_VDEC;

    ret = handleOutputBuffer(getOutputPort(context->stCodec), MPP_FALSE, pstFrame);
    if (ret == MPP_RESOLUTION_CHANGED) {
        if (context->nRotation == 90 || context->nRotation == 270) {
            context->nWidth = getBufHeight(getOutputPort(context->stCodec));
            context->nHeight = getBufWidth(getOutputPort(context->stCodec));
        } else {
            context->nWidth = getBufWidth(getOutputPort(context->stCodec));
            context->nHeight = getBufHeight(getOutputPort(context->stCodec));
        }
        context->nOutputBufferNum = (U32)getBufNum(getOutputPort(context->stCodec));

        /* when resolution changed, output buffers must be re-queued by the
         * caller, so mark all of them as owned by the decoder again. */
        for (S32 i = 0; i < MAX_OUTPUT_BUF_NUM; i++) {
            context->bIsBufferInDecoder[i] = MPP_TRUE;
        }

        /* report the new (rotation-adjusted) geometry to the caller */
        pstFrame->stVdecFrameInfo.stCommFrameInfo.u32Width = (U32)context->nWidth;
        pstFrame->stVdecFrameInfo.stCommFrameInfo.u32Height = (U32)context->nHeight;
    } else if (ret == MPP_CODER_NO_DATA) {
        return MPP_CODER_NO_DATA;
    } else if (ret == MPP_ERROR_FRAME) {
        // check if it is the last frame
        if (context->nEosPts > 0 && (S64)pstFrame->stVFrame.u64PTS == context->nEosPts) {
            error("it is a EOS frame eos pts:(%ld)", context->nEosPts);
            context->bEosReached = MPP_TRUE;
            return MPP_CODER_EOS;
        }
    } else if (ret == MPP_CODER_EOS) {
        context->bEosReached = MPP_TRUE;
    }

    if (ret == MPP_OK) {
        if (pstFrame->u32Idx < MAX_OUTPUT_BUF_NUM)
            context->bIsBufferInDecoder[pstFrame->u32Idx] = MPP_FALSE;
        if (pstFrame->stVdecFrameInfo.bEndOfStream)
            context->bEosReached = MPP_TRUE;
    }

    return ret;
}

S32 al_dec_return_output_frame(ALBaseContext *ctx, const VideoFrameInfo *pstFrame) {
    if (!ctx) {
        error("input para ALBaseContext is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    if (!pstFrame) {
        error("input para VideoFrameInfo is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;
    S32 ret = 0;

    S32 buf_idx = (S32)pstFrame->u32Idx;
    Buffer *buf = getBuffer(getOutputPort(context->stCodec), buf_idx);
    if (!buf) {
        error("buf is NULL, this should not happen, please check!");
    } else {
        clearBytesUsed(buf);

        // for DMABUF_EXTERNAL, set external fd before queueing
        if (getPortBufferType(getOutputPort(context->stCodec)) == MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL) {
            setExternalDmaBuf(
                buf, (S32)pstFrame->stVFrame.u32Fd[0], (U8 *)pstFrame->stVFrame.ulPlaneVirAddr[0], buf_idx);
        }

        ret = queueBuffer(getOutputPort(context->stCodec), buf);
        if (ret) {
            error("queueBuffer failed, this should not happen, please check!");
        }

        if (buf_idx >= 0 && buf_idx < MAX_OUTPUT_BUF_NUM)
            context->bIsBufferInDecoder[buf_idx] = MPP_TRUE;
    }

    return MPP_OK;
}

S32 al_dec_queue_output_buffer(ALBaseContext *ctx, const VideoFrameInfo *pstFrame) {
    if (!ctx) {
        error("input para ALBaseContext is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    if (!pstFrame) {
        error("input para VideoFrameInfo is NULL, please check!");
        return MPP_NULL_POINTER;
    }

    ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;
    S32 ret = 0;

    S32 buf_idx = (S32)pstFrame->u32Idx;

    if (buf_idx < 0 || buf_idx >= getBufNum(getOutputPort(context->stCodec))) {
        error("buf_idx(%d) is out of range, please check!", buf_idx);
        return MPP_CHECK_FAILED;
    }

    Buffer *buf = getBuffer(getOutputPort(context->stCodec), buf_idx);
    if (!buf) {
        error("buf is NULL, this should not happen, please check!");
        return MPP_NULL_POINTER;
    }

    clearBytesUsed(buf);
    setExternalDmaBuf(buf, (S32)pstFrame->stVFrame.u32Fd[0], (U8 *)pstFrame->stVFrame.ulPlaneVirAddr[0], buf_idx);

    ret = queueBuffer(getOutputPort(context->stCodec), buf);
    if (ret) {
        error("queueBuffer failed, please check!");
        return ret;
    }

    if (buf_idx < MAX_OUTPUT_BUF_NUM)
        context->bIsBufferInDecoder[buf_idx] = MPP_TRUE;

    return MPP_OK;
}

S32 al_dec_reset(ALBaseContext *ctx) {
    if (!ctx)
        return MPP_NULL_POINTER;
    ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;

    debug("Reset start ========================================");

    handleFlush(context->stCodec, MPP_FALSE);
    context->nInputQueuedNum = 0;
    context->nInputQueueLeftNum = getBufNum(getInputPort(context->stCodec));
    context->bInputEos = MPP_FALSE;
    context->bEosReached = MPP_FALSE;

    debug("Reset finish ========================================");

    return MPP_OK;
}

S32 al_dec_flush(ALBaseContext *ctx) {
    if (!ctx)
        return MPP_NULL_POINTER;
    ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;

    debug("Flush start ========================================");

    handleFlush(context->stCodec, MPP_FALSE);
    context->nInputQueuedNum = 0;
    context->nInputQueueLeftNum = getBufNum(getInputPort(context->stCodec));
    context->bInputEos = MPP_FALSE;
    context->bEosReached = MPP_FALSE;

    debug("Flush finish ========================================");

    return MPP_OK;
}

void al_dec_destory(ALBaseContext *ctx) {
    if (!ctx)
        return;
    ALLinlonv5v7DecContext *context = (ALLinlonv5v7DecContext *)ctx;
    atomic_store(&context->bIsDestoryed, true);
    debug("destory start");

    if (context->nVideoFd && context->stCodec) {
        enum v4l2_buf_type input_type = getV4l2BufType(getInputPort(context->stCodec));
        enum v4l2_buf_type output_type = getV4l2BufType(getOutputPort(context->stCodec));
        mpp_v4l2_stream_off(context->nVideoFd, &input_type);
        mpp_v4l2_stream_off(context->nVideoFd, &output_type);
        debug("stream off finish");
    }

    if (context->bPollThreadCreated) {
        /*
         * The poll loop checks bIsDestoryed on every iteration (POLL_TIMEOUT is
         * non-blocking and the loop only sleeps 10ms), so it exits promptly once
         * bIsDestoryed is set above. We must block here until the thread has
         * fully exited before destroying the codec/context below, otherwise the
         * poll thread could touch already-freed resources (use-after-free).
         */
        pthread_join(context->pollthread, NULL);
        debug("pthread join finish");
        context->bPollThreadCreated = MPP_FALSE;
    }

    if (context->nVideoFd && context->stCodec) {
        destoryCodec(context->stCodec);
        debug("destory codec finish");
        close(context->nVideoFd);
    }
    free(context);
    context = NULL;
}
