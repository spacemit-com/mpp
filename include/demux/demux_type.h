/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    demux_type.h
 * @Date      :    2026-04-15
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    DEMUX module type definitions for MPP.
 *------------------------------------------------------------------------------
 */

#ifndef __DEMUX_TYPE_H__
#define __DEMUX_TYPE_H__

#include "sys/type.h"
#include "sys/sys_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ======================== 常量 ======================== */

#define DEMUX_MAX_CHN       16
#define DEMUX_URL_MAX_LEN   256

/* ======================== 错误码 ======================== */

#define ERR_DEMUX_OK            0
#define ERR_DEMUX_NULL_PTR      (-1001)
#define ERR_DEMUX_INVALID_CHN   (-1002)
#define ERR_DEMUX_NOT_INIT      (-1003)
#define ERR_DEMUX_ALREADY_INIT  (-1004)
#define ERR_DEMUX_BUSY          (-1005)
#define ERR_DEMUX_NOMEM         (-1006)
#define ERR_DEMUX_NOT_STARTED   (-1007)
#define ERR_DEMUX_OPEN_FAIL     (-1008)
#define ERR_DEMUX_NO_STREAM     (-1009)

/* ======================== 枚举 ======================== */

typedef enum _DemuxInputType {
    DEMUX_INPUT_RTSP = 0,
    DEMUX_INPUT_FILE,
    DEMUX_INPUT_MAX
} DemuxInputType;

typedef enum _DemuxCodecType {
    DEMUX_CODEC_H264 = 0,
    DEMUX_CODEC_H265,
    DEMUX_CODEC_MJPEG,
    DEMUX_CODEC_UNKNOWN
} DemuxCodecType;

/* ======================== 结构体 ======================== */

/**
 * @brief 解封装后的流信息（连接成功后可查询）
 */
typedef struct _DemuxStreamInfo {
    DemuxCodecType  eCodecType;
    U32             u32Width;
    U32             u32Height;
    U32             u32Fps;
} DemuxStreamInfo;

/**
 * @brief 解封装输出的编码包（Annex-B 格式）
 *        数据指针仅在回调期间有效
 */
typedef struct _DemuxPacket {
    const U8       *pu8Data;
    U32             u32Size;
    BOOL            bKeyFrame;
    DemuxCodecType  eCodecType;
    U64             u64PTS;            /* 微秒 */
    U32             u32Width;
    U32             u32Height;
} DemuxPacket;

/**
 * @brief 编码包回调原型
 * @param s32ChnId  通道号
 * @param pstPkt    编码包（仅回调期间有效）
 * @param pPriv     用户私有数据
 * @return 0 继续，非 0 停止
 */
typedef S32 (*DemuxPacketCallback)(S32 s32ChnId, const DemuxPacket *pstPkt, VOID *pPriv);

/**
 * @brief DEMUX 通道属性
 */
typedef struct _DemuxChnAttr {
    DemuxInputType  eInputType;
    CHAR            szUrl[DEMUX_URL_MAX_LEN];
    BOOL            bPreferTcp;            /* RTSP over TCP */
    BOOL            bLowLatency;           /* nobuffer / reorder_queue_size=0 */
    U32             u32OpenTimeoutMs;      /* 打开超时 ms */
    U32             u32RwTimeoutMs;        /* 读写超时 ms */
    U32             u32ReconnectMs;        /* 断线重连间隔 ms */
    U32             u32AnalyzeDurationMs;  /* 流分析时长 ms, 0=默认 */
    U32             u32ProbeSizeBytes;     /* 探测大小, 0=默认 */
    BOOL            bInjectPS;             /* IDR 前注入 VPS/SPS/PPS */
} DemuxChnAttr;

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __DEMUX_TYPE_H__ */
