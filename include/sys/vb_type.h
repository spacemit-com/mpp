/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    vb_types.h
* @Date      :    2026-3-16
* @Author    :    rmwei(rongmin.wei@spacemit.com)
* @Brief     :    Media Interface for MPP.
*------------------------------------------------------------------------------
*/

#ifndef VB_TYPE_H
#define VB_TYPE_H

#include "type.h"
#include "sys_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define FRAME_MAX_PLANE 6

typedef enum _VbRemapMode {
    VB_REMAP_MODE_NONE        = 0,
    VBUF_REMAP_MODE_NOCACHE   = 1,
    VBUF_REMAP_MODE_CACHED    = 2,
    VBUF_REMAP_MODE_MAX
} VbRemapMode;

typedef struct _VbPoolCfg {
    U32 u32BufSize;
    U32 u32BufCnt;
    ModId eModId;
    VbRemapMode eRemapMode;
} VbPoolCfg;

typedef enum _FrameType {
    FRAME_TYPE_COMMON,
    FRAME_TYPE_VI,
    FRAME_TYPE_CPP,
    FRAME_TYPE_VO,
    FRAME_TYPE_VENC,
    FRAME_TYPE_VDEC,

    FRAME_TYPE_MAX
} FrameType;

typedef enum _Rotation {
    ROTATION_0   = 0,
    ROTATION_90  = 1,
    ROTATION_180 = 2,
    ROTATION_270 = 3,
    ROTATION_MAX
} Rotation;

/***
* @description: pixelformat mpp or some other platform may use.
*/
typedef enum _MppPixelFormat {
    MPP_PIXEL_FORMAT_UNKNOWN = 0,

    /***
    * YYYYYYYYVVUU
    */
    MPP_PIXEL_FORMAT_YV12,

    /***
    * YYYYYYYYUUVV  YU12/YUV420P is the same
    */
    MPP_PIXEL_FORMAT_I420,

    /***
    * YYYYYYYYVUVU
    */
    MPP_PIXEL_FORMAT_NV21,

    /***
    * YYYYYYYYUVUV
    */
    MPP_PIXEL_FORMAT_NV12,

    /***
    * 11111111 11000000, 16bit only use 10bit
    */
    MPP_PIXEL_FORMAT_YV12_P010,

    /***
    * 11111111 11000000, 16bit only use 10bit
    */
    MPP_PIXEL_FORMAT_I420_P010,

    /***
    * 11111111 11000000, 16bit only use 10bit
    */
    MPP_PIXEL_FORMAT_NV21_P010,

    /***
    * 11111111 11000000, 16bit only use 10bit
    */
    MPP_PIXEL_FORMAT_NV12_P010,
    MPP_PIXEL_FORMAT_YV12_P016,
    MPP_PIXEL_FORMAT_I420_P016,
    MPP_PIXEL_FORMAT_NV21_P016,
    MPP_PIXEL_FORMAT_NV12_P016,

    /***
    * YYYYUUVV, YU16 is the same
    */
    MPP_PIXEL_FORMAT_YUV422P,

    /***
    * YYYYVVUU
    */
    MPP_PIXEL_FORMAT_YV16,

    /***
    * YYYYUVUV  NV16 is the same
    */
    MPP_PIXEL_FORMAT_YUV422SP,

    /***
    * YYYYVUVU
    */
    MPP_PIXEL_FORMAT_NV61,
    MPP_PIXEL_FORMAT_YUV422P_P010,
    MPP_PIXEL_FORMAT_YV16_P010,
    MPP_PIXEL_FORMAT_YUV422SP_P010,
    MPP_PIXEL_FORMAT_NV61_P010,

    /***
    * YYUUVV
    */
    MPP_PIXEL_FORMAT_YUV444P,

    /***
    * YYUVUV
    */
    MPP_PIXEL_FORMAT_YUV444SP,
    MPP_PIXEL_FORMAT_YUYV,
    MPP_PIXEL_FORMAT_YVYU,
    MPP_PIXEL_FORMAT_UYVY,
    MPP_PIXEL_FORMAT_VYUY,
    MPP_PIXEL_FORMAT_YUV_MB32_420,
    MPP_PIXEL_FORMAT_YUV_MB32_422,
    MPP_PIXEL_FORMAT_YUV_MB32_444,
    MPP_PIXEL_FORMAT_YUV_MAX,

    MPP_PIXEL_FORMAT_RGB_MIN,
    MPP_PIXEL_FORMAT_RGBA,
    MPP_PIXEL_FORMAT_ARGB,
    MPP_PIXEL_FORMAT_ABGR,
    MPP_PIXEL_FORMAT_BGRA,
    MPP_PIXEL_FORMAT_RGBA_5658,
    MPP_PIXEL_FORMAT_ARGB_8565,
    MPP_PIXEL_FORMAT_ABGR_8565,
    MPP_PIXEL_FORMAT_BGRA_5658,
    MPP_PIXEL_FORMAT_RGBA_5551,
    MPP_PIXEL_FORMAT_ARGB_1555,
    MPP_PIXEL_FORMAT_ABGR_1555,
    MPP_PIXEL_FORMAT_BGRA_5551,
    MPP_PIXEL_FORMAT_RGBA_4444,
    MPP_PIXEL_FORMAT_ARGB_4444,
    MPP_PIXEL_FORMAT_ABGR_4444,
    MPP_PIXEL_FORMAT_BGRA_4444,
    MPP_PIXEL_FORMAT_RGB_888,
    MPP_PIXEL_FORMAT_BGR_888,
    MPP_PIXEL_FORMAT_RGB_565,
    MPP_PIXEL_FORMAT_BGR_565,
    MPP_PIXEL_FORMAT_RGB_555,
    MPP_PIXEL_FORMAT_BGR_555,
    MPP_PIXEL_FORMAT_RGB_444,
    MPP_PIXEL_FORMAT_BGR_444,
    MPP_PIXEL_FORMAT_RGB_MAX,

    MPP_PIXEL_FORMAT_RGB_BAYER_8BITS,
    MPP_PIXEL_FORMAT_RGB_BAYER_10BITS,
    MPP_PIXEL_FORMAT_RGB_BAYER_12BITS,
    MPP_PIXEL_FORMAT_RGB_BAYER_14BITS,
    MPP_PIXEL_FORMAT_RGB_BAYER_16BITS,
    MPP_PIXEL_FORMAT_RGB_BAYER_20BITS,

    MPP_PIXEL_FORMAT_AFBC_YUV420_8,
    MPP_PIXEL_FORMAT_AFBC_YUV420_10,
    MPP_PIXEL_FORMAT_AFBC_YUV422_8,
    MPP_PIXEL_FORMAT_AFBC_YUV422_10,

    /***
    * for usb camera
    */
    MPP_PIXEL_FORMAT_H264,
    MPP_PIXEL_FORMAT_MJPEG,

    MPP_PIXEL_FORMAT_MAX,
} MppPixelFormat;

typedef enum _CompressMode {
    COMPRESS_MODE_NONE = 0,
    COMPRESS_MODE_AFBC,
    COMPRESS_MODE_MAX
} CompressMode;

typedef enum _ColorSpace {
    COLOR_SPACE_BT601 = 0,
    COLOR_SPACE_BT601_LIMIT,
    COLOR_SPACE_BT709,
    COLOR_SPACE_BT709_LIMIT,
    COLOR_SPACE_USER,
    COLOR_SPACE_MAX
} ColorSpace;

typedef struct _VideoFrame {
    U32 u32TotalSize;
    U32 u32PlaneNum;
    U32 u32PlaneStride[FRAME_MAX_PLANE];
    U32 u32PlaneSize[FRAME_MAX_PLANE];
    U32 u32PlaneSizeValid[FRAME_MAX_PLANE];
    U64 u64PlanePhyAddr[FRAME_MAX_PLANE];
    UL ulPlaneVirAddr[FRAME_MAX_PLANE];
    UL u32Fd[FRAME_MAX_PLANE];
    U64 u64PTS;
    U32 u32FrameFlag;
    Rotation enRotation;
    U32 u32PrivateData;
} VideoFrame;

typedef struct _CommonFrameInfo {
    U32 u32Width;
    U32 u32Height;
    U32 u32Align;
    MppPixelFormat ePixelFormat;
    CompressMode eCompressMode;
    ColorSpace eColorSpace;
} CommonFrameInfo;

typedef struct _VdecScaleInfo {
    U32 u32PicWidth;
    U32 u32PicHeight;
    U32 u32outWidth;
    U32 u32outHeight;
} VdecScaleInfo;

typedef struct _ViFrameMetaInfo {
    U32 u32FrameId;
    U32 u32ExpTime[3];
    U32 u32ExpLine[3];
    U32 u32Again[3];
    U32 u32Dgain[3];
    U32 u32IspDgain[3];
    U32 u32ColorTemp;
    U32 u32RGain;
    U32 u32BGain;
    U32 u32CCM[9];
    U32 u32BlackLevel[4];
    U8 u8AeStable;
    U8 u8AwbStable;
    U8 au8Reserved[2];
} ViFrameMetaInfo;

typedef struct _ViFrameInfo {
    CommonFrameInfo stCommFrameInfo;
    ViFrameMetaInfo stFrameMetaInfo;
} ViFrameInfo;

typedef struct _CppFrameInfo {
    CommonFrameInfo stCommFrameInfo;
} CppFrameInfo;

typedef struct _VoFrameInfo {
    CommonFrameInfo stCommFrameInfo;
} VoFrameInfo;

typedef struct _VencFrameInfo {
    CommonFrameInfo stCommFrameInfo;
} VencFrameInfo;

typedef struct _VdecFrameInfo {
    CommonFrameInfo stCommFrameInfo;
    VdecScaleInfo stScaleInfo;
    BOOL bEndOfStream;
} VdecFrameInfo;

typedef struct _VideoFrameInfo {
    VideoFrame stVFrame;
    FrameType eFrameType;
    U32 u32Idx;
    ModId eModId;
    UL ulPoolId;
    UL ulBufferId;
    union {
        CommonFrameInfo stCommFrameInfo;
        ViFrameInfo stViFrameInfo;
        CppFrameInfo stCppFrameInfo;
        VoFrameInfo stVoFrameInfo;
        VencFrameInfo stVencFrameInfo;
        VdecFrameInfo stVdecFrameInfo;
        U8 u8UserDef[128];
    };
} VideoFrameInfo;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /*__VB_TYPE_H__ */
