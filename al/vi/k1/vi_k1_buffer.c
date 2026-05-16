/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
*------------------------------------------------------------------------------
*/
#include <stdio.h>
#include "include/vi_k1_defs.h"
#include "include/vi_k1_ctx.h"
#include "include/vi_k1_buffer.h"
#include "include/vi_k1_common.h"

static PIXEL_FORMAT_E K1_VI_ToAsrImagePixelFormat(MppPixelFormat ePixelFormat)
{
    switch (ePixelFormat) {
    case MPP_PIXEL_FORMAT_NV12:
        return PIXEL_FORMAT_NV12_DWT;
    case MPP_PIXEL_FORMAT_NV21:
        return PIXEL_FORMAT_NV12_DWT;
    case MPP_PIXEL_FORMAT_RGB_BAYER_8BITS:
        return PIXEL_FORMAT_RAW_8BPP;
    case MPP_PIXEL_FORMAT_RGB_BAYER_10BITS:
        return PIXEL_FORMAT_RAW_10BPP;
    case MPP_PIXEL_FORMAT_RGB_BAYER_12BITS:
        return PIXEL_FORMAT_RAW_12BPP;
    case MPP_PIXEL_FORMAT_RGB_BAYER_14BITS:
        return PIXEL_FORMAT_RAW_14BPP;
    case MPP_PIXEL_FORMAT_RGB_565:
        return PIXEL_FORMAT_RGB565;
    case MPP_PIXEL_FORMAT_RGB_888:
        return PIXEL_FORMAT_RGB888;
    case MPP_PIXEL_FORMAT_YUYV:
    case MPP_PIXEL_FORMAT_YVYU:
    case MPP_PIXEL_FORMAT_UYVY:
    case MPP_PIXEL_FORMAT_VYUY:
        return PIXEL_FORMAT_YUYV;
    case MPP_PIXEL_FORMAT_YUV422SP_P010:
        return PIXEL_FORMAT_P210;
    case MPP_PIXEL_FORMAT_NV12_P010:
        return PIXEL_FORMAT_P010;
    default:
        return PIXEL_FORMAT_MAX;
    }
}

static U32 K1_VI_AlignUp(U32 u32Value, U32 u32Align)
{
    if (u32Align == 0){
        return u32Value;
    }

    return (u32Value + u32Align - 1U) & ~(u32Align - 1U);
}

static U32 K1_VI_GetStrideAlign(const ViChnAttrS *pstChnAttr)
{
    if (pstChnAttr == NULL){
        return K1_VI_DEFAULT_ALIGN;
    }

    switch (pstChnAttr->eStrideAlign) {
    case VI_STRIDE_ALIGN_16:
        return 16U;
    case VI_STRIDE_ALIGN_32:
        return 32U;
    case VI_STRIDE_ALIGN_64:
        return 64U;
    case VI_STRIDE_ALIGN_DEFAULT:
    case VI_STRIDE_ALIGN_BUTT:
    default:
        return K1_VI_DEFAULT_ALIGN;
    }
}

static U32 K1_VI_CalcDwtPlaneLength(U32 u32Width, U32 u32Height, U32 u32Level, U32 u32Plane)
{
    U32 u32Divisor = 1U << u32Level;
    U32 u32AlignedWidth = K1_VI_AlignUp(u32Width, 64U);
    U32 u32AlignedHeight = K1_VI_AlignUp(u32Height, 32U);
    U32 u32PlaneWidth = ((u32AlignedWidth / u32Divisor) * 10U + 7U) / 8U;
    U32 u32PlaneHeight = (u32AlignedHeight / u32Divisor);

    if (u32Plane != 0U){
        u32PlaneHeight /= 2U;
    }

    return K1_VI_AlignUp(u32PlaneWidth * u32PlaneHeight, 4096U);
}

static U32 K1_VI_CalcDwtTotalSize(U32 u32Width, U32 u32Height)
{
    U32 u32Total = 0;
    U32 u32Level = 0;

    for (u32Level = 1; u32Level <= 4U; u32Level++) {
        u32Total += K1_VI_CalcDwtPlaneLength(u32Width, u32Height, u32Level, 0U);
        u32Total += K1_VI_CalcDwtPlaneLength(u32Width, u32Height, u32Level, 1U);
    }

    return u32Total;
}

static VOID K1_VI_FillDwtPlanes(IMAGE_BUFFER_S *pstImageBuffer, U32 u32BaseWidth, U32 u32BaseHeight,
    int iFd, void *pBaseVir, U32 *pu32Offset)
{
    U32 u32Level = 0;
    IMAGE_BUFFER_PLANE_S *pastDwt[4] = {
        pstImageBuffer->dwt1,
        pstImageBuffer->dwt2,
        pstImageBuffer->dwt3,
        pstImageBuffer->dwt4,
    };

    for (u32Level = 1; u32Level <= 4U; u32Level++) {
        IMAGE_BUFFER_PLANE_S *pstPlane = pastDwt[u32Level - 1U];
        U32 u32Divisor = 1U << u32Level;
        U32 u32Width = ((K1_VI_AlignUp(u32BaseWidth, 64U) / u32Divisor) * 10U + 7U) / 8U;
        U32 u32Height = (K1_VI_AlignUp(u32BaseHeight, 32U) / u32Divisor);
        U32 u32Plane = 0;

        for (u32Plane = 0; u32Plane < 2U; u32Plane++) {
            U32 u32PlaneHeight = (u32Plane == 0U) ? u32Height : (u32Height / 2U);
            U32 u32Length = K1_VI_CalcDwtPlaneLength(u32BaseWidth, u32BaseHeight, u32Level, u32Plane);

            pstPlane[u32Plane].width = u32Width;
            pstPlane[u32Plane].height = u32PlaneHeight;
            pstPlane[u32Plane].stride = u32Width;
            pstPlane[u32Plane].scanline = u32PlaneHeight;
            pstPlane[u32Plane].offset = *pu32Offset;
            pstPlane[u32Plane].length = u32Length;
            pstPlane[u32Plane].virAddr = (pBaseVir != NULL) ? ((char *)pBaseVir + *pu32Offset) : NULL;
            pstPlane[u32Plane].fd = iFd;
            *pu32Offset += u32Length;
        }
    }
}

S32 K1_VI_ImportExternalBufPool(VI_DEV ViDev, VI_CHN ViChn, K1_VI_CHN_CTX_S *pstChnCtx,
    UL ulPoolId, U32 u32BufCnt,
    const UL *paulBufferId,
    const VideoFrameInfo *pastFrameInfo,
    const IMAGE_BUFFER_S *pastImageBuffer)
{
    U32 i = 0;

    if (pstChnCtx == NULL || paulBufferId == NULL || pastFrameInfo == NULL ||
        pastImageBuffer == NULL || u32BufCnt == 0 || u32BufCnt > K1_VI_MAX_BUF_CNT){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstChnCtx->ulVbPool = ulPoolId;
    pstChnCtx->u32BufCnt = u32BufCnt;
    pstChnCtx->u32DoneHead = 0;
    pstChnCtx->u32DoneTail = 0;
    pstChnCtx->u32DoneNum = 0;


    for (i = 0; i < u32BufCnt; i++) {
        K1_VI_BUF_NODE_S *pstBufNode = &pstChnCtx->astBufNode[i];

        memset(pstBufNode, 0, sizeof(*pstBufNode));
        pstBufNode->bValid = MPP_TRUE;
        pstBufNode->u32Index = i;
        pstBufNode->ulPoolId = ulPoolId;
        pstBufNode->ulBufferId = paulBufferId[i];
        memcpy(&pstBufNode->stFrameInfo, &pastFrameInfo[i], sizeof(pstBufNode->stFrameInfo));
        memcpy(&pstBufNode->stImageBuffer, &pastImageBuffer[i], sizeof(pstBufNode->stImageBuffer));
        pstBufNode->stFrameInfo.stVFrame.u32PrivateData = ((U32)ViDev << 16) | (U32)ViChn;
        pstBufNode->enState = K1_VI_BUF_STATE_IDLE;
    }

    return K1_VI_SUCCESS;
}

K1_VI_BUF_NODE_S *K1_VI_FindBufNodeByBufferId(K1_VI_CHN_CTX_S *pstChnCtx, UL ulBufferId)
{
    U32 i = 0;

    if (pstChnCtx == NULL){
        return NULL;
    }

    for (i = 0; i < pstChnCtx->u32BufCnt; i++) {
        if (pstChnCtx->astBufNode[i].bValid == MPP_TRUE && pstChnCtx->astBufNode[i].ulBufferId == ulBufferId){
            return &pstChnCtx->astBufNode[i];
        }
    }

    return NULL;
}

K1_VI_BUF_NODE_S *K1_VI_FindBufNodeByImageBuffer(K1_VI_CHN_CTX_S *pstChnCtx, const IMAGE_BUFFER_S *pstImageBuffer)
{
    U32 i = 0;

    if (pstChnCtx == NULL || pstImageBuffer == NULL){
        return NULL;
    }

    for (i = 0; i < pstChnCtx->u32BufCnt; i++) {
        if (pstChnCtx->astBufNode[i].bValid == MPP_TRUE &&
            pstChnCtx->astBufNode[i].stImageBuffer.planes[0].virAddr == pstImageBuffer->planes[0].virAddr) {
            return &pstChnCtx->astBufNode[i];
        }
    }

    return NULL;
}

K1_VI_BUF_NODE_S *K1_VI_GetIdleBufNode(K1_VI_CHN_CTX_S *pstChnCtx)
{
    U32 i = 0;

    if (pstChnCtx == NULL){
        return NULL;
    }

    for (i = 0; i < pstChnCtx->u32BufCnt; i++) {
        if (pstChnCtx->astBufNode[i].bValid == MPP_TRUE &&
            pstChnCtx->astBufNode[i].enState == K1_VI_BUF_STATE_IDLE) {
            return &pstChnCtx->astBufNode[i];
        }
    }

    return NULL;
}

K1_VI_BUF_NODE_S *K1_VI_GetBufNodeByIndex(K1_VI_CHN_CTX_S *pstChnCtx, U32 u32Index)
{
    if (pstChnCtx == NULL || u32Index >= pstChnCtx->u32BufCnt){
        return NULL;
    }

    if (pstChnCtx->astBufNode[u32Index].bValid != MPP_TRUE){
        return NULL;
    }

    return &pstChnCtx->astBufNode[u32Index];
}

S32 K1_VI_DonePush(K1_VI_CHN_CTX_S *pstChnCtx, U32 u32Index)
{
    if (pstChnCtx == NULL || pstChnCtx->u32DoneNum >= K1_VI_MAX_BUF_CNT){
        return K1_VI_ERR_BUSY;
    }

    pstChnCtx->au32DoneQueue[pstChnCtx->u32DoneTail] = u32Index;
    pstChnCtx->u32DoneTail = (pstChnCtx->u32DoneTail + 1U) % K1_VI_MAX_BUF_CNT;
    pstChnCtx->u32DoneNum++;
    return K1_VI_SUCCESS;
}

S32 K1_VI_DonePop(K1_VI_CHN_CTX_S *pstChnCtx, U32 *pu32Index)
{
    if (pstChnCtx == NULL || pu32Index == NULL || pstChnCtx->u32DoneNum == 0){
        return K1_VI_ERR_BUSY;
    }

    *pu32Index = pstChnCtx->au32DoneQueue[pstChnCtx->u32DoneHead];
    pstChnCtx->u32DoneHead = (pstChnCtx->u32DoneHead + 1U) % K1_VI_MAX_BUF_CNT;
    pstChnCtx->u32DoneNum--;
    return K1_VI_SUCCESS;
}

VOID K1_VI_DestroyOutBufPool(K1_VI_CHN_CTX_S *pstChnCtx)
{
    U32 i = 0;

    if (pstChnCtx == NULL){
        return;
    }

    for (i = 0; i < pstChnCtx->u32BufCnt; i++) {
        if (pstChnCtx->astBufNode[i].bValid == MPP_TRUE) {
            memset(&pstChnCtx->astBufNode[i], 0, sizeof(pstChnCtx->astBufNode[i]));
        }
    }

    pstChnCtx->ulVbPool = 0;
    pstChnCtx->u32BufCnt = 0;
    pstChnCtx->u32DoneHead = 0;
    pstChnCtx->u32DoneTail = 0;
    pstChnCtx->u32DoneNum = 0;
}

VOID K1_VI_UpdateBufNodeMeta(K1_VI_BUF_NODE_S *pstBufNode, const VI_IMAGE_BUFFER_S *vi_buffer)
{
    if (pstBufNode == NULL || vi_buffer == NULL){
        return;
    }

    pstBufNode->stFrameInfo.stVFrame.u64PTS = vi_buffer->timeStamp;
    pstBufNode->stFrameInfo.stVFrame.u32PrivateData = (U32)vi_buffer->frameId;
    pstBufNode->stImageBuffer.frameId = (int)vi_buffer->frameId;
    pstBufNode->stImageBuffer.timeStamp = vi_buffer->timeStamp;
    pstBufNode->stImageBuffer.index = pstBufNode->u32Index;
    pstBufNode->stImageBuffer.m.blockId = (int)pstBufNode->ulBufferId;
}

S32 K1_VI_QueueBufNode(K1_VI_CHN_CTX_S *pstChnCtx, K1_VI_BUF_NODE_S *pstBufNode)
{
    S32 s32Ret = 0;

    if (pstChnCtx == NULL || pstBufNode == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }
    if (pstBufNode->bValid != MPP_TRUE){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstBufNode->stImageBuffer.type = 2;

    // info("Queueing buffer node index %u with buffer ID %lu for VI channel context...\n", pstBufNode->u32Index, pstBufNode->ulBufferId);
    // info("pstChnCtx->ulVbPool: %lu\n", pstChnCtx->ulVbPool);
    // info("pstChnCtx->u32AsrChn: %u\n", pstChnCtx->u32AsrChn);

    // info("test_buffer_prepare: buffer fd %d, width %d, height %d, stride %d\n", pstBufNode->stImageBuffer.m.fd,pstBufNode->stImageBuffer.planes[0].width, pstBufNode->stImageBuffer.planes[0].height, pstBufNode->stImageBuffer.planes[0].stride);
    // info("test_buffer_prepare: type %d, format %d\n", pstBufNode->stImageBuffer.type, pstBufNode->stImageBuffer.format);
    // info("test_buffer_prepare: buffer plane0 size %d plane1 size %d\n", pstBufNode->stImageBuffer.planes[0].length, pstBufNode->stImageBuffer.planes[1].length);
    // info("test_buffer_prepare: buffer plane0 virAddr %p plane1 virAddr %p\n", pstBufNode->stImageBuffer.planes[0].virAddr, pstBufNode->stImageBuffer.planes[1].virAddr);

    // info("test_buffer_prepare: buffer width %d, height %d, format %d\n", pstBufNode->stImageBuffer.size.width, pstBufNode->stImageBuffer.size.height, pstBufNode->stImageBuffer.format);
    s32Ret = ASR_VI_ChnQueueBuffer(pstChnCtx->u32AsrChn, &pstBufNode->stImageBuffer);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    pstBufNode->enState = K1_VI_BUF_STATE_IN_HW;
    return K1_VI_SUCCESS;
}

S32 K1_VI_QueueAllBuffers(K1_VI_CHN_CTX_S *pstChnCtx)
{
    U32 i = 0;
    S32 s32Ret = 0;

    if (pstChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    for (i = 0; i < pstChnCtx->u32BufCnt; i++) {

        // info("Queueing buffer %u for VI channel context...\n", i);
        K1_VI_BUF_NODE_S *pstBufNode = &pstChnCtx->astBufNode[i];

        if (pstBufNode->bValid != MPP_TRUE){
            continue;
        }

        if (pstBufNode->enState != K1_VI_BUF_STATE_IDLE){
            continue;
        }

        s32Ret = K1_VI_QueueBufNode(pstChnCtx, pstBufNode);
        if (s32Ret != K1_VI_SUCCESS){
            return s32Ret;
        }
    }

    return K1_VI_SUCCESS;
}
