/*
 * Copyright 2022-2023 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @Description: para.h - MPP parameter definitions
 *               Ported from mpp1 and adapted for the current mpp project.
 *               MppPixelFormat is defined in vb_type.h, not here.
 */

#ifndef PARA_H
#define PARA_H

#include "type.h"
#include "vb_type.h"

/* ======================== Module Type ======================== */

typedef enum _MppModuleType {
    CODEC_AUTO = 0,
    CODEC_OPENH264,
    CODEC_FFMPEG,
    CODEC_SFDEC,
    CODEC_SFENC,
    CODEC_CODADEC,
    CODEC_SFOMX,
    CODEC_V4L2,
    CODEC_FAKEDEC,
    CODEC_V4L2_LINLONV5V7,
    CODEC_K1_JPU,
    CODEC_MAX,

    VO_AUTO = 100,
    VO_SDL2,
    VO_FILE,
    VO_DRM,
    VO_MAX,

    VI_AUTO = 200,
    VI_V4L2,
    VI_K1_CAM,
    VI_FILE,
    VI_K3_CAM,
    VI_MAX,

    VPS_AUTO = 300,
    VPS_K1_V2D,
    VPS_MAX,
} MppModuleType;

/* ======================== Coding Type ======================== */

typedef enum _MppCodingType {
    CODING_UNKNOWN = 0,
    CODING_H263,
    CODING_H264,
    CODING_H264_MVC,
    CODING_H264_NO_SC,
    CODING_H265,
    CODING_MJPEG,
    CODING_JPEG,
    CODING_VP8,
    CODING_VP9,
    CODING_AV1,
    CODING_AVS,
    CODING_AVS2,
    CODING_MPEG1,
    CODING_MPEG2,
    CODING_MPEG4,
    CODING_RV,
    CODING_VC1,
    CODING_VC1_ANNEX_L,
    CODING_FWHT,
    CODING_MAX,
} MppCodingType;

/* ======================== Profile (codec) ======================== */

typedef enum _MppProfileType {
    PROFILE_UNKNOWN = 0,
    PROFILE_MPEG2_422,
    PROFILE_MPEG2_HIGH,
    PROFILE_MPEG2_SS,
    PROFILE_MPEG2_SNR_SCALABLE,
    PROFILE_MPEG2_MAIN,
    PROFILE_MPEG2_SIMPLE,
} MppProfileType;

/* ======================== Return Value ======================== */

typedef enum _MppReturnValue {
    MPP_OK = 0,

    /* memory errors */
    MPP_OUT_OF_MEM = -1,
    MPP_MALLOC_FAILED = -2,
    MPP_MMAP_FAILED = -3,
    MPP_MUNMAP_FAILED = -4,
    MPP_NULL_POINTER = -5,

    /* file / device errors */
    MPP_FILE_NOT_EXIST = -100,
    MPP_OPEN_FAILED = -101,
    MPP_IOCTL_FAILED = -102,
    MPP_CLOSE_FAILED = -103,
    MPP_POLL_FAILED = -104,

    /* codec errors */
    MPP_NO_STREAM = -200,
    MPP_NO_FRAME = -201,
    MPP_DECODER_ERROR = -202,
    MPP_ENCODER_ERROR = -203,
    MPP_CONVERTER_ERROR = -204,
    MPP_CODER_EOS = -205,
    MPP_CODER_NO_DATA = -206,
    MPP_RESOLUTION_CHANGED = -207,
    MPP_ERROR_FRAME = -208,
    MPP_CODER_NULL_DATA = -209,

    /* dataqueue errors */
    MPP_DATAQUEUE_FULL = -300,
    MPP_DATAQUEUE_EMPTY = -301,

    /* other */
    MPP_INIT_FAILED = -400,
    MPP_CHECK_FAILED = -401,
    MPP_BIND_NOT_MATCH = -402,
    MPP_NOT_SUPPORTED_FORMAT = -403,
    MPP_NOT_SUPPORTED = -404,

    MPP_ERROR_UNKNOWN = -1023
} MppReturnValue;

static inline const char *mpp_err2str(int cmd) {
#define MPP_ERR2STR(cmd) \
    case cmd:            \
        return #cmd

    switch (cmd) {
        MPP_ERR2STR(MPP_OUT_OF_MEM);
        MPP_ERR2STR(MPP_MALLOC_FAILED);
        MPP_ERR2STR(MPP_MMAP_FAILED);
        MPP_ERR2STR(MPP_MUNMAP_FAILED);
        MPP_ERR2STR(MPP_NULL_POINTER);
        MPP_ERR2STR(MPP_FILE_NOT_EXIST);
        MPP_ERR2STR(MPP_OPEN_FAILED);
        MPP_ERR2STR(MPP_IOCTL_FAILED);
        MPP_ERR2STR(MPP_CLOSE_FAILED);
        MPP_ERR2STR(MPP_POLL_FAILED);
        MPP_ERR2STR(MPP_NO_STREAM);
        MPP_ERR2STR(MPP_NO_FRAME);
        MPP_ERR2STR(MPP_DECODER_ERROR);
        MPP_ERR2STR(MPP_ENCODER_ERROR);
        MPP_ERR2STR(MPP_CONVERTER_ERROR);
        MPP_ERR2STR(MPP_CODER_EOS);
        MPP_ERR2STR(MPP_CODER_NO_DATA);
        MPP_ERR2STR(MPP_RESOLUTION_CHANGED);
        MPP_ERR2STR(MPP_ERROR_FRAME);
        MPP_ERR2STR(MPP_DATAQUEUE_FULL);
        MPP_ERR2STR(MPP_DATAQUEUE_EMPTY);
        MPP_ERR2STR(MPP_INIT_FAILED);
        MPP_ERR2STR(MPP_CHECK_FAILED);
        MPP_ERR2STR(MPP_BIND_NOT_MATCH);
        MPP_ERR2STR(MPP_NOT_SUPPORTED_FORMAT);
        MPP_ERR2STR(MPP_NOT_SUPPORTED);
        default:
            return "UNKNOWN";
    }
#undef MPP_ERR2STR
}

/* ======================== Frame Buffer Type ======================== */

typedef enum _MppFrameBufferType {
    MPP_FRAME_BUFFERTYPE_NORMAL_INTERNAL = 0,
    MPP_FRAME_BUFFERTYPE_DMABUF_INTERNAL = 1,
    MPP_FRAME_BUFFERTYPE_NORMAL_EXTERNAL = 2,
    MPP_FRAME_BUFFERTYPE_DMABUF_EXTERNAL = 3,
    MPP_FRAME_BUFFERTYPE_TOTAL_NUM = 4,
} MppFrameBufferType;

/* ======================== Data Transmission Mode ======================== */

typedef enum _MppDataTransmissinMode {
    MPP_SYNC = 0,
    MPP_INPUT_SYNC_OUTPUT_SYNC = 1,
    MPP_INPUT_SYNC_OUTPUT_ASYNC = 2,
    MPP_INPUT_ASYNC_OUTPUT_SYNC = 3,
    MPP_INPUT_ASYNC_OUTPUT_ASYNC = 4,
} MppDataTransmissinMode;

/* ======================== Frame EOS ======================== */

typedef enum _MppFrameEos {
    FRAME_NO_EOS = 0,
    FRAME_EOS_WITH_DATA = 1,
    FRAME_EOS_WITHOUT_DATA = 2,
} MppFrameEos;

/* ======================== Codec Para Structs ======================== */

typedef struct _MppVdecPara {
    MppCodingType eCodingType;
    S32 nProfile;
    MppFrameBufferType eFrameBufferType;
    MppDataTransmissinMode eDataTransmissinMode;
    S32 nWidth;
    S32 nHeight;
    S32 nStride;
    S32 nAlign;
    S32 nScale;
    S32 nHorizonScaleDownRatio;
    S32 nVerticalScaleDownRatio;
    S32 nHorizonScaleDownFrameWidth;
    S32 nVerticalScaleDownFrameHeight;
    S32 nRotateDegree;
    S32 bThumbnailMode;
    BOOL bIsInterlaced;
    BOOL bIsFrameReordering;
    BOOL bIgnoreStreamHeaders;
    MppPixelFormat eOutputPixelFormat;
    BOOL bNoBFrames;
    BOOL bDisable3D;
    BOOL bSupportMaf;
    BOOL bDispErrorFrame;
    BOOL bInputBlockModeEnable;
    BOOL bOutputBlockModeEnable;
    S32 nInputQueueLeftNum;
    S32 nOutputQueueLeftNum;
    S32 nInputBufferNum;
    S32 nOutputBufferNum;
    void *pFrame[64];
    S32 nOldWidth;
    S32 nOldHeight;
    BOOL bIsResolutionChanged;
    BOOL bIsBufferInDecoder[64];
    S32 nOutputBufferFd[64];
} MppVdecPara;

typedef struct _MppVencPara {
    MppCodingType eCodingType;
    S32 nProfile;
    MppPixelFormat PixelFormat;
    MppFrameBufferType eFrameBufferType;
    MppDataTransmissinMode eDataTransmissinMode;
    S32 nWidth;
    S32 nHeight;
    S32 nStride;
    S32 nAlign;
    S32 nBitrate;
    S32 nFrameRate;
    S32 nRotateDegree;
} MppVencPara;

typedef struct _MppVoPara {
    MppFrameBufferType eFrameBufferType;
    MppDataTransmissinMode eDataTransmissinMode;
    BOOL bIsFrame;
    MppPixelFormat ePixelFormat;
    S32 nWidth;
    S32 nHeight;
    S32 nStride;
    U8 *pOutputFileName;
} MppVoPara;

typedef struct _MppViPara {
    MppFrameBufferType eFrameBufferType;
    MppDataTransmissinMode eDataTransmissinMode;
    BOOL bIsFrame;
    MppPixelFormat ePixelFormat;
    S32 nWidth;
    S32 nHeight;
    S32 nStride;
    MppCodingType eCodingType;
    S32 nBufferNum;
    U8 *pVideoDeviceName;
    U8 *pInputFileName;
} MppViPara;

/* ======================== VENC dynamic set-param ======================== */

typedef struct _MppVencParaH264FixedQP {
    S32 nGop;
    S32 nIQp;
    S32 nPQp;
    S32 nBQp;
} MppVencParaH264FixedQP;

typedef struct _MppVencParaHEVCFixedQP {
    S32 nGop;
    S32 nIQp;
    S32 nPQp;
    S32 nBQp;
} MppVencParaHEVCFixedQP;

typedef struct _MppVencParaH264CBR {
    S32 nGop;
    S32 nMinQP;
    S32 nMaxQP;
} MppVencParaH264CBR;

typedef struct _MppVencParaH264VBR {
    S32 nGop;
    S32 nMinQP;
    S32 nMaxQP;
} MppVencParaH264VBR;

typedef struct _MppVencParaH264CVBR {
    S32 nGop;
    S32 nMinQP;
    S32 nMaxQP;
} MppVencParaH264CVBR;

typedef struct _MppVencParaHEVCCBR {
    S32 nGop;
    S32 nMinQP;
    S32 nMaxQP;
} MppVencParaHEVCCBR;

typedef struct _MppVencParaHEVCVBR {
    S32 nGop;
    S32 nMinQP;
    S32 nMaxQP;
} MppVencParaHEVCVBR;

typedef struct _MppVencParaHEVCCVBR {
    S32 nGop;
    S32 nMinQP;
    S32 nMaxQP;
} MppVencParaHEVCCVBR;

typedef struct _MppVencRateControl {
    U32 nRcType;
#define MPP_RATE_CONTROL_MODE_OFF (0)
#define MPP_RATE_CONTROL_MODE_STANDARD (1)
#define MPP_RATE_CONTROL_MODE_VBR (2)
#define MPP_RATE_CONTROL_MODE_CBR (3)
#define MPP_RATE_CONTROL_MODE_CVBR (4)
    U32 nTargetBitrate;
    U32 nMaximumBitrate;
} MppVencRateControl;

#define MPP_MVX_MAX_FRAME_REGIONS 16
typedef struct _MPPBufferRegion {
    U16 nMbxLeft;
    U16 nMbxRight;
    U16 nMbyTop;
    U16 nMbyBottom;
    S16 nQpDelta;
} MppBufferRegion;

typedef struct _MppVencRoiRegions {
    U32 nPicIndex;
    U8 nQpPresent;
    U8 nQp;
    U8 nRoiPresent;
    U8 nNumRoi;
    MppBufferRegion roi[MPP_MVX_MAX_FRAME_REGIONS];
} MppVencRoiRegions;

typedef struct _MppVencMirror {
    S32 nMirror;
} MppVencMirror;

typedef struct _MppVencSlice {
    S32 nSpacing;
} MppVencSlice;

#endif /*PARA_H*/
