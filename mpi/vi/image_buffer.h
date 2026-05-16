/*
* Copyright (C) 2025-2026 SPACEMIT Limited
* All Rights Reserved.
*/

#ifndef IMAGE_BUFFER_H
#define IMAGE_BUFFER_H

#include <stdint.h>
#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IMAGE_BUFFER_MAX_PLANES 4
#define DWT_MAX_PLANES          2

typedef struct spmSIZE_S {
    U32 width;
    U32 height;
} Size;

typedef struct spmRECT_S {
    S32 x;
    S32 y;
    U32 width;
    U32 height;
} Rect;

typedef enum spmPIXEL_FORMAT_E {
    PIXEL_FORMAT_NV12,
    PIXEL_FORMAT_NV12_DWT,
    PIXEL_FORMAT_FBC,
    PIXEL_FORMAT_FBC_DWT,
    PIXEL_FORMAT_RAW_8BPP,
    PIXEL_FORMAT_RAW_10BPP,
    PIXEL_FORMAT_RAW_12BPP,
    PIXEL_FORMAT_RAW_14BPP,
    PIXEL_FORMAT_RAW,
    PIXEL_FORMAT_JPEG,
    PIXEL_FORMAT_RGB565,
    PIXEL_FORMAT_RGB888,
    PIXEL_FORMAT_Y210,
    PIXEL_FORMAT_P210,
    PIXEL_FORMAT_P010,
    PIXEL_FORMAT_YUYV,
    PIXEL_FORMAT_YVYU,

    PIXEL_FORMAT_MAX,
} PixelFormat;

typedef struct spmIMAGE_BUFFER_PLANE_S {
    U32 width;
    U32 height;
    U32 stride;
    U32 scanline;
    U32 offset;
    U32 length;
    void* virAddr;
    int fd;
} ImageBufferPlane;

typedef struct spmIMAGE_BUFFER_S {
    Size size;
    PixelFormat format;
    U32 numPlanes;
    ImageBufferPlane planes[IMAGE_BUFFER_MAX_PLANES];
    ImageBufferPlane dwt1[DWT_MAX_PLANES];
    ImageBufferPlane dwt2[DWT_MAX_PLANES];
    ImageBufferPlane dwt3[DWT_MAX_PLANES];
    ImageBufferPlane dwt4[DWT_MAX_PLANES];
    U32 type;
    U64 viT1;
    int frameId;
    U32 index;
    U64 timeStamp;

    union {
        U64 phyAddr;
        S32 blockId;
        S32 fd;
    } m;
} ImageBuffer;

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_BUFFER_H */
