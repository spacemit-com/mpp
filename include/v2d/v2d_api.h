/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Spacemit Co., Ltd.
 */

#ifndef MPP_V2D_API_H
#define MPP_V2D_API_H

#include "vb_type.h"
#include "v2d_type.h"

#ifdef __cplusplus
extern "C" {
#endif

S32 V2D_BeginJob(V2DHandle *pHandle);
S32 V2D_EndJob(V2DHandle handle);
S32 V2D_CancelJob(V2DHandle handle);

S32 V2D_AddFillTask(V2DHandle handle,
                    VideoFrameInfo *pstDstFrame,
                    V2DArea *pstDstRect,
                    V2DFillColor *pstFillColor);

S32 V2D_AddBitblitTask(V2DHandle handle,
                        const VideoFrameInfo *pstSrcFrame,
                        V2DArea *pstSrcRect,
                        VideoFrameInfo *pstDstFrame,
                        V2DArea *pstDstRect,
                        V2DCscMode eCscMode);

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
                    V2DDither eDither);


S32 V2D_DrawLine(V2DHandle handle,
                 VideoFrameInfo *pstDstFrame,
                 V2DLine *pstLine);

S32 V2D_DrawRect(V2DHandle handle,
                VideoFrameInfo *pstDstFrame,
                V2DArea *pstRect,
                V2DFillColor *pstColor,
                U32 u32LineWidth);

S32 V2D_DrawCircle(V2DHandle handle,
                   VideoFrameInfo *pstDstFrame,
                   V2DCircle *pstCircle);

S32 V2D_DrawMask(V2DHandle handle,
                const VideoFrameInfo *pstBackgroundFrame,
                const V2DArea *pstBackgroundRect,
                const VideoFrameInfo *pstForegroundFrame,
                const V2DArea *pstForegroundRect,
                const VideoFrameInfo *pstMaskFrame,
                const V2DArea *pstMaskRect,
                VideoFrameInfo *pstDstFrame,
                const V2DArea *pstDstRect);

S32 V2D_Adv2Layers(V2DHandle handle,
                    const VideoFrameInfo *pstBackgroundFrame,
                    const VideoFrameInfo *pstForegroundFrame,
                    const V2DArea *pstForegroundArea,
                    VideoFrameInfo *pstOutputFrame);

S32 V2D_ConvertFrame(V2DHandle handle,
                    const VideoFrameInfo *pstSrcFrame,
                    VideoFrameInfo *pstDstFrame,
                    V2DCscMode eCscMode);

S32 V2D_RotateFrame(V2DHandle handle,
                    const VideoFrameInfo *pstSrcFrame,
                    VideoFrameInfo *pstDstFrame,
                    V2DRotateAngle eRotate);

S32 V2D_ScaleFrame(V2DHandle handle,
                    const VideoFrameInfo *pstSrcFrame,
                    VideoFrameInfo *pstDstFrame,
                    V2DCscMode eCscMode);

#ifdef __cplusplus
}
#endif

#endif
