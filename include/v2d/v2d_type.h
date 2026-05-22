/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Spacemit Co., Ltd.
 */

#ifndef V2D_TYPE_H
#define V2D_TYPE_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SUCCESS
#define SUCCESS 0
#endif

#ifndef FAILURE
#define FAILURE (-1)
#endif

/*
 * V2D error codes
 */
#define V2D_OK 0

/* -1 ~ -19: handle / generic parameter errors */
#define V2D_ERR_FAILURE (-1)        /* generic failure, same as legacy FAILURE */
#define V2D_ERR_NULL_PTR (-2)       /* required pointer argument is NULL */
#define V2D_ERR_INVALID_HANDLE (-3) /* V2DHandle is zero or otherwise invalid */
#define V2D_ERR_INVALID_PARAM (-4)  /* numeric parameter is out of range */
#define V2D_ERR_PARAM_PAIR (-5)     /* only one argument in a required pair is provided */

/* -20 ~ -39: frame / surface / rect validation */
#define V2D_ERR_INVALID_FRAME (-20)   /* PlaneNum==0, Fd==0, or invalid VB handle */
#define V2D_ERR_INVALID_FORMAT (-21)  /* unsupported MppPixelFormat or V2DColorFormat */
#define V2D_ERR_INVALID_STRIDE (-22)  /* PlaneStride does not match width/format */
#define V2D_ERR_INVALID_RECT (-23)    /* rect width/height is zero or out of bounds */
#define V2D_ERR_FORMAT_MISMATCH (-24) /* multi-frame format constraint is not satisfied */
#define V2D_ERR_SIZE_MISMATCH (-25)   /* frame sizes do not match */
#define V2D_ERR_FBC_UNSUPPORTED (-26) /* current operation does not support FBC surfaces */
#define V2D_ERR_NO_CSC (-27)          /* no CSC mode can be derived from src/dst formats */

/* -40 ~ -59: resource / state errors */
#define V2D_ERR_TASK_FULL (-40)  /* task count in one job reached V2D_MAX_TASK_NUM */
#define V2D_ERR_ALLOC (-41)      /* calloc or malloc failed */
#define V2D_ERR_VB_VIRADDR (-42) /* VB_GetVirAddr failed */

/* -60 ~ -79: device I/O / fence errors */
#define V2D_ERR_DEV_OPEN (-60)      /* open(/dev/v2d_dev) failed */
#define V2D_ERR_DEV_WRITE (-61)     /* task write to driver failed or was short */
#define V2D_ERR_FENCE_TIMEOUT (-62) /* poll timed out while waiting for fence */
#define V2D_ERR_FENCE_POLL (-63)    /* poll failed with POLLERR/POLLNVAL or system error */

typedef U64 V2DHandle;

typedef enum V2DInputLayer {
    V2D_INPUT_LAYER0 = 0,
    V2D_INPUT_LAYER1 = 1,
    V2D_INPUT_LAYER_NUM = 2,
} V2DInputLayer;

typedef enum V2DFunctionMode {
    V2D_FUNC_DISABLE = 0,
    V2D_FUNC_ENABLE = 1,
} V2DFunctionMode;

typedef enum V2DDither {
    V2D_NO_DITHER = 0,
    V2D_DITHER_4X4 = 1,
    V2D_DITHER_8X8 = 2,
} V2DDither;

typedef enum V2DRotateAngle {
    V2D_ROT_0 = 0,
    V2D_ROT_90 = 1,
    V2D_ROT_180 = 2,
    V2D_ROT_270 = 3,
    V2D_ROT_MIRROR = 4,
    V2D_ROT_FLIP = 5,
} V2DRotateAngle;

typedef enum V2DBlendCmd {
    V2D_BLENDCMD_ALPHA = 0,
    V2D_BLENDCMD_ROP2 = 1,
    V2D_BLENDCMD_BUTT,
} V2DBlendCmd;

typedef enum V2DMaskCmd {
    V2D_MASKCMD_DISABLE = 0,
    V2D_MASKCMD_NORMAL = 1,
    V2D_MASKCMD_AS_VALUE = 2,
    V2D_MASKCMD_BUTT,
} V2DMaskCmd;

typedef enum V2DBlendAlphaSource {
    V2D_BLENDALPHA_SOURCE_PIXEL = 0,
    V2D_BLENDALPHA_SOURCE_GLOBAL = 1,
    V2D_BLENDALPHA_SOURCE_MASK = 2,
    V2D_BLENDALPHA_SOURCE_BUTT,
} V2DBlendAlphaSource;

typedef enum V2DBlendPreAlphaFunc {
    V2D_BLEND_PRE_ALPHA_FUNC_DISABLE = 0,
    V2D_BLEND_PRE_ALPHA_FUNC_GLOBAL_MULTI_SOURCE = 1,
    V2D_BLEND_PRE_ALPHA_FUNC_MASK_MULTI_SOURCE = 2,
    V2D_BLEND_PRE_ALPHA_FUNC_BUTT,
} V2DBlendPreAlphaFunc;

typedef enum V2DBlendMode {
    V2D_BLEND_ZERO = 0,
    V2D_BLEND_ONE,
    V2D_BLEND_SRC_ALPHA,
    V2D_BLEND_ONE_MINUS_SRC_ALPHA,
    V2D_BLEND_DST_ALPHA,
    V2D_BLEND_ONE_MINUS_DST_ALPHA,
    V2D_BLEND_BUTT,
} V2DBlendMode;

typedef enum V2DRop2Mode {
    V2D_ROP2_BLACK = 0,
    V2D_ROP2_NOTMERGEPEN = 1,
    V2D_ROP2_MASKNOTPEN = 2,
    V2D_ROP2_NOTCOPYPEN = 3,
    V2D_ROP2_MASKPENNOT = 4,
    V2D_ROP2_NOT = 5,
    V2D_ROP2_XORPEN = 6,
    V2D_ROP2_NOTMASKPEN = 7,
    V2D_ROP2_MASKPEN = 8,
    V2D_ROP2_NOTXORPEN = 9,
    V2D_ROP2_NOP = 10,
    V2D_ROP2_MERGENOTPEN = 11,
    V2D_ROP2_COPYPEN = 12,
    V2D_ROP2_MERGEPENNOT = 13,
    V2D_ROP2_MERGEPEN = 14,
    V2D_ROP2_WHITE = 15,
    V2D_ROP2_BUTT = 16,
} V2DRop2Mode;

typedef enum V2DColorFormat {
    V2D_COLOR_FORMAT_RGB888 = 0,
    V2D_COLOR_FORMAT_RGBX8888 = 1,
    V2D_COLOR_FORMAT_RGBA8888 = 2,
    V2D_COLOR_FORMAT_ARGB8888 = 3,
    V2D_COLOR_FORMAT_RGB565 = 4,
    V2D_COLOR_FORMAT_NV12 = 5,
    V2D_COLOR_FORMAT_RGBA5658 = 6,
    V2D_COLOR_FORMAT_ARGB8565 = 7,
    V2D_COLOR_FORMAT_A8 = 8,
    V2D_COLOR_FORMAT_Y8 = 9,
    V2D_COLOR_FORMAT_L8_RGBA8888 = 10,
    V2D_COLOR_FORMAT_L8_RGB888 = 11,
    V2D_COLOR_FORMAT_L8_RGB565 = 12,
    V2D_COLOR_FORMAT_BGR888 = 13,
    V2D_COLOR_FORMAT_BGRX8888 = 14,
    V2D_COLOR_FORMAT_BGRA8888 = 15,
    V2D_COLOR_FORMAT_ABGR8888 = 16,
    V2D_COLOR_FORMAT_BGR565 = 17,
    V2D_COLOR_FORMAT_NV21 = 18,
    V2D_COLOR_FORMAT_BGRA5658 = 19,
    V2D_COLOR_FORMAT_ABGR8565 = 20,
    V2D_COLOR_FORMAT_L8_BGRA8888 = 21,
    V2D_COLOR_FORMAT_L8_BGR888 = 22,
    V2D_COLOR_FORMAT_L8_BGR565 = 23,
    V2D_COLOR_FORMAT_BUTT,
} V2DColorFormat;

typedef enum V2DCscMode {
    V2D_CSC_MODE_RGB_2_BT601WIDE = 0,
    V2D_CSC_MODE_BT601WIDE_2_RGB = 1,
    V2D_CSC_MODE_RGB_2_BT601NARROW = 2,
    V2D_CSC_MODE_BT601NARROW_2_RGB = 3,
    V2D_CSC_MODE_RGB_2_BT709WIDE = 4,
    V2D_CSC_MODE_BT709WIDE_2_RGB = 5,
    V2D_CSC_MODE_RGB_2_BT709NARROW = 6,
    V2D_CSC_MODE_BT709NARROW_2_RGB = 7,
    V2D_CSC_MODE_BT601WIDE_2_BT709WIDE = 8,
    V2D_CSC_MODE_BT601WIDE_2_BT709NARROW = 9,
    V2D_CSC_MODE_BT601WIDE_2_BT601NARROW = 10,
    V2D_CSC_MODE_BT601NARROW_2_BT709WIDE = 11,
    V2D_CSC_MODE_BT601NARROW_2_BT709NARROW = 12,
    V2D_CSC_MODE_BT601NARROW_2_BT601WIDE = 13,
    V2D_CSC_MODE_BT709WIDE_2_BT601WIDE = 14,
    V2D_CSC_MODE_BT709WIDE_2_BT601NARROW = 15,
    V2D_CSC_MODE_BT709WIDE_2_BT709NARROW = 16,
    V2D_CSC_MODE_BT709NARROW_2_BT601WIDE = 17,
    V2D_CSC_MODE_BT709NARROW_2_BT601NARROW = 18,
    V2D_CSC_MODE_BT709NARROW_2_BT709WIDE = 19,
    V2D_CSC_MODE_RGB_2_GREY = 20,
    V2D_CSC_MODE_RGB_2_RGB = 21,
    V2D_CSC_MODE_BUTT = 22,
} V2DCscMode;

typedef struct V2DArea {
    U16 u16X;
    U16 u16Y;
    U16 u16W;
    U16 u16H;
} V2DArea;

typedef struct V2DFillColor {
    U32 u32ColorValue;
    V2DColorFormat enFormat;
} V2DFillColor;

typedef struct V2DPalette {
    U8 u8PalVal[1024];
    S32 s32Len;
} V2DPalette;

typedef struct V2DBlendFactor {
    V2DBlendMode enSrcColorFactor;
    V2DBlendMode enDstColorFactor;
    V2DBlendMode enSrcAlphaFactor;
    V2DBlendMode enDstAlphaFactor;
} V2DBlendFactor;

typedef struct V2DRop2Code {
    V2DRop2Mode enColorRop2Code;
    V2DRop2Mode enAlphaRop2Code;
} V2DRop2Code;

typedef struct V2DBlendLayerConf {
    V2DBlendAlphaSource enBlendAlphaSource;
    V2DBlendPreAlphaFunc enBlendPreAlphaFunc;
    U8 u8GlobalAlpha;
    union {
        V2DBlendFactor stBlendFactor;
        V2DRop2Code stRop2Code;
    };
    V2DArea stBlendArea;
} V2DBlendLayerConf;

typedef struct V2DBlendConf {
    V2DBlendCmd enBlendCmd;
    struct {
        V2DFillColor stFillColor;
        bool bEnable;
    } stBgColor;
    V2DMaskCmd enMaskCmd;
    V2DArea stBlendMaskArea;
    V2DBlendLayerConf stBlendLayer[V2D_INPUT_LAYER_NUM];
} V2DBlendConf;

typedef struct V2DLine {
    S32 s32X0;
    S32 s32Y0;
    S32 s32X1;
    S32 s32Y1;
    V2DFillColor stColor;
    U32 u32LineWidth;
} V2DLine;

typedef struct V2DCircle {
    S32 s32CenterX;
    S32 s32CenterY;
    U32 u32Radius;
    V2DFillColor stColor;
    S32 s32Thickness;
} V2DCircle;

#ifdef __cplusplus
}
#endif

#endif
