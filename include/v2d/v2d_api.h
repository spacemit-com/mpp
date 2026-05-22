/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Spacemit Co., Ltd.
 */

#ifndef V2D_API_H
#define V2D_API_H

#include "vb_type.h"
#include "v2d_type.h"

#ifdef __cplusplus
extern "C" {
#endif

S32 V2D_BeginJob(V2DHandle *pHandle);
S32 V2D_EndJob(V2DHandle handle);
S32 V2D_CancelJob(V2DHandle handle);

S32 V2D_AddFillTask(V2DHandle handle, VideoFrameInfo *pstDstFrame, V2DArea *pstDstRect, V2DFillColor *pstFillColor);

S32 V2D_AddBitblitTask(
    V2DHandle handle,
    const VideoFrameInfo *pstSrcFrame,
    V2DArea *pstSrcRect,
    VideoFrameInfo *pstDstFrame,
    V2DArea *pstDstRect,
    V2DCscMode eCscMode);

S32 V2D_AddBlendTask(
    V2DHandle handle,
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
    V2DDither eDither);

S32 V2D_DrawLine(V2DHandle handle, VideoFrameInfo *pstDstFrame, V2DLine *pstLine);

S32 V2D_DrawRect(
    V2DHandle handle, VideoFrameInfo *pstDstFrame, V2DArea *pstRect, V2DFillColor *pstColor, U32 u32LineWidth);

S32 V2D_DrawCircle(V2DHandle handle, VideoFrameInfo *pstDstFrame, V2DCircle *pstCircle);

S32 V2D_DrawMask(
    V2DHandle handle,
    const VideoFrameInfo *pstBackgroundFrame,
    const VideoFrameInfo *pstForegroundFrame,
    const VideoFrameInfo *pstMaskFrame,
    VideoFrameInfo *pstDstFrame);

S32 V2D_Adv2Layers(
    V2DHandle handle,
    const VideoFrameInfo *pstBackgroundFrame,
    const VideoFrameInfo *pstForegroundFrame,
    VideoFrameInfo *pstOutputFrame);

S32 V2D_ConvertFrame(V2DHandle handle, const VideoFrameInfo *pstSrcFrame, VideoFrameInfo *pstDstFrame);

S32 V2D_RotateFrame(
    V2DHandle handle, const VideoFrameInfo *pstSrcFrame, VideoFrameInfo *pstDstFrame, V2DRotateAngle eRotate);

S32 V2D_ResizeFrame(V2DHandle handle, const VideoFrameInfo *pstSrcFrame, VideoFrameInfo *pstDstFrame);

S32 V2D_BorderFill(
    V2DHandle handle,
    const VideoFrameInfo *pstSrcFrame,
    VideoFrameInfo *pstDstFrame,
    U32 u32Top,
    U32 u32Bottom,
    U32 u32Left,
    U32 u32Right,
    const V2DFillColor *pstBorderColor);

#ifdef __cplusplus
}
#endif

#endif
