/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    cpp_type.h
* @Date      :    2026-4-10
* @Author    :    SPACEMIT
* @Brief     :    CPP type definitions for MPP.
*------------------------------------------------------------------------------
*/

#ifndef CPP_TYPE_H
#define CPP_TYPE_H

#include "type.h"
#include "vb_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define CPP_INVALID_GRP_ID   (-1)
#define CPP_INVALID_CHN_ID   (-1)

#define CPP_MAX_GRP_NUM      4
#define CPP_MAX_CHN_NUM      4

#define CPP_MIN_WIDTH        64
#define CPP_MIN_HEIGHT       64
#define CPP_MAX_WIDTH        8192
#define CPP_MAX_HEIGHT       8192

#define CPP_3DNR_LAYER_NUM   5
#define CPP_3DNR_GAIN_NUM    12
#define CPP_NIGHT_SEG_NUM    9

typedef S32 CPP_GRP;
typedef S32 CPP_CHN;

typedef enum _CppProcessMode {
    CPP_PROCESS_MODE_INVALID = -1,
    CPP_PROCESS_MODE_FRAME = 0,
    CPP_PROCESS_MODE_SLICE,
    CPP_PROCESS_MODE_MAX
} CppProcessMode;

typedef enum _CppPixelFormat {
    CPP_PIXEL_FORMAT_INVALID = 0,
    CPP_PIXEL_FORMAT_USE_FRAME = 1,
} CppPixelFormat;

typedef enum _CppCallbackEvent {
    CPP_CALLBACK_EVENT_FRAME_DONE = 0,
    CPP_CALLBACK_EVENT_ERROR,
    CPP_CALLBACK_EVENT_TIMEOUT,
    CPP_CALLBACK_EVENT_MAX
} CppCallbackEvent;

typedef struct _CppGrpAttrS {
    U32 u32Width;
    U32 u32Height;
    MppPixelFormat ePixelFormat;
    CppProcessMode eProcessMode;
} CppGrpAttrS;

typedef struct _CppChnAttrS {
    BOOL bEnable;
    U32 u32Width;
    U32 u32Height;
    MppPixelFormat ePixelFormat;
} CppChnAttrS;

typedef struct _CppFrameRateCtrlS {
    U32 u32InputFrameStep;
    U32 u32OutputFrameStep;
} CppFrameRateCtrlS;

typedef struct _CppProcCfgS {
    BOOL bEnable3Dnr;
    BOOL bEnableDci;
    BOOL bEnableSharpen;
    BOOL bEnableLdch;
    BOOL bEnableRotation;
    Rotation eRotation;
} CppProcCfgS;

typedef struct _CppBufferPairS {
    const VideoFrameInfo *pstInFrame;
    const VideoFrameInfo *pstOutFrame;
    U32 u32FrameId;
    VOID                 *pUserData;
} CppBufferPairS;

typedef struct _CppCallbackInfoS {
    CppCallbackEvent eEvent;
    CPP_GRP CppGrp;
    CPP_CHN CppChn;
    S32 s32FrameId;
    S32 s32Result;
    const VideoFrameInfo *pstInFrame;
    const VideoFrameInfo *pstOutFrame;
    VOID             *pUserData;
} CppCallbackInfoS;

typedef VOID (*CppCallback)(const CppCallbackInfoS *pstCbInfo);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* CPP_TYPE_H */
