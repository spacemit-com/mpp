/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_k1_buffer.h
 * @Date      :    2026-3-30
 * @Author    :    SPACEMIT
 * @Brief     :    K1 VI buffer helpers.
 *------------------------------------------------------------------------------
 */

#ifndef VI_K1_BUFFER_H
#define VI_K1_BUFFER_H

#include "vi_k1_ctx.h"

#define K1_VI_BUF_EXTERN_CAPABILITY 1

S32 K1_VI_ImportExternalBufPool(
    VI_DEV ViDev,
    VI_CHN ViChn,
    K1_VI_CHN_CTX_S *pstChnCtx,
    UL ulPoolId,
    U32 u32BufCnt,
    const UL *paulBufferId,
    const VideoFrameInfo *pastFrameInfo,
    const IMAGE_BUFFER_S *pastImageBuffer);
K1_VI_BUF_NODE_S *K1_VI_FindBufNodeByBufferId(K1_VI_CHN_CTX_S *pstChnCtx, UL ulBufferId);
K1_VI_BUF_NODE_S *K1_VI_FindBufNodeByImageBuffer(K1_VI_CHN_CTX_S *pstChnCtx, const IMAGE_BUFFER_S *pstImageBuffer);
K1_VI_BUF_NODE_S *K1_VI_GetIdleBufNode(K1_VI_CHN_CTX_S *pstChnCtx);
K1_VI_BUF_NODE_S *K1_VI_GetBufNodeByIndex(K1_VI_CHN_CTX_S *pstChnCtx, U32 u32Index);
S32 K1_VI_DonePush(K1_VI_CHN_CTX_S *pstChnCtx, U32 u32Index);
S32 K1_VI_DonePop(K1_VI_CHN_CTX_S *pstChnCtx, U32 *pu32Index);
VOID K1_VI_DestroyOutBufPool(K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_QueueAllBuffers(K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_QueueBufNode(K1_VI_CHN_CTX_S *pstChnCtx, K1_VI_BUF_NODE_S *pstBufNode);
VOID K1_VI_UpdateBufNodeMeta(K1_VI_BUF_NODE_S *pstBufNode, const VI_IMAGE_BUFFER_S *vi_buffer);
VOID K1_VI_FillImageBufferFromVideoFrame(const VideoFrameInfo *fi, IMAGE_BUFFER_S *ib);

#endif /* VI_K1_BUFFER_H */
