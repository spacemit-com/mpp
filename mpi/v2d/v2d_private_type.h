/*
* SPDX-License-Identifier: Apache-2.0
* Copyright (C) 2026 Spacemit Co., Ltd.
*/

#ifndef V2D_PRIVATE_TYPE_H
#define V2D_PRIVATE_TYPE_H

#include "v2d_type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum FbcDecoderMode {
    FBC_DECODER_MODE_SCAN_LINE = 0,
    FBC_DECODER_MODE_LDC_Y = 1,
    FBC_DECODER_MODE_LDC_UV = 2,
    FBC_DECODER_MODE_H264_32x16 = 3,
    FBC_DECODER_MODE_H265_32x32 = 4,
    FBC_DECODER_MODE_BUTT = 5,
} FbcDecoderMode;

typedef enum FbcDecoderFormat {
    FBC_DECODER_FORMAT_NV12 = 0,
    FBC_DECODER_FORMAT_RGB888 = 1,
    FBC_DECODER_FORMAT_ARGB8888 = 2,
    FBC_DECODER_FORMAT_RGB565 = 3,
    FBC_DECODER_FORMAT_BUTT = 4,
} FbcDecoderFormat;

typedef struct V2DBackground {
    V2DFillColor stFillColor;
    bool bEnable;
} V2DBackground;

typedef struct V2DSolidColor {
    V2DFillColor stFillColor;
    bool bEnable;
} V2DSolidColor;

typedef struct FbcDecoder {
    S32 s32Fd;
    U32 u32HeaderAddrH;
    U32 u32HeaderAddrL;
    U16 u16BboxLeft;
    U16 u16BboxRight;
    U16 u16BboxTop;
    U16 u16BboxBottom;
    bool bRgbPackEn;
    bool bIsSplit;
    FbcDecoderMode enMode;
    FbcDecoderFormat enFormat;
} FbcDecoder;

typedef FbcDecoderFormat FbcEncoderFormat;

typedef struct FbcEncoder {
    S32 s32Fd;
    S32 s32Offset;
    U32 u32HeaderAddrH;
    U32 u32HeaderAddrL;
    U32 u32PayloadAddrH;
    U32 u32PayloadAddrL;
    U16 u16BboxLeft;
    U16 u16BboxRight;
    U16 u16BboxTop;
    U16 u16BboxBottom;
    bool bIsSplit;
    FbcEncoderFormat enFormat;
} FbcEncoder;

typedef struct V2DSurface {
    bool bFbcEnable;
    S32 s32Fd;
    S32 s32Offset;
    U32 u32PhyAddrYL;
    U32 u32PhyAddrYH;
    U32 u32PhyAddrUvL;
    U32 u32PhyAddrUvH;
    U16 u16W;
    U16 u16H;
    U16 u16Stride;
    V2DColorFormat enFormat;
    union {
        FbcDecoder stFbcDecInfo;
        FbcEncoder stFbcEncInfo;
    } stFbcInfo;
    V2DSolidColor stSolidColor;
} V2DSurface;

typedef struct V2DParam {
    V2DSurface stLayer0;
    V2DSurface stLayer1;
    V2DSurface stMask;
    V2DSurface stDst;
    V2DArea stL0Rect;
    V2DArea stL1Rect;
    V2DArea stMaskRect;
    V2DArea stDstRect;
    V2DBlendConf stBlendConf;
    V2DRotateAngle enL0Rt;
    V2DRotateAngle enL1Rt;
    V2DCscMode enL0Csc;
    V2DCscMode enL1Csc;
    V2DDither enDither;
    V2DPalette stPalette;
} V2DParam;

typedef struct V2DSubmitTask {
    V2DParam stParam;
    S32 s32AcquireFenceFd;
    S32 s32CompleteFenceFd;
} V2DSubmitTask;

#ifdef __cplusplus
}
#endif

#endif
