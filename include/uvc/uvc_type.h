/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    uvc_types.h
* @Date      :    2026-3-16
* @Author    :    SPACEMIT
* @Brief     :    Media Interface for MPP.
*------------------------------------------------------------------------------
*/

#ifndef UVC_TYPE_H
#define UVC_TYPE_H

#include "type.h"
#include "vb_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== Basic Types ======================== */

typedef S32 UVC_DEV;
typedef S32 UVC_CHN;

#define UVC_MAX_DEV_NUM     4
#define UVC_MAX_CHN_NUM     1

/* ======================== Enums ======================== */

/* ======================== Device Attributes ======================== */

typedef struct _UvcDevAttr {
    CHAR acDevNode[128];                /* UVC device node path, e.g. "/dev/video0" */
} UvcDevAttr;

/* ======================== Channel Attributes ======================== */

typedef struct _UvcChnAttr {
    U32 u32Width;                       /* capture width */
    U32 u32Height;                      /* capture height */
    MppPixelFormat ePixelFormat;        /* pixel format: PIXEL_FORMAT_MJPEG, etc. */
    U32 u32Fps;                         /* target frame rate */
    U32 u32Depth;                       /* user frame queue depth; 0 = no user queue (bind-only) */
} UvcChnAttr;

/* ======================== Effect Attributes ======================== */

typedef struct _UvcEffectAttr {
    S32 s32Brightness;                  /* brightness [0, 255] */
    S32 s32Contrast;                    /* contrast   [0, 255] */
    S32 s32Saturation;                  /* saturation [0, 255] */
    S32 s32Hue;                         /* hue        [0, 255] */
    S32 s32Sharpness;                   /* sharpness  [0, 255] */
    U32 u32Gamma;                       /* gamma      [1, 500] */
    U32 u32Gain;                        /* gain       [0, 255] */
    BOOL bAutoWhiteBalance;             /* auto white balance enable */
    U32 u32WhiteBalanceTemp;             /* white balance temperature [2800, 6500] */
    BOOL bBacklightComp;                /* backlight compensation enable */
    BOOL bAutoExposure;                 /* auto exposure enable */
    U32 u32ExposureTime;                /* manual exposure time (100us units) */
} UvcEffectAttr;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /*__UVC_TYPE_H__ */
