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

#include "sys/sys_type.h"
#include "sys/type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define MUX_MAX_CHN 16
#define MUX_URL_MAX_LEN 256
#define MUX_PATTERN_MAX_LEN 256

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
#define ERR_MUX_WRITE_FAIL (-1110)
#define ERR_MUX_UNSUPPORTED (-1111)
#define ERR_MUX_INVALID_ARG (-1112)
#define ERR_MUX_NEED_KEYFRAME (-1113)

/**
 * @brief  输出目标类型。
 *   - MUX_OUTPUT_RTSP：内置 RTSP 服务对外发布
 *   - MUX_OUTPUT_FILE：本地文件录像（按 eFileFormat 选择封装）
 *   - MUX_OUTPUT_RTMP：RTMP 推流到远端服务器
 */
typedef enum _MuxOutputType {
    MUX_OUTPUT_RTSP = 0,
    MUX_OUTPUT_FILE,
    MUX_OUTPUT_RTMP,
    MUX_OUTPUT_MAX
} MuxOutputType;

typedef enum _MuxCodecType { MUX_CODEC_H264 = 0, MUX_CODEC_H265, MUX_CODEC_MJPEG, MUX_CODEC_UNKNOWN } MuxCodecType;

/**
 * @brief  文件录像封装格式。
 *   - MUX_FILE_FMP4：fragmented MP4（moof+mdat），断电安全、兼容性好
 *   - MUX_FILE_TS  ：MPEG-TS，天然流式、最抗损坏
 */
typedef enum _MuxFileFormat {
    MUX_FILE_FMP4 = 0,
    MUX_FILE_TS,
    MUX_FILE_MAX
} MuxFileFormat;

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

/**
 * @brief  文件录像分段配置（仅 MUX_OUTPUT_FILE 有效）。
 *
 * 切片在关键帧（IDR）边界进行，任一阈值先到达即触发切片。两个阈值为 0
 * 时表示不限制，整段录到一个文件。
 */
typedef struct _MuxSegmentAttr {
    MuxFileFormat eFileFormat;     /* 封装格式：fMP4 / TS */
    U32 u32MaxDurationMs;          /* 单文件最大时长，0=不限 */
    U32 u32MaxSizeBytes;           /* 单文件最大字节数，0=不限 */
    U32 u32FragDurationMs;         /* fMP4 单分片(moof)目标时长，0=每 GOP 一片 */
    U32 u32FsyncIntervalMs;        /* 周期落盘间隔(ms)，0=用默认 1000ms */
    /**
     * 文件名模板，支持 strftime 占位符与 "%d" 序号。
     * 例: "/mnt/sd/rec_%Y%m%d_%H%M%S.mp4"。为空时用内部默认模板。
     */
    CHAR szPattern[MUX_PATTERN_MAX_LEN];
} MuxSegmentAttr;

typedef struct _MuxChnAttr {
    MuxOutputType eOutputType;
    CHAR szUrl[MUX_URL_MAX_LEN];
    MuxStreamAttr stStreamAttr;
    BOOL bPreferTcp;
    U32 u32MaxDelayMs;
    MuxSegmentAttr stSegment; /* 文件录像分段配置，仅 MUX_OUTPUT_FILE 使用 */
} MuxChnAttr;

typedef struct _MuxChnStat {
    U32 u32ActiveClients;
    U64 u64TotalPkts;
    U64 u64TotalBytes;
    S32 s32State;        /* 0=idle, 1=created, 2=running */
    U32 u32FileCount;    /* 文件录像已生成的分段文件数量 */
    U64 u64CurFileBytes; /* 当前分段文件已写入字节数 */
    CHAR szCurFile[MUX_PATTERN_MAX_LEN]; /* 当前正在写入的文件路径 */
} MuxChnStat;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __MUX_TYPE_H__ */
