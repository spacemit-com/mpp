/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
*------------------------------------------------------------------------------
*/

#include "include/vi_k1_defs.h"
#include "include/vi_k1_ctx.h"
#include "include/vi_k1_raw.h"
#include "include/vi_k1_buffer.h"
#include "include/vi_k1_common.h"
#include <stdio.h>

MppPixelFormat K1_VI_GetRawDumpPixelFormat(ViRawType eRawType)
{
    switch (eRawType) {
    case VI_RAW_TYPE_8BIT:
        return MPP_PIXEL_FORMAT_RGB_BAYER_8BITS;
    case VI_RAW_TYPE_10BIT:
        return MPP_PIXEL_FORMAT_RGB_BAYER_10BITS;
    case VI_RAW_TYPE_12BIT:
        return MPP_PIXEL_FORMAT_RGB_BAYER_12BITS;
    case VI_RAW_TYPE_14BIT:
        return MPP_PIXEL_FORMAT_RGB_BAYER_14BITS;
    default:
        return MPP_PIXEL_FORMAT_MAX;
    }
}

K1_VI_RAW_CTX_S *K1_VI_GetRawCtx(VI_DEV ViDev, VI_CHN ViChn)
{
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE){
        return NULL;
    }

    return &g_stK1ViCtx.astRawCtx[ViDev][ViChn];
}

S32 K1_VI_InitRawDumpCtx(VI_DEV ViDev, VI_CHN ViChn, K1_VI_RAW_CTX_S *pstRawCtx,
    const K1_VI_CHN_CTX_S *pstPhyChnCtx)
{
    ViChnAttrS stRawAttr;
    MppPixelFormat eRawPixelFormat;
    ViRawType eRawType;
    U32 u32AsrChn = 0;
    S32 s32Ret = 0;

    if (pstRawCtx == NULL || pstPhyChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    s32Ret = K1_VI_GetSensorRawType(&g_stK1ViCtx.astDevCtx[ViDev], &eRawType);
    if (s32Ret != K1_VI_SUCCESS){
        return s32Ret;
    }

    eRawPixelFormat = K1_VI_GetRawDumpPixelFormat(eRawType);
    if (eRawPixelFormat >= MPP_PIXEL_FORMAT_MAX){
        return K1_VI_ERR_NOT_SUPPORT;
    }

    memset(&stRawAttr, 0, sizeof(stRawAttr));
    stRawAttr.eChnType = VI_CHN_TYPE_PHYSICAL;
    stRawAttr.ePixelFormat = eRawPixelFormat;
    stRawAttr.u32Width = g_stK1ViCtx.astDevCtx[ViDev].stAttr.u32Width;
    stRawAttr.u32Height = g_stK1ViCtx.astDevCtx[ViDev].stAttr.u32Height;

    s32Ret = K1_VI_GetAsrChnId(ViDev, ViChn, &stRawAttr, &u32AsrChn);
    if (s32Ret != K1_VI_SUCCESS){
        return s32Ret;
    }

    memset(pstRawCtx, 0, sizeof(*pstRawCtx));
    pstRawCtx->ViDev = ViDev;
    pstRawCtx->ViChn = ViChn;
    pstRawCtx->u32AsrChn = u32AsrChn;
    pstRawCtx->bTriggered = MPP_FALSE;
    pstRawCtx->bFrameValid = MPP_FALSE;
    pstRawCtx->enState = K1_VI_BUF_STATE_IDLE;
    memcpy(&pstRawCtx->stAttr, &stRawAttr, sizeof(stRawAttr));

    pstRawCtx->bCreated = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_GetRawDumpAttr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstRawAttr)
{
    K1_VI_CHN_CTX_S *pstPhyChnCtx = NULL;
    ViRawType eRawType;
    MppPixelFormat eRawPixelFormat;
    S32 s32Ret = 0;

    if (pstRawAttr == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstPhyChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstPhyChnCtx->bCreated != MPP_TRUE || pstPhyChnCtx->stAttr.eChnType != VI_CHN_TYPE_PHYSICAL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    s32Ret = K1_VI_GetSensorRawType(&g_stK1ViCtx.astDevCtx[ViDev], &eRawType);
    if (s32Ret != K1_VI_SUCCESS){
        return s32Ret;
    }

    eRawPixelFormat = K1_VI_GetRawDumpPixelFormat(eRawType);
    if (eRawPixelFormat >= MPP_PIXEL_FORMAT_MAX){
        return K1_VI_ERR_NOT_SUPPORT;
    }

    memset(pstRawAttr, 0, sizeof(*pstRawAttr));
    pstRawAttr->eChnType = VI_CHN_TYPE_PHYSICAL;
    pstRawAttr->ePixelFormat = eRawPixelFormat;
    pstRawAttr->u32Width = g_stK1ViCtx.astDevCtx[ViDev].stAttr.u32Width;
    pstRawAttr->u32Height = g_stK1ViCtx.astDevCtx[ViDev].stAttr.u32Height;

    return K1_VI_SUCCESS;
}

S32 K1_VI_ImportRawDumpBuffer(VI_DEV ViDev, VI_CHN ViChn, K1_VI_RAW_CTX_S *pstRawCtx,
    const VideoFrameInfo *pstFrameInfo,
    const IMAGE_BUFFER_S *pstImageBuffer)
{
    if (pstRawCtx == NULL || pstFrameInfo == NULL || pstImageBuffer == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstRawCtx->ulVbPool = pstFrameInfo->ulPoolId;
    pstRawCtx->ulBufferId = pstFrameInfo->ulBufferId;
    pstRawCtx->stFrameInfo = *pstFrameInfo;
    pstRawCtx->stImageBuffer = *pstImageBuffer;
    pstRawCtx->stImageBuffer.type = 2;
    pstRawCtx->stFrameInfo.stVFrame.u32PrivateData = ((U32)ViDev << 16) | (U32)ViChn;
    pstRawCtx->bExternalBuf = MPP_TRUE;

    return K1_VI_SUCCESS;
}

K1_VI_RAW_CTX_S *K1_VI_GetOrCreateRawDumpCtx(VI_DEV ViDev, VI_CHN ViChn)
{
    K1_VI_CHN_CTX_S *pstPhyChnCtx = NULL;
    K1_VI_RAW_CTX_S *pstRawCtx = NULL;

    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE){
        return NULL;
    }

    pstPhyChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstPhyChnCtx->bCreated != MPP_TRUE){
        return NULL;
    }
    if (pstPhyChnCtx->stAttr.eChnType != VI_CHN_TYPE_PHYSICAL){
        return NULL;
    }

    pstRawCtx = K1_VI_GetRawCtx(ViDev, ViChn);
    if (pstRawCtx == NULL){
        return NULL;
    }
    if (pstRawCtx->bCreated == MPP_TRUE){
        return pstRawCtx;
    }

    if (K1_VI_InitRawDumpCtx(ViDev, ViChn, pstRawCtx, pstPhyChnCtx) != K1_VI_SUCCESS){
        return NULL;
    }

    return pstRawCtx;
}

S32 K1_VI_StartRawCtx(K1_VI_RAW_CTX_S *pstRawCtx)
{
    S32 s32Ret = 0;

    if (pstRawCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstRawCtx->bEnabled == MPP_TRUE){
        return K1_VI_SUCCESS;
    }

    s32Ret = ASR_VI_SetCallback(pstRawCtx->u32AsrChn, K1_VI_BufferCallback);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    s32Ret = ASR_VI_EnableBayerDump((U32)pstRawCtx->ViDev);
    if (s32Ret != SUCCESS) {
        return s32Ret;
    }

    pstRawCtx->bEnabled = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_StopRawCtx(K1_VI_RAW_CTX_S *pstRawCtx)
{
    S32 s32Ret = 0;

    if (pstRawCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstRawCtx->bEnabled == MPP_TRUE) {
        ASR_VI_DisableBayerDump((U32)pstRawCtx->ViDev);
    }

    pstRawCtx->bEnabled = MPP_FALSE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_QueueRawBuffer(K1_VI_RAW_CTX_S *pstRawCtx)
{
    S32 s32Ret = 0;

    if (pstRawCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    // info("[rawdump-queue] dev=%d chn=%d asrChn=%u fmt=%d type=%d idx=%d mfd=%d size=%ux%u planes=%d stride0=%u scan0=%u len0=%u pfd0=%d state=%d ext=%d bufId=%lu\n",
    //        pstRawCtx->ViDev,
    //        pstRawCtx->ViChn,
    //        pstRawCtx->u32AsrChn,
    //        pstRawCtx->stImageBuffer.format,
    //         pstRawCtx->stImageBuffer.type,
    //         pstRawCtx->stImageBuffer.index,
    //         pstRawCtx->stImageBuffer.m.fd,
    //        pstRawCtx->stImageBuffer.size.width,
    //        pstRawCtx->stImageBuffer.size.height,
    //        pstRawCtx->stImageBuffer.numPlanes,
    //        pstRawCtx->stImageBuffer.planes[0].stride,
    //         pstRawCtx->stImageBuffer.planes[0].scanline,
    //        pstRawCtx->stImageBuffer.planes[0].length,
    //        pstRawCtx->stImageBuffer.planes[0].fd,
    //        pstRawCtx->enState,
    //        pstRawCtx->bExternalBuf,
    //        pstRawCtx->ulBufferId);

    s32Ret = ASR_VI_ChnQueueBuffer(pstRawCtx->u32AsrChn, &pstRawCtx->stImageBuffer);
    if (s32Ret != SUCCESS) {
        error("[rawdump-queue] ASR_VI_ChnQueueBuffer failed, asrChn=%u ret=%d\n",
            pstRawCtx->u32AsrChn, s32Ret);
        return s32Ret;
    }

    pstRawCtx->enState = K1_VI_BUF_STATE_IN_HW;
    return K1_VI_SUCCESS;
}

S32 K1_VI_HandleRawDumpCallback(K1_VI_RAW_CTX_S *pstRawCtx, const VI_IMAGE_BUFFER_S *vi_buffer)
{
    if (pstRawCtx == NULL || vi_buffer == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstRawCtx->stFrameInfo.stVFrame.u64PTS = vi_buffer->timeStamp;
    pstRawCtx->stFrameInfo.stVFrame.u32PrivateData = vi_buffer->frameId;
    pstRawCtx->stImageBuffer.frameId = (int)vi_buffer->frameId;
    pstRawCtx->stImageBuffer.timeStamp = vi_buffer->timeStamp;

    pstRawCtx->enState = K1_VI_BUF_STATE_READY;
    pstRawCtx->bFrameValid = MPP_TRUE;
    pstRawCtx->bTriggered = MPP_FALSE;
    return K1_VI_SUCCESS;
}
