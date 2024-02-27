/*
 * Copyright (C) 2019 ASR Micro Limited
 * All Rights Reserved.
 */

#ifndef __ASR_V2D_API_H__
#define __ASR_V2D_API_H__

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef  __cplusplus
extern "C" {
#endif

#include "asr_v2d_type.h"

/*****************************************************************************
 Prototype    : ASR_V2D_BeginJob
 Description  : Begin a v2d job,then add task into the job,v2d will finish all the task in the job.
 Input        : V2D_HANDLE *phHandle
 Output       : None
 Return Value :
 Calls        :
 Called By    :
*****************************************************************************/
int32_t ASR_V2D_BeginJob(V2D_HANDLE *phHandle);

/*****************************************************************************
 Prototype    : ASR_V2D_EndJob
 Description  : End a job,all tasks in the job will be submmitted to v2d
 Input        : V2D_HANDLE hHandle
 Output       : None
 Return Value :
 Calls        :
 Called By    :
*****************************************************************************/
int32_t ASR_V2D_EndJob(V2D_HANDLE hHandle);

/*****************************************************************************
 Prototype    : ASR_V2D_AddFillTask
 Description  : add a Fill task into a job
 Input        : V2D_HANDLE hHandle
 Output       : None
 Return Value :
 Calls        :
 Called By    :
*****************************************************************************/
int32_t ASR_V2D_AddFillTask(V2D_HANDLE hHandle, V2D_SURFACE_S *pstSrc, V2D_AREA_S *pstFillRect, V2D_SURFACE_S *pstDst, V2D_FILLCOLOR_S *pstFillColor);

/*****************************************************************************
 Prototype    : ASR_V2D_AddBitblitTask
 Description  : add a Bitblit task into a job
 Input        : V2D_HANDLE hHandle
 Output       : None
 Return Value :
 Calls        :
 Called By    :
*****************************************************************************/
int32_t ASR_V2D_AddBitblitTask(V2D_HANDLE hHandle, V2D_SURFACE_S *pstDst, V2D_AREA_S *pstDstRect, V2D_SURFACE_S *pstSrc,
                                V2D_AREA_S *pstSrcRect, V2D_CSC_MODE_E enCSCMode, V2D_PALETTE_S* pstPalette, uint8_t opa);

/*****************************************************************************
 Prototype    : ASR_V2D_AddBlendTask
 Description  : add a Jpeg task into a job
 Input        : V2D_HANDLE hHandle
 Output       : None
 Return Value :
 Calls        :
 Called By    :
*****************************************************************************/
int32_t ASR_V2D_AddBlendTask(V2D_HANDLE hHandle, 
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

#endif /*end of __MPI_V2D_H__*/
