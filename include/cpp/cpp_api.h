/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    cpp_api.h
 * @Date      :    2026-4-10
 * @Author    :    SPACEMIT
 * @Brief     :    CPP public API for MPP.
 *------------------------------------------------------------------------------
 */

#ifndef __CPP_API_H__
#define __CPP_API_H__

#include "type.h"
#include "vb_type.h"
#include "cpp_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/**
 * @description: Initialize CPP module and allocate global resources.
 *               Must be called before any other CPP interface.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_Init(VOID);

/**
 * @description: Deinitialize CPP module and release global resources.
 *               Paired with CPP_Init.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_DeInit(VOID);

/**
 * @description: Create a CPP processing group.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_CreateGrp(CPP_GRP CppGrp);

/**
 * @description: Destroy a CPP processing group.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_DestroyGrp(CPP_GRP CppGrp);

/**
 * @description: Set CPP group attributes such as image size, pixel format, and process mode.
 *               Must be called before CPP_StartGrp.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @param {const CppGrpAttrS *} pstGrpAttr Pointer to group attribute structure.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_SetGrpAttr(CPP_GRP CppGrp, const CppGrpAttrS *pstGrpAttr);

/**
 * @description: Get current CPP group attributes.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @param {CppGrpAttrS *} pstGrpAttr Output parameter to receive group attributes.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_GetGrpAttr(CPP_GRP CppGrp, CppGrpAttrS *pstGrpAttr);

/**
 * @description: Reserved legacy interface. First pull-mode implementation keeps callback internal
 *               in CPP and no longer exposes completion callback behavior to users.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @param {CppCallback} pfnCallback Unused in pull mode, should be NULL.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_SetCallback(CPP_GRP CppGrp, CppCallback pfnCallback);

/**
 * @description: Start CPP processing group.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_StartGrp(CPP_GRP CppGrp);

/**
 * @description: Stop CPP processing group.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_StopGrp(CPP_GRP CppGrp);

/**
 * @description: Set output attributes for a CPP group.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @param {const CppChnAttrS *} pstChnAttr Pointer to output attribute structure.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_SetAttr(CPP_GRP CppGrp, const CppChnAttrS *pstChnAttr);

/**
 * @description: Get output attributes for a CPP group.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @param {CppChnAttrS *} pstChnAttr Output parameter to receive output attributes.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_GetAttr(CPP_GRP CppGrp, CppChnAttrS *pstChnAttr);

/**
 * @description: Set frame-rate control for CPP output channel.
 *               When output step is smaller than input step, some input frames
 *               are dropped before submitting to CPP hardware.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @param {const CppFrameRateCtrlS *} pstFrameRateCtrl Pointer to frame-rate control structure.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_SetFrameRate(CPP_GRP CppGrp, const CppFrameRateCtrlS *pstFrameRateCtrl);

/**
 * @description: Get current frame-rate control configuration for CPP output channel.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @param {CppFrameRateCtrlS *} pstFrameRateCtrl Output parameter to receive frame-rate control configuration.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_GetFrameRate(CPP_GRP CppGrp, CppFrameRateCtrlS *pstFrameRateCtrl);

/**
 * @description: Enable a CPP group output.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_Enable(CPP_GRP CppGrp);

/**
 * @description: Disable a CPP group output.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_Disable(CPP_GRP CppGrp);

/**
 * @description: Submit one frame for CPP processing.
 *               CPP allocates and manages output VB buffers internally.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @param {const VideoFrameInfo *} pstInFrame Input frame.
 * @param {U32} u32FrameId Frame sequence ID.
 * @param {VOID *} pUserData User private pointer passed through to internal metadata.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_SendFrame(CPP_GRP CppGrp,
                  const VideoFrameInfo *pstInFrame,
                  U32 u32FrameId,
                  VOID *pUserData);

/**
 * @description: Get one processed frame from CPP output channel in pull mode.
 *               The frame must be returned by CPP_ReleaseChnFrame after use.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @param {VideoFrameInfo *} pstVideoFrame Output parameter to receive processed frame.
 * @param {S32} s32MilliSec Timeout in milliseconds, 0 for non-blocking.
 * @return {S32} Returns 0 on success, error code on failure or timeout.
 */
S32 CPP_GetFrame(CPP_GRP CppGrp, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec);

/**
 * @description: Release one processed frame obtained from CPP_GetChnFrame.
 *               Internally releases the output VB buffer associated with the completed frame.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @param {const VideoFrameInfo *} pstVideoFrame Pointer to frame information to release.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_ReleaseFrame(CPP_GRP CppGrp, const VideoFrameInfo *pstVideoFrame);

/**
 * @description: Dump CPP output frames to files for debugging.
 * @param {CPP_GRP} CppGrp CPP group ID.
 * @param {const CHAR *} pszPath Output directory path.
 * @param {U32} u32Count Number of frames to dump.
 * @return {S32} Returns 0 on success, error code on failure.
 */
S32 CPP_DumpFrame(CPP_GRP CppGrp, const CHAR *pszPath, U32 u32Count);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __CPP_API_H__ */
