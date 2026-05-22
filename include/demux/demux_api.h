/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    demux_api.h
 * @Date      :    2026-04-15
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    DEMUX module API for MPP (RTSP pull / file demux).
 *------------------------------------------------------------------------------
 */

#ifndef DEMUX_API_H
#define DEMUX_API_H

#include "sys/type.h"
#include "demux_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/**
 * @brief  初始化 DEMUX 模块，分配全局资源。
 *         必须在其他 DEMUX 接口之前调用，仅调用一次。
 * @return 0 成功，错误码失败
 */
S32 DEMUX_Init(VOID);

/**
 * @brief  反初始化 DEMUX 模块，释放全局资源。
 *         所有通道必须先销毁，与 DEMUX_Init 配对。
 * @return 0 成功，错误码失败
 */
S32 DEMUX_Exit(VOID);

/**
 * @brief  创建 DEMUX 通道（一个通道对应一路输入源）
 * @param  s32ChnId  通道号 [0, DEMUX_MAX_CHN)
 * @param  pstAttr   通道属性
 * @return 0 成功，错误码失败
 */
S32 DEMUX_CreateChn(S32 s32ChnId, const DemuxChnAttr *pstAttr);

/**
 * @brief  销毁 DEMUX 通道，释放通道资源。
 *         通道必须先停止，与 DEMUX_CreateChn 配对。
 * @param  s32ChnId  通道号
 * @return 0 成功，错误码失败
 */
S32 DEMUX_DestroyChn(S32 s32ChnId);

/**
 * @brief  启动通道（开始拉流/读文件）
 * @param  s32ChnId  通道号
 * @return 0 成功，错误码失败
 */
S32 DEMUX_StartChn(S32 s32ChnId);

/**
 * @brief  停止通道
 * @param  s32ChnId  通道号
 * @return 0 成功，错误码失败
 */
S32 DEMUX_StopChn(S32 s32ChnId);

/**
 * @brief  查询流信息（连接成功后有效）
 * @param  s32ChnId  通道号
 * @param  pstInfo   输出流信息
 * @return 0 成功，错误码失败
 */
S32 DEMUX_GetStreamInfo(S32 s32ChnId, DemuxStreamInfo *pstInfo);

/**
 * @brief  设置编码包回调（手动模式）
 *         绑定模式下无需调用，数据通过 SYS_SendStream 自动流转。
 * @param  s32ChnId  通道号
 * @param  pfnCb     回调函数，NULL 取消回调
 * @param  pPriv     用户私有数据
 * @return 0 成功，错误码失败
 */
S32 DEMUX_SetPacketCallback(S32 s32ChnId, DemuxPacketCallback pfnCb, VOID *pPriv);

/**
 * @brief  获取 DEMUX 码流输出节点，用于 SYS_Bind 压缩域绑定。
 * @param  s32ChnId 通道号
 * @param  pstNode  输出节点
 * @return 0 成功，错误码失败
 */
S32 DEMUX_GetSrcNode(S32 s32ChnId, MppNode *pstNode);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* DEMUX_API_H */
