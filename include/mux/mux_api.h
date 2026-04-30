/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_api.h
 * @Date      :    2026-04-15
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    MUX module API for MPP (RTSP push / file mux).
 *------------------------------------------------------------------------------
 */

#ifndef __MUX_API_H__
#define __MUX_API_H__

#include "sys/type.h"
#include "mux_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/**
 * @brief  初始化 MUX 模块，分配全局资源。
 * @return 0 成功，错误码失败
 */
S32 MUX_Init(VOID);

/**
 * @brief  反初始化 MUX 模块，释放全局资源。
 * @return 0 成功，错误码失败
 */
S32 MUX_Exit(VOID);

/**
 * @brief  创建推流通道
 * @param  s32ChnId  通道号 [0, MUX_MAX_CHN)
 * @param  pstAttr   通道属性
 * @return 0 成功，错误码失败
 */
S32 MUX_CreateChn(S32 s32ChnId, const MuxChnAttr *pstAttr);

/**
 * @brief  销毁推流通道
 * @param  s32ChnId  通道号
 * @return 0 成功，错误码失败
 */
S32 MUX_DestroyChn(S32 s32ChnId);

/**
 * @brief  启动推流通道
 * @param  s32ChnId  通道号
 * @return 0 成功，错误码失败
 */
S32 MUX_StartChn(S32 s32ChnId);

/**
 * @brief  停止推流通道
 * @param  s32ChnId  通道号
 * @return 0 成功，错误码失败
 */
S32 MUX_StopChn(S32 s32ChnId);

/**
 * @brief  发送编码包到 MUX 通道
 * @param  s32ChnId  通道号
 * @param  pstPkt    编码包（Annex-B）
 * @return 0 成功，错误码失败
 */
S32 MUX_SendPacket(S32 s32ChnId, const MuxPacket *pstPkt);

/**
 * @brief  查询 MUX 通道统计信息
 * @param  s32ChnId  通道号
 * @param  pstStat   输出统计结构体
 * @return 0 成功，错误码失败
 */
S32 MUX_GetChnStat(S32 s32ChnId, MuxChnStat *pstStat);

/**
 * @brief  查询 MUX 通道属性
 * @param  s32ChnId  通道号
 * @param  pstAttr   输出属性结构体
 * @return 0 成功，错误码失败
 */
S32 MUX_GetChnAttr(S32 s32ChnId, MuxChnAttr *pstAttr);

/**
 * @brief  获取 MUX 码流输入节点，用于 SYS_Bind 压缩域绑定。
 * @param  s32ChnId 通道号
 * @param  pstNode  输入节点
 * @return 0 成功，错误码失败
 */
S32 MUX_GetSinkNode(S32 s32ChnId, MppNode *pstNode);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __MUX_API_H__ */
