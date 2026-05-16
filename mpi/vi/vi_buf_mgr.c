/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*------------------------------------------------------------------------------
*/

#include <string.h>
#include <stdint.h>

#include "vi_buf_mgr.h"

#define MPI_VI_SUCCESS           (0)
#define MPI_VI_ERR_INVALID_PARAM (-1)
#define MPI_VI_ERR_NOT_SUPPORT   (-3)
#define MPI_VI_ERR_BUSY          (-4)
#define MPI_VI_DEFAULT_ALIGN     (16)
#define MPI_VI_RAW10_DUMP_SIZE(w, h) ((((w) / 12U) + (((w) % 12U) ? 1U : 0U)) * 16U * (h))
#define MPI_VI_RAW12_DUMP_SIZE(w, h) ((((w) / 10U) + (((w) % 10U) ? 1U : 0U)) * 16U * (h))

static U32 MPI_VI_AlignUp(U32 u32Value, U32 u32Align)
{
    if (u32Align == 0){
        return u32Value;
    }

    return (u32Value + u32Align - 1U) & ~(u32Align - 1U);
}

static U32 MPI_VI_GetStrideAlign(const ViChnAttrS *pstChnAttr)
{
    if (pstChnAttr == NULL){
        return MPI_VI_DEFAULT_ALIGN;
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
        return MPI_VI_DEFAULT_ALIGN;
    }
}

static U32 MPI_VI_CalcDwtPlaneLength(U32 u32Width, U32 u32Height, U32 u32Level, U32 u32Plane)
{
    U32 u32Divisor = 1U << u32Level;
    U32 u32AlignedWidth = MPI_VI_AlignUp(u32Width, 64U);
    U32 u32AlignedHeight = MPI_VI_AlignUp(u32Height, 32U);
    U32 u32PlaneWidth = ((u32AlignedWidth / u32Divisor) * 10U + 7U) / 8U;
    U32 u32PlaneHeight = (u32AlignedHeight / u32Divisor);

    if (u32Plane != 0U){
        u32PlaneHeight /= 2U;
    }

    return MPI_VI_AlignUp(u32PlaneWidth * u32PlaneHeight, 4096U);
}

static VOID MPI_VI_FillDwtPlanes(ImageBuffer *pstImageBuffer, U32 u32BaseWidth, U32 u32BaseHeight,
    int iFd, void *pBaseVir, U32 *pu32Offset)
{
    U32 u32Level = 0;
    ImageBufferPlane *pastDwt[4] = {
        pstImageBuffer->dwt1,
        pstImageBuffer->dwt2,
        pstImageBuffer->dwt3,
        pstImageBuffer->dwt4,
    };

    for (u32Level = 1; u32Level <= 4U; u32Level++) {
        ImageBufferPlane *pstPlane = pastDwt[u32Level - 1U];
        U32 u32Divisor = 1U << u32Level;
        U32 u32Width = ((MPI_VI_AlignUp(u32BaseWidth, 64U) / u32Divisor) * 10U + 7U) / 8U;
        U32 u32Height = (MPI_VI_AlignUp(u32BaseHeight, 32U) / u32Divisor);
        U32 u32Plane = 0;

        for (u32Plane = 0; u32Plane < 2U; u32Plane++) {
            U32 u32PlaneHeight = (u32Plane == 0U) ? u32Height : (u32Height / 2U);
            U32 u32Length = MPI_VI_CalcDwtPlaneLength(u32BaseWidth, u32BaseHeight, u32Level, u32Plane);

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

static U32 MPI_VI_CalcDwtTotalSize(U32 u32Width, U32 u32Height)
{
    U32 u32Total = 0;
    U32 u32Level = 0;

    for (u32Level = 1; u32Level <= 4U; u32Level++) {
        u32Total += MPI_VI_CalcDwtPlaneLength(u32Width, u32Height, u32Level, 0U);
        u32Total += MPI_VI_CalcDwtPlaneLength(u32Width, u32Height, u32Level, 1U);
    }

    return u32Total;
}

S32 MPI_VI_CalcFrameInfo(const ViChnAttrS *pstChnAttr, VideoFrameInfo *pstFrameInfo)
{
    U32 u32Width = 0;
    U32 u32Height = 0;
    U32 u32Stride = 0;
    U32 u32YSize = 0;
    U32 u32Align = 0;

    if (pstChnAttr == NULL || pstFrameInfo == NULL){
        return MPI_VI_ERR_INVALID_PARAM;
    }

    memset(pstFrameInfo, 0, sizeof(*pstFrameInfo));

    u32Width = pstChnAttr->u32Width;
    u32Height = pstChnAttr->u32Height;
    u32Align = MPI_VI_GetStrideAlign(pstChnAttr);
    u32Stride = MPI_VI_AlignUp(u32Width, u32Align);

    pstFrameInfo->eFrameType = FRAME_TYPE_VI;
    pstFrameInfo->eModId = MPP_ID_VI;
    pstFrameInfo->stViFrameInfo.stCommFrameInfo.u32Width = u32Width;
    pstFrameInfo->stViFrameInfo.stCommFrameInfo.u32Height = u32Height;
    pstFrameInfo->stViFrameInfo.stCommFrameInfo.u32Align = u32Align;
    pstFrameInfo->stViFrameInfo.stCommFrameInfo.ePixelFormat = pstChnAttr->ePixelFormat;
    pstFrameInfo->stViFrameInfo.stCommFrameInfo.eCompressMode = COMPRESS_MODE_NONE;
    pstFrameInfo->stViFrameInfo.stCommFrameInfo.eColorSpace = COLOR_SPACE_BT601;

    switch (pstChnAttr->ePixelFormat) {
    case MPP_PIXEL_FORMAT_NV12:
    case MPP_PIXEL_FORMAT_NV21:
        u32YSize = u32Stride * u32Height;
        pstFrameInfo->stVFrame.u32PlaneNum = 2;
        pstFrameInfo->stVFrame.u32PlaneStride[0] = u32Stride;
        pstFrameInfo->stVFrame.u32PlaneStride[1] = u32Stride;
        pstFrameInfo->stVFrame.u32PlaneSize[0] = u32YSize;
        pstFrameInfo->stVFrame.u32PlaneSize[1] = u32YSize / 2U;
        pstFrameInfo->stVFrame.u32PlaneSizeValid[0] = pstFrameInfo->stVFrame.u32PlaneSize[0];
        pstFrameInfo->stVFrame.u32PlaneSizeValid[1] = pstFrameInfo->stVFrame.u32PlaneSize[1];
        pstFrameInfo->stVFrame.u32TotalSize = pstFrameInfo->stVFrame.u32PlaneSize[0] +
            pstFrameInfo->stVFrame.u32PlaneSize[1];
        if (pstChnAttr->ePixelFormat == MPP_PIXEL_FORMAT_NV12 ||
            pstChnAttr->ePixelFormat == MPP_PIXEL_FORMAT_NV21){
            pstFrameInfo->stVFrame.u32TotalSize += MPI_VI_CalcDwtTotalSize(u32Width, u32Height);
        }
        break;
    case MPP_PIXEL_FORMAT_RGB_565:
        pstFrameInfo->stVFrame.u32PlaneNum = 1;
        pstFrameInfo->stVFrame.u32PlaneStride[0] = u32Stride * 2U;
        pstFrameInfo->stVFrame.u32PlaneSize[0] = pstFrameInfo->stVFrame.u32PlaneStride[0] * u32Height;
        pstFrameInfo->stVFrame.u32PlaneSizeValid[0] = pstFrameInfo->stVFrame.u32PlaneSize[0];
        pstFrameInfo->stVFrame.u32TotalSize = pstFrameInfo->stVFrame.u32PlaneSize[0];
        break;
    case MPP_PIXEL_FORMAT_RGB_888:
        pstFrameInfo->stVFrame.u32PlaneNum = 1;
        pstFrameInfo->stVFrame.u32PlaneStride[0] = u32Stride * 3U;
        pstFrameInfo->stVFrame.u32PlaneSize[0] = pstFrameInfo->stVFrame.u32PlaneStride[0] * u32Height;
        pstFrameInfo->stVFrame.u32PlaneSizeValid[0] = pstFrameInfo->stVFrame.u32PlaneSize[0];
        pstFrameInfo->stVFrame.u32TotalSize = pstFrameInfo->stVFrame.u32PlaneSize[0];
        break;
    case MPP_PIXEL_FORMAT_RGB_BAYER_8BITS:
        pstFrameInfo->stVFrame.u32PlaneNum = 1;
        pstFrameInfo->stVFrame.u32PlaneStride[0] = u32Stride;
        pstFrameInfo->stVFrame.u32PlaneSize[0] = u32Stride * u32Height;
        pstFrameInfo->stVFrame.u32PlaneSizeValid[0] = pstFrameInfo->stVFrame.u32PlaneSize[0];
        pstFrameInfo->stVFrame.u32TotalSize = pstFrameInfo->stVFrame.u32PlaneSize[0];
        break;
    case MPP_PIXEL_FORMAT_RGB_BAYER_10BITS:
    case MPP_PIXEL_FORMAT_RGB_BAYER_12BITS:
    case MPP_PIXEL_FORMAT_RGB_BAYER_14BITS:
        pstFrameInfo->stVFrame.u32PlaneNum = 1;
        pstFrameInfo->stVFrame.u32PlaneStride[0] = u32Stride * 2U;
        pstFrameInfo->stVFrame.u32PlaneSize[0] = pstFrameInfo->stVFrame.u32PlaneStride[0] * u32Height;
        pstFrameInfo->stVFrame.u32PlaneSizeValid[0] = pstFrameInfo->stVFrame.u32PlaneSize[0];
        pstFrameInfo->stVFrame.u32TotalSize = pstFrameInfo->stVFrame.u32PlaneSize[0];
        break;
    default:
        return MPI_VI_ERR_NOT_SUPPORT;
    }

    return MPI_VI_SUCCESS;
}

S32 MPI_VI_CalcRawDumpFrameInfo(const ViChnAttrS *pstChnAttr, VideoFrameInfo *pstFrameInfo)
{
    U32 u32Width = 0;
    U32 u32Height = 0;
    U32 u32Stride = 0;
    U32 u32Size = 0;

    if (pstChnAttr == NULL || pstFrameInfo == NULL){
        return MPI_VI_ERR_INVALID_PARAM;
    }

    memset(pstFrameInfo, 0, sizeof(*pstFrameInfo));
    u32Width = pstChnAttr->u32Width;
    u32Height = pstChnAttr->u32Height;

    pstFrameInfo->eFrameType = FRAME_TYPE_VI;
    pstFrameInfo->eModId = MPP_ID_VI;
    pstFrameInfo->stViFrameInfo.stCommFrameInfo.u32Width = u32Width;
    pstFrameInfo->stViFrameInfo.stCommFrameInfo.u32Height = u32Height;
    pstFrameInfo->stViFrameInfo.stCommFrameInfo.u32Align = VI_STRIDE_ALIGN_DEFAULT;
    pstFrameInfo->stViFrameInfo.stCommFrameInfo.ePixelFormat = pstChnAttr->ePixelFormat;
    pstFrameInfo->stViFrameInfo.stCommFrameInfo.eCompressMode = COMPRESS_MODE_NONE;
    pstFrameInfo->stViFrameInfo.stCommFrameInfo.eColorSpace = COLOR_SPACE_BT601;
    pstFrameInfo->stVFrame.u32PlaneNum = 1;

    switch (pstChnAttr->ePixelFormat) {
    case MPP_PIXEL_FORMAT_RGB_BAYER_8BITS:
        u32Stride = u32Width;
        u32Size = u32Stride * u32Height;
        break;
    case MPP_PIXEL_FORMAT_RGB_BAYER_10BITS:
        u32Stride = (((u32Width / 12U) + ((u32Width % 12U) ? 1U : 0U)) * 16U);
        u32Size = MPI_VI_RAW10_DUMP_SIZE(u32Width, u32Height);
        break;
    case MPP_PIXEL_FORMAT_RGB_BAYER_12BITS:
        u32Stride = (((u32Width / 10U) + ((u32Width % 10U) ? 1U : 0U)) * 16U);
        u32Size = MPI_VI_RAW12_DUMP_SIZE(u32Width, u32Height);
        break;
    case MPP_PIXEL_FORMAT_RGB_BAYER_14BITS:
        u32Stride = u32Width * 2U;
        u32Size = u32Stride * u32Height;
        break;
    default:
        return MPI_VI_ERR_NOT_SUPPORT;
    }

    pstFrameInfo->stVFrame.u32PlaneStride[0] = u32Stride;
    pstFrameInfo->stVFrame.u32PlaneSize[0] = u32Size;
    pstFrameInfo->stVFrame.u32PlaneSizeValid[0] = u32Size;
    pstFrameInfo->stVFrame.u32TotalSize = u32Size;

    return MPI_VI_SUCCESS;
}

S32 MPI_VI_FillImageBufferFromFrameInfo(const VideoFrameInfo *pstFrameInfo, ImageBuffer *pstImageBuffer)
{
    U32 i = 0;
    U32 u32Offset = 0;
    void *pBaseVir = NULL;
    int iFd = -1;

    if (pstFrameInfo == NULL || pstImageBuffer == NULL){
        return MPI_VI_ERR_INVALID_PARAM;
    }

    memset(pstImageBuffer, 0, sizeof(*pstImageBuffer));
    pstImageBuffer->size.width = pstFrameInfo->stViFrameInfo.stCommFrameInfo.u32Width;
    pstImageBuffer->size.height = pstFrameInfo->stViFrameInfo.stCommFrameInfo.u32Height;
    pstImageBuffer->format = (int)pstFrameInfo->stViFrameInfo.stCommFrameInfo.ePixelFormat;
    pstImageBuffer->numPlanes = pstFrameInfo->stVFrame.u32PlaneNum;
    pBaseVir = (void *)pstFrameInfo->stVFrame.ulPlaneVirAddr[0];
    iFd = (int)pstFrameInfo->stVFrame.u32Fd[0];

    if ((pBaseVir == NULL) && (pstFrameInfo->ulBufferId != 0) && (pstFrameInfo->ulBufferId != (UL)-1)) {
        if (VB_GetVirAddr(pstFrameInfo->ulBufferId, &pBaseVir) != MPI_VI_SUCCESS){
            pBaseVir = NULL;
        }
    }

    if ((iFd <= 0) && (pstFrameInfo->ulBufferId != 0) && (pstFrameInfo->ulBufferId != (UL)-1)) {
        S32 s32Fd = -1;

        if (VB_GetDmaBufFd(pstFrameInfo->ulBufferId, &s32Fd) == MPI_VI_SUCCESS){
            iFd = s32Fd;
        }
    }

    for (i = 0; i < pstFrameInfo->stVFrame.u32PlaneNum && i < IMAGE_BUFFER_MAX_PLANES; i++) {
        pstImageBuffer->planes[i].width = pstFrameInfo->stViFrameInfo.stCommFrameInfo.u32Width;
        pstImageBuffer->planes[i].height = (i == 0) ? pstFrameInfo->stViFrameInfo.stCommFrameInfo.u32Height
                                                    : (pstFrameInfo->stViFrameInfo.stCommFrameInfo.u32Height / 2U);
        pstImageBuffer->planes[i].stride = pstFrameInfo->stVFrame.u32PlaneStride[i];
        pstImageBuffer->planes[i].scanline = pstImageBuffer->planes[i].height;
        pstImageBuffer->planes[i].offset = u32Offset;
        pstImageBuffer->planes[i].length = pstFrameInfo->stVFrame.u32PlaneSize[i];
        if (pstFrameInfo->stVFrame.ulPlaneVirAddr[i] != 0U) {
            pstImageBuffer->planes[i].virAddr = (void *)pstFrameInfo->stVFrame.ulPlaneVirAddr[i];
        } else if (pBaseVir != NULL) {
            pstImageBuffer->planes[i].virAddr = (void *)((char *)pBaseVir + u32Offset);
        }
        pstImageBuffer->planes[i].fd = iFd;
        u32Offset += pstFrameInfo->stVFrame.u32PlaneSize[i];
    }

    if (pstFrameInfo->stViFrameInfo.stCommFrameInfo.ePixelFormat == MPP_PIXEL_FORMAT_NV12 ||
        pstFrameInfo->stViFrameInfo.stCommFrameInfo.ePixelFormat == MPP_PIXEL_FORMAT_NV21) {
        MPI_VI_FillDwtPlanes(pstImageBuffer,
            pstImageBuffer->size.width,
            pstImageBuffer->size.height,
            iFd,
            pBaseVir,
            &u32Offset);
    }

    pstImageBuffer->m.fd = iFd;
    pstImageBuffer->index = pstFrameInfo->u32Idx;
    return MPI_VI_SUCCESS;
}

S32 MPI_VI_CreateOutBufPool(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr,
    U32 u32BufCnt, UL *pulPoolId, VideoFrameInfo *pstFrameTemplate,
    VideoFrameInfo *pastFrameInfo, ImageBuffer *pastImageBuffer,
    UL *paulBufferId)
{
    VbPoolCfg stPoolCfg;
    VideoFrameInfo stFrameInfo;
    U32 i = 0;
    S32 s32Ret = 0;

    if (pstChnAttr == NULL || pulPoolId == NULL || pstFrameTemplate == NULL ||
        pastFrameInfo == NULL || pastImageBuffer == NULL || paulBufferId == NULL || u32BufCnt == 0){
        return MPI_VI_ERR_INVALID_PARAM;
    }

    memset(&stPoolCfg, 0, sizeof(stPoolCfg));
    memset(&stFrameInfo, 0, sizeof(stFrameInfo));

    s32Ret = MPI_VI_CalcFrameInfo(pstChnAttr, &stFrameInfo);
    if (s32Ret != MPI_VI_SUCCESS){
        return s32Ret;
    }

    stPoolCfg.u32BufCnt = u32BufCnt;
    stPoolCfg.u32BufSize = stFrameInfo.stVFrame.u32TotalSize;
    stPoolCfg.eModId = MPP_ID_VI;
    stPoolCfg.eRemapMode = VBUF_REMAP_MODE_NOCACHE;

    *pulPoolId = VB_CreatePool(&stPoolCfg);
    if (*pulPoolId == 0 || *pulPoolId == (UL)-1){
        return MPI_VI_ERR_BUSY;
    }

    s32Ret = VB_SetFrameInfo(*pulPoolId, &stFrameInfo);
    if (s32Ret != MPI_VI_SUCCESS) {
        VB_DestroyPool(*pulPoolId);
        *pulPoolId = 0;
        return s32Ret;
    }

    *pstFrameTemplate = stFrameInfo;

    for (i = 0; i < u32BufCnt; i++) {
        S32 s32Fd = -1;
        U32 j = 0;

        memset(&pastFrameInfo[i], 0, sizeof(pastFrameInfo[i]));
        memset(&pastImageBuffer[i], 0, sizeof(pastImageBuffer[i]));
        paulBufferId[i] = VB_GetBuffer(*pulPoolId, 0);
        if (paulBufferId[i] == 0 || paulBufferId[i] == (UL)-1) {
            MPI_VI_DestroyOutBufPool(*pulPoolId, u32BufCnt, paulBufferId, pastFrameInfo, pastImageBuffer);
            *pulPoolId = 0;
            return MPI_VI_ERR_BUSY;
        }

        s32Ret = VB_GetFrameInfo(paulBufferId[i], &pastFrameInfo[i]);
        if (s32Ret != MPI_VI_SUCCESS) {
            MPI_VI_DestroyOutBufPool(*pulPoolId, u32BufCnt, paulBufferId, pastFrameInfo, pastImageBuffer);
            *pulPoolId = 0;
            return s32Ret;
        }

        if (VB_GetVirAddr(paulBufferId[i], (void **)&pastFrameInfo[i].stVFrame.ulPlaneVirAddr[0]) == MPI_VI_SUCCESS) {
            U32 u32PlaneOffset = 0;

            for (j = 1; j < pastFrameInfo[i].stVFrame.u32PlaneNum && j < FRAME_MAX_PLANE; j++) {
                u32PlaneOffset += pastFrameInfo[i].stVFrame.u32PlaneSize[j - 1U];
                pastFrameInfo[i].stVFrame.ulPlaneVirAddr[j] =
                    (UL)((uintptr_t)pastFrameInfo[i].stVFrame.ulPlaneVirAddr[0] + u32PlaneOffset);
            }
        }

        if (VB_GetDmaBufFd(paulBufferId[i], &s32Fd) == MPI_VI_SUCCESS) {
            for (j = 0; j < pastFrameInfo[i].stVFrame.u32PlaneNum && j < FRAME_MAX_PLANE; j++){
                pastFrameInfo[i].stVFrame.u32Fd[j] = (UL)s32Fd;
            }
        }

        pastFrameInfo[i].eFrameType = FRAME_TYPE_VI;
        pastFrameInfo[i].eModId = MPP_ID_VI;
        pastFrameInfo[i].u32Idx = i;
        pastFrameInfo[i].ulPoolId = *pulPoolId;
        pastFrameInfo[i].ulBufferId = paulBufferId[i];
        pastFrameInfo[i].stVFrame.u32PrivateData = ((U32)ViDev << 16) | (U32)ViChn;

        s32Ret = MPI_VI_FillImageBufferFromFrameInfo(&pastFrameInfo[i], &pastImageBuffer[i]);
        if (s32Ret != MPI_VI_SUCCESS) {
            MPI_VI_DestroyOutBufPool(*pulPoolId, u32BufCnt, paulBufferId, pastFrameInfo, pastImageBuffer);
            *pulPoolId = 0;
            return s32Ret;
        }
    }

    return MPI_VI_SUCCESS;
}

VOID MPI_VI_DestroyOutBufPool(UL ulPoolId, U32 u32BufCnt, UL *paulBufferId,
    VideoFrameInfo *pastFrameInfo, ImageBuffer *pastImageBuffer)
{
    U32 i = 0;

    if (paulBufferId != NULL) {
        for (i = 0; i < u32BufCnt; i++) {
            if (paulBufferId[i] != 0 && paulBufferId[i] != (UL)-1){
                (void)VB_ReleaseBuffer(paulBufferId[i]);
            }
            paulBufferId[i] = 0;
        }
    }

    if (pastFrameInfo != NULL) {
        for (i = 0; i < u32BufCnt; i++){
            memset(&pastFrameInfo[i], 0, sizeof(pastFrameInfo[i]));
        }
    }

    if (pastImageBuffer != NULL) {
        for (i = 0; i < u32BufCnt; i++){
            memset(&pastImageBuffer[i], 0, sizeof(pastImageBuffer[i]));
        }
    }

    if (ulPoolId != 0){
        (void)VB_DestroyPool(ulPoolId);
    }
}
