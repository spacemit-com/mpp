/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *------------------------------------------------------------------------------
 */

#include <stdio.h>

#include "include/vi_k1_defs.h"
#include "include/vi_k1_ctx.h"
#include "include/vi_k1_common.h"
#include "include/vi_k1_buffer.h"
#include "include/vi_k1_sensor.h"
#include "include/vi_k1_flow.h"

static BOOL g_bK1ViCcicInit = MPP_FALSE;

S32 K1_VI_CcicInit(VOID)
{
    if (g_bK1ViCcicInit == MPP_TRUE)
        return K1_VI_SUCCESS;

    if (ASR_CCIC_Init() != SUCCESS)
        return K1_VI_ERR_BUSY;

    g_bK1ViCcicInit = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_CcicDeInit(VOID)
{
    if (g_bK1ViCcicInit != MPP_TRUE)
        return K1_VI_SUCCESS;

    (void)ASR_CCIC_Deinit();
    g_bK1ViCcicInit = MPP_FALSE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_CcicSetDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr)
{
    K1_ASR_CCIC_DEV_ATTR_S stCcicDevAttr;

    if (K1_VI_ToAsrCcicDevAttr(ViDev, pstDevAttr, &stCcicDevAttr) != K1_VI_SUCCESS)
        return K1_VI_ERR_INVALID_PARAM;

    return (ASR_CCIC_SetDevAttr((U32)ViDev, &stCcicDevAttr) == SUCCESS) ? K1_VI_SUCCESS : K1_VI_ERR_BUSY;
}

S32 K1_VI_CcicSetChnAttr(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr, U32 *pu32CcicChn)
{
    K1_ASR_CCIC_CHN_ATTR_S stCcicChnAttr;
    U32 u32CcicChn = 0;
    S32 s32Ret;

    s32Ret = K1_VI_GetCcicChnId(ViDev, ViChn, &u32CcicChn);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    s32Ret = K1_VI_ToAsrCcicChnAttr(pstChnAttr, &stCcicChnAttr);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    s32Ret = ASR_CCIC_SetChnAttr(u32CcicChn, &stCcicChnAttr);
    if (s32Ret != SUCCESS)
        return s32Ret;

    if (pu32CcicChn != NULL)
        *pu32CcicChn = u32CcicChn;

    return K1_VI_SUCCESS;
}

S32 K1_VI_CcicQueueBufNode(K1_VI_CHN_CTX_S *pstChnCtx, K1_VI_BUF_NODE_S *pstBufNode)
{
    if (pstChnCtx == NULL || pstBufNode == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    if (ASR_CCIC_ChnQueueBuffer(pstChnCtx->u32AsrChn, &pstBufNode->stImageBuffer) != SUCCESS)
        return K1_VI_ERR_BUSY;

    pstBufNode->enState = K1_VI_BUF_STATE_IN_HW;
    return K1_VI_SUCCESS;
}

S32 K1_VI_StartCcicChnCtx(VI_DEV ViDev, VI_CHN ViChn, K1_VI_CHN_CTX_S *pstChnCtx)
{
    S32 s32Ret;

    if (pstChnCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    if (pstChnCtx->ulVbPool == 0)
        return K1_VI_ERR_INVALID_PARAM;

    s32Ret = ASR_CCIC_SetCallback(pstChnCtx->u32AsrChn, K1_VI_CcicBufferCallback);
    if (s32Ret != SUCCESS) {
        return s32Ret;
    }

    s32Ret = ASR_CCIC_EnableDev((U32)ViDev);
    if (s32Ret != SUCCESS) {
        return s32Ret;
    }

    for (U32 i = 0; i < pstChnCtx->u32BufCnt; ++i) {
        K1_VI_BUF_NODE_S *pstBufNode = &pstChnCtx->astBufNode[i];
        if (pstBufNode->bValid != MPP_TRUE)
            continue;
        s32Ret = K1_VI_CcicQueueBufNode(pstChnCtx, pstBufNode);
        if (s32Ret != K1_VI_SUCCESS) {
            (void)ASR_CCIC_DisableDev((U32)ViDev);
            return s32Ret;
        }
    }

    s32Ret = ASR_CCIC_EnableChn(pstChnCtx->u32AsrChn);
    if (s32Ret != SUCCESS) {
        (void)ASR_CCIC_DisableDev((U32)ViDev);
        return s32Ret;
    }

    s32Ret = K1_VI_StartSensor(ViDev);
    if (s32Ret != K1_VI_SUCCESS) {
        (void)ASR_CCIC_DisableChn(pstChnCtx->u32AsrChn);
        (void)ASR_CCIC_DisableDev((U32)ViDev);
        return s32Ret;
    }

    pstChnCtx->bEnabled = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_StopCcicChnCtx(VI_DEV ViDev, K1_VI_CHN_CTX_S *pstChnCtx, BOOL bDestroyPool)
{
    if (pstChnCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    if (pstChnCtx->bEnabled == MPP_TRUE) {
        (void)ASR_CCIC_DisableChn(pstChnCtx->u32AsrChn);
        (void)K1_VI_StopSensor(ViDev);
        (void)ASR_CCIC_DisableDev((U32)ViDev);
    }

    if (bDestroyPool == MPP_TRUE)
        K1_VI_DestroyOutBufPool(pstChnCtx);

    pstChnCtx->bEnabled = MPP_FALSE;
    return K1_VI_SUCCESS;
}

int32_t K1_VI_CcicBufferCallback(uint32_t nChn, CCIC_IMAGE_BUFFER_S *ccic_buffer)
{
    K1_VI_CHN_CTX_S *pstChnCtx;
    K1_VI_BUF_NODE_S *pstBufNode;

    if (ccic_buffer == NULL || ccic_buffer->buffer == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx = K1_VI_FindChnCtxByAsrChn(nChn);
    if (pstChnCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    pstBufNode = K1_VI_FindBufNodeByImageBuffer(pstChnCtx, ccic_buffer->buffer);
    if (pstBufNode == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    pstBufNode->stImageBuffer.frameId = ccic_buffer->frameId;
    pstBufNode->stImageBuffer.timeStamp = ccic_buffer->timeStamp;
    pstBufNode->stFrameInfo.stVFrame.u64PTS = ccic_buffer->timeStamp;
    pstBufNode->stFrameInfo.u32Idx = ccic_buffer->frameId;

    return K1_VI_HandleNormalCallback(pstChnCtx, pstBufNode);
}