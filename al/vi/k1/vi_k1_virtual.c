/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
*------------------------------------------------------------------------------
*/

#include "include/vi_k1_defs.h"
#include "include/vi_k1_ctx.h"
#include "include/vi_k1_virtual.h"
#include "include/vi_k1_buffer.h"
#include <stdio.h>
#include "v2d_api.h"

extern S32 K1_VI_HandleNormalCallback(K1_VI_CHN_CTX_S *pstChnCtx, K1_VI_BUF_NODE_S *pstBufNode);

static V2DRotateAngle K1_VI_GetV2dRotateMode(const ViChnAttrS *pstChnAttr)
{
    if (pstChnAttr == NULL){
        return V2D_ROT_0;
    }

    switch (pstChnAttr->eRotateMode) {
    case VI_ROT_0:
        return V2D_ROT_0;
    case VI_ROT_90:
        return V2D_ROT_90;
    case VI_ROT_180:
        return V2D_ROT_180;
    case VI_ROT_270:
        return V2D_ROT_270;
    case VI_ROT_MIRROR:
        return V2D_ROT_MIRROR;
    case VI_ROT_FLIP:
        return V2D_ROT_FLIP;
    case VI_ROT_BUTT:
    default:
        break;
    }

    if (pstChnAttr->bMirror == MPP_TRUE && pstChnAttr->bFlip == MPP_TRUE){
        return V2D_ROT_180;
    }
    if (pstChnAttr->bMirror == MPP_TRUE){
        return V2D_ROT_MIRROR;
    }
    if (pstChnAttr->bFlip == MPP_TRUE){
        return V2D_ROT_FLIP;
    }

    return V2D_ROT_0;
}

static S32 K1_VI_OutputVirtualFrame(K1_VI_CHN_CTX_S *pstVirtChnCtx, K1_VI_BUF_NODE_S *pstBufNode)
{
    if (pstVirtChnCtx == NULL || pstBufNode == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstVirtChnCtx->bSysBound == MPP_TRUE) {
        /*
        * SYS bind forwarding stub for virtual channels.
        * Virtual frames use an independent pool, so recycle immediately
        * until real sink delivery is wired in.
        */
        pstBufNode->enState = K1_VI_BUF_STATE_IDLE;
        return K1_VI_SUCCESS;
    }

    return K1_VI_HandleNormalCallback(pstVirtChnCtx, pstBufNode);
}

VOID K1_VI_CopyFrameMeta(VideoFrameInfo *pstDstFrame, const VideoFrameInfo *pstSrcFrame)
{
    VideoFrame stDstVFrame;
    UL ulPoolId = 0;
    UL ulBufferId = 0;
    U32 u32Idx = 0;
    U32 u32Width = 0;
    U32 u32Height = 0;
    MppPixelFormat ePixelFormat = MPP_PIXEL_FORMAT_MAX;

    if (pstDstFrame == NULL || pstSrcFrame == NULL){
        return;
    }

    memcpy(&stDstVFrame, &pstDstFrame->stVFrame, sizeof(stDstVFrame));
    ulPoolId = pstDstFrame->ulPoolId;
    ulBufferId = pstDstFrame->ulBufferId;
    u32Idx = pstDstFrame->u32Idx;
    u32Width = pstDstFrame->stViFrameInfo.stCommFrameInfo.u32Width;
    u32Height = pstDstFrame->stViFrameInfo.stCommFrameInfo.u32Height;
    ePixelFormat = pstDstFrame->stViFrameInfo.stCommFrameInfo.ePixelFormat;

    memcpy(pstDstFrame, pstSrcFrame, sizeof(*pstDstFrame));
    memcpy(&pstDstFrame->stVFrame, &stDstVFrame, sizeof(stDstVFrame));
    pstDstFrame->stVFrame.u64PTS = pstSrcFrame->stVFrame.u64PTS;
    pstDstFrame->stVFrame.u32FrameFlag = pstSrcFrame->stVFrame.u32FrameFlag;
    pstDstFrame->stVFrame.enRotation = pstSrcFrame->stVFrame.enRotation;
    pstDstFrame->ulPoolId = ulPoolId;
    pstDstFrame->ulBufferId = ulBufferId;
    pstDstFrame->u32Idx = u32Idx;
    pstDstFrame->stViFrameInfo.stCommFrameInfo.u32Width = u32Width;
    pstDstFrame->stViFrameInfo.stCommFrameInfo.u32Height = u32Height;
    pstDstFrame->stViFrameInfo.stCommFrameInfo.ePixelFormat = ePixelFormat;
}

static VOID K1_VI_GetSrcCropRect(const K1_VI_CHN_CTX_S *pstSrcChnCtx,
    const K1_VI_CHN_CTX_S *pstDstChnCtx,
    V2DArea *pstSrcRect)
{
    if (pstSrcRect == NULL || pstSrcChnCtx == NULL || pstDstChnCtx == NULL){
        return;
    }

    pstSrcRect->u16X = 0;
    pstSrcRect->u16Y = 0;
    pstSrcRect->u16W = (U16)pstSrcChnCtx->stAttr.u32Width;
    pstSrcRect->u16H = (U16)pstSrcChnCtx->stAttr.u32Height;

    if (pstDstChnCtx->stAttr.bCropEnable == MPP_TRUE) {
        pstSrcRect->u16X = (U16)pstDstChnCtx->stAttr.u32CropX;
        pstSrcRect->u16Y = (U16)pstDstChnCtx->stAttr.u32CropY;
        pstSrcRect->u16W = (U16)pstDstChnCtx->stAttr.u32CropWidth;
        pstSrcRect->u16H = (U16)pstDstChnCtx->stAttr.u32CropHeight;
    }
}

S32 K1_VI_V2dProcessFrame(const K1_VI_CHN_CTX_S *pstSrcChnCtx,
    const VI_IMAGE_BUFFER_S *pstSrcBuffer,
    K1_VI_CHN_CTX_S *pstDstChnCtx,
    VideoFrameInfo *pstDstFrame)
{
    const IMAGE_BUFFER_S *pstSrcImageBuffer = NULL;
    K1_VI_BUF_NODE_S *pstSrcBufNode = NULL;
    V2DArea stSrcRect;
    V2DArea stDstRect;
    V2DBlendConf stBlendConf;
    V2DHandle hHandle = 0;
    V2DRotateAngle enRotate = V2D_ROT_0;
    S32 s32Ret = 0;

    if (pstSrcChnCtx == NULL || pstSrcBuffer == NULL || pstDstChnCtx == NULL || pstDstFrame == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstSrcImageBuffer = pstSrcBuffer->buffer;
    if (pstSrcImageBuffer == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstDstFrame->stVFrame.u32PlaneNum == 0){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstSrcBufNode = K1_VI_FindBufNodeByImageBuffer((K1_VI_CHN_CTX_S *)pstSrcChnCtx, pstSrcImageBuffer);
    if (pstSrcBufNode == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }


    K1_VI_GetSrcCropRect(pstSrcChnCtx, pstDstChnCtx, &stSrcRect);

    stDstRect.u16X = 0;
    stDstRect.u16Y = 0;
    stDstRect.u16W = (U16)pstDstChnCtx->stAttr.u32Width;
    stDstRect.u16H = (U16)pstDstChnCtx->stAttr.u32Height;

    enRotate = K1_VI_GetV2dRotateMode(&pstDstChnCtx->stAttr);


    s32Ret = V2D_BeginJob(&hHandle);
    if (s32Ret != K1_VI_SUCCESS){
        return s32Ret;
    }

    if (enRotate == V2D_ROT_0) {
        s32Ret = V2D_AddBitblitTask(hHandle,
            &pstSrcBufNode->stFrameInfo,
            &stSrcRect,
            pstDstFrame,
            &stDstRect,
            V2D_CSC_MODE_BUTT);
    } else {
        memset(&stBlendConf, 0, sizeof(stBlendConf));
        stBlendConf.stBlendLayer[0].stBlendArea = stDstRect;

        s32Ret = V2D_AddBlendTask(hHandle,
            &pstSrcBufNode->stFrameInfo,
            &stSrcRect,
            NULL,
            NULL,
            NULL,
            NULL,
            pstDstFrame,
            &stDstRect,
            &stBlendConf,
            enRotate,
            enRotate,
            V2D_CSC_MODE_BUTT,
            V2D_CSC_MODE_BUTT,
            NULL,
            V2D_NO_DITHER);
    }
    if (s32Ret != K1_VI_SUCCESS) {
        return s32Ret;
    }

    s32Ret = V2D_EndJob(hHandle);
    if (s32Ret != K1_VI_SUCCESS){
        return s32Ret;
    }

    pstDstFrame->stVFrame.u32PlaneSizeValid[0] = pstDstFrame->stVFrame.u32PlaneSize[0];
    if (pstDstFrame->stVFrame.u32PlaneNum > 1){
        pstDstFrame->stVFrame.u32PlaneSizeValid[1] = pstDstFrame->stVFrame.u32PlaneSize[1];
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_ProcessOneVirtualChn(K1_VI_CHN_CTX_S *pstSrcChnCtx,
    const VI_IMAGE_BUFFER_S *pstSrcBuffer,
    const VideoFrameInfo *pstSrcFrame,
    K1_VI_CHN_CTX_S *pstVirtChnCtx)
{
    K1_VI_BUF_NODE_S *pstBufNode = NULL;
    S32 s32Ret = 0;

    if (pstSrcChnCtx == NULL || pstSrcBuffer == NULL || pstSrcFrame == NULL || pstVirtChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstBufNode = K1_VI_GetIdleBufNode(pstVirtChnCtx);

    if (pstBufNode == NULL){
        return K1_VI_ERR_BUSY;
    }

    K1_VI_CopyFrameMeta(&pstBufNode->stFrameInfo, pstSrcFrame);
    pstBufNode->stFrameInfo.eFrameType = FRAME_TYPE_VI;
    pstBufNode->stFrameInfo.eModId = MPP_ID_VI;
    pstBufNode->stFrameInfo.u32Idx = pstBufNode->u32Index;
    pstBufNode->stFrameInfo.ulPoolId = pstVirtChnCtx->ulVbPool;
    pstBufNode->stFrameInfo.ulBufferId = pstBufNode->ulBufferId;
    pstBufNode->stFrameInfo.stViFrameInfo.stCommFrameInfo.u32Width = pstVirtChnCtx->stAttr.u32Width;
    pstBufNode->stFrameInfo.stViFrameInfo.stCommFrameInfo.u32Height = pstVirtChnCtx->stAttr.u32Height;
    pstBufNode->stFrameInfo.stViFrameInfo.stCommFrameInfo.ePixelFormat = pstVirtChnCtx->stAttr.ePixelFormat;

    s32Ret = K1_VI_V2dProcessFrame(pstSrcChnCtx, pstSrcBuffer, pstVirtChnCtx, &pstBufNode->stFrameInfo);
    if (s32Ret != K1_VI_SUCCESS) {
        pstBufNode->enState = K1_VI_BUF_STATE_IDLE;
        return s32Ret;
    }

    return K1_VI_OutputVirtualFrame(pstVirtChnCtx, pstBufNode);
}

VOID K1_VI_DispatchVirtualFrames(K1_VI_CHN_CTX_S *pstSrcChnCtx,
    const VI_IMAGE_BUFFER_S *pstSrcBuffer,
    const VideoFrameInfo *pstSrcFrame)
{
    VI_CHN ViChn;
    if (pstSrcChnCtx == NULL || pstSrcBuffer == NULL || pstSrcFrame == NULL){
        return;
    }

    for (ViChn = 0; ViChn < VI_MAX_CHN_NUM; ViChn++) {
        K1_VI_CHN_CTX_S *pstVirtChnCtx = &g_stK1ViCtx.astChnCtx[pstSrcChnCtx->ViDev][ViChn];

        if (pstVirtChnCtx->bCreated != MPP_TRUE || pstVirtChnCtx->bEnabled != MPP_TRUE){
            continue;
        }

        if (pstVirtChnCtx->bIsVirtual != MPP_TRUE){
            continue;
        }

        if (pstVirtChnCtx->ViSrcChn != pstSrcChnCtx->ViChn){
            continue;
        }

        (void)K1_VI_ProcessOneVirtualChn(pstSrcChnCtx, pstSrcBuffer, pstSrcFrame, pstVirtChnCtx);
    }
}

S32 K1_VI_StartVirtualChnCtx(VI_DEV ViDev, VI_CHN ViChn, K1_VI_CHN_CTX_S *pstChnCtx)
{
    if (pstChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (ViDev != pstChnCtx->ViDev || ViChn != pstChnCtx->ViChn){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstChnCtx->bEnabled == MPP_TRUE){
        return K1_VI_SUCCESS;
    }

    if (K1_VI_IsValidChn(pstChnCtx->ViSrcChn) != MPP_TRUE){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (g_stK1ViCtx.astChnCtx[ViDev][pstChnCtx->ViSrcChn].bEnabled != MPP_TRUE){
        return K1_VI_ERR_BUSY;
    }

    if (pstChnCtx->ulVbPool == 0){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstChnCtx->bEnabled = MPP_TRUE;
    return K1_VI_SUCCESS;
}
