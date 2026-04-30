/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_type.h
 * @Date      :    2026-3-24
 * @Author    :    SPACEMIT
 * @Brief     :    VI type definitions for MPP.
 *------------------------------------------------------------------------------
 */

#ifndef __VI_TYPE_H__
#define __VI_TYPE_H__

#include "type.h"
#include "vb_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define VI_INVALID_DEV_ID   (-1)
#define VI_INVALID_CHN_ID   (-1)

#define VI_MAX_DEV_NUM      4
#define VI_MAX_CHN_NUM      6
#define VI_MAX_PHY_CHN_NUM  4
#define VI_MAX_RAW_CHN_NUM  4

#define VI_MIN_WIDTH        64 //待定
#define VI_MIN_HEIGHT       64	//待定
#define VI_MAX_WIDTH        3864 //待定
#define VI_MAX_HEIGHT       2192 //待定

typedef S32 VI_DEV;
typedef S32 VI_CHN;

typedef enum _ViWorkMode {
    VI_WORK_MODE_INVALID = -1,
    VI_WORK_MODE_ONLINE = 0, //从sensor获取数据
    VI_WORK_MODE_OFFLINE, // 从ddr 获取数据
    VI_WORK_MODE_ISP_BYPASS, //不走isp，直接从ccic 取raw图出来
    VI_WORK_MODE_MAX
} ViWorkMode;

typedef enum _ViChnType {
    VI_CHN_TYPE_PHYSICAL = 0,
    VI_CHN_TYPE_VIRTUAL,
    VI_CHN_TYPE_MAX
} ViChnType;

typedef enum _ViRawType {
    VI_RAW_TYPE_UNKNOWN = 0,
    VI_RAW_TYPE_8BIT,
    VI_RAW_TYPE_10BIT,
    VI_RAW_TYPE_12BIT,
    VI_RAW_TYPE_14BIT,
    VI_RAW_TYPE_16BIT,
    VI_RAW_TYPE_MAX
} ViRawType;

typedef enum _ViRotateModeE {
    VI_ROT_0 = 0,
    VI_ROT_90 = 1,
    VI_ROT_180 = 2,
    VI_ROT_270 = 3,
    VI_ROT_MIRROR = 4,
    VI_ROT_FLIP = 5,
    VI_ROT_BUTT
} ViRotateModeE;

typedef enum _ViStrideAlignE {
    VI_STRIDE_ALIGN_DEFAULT = 0,
    VI_STRIDE_ALIGN_16 = 16,
    VI_STRIDE_ALIGN_32 = 32,
    VI_STRIDE_ALIGN_64 = 64,
    VI_STRIDE_ALIGN_BUTT
} ViStrideAlignE;

typedef struct _ViDevAttrS {
    ViWorkMode     eWorkMode;
    U32             u32Width;
    U32             u32Height;
    U32             u32MipiLaneNum;
    U32       		u32mbps;//保留 or 去掉都可 配置mipi 速率
    BOOL            bCapture2Preview;
} ViDevAttrS;

typedef struct _ViChnAttrS {
    ViChnType          eChnType;
    MppPixelFormat      ePixelFormat;
    U32                 u32Width;
    U32                 u32Height;
    BOOL				bMirror;
    BOOL				bFlip;
    ViRotateModeE       eRotateMode;
    BOOL                bCropEnable;
    U32                 u32CropX;
    U32                 u32CropY;
    U32                 u32CropWidth;
    U32                 u32CropHeight;
    ViStrideAlignE      eStrideAlign;
} ViChnAttrS;

typedef struct _ViBayerReadAttr {
    BOOL bGenTiming;
    S32  s32FrmRate;
} ViBayerReadAttr;

typedef struct _ViFrameRateCtrlS {
    U32 u32InputFrameStep;
    U32 u32OutputFrameStep;
} ViFrameRateCtrlS;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __VI_TYPE_H__ */
