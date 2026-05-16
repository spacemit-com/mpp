/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    sys_types.h
* @Date      :    2026-3-16
* @Author    :    rmwei(rongmin.wei@spacemit.com)
* @Brief     :    Media Interface for MPP.
*------------------------------------------------------------------------------
*/

#ifndef SYS_TYPE_H
#define SYS_TYPE_H

#include "type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== Error Codes ======================== */

#define SYS_ERR_OK           0
#define SYS_ERR_INVAL       (-2)
#define SYS_ERR_NOMEM       (-3)
#define SYS_ERR_NOT_INIT    (-4)
#define SYS_ERR_BUSY        (-5)
#define SYS_ERR_NOT_FOUND   (-6)
#define SYS_ERR_EXIST       (-7)
#define SYS_ERR_FULL        (-8)
#define SYS_ERR_DOUBLE_INIT (-9)
#define SYS_ERR_TIMEOUT    (-10)

typedef enum _ModId {
    MPP_ID_SYS   = 1,
    MPP_ID_VI    = 2,
    MPP_ID_VO    = 3,
    MPP_ID_CPP   = 4,
    MPP_ID_VENC  = 5,
    MPP_ID_VDEC  = 6,
    MPP_ID_RGN   = 7,
    MPP_ID_MUX   = 8,
    MPP_ID_DEMUX = 9,
    MPP_ID_UVC   = 10,

    MPP_ID_MAX,
} ModId;

typedef struct _MppNode {
    ModId eModId;
    S32 s32DevId;
    S32 s32ChnId;
} MppNode;

typedef enum _MppPayloadType {
    MPP_PAYLOAD_TYPE_FRAME = 0,
    MPP_PAYLOAD_TYPE_STREAM,
    MPP_PAYLOAD_TYPE_MAX
} MppPayloadType;

typedef enum _MppStreamCodecType {
    MPP_STREAM_CODEC_UNKNOWN = 0,
    MPP_STREAM_CODEC_H263,
    MPP_STREAM_CODEC_H264,
    MPP_STREAM_CODEC_H264_MVC,
    MPP_STREAM_CODEC_H264_NO_SC,
    MPP_STREAM_CODEC_H265,
    MPP_STREAM_CODEC_MJPEG,
    MPP_STREAM_CODEC_JPEG,
    MPP_STREAM_CODEC_VP8,
    MPP_STREAM_CODEC_VP9,
    MPP_STREAM_CODEC_AV1,
    MPP_STREAM_CODEC_AVS,
    MPP_STREAM_CODEC_AVS2,
    MPP_STREAM_CODEC_MPEG1,
    MPP_STREAM_CODEC_MPEG2,
    MPP_STREAM_CODEC_MPEG4,
    MPP_STREAM_CODEC_RV,
    MPP_STREAM_CODEC_VC1,
    MPP_STREAM_CODEC_VC1_ANNEX_L,
    MPP_STREAM_CODEC_FWHT,
    MPP_STREAM_CODEC_MAX
} MppStreamCodecType;

typedef struct _StreamBufferInfo {
    const U8          *pu8Addr;
    U32 u32Size;
    BOOL bKeyFrame;
    BOOL bEndOfStream;
    MppStreamCodecType eCodecType;
    U64 u64PTS;
    U32 u32Width;
    U32 u32Height;
    UL ulPrivate;
} StreamBufferInfo;


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /*__SYS_TYPE_H__ */
