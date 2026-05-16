/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    vi_k1_flow.h
* @Date      :    2026-3-30
* @Author    :    SPACEMIT
* @Brief     :    K1 VI channel flow helpers.
*------------------------------------------------------------------------------
*/

#ifndef VI_K1_FLOW_H
#define VI_K1_FLOW_H

#include "vi_k1_ctx.h"

S32 K1_VI_HandleNormalCallback(K1_VI_CHN_CTX_S *pstChnCtx, K1_VI_BUF_NODE_S *pstBufNode);
S32 K1_VI_StartChnCtx(VI_DEV ViDev, VI_CHN ViChn, K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_StopChnCtx(VI_DEV ViDev, K1_VI_CHN_CTX_S *pstChnCtx, BOOL bDestroyPool);
int32_t K1_VI_BufferCallback(uint32_t nChn, VI_IMAGE_BUFFER_S *vi_buffer);
int32_t K1_VI_CcicBufferCallback(uint32_t nChn, CCIC_IMAGE_BUFFER_S *ccic_buffer);

#endif /* VI_K1_FLOW_H */
