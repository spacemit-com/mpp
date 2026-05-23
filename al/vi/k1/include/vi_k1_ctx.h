/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_k1_ctx.h
 * @Date      :    2026-3-30
 * @Author    :    SPACEMIT
 * @Brief     :    K1 VI internal context definitions.
 *------------------------------------------------------------------------------
 */

#ifndef VI_K1_CTX_H
#define VI_K1_CTX_H

#include "vi_k1_defs.h"

#define K1_VI_META_CACHE_DEPTH 64

typedef struct _K1_VI_ISP_FRAMEINFO_NODE_S {
    BOOL bAllocated;
    IMAGE_BUFFER_S stImageBuffer;
} K1_VI_ISP_FRAMEINFO_NODE_S;

typedef struct _K1_VI_FRAME_META_NODE_S {
    BOOL bValid;
    U32 u32FrameId;
    FRAME_INFO_S stFrameInfo;
} K1_VI_FRAME_META_NODE_S;

typedef struct _K1_VI_DEV_CTX_S {
    BOOL bCreated;
    BOOL bEnabled;
    ViDevAttrS stAttr;
    ViRawType eOfflineRawType;
    BOOL bSensorInfoValid;
    BOOL bSensorAutoManaged;
    void *pSensorHandle;
    SENSORS_MODULE_INFO_S stSensorModuleInfo;
    SENSORS_MODULE_CAPABILITY_S stSensorCapability;
    ISP_SENSOR_REGISTER_S stSensorOps;
    ISP_AF_MOTOR_REGISTER_S stAfOps;
    SENSOR_CONFIG_S *pstSensorCfg;
    BOOL bSensorOpened;
    BOOL bSensorStreaming;
    BOOL bVcmOpened;
    BOOL bSensorDetected;
    CHAR szSensorName[32];
} K1_VI_DEV_CTX_S;

typedef struct _K1_VI_CHN_CTX_S {
    BOOL bCreated;
    BOOL bEnabled;
    BOOL bIspInit;
    BOOL bIspStreaming;
    VI_DEV ViDev;
    VI_CHN ViChn;
    U32 u32AsrChn;
    U32 u32IspPipeline;
    ViChnAttrS stAttr;
    VI_CHN ViSrcChn;
    BOOL bIsVirtual;
    BOOL bSysBound;
    MppNode stBindSink;
    UL ulVbPool;
    U32 u32BufCnt;
    U32 u32DoneHead;
    U32 u32DoneTail;
    U32 u32DoneNum;
    K1_VI_BUF_NODE_S astBufNode[K1_VI_MAX_BUF_CNT];
    U32 au32DoneQueue[K1_VI_MAX_BUF_CNT];
    U32 u32MetaWritePos;
    ViFrameRateCtrlS stFrameRateCtrl;
    U32 u32FrameRateSeq;
    K1_VI_FRAME_META_NODE_S astMetaCache[K1_VI_META_CACHE_DEPTH];
    K1_VI_ISP_FRAMEINFO_NODE_S astIspFrameInfoNode[K1_VI_META_CACHE_DEPTH];
} K1_VI_CHN_CTX_S;

typedef struct _K1_VI_RAW_CTX_S {
    BOOL bCreated;
    BOOL bEnabled;
    BOOL bTriggered;
    BOOL bFrameValid;
    VI_DEV ViDev;
    VI_CHN ViChn;
    U32 u32AsrChn;
    ViChnAttrS stAttr;
    UL ulVbPool;
    UL ulBufferId;
    BOOL bExternalBuf;
    IMAGE_BUFFER_S stImageBuffer;
    VideoFrameInfo stFrameInfo;
    K1_VI_BUF_STATE_E enState;
} K1_VI_RAW_CTX_S;

typedef struct _K1_VI_CTX_S {
    BOOL bInit;
    K1_VI_DEV_CTX_S astDevCtx[VI_MAX_DEV_NUM];
    K1_VI_CHN_CTX_S astChnCtx[VI_MAX_DEV_NUM][VI_MAX_CHN_NUM];
    K1_VI_RAW_CTX_S astRawCtx[VI_MAX_DEV_NUM][VI_MAX_CHN_NUM];
} K1_VI_CTX_S;

extern K1_VI_CTX_S g_stK1ViCtx;

#endif /* VI_K1_CTX_H */
