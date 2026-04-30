/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_k1_isp.h
 * @Date      :    2026-3-30
 * @Author    :    SPACEMIT
 * @Brief     :    K1 VI ISP helpers.
 *------------------------------------------------------------------------------
 */

#ifndef __AL_VI_K1_ISP_H__
#define __AL_VI_K1_ISP_H__

#include "vi_k1_ctx.h"

S32 K1_VI_ToIspBayerPattern(U32 u32BayerPattern, ISP_BAYER_PATTERN_E *penBayerPattern);
int32_t K1_VI_IspFrameInfoCallback(uint32_t pipelineID, void *pstFrameinfoBuf);
S32 K1_VI_CreateIspFrameInfoPool(K1_VI_CHN_CTX_S *pstChnCtx);
VOID K1_VI_DestroyIspFrameInfoPool(K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_QueueIspFrameInfoBuffer(K1_VI_CHN_CTX_S *pstChnCtx, IMAGE_BUFFER_S *pstFrameInfoBuf);
S32 K1_VI_QueueAllIspFrameInfoBuffers(K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_InitIsp(K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_DeInitIsp(K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_StartIsp(K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_StopIsp(K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_InitOfflineIsp(K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_DeInitOfflineIsp(K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_StartOfflineIsp(K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_StopOfflineIsp(K1_VI_CHN_CTX_S *pstChnCtx);

#endif /* __AL_VI_K1_ISP_H__ */
