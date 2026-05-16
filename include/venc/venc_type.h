/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    venc_type.h
* @Date      :    2026-04-19
* @Brief     :    VENC module type definitions for MPP.
*------------------------------------------------------------------------------
*/

#ifndef VENC_TYPE_H
#define VENC_TYPE_H

#include "sys/type.h"
#include "sys/sys_type.h"
#include "sys/vb_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== Constants ======================== */

#define VENC_MAX_CHN        16

/* ======================== Error Codes ======================== */

#define ERR_VENC_OK              0
#define ERR_VENC_NULL_PTR       (-3001)
#define ERR_VENC_INVALID_CHN    (-3002)
#define ERR_VENC_NOT_INIT       (-3003)
#define ERR_VENC_ALREADY_INIT   (-3004)
#define ERR_VENC_BUSY           (-3005)
#define ERR_VENC_NOMEM          (-3006)
#define ERR_VENC_NOT_STARTED    (-3007)
#define ERR_VENC_NO_FRAME       (-3008)
#define ERR_VENC_NO_STREAM      (-3009)
#define ERR_VENC_TIMEOUT        (-3010)
#define ERR_VENC_EOS            (-3011)
#define ERR_VENC_NOT_SUPPORT    (-3012)

/* ======================== Enums ======================== */

typedef enum {
    MPP_VENC_CMD_SET_PARAM_H264_FIXED_QP,
    MPP_VENC_CMD_SET_PARAM_HEVC_FIXED_QP,
    MPP_VENC_CMD_SET_PARAM_H264_CBR,
    MPP_VENC_CMD_SET_PARAM_H264_VBR,
    MPP_VENC_CMD_SET_PARAM_H264_CVBR,
    MPP_VENC_CMD_SET_PARAM_HEVC_CBR,
    MPP_VENC_CMD_SET_PARAM_HEVC_VBR,
    MPP_VENC_CMD_SET_PARAM_HEVC_CVBR,
    MPP_VENC_CMD_SET_CBR_RATE_CONTROL_PARAM,
    MPP_VENC_CMD_SET_VBR_RATE_CONTROL_PARAM,
    MPP_VENC_CMD_SET_CVBR_RATE_CONTROL_PARAM,
    MPP_VENC_CMD_SET_ROI_REGIONS_PARAM,
    MPP_VENC_CMD_SET_MIRROR,
    MPP_VENC_CMD_SET_SLICE
} MppVencCmd;

/**
* @brief Frame buffer input mode for encoder
*/
typedef enum _VencFrameBufMode {
    VENC_FRAME_BUF_DMABUF_INTERNAL = 0,   /**< encoder allocates dma-buf internally */
    VENC_FRAME_BUF_NORMAL_INTERNAL,        /**< encoder allocates mmap internally */
    VENC_FRAME_BUF_DMABUF_EXTERNAL,        /**< APP provides dma-buf via VB pool */
    VENC_FRAME_BUF_MAX
} VencFrameBufMode;

/**
* @brief Rate control mode
*/
typedef enum _VencRcMode {
    VENC_RC_MODE_FIXQP = 0,    /**< fixed QP */
    VENC_RC_MODE_CBR,          /**< constant bitrate */
    VENC_RC_MODE_VBR,          /**< variable bitrate */
    VENC_RC_MODE_CVBR,         /**< constrained VBR */
    VENC_RC_MODE_MAX
} VencRcMode;

/* ======================== Structures ======================== */

/**
* @brief VENC channel attributes (set before VENC_EnableChn)
*/
typedef struct _VencChnAttr {
    MppStreamCodecType eCodecType;          /**< H264 / H265 / MJPEG */
    MppPixelFormat eInputPixelFormat;        /**< input frame pixel format */
    U32 u32Width;                            /**< input frame width */
    U32 u32Height;                           /**< input frame height */
    U32 u32Align;                            /**< buffer stride alignment */
    U32 u32Bitrate;                          /**< target bitrate in bps */
    U32 u32FrameRate;                        /**< frame rate */
    U32 u32Gop;                              /**< GOP size */
    U32 u32Profile;                          /**< codec profile (0 = default) */
    U32 u32RotateDegree;                     /**< 0 / 90 / 180 / 270 */
    VencFrameBufMode eFrameBufMode;          /**< frame buffer mode */
    VencRcMode eRcMode;                     /**< rate control mode */
    U32 u32MinQp;                           /**< min QP for RC */
    U32 u32MaxQp;                           /**< max QP for RC */
    U32 u32IQp;                            /**< I-frame QP (FIXQP mode) */
    U32 u32PQp;                            /**< P-frame QP (FIXQP mode) */
    U32 u32BQp;                            /**< B-frame QP (FIXQP mode) */
} VencChnAttr;

/**
* @brief VENC channel status (read-only, queried via VENC_QueryStatus)
*/
typedef struct _VencChnStatus {
    U32 u32LeftInputFrames;         /**< pending input frames */
    U32 u32LeftOutputStreams;       /**< encoded streams available for output */
    U32 u32Width;                  /**< current encode width */
    U32 u32Height;                 /**< current encode height */
    BOOL bEndOfStream;             /**< EOS reached */
} VencChnStatus;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /*__VENC_TYPE_H__ */
