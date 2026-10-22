/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *------------------------------------------------------------------------------
 */

#include <string.h>
#include <stdint.h>

#include "vi_buf_mgr.h"

#define MPI_VI_SUCCESS (0)
#define MPI_VI_ERR_INVALID_PARAM (-1)
#define MPI_VI_ERR_NOT_SUPPORT (-3)
#define MPI_VI_ERR_BUSY (-4)
#define MPI_VI_DEFAULT_ALIGN (16)

/*
 * Neutral pixel-math layout only.  Platform-specific memory organisation
 * (e.g. K1's DWT sub-band tails) is answered by the backend through
 * ViAlOps.calc_frame_layout; this file must stay free of per-platform
 * formulas.  Rawdump and offline buffers are allocated by the backend.
 */

static U32 MPI_VI_AlignUp(U32 u32Value, U32 u32Align) {
    if (u32Align == 0)
        return u32Value;

    return (u32Value + u32Align - 1U) & ~(u32Align - 1U);
}

static U32 MPI_VI_GetStrideAlign(const ViChnAttrS *pstChnAttr) {
    if (pstChnAttr == NULL)
        return MPI_VI_DEFAULT_ALIGN;

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

S32 MPI_VI_CalcFrameInfo(const ViChnAttrS *pstChnAttr, VideoFrameInfo *pstFrameInfo) {
    U32 u32Width = 0;
    U32 u32Height = 0;
    U32 u32Stride = 0;
    U32 u32YSize = 0;
    U32 u32Align = 0;

    if (pstChnAttr == NULL || pstFrameInfo == NULL)
        return MPI_VI_ERR_INVALID_PARAM;

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
            pstFrameInfo->stVFrame.u32TotalSize =
                pstFrameInfo->stVFrame.u32PlaneSize[0] + pstFrameInfo->stVFrame.u32PlaneSize[1];
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
        case MPP_PIXEL_FORMAT_RGB_BAYER_10BITS_PACKED:
            /* MIPI CSI-2 RAW10 packed: every 4 pixels share 5 bytes, no padding.
             * Matches the V4L2 'pBAA' fourcc sizeimage = (width * 5 / 4) * height. */
            pstFrameInfo->stVFrame.u32PlaneNum = 1;
            pstFrameInfo->stVFrame.u32PlaneStride[0] = ((u32Width / 4U) + ((u32Width % 4U) ? 1U : 0U)) * 5U;
            pstFrameInfo->stVFrame.u32PlaneSize[0] = pstFrameInfo->stVFrame.u32PlaneStride[0] * u32Height;
            pstFrameInfo->stVFrame.u32PlaneSizeValid[0] = pstFrameInfo->stVFrame.u32PlaneSize[0];
            pstFrameInfo->stVFrame.u32TotalSize = pstFrameInfo->stVFrame.u32PlaneSize[0];
            break;
        case MPP_PIXEL_FORMAT_UYVY:
        case MPP_PIXEL_FORMAT_YUYV:
        case MPP_PIXEL_FORMAT_YVYU:
        case MPP_PIXEL_FORMAT_VYUY:
            /* YUV422 packed: 2 bytes per pixel, single plane (e.g. UYVY, YUYV). */
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

S32 MPI_VI_CreateOutBufPool(
    VI_DEV ViDev,
    VI_CHN ViChn,
    const ViChnAttrS *pstChnAttr,
    const VideoFrameInfo *pstFrameTemplate,
    U32 u32BufCnt,
    UL *pulPoolId,
    VideoFrameInfo *pastFrameInfo,
    UL *paulBufferId
) {
    VbPoolCfg stPoolCfg;
    U32 i = 0;
    S32 s32Ret = 0;

    if (pstChnAttr == NULL || pstFrameTemplate == NULL || pulPoolId == NULL || pastFrameInfo == NULL ||
        paulBufferId == NULL || u32BufCnt == 0)
        return MPI_VI_ERR_INVALID_PARAM;

    memset(&stPoolCfg, 0, sizeof(stPoolCfg));

    stPoolCfg.u32BufCnt = u32BufCnt;
    stPoolCfg.u32BufSize = pstFrameTemplate->stVFrame.u32TotalSize;
    stPoolCfg.eModId = MPP_ID_VI;
    stPoolCfg.eRemapMode = VBUF_REMAP_MODE_NOCACHE;

    *pulPoolId = VB_CreatePool(&stPoolCfg);
    if (*pulPoolId == 0 || *pulPoolId == (UL)-1)
        return MPI_VI_ERR_BUSY;

    s32Ret = VB_SetFrameInfo(*pulPoolId, pstFrameTemplate);
    if (s32Ret != MPI_VI_SUCCESS) {
        VB_DestroyPool(*pulPoolId);
        *pulPoolId = 0;
        return s32Ret;
    }

    for (i = 0; i < u32BufCnt; i++) {
        S32 s32Fd = -1;
        U32 j = 0;

        memset(&pastFrameInfo[i], 0, sizeof(pastFrameInfo[i]));
        paulBufferId[i] = VB_ModGetBuffer(*pulPoolId, MPP_ID_VI, 0);
        if (paulBufferId[i] == 0 || paulBufferId[i] == (UL)-1) {
            MPI_VI_DestroyOutBufPool(*pulPoolId, u32BufCnt, paulBufferId);
            *pulPoolId = 0;
            return MPI_VI_ERR_BUSY;
        }

        s32Ret = VB_GetFrameInfo(paulBufferId[i], &pastFrameInfo[i]);
        if (s32Ret != MPI_VI_SUCCESS) {
            MPI_VI_DestroyOutBufPool(*pulPoolId, u32BufCnt, paulBufferId);
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
            for (j = 0; j < pastFrameInfo[i].stVFrame.u32PlaneNum && j < FRAME_MAX_PLANE; j++)
                pastFrameInfo[i].stVFrame.u32Fd[j] = (UL)s32Fd;
        }

        pastFrameInfo[i].eFrameType = FRAME_TYPE_VI;
        pastFrameInfo[i].eModId = MPP_ID_VI;
        pastFrameInfo[i].u32Idx = i;
        pastFrameInfo[i].ulPoolId = *pulPoolId;
        pastFrameInfo[i].ulBufferId = paulBufferId[i];
        pastFrameInfo[i].stVFrame.u32PrivateData = ((U32)ViDev << 16) | (U32)ViChn;
    }

    return MPI_VI_SUCCESS;
}

VOID MPI_VI_DestroyOutBufPool(UL ulPoolId, U32 u32BufCnt, UL *paulBufferId) {
    U32 i = 0;

    if (paulBufferId != NULL) {
        for (i = 0; i < u32BufCnt; i++) {
            if (paulBufferId[i] != 0 && paulBufferId[i] != (UL)-1)
                (void)VB_ModReleaseBuffer(paulBufferId[i], MPP_ID_VI);
            paulBufferId[i] = 0;
        }
    }

    if (ulPoolId != 0)
        (void)VB_DestroyPool(ulPoolId);
}
