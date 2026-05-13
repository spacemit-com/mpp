/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Spacemit Co., Ltd.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "vb_api.h"
#include "v2d_api.h"
#include "v2d_private_type.h"
#include "log.h"

#define V2D_DEV_NAME "/dev/v2d_dev"
#define V2D_MAX_TASK_NUM 64U

typedef enum MppV2DTaskType {
    MPP_V2D_TASK_NONE = 0,
    MPP_V2D_TASK_CPU_LINE,
    MPP_V2D_TASK_CPU_CIRCLE,
    MPP_V2D_TASK_FILL,
    MPP_V2D_TASK_BITBLIT,
    MPP_V2D_TASK_BLEND,
} MppV2DTaskType;

typedef struct MppV2DTaskNode {
    V2DSubmitTask submitTask;
    VideoFrameInfo *cpuDstFrame;
    V2DLine cpuLine;
    V2DCircle cpuCircle;
    MppV2DTaskType type;
    struct MppV2DTaskNode *next;
} MppV2DTaskNode;

typedef struct MppV2DJobCtx {
    U32 taskCount;
    S32 devFd;
    MppV2DTaskNode *head;
    MppV2DTaskNode *tail;
} MppV2DJobCtx;

static S32 V2D_SurfaceFromVideoFrame(const VideoFrameInfo *frame,
                          V2DSurface *surface);

static S32 mpp_v2d_wait_fence(S32 fd, S32 timeout_ms)
{
    struct pollfd pollFd;
    S32 ret;

    if (fd < 0) {
        return SUCCESS;
    }

    pollFd.fd = fd;
    pollFd.events = POLLIN;

    do {
        ret = poll(&pollFd, 1, timeout_ms);
    } while ((ret < 0) && (errno == EINTR));

    if (ret <= 0) {
        return FAILURE;
    }

    if ((pollFd.revents & (POLLERR | POLLNVAL)) != 0) {
        return FAILURE;
    }

    return SUCCESS;
}

static S32 mpp_v2d_get_format_bpp(V2DColorFormat format, U32 *bpp)
{
    if (bpp == NULL) {
        return FAILURE;
    }

    switch (format) {
    case V2D_COLOR_FORMAT_RGB565:
    case V2D_COLOR_FORMAT_BGR565:
    case V2D_COLOR_FORMAT_RGBA5658:
    case V2D_COLOR_FORMAT_ARGB8565:
    case V2D_COLOR_FORMAT_BGRA5658:
    case V2D_COLOR_FORMAT_ABGR8565:
        *bpp = 2U;
        return SUCCESS;
    case V2D_COLOR_FORMAT_RGB888:
    case V2D_COLOR_FORMAT_BGR888:
    case V2D_COLOR_FORMAT_L8_RGB888:
    case V2D_COLOR_FORMAT_L8_BGR888:
        *bpp = 3U;
        return SUCCESS;
    case V2D_COLOR_FORMAT_RGBX8888:
    case V2D_COLOR_FORMAT_RGBA8888:
    case V2D_COLOR_FORMAT_ARGB8888:
    case V2D_COLOR_FORMAT_BGRA8888:
    case V2D_COLOR_FORMAT_BGRX8888:
    case V2D_COLOR_FORMAT_ABGR8888:
    case V2D_COLOR_FORMAT_L8_RGBA8888:
    case V2D_COLOR_FORMAT_L8_BGRA8888:
        *bpp = 4U;
        return SUCCESS;
    case V2D_COLOR_FORMAT_A8:
    case V2D_COLOR_FORMAT_Y8:
        *bpp = 1U;
        return SUCCESS;
    case V2D_COLOR_FORMAT_NV12:
    case V2D_COLOR_FORMAT_NV21:
        *bpp = 1U;
        return SUCCESS;
    default:
        return FAILURE;
    }
}

static S32 mpp_v2d_format_has_cpu_writer(V2DColorFormat format)
{
    switch (format) {
    case V2D_COLOR_FORMAT_NV12:
    case V2D_COLOR_FORMAT_NV21:
        return SUCCESS;
    default:
        return FAILURE;
    }
}

static S32 mpp_v2d_line_get_base_addr(const VideoFrameInfo *frame, U8 **base_addr)
{
    UL vbHandle;
    void *virAddr = NULL;

    if ((frame == NULL) || (base_addr == NULL)) {
        return FAILURE;
    }

    *base_addr = NULL;

    vbHandle = frame->ulBufferId;
    if (vbHandle == 0U) {
        return FAILURE;
    }

    if (VB_GetVirAddr(vbHandle, &virAddr) != SUCCESS) {
        return FAILURE;
    }

    *base_addr = (U8 *)virAddr;
    return SUCCESS;
}

static S32 mpp_v2d_write_pixel(V2DSurface *dst,
                       U8 *base_addr,
                       S32 x,
                       S32 y,
                       const V2DFillColor *color)
{
    U32 offset;
    U32 colorValue;
    U8 yValue;
    U8 uValue;
    U8 vValue;
    U8 *uvPlane;

    if ((dst == NULL) || (base_addr == NULL) || (color == NULL)) {
        return FAILURE;
    }

    if ((x < 0) || (y < 0) || ((U32)x >= dst->u16W) || ((U32)y >= dst->u16H)) {
        return SUCCESS;
    }

    if ((dst->enFormat != V2D_COLOR_FORMAT_NV12) &&
        (dst->enFormat != V2D_COLOR_FORMAT_NV21)) {
        return FAILURE;
    }

    colorValue = color->u32ColorValue;
    yValue = (U8)(colorValue & 0xFFU);
    uValue = (U8)((colorValue >> 8) & 0xFFU);
    vValue = (U8)((colorValue >> 16) & 0xFFU);

    offset = ((U32)y * dst->u16Stride) + (U32)x;
    base_addr[offset] = yValue;

    uvPlane = base_addr + dst->s32Offset;
    offset = (((U32)y / 2U) * dst->u16Stride) + (((U32)x / 2U) * 2U);
    if (dst->enFormat == V2D_COLOR_FORMAT_NV12) {
        uvPlane[offset] = uValue;
        uvPlane[offset + 1U] = vValue;
    } else {
        uvPlane[offset] = vValue;
        uvPlane[offset + 1U] = uValue;
    }

    return SUCCESS;
}

static S32 mpp_v2d_draw_line_cpu(V2DSurface *dst,
                     U8 *base_addr,
                     const V2DLine *line)
{
    S32 x0;
    S32 y0;
    S32 x1;
    S32 y1;
    S32 dx;
    S32 dy;
    S32 sx;
    S32 sy;
    S32 err;

    if ((dst == NULL) || (base_addr == NULL) || (line == NULL)) {
        return FAILURE;
    }

    x0 = line->s32X0;
    y0 = line->s32Y0;
    x1 = line->s32X1;
    y1 = line->s32Y1;
    dx = abs(x1 - x0);
    dy = abs(y1 - y0);
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx - dy;

    for (;;) {
        if (mpp_v2d_write_pixel(dst, base_addr, x0, y0, &line->stColor) != SUCCESS) {
            return FAILURE;
        }

        if ((x0 == x1) && (y0 == y1)) {
            break;
        }
        {
            S32 e2 = err * 2;

            if (e2 > -dy) {
                err -= dy;
                x0 += sx;
            }

            if (e2 < dx) {
                err += dx;
                y0 += sy;
            }
        }
    }

    return SUCCESS;
}

static S32 mpp_v2d_draw_circle_points(V2DSurface *pstDst,
                                      U8 *pu8BaseAddr,
                                      const V2DCircle *pstCircle,
                                      S32 s32OffsetX,
                                      S32 s32OffsetY)
{
    S32 s32CenterX;
    S32 s32CenterY;

    if ((pstDst == NULL) || (pu8BaseAddr == NULL) || (pstCircle == NULL)) {
        return FAILURE;
    }

    s32CenterX = pstCircle->s32CenterX;
    s32CenterY = pstCircle->s32CenterY;

    if ((mpp_v2d_write_pixel(pstDst, pu8BaseAddr, s32CenterX + s32OffsetX, s32CenterY + s32OffsetY, &pstCircle->stColor) != SUCCESS) ||
        (mpp_v2d_write_pixel(pstDst, pu8BaseAddr, s32CenterX - s32OffsetX, s32CenterY + s32OffsetY, &pstCircle->stColor) != SUCCESS) ||
        (mpp_v2d_write_pixel(pstDst, pu8BaseAddr, s32CenterX + s32OffsetX, s32CenterY - s32OffsetY, &pstCircle->stColor) != SUCCESS) ||
        (mpp_v2d_write_pixel(pstDst, pu8BaseAddr, s32CenterX - s32OffsetX, s32CenterY - s32OffsetY, &pstCircle->stColor) != SUCCESS) ||
        (mpp_v2d_write_pixel(pstDst, pu8BaseAddr, s32CenterX + s32OffsetY, s32CenterY + s32OffsetX, &pstCircle->stColor) != SUCCESS) ||
        (mpp_v2d_write_pixel(pstDst, pu8BaseAddr, s32CenterX - s32OffsetY, s32CenterY + s32OffsetX, &pstCircle->stColor) != SUCCESS) ||
        (mpp_v2d_write_pixel(pstDst, pu8BaseAddr, s32CenterX + s32OffsetY, s32CenterY - s32OffsetX, &pstCircle->stColor) != SUCCESS) ||
        (mpp_v2d_write_pixel(pstDst, pu8BaseAddr, s32CenterX - s32OffsetY, s32CenterY - s32OffsetX, &pstCircle->stColor) != SUCCESS)) {
        return FAILURE;
    }

    return SUCCESS;
}

static S32 mpp_v2d_draw_circle_outline_cpu(V2DSurface *pstDst,
                                           U8 *pu8BaseAddr,
                                           const V2DCircle *pstCircle,
                                           U32 u32Radius)
{
    S32 s32X;
    S32 s32Y;
    S32 s32Decision;

    if ((pstDst == NULL) || (pu8BaseAddr == NULL) || (pstCircle == NULL)) {
        return FAILURE;
    }

    s32X = 0;
    s32Y = (S32)u32Radius;
    s32Decision = 1 - s32Y;

    while (s32X <= s32Y) {
        if (mpp_v2d_draw_circle_points(pstDst, pu8BaseAddr, pstCircle, s32X, s32Y) != SUCCESS) {
            return FAILURE;
        }

        s32X++;
        if (s32Decision < 0) {
            s32Decision += (2 * s32X) + 1;
        } else {
            s32Y--;
            s32Decision += (2 * (s32X - s32Y)) + 1;
        }
    }

    return SUCCESS;
}

static S32 mpp_v2d_draw_circle_filled_cpu(V2DSurface *pstDst,
                                          U8 *pu8BaseAddr,
                                          const V2DCircle *pstCircle)
{
    S32 s32OffsetY;
    S32 s32Radius;

    if ((pstDst == NULL) || (pu8BaseAddr == NULL) || (pstCircle == NULL)) {
        return FAILURE;
    }

    s32Radius = (S32)pstCircle->u32Radius;
    for (s32OffsetY = -s32Radius; s32OffsetY <= s32Radius; ++s32OffsetY) {
        S32 s32Remain;
        S32 s32OffsetX;
        V2DLine stLine;

        s32Remain = (s32Radius * s32Radius) - (s32OffsetY * s32OffsetY);
        s32OffsetX = 0;
        while (((s32OffsetX + 1) * (s32OffsetX + 1)) <= s32Remain) {
            ++s32OffsetX;
        }

        memset(&stLine, 0, sizeof(stLine));
        stLine.s32X0 = pstCircle->s32CenterX - s32OffsetX;
        stLine.s32Y0 = pstCircle->s32CenterY + s32OffsetY;
        stLine.s32X1 = pstCircle->s32CenterX + s32OffsetX;
        stLine.s32Y1 = pstCircle->s32CenterY + s32OffsetY;
        stLine.stColor = pstCircle->stColor;
        stLine.u32LineWidth = 1U;

        if (mpp_v2d_draw_line_cpu(pstDst, pu8BaseAddr, &stLine) != SUCCESS) {
            return FAILURE;
        }
    }

    return SUCCESS;
}

static S32 mpp_v2d_draw_circle_cpu(V2DSurface *pstDst,
                                   U8 *pu8BaseAddr,
                                   const V2DCircle *pstCircle)
{
    U32 u32Thickness;
    U32 u32Radius;

    if ((pstDst == NULL) || (pu8BaseAddr == NULL) || (pstCircle == NULL)) {
        return FAILURE;
    }

    if (pstCircle->s32Thickness < 0) {
        return mpp_v2d_draw_circle_filled_cpu(pstDst, pu8BaseAddr, pstCircle);
    }

    u32Thickness = (U32)pstCircle->s32Thickness;
    if (u32Thickness == 0U) {
        return FAILURE;
    }

    if (u32Thickness > pstCircle->u32Radius) {
        u32Thickness = pstCircle->u32Radius;
    }

    for (u32Radius = pstCircle->u32Radius; u32Radius > (pstCircle->u32Radius - u32Thickness); --u32Radius) {
        if (mpp_v2d_draw_circle_outline_cpu(pstDst, pu8BaseAddr, pstCircle, u32Radius) != SUCCESS) {
            return FAILURE;
        }
    }

    return SUCCESS;
}

static S32 mpp_v2d_validate_surface(const V2DSurface *surface)
{
    U32 bpp = 0U;

    if (surface == NULL) {
        return FAILURE;
    }

    if (surface->stSolidColor.bEnable) {
        return SUCCESS;
    }

    if ((surface->u16W == 0U) || (surface->u16H == 0U)) {
        return FAILURE;
    }

    if (surface->s32Fd < 0) {
        return FAILURE;
    }

    if (mpp_v2d_get_format_bpp(surface->enFormat, &bpp) != SUCCESS) {
        return FAILURE;
    }

    if ((surface->enFormat == V2D_COLOR_FORMAT_NV12) ||
        (surface->enFormat == V2D_COLOR_FORMAT_NV21)) {
        if (surface->u16Stride < surface->u16W) {
            return FAILURE;
        }
    } else if (surface->u16Stride < (surface->u16W * bpp)) {
        return FAILURE;
    }

    return SUCCESS;
}

static S32 mpp_v2d_validate_rect(const V2DArea *rect, const V2DSurface *surface)
{
    if ((rect == NULL) || (surface == NULL)) {
        return FAILURE;
    }

    if ((rect->u16W == 0U) || (rect->u16H == 0U)) {
        return FAILURE;
    }

    if (((U32)rect->u16X + rect->u16W) > surface->u16W) {
        return FAILURE;
    }

    if (((U32)rect->u16Y + rect->u16H) > surface->u16H) {
        return FAILURE;
    }

    return SUCCESS;
}

static S32 mpp_v2d_validate_adv_2layer_frame(const VideoFrameInfo *frame, U32 width, U32 height, MppPixelFormat format)
{
    if (frame == NULL) {
        return FAILURE;
    }

    if ((frame->stCommFrameInfo.u32Width != width) ||
        (frame->stCommFrameInfo.u32Height != height) ||
        (frame->stCommFrameInfo.ePixelFormat != format)) {
        return FAILURE;
    }

    if ((frame->stVFrame.u32PlaneNum == 0U) || (frame->stVFrame.u32Fd[0] == 0U)) {
        return FAILURE;
    }

    return SUCCESS;
}

static S32 mpp_v2d_validate_adv_2layer_frames(const VideoFrameInfo *background_frame,
                                             const VideoFrameInfo *foreground_frame,
                                             const V2DArea *foreground_area,
                                             const VideoFrameInfo *output_frame)
{
    U32 width;
    U32 height;

    if ((background_frame == NULL) || (foreground_frame == NULL) ||
        (foreground_area == NULL) || (output_frame == NULL)) {
        return FAILURE;
    }

    width = background_frame->stCommFrameInfo.u32Width;
    height = background_frame->stCommFrameInfo.u32Height;
    if ((width == 0U) || (height == 0U)) {
        return FAILURE;
    }

    if (mpp_v2d_validate_adv_2layer_frame(background_frame, width, height, MPP_PIXEL_FORMAT_NV12) != SUCCESS) {
        return FAILURE;
    }

    if (mpp_v2d_validate_adv_2layer_frame(foreground_frame, width, height, MPP_PIXEL_FORMAT_BGRA) != SUCCESS) {
        return FAILURE;
    }

    if (mpp_v2d_validate_adv_2layer_frame(output_frame, width, height, MPP_PIXEL_FORMAT_NV12) != SUCCESS) {
        return FAILURE;
    }

    if ((foreground_area->u16W == 0U) || (foreground_area->u16H == 0U)) {
        return FAILURE;
    }

    if (((U32)foreground_area->u16X + foreground_area->u16W) > width) {
        return FAILURE;
    }

    if (((U32)foreground_area->u16Y + foreground_area->u16H) > height) {
        return FAILURE;
    }

    return SUCCESS;
}

static void mpp_v2d_dump_area(const char *name, const V2DArea *rect)
{
    if (name == NULL) {
        name = "rect";
    }

    if (rect == NULL) {
        error("%s: NULL\n", name);
        return;
    }

    info("%s: x=%d y=%d w=%u h=%u\n",
           name,
           rect->u16X,
           rect->u16Y,
           rect->u16W,
           rect->u16H);
}

static void mpp_v2d_dump_surface(const char *name, const V2DSurface *surface)
{
    if (name == NULL) {
        name = "surface";
    }

    if (surface == NULL) {
        error("%s: NULL\n", name);
        return;
    }

    info("%s: fd=%d format=%d w=%u h=%u stride=%u offset=%d fbc_enable=%u\n",
           name,
           surface->s32Fd,
           surface->enFormat,
           surface->u16W,
           surface->u16H,
           surface->u16Stride,
           surface->s32Offset,
           surface->bFbcEnable);
    info("%s: phy_y=0x%08x%08x phy_uv=0x%08x%08x solid_enable=%u solid_color=0x%08x\n",
           name,
           surface->u32PhyAddrYH,
           surface->u32PhyAddrYL,
           surface->u32PhyAddrUvH,
           surface->u32PhyAddrUvL,
           surface->stSolidColor.bEnable,
           surface->stSolidColor.stFillColor.u32ColorValue);
    info("%s: fbc_dec_fd=%d fbc_enc_fd=%d\n",
           name,
           surface->stFbcInfo.stFbcDecInfo.s32Fd,
           surface->stFbcInfo.stFbcEncInfo.s32Fd);
}

static void mpp_v2d_dump_blend_conf(const V2DBlendConf *blend_conf)
{
    U32 i;

    if (blend_conf == NULL) {
        error("blend_conf: NULL\n");
        return;
    }

    info("blend_conf: blend_cmd=%d mask_cmd=%d bgcolor.color=0x%x bgcolor.format=%d\n",
           blend_conf->enBlendCmd,
           blend_conf->enMaskCmd,
           blend_conf->stBgColor.stFillColor.u32ColorValue,
           blend_conf->stBgColor.stFillColor.enFormat);
    mpp_v2d_dump_area("blend_conf.mask_area", &blend_conf->stBlendMaskArea);

        for (i = 0; i < V2D_INPUT_LAYER_NUM; ++i) {
        info("blend_conf.layer[%u]: alpha_source=%d pre_alpha_func=%d global_alpha=%u\n",
             i,
             blend_conf->stBlendLayer[i].enBlendAlphaSource,
             blend_conf->stBlendLayer[i].enBlendPreAlphaFunc,
             blend_conf->stBlendLayer[i].u8GlobalAlpha);
        info("blend_conf.layer[%u]: src_color_factor=%d dst_color_factor=%d src_alpha_factor=%d dst_alpha_factor=%d\n",
             i,
             blend_conf->stBlendLayer[i].stBlendFactor.enSrcColorFactor,
             blend_conf->stBlendLayer[i].stBlendFactor.enDstColorFactor,
             blend_conf->stBlendLayer[i].stBlendFactor.enSrcAlphaFactor,
             blend_conf->stBlendLayer[i].stBlendFactor.enDstAlphaFactor);
        info("blend_conf.layer[%u]: rop2_color=%d rop2_alpha=%d\n",
             i,
             blend_conf->stBlendLayer[i].stRop2Code.enColorRop2Code,
             blend_conf->stBlendLayer[i].stRop2Code.enAlphaRop2Code);
        info("blend_conf.layer[%u].blend_area: x=%d y=%d w=%u h=%u\n",
             i,
             blend_conf->stBlendLayer[i].stBlendArea.u16X,
             blend_conf->stBlendLayer[i].stBlendArea.u16Y,
             blend_conf->stBlendLayer[i].stBlendArea.u16W,
             blend_conf->stBlendLayer[i].stBlendArea.u16H);
    }
}

static void mpp_v2d_dump_palette(const V2DPalette *palette)
{
    if (palette == NULL) {
        error("palette: NULL\n");
        return;
    }

    // info("palette: alp=%u color0=0x%08x color1=0x%08x color2=0x%08x color3=0x%08x\n",
    // 	   palette->alp,
    // 	   palette->color0,
    // 	   palette->color1,
    // 	   palette->color2,
    // 	   palette->color3);
}

static S32 mpp_v2d_submit_task(MppV2DJobCtx *job, MppV2DTaskNode *node)
{
    V2DSurface dst;
    U8 *base_addr = NULL;
    ssize_t written;
    S32 ret;

    if ((job == NULL) || (node == NULL)) {
        return FAILURE;
    }

    if (node->type == MPP_V2D_TASK_CPU_LINE) {
        if ((node->cpuDstFrame == NULL) ||
            (V2D_SurfaceFromVideoFrame(node->cpuDstFrame, &dst) != SUCCESS)) {
            return FAILURE;
        }

        ret = mpp_v2d_line_get_base_addr(node->cpuDstFrame, &base_addr);
        if (ret != SUCCESS) {
            return ret;
        }

        return mpp_v2d_draw_line_cpu(&dst, base_addr, &node->cpuLine);
    }

    if (node->type == MPP_V2D_TASK_CPU_CIRCLE) {
        if ((node->cpuDstFrame == NULL) ||
            (V2D_SurfaceFromVideoFrame(node->cpuDstFrame, &dst) != SUCCESS)) {
            return FAILURE;
        }

        ret = mpp_v2d_line_get_base_addr(node->cpuDstFrame, &base_addr);
        if (ret != SUCCESS) {
            return ret;
        }

        return mpp_v2d_draw_circle_cpu(&dst, base_addr, &node->cpuCircle);
    }

    written = write(job->devFd, &node->submitTask, sizeof(node->submitTask));
    if (written != (ssize_t)sizeof(node->submitTask)) {
        return FAILURE;
    }

    if (mpp_v2d_wait_fence(node->submitTask.s32CompleteFenceFd, 3000) != SUCCESS) {
        return FAILURE;
    }

    if (node->submitTask.s32AcquireFenceFd >= 0) {
        close(node->submitTask.s32AcquireFenceFd);
        node->submitTask.s32AcquireFenceFd = -1;
    }

    if (node->submitTask.s32CompleteFenceFd >= 0) {
        close(node->submitTask.s32CompleteFenceFd);
        node->submitTask.s32CompleteFenceFd = -1;
    }

    return SUCCESS;
}

static void mpp_v2d_destroy_task_list(MppV2DTaskNode *head)
{
    MppV2DTaskNode *node = head;

    while (node != NULL) {
        MppV2DTaskNode *next = node->next;
        free(node);
        node = next;
    }
}

static MppV2DJobCtx *mpp_v2d_job_from_handle(V2DHandle handle)
{
    if (handle == 0U) {
        return NULL;
    }

    return (MppV2DJobCtx *)(uintptr_t)handle;
}

static S32 mpp_v2d_append_task(MppV2DJobCtx *job,
                               MppV2DTaskType type,
                               MppV2DTaskNode **out_node)
{
    MppV2DTaskNode *node;

    if ((job == NULL) || (out_node == NULL)) {
        return FAILURE;
    }

    if (job->taskCount >= V2D_MAX_TASK_NUM) {
        return FAILURE;
    }

    node = (MppV2DTaskNode *)calloc(1, sizeof(*node));
    if (node == NULL) {
        return FAILURE;
    }

    node->type = type;
    memset(&node->submitTask.stParam, 0, sizeof(node->submitTask.stParam));
    node->submitTask.s32AcquireFenceFd = -1;
    node->submitTask.s32CompleteFenceFd = -1;

    if (job->head == NULL) {
        job->head = node;
    } else {
        job->tail->next = node;
    }

    job->tail = node;
    job->taskCount++;
    *out_node = node;

    return SUCCESS;
}

static S32 mpp_v2d_open_device(MppV2DJobCtx *job)
{
    if (job == NULL) {
        return FAILURE;
    }

    if (job->devFd >= 0) {
        return SUCCESS;
    }

    job->devFd = open(V2D_DEV_NAME, O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (job->devFd < 0) {
        return -errno;
    }

    return SUCCESS;
}

static S32 mpp_v2d_submit_job(MppV2DJobCtx *job)
{
    MppV2DTaskNode *node;

    if ((job == NULL) || (job->taskCount == 0U)) {
        return FAILURE;
    }

    if (mpp_v2d_open_device(job) != SUCCESS) {
        return FAILURE;
    }

    node = job->head;
    while (node != NULL) {
        if (mpp_v2d_submit_task(job, node) != SUCCESS) {
            return FAILURE;
        }
        node = node->next;
    }

    return SUCCESS;
}


static S32 V2D_SurfaceFromVideoFrame(const VideoFrameInfo *frame, V2DSurface *surface)
{
    if ((frame == NULL) || (surface == NULL)) {
        return FAILURE;
    }

    memset(surface, 0, sizeof(*surface));
    surface->s32Fd = -1;
    surface->stFbcInfo.stFbcDecInfo.s32Fd = -1;
    surface->stFbcInfo.stFbcEncInfo.s32Fd = -1;

    if ((frame->stVFrame.u32PlaneNum == 0U) || (frame->stVFrame.u32Fd[0] == 0U)) {
        return FAILURE;
    }

    surface->s32Fd = (S32)frame->stVFrame.u32Fd[0];
    surface->u16W = (U16)frame->stCommFrameInfo.u32Width;
    surface->u16H = (U16)frame->stCommFrameInfo.u32Height;
    surface->u16Stride = (U16)frame->stVFrame.u32PlaneStride[0];

    switch (frame->stCommFrameInfo.ePixelFormat) {
    case MPP_PIXEL_FORMAT_NV12:
        surface->enFormat = V2D_COLOR_FORMAT_NV12;
        surface->s32Offset = (S32)frame->stVFrame.u32PlaneSize[0];

        break;
    case MPP_PIXEL_FORMAT_NV21:
        surface->enFormat = V2D_COLOR_FORMAT_NV21;
        surface->s32Offset = (S32)frame->stVFrame.u32PlaneSize[0];
        break;
    case MPP_PIXEL_FORMAT_RGB_888:
        surface->enFormat = V2D_COLOR_FORMAT_RGB888;
        break;
    case MPP_PIXEL_FORMAT_BGR_888:
        surface->enFormat = V2D_COLOR_FORMAT_BGR888;
        break;
    case MPP_PIXEL_FORMAT_RGB_565:
        surface->enFormat = V2D_COLOR_FORMAT_RGB565;
        break;
    case MPP_PIXEL_FORMAT_BGR_565:
        surface->enFormat = V2D_COLOR_FORMAT_BGR565;
        break;
    case MPP_PIXEL_FORMAT_RGBA:
        surface->enFormat = V2D_COLOR_FORMAT_RGBA8888;
        break;
    case MPP_PIXEL_FORMAT_ARGB:
        surface->enFormat = V2D_COLOR_FORMAT_ARGB8888;
        break;
    case MPP_PIXEL_FORMAT_BGRA:
        surface->enFormat = V2D_COLOR_FORMAT_BGRA8888;
        break;
    case MPP_PIXEL_FORMAT_ABGR:
        surface->enFormat = V2D_COLOR_FORMAT_ABGR8888;
        break;
    case MPP_PIXEL_FORMAT_A8:
        surface->enFormat = V2D_COLOR_FORMAT_A8;
        break;
    default:
        return FAILURE;
    }

    return mpp_v2d_validate_surface(surface);
}


S32 V2D_BeginJob(V2DHandle *pHandle)
{
    MppV2DJobCtx *job;

    if (pHandle == NULL) {
        return FAILURE;
    }

    job = (MppV2DJobCtx *)calloc(1, sizeof(*job));
    if (job == NULL) {
        return FAILURE;
    }

    job->devFd = -1;
    *pHandle = (V2DHandle)(uintptr_t)job;
    return SUCCESS;
}

S32 V2D_EndJob(V2DHandle handle)
{
    S32 ret;
    MppV2DJobCtx *job = mpp_v2d_job_from_handle(handle);

    if (job == NULL) {
        return FAILURE;
    }

    ret = mpp_v2d_submit_job(job);

    if (job->devFd >= 0) {
        close(job->devFd);
        job->devFd = -1;
    }

    mpp_v2d_destroy_task_list(job->head);
    free(job);
    return ret;
}

S32 V2D_CancelJob(V2DHandle handle)
{
    MppV2DJobCtx *job = mpp_v2d_job_from_handle(handle);

    if (job == NULL) {
        return FAILURE;
    }

    if (job->devFd >= 0) {
        close(job->devFd);
    }

    mpp_v2d_destroy_task_list(job->head);
    free(job);
    return SUCCESS;
}

S32 V2D_AddFillTask(V2DHandle handle,
                    VideoFrameInfo *pstDstFrame,
                    V2DArea *pstDstRect,
                    V2DFillColor *pstFillColor)
{
    MppV2DJobCtx *job = mpp_v2d_job_from_handle(handle);
    MppV2DTaskNode *node;
    V2DParam *param;
    V2DSurface dst;

    if ((job == NULL) || (pstDstFrame == NULL) || (pstDstRect == NULL) || (pstFillColor == NULL)) {
        return FAILURE;
    }

    if ((V2D_SurfaceFromVideoFrame(pstDstFrame, &dst) != SUCCESS) ||
        (mpp_v2d_validate_rect(pstDstRect, &dst) != SUCCESS)) {
        return FAILURE;
    }

    if (mpp_v2d_append_task(job, MPP_V2D_TASK_FILL, &node) != SUCCESS) {
        return FAILURE;
    }

    param = &node->submitTask.stParam;
    param->enL0Csc = V2D_CSC_MODE_BUTT;
    param->stLayer0.stSolidColor.bEnable = true;
    param->stLayer0.stSolidColor.stFillColor = *pstFillColor;
    param->stDst = dst;
    param->stDstRect = *pstDstRect;
    param->stBlendConf.enBlendCmd = V2D_BLENDCMD_ALPHA;
    param->stBlendConf.stBlendLayer[0].stBlendArea = *pstDstRect;

    return SUCCESS;
}

S32 V2D_AddBitblitTask(V2DHandle handle,
                        const VideoFrameInfo *pstSrcFrame,
                        V2DArea *pstSrcRect,
                        VideoFrameInfo *pstDstFrame,
                        V2DArea *pstDstRect,
                        V2DCscMode eCscMode)
{
    MppV2DJobCtx *job = mpp_v2d_job_from_handle(handle);
    MppV2DTaskNode *node;
    V2DParam *param;
    V2DSurface dst;
    V2DSurface src;

    if ((job == NULL) || (pstDstFrame == NULL) || (pstDstRect == NULL) || (pstSrcFrame == NULL) || (pstSrcRect == NULL)) {
        return FAILURE;
    }

    if ((V2D_SurfaceFromVideoFrame(pstDstFrame, &dst) != SUCCESS) ||
        (V2D_SurfaceFromVideoFrame(pstSrcFrame, &src) != SUCCESS) ||
        (mpp_v2d_validate_rect(pstDstRect, &dst) != SUCCESS) ||
        (mpp_v2d_validate_rect(pstSrcRect, &src) != SUCCESS)) {
        return FAILURE;
    }

    if (mpp_v2d_append_task(job, MPP_V2D_TASK_BITBLIT, &node) != SUCCESS) {
        return FAILURE;
    }

    param = &node->submitTask.stParam;
    param->enL0Csc = eCscMode;
    param->stLayer0 = src;
    param->stL0Rect = *pstSrcRect;
    param->stDst = dst;
    param->stDstRect = *pstDstRect;
    param->stBlendConf.stBlendLayer[0].stBlendArea = *pstDstRect;

    return SUCCESS;
}

S32 V2D_AddBlendTask(V2DHandle handle,
                         const VideoFrameInfo *pstBackgroundFrame,
                         V2DArea *pstBackgroundRect,
                         const VideoFrameInfo *pstForegroundFrame,
                         V2DArea *pstForegroundRect,
                         const VideoFrameInfo *pstMaskFrame,
                         V2DArea *pstMaskRect,
                         VideoFrameInfo *pstDstFrame,
                         V2DArea *pstDstRect,
                         V2DBlendConf *pstBlendConf,
                         V2DRotateAngle eForeRotate,
                         V2DRotateAngle eBackRotate,
                         V2DCscMode eForeCscMode,
                         V2DCscMode eBackCscMode,
                         V2DPalette *pstPalette,
                         V2DDither eDither)
{
    MppV2DJobCtx *job = mpp_v2d_job_from_handle(handle);
    MppV2DTaskNode *node;
    V2DParam *param;
    V2DSurface background;
    V2DSurface foreground;
    V2DSurface mask;
    V2DSurface dst;
    V2DSurface *backgroundSurface = NULL;
    V2DSurface *foregroundSurface = NULL;
    V2DSurface *maskSurface = NULL;

    // info("V2D_AddBlendTask: handle=0x%llx fore_rotate=%d back_rotate=%d fore_csc_mode=%d back_csc_mode=%d dither=%d\n",
    // 	   (unsigned long long)handle,
    // 	   fore_rotate,
    // 	   back_rotate,
    // 	   fore_csc_mode,
    // 	   back_csc_mode,
    // 	   dither);
    // mpp_v2d_dump_surface("background", background);
    // mpp_v2d_dump_area("background_rect", background_rect);
    // mpp_v2d_dump_surface("dst", dst);
    // mpp_v2d_dump_area("dst_rect", dst_rect);
    // mpp_v2d_dump_blend_conf(blend_conf);

    if ((job == NULL) || (pstDstFrame == NULL) || (pstDstRect == NULL) || (pstBlendConf == NULL)) {
        return FAILURE;
    }

    if ((V2D_SurfaceFromVideoFrame(pstDstFrame, &dst) != SUCCESS) ||
        (mpp_v2d_validate_rect(pstDstRect, &dst) != SUCCESS)) {
        return FAILURE;
    }

    if ((pstBackgroundFrame != NULL) && (pstBackgroundRect != NULL)) {
        if ((V2D_SurfaceFromVideoFrame(pstBackgroundFrame, &background) != SUCCESS) ||
            (mpp_v2d_validate_rect(pstBackgroundRect, &background) != SUCCESS)) {
            return FAILURE;
        }
        backgroundSurface = &background;
    } else if ((pstBackgroundFrame != NULL) || (pstBackgroundRect != NULL)) {
        return FAILURE;
    }

    if ((pstForegroundFrame != NULL) && (pstForegroundRect != NULL)) {
        if ((V2D_SurfaceFromVideoFrame(pstForegroundFrame, &foreground) != SUCCESS) ||
            (mpp_v2d_validate_rect(pstForegroundRect, &foreground) != SUCCESS)) {
            return FAILURE;
        }
        foregroundSurface = &foreground;
    } else if ((pstForegroundFrame != NULL) || (pstForegroundRect != NULL)) {
        return FAILURE;
    }

    if ((pstMaskFrame != NULL) && (pstMaskRect != NULL)) {
        if ((V2D_SurfaceFromVideoFrame(pstMaskFrame, &mask) != SUCCESS) ||
            (mpp_v2d_validate_rect(pstMaskRect, &mask) != SUCCESS)) {
            return FAILURE;
        }
        maskSurface = &mask;
    } else if ((pstMaskFrame != NULL) || (pstMaskRect != NULL)) {
        return FAILURE;
    }

    if (mpp_v2d_append_task(job, MPP_V2D_TASK_BLEND, &node) != SUCCESS) {
        return FAILURE;
    }

    param = &node->submitTask.stParam;
    param->enL0Rt = eBackRotate;
    param->enL1Rt = eForeRotate;
    param->enL0Csc = eBackCscMode;
    param->enL1Csc = eForeCscMode;
    param->enDither = eDither;
    param->stBlendConf = *pstBlendConf;
    param->stDst = dst;
    param->stDstRect = *pstDstRect;

    if ((backgroundSurface != NULL) && (pstBackgroundRect != NULL)) {
        param->stLayer0 = *backgroundSurface;
        param->stL0Rect = *pstBackgroundRect;
    }

    if ((foregroundSurface != NULL) && (pstForegroundRect != NULL)) {
        param->stLayer1 = *foregroundSurface;
        param->stL1Rect = *pstForegroundRect;
    }

    if ((maskSurface != NULL) && (pstMaskRect != NULL)) {
        param->stMask = *maskSurface;
        param->stMaskRect = *pstMaskRect;
    }

    if (pstPalette != NULL) {
        param->stPalette = *pstPalette;
    }

    return SUCCESS;
}

S32 V2D_DrawLine(V2DHandle handle,
             VideoFrameInfo *pstDstFrame,
             V2DLine *pstLine)
{
    MppV2DJobCtx *job = mpp_v2d_job_from_handle(handle);
    MppV2DTaskNode *node;
    V2DSurface dst;

    if ((job == NULL) || (pstDstFrame == NULL) || (pstLine == NULL)) {
        return FAILURE;
    }

    if (V2D_SurfaceFromVideoFrame(pstDstFrame, &dst) != SUCCESS) {
        return FAILURE;
    }

    if (dst.bFbcEnable) {
        return FAILURE;
    }

    if (mpp_v2d_format_has_cpu_writer(dst.enFormat) != SUCCESS) {
        return FAILURE;
    }

    if ((pstLine->stColor.enFormat != V2D_COLOR_FORMAT_NV12) &&
        (pstLine->stColor.enFormat != V2D_COLOR_FORMAT_NV21)) {
        return FAILURE;
    }

    if (pstLine->u32LineWidth == 0U) {
        return FAILURE;
    }

    if (pstLine->u32LineWidth != 1U) {
        return FAILURE;
    }

    if ((pstLine->s32X0 < 0) || (pstLine->s32Y0 < 0) ||
        (pstLine->s32X1 < 0) || (pstLine->s32Y1 < 0)) {
        return FAILURE;
    }

    if (((U32)pstLine->s32X0 >= dst.u16W) || ((U32)pstLine->s32Y0 >= dst.u16H) ||
        ((U32)pstLine->s32X1 >= dst.u16W) || ((U32)pstLine->s32Y1 >= dst.u16H)) {
        return FAILURE;
    }

    if (mpp_v2d_append_task(job, MPP_V2D_TASK_CPU_LINE, &node) != SUCCESS) {
        return FAILURE;
    }

    node->cpuDstFrame = pstDstFrame;
    node->cpuLine = *pstLine;

    return SUCCESS;
}

S32 V2D_DrawRect(V2DHandle handle,
             VideoFrameInfo *pstDstFrame,
             V2DArea *pstRect,
             V2DFillColor *pstColor,
             U32 u32LineWidth)
{
    V2DSurface dst;
    V2DLine line;
    S32 left;
    S32 top;
    S32 right;
    S32 bottom;

    if ((pstDstFrame == NULL) || (pstRect == NULL) || (pstColor == NULL)) {
        return FAILURE;
    }

    if ((V2D_SurfaceFromVideoFrame(pstDstFrame, &dst) != SUCCESS) ||
        (mpp_v2d_validate_rect(pstRect, &dst) != SUCCESS)) {
        return FAILURE;
    }

    if (u32LineWidth != 1U) {
        return FAILURE;
    }

    left = pstRect->u16X;
    top = pstRect->u16Y;
    right = pstRect->u16X + pstRect->u16W - 1U;
    bottom = pstRect->u16Y + pstRect->u16H - 1U;

    memset(&line, 0, sizeof(line));
    line.stColor = *pstColor;
    line.u32LineWidth = u32LineWidth;

    line.s32X0 = left;
    line.s32Y0 = top;
    line.s32X1 = right;
    line.s32Y1 = top;
    if (V2D_DrawLine(handle, pstDstFrame, &line) != SUCCESS) {
        return FAILURE;
    }

    line.s32X0 = right;
    line.s32Y0 = top;
    line.s32X1 = right;
    line.s32Y1 = bottom;
    if (V2D_DrawLine(handle, pstDstFrame, &line) != SUCCESS) {
        return FAILURE;
    }

    line.s32X0 = right;
    line.s32Y0 = bottom;
    line.s32X1 = left;
    line.s32Y1 = bottom;
    if (V2D_DrawLine(handle, pstDstFrame, &line) != SUCCESS) {
        return FAILURE;
    }

    line.s32X0 = left;
    line.s32Y0 = bottom;
    line.s32X1 = left;
    line.s32Y1 = top;
    if (V2D_DrawLine(handle, pstDstFrame, &line) != SUCCESS) {
        return FAILURE;
    }

    return SUCCESS;
}

S32 V2D_DrawCircle(V2DHandle handle,
                   VideoFrameInfo *pstDstFrame,
                   V2DCircle *pstCircle)
{
    MppV2DJobCtx *job = mpp_v2d_job_from_handle(handle);
    MppV2DTaskNode *node;
    V2DSurface dst;
    S32 s32OuterRadius;

    if ((job == NULL) || (pstDstFrame == NULL) || (pstCircle == NULL)) {
        return FAILURE;
    }

    if (V2D_SurfaceFromVideoFrame(pstDstFrame, &dst) != SUCCESS) {
        return FAILURE;
    }

    if (dst.bFbcEnable) {
        return FAILURE;
    }

    if (mpp_v2d_format_has_cpu_writer(dst.enFormat) != SUCCESS) {
        return FAILURE;
    }

    if ((pstCircle->stColor.enFormat != V2D_COLOR_FORMAT_NV12) &&
        (pstCircle->stColor.enFormat != V2D_COLOR_FORMAT_NV21)) {
        return FAILURE;
    }

    if (pstCircle->u32Radius == 0U) {
        return FAILURE;
    }

    if ((pstCircle->s32CenterX < 0) || (pstCircle->s32CenterY < 0)) {
        return FAILURE;
    }

    s32OuterRadius = (S32)pstCircle->u32Radius;
    if ((pstCircle->s32CenterX - s32OuterRadius < 0) ||
        (pstCircle->s32CenterY - s32OuterRadius < 0) ||
        ((U32)(pstCircle->s32CenterX + s32OuterRadius) >= dst.u16W) ||
        ((U32)(pstCircle->s32CenterY + s32OuterRadius) >= dst.u16H)) {
        return FAILURE;
    }

    if (pstCircle->s32Thickness == 0) {
        return FAILURE;
    }

    if (mpp_v2d_append_task(job, MPP_V2D_TASK_CPU_CIRCLE, &node) != SUCCESS) {
        return FAILURE;
    }

    node->cpuDstFrame = pstDstFrame;
    node->cpuCircle = *pstCircle;

    return SUCCESS;
}

S32 V2D_DrawMask(V2DHandle handle,
                 const VideoFrameInfo *pstBackgroundFrame,
                 const V2DArea *pstBackgroundRect,
                 const VideoFrameInfo *pstForegroundFrame,
                 const V2DArea *pstForegroundRect,
                 const VideoFrameInfo *pstMaskFrame,
                 const V2DArea *pstMaskRect,
                 VideoFrameInfo *pstDstFrame,
                 const V2DArea *pstDstRect)
{
    V2DBlendConf blendConf;
    V2DArea backgroundRect;
    V2DArea foregroundRect;
    V2DArea maskRect;
    V2DArea dstRect;

    if ((handle == 0) || (pstBackgroundFrame == NULL) || (pstForegroundFrame == NULL) ||
        (pstMaskFrame == NULL) || (pstDstFrame == NULL)) {
        return FAILURE;
    }

    if (pstBackgroundRect != NULL) {
        backgroundRect = *pstBackgroundRect;
    } else {
        backgroundRect.u16X = 0;
        backgroundRect.u16Y = 0;
        backgroundRect.u16W = pstBackgroundFrame->stCommFrameInfo.u32Width;
        backgroundRect.u16H = pstBackgroundFrame->stCommFrameInfo.u32Height;
    }

    if (pstForegroundRect != NULL) {
        foregroundRect = *pstForegroundRect;
    } else {
        foregroundRect.u16X = 0;
        foregroundRect.u16Y = 0;
        foregroundRect.u16W = pstForegroundFrame->stCommFrameInfo.u32Width;
        foregroundRect.u16H = pstForegroundFrame->stCommFrameInfo.u32Height;
    }

    if (pstMaskRect != NULL) {
        maskRect = *pstMaskRect;
    } else {
        maskRect.u16X = 0;
        maskRect.u16Y = 0;
        maskRect.u16W = pstMaskFrame->stCommFrameInfo.u32Width;
        maskRect.u16H = pstMaskFrame->stCommFrameInfo.u32Height;
    }

    if (pstDstRect != NULL) {
        dstRect = *pstDstRect;
    } else {
        dstRect.u16X = 0;
        dstRect.u16Y = 0;
        dstRect.u16W = pstDstFrame->stCommFrameInfo.u32Width;
        dstRect.u16H = pstDstFrame->stCommFrameInfo.u32Height;
    }

    memset(&blendConf, 0, sizeof(blendConf));
    blendConf.enBlendCmd = V2D_BLENDCMD_ALPHA;
    blendConf.enMaskCmd = V2D_MASKCMD_AS_VALUE;
    blendConf.stBlendLayer[0].stBlendArea = backgroundRect;
    blendConf.stBlendLayer[1].stBlendArea = foregroundRect;
    blendConf.stBlendMaskArea = maskRect;
    blendConf.stBlendLayer[1].enBlendAlphaSource = V2D_BLENDALPHA_SOURCE_MASK;
    blendConf.stBlendLayer[1].enBlendPreAlphaFunc = V2D_BLEND_PRE_ALPHA_FUNC_DISABLE;
    blendConf.stBlendLayer[1].stBlendFactor.enSrcColorFactor = V2D_BLEND_SRC_ALPHA;
    blendConf.stBlendLayer[1].stBlendFactor.enSrcAlphaFactor = V2D_BLEND_ONE;
    blendConf.stBlendLayer[1].stBlendFactor.enDstColorFactor = V2D_BLEND_ONE_MINUS_SRC_ALPHA;
    blendConf.stBlendLayer[1].stBlendFactor.enDstAlphaFactor = V2D_BLEND_ZERO;

    return V2D_AddBlendTask(handle,
                            pstBackgroundFrame,
                            &backgroundRect,
                            pstForegroundFrame,
                            &foregroundRect,
                            pstMaskFrame,
                            &maskRect,
                            pstDstFrame,
                            &dstRect,
                            &blendConf,
                            V2D_ROT_0,
                            V2D_ROT_0,
                            V2D_CSC_MODE_RGB_2_BT709NARROW,
                            V2D_CSC_MODE_BUTT,
                            NULL,
                            V2D_NO_DITHER);
}

S32 V2D_ConvertFrame(V2DHandle handle,
                     const VideoFrameInfo *pstSrcFrame,
                     VideoFrameInfo *pstDstFrame,
                     V2DCscMode eCscMode)
{
    V2DArea srcRect;
    V2DArea dstRect;

    if ((handle == 0) || (pstSrcFrame == NULL) || (pstDstFrame == NULL)) {
        return FAILURE;
    }

    if ((pstSrcFrame->stCommFrameInfo.u32Width != pstDstFrame->stCommFrameInfo.u32Width) ||
        (pstSrcFrame->stCommFrameInfo.u32Height != pstDstFrame->stCommFrameInfo.u32Height)) {
        return FAILURE;
    }

    srcRect.u16X = 0;
    srcRect.u16Y = 0;
    srcRect.u16W = pstSrcFrame->stCommFrameInfo.u32Width;
    srcRect.u16H = pstSrcFrame->stCommFrameInfo.u32Height;
    dstRect.u16X = 0;
    dstRect.u16Y = 0;
    dstRect.u16W = pstDstFrame->stCommFrameInfo.u32Width;
    dstRect.u16H = pstDstFrame->stCommFrameInfo.u32Height;

    return V2D_AddBitblitTask(handle,
                     pstSrcFrame,
                     &srcRect,
                     pstDstFrame,
                     &dstRect,
                     eCscMode);
}

S32 V2D_ScaleFrame(V2DHandle handle,
                   const VideoFrameInfo *pstSrcFrame,
                   VideoFrameInfo *pstDstFrame,
                   V2DCscMode eCscMode)
{
    V2DArea srcRect;
    V2DArea dstRect;

    if ((handle == 0) || (pstSrcFrame == NULL) || (pstDstFrame == NULL)) {
        return FAILURE;
    }

    srcRect.u16X = 0;
    srcRect.u16Y = 0;
    srcRect.u16W = pstSrcFrame->stCommFrameInfo.u32Width;
    srcRect.u16H = pstSrcFrame->stCommFrameInfo.u32Height;
    dstRect.u16X = 0;
    dstRect.u16Y = 0;
    dstRect.u16W = pstDstFrame->stCommFrameInfo.u32Width;
    dstRect.u16H = pstDstFrame->stCommFrameInfo.u32Height;

    return V2D_AddBitblitTask(handle,
                     pstSrcFrame,
                     &srcRect,
                     pstDstFrame,
                     &dstRect,
                     eCscMode);
}

S32 V2D_RotateFrame(V2DHandle handle,
                const VideoFrameInfo *pstSrcFrame,
                VideoFrameInfo *pstDstFrame,
                V2DRotateAngle eRotate)
{
    V2DArea srcRect;
    V2DArea dstRect;
    V2DBlendConf blendConf;

    if ((handle == 0) || (pstSrcFrame == NULL) || (pstDstFrame == NULL)) {
        return FAILURE;
    }

    srcRect.u16X = 0;
    srcRect.u16Y = 0;
    srcRect.u16W = pstSrcFrame->stCommFrameInfo.u32Width;
    srcRect.u16H = pstSrcFrame->stCommFrameInfo.u32Height;
    dstRect.u16X = 0;
    dstRect.u16Y = 0;
    dstRect.u16W = pstDstFrame->stCommFrameInfo.u32Width;
    dstRect.u16H = pstDstFrame->stCommFrameInfo.u32Height;

    memset(&blendConf, 0, sizeof(blendConf));
    blendConf.stBlendLayer[0].stBlendArea = dstRect;

    return V2D_AddBlendTask(handle,
                      pstSrcFrame,
                      &srcRect,
                      NULL,
                      NULL,
                      NULL,
                      NULL,
                      pstDstFrame,
                      &dstRect,
                      &blendConf,
                      V2D_ROT_0,
                      eRotate,
                      V2D_CSC_MODE_BUTT,
                      V2D_CSC_MODE_BUTT,
                      NULL,
                      V2D_NO_DITHER);
}

S32 V2D_Adv2Layers(V2DHandle handle,
                       const VideoFrameInfo *pstBackgroundFrame,
                       const VideoFrameInfo *pstForegroundFrame,
                       const V2DArea *pstForegroundArea,
                       VideoFrameInfo *pstOutputFrame)
{
    U32 width;
    U32 height;
    V2DArea backgroundRect;
    V2DArea foregroundRect;
    V2DArea dstRect;
    V2DBlendConf blendConf;

    if ((handle == 0) ||
        (mpp_v2d_validate_adv_2layer_frames(pstBackgroundFrame, pstForegroundFrame, pstForegroundArea, pstOutputFrame) != SUCCESS)) {
        return FAILURE;
    }

    width = pstBackgroundFrame->stCommFrameInfo.u32Width;
    height = pstBackgroundFrame->stCommFrameInfo.u32Height;

    /* reference flow:
     *   background: NV12, full screen, no CSC
     *   foreground: BGRA8888, blended in caller specified area, CSC to YUV
     *   output    : NV12
     */

    backgroundRect.u16X = 0;
    backgroundRect.u16Y = 0;
    backgroundRect.u16W = width;
    backgroundRect.u16H = height;

    foregroundRect.u16X = 0;
    foregroundRect.u16Y = 0;
    foregroundRect.u16W = width;
    foregroundRect.u16H = height;

    dstRect = backgroundRect;

    memset(&blendConf, 0, sizeof(blendConf));
    blendConf.stBlendLayer[0].stBlendArea.u16X = 0;
    blendConf.stBlendLayer[0].stBlendArea.u16Y = 0;
    blendConf.stBlendLayer[0].stBlendArea.u16W = width;
    blendConf.stBlendLayer[0].stBlendArea.u16H = height;
    blendConf.stBlendLayer[1].stBlendArea = *pstForegroundArea;
    blendConf.stBlendLayer[1].stBlendFactor.enSrcAlphaFactor = V2D_BLEND_ONE;
    blendConf.stBlendLayer[1].stBlendFactor.enSrcColorFactor = V2D_BLEND_SRC_ALPHA;
    blendConf.stBlendLayer[1].stBlendFactor.enDstAlphaFactor = V2D_BLEND_ONE_MINUS_SRC_ALPHA;
    blendConf.stBlendLayer[1].stBlendFactor.enDstColorFactor = V2D_BLEND_ONE_MINUS_SRC_ALPHA;

    return V2D_AddBlendTask(handle,
                      pstBackgroundFrame,
                      &backgroundRect,
                      pstForegroundFrame,
                      &foregroundRect,
                      NULL,
                      NULL,
                      pstOutputFrame,
                      &dstRect,
                      &blendConf,
                      V2D_ROT_0,
                      V2D_ROT_0,
                      V2D_CSC_MODE_RGB_2_BT709NARROW,
                      V2D_CSC_MODE_BUTT,
                      NULL,
                      V2D_NO_DITHER);
}
