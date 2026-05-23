/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    uvc.c
 * @Date      :    2026-4-9
 * @Author    :    SPACEMIT
 * @Brief     :    UVC module implementation for MPP.
 *                 Manages UVC (USB Video Class) camera devices,
 *                 channels, and image effect controls.
 *------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <linux/videodev2.h>

#include "uvc_api.h"
#include "vb_api.h"
#include "sys_api.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== Error Codes ======================== */

#define UVC_ERR_OK 0
#define UVC_ERR_INVAL (-2)
#define UVC_ERR_NOMEM (-3)
#define UVC_ERR_NOT_INIT (-4)
#define UVC_ERR_BUSY (-5)
#define UVC_ERR_NOT_FOUND (-6)
#define UVC_ERR_EXIST (-7)
#define UVC_ERR_NOT_ENABLE (-8)
#define UVC_ERR_DOUBLE_INIT (-9)
#define UVC_ERR_NOT_CFG (-10)
#define UVC_ERR_TIMEOUT (-11)

#define UVC_DEPTH_MAX 16 /* max depth queue entries per channel */

/* ======================== Log Macros ======================== */

#define UVC_LOG_ERR(fmt, ...) fprintf(stderr, "[UVC][ERR] %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define UVC_LOG_WARN(fmt, ...) fprintf(stderr, "[UVC][WARN] %s:%d " fmt "\n", __func__, __LINE__, ##__VA_ARGS__)
#define UVC_LOG_INFO(fmt, ...) fprintf(stdout, "[UVC][INFO] " fmt "\n", ##__VA_ARGS__)

/* ======================== Internal State ======================== */

#define UVC_MAX_V4L2_BUF 16
#define UVC_DEFAULT_BUF_CNT 12 /* default number of V4L2 buffers */

typedef struct _UvcV4l2Buf {
    void *pAddr;
    U32 u32Len;
} UvcV4l2Buf;

/* Depth queue entry: holds a VB buffer handle + frame metadata */
typedef struct _UvcDepthEntry {
    UL ulBufferId;              /* VB buffer handle (ref-counted) */
    VideoFrameInfo stFrameInfo; /* snapshot of frame info at DQBUF time */
} UvcDepthEntry;

typedef struct _UvcChnCtx {
    BOOL bEnabled;
    BOOL bAttrSet;
    UvcChnAttr stChnAttr;
    /* V4L2 / VB buffer resources (per-channel) */
    U32 u32BufCnt;
    UvcV4l2Buf astBufs[UVC_MAX_V4L2_BUF];
    UL ulVbPool;
    UL aulVbBuf[UVC_MAX_V4L2_BUF];      /* VB buffer handle per slot */
    S32 as32DmaBufFd[UVC_MAX_V4L2_BUF]; /* dma-buf fd per slot */
    BOOL bVbPoolCreated;
    /* depth queue (ring buffer, protected by depthLock) */
    UvcDepthEntry astDepth[UVC_DEPTH_MAX];
    U32 u32DepthHead;  /* read index  */
    U32 u32DepthTail;  /* write index */
    U32 u32DepthCount; /* current entries */
    pthread_mutex_t depthLock;
    pthread_cond_t depthNotEmpty;
    /* capture task thread */
    pthread_t taskTid;
    BOOL bTaskRun; /* flag to stop the task */
    /* recycle thread: picks up free VB buffers and QBUFs them back to V4L2 */
    pthread_t recycleTid;
    BOOL bRecycleRun;
} UvcChnCtx;

typedef struct _UvcDevCtx {
    BOOL bEnabled;
    BOOL bAttrSet;
    UvcDevAttr stDevAttr;
    UvcEffectAttr stEffectAttr;
    BOOL bEffectSet;
    UvcChnCtx astChn[UVC_MAX_CHN_NUM];
    /* V4L2 runtime */
    S32 s32Fd;
    BOOL bStreaming;
} UvcDevCtx;

typedef struct _UvcModCtx {
    BOOL bInited;
    pthread_mutex_t lock;
    UvcDevCtx astDev[UVC_MAX_DEV_NUM];
    BOOL abDevCreated[UVC_MAX_DEV_NUM]; /* device slot allocated */
} UvcModCtx;

typedef struct _UvcTaskArg {
    UVC_DEV dev;
    UVC_CHN chn;
} UvcTaskArg;

static UvcModCtx g_stUvcCtx;
static pthread_once_t g_stUvcOnce = PTHREAD_ONCE_INIT;

/* ======================== Validation Helpers ======================== */

#define UVC_CHECK_INIT(bInited)             \
    do {                                    \
        if (!(bInited)) {                   \
            UVC_LOG_ERR("not initialized"); \
            return UVC_ERR_NOT_INIT;        \
        }                                   \
    } while (0)

#define UVC_CHECK_DEV(dev)                               \
    do {                                                 \
        if (((dev) < 0) || ((dev) >= UVC_MAX_DEV_NUM)) { \
            UVC_LOG_ERR("invalid dev %d", dev);          \
            return UVC_ERR_INVAL;                        \
        }                                                \
    } while (0)

#define UVC_CHECK_CHN(ch)                              \
    do {                                               \
        if (((ch) < 0) || ((ch) >= UVC_MAX_CHN_NUM)) { \
            UVC_LOG_ERR("invalid chn %d", ch);         \
            return UVC_ERR_INVAL;                      \
        }                                              \
    } while (0)

#define UVC_CHECK_POINTER(ptr)           \
    do {                                 \
        if (NULL == ptr) {               \
            UVC_LOG_ERR("null pointer"); \
            return UVC_ERR_INVAL;        \
        }                                \
    } while (0)

/* ======================== V4L2 Helpers ======================== */

static S32 uvc_xioctl(S32 fd, uint64_t request, void *arg) {
    S32 r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static U32 uvc_pixfmt_to_v4l2(MppPixelFormat eFmt) {
    switch (eFmt) {
        case MPP_PIXEL_FORMAT_MJPEG:
            return V4L2_PIX_FMT_MJPEG;
        case MPP_PIXEL_FORMAT_H264:
            return V4L2_PIX_FMT_H264;
        case MPP_PIXEL_FORMAT_YUYV:
            return V4L2_PIX_FMT_YUYV;
        case MPP_PIXEL_FORMAT_NV12:
            return V4L2_PIX_FMT_NV12;
        case MPP_PIXEL_FORMAT_NV21:
            return V4L2_PIX_FMT_NV21;
        case MPP_PIXEL_FORMAT_I420:
            return V4L2_PIX_FMT_YUV420;
        default:
            return V4L2_PIX_FMT_MJPEG;
    }
}

static S32 uvc_v4l2_open(UvcDevCtx *pDev) {
    S32 fd = open(pDev->stDevAttr.acDevNode, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) {
        UVC_LOG_ERR("open %s failed: %s", pDev->stDevAttr.acDevNode, strerror(errno));
        return UVC_ERR_INVAL;
    }

    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (uvc_xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        UVC_LOG_ERR("VIDIOC_QUERYCAP failed: %s", strerror(errno));
        close(fd);
        return UVC_ERR_INVAL;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        UVC_LOG_ERR("%s is not a video capture device", pDev->stDevAttr.acDevNode);
        close(fd);
        return UVC_ERR_INVAL;
    }

    pDev->s32Fd = fd;
    return UVC_ERR_OK;
}

static MppPixelFormat uvc_v4l2_to_pixfmt(U32 pixfmt) {
    switch (pixfmt) {
        case V4L2_PIX_FMT_MJPEG:
            return MPP_PIXEL_FORMAT_MJPEG;
        case V4L2_PIX_FMT_H264:
            return MPP_PIXEL_FORMAT_H264;
        case V4L2_PIX_FMT_YUYV:
            return MPP_PIXEL_FORMAT_YUYV;
        case V4L2_PIX_FMT_NV12:
            return MPP_PIXEL_FORMAT_NV12;
        case V4L2_PIX_FMT_NV21:
            return MPP_PIXEL_FORMAT_NV21;
        case V4L2_PIX_FMT_YUV420:
            return MPP_PIXEL_FORMAT_I420;
        default:
            return MPP_PIXEL_FORMAT_UNKNOWN;
    }
}

/**
 * @brief Negotiate format with V4L2 driver.
 *        Sends the requested parameters via VIDIOC_S_FMT and accepts
 *        whatever the driver returns. The actually negotiated values
 *        are written back into *pNegotiated so the caller can update
 *        its channel attributes.
 */
static S32 uvc_v4l2_set_fmt(UvcDevCtx *pDev, const UvcChnAttr *pAttr, UvcChnAttr *pNegotiated, U32 *pu32SizeImage) {
    U32 pixfmt = uvc_pixfmt_to_v4l2(pAttr->ePixelFormat);

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = pAttr->u32Width;
    fmt.fmt.pix.height = pAttr->u32Height;
    fmt.fmt.pix.pixelformat = pixfmt;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (uvc_xioctl(pDev->s32Fd, VIDIOC_S_FMT, &fmt) < 0) {
        UVC_LOG_ERR("VIDIOC_S_FMT failed: %s", strerror(errno));
        return UVC_ERR_INVAL;
    }

    /* log if driver adjusted any parameter */
    if (fmt.fmt.pix.pixelformat != pixfmt || fmt.fmt.pix.width != pAttr->u32Width ||
        fmt.fmt.pix.height != pAttr->u32Height) {
        UVC_LOG_WARN(
            "negotiated: requested %ux%u fmt=0x%x, got %ux%u fmt=0x%x",
            pAttr->u32Width,
            pAttr->u32Height,
            pixfmt,
            fmt.fmt.pix.width,
            fmt.fmt.pix.height,
            fmt.fmt.pix.pixelformat);
    }

    /* convert negotiated V4L2 pixfmt back to MppPixelFormat */
    MppPixelFormat eNegFmt = uvc_v4l2_to_pixfmt(fmt.fmt.pix.pixelformat);
    if (eNegFmt == MPP_PIXEL_FORMAT_UNKNOWN) {
        UVC_LOG_ERR("driver returned unsupported pixfmt 0x%x", fmt.fmt.pix.pixelformat);
        return UVC_ERR_INVAL;
    }

    /* write back negotiated values */
    *pNegotiated = *pAttr;
    pNegotiated->u32Width = fmt.fmt.pix.width;
    pNegotiated->u32Height = fmt.fmt.pix.height;
    pNegotiated->ePixelFormat = eNegFmt;

    /* return the driver-reported frame buffer size */
    if (pu32SizeImage)
        *pu32SizeImage = fmt.fmt.pix.sizeimage;

    /* negotiate frame rate */
    if (pAttr->u32Fps > 0) {
        struct v4l2_streamparm parm;
        memset(&parm, 0, sizeof(parm));
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = pAttr->u32Fps;
        if (uvc_xioctl(pDev->s32Fd, VIDIOC_S_PARM, &parm) < 0) {
            UVC_LOG_WARN("VIDIOC_S_PARM fps=%u failed: %s (non-fatal)", pAttr->u32Fps, strerror(errno));
        } else if (parm.parm.capture.timeperframe.numerator > 0) {
            pNegotiated->u32Fps = parm.parm.capture.timeperframe.denominator / parm.parm.capture.timeperframe.numerator;
        }
    }

    return UVC_ERR_OK;
}

static S32 uvc_v4l2_req_bufs(UvcDevCtx *pDev, UvcChnCtx *pChn, U32 u32FrameSize) {
    U32 u32BufCnt = UVC_DEFAULT_BUF_CNT;

    /* --- Create VB pool to manage frame buffers --- */
    VbPoolCfg stPoolCfg;
    memset(&stPoolCfg, 0, sizeof(stPoolCfg));
    stPoolCfg.u32BufSize = u32FrameSize;
    stPoolCfg.u32BufCnt = u32BufCnt;
    stPoolCfg.eModId = MPP_ID_UVC;
    stPoolCfg.eRemapMode = VBUF_REMAP_MODE_CACHED;

    pChn->ulVbPool = VB_CreatePool(&stPoolCfg);
    if (pChn->ulVbPool == 0) {
        UVC_LOG_ERR("VB_CreatePool failed (size=%u cnt=%u)", u32FrameSize, u32BufCnt);
        return UVC_ERR_NOMEM;
    }
    pChn->bVbPoolCreated = MPP_TRUE;

    UVC_LOG_INFO("VB pool created: pool=%lu, bufSize=%u, bufCnt=%u", pChn->ulVbPool, u32FrameSize, u32BufCnt);

    /* Get VB buffers and their dma-buf fds for DMABUF mode */
    for (U32 i = 0; i < u32BufCnt; i++) {
        pChn->aulVbBuf[i] = VB_GetBuffer(pChn->ulVbPool, 0);
        if (pChn->aulVbBuf[i] == 0) {
            UVC_LOG_ERR("VB_GetBuffer[%u] failed", i);
            goto err_destroy_pool;
        }

        /* get dma-buf fd for V4L2 DMABUF mode */
        S32 s32DmaBufFd = -1;
        S32 ret = VB_GetDmaBufFd(pChn->aulVbBuf[i], &s32DmaBufFd);
        if (ret != 0 || s32DmaBufFd < 0) {
            UVC_LOG_ERR("VB_GetDmaBufFd[%u] failed (ret=%d)", i, ret);
            goto err_destroy_pool;
        }
        pChn->as32DmaBufFd[i] = s32DmaBufFd;

        /* also get virtual address for CPU access (reading frame data) */
        VOID *pVirAddr = NULL;
        ret = VB_GetVirAddr(pChn->aulVbBuf[i], &pVirAddr);
        if (ret != 0 || !pVirAddr) {
            UVC_LOG_ERR("VB_GetVirAddr[%u] failed", i);
            goto err_destroy_pool;
        }

        pChn->astBufs[i].pAddr = pVirAddr;
        pChn->astBufs[i].u32Len = u32FrameSize;
    }

    /* --- Request V4L2 DMABUF buffers --- */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = u32BufCnt;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_DMABUF;

    if (uvc_xioctl(pDev->s32Fd, VIDIOC_REQBUFS, &req) < 0) {
        UVC_LOG_ERR("VIDIOC_REQBUFS(DMABUF) failed: %s", strerror(errno));
        goto err_destroy_pool;
    }

    if (req.count < 2) {
        UVC_LOG_ERR("insufficient buffer count: %u", req.count);
        goto err_destroy_pool;
    }

    pChn->u32BufCnt = (req.count < u32BufCnt) ? req.count : u32BufCnt;

    return UVC_ERR_OK;

err_destroy_pool:
    for (U32 i = 0; i < u32BufCnt; i++) {
        if (pChn->aulVbBuf[i] != 0)
            VB_ReleaseBuffer(pChn->aulVbBuf[i]);
    }
    VB_DestroyPool(pChn->ulVbPool);
    pChn->bVbPoolCreated = MPP_FALSE;
    return UVC_ERR_NOMEM;
}

static S32 uvc_v4l2_stream_on(UvcDevCtx *pDev, UvcChnCtx *pChn) {
    /* queue all DMABUF buffers backed by VB dma-buf fds */
    for (U32 i = 0; i < pChn->u32BufCnt; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_DMABUF;
        buf.index = i;
        buf.m.fd = pChn->as32DmaBufFd[i];
        buf.length = pChn->astBufs[i].u32Len;

        if (uvc_xioctl(pDev->s32Fd, VIDIOC_QBUF, &buf) < 0) {
            UVC_LOG_ERR("VIDIOC_QBUF[%u] DMABUF failed: %s", i, strerror(errno));
            return UVC_ERR_INVAL;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (uvc_xioctl(pDev->s32Fd, VIDIOC_STREAMON, &type) < 0) {
        UVC_LOG_ERR("VIDIOC_STREAMON failed: %s", strerror(errno));
        return UVC_ERR_INVAL;
    }

    pDev->bStreaming = MPP_TRUE;
    return UVC_ERR_OK;
}

static S32 uvc_v4l2_stream_off(UvcDevCtx *pDev) {
    if (!pDev->bStreaming)
        return UVC_ERR_OK;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (uvc_xioctl(pDev->s32Fd, VIDIOC_STREAMOFF, &type) < 0) {
        UVC_LOG_WARN("VIDIOC_STREAMOFF failed: %s", strerror(errno));
    }

    pDev->bStreaming = MPP_FALSE;
    return UVC_ERR_OK;
}

static VOID uvc_v4l2_release_bufs(UvcDevCtx *pDev, UvcChnCtx *pChn) {
    /* release V4L2 kernel buffers */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 0;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_DMABUF;
    uvc_xioctl(pDev->s32Fd, VIDIOC_REQBUFS, &req);

    /* release VB buffers and destroy pool */
    if (pChn->bVbPoolCreated) {
        for (U32 i = 0; i < pChn->u32BufCnt; i++) {
            if (pChn->aulVbBuf[i] != 0) {
                VB_ReleaseBuffer(pChn->aulVbBuf[i]);
                pChn->aulVbBuf[i] = 0;
            }
            pChn->as32DmaBufFd[i] = -1;
        }
        VB_DestroyPool(pChn->ulVbPool);
        pChn->ulVbPool = 0;
        pChn->bVbPoolCreated = MPP_FALSE;
        UVC_LOG_INFO("VB pool destroyed");
    }

    for (U32 i = 0; i < UVC_MAX_V4L2_BUF; i++) {
        pChn->astBufs[i].pAddr = NULL;
        pChn->astBufs[i].u32Len = 0;
    }
    pChn->u32BufCnt = 0;
}

static VOID uvc_v4l2_close(UvcDevCtx *pDev) {
    if (pDev->s32Fd >= 0) {
        close(pDev->s32Fd);
        pDev->s32Fd = -1;
    }
}

static S32 uvc_v4l2_set_ctrl(S32 fd, U32 id, S32 value) {
    struct v4l2_control ctrl;
    memset(&ctrl, 0, sizeof(ctrl));
    ctrl.id = id;
    ctrl.value = value;
    if (uvc_xioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        UVC_LOG_WARN("VIDIOC_S_CTRL id=0x%x val=%d failed: %s (non-fatal)", id, value, strerror(errno));
        return UVC_ERR_INVAL;
    }
    return UVC_ERR_OK;
}

/* ======================== Depth Queue Helpers ======================== */

/**
 * @brief Build a VideoFrameInfo from a DQBUF result.
 *        Shared by the task thread (for depth queue / SYS_SendFrame)
 *        and the legacy direct-DQBUF path.
 */
static VOID uvc_fill_frame_info(
    UvcDevCtx *pDev, UvcChnCtx *pChn, const struct v4l2_buffer *pBuf, VideoFrameInfo *pOut
) {
    memset(pOut, 0, sizeof(VideoFrameInfo));
    pOut->eFrameType = FRAME_TYPE_COMMON;
    pOut->eModId = MPP_ID_UVC;
    pOut->u32Idx = pBuf->index;

    if (pBuf->index < pChn->u32BufCnt) {
        pOut->ulPoolId = pChn->ulVbPool;
        pOut->ulBufferId = pChn->aulVbBuf[pBuf->index];

        VideoFrame *pVF = &pOut->stVFrame;
        pVF->u32PlaneNum = 1;
        pVF->ulPlaneVirAddr[0] = (UL)pChn->astBufs[pBuf->index].pAddr;
        pVF->u32PlaneSize[0] = pChn->astBufs[pBuf->index].u32Len;
        pVF->u32PlaneSizeValid[0] = pBuf->bytesused;
        pVF->u32TotalSize = pChn->astBufs[pBuf->index].u32Len;
        pVF->u32Fd[0] = (UL)pChn->as32DmaBufFd[pBuf->index];

        U64 u64PhyAddr = 0;
        if (pChn->aulVbBuf[pBuf->index] != 0)
            VB_GetPhyAddr(pChn->aulVbBuf[pBuf->index], &u64PhyAddr);
        pVF->u64PlanePhyAddr[0] = u64PhyAddr;
    }

    pOut->stVFrame.u64PTS = (U64)pBuf->timestamp.tv_sec * 1000000ULL + (U64)pBuf->timestamp.tv_usec;
    pOut->stVFrame.u32FrameFlag = pBuf->sequence;

    pOut->stCommFrameInfo.u32Width = pChn->stChnAttr.u32Width;
    pOut->stCommFrameInfo.u32Height = pChn->stChnAttr.u32Height;
    pOut->stCommFrameInfo.ePixelFormat = pChn->stChnAttr.ePixelFormat;
}

/**
 * @brief Re-queue a V4L2 DMABUF buffer back to the driver.
 */
static S32 uvc_v4l2_qbuf(UvcDevCtx *pDev, UvcChnCtx *pChn, U32 u32BufIdx) {
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.index = u32BufIdx;
    buf.m.fd = pChn->as32DmaBufFd[u32BufIdx];
    buf.length = pChn->astBufs[u32BufIdx].u32Len;

    if (uvc_xioctl(pDev->s32Fd, VIDIOC_QBUF, &buf) < 0) {
        UVC_LOG_ERR("VIDIOC_QBUF[%u] failed: %s", u32BufIdx, strerror(errno));
        return UVC_ERR_INVAL;
    }
    return UVC_ERR_OK;
}

/* ======================== Capture Task Thread ======================== */

/**
 * @brief Recycle thread.
 *
 * Waits for VB buffers whose refcount has dropped to 0 (returned to
 * the pool by all consumers).  When VB_GetBuffer succeeds, the buffer
 * is free — we QBUF it back to V4L2 so the driver can fill it again.
 * The VB_GetBuffer call gives us ref=1, which represents "V4L2 owns
 * this buffer".
 */
static void *uvc_recycle_task(void *arg) {
    UvcTaskArg *pArg = (UvcTaskArg *)arg;
    UVC_DEV dev = pArg->dev;
    UVC_CHN chn = pArg->chn;
    free(pArg);

    UvcDevCtx *pDev = &g_stUvcCtx.astDev[dev];
    UvcChnCtx *pChn = &pDev->astChn[chn];

    UVC_LOG_INFO("recycle task started: dev %d chn %d pool=%lu", dev, chn, pChn->ulVbPool);

    while (pChn->bRecycleRun) {
        /* Block up to 100ms waiting for a free buffer in the pool.
         * VB_GetBuffer returns when some consumer's VB_ReleaseBuffer
         * drops refcount to 0, putting the buffer back on the free list. */
        UL ulBuf = VB_GetBuffer(pChn->ulVbPool, 100);
        if (ulBuf == 0)
            continue; /* timeout or shutting down */

        /* Find which V4L2 slot this VB handle belongs to */
        U32 slot = (U32)-1;
        for (U32 i = 0; i < pChn->u32BufCnt; i++) {
            if (pChn->aulVbBuf[i] == ulBuf) {
                slot = i;
                break;
            }
        }
        if (slot == (U32)-1) {
            UVC_LOG_ERR("recycle: unknown VB handle %lu", ulBuf);
            VB_ReleaseBuffer(ulBuf);
            continue;
        }

        /* QBUF back to V4L2.  We keep ref=1 (from VB_GetBuffer) to
         * represent "V4L2 driver holds this buffer". */
        pthread_mutex_lock(&g_stUvcCtx.lock);
        uvc_v4l2_qbuf(pDev, pChn, slot);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
    }

    UVC_LOG_INFO("recycle task exiting: dev %d chn %d", dev, chn);
    return NULL;
}

/**
 * @brief Check if pixel format is a compressed stream (MJPEG / H264).
 *        Returns the corresponding MppStreamCodecType, or
 *        MPP_STREAM_CODEC_UNKNOWN for raw pixel formats.
 */
static MppStreamCodecType uvc_pixfmt_to_codec(MppPixelFormat eFmt) {
    switch (eFmt) {
        case MPP_PIXEL_FORMAT_MJPEG:
            return MPP_STREAM_CODEC_MJPEG;
        case MPP_PIXEL_FORMAT_H264:
            return MPP_STREAM_CODEC_H264;
        default:
            return MPP_STREAM_CODEC_UNKNOWN;
    }
}

/**
 * @brief Capture task thread.
 *
 * Continuously DQBUFs from V4L2, then:
 *   1. For compressed formats (MJPEG/H264): SYS_SendStream.
 *      For raw formats (NV12/YUYV/...):     SYS_SendFrame.
 *   2. If depth > 0, VB_RefAdd and push into the depth queue.
 *   3. Release the "V4L2 base ref" via VB_ReleaseBuffer.
 *
 * The buffer is NOT directly re-queued to V4L2 here.  When ALL
 * consumers (SYS sinks + depth queue user) have called
 * VB_ReleaseBuffer, the VB refcount drops to 0, the buffer returns
 * to the pool, and the recycle thread picks it up and QBUFs it.
 */
static void *uvc_capture_task(void *arg) {
    UvcTaskArg *pArg = (UvcTaskArg *)arg;
    UVC_DEV dev = pArg->dev;
    UVC_CHN chn = pArg->chn;
    free(pArg);

    UvcDevCtx *pDev = &g_stUvcCtx.astDev[dev];
    UvcChnCtx *pChn = &pDev->astChn[chn];
    S32 fd = pDev->s32Fd;
    U32 u32Depth = pChn->stChnAttr.u32Depth;

    MppNode stSrcNode;
    stSrcNode.eModId = MPP_ID_UVC;
    stSrcNode.s32DevId = dev;
    stSrcNode.s32ChnId = chn;

    MppPixelFormat ePixFmt = pChn->stChnAttr.ePixelFormat;
    BOOL bCompressed = (ePixFmt == MPP_PIXEL_FORMAT_MJPEG || ePixFmt == MPP_PIXEL_FORMAT_H264);

    UVC_LOG_INFO("capture task started: dev %d chn %d depth=%u compressed=%d", dev, chn, u32Depth, bCompressed);

    while (pChn->bTaskRun) {
        /* poll with 100ms timeout so we can check bTaskRun periodically */
        fd_set fds;
        struct timeval tv;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000; /* 100 ms */

        S32 r = select(fd + 1, &fds, NULL, NULL, &tv);
        if (r <= 0)
            continue; /* timeout or EINTR — loop back */

        /* DQBUF — the buffer currently has ref=1 ("V4L2 base ref") */
        struct v4l2_buffer v4l2buf;
        memset(&v4l2buf, 0, sizeof(v4l2buf));
        v4l2buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2buf.memory = V4L2_MEMORY_DMABUF;

        pthread_mutex_lock(&g_stUvcCtx.lock);
        if (uvc_xioctl(fd, VIDIOC_DQBUF, &v4l2buf) < 0) {
            pthread_mutex_unlock(&g_stUvcCtx.lock);
            if (errno == EAGAIN)
                continue;
            UVC_LOG_ERR("task DQBUF failed: %s", strerror(errno));
            break;
        }

        /* build frame info */
        VideoFrameInfo stFrame;
        uvc_fill_frame_info(pDev, pChn, &v4l2buf, &stFrame);
        UL ulBuf = stFrame.ulBufferId;
        VB_UpdateBufferFrameInfo(ulBuf, &stFrame);
        pthread_mutex_unlock(&g_stUvcCtx.lock);

        /*
         * At this point ref=1 (the "V4L2 base ref" from the initial
         * VB_GetBuffer or the recycle thread's VB_GetBuffer).
         *
         * SYS_SendFrame internally does VB_RefAdd for each bound sink,
         * and each sink will eventually VB_ReleaseBuffer.
         *
         * If depth > 0, we VB_RefAdd once for the depth queue consumer.
         *
         * Finally we VB_ReleaseBuffer to drop the base ref.  When all
         * consumers are done, refcount reaches 0, buffer goes back to
         * the pool, and the recycle thread QBUFs it to V4L2.
         */

        /* --- 1. Send to bound sinks --- */
        if (bCompressed) {
            /* MJPEG / H264: send as compressed stream */
            StreamBufferInfo stStreamInfo;
            memset(&stStreamInfo, 0, sizeof(stStreamInfo));
            stStreamInfo.pu8Addr = (const U8 *)stFrame.stVFrame.ulPlaneVirAddr[0];
            stStreamInfo.u32Size = stFrame.stVFrame.u32PlaneSizeValid[0];
            stStreamInfo.u64PTS = stFrame.stVFrame.u64PTS;
            stStreamInfo.u32Width = pChn->stChnAttr.u32Width;
            stStreamInfo.u32Height = pChn->stChnAttr.u32Height;
            stStreamInfo.bKeyFrame = MPP_TRUE;
            stStreamInfo.bEndOfStream = MPP_FALSE;
            stStreamInfo.eCodecType =
                (ePixFmt == MPP_PIXEL_FORMAT_H264) ? MPP_STREAM_CODEC_H264 : MPP_STREAM_CODEC_MJPEG;
            SYS_SendStream(&stSrcNode, &stStreamInfo);
        } else {
            /* Raw YUV: send as frame (zero-copy via VB ref) */
            SYS_SendFrame(&stSrcNode, ulBuf);
        }

        /* --- 2. Push into depth queue if depth > 0 --- */
        if (u32Depth > 0 && ulBuf != 0) {
            /* add a ref for the depth queue consumer */
            VB_RefAdd(ulBuf);

            pthread_mutex_lock(&pChn->depthLock);

            if (pChn->u32DepthCount >= u32Depth) {
                /* queue full — drop oldest, release its ref.
                 * Do NOT QBUF here; the recycle thread handles that
                 * when the VB refcount reaches 0. */
                UvcDepthEntry *pOld = &pChn->astDepth[pChn->u32DepthHead];
                VB_ReleaseBuffer(pOld->ulBufferId);
                pChn->u32DepthHead = (pChn->u32DepthHead + 1) % UVC_DEPTH_MAX;
                pChn->u32DepthCount--;
            }

            UvcDepthEntry *pNew = &pChn->astDepth[pChn->u32DepthTail];
            pNew->ulBufferId = ulBuf;
            memcpy(&pNew->stFrameInfo, &stFrame, sizeof(VideoFrameInfo));
            pChn->u32DepthTail = (pChn->u32DepthTail + 1) % UVC_DEPTH_MAX;
            pChn->u32DepthCount++;

            pthread_cond_signal(&pChn->depthNotEmpty);
            pthread_mutex_unlock(&pChn->depthLock);
        }

        /* --- 3. Release the V4L2 base ref --- */
        VB_ReleaseBuffer(ulBuf);
    }

    UVC_LOG_INFO("capture task exiting: dev %d chn %d", dev, chn);
    return NULL;
}

/* ======================== UVC API Implementation ======================== */

static VOID uvc_init_once(VOID) {
    memset(&g_stUvcCtx, 0, sizeof(UvcModCtx));

    /* init all device fds to -1 and per-channel dma-buf fds to -1 */
    for (S32 d = 0; d < UVC_MAX_DEV_NUM; d++) {
        g_stUvcCtx.astDev[d].s32Fd = -1;
        for (S32 c = 0; c < UVC_MAX_CHN_NUM; c++) {
            for (S32 i = 0; i < UVC_MAX_V4L2_BUF; i++)
                g_stUvcCtx.astDev[d].astChn[c].as32DmaBufFd[i] = -1;
        }
    }

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_stUvcCtx.lock, &attr);
    pthread_mutexattr_destroy(&attr);

    g_stUvcCtx.bInited = MPP_TRUE;

    UVC_LOG_INFO("UVC_Init done");
}

S32 UVC_Init(VOID) {
    pthread_once(&g_stUvcOnce, uvc_init_once);
    return UVC_ERR_OK;
}

S32 UVC_Exit(VOID) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);

    pthread_mutex_lock(&g_stUvcCtx.lock);

    /* warn about devices still created/enabled, force cleanup V4L2 resources */
    for (S32 d = 0; d < UVC_MAX_DEV_NUM; d++) {
        UvcDevCtx *pDev = &g_stUvcCtx.astDev[d];
        if (pDev->bEnabled) {
            UVC_LOG_WARN("dev %d still enabled at exit, force disable", d);
            for (S32 c = 0; c < UVC_MAX_CHN_NUM; c++) {
                UvcChnCtx *pChn = &pDev->astChn[c];
                if (pChn->bEnabled) {
                    UVC_LOG_WARN("dev %d chn %d still enabled at exit", d, c);
                    pChn->bEnabled = MPP_FALSE;
                }
                uvc_v4l2_release_bufs(pDev, pChn);
            }
            uvc_v4l2_stream_off(pDev);
            uvc_v4l2_close(pDev);
            pDev->bEnabled = MPP_FALSE;
        }
        g_stUvcCtx.abDevCreated[d] = MPP_FALSE;
    }

    pthread_mutex_unlock(&g_stUvcCtx.lock);
    pthread_mutex_destroy(&g_stUvcCtx.lock);

    memset(&g_stUvcCtx, 0, sizeof(UvcModCtx));

    /* allow re-initialization via UVC_Init */
    g_stUvcOnce = PTHREAD_ONCE_INIT;

    UVC_LOG_INFO("UVC_Exit done");
    return UVC_ERR_OK;
}

/* ======================== Create / Destroy Device ======================== */

S32 UVC_CreateDev(UVC_DEV dev, const UvcDevAttr *pstDevAttr) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);
    UVC_CHECK_DEV(dev);
    UVC_CHECK_POINTER(pstDevAttr);

    pthread_mutex_lock(&g_stUvcCtx.lock);

    if (g_stUvcCtx.abDevCreated[dev]) {
        UVC_LOG_ERR("dev %d already created", dev);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_EXIST;
    }

    UvcDevCtx *pDevCtx = &g_stUvcCtx.astDev[dev];
    memset(pDevCtx, 0, sizeof(UvcDevCtx));
    pDevCtx->s32Fd = -1;
    memcpy(&pDevCtx->stDevAttr, pstDevAttr, sizeof(UvcDevAttr));
    pDevCtx->bAttrSet = MPP_TRUE;

    g_stUvcCtx.abDevCreated[dev] = MPP_TRUE;

    pthread_mutex_unlock(&g_stUvcCtx.lock);

    UVC_LOG_INFO("UVC_CreateDev: dev %d created, node=%s", dev, pstDevAttr->acDevNode);
    return UVC_ERR_OK;
}

S32 UVC_DestroyDev(UVC_DEV dev) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);
    UVC_CHECK_DEV(dev);

    pthread_mutex_lock(&g_stUvcCtx.lock);

    if (!g_stUvcCtx.abDevCreated[dev]) {
        UVC_LOG_ERR("dev %d not created", dev);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_NOT_FOUND;
    }

    UvcDevCtx *pDev = &g_stUvcCtx.astDev[dev];
    if (pDev->bEnabled) {
        UVC_LOG_ERR("dev %d still enabled, disable it first", dev);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_BUSY;
    }

    memset(pDev, 0, sizeof(UvcDevCtx));
    pDev->s32Fd = -1;
    g_stUvcCtx.abDevCreated[dev] = MPP_FALSE;

    pthread_mutex_unlock(&g_stUvcCtx.lock);

    UVC_LOG_INFO("UVC_DestroyDev: dev %d destroyed", dev);
    return UVC_ERR_OK;
}

/* ======================== Device Enable / Disable ======================== */

S32 UVC_EnableDev(UVC_DEV dev) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);
    UVC_CHECK_DEV(dev);

    pthread_mutex_lock(&g_stUvcCtx.lock);

    UvcDevCtx *pDev = &g_stUvcCtx.astDev[dev];
    if (!pDev->bAttrSet) {
        UVC_LOG_ERR("dev %d attr not set", dev);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_NOT_CFG;
    }
    if (pDev->bEnabled) {
        UVC_LOG_ERR("dev %d already enabled", dev);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_BUSY;
    }

    /* open V4L2 device node */
    S32 ret = uvc_v4l2_open(pDev);
    if (ret != UVC_ERR_OK) {
        UVC_LOG_ERR("dev %d open V4L2 device failed", dev);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return ret;
    }

    pDev->bEnabled = MPP_TRUE;

    pthread_mutex_unlock(&g_stUvcCtx.lock);

    UVC_LOG_INFO("UVC_EnableDev: dev %d enabled, node=%s", dev, pDev->stDevAttr.acDevNode);
    return UVC_ERR_OK;
}

S32 UVC_DisableDev(UVC_DEV dev) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);
    UVC_CHECK_DEV(dev);

    pthread_mutex_lock(&g_stUvcCtx.lock);

    UvcDevCtx *pDev = &g_stUvcCtx.astDev[dev];
    if (!pDev->bEnabled) {
        UVC_LOG_ERR("dev %d not enabled", dev);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_NOT_ENABLE;
    }

    /* check if any channel still enabled */
    for (S32 c = 0; c < UVC_MAX_CHN_NUM; c++) {
        if (pDev->astChn[c].bEnabled) {
            UVC_LOG_ERR("dev %d chn %d still enabled, disable it first", dev, c);
            pthread_mutex_unlock(&g_stUvcCtx.lock);
            return UVC_ERR_BUSY;
        }
    }

    /* stop streaming, close device fd.
     * VB buffers are already released per-channel in UVC_DisableChn. */
    uvc_v4l2_stream_off(pDev);
    uvc_v4l2_close(pDev);

    pDev->bEnabled = MPP_FALSE;

    pthread_mutex_unlock(&g_stUvcCtx.lock);

    UVC_LOG_INFO("UVC_DisableDev: dev %d disabled", dev);
    return UVC_ERR_OK;
}

/* ======================== Channel Attributes ======================== */

S32 UVC_SetChnAttr(UVC_DEV dev, UVC_CHN chn, const UvcChnAttr *pstChnAttr) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);
    UVC_CHECK_DEV(dev);
    UVC_CHECK_CHN(chn);
    UVC_CHECK_POINTER(pstChnAttr);
    if (pstChnAttr->u32Width == 0 || pstChnAttr->u32Height == 0) {
        UVC_LOG_ERR("invalid resolution %ux%u", pstChnAttr->u32Width, pstChnAttr->u32Height);
        return UVC_ERR_INVAL;
    }
    if (pstChnAttr->ePixelFormat == MPP_PIXEL_FORMAT_UNKNOWN) {
        UVC_LOG_ERR("invalid pixel format %d", pstChnAttr->ePixelFormat);
        return UVC_ERR_INVAL;
    }

    pthread_mutex_lock(&g_stUvcCtx.lock);

    UvcDevCtx *pDev = &g_stUvcCtx.astDev[dev];
    if (!pDev->bAttrSet) {
        UVC_LOG_ERR("dev %d attr not set", dev);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_NOT_CFG;
    }

    UvcChnCtx *pChn = &pDev->astChn[chn];
    if (pChn->bEnabled) {
        UVC_LOG_ERR("dev %d chn %d already enabled, cannot set attr", dev, chn);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_BUSY;
    }

    memcpy(&pChn->stChnAttr, pstChnAttr, sizeof(UvcChnAttr));
    pChn->bAttrSet = MPP_TRUE;

    pthread_mutex_unlock(&g_stUvcCtx.lock);

    UVC_LOG_INFO(
        "UVC_SetChnAttr: dev %d chn %d, %ux%u pixfmt=%d fps=%u",
        dev,
        chn,
        pstChnAttr->u32Width,
        pstChnAttr->u32Height,
        pstChnAttr->ePixelFormat,
        pstChnAttr->u32Fps);
    return UVC_ERR_OK;
}

S32 UVC_GetChnAttr(UVC_DEV dev, UVC_CHN chn, UvcChnAttr *pstChnAttr) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);
    UVC_CHECK_DEV(dev);
    UVC_CHECK_CHN(chn);
    UVC_CHECK_POINTER(pstChnAttr);

    pthread_mutex_lock(&g_stUvcCtx.lock);

    UvcChnCtx *pChn = &g_stUvcCtx.astDev[dev].astChn[chn];
    if (!pChn->bAttrSet) {
        UVC_LOG_ERR("dev %d chn %d attr not set", dev, chn);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_NOT_CFG;
    }

    memcpy(pstChnAttr, &pChn->stChnAttr, sizeof(UvcChnAttr));

    pthread_mutex_unlock(&g_stUvcCtx.lock);
    return UVC_ERR_OK;
}

/* ======================== Channel Enable / Disable ======================== */

S32 UVC_EnableChn(UVC_DEV dev, UVC_CHN chn) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);
    UVC_CHECK_DEV(dev);
    UVC_CHECK_CHN(chn);

    pthread_mutex_lock(&g_stUvcCtx.lock);

    UvcDevCtx *pDev = &g_stUvcCtx.astDev[dev];
    if (!pDev->bEnabled) {
        UVC_LOG_ERR("dev %d not enabled", dev);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_NOT_ENABLE;
    }

    UvcChnCtx *pChn = &pDev->astChn[chn];
    if (!pChn->bAttrSet) {
        UVC_LOG_ERR("dev %d chn %d attr not set", dev, chn);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_NOT_CFG;
    }
    if (pChn->bEnabled) {
        UVC_LOG_ERR("dev %d chn %d already enabled", dev, chn);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_BUSY;
    }

    /* negotiate format with driver, then request buffers and start streaming */
    UvcChnAttr stNegotiated;
    U32 u32SizeImage = 0;
    S32 ret = uvc_v4l2_set_fmt(pDev, &pChn->stChnAttr, &stNegotiated, &u32SizeImage);
    if (ret != UVC_ERR_OK) {
        UVC_LOG_ERR("dev %d chn %d set format failed", dev, chn);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return ret;
    }

    /* write back the actually negotiated parameters (preserve u32Depth) */
    U32 u32Depth = pChn->stChnAttr.u32Depth;
    memcpy(&pChn->stChnAttr, &stNegotiated, sizeof(UvcChnAttr));
    pChn->stChnAttr.u32Depth = u32Depth;

    /* fallback: if driver didn't report sizeimage, estimate it */
    if (u32SizeImage == 0)
        u32SizeImage = stNegotiated.u32Width * stNegotiated.u32Height * 2;

    UVC_LOG_INFO("UVC_EnableChn: sizeimage=%u for %ux%u", u32SizeImage, stNegotiated.u32Width, stNegotiated.u32Height);

    ret = uvc_v4l2_req_bufs(pDev, pChn, u32SizeImage);
    if (ret != UVC_ERR_OK) {
        UVC_LOG_ERR("dev %d chn %d request buffers failed", dev, chn);
        goto err_unlock;
    }

    ret = uvc_v4l2_stream_on(pDev, pChn);
    if (ret != UVC_ERR_OK) {
        UVC_LOG_ERR("dev %d chn %d stream on failed", dev, chn);
        goto err_release_bufs;
    }

    /* initialize depth queue */
    pChn->u32DepthHead = 0;
    pChn->u32DepthTail = 0;
    pChn->u32DepthCount = 0;
    pthread_mutex_init(&pChn->depthLock, NULL);
    pthread_cond_init(&pChn->depthNotEmpty, NULL);

    pChn->bEnabled = MPP_TRUE;

    /* start recycle thread (per-channel) */
    pChn->bRecycleRun = MPP_TRUE;
    UvcTaskArg *pRecycleArg = (UvcTaskArg *)malloc(sizeof(UvcTaskArg));
    if (!pRecycleArg) {
        UVC_LOG_ERR("malloc recycle arg failed");
        pChn->bRecycleRun = MPP_FALSE;
        ret = UVC_ERR_NOMEM;
        goto err_disable_chn;
    }
    pRecycleArg->dev = dev;
    pRecycleArg->chn = chn;

    ret = pthread_create(&pChn->recycleTid, NULL, uvc_recycle_task, pRecycleArg);
    if (ret != 0) {
        UVC_LOG_ERR("recycle thread create failed: %s", strerror(ret));
        free(pRecycleArg);
        pChn->bRecycleRun = MPP_FALSE;
        ret = UVC_ERR_NOMEM;
        goto err_disable_chn;
    }

    /* start capture task thread */
    pChn->bTaskRun = MPP_TRUE;
    UvcTaskArg *pTaskArg = (UvcTaskArg *)malloc(sizeof(UvcTaskArg));
    if (!pTaskArg) {
        UVC_LOG_ERR("malloc task arg failed");
        ret = UVC_ERR_NOMEM;
        goto err_disable_chn;
    }
    pTaskArg->dev = dev;
    pTaskArg->chn = chn;

    ret = pthread_create(&pChn->taskTid, NULL, uvc_capture_task, pTaskArg);
    if (ret != 0) {
        UVC_LOG_ERR("pthread_create failed: %s", strerror(ret));
        free(pTaskArg);
        ret = UVC_ERR_NOMEM;
        goto err_disable_chn;
    }

    pthread_mutex_unlock(&g_stUvcCtx.lock);

    UVC_LOG_INFO("UVC_EnableChn: dev %d chn %d enabled, depth=%u", dev, chn, u32Depth);
    return UVC_ERR_OK;

err_disable_chn:
    pChn->bEnabled = MPP_FALSE;
err_release_bufs:
    uvc_v4l2_stream_off(pDev);
    uvc_v4l2_release_bufs(pDev, pChn);
err_unlock:
    pthread_mutex_unlock(&g_stUvcCtx.lock);
    return ret;
}

S32 UVC_DisableChn(UVC_DEV dev, UVC_CHN chn) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);
    UVC_CHECK_DEV(dev);
    UVC_CHECK_CHN(chn);

    pthread_mutex_lock(&g_stUvcCtx.lock);

    UvcChnCtx *pChn = &g_stUvcCtx.astDev[dev].astChn[chn];
    if (!pChn->bEnabled) {
        UVC_LOG_ERR("dev %d chn %d not enabled", dev, chn);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_NOT_ENABLE;
    }

    /* signal task thread to stop */
    pChn->bTaskRun = MPP_FALSE;
    pthread_mutex_unlock(&g_stUvcCtx.lock);

    /* join the task thread (it polls with 100ms timeout, will exit soon) */
    pthread_join(pChn->taskTid, NULL);

    pthread_mutex_lock(&g_stUvcCtx.lock);

    /* flush depth queue — only release VB refs, recycle thread handles QBUF */
    UvcDevCtx *pDev = &g_stUvcCtx.astDev[dev];
    pthread_mutex_lock(&pChn->depthLock);
    while (pChn->u32DepthCount > 0) {
        UvcDepthEntry *pEntry = &pChn->astDepth[pChn->u32DepthHead];
        VB_ReleaseBuffer(pEntry->ulBufferId);
        pChn->u32DepthHead = (pChn->u32DepthHead + 1) % UVC_DEPTH_MAX;
        pChn->u32DepthCount--;
    }
    pthread_mutex_unlock(&pChn->depthLock);
    pthread_mutex_destroy(&pChn->depthLock);
    pthread_cond_destroy(&pChn->depthNotEmpty);

    pChn->bEnabled = MPP_FALSE;

    /* stop recycle thread (per-channel) */
    pChn->bRecycleRun = MPP_FALSE;
    pthread_mutex_unlock(&g_stUvcCtx.lock);
    pthread_join(pChn->recycleTid, NULL);
    pthread_mutex_lock(&g_stUvcCtx.lock);

    /* check if any other channel is still enabled on this device */
    BOOL bAnyChnEnabled = MPP_FALSE;
    for (S32 c = 0; c < UVC_MAX_CHN_NUM; c++) {
        if (pDev->astChn[c].bEnabled) {
            bAnyChnEnabled = MPP_TRUE;
            break;
        }
    }

    /* release per-channel VB buffers */
    uvc_v4l2_release_bufs(pDev, pChn);

    if (!bAnyChnEnabled) {
        /* stop V4L2 streaming */
        uvc_v4l2_stream_off(pDev);
    }

    pthread_mutex_unlock(&g_stUvcCtx.lock);

    UVC_LOG_INFO("UVC_DisableChn: dev %d chn %d disabled", dev, chn);
    return UVC_ERR_OK;
}

/* ======================== Get / Release Frame ======================== */

/**
 * @brief Get a frame from the channel depth queue.
 *
 * When depth > 0, the task thread pushes frames into the depth queue;
 * UVC_GetFrame pops from it.  The returned frame holds a VB ref that
 * the caller must release via UVC_ReleaseFrame.
 *
 * When depth == 0, there is no user-facing queue — frames are only
 * forwarded via SYS_SendFrame.  UVC_GetFrame returns UVC_ERR_NOT_CFG.
 */
S32 UVC_GetFrame(UVC_DEV dev, UVC_CHN chn, VideoFrameInfo *pstFrameInfo, S32 s32MilliSec) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);
    UVC_CHECK_DEV(dev);
    UVC_CHECK_CHN(chn);
    UVC_CHECK_POINTER(pstFrameInfo);

    UvcDevCtx *pDev = &g_stUvcCtx.astDev[dev];
    UvcChnCtx *pChn = &pDev->astChn[chn];

    if (!pChn->bEnabled) {
        UVC_LOG_ERR("dev %d chn %d not enabled", dev, chn);
        return UVC_ERR_NOT_ENABLE;
    }
    if (pChn->stChnAttr.u32Depth == 0) {
        UVC_LOG_ERR("dev %d chn %d depth=0, use SYS_RecvFrame on bound sink instead", dev, chn);
        return UVC_ERR_NOT_CFG;
    }

    /* pop from depth queue with optional timeout */
    pthread_mutex_lock(&pChn->depthLock);

    if (pChn->u32DepthCount == 0 && s32MilliSec != 0) {
        if (s32MilliSec < 0) {
            /* blocking wait */
            while (pChn->u32DepthCount == 0 && pChn->bTaskRun)
                pthread_cond_wait(&pChn->depthNotEmpty, &pChn->depthLock);
        } else {
            /* timed wait */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += s32MilliSec / 1000;
            ts.tv_nsec += (s32MilliSec % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000L;
            }
            while (pChn->u32DepthCount == 0 && pChn->bTaskRun) {
                S32 r = pthread_cond_timedwait(&pChn->depthNotEmpty, &pChn->depthLock, &ts);
                if (r == ETIMEDOUT)
                    break;
            }
        }
    }

    if (pChn->u32DepthCount == 0) {
        pthread_mutex_unlock(&pChn->depthLock);
        return UVC_ERR_TIMEOUT;
    }

    /* dequeue the oldest entry — caller inherits the VB ref */
    UvcDepthEntry *pEntry = &pChn->astDepth[pChn->u32DepthHead];
    memcpy(pstFrameInfo, &pEntry->stFrameInfo, sizeof(VideoFrameInfo));
    pChn->u32DepthHead = (pChn->u32DepthHead + 1) % UVC_DEPTH_MAX;
    pChn->u32DepthCount--;

    pthread_mutex_unlock(&pChn->depthLock);
    return UVC_ERR_OK;
}

/**
 * @brief Release a frame obtained via UVC_GetFrame.
 *
 * Decrements the VB reference count.  When the refcount reaches 0,
 * the buffer returns to the VB pool and the recycle thread will
 * QBUF it back to V4L2 automatically.
 */
S32 UVC_ReleaseFrame(UVC_DEV dev, UVC_CHN chn, const VideoFrameInfo *pstFrameInfo) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);
    UVC_CHECK_DEV(dev);
    UVC_CHECK_CHN(chn);
    UVC_CHECK_POINTER(pstFrameInfo);

    /* Only release the VB ref.  Do NOT directly QBUF.
     * When refcount drops to 0, the buffer goes back to the VB pool.
     * The recycle thread will pick it up and QBUF to V4L2. */
    if (pstFrameInfo->ulBufferId != 0)
        VB_ReleaseBuffer(pstFrameInfo->ulBufferId);

    return UVC_ERR_OK;
}

/* ======================== Effect Attributes ======================== */

S32 UVC_SetEffectAttr(UVC_DEV dev, const UvcEffectAttr *pstEffect) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);
    UVC_CHECK_DEV(dev);
    UVC_CHECK_POINTER(pstEffect);

    pthread_mutex_lock(&g_stUvcCtx.lock);

    UvcDevCtx *pDev = &g_stUvcCtx.astDev[dev];
    if (!pDev->bEnabled) {
        UVC_LOG_ERR("dev %d not enabled", dev);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_NOT_ENABLE;
    }

    /* apply V4L2 controls to device fd */
    S32 fd = pDev->s32Fd;
    uvc_v4l2_set_ctrl(fd, V4L2_CID_BRIGHTNESS, pstEffect->s32Brightness);
    uvc_v4l2_set_ctrl(fd, V4L2_CID_CONTRAST, pstEffect->s32Contrast);
    uvc_v4l2_set_ctrl(fd, V4L2_CID_SATURATION, pstEffect->s32Saturation);
    uvc_v4l2_set_ctrl(fd, V4L2_CID_HUE, pstEffect->s32Hue);
    uvc_v4l2_set_ctrl(fd, V4L2_CID_SHARPNESS, pstEffect->s32Sharpness);
    uvc_v4l2_set_ctrl(fd, V4L2_CID_GAMMA, (S32)pstEffect->u32Gamma);
    uvc_v4l2_set_ctrl(fd, V4L2_CID_GAIN, (S32)pstEffect->u32Gain);
    uvc_v4l2_set_ctrl(fd, V4L2_CID_AUTO_WHITE_BALANCE, pstEffect->bAutoWhiteBalance);
    uvc_v4l2_set_ctrl(fd, V4L2_CID_WHITE_BALANCE_TEMPERATURE, (S32)pstEffect->u32WhiteBalanceTemp);
    uvc_v4l2_set_ctrl(fd, V4L2_CID_BACKLIGHT_COMPENSATION, pstEffect->bBacklightComp);
    uvc_v4l2_set_ctrl(fd, V4L2_CID_EXPOSURE_AUTO, pstEffect->bAutoExposure ? V4L2_EXPOSURE_AUTO : V4L2_EXPOSURE_MANUAL);
    if (!pstEffect->bAutoExposure) {
        uvc_v4l2_set_ctrl(fd, V4L2_CID_EXPOSURE_ABSOLUTE, (S32)pstEffect->u32ExposureTime);
    }

    memcpy(&pDev->stEffectAttr, pstEffect, sizeof(UvcEffectAttr));
    pDev->bEffectSet = MPP_TRUE;

    pthread_mutex_unlock(&g_stUvcCtx.lock);

    UVC_LOG_INFO(
        "UVC_SetEffectAttr: dev %d, brightness=%d contrast=%d saturation=%d",
        dev,
        pstEffect->s32Brightness,
        pstEffect->s32Contrast,
        pstEffect->s32Saturation);
    return UVC_ERR_OK;
}

S32 UVC_GetEffectAttr(UVC_DEV dev, UvcEffectAttr *pstEffect) {
    UVC_CHECK_INIT(g_stUvcCtx.bInited);
    UVC_CHECK_DEV(dev);
    UVC_CHECK_POINTER(pstEffect);

    pthread_mutex_lock(&g_stUvcCtx.lock);

    UvcDevCtx *pDev = &g_stUvcCtx.astDev[dev];
    if (!pDev->bEnabled) {
        UVC_LOG_ERR("dev %d not enabled", dev);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_NOT_ENABLE;
    }
    if (!pDev->bEffectSet) {
        UVC_LOG_ERR("dev %d effect not set", dev);
        pthread_mutex_unlock(&g_stUvcCtx.lock);
        return UVC_ERR_NOT_CFG;
    }

    memcpy(pstEffect, &pDev->stEffectAttr, sizeof(UvcEffectAttr));

    pthread_mutex_unlock(&g_stUvcCtx.lock);
    return UVC_ERR_OK;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
