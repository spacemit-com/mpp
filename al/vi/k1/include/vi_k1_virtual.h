/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_k1_virtual.h
 * @Date      :    2026-3-30
 * @Author    :    SPACEMIT
 * @Brief     :    K1 VI virtual channel helpers.
 *------------------------------------------------------------------------------
 */

#ifndef __AL_VI_K1_VIRTUAL_H__
#define __AL_VI_K1_VIRTUAL_H__

#include "vi_k1_ctx.h"

VOID K1_VI_CopyFrameMeta(VideoFrameInfo *pstDstFrame, const VideoFrameInfo *pstSrcFrame);
S32 K1_VI_V2dProcessFrame(const K1_VI_CHN_CTX_S *pstSrcChnCtx,
                     const VI_IMAGE_BUFFER_S *pstSrcBuffer,
                          K1_VI_CHN_CTX_S *pstDstChnCtx,
                          VideoFrameInfo *pstDstFrame);
S32 K1_VI_ProcessOneVirtualChn(K1_VI_CHN_CTX_S *pstSrcChnCtx,
                         const VI_IMAGE_BUFFER_S *pstSrcBuffer,
                         const VideoFrameInfo *pstSrcFrame,
                               K1_VI_CHN_CTX_S *pstVirtChnCtx);
VOID K1_VI_DispatchVirtualFrames(K1_VI_CHN_CTX_S *pstSrcChnCtx,
                           const VI_IMAGE_BUFFER_S *pstSrcBuffer,
                           const VideoFrameInfo *pstSrcFrame);
S32 K1_VI_StartVirtualChnCtx(VI_DEV ViDev, VI_CHN ViChn, K1_VI_CHN_CTX_S *pstChnCtx);

#endif /* __AL_VI_K1_VIRTUAL_H__ */
