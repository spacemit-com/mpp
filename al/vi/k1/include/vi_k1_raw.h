/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    vi_k1_raw.h
* @Date      :    2026-3-30
* @Author    :    SPACEMIT
* @Brief     :    K1 VI raw dump helpers.
*------------------------------------------------------------------------------
*/

#ifndef VI_K1_RAW_H
#define VI_K1_RAW_H

#include "vi_k1_ctx.h"

S32 K1_VI_InitRawDumpCtx(VI_DEV ViDev, VI_CHN ViChn, K1_VI_RAW_CTX_S *pstRawCtx,
    const K1_VI_CHN_CTX_S *pstPhyChnCtx);
S32 K1_VI_GetRawDumpAttr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstRawAttr);
S32 K1_VI_ImportRawDumpBuffer(VI_DEV ViDev, VI_CHN ViChn, K1_VI_RAW_CTX_S *pstRawCtx,
    const VideoFrameInfo *pstFrameInfo,
    const IMAGE_BUFFER_S *pstImageBuffer);
MppPixelFormat K1_VI_GetRawDumpPixelFormat(ViRawType eRawType);
S32 K1_VI_StartRawCtx(K1_VI_RAW_CTX_S *pstRawCtx);
S32 K1_VI_StopRawCtx(K1_VI_RAW_CTX_S *pstRawCtx);
S32 K1_VI_QueueRawBuffer(K1_VI_RAW_CTX_S *pstRawCtx);
S32 K1_VI_HandleRawDumpCallback(K1_VI_RAW_CTX_S *pstRawCtx, const VI_IMAGE_BUFFER_S *vi_buffer);
int32_t K1_VI_BufferCallback(uint32_t nChn, VI_IMAGE_BUFFER_S *vi_buffer);

#endif /* VI_K1_RAW_H */
