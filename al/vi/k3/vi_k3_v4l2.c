/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_k3_v4l2.c
 * @Brief     :    Pure V4L2 wrapper — no VB, no SYS, no pthread.
 *                 All buffer management is done by MPI layer.
 *------------------------------------------------------------------------------
 */

#include "vi_k3.h"
#include "vi_k3_ctx.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <unistd.h>

typedef struct _K3_V4L2_BUF_S {
    struct v4l2_buffer buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
} K3_V4L2_BUF_S;

#define K3_V4L2_DEMO_WIDTH 1920U
#define K3_V4L2_DEMO_HEIGHT 1080U
#define K3_V4L2_DEMO_BUFCNT 4U

static int k3_xioctl(int fd, int request, void *arg) {
    int r;

    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);

    return r;
}

/* Map current errno to a K3 fine-grained error code.
 * `fallback` is returned for unrecognised errnos so the caller still gets the
 * ioctl-specific category (e.g. K3_VI_ERR_QBUF). */
static S32 k3_errno_to_err(S32 fallback) {
    switch (errno) {
        case EAGAIN:
            return K3_VI_ERR_TRY_AGAIN;
        case EBUSY:
            return K3_VI_ERR_DEV_BUSY;
        case ENODEV:
        case ENXIO:
            return K3_VI_ERR_NO_DEVICE;
        case EACCES:
        case EPERM:
            return K3_VI_ERR_PERM;
        case ENOMEM:
            return K3_VI_ERR_NO_MEM;
        case EINVAL:
            return K3_VI_ERR_INVALID_PARAM;
        case ENOTTY:
            return K3_VI_ERR_NOT_SUPPORT;
        case ETIMEDOUT:
            return K3_VI_ERR_TIMEOUT;
        default:
            return fallback;
    }
}

#define K3_IOCTL_FAIL(_name, _fallback)                                                \
    do {                                                                               \
        error("K3_V4L2: %s failed, errno=%d (%s)\n", (_name), errno, strerror(errno)); \
        return k3_errno_to_err(_fallback);                                             \
    } while (0)

static const char *k3_devnode(void) {
    const char *dev = getenv("K3_V4L2_DEV");
    return (dev != NULL) ? dev : "/dev/video3";
}

S32 K3_V4L2_Open(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx) {
    struct v4l2_capability cap;
    const char *dev;

    (void)ViDev;
    (void)ViChn;

    if (pstCtx == NULL)
        return K3_VI_ERR_INVALID_PARAM;
    if (pstCtx->s32Fd >= 0)
        return K3_VI_SUCCESS;

    dev = k3_devnode();
    pstCtx->s32Fd = open(dev, O_RDWR | O_NONBLOCK, 0);
    info("K3_V4L2_Open: open %s, fd=%d\n", dev, pstCtx->s32Fd);
    if (pstCtx->s32Fd < 0) {
        error("K3_V4L2_Open: open(%s) failed, errno=%d (%s)\n", dev, errno, strerror(errno));
        return k3_errno_to_err(K3_VI_ERR_OPEN_FAIL);
    }

    if (k3_xioctl(pstCtx->s32Fd, VIDIOC_QUERYCAP, &cap) < 0)
        K3_IOCTL_FAIL("VIDIOC_QUERYCAP", K3_VI_ERR_QUERYCAP);
    if (!(cap.capabilities & V4L2_CAP_STREAMING))
        return K3_VI_ERR_BAD_FORMAT;
    return K3_VI_SUCCESS;
}

static U32 k3_mpp_to_v4l2_pixfmt(MppPixelFormat eFmt) {
    switch (eFmt) {
        case MPP_PIXEL_FORMAT_UYVY:
            return V4L2_PIX_FMT_UYVY;
        case MPP_PIXEL_FORMAT_YUYV:
            return V4L2_PIX_FMT_YUYV;
        case MPP_PIXEL_FORMAT_RGB_BAYER_10BITS_PACKED:
        default:
            return v4l2_fourcc('p', 'B', 'A', 'A');
    }
}

static U32 k3_mpp_to_v4l2_colorspace(MppPixelFormat eFmt) {
    switch (eFmt) {
        case MPP_PIXEL_FORMAT_UYVY:
        case MPP_PIXEL_FORMAT_YUYV:
            return V4L2_COLORSPACE_SMPTE170M;
        case MPP_PIXEL_FORMAT_RGB_BAYER_10BITS_PACKED:
        default:
            return V4L2_COLORSPACE_RAW;
    }
}

S32 K3_V4L2_Config(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx) {
    struct v4l2_format fmt;
    struct v4l2_format get_fmt;
    struct v4l2_requestbuffers req;
    U32 u32ReqCnt;

    (void)ViDev;
    (void)ViChn;

    if (pstCtx == NULL)
        return K3_VI_ERR_INVALID_PARAM;
    if (pstCtx->s32Fd < 0)
        return K3_VI_ERR_BAD_STATE;

    U32 width = pstCtx->stChnAttr.u32Width;
    U32 height = pstCtx->stChnAttr.u32Height;

    if (width == 0U || height == 0U) {
        width = K3_V4L2_DEMO_WIDTH;
        height = K3_V4L2_DEMO_HEIGHT;
        info("K3_V4L2_Config: ChnAttr is 0x0, fallback to demo resolution %ux%u\n", width, height);
    }

    /* ========== Step 1: Set format ========== */
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = width;
    fmt.fmt.pix_mp.height = height;
    fmt.fmt.pix_mp.pixelformat = k3_mpp_to_v4l2_pixfmt(pstCtx->stChnAttr.ePixelFormat);
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.colorspace = k3_mpp_to_v4l2_colorspace(pstCtx->stChnAttr.ePixelFormat);

    info(
        "K3_V4L2_Config: set format %ux%u, pixfmt=0x%08X\n",
        fmt.fmt.pix_mp.width,
        fmt.fmt.pix_mp.height,
        fmt.fmt.pix_mp.pixelformat);
    if (k3_xioctl(pstCtx->s32Fd, VIDIOC_S_FMT, &fmt) < 0)
        K3_IOCTL_FAIL("VIDIOC_S_FMT", K3_VI_ERR_S_FMT);

    /* Read back actual format */
    memset(&get_fmt, 0, sizeof(get_fmt));
    get_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (k3_xioctl(pstCtx->s32Fd, VIDIOC_G_FMT, &get_fmt) < 0)
        K3_IOCTL_FAIL("VIDIOC_G_FMT", K3_VI_ERR_G_FMT);
    if (get_fmt.fmt.pix_mp.num_planes > 0)
        fmt = get_fmt;
    if (fmt.fmt.pix_mp.num_planes == 0)
        fmt.fmt.pix_mp.num_planes = 1;
    if (fmt.fmt.pix_mp.num_planes > VIDEO_MAX_PLANES)
        fmt.fmt.pix_mp.num_planes = VIDEO_MAX_PLANES;

    pstCtx->u32PlaneCnt = fmt.fmt.pix_mp.num_planes;

    /* Save plane sizes for later QBUF (will be overwritten by
     * al_vi_set_external_buf_pool if MPI provides explicit sizes) */
    for (U32 i = 0; i < pstCtx->u32PlaneCnt; i++) {
        info("K3_V4L2_Config: plane[%u] sizeimage=%u\n", i, fmt.fmt.pix_mp.plane_fmt[i].sizeimage);
    }

    /* ========== Step 2: REQBUFS (DMABUF) ========== */
    u32ReqCnt = K3_V4L2_DEMO_BUFCNT;

    memset(&req, 0, sizeof(req));
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_DMABUF;
    req.count = u32ReqCnt;
    if (k3_xioctl(pstCtx->s32Fd, VIDIOC_REQBUFS, &req) < 0) {
        error("K3_V4L2_Config: VIDIOC_REQBUFS(DMABUF) failed: %s\n", strerror(errno));
        return k3_errno_to_err(K3_VI_ERR_REQBUFS);
    }

    if (req.count < 2) {
        error("K3_V4L2_Config: insufficient buffer count: %u\n", req.count);
        return K3_VI_ERR_NO_MEM;
    }

    info(
        "K3_V4L2_Config: REQBUFS(DMABUF) requested=%u granted=%u, planes=%u\n",
        u32ReqCnt,
        req.count,
        pstCtx->u32PlaneCnt);
    pstCtx->u32BufCnt = (req.count < u32ReqCnt) ? req.count : u32ReqCnt;

    return K3_VI_SUCCESS;
}

S32 K3_V4L2_Start(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    U32 i;
    S32 s32Ret;

    if (pstCtx == NULL)
        return K3_VI_ERR_INVALID_PARAM;
    if (pstCtx->s32Fd < 0)
        return K3_VI_ERR_BAD_STATE;

    /* 使用 DMABUF 模式 QBUF 所有缓冲区 */
    for (i = 0; i < pstCtx->u32BufCnt; ++i) {
        s32Ret = K3_V4L2_QBuf_DmaBuf(ViDev, ViChn, pstCtx, i);
        if (s32Ret != K3_VI_SUCCESS) {
            error("K3_V4L2_Start: QBuf_DmaBuf[%u] failed: %d\n", i, s32Ret);
            return s32Ret;
        }
    }

    if (k3_xioctl(pstCtx->s32Fd, VIDIOC_STREAMON, &type) < 0)
        K3_IOCTL_FAIL("VIDIOC_STREAMON", K3_VI_ERR_STREAMON);
    pstCtx->bStreaming = MPP_TRUE;
    return K3_VI_SUCCESS;
}

S32 K3_V4L2_Stop(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    (void)ViDev;
    (void)ViChn;

    if (pstCtx == NULL)
        return K3_VI_ERR_INVALID_PARAM;
    if (pstCtx->s32Fd < 0)
        return K3_VI_ERR_BAD_STATE;
    if (pstCtx->bStreaming == MPP_TRUE) {
        if (k3_xioctl(pstCtx->s32Fd, VIDIOC_STREAMOFF, &type) < 0)
            error("K3_V4L2_Stop: VIDIOC_STREAMOFF failed, errno=%d (%s)\n", errno, strerror(errno));
    }
    pstCtx->bStreaming = MPP_FALSE;
    return K3_VI_SUCCESS;
}

S32 K3_V4L2_Close(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx) {
    (void)ViDev;
    (void)ViChn;

    if (pstCtx == NULL)
        return K3_VI_ERR_INVALID_PARAM;
    if (pstCtx->s32Fd >= 0) {
        close(pstCtx->s32Fd);
        pstCtx->s32Fd = -1;
    }
    pstCtx->u32BufCnt = 0;
    return K3_VI_SUCCESS;
}

/* ======================== DMABUF helpers ======================== */

/**
 * @brief QBUF a DMABUF-backed slot back to the V4L2 driver.
 *
 * pstCtx->as32DmaBufFd[u32BufIdx] and pstCtx->au32PlaneSize[u32BufIdx][p]
 * MUST have been populated (by al_vi_set_external_buf_pool).
 */
S32 K3_V4L2_QBuf_DmaBuf(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx, U32 u32BufIdx) {
    K3_V4L2_BUF_S vbuf;
    U32 i;

    (void)ViDev;
    (void)ViChn;

    if (pstCtx == NULL)
        return K3_VI_ERR_INVALID_PARAM;
    if (u32BufIdx >= pstCtx->u32BufCnt)
        return K3_VI_ERR_INVALID_PARAM;
    if (pstCtx->s32Fd < 0)
        return K3_VI_ERR_BAD_STATE;
    if (pstCtx->as32DmaBufFd[u32BufIdx] < 0) {
        error("K3_V4L2_QBuf_DmaBuf: slot %u has no dmabuf fd (call set_external_buf_pool first)\n", u32BufIdx);
        return K3_VI_ERR_BAD_STATE;
    }

    memset(&vbuf, 0, sizeof(vbuf));
    vbuf.buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    vbuf.buf.memory = V4L2_MEMORY_DMABUF;
    vbuf.buf.index = u32BufIdx;
    vbuf.buf.length = pstCtx->u32PlaneCnt;
    vbuf.buf.m.planes = vbuf.planes;

    for (i = 0; i < pstCtx->u32PlaneCnt && i < VIDEO_MAX_PLANES; i++) {
        vbuf.planes[i].m.fd = pstCtx->as32DmaBufFd[u32BufIdx];
        vbuf.planes[i].length = pstCtx->au32PlaneSize[u32BufIdx][i];
    }

    if (k3_xioctl(pstCtx->s32Fd, VIDIOC_QBUF, &vbuf.buf) < 0)
        K3_IOCTL_FAIL("VIDIOC_QBUF(DMABUF)", K3_VI_ERR_QBUF);

    return K3_VI_SUCCESS;
}

/**
 * @brief Wait for a frame to be ready and DQBUF it.
 *
 * @param s32MilliSec  < 0 = wait forever, 0 = non-blocking, > 0 = timeout in ms
 * @param pu32BufIdx   [out] returned slot index
 *
 * On success, pstCtx->astSlotMeta[*pu32BufIdx] is updated with the latest
 * v4l2 timestamp / sequence / bytesused so MPI can build a VideoFrameInfo.
 */
S32 K3_V4L2_DQBuf_Wait(VI_DEV ViDev, VI_CHN ViChn, K3_VI_CHN_CTX_S *pstCtx, S32 s32MilliSec, U32 *pu32BufIdx) {
    K3_V4L2_BUF_S vbuf;
    fd_set fds;
    struct timeval tv;
    struct timeval *pTv;
    int sel;
    U32 i;

    (void)ViDev;
    (void)ViChn;

    if (pstCtx == NULL || pu32BufIdx == NULL)
        return K3_VI_ERR_INVALID_PARAM;
    if (pstCtx->s32Fd < 0 || !pstCtx->bStreaming)
        return K3_VI_ERR_BAD_STATE;

    /* Wait for a frame to be ready */
    if (s32MilliSec < 0) {
        pTv = NULL; /* infinite */
    } else {
        tv.tv_sec = s32MilliSec / 1000;
        tv.tv_usec = (s32MilliSec % 1000) * 1000;
        pTv = &tv;
    }

    FD_ZERO(&fds);
    FD_SET(pstCtx->s32Fd, &fds);

    do {
        sel = select(pstCtx->s32Fd + 1, &fds, NULL, NULL, pTv);
    } while (sel == -1 && errno == EINTR);

    if (sel == 0)
        return K3_VI_ERR_BUSY; /* timeout */
    if (sel < 0) {
        error("K3_V4L2_DQBuf_Wait: select failed: %s\n", strerror(errno));
        return k3_errno_to_err(K3_VI_ERR_SELECT);
    }

    /* DQBUF */
    memset(&vbuf, 0, sizeof(vbuf));
    vbuf.buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    vbuf.buf.memory = V4L2_MEMORY_DMABUF;
    vbuf.buf.length = pstCtx->u32PlaneCnt;
    vbuf.buf.m.planes = vbuf.planes;

    if (k3_xioctl(pstCtx->s32Fd, VIDIOC_DQBUF, &vbuf.buf) < 0) {
        if (errno == EAGAIN)
            return K3_VI_ERR_TRY_AGAIN;
        error("K3_V4L2_DQBuf_Wait: VIDIOC_DQBUF failed: %s\n", strerror(errno));
        return k3_errno_to_err(K3_VI_ERR_DQBUF);
    }

    if (vbuf.buf.index >= pstCtx->u32BufCnt) {
        error("K3_V4L2_DQBuf_Wait: out-of-range index %u\n", vbuf.buf.index);
        return K3_VI_ERR_BAD_STATE;
    }

    /* Cache metadata for MPI */
    pstCtx->astSlotMeta[vbuf.buf.index].u64TimestampUs =
        (U64)vbuf.buf.timestamp.tv_sec * 1000000ULL + (U64)vbuf.buf.timestamp.tv_usec;
    pstCtx->astSlotMeta[vbuf.buf.index].u32Sequence = vbuf.buf.sequence;

    for (i = 0; i < pstCtx->u32PlaneCnt && i < VIDEO_MAX_PLANES; i++) {
        pstCtx->astSlotMeta[vbuf.buf.index].au32BytesUsed[i] = vbuf.planes[i].bytesused;
    }

    *pu32BufIdx = vbuf.buf.index;
    return K3_VI_SUCCESS;
}
