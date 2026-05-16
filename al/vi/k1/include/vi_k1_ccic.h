/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
*------------------------------------------------------------------------------
*/

#ifndef VI_K1_CCIC_H
#define VI_K1_CCIC_H

#include "vi_k1_ctx.h"

S32 K1_VI_CcicInit(VOID);
S32 K1_VI_CcicDeInit(VOID);
S32 K1_VI_CcicSetDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr);
S32 K1_VI_CcicSetChnAttr(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr, U32 *pu32CcicChn);
S32 K1_VI_StartCcicChnCtx(VI_DEV ViDev, VI_CHN ViChn, K1_VI_CHN_CTX_S *pstChnCtx);
S32 K1_VI_StopCcicChnCtx(VI_DEV ViDev, K1_VI_CHN_CTX_S *pstChnCtx, BOOL bDestroyPool);
S32 K1_VI_CcicQueueBufNode(K1_VI_CHN_CTX_S *pstChnCtx, K1_VI_BUF_NODE_S *pstBufNode);

#endif /* VI_K1_CCIC_H */
