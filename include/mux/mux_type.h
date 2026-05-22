/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_type.h
 * @Date      :    2026-04-15
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    MUX module type definitions for MPP.
 *------------------------------------------------------------------------------
 */

#ifndef MUX_TYPE_H
#define MUX_TYPE_H

#include "sys/type.h"
#include "sys/sys_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define MUX_MAX_CHN 16
#define MUX_URL_MAX_LEN 256

#define ERR_MUX_OK 0
#define ERR_MUX_NULL_PTR (-1101)
#define ERR_MUX_INVALID_CHN (-1102)
#define ERR_MUX_NOT_INIT (-1103)
#define ERR_MUX_ALREADY_INIT (-1104)
#define ERR_MUX_BUSY (-1105)
#define ERR_MUX_NOMEM (-1106)
#define ERR_MUX_NOT_STARTED (-1107)
#define ERR_MUX_OPEN_FAIL (-1108)
#define ERR_MUX_BAD_STATE (-1109)

typedef enum _MuxOutputType { MUX_OUTPUT_RTSP = 0, MUX_OUTPUT_FILE, MUX_OUTPUT_MAX } MuxOutputType;

typedef enum _MuxCodecType { MUX_CODEC_H264 = 0, MUX_CODEC_H265, MUX_CODEC_MJPEG, MUX_CODEC_UNKNOWN } MuxCodecType;

typedef struct _MuxStreamAttr {
    MuxCodecType eCodecType;
    U32 u32Width;
    U32 u32Height;
    U32 u32Fps;
    U32 u32BitrateKbps;
} MuxStreamAttr;

typedef struct _MuxPacket {
    const U8 *pu8Data;
    U32 u32Size;
    BOOL bKeyFrame;
    MuxCodecType eCodecType;
    U64 u64PTS; /* 微秒 */
} MuxPacket;

typedef struct _MuxChnAttr {
    MuxOutputType eOutputType;
    CHAR szUrl[MUX_URL_MAX_LEN];
    MuxStreamAttr stStreamAttr;
    BOOL bPreferTcp;
    U32 u32MaxDelayMs;
} MuxChnAttr;

typedef struct _MuxChnStat {
    U32 u32ActiveClients;
    U64 u64TotalPkts;
    U64 u64TotalBytes;
    S32 s32State; /* 0=idle, 1=created, 2=running */
} MuxChnStat;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* MUX_TYPE_H */
