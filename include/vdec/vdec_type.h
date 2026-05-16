/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    vdec_type.h
* @Date      :    2026-04-18
* @Brief     :    VDEC module type definitions for MPP.
*------------------------------------------------------------------------------
*/

#ifndef VDEC_TYPE_H
#define VDEC_TYPE_H

#include "sys/type.h"
#include "sys/sys_type.h"
#include "sys/vb_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== Constants ======================== */

#define VDEC_MAX_CHN        64

/* ======================== Error Codes ======================== */

#define ERR_VDEC_OK              0
#define ERR_VDEC_NULL_PTR       (-2001)
#define ERR_VDEC_INVALID_CHN    (-2002)
#define ERR_VDEC_NOT_INIT       (-2003)
#define ERR_VDEC_ALREADY_INIT   (-2004)
#define ERR_VDEC_BUSY           (-2005)
#define ERR_VDEC_NOMEM          (-2006)
#define ERR_VDEC_NOT_STARTED    (-2007)
#define ERR_VDEC_NO_STREAM      (-2008)
#define ERR_VDEC_NO_FRAME       (-2009)
#define ERR_VDEC_TIMEOUT        (-2010)
#define ERR_VDEC_EOS            (-2011)
#define ERR_VDEC_RESOLUTION_CHG (-2012)
#define ERR_VDEC_NOT_SUPPORT    (-2013)

/* ======================== Enums ======================== */

/* ======================== Structures ======================== */

/**
* @brief VDEC channel attributes (set before VDEC_EnableChn)
*/
typedef struct _VdecScale {
    U32 u32Align;                       /**< alignment for scaled width/height (e.g. 16) */
    U32 u32Width;                       /**< scaled width (0 = no scaling) */
    U32 u32Height;                      /**< scaled height (0 = no scaling) */
    BOOL bScaleEnable;                  /**< enable scaling (if FALSE, u32Width/u32Height are ignored) */
} VdecScale;

/**
* @brief VDEC channel attributes (set before VDEC_EnableChn)
*/
typedef struct _VdecChnAttr {
    MppStreamCodecType eCodecType;              /**< H264 / H265 / MJPEG */
    MppPixelFormat eOutputPixelFormat;          /**< desired output pixel format */
    U32 u32Align;                               /**< alignment for decoded width/height (e.g. 16) */
    U32 u32Width;                               /**< stream width  (0 = auto detect) */
    U32 u32Height;                              /**< stream height (0 = auto detect) */
    BOOL bIsInterlaced;                         /**< interlaced stream */
    BOOL bIsFrameReordering;                    /**< enable frame reordering */
    U32 u32RotateDegree;                        /**< 0 / 90 / 180 / 270 */
    BOOL bDispErrorFrame;                       /**< display error frames */
    VdecScale stScale;                          /**< scaling parameters */
} VdecChnAttr;

/**
* @brief VDEC channel status (read-only, queried via VDEC_QueryStatus)
*/
typedef struct _VdecChnStatus {
    U32 u32LeftStreamFrames;        /**< pending stream packets in input queue */
    U32 u32LeftDecodedFrames;       /**< decoded frames available for output */
    U32 u32Width;                   /**< current decoded width */
    U32 u32Height;                  /**< current decoded height */
    BOOL bEndOfStream;              /**< EOS reached */
} VdecChnStatus;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /*__VDEC_TYPE_H__ */
