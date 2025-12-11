/*
* V2D test for Spacemit
* Copyright (C) 2023 Spacemit Co., Ltd.
*
*/

#ifndef __V2D_API_H__
#define __V2D_API_H__

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef  __cplusplus
extern "C" {
#endif

#include "v2d_type.h"

/*****************************************************************************
 Prototype    : V2D_BeginJob
 Description  : Begin a v2d job,then add task into the job,v2d will finish all the task in the job.
 Input        : V2D_HANDLE *phHandle
 Output       : None
 Return Value :
 Calls        :
 Called By    :
*****************************************************************************/
int32_t V2D_BeginJob(V2D_HANDLE *phHandle);

/*****************************************************************************
 Prototype    : V2D_EndJob
 Description  : End a job,all tasks in the job will be submmitted to v2d
 Input        : V2D_HANDLE hHandle
 Output       : None
 Return Value :
 Calls        :
 Called By    :
*****************************************************************************/
int32_t V2D_EndJob(V2D_HANDLE hHandle);

/*****************************************************************************
 Prototype    : V2D_AddFillTask
 Description  : add a Fill task into a job
 Input        : V2D_HANDLE hHandle
 Output       : None
 Return Value :
 Calls        :
 Called By    :
*****************************************************************************/
int32_t V2D_AddFillTask(V2D_HANDLE hHandle, V2D_SURFACE_S *pstDst, V2D_AREA_S *pstDstRect,  V2D_FILLCOLOR_S *pstFillColor);

/*****************************************************************************
 Prototype    : V2D_AddBitblitTask
 Description  : add a Bitblit task into a job
 Input        : V2D_HANDLE hHandle
 Output       : None
 Return Value :
 Calls        :
 Called By    :
*****************************************************************************/
int32_t V2D_AddBitblitTask(V2D_HANDLE hHandle, V2D_SURFACE_S *pstDst, V2D_AREA_S *pstDstRect, V2D_SURFACE_S *pstSrc,
                                V2D_AREA_S *pstSrcRect, V2D_CSC_MODE_E enCSCMode);

/*****************************************************************************
 Prototype    : V2D_AddBlendTask
 Description  : add a blend task into a job
 Input        : V2D_HANDLE hHandle
 Output       : None
 Return Value :
 Calls        :
 Called By    :
*****************************************************************************/
int32_t V2D_AddBlendTask(V2D_HANDLE hHandle, 
                             V2D_SURFACE_S *pstBackGround,
                             V2D_AREA_S *pstBackGroundRect,
                             V2D_SURFACE_S *pstForeGround,
                             V2D_AREA_S *pstForeGroundRect,
                             V2D_SURFACE_S *pstMask,
                             V2D_AREA_S *pstMaskRect,
                             V2D_SURFACE_S *pstDst,
                             V2D_AREA_S *pstDstRect,
                             V2D_BLEND_CONF_S *pstBlendConf,
                             V2D_ROTATE_ANGLE_E enForeRotateAngle,
                             V2D_ROTATE_ANGLE_E enBackRotateAngle,
                             V2D_CSC_MODE_E enForeCSCMode,
                             V2D_CSC_MODE_E enBackCSCMode,
                             V2D_PALETTE_S *pstPalette,
                             V2D_DITHER_E dither);

#ifdef  __cplusplus
}
#endif

#endif
