/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 *------------------------------------------------------------------------------
 */

#include "include/vi_k1_defs.h"
#include "include/vi_k1_ctx.h"
#include "include/vi_k1_raw.h"
#include "include/vi_k1_buffer.h"
#include "include/vi_k1_common.h"
#include "dma_alloc.h"
#include <stdio.h>

MppPixelFormat K1_VI_GetRawDumpPixelFormat(ViRawType eRawType) {
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

/* K1 ISP raw packing: RAW10 uses 16 bytes per 12 pixels, RAW12 uses
 * 16 bytes per 10 pixels (unpacked, HW tail-padded).  These layouts are
 * K1-specific; MPI allocates the rawdump VB purely from the VideoFrameInfo
 * this function fills. */
#define K1_VI_RAW10_DUMP_SIZE(w, h) ((((w) / 12U) + (((w) % 12U) ? 1U : 0U)) * 16U * (h))
#define K1_VI_RAW12_DUMP_SIZE(w, h) ((((w) / 10U) + (((w) % 10U) ? 1U : 0U)) * 16U * (h))

S32 K1_VI_CalcRawDumpFrameLayout(const ViChnAttrS *pstChnAttr, VideoFrameInfo *pstFrameInfo) {
    U32 u32Width = 0;
    U32 u32Height = 0;
    U32 u32Stride = 0;
    U32 u32Size = 0;

    if (pstChnAttr == NULL || pstFrameInfo == NULL)
        return K1_VI_ERR_INVALID_PARAM;

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
            u32Size = K1_VI_RAW10_DUMP_SIZE(u32Width, u32Height);
            break;
        case MPP_PIXEL_FORMAT_RGB_BAYER_10BITS_PACKED:
            /* MIPI CSI-2 RAW10 packed: 5 bytes per 4 pixels, matches V4L2 sizeimage. */
            u32Stride = ((u32Width / 4U) + ((u32Width % 4U) ? 1U : 0U)) * 5U;
            u32Size = u32Stride * u32Height;
            break;
        case MPP_PIXEL_FORMAT_RGB_BAYER_12BITS:
            u32Stride = (((u32Width / 10U) + ((u32Width % 10U) ? 1U : 0U)) * 16U);
            u32Size = K1_VI_RAW12_DUMP_SIZE(u32Width, u32Height);
            break;
        case MPP_PIXEL_FORMAT_RGB_BAYER_14BITS:
            u32Stride = u32Width * 2U;
            u32Size = u32Stride * u32Height;
            break;
        default:
            return K1_VI_ERR_NOT_SUPPORT;
    }

    pstFrameInfo->stVFrame.u32PlaneStride[0] = u32Stride;
    pstFrameInfo->stVFrame.u32PlaneSize[0] = u32Size;
    pstFrameInfo->stVFrame.u32PlaneSizeValid[0] = u32Size;
    pstFrameInfo->stVFrame.u32TotalSize = u32Size;

    return K1_VI_SUCCESS;
}

K1_VI_RAW_CTX_S *K1_VI_GetRawCtx(VI_DEV ViDev, VI_CHN ViChn) {
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return NULL;

    return &g_stK1ViCtx.astRawCtx[ViDev][ViChn];
}

S32 K1_VI_InitRawDumpCtx(VI_DEV ViDev, VI_CHN ViChn, K1_VI_RAW_CTX_S *pstRawCtx, const K1_VI_CHN_CTX_S *pstPhyChnCtx) {
    ViChnAttrS stRawAttr;
    MppPixelFormat eRawPixelFormat;
    ViRawType eRawType;
    U32 u32AsrChn = 0;
    S32 s32Ret = 0;

    if (pstRawCtx == NULL || pstPhyChnCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    s32Ret = K1_VI_GetSensorRawType(&g_stK1ViCtx.astDevCtx[ViDev], &eRawType);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    eRawPixelFormat = K1_VI_GetRawDumpPixelFormat(eRawType);
    if (eRawPixelFormat >= MPP_PIXEL_FORMAT_MAX)
        return K1_VI_ERR_NOT_SUPPORT;

    memset(&stRawAttr, 0, sizeof(stRawAttr));
    stRawAttr.eChnType = VI_CHN_TYPE_PHYSICAL;
    stRawAttr.ePixelFormat = eRawPixelFormat;
    stRawAttr.u32Width = g_stK1ViCtx.astDevCtx[ViDev].stAttr.u32Width;
    stRawAttr.u32Height = g_stK1ViCtx.astDevCtx[ViDev].stAttr.u32Height;

    s32Ret = K1_VI_GetAsrChnId(ViDev, ViChn, &stRawAttr, &u32AsrChn);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

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

/*
 * Allocate the rawdump buffer.  The rawdump buffer is owned by the K1
 * backend: the layout comes from K1_VI_CalcRawDumpFrameLayout (the same
 * formulas the ISP writes), the memory is taken directly from the CMA
 * dma-heap and released in K1_VI_FreeRawDumpBuffer when the device is
 * disabled.  The frame stays valid until the next trigger on the same
 * channel.
 */
S32 K1_VI_AllocRawDumpBuffer(K1_VI_RAW_CTX_S *pstRawCtx) {
    VideoFrameInfo stFrameInfo;
    S32 s32Fd = -1;
    U64 u64Phy = 0;
    VOID *pVir = NULL;
    S32 s32Ret = 0;
    U32 j;

    if (pstRawCtx == NULL || pstRawCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    s32Ret = K1_VI_CalcRawDumpFrameLayout(&pstRawCtx->stAttr, &stFrameInfo);
    if (s32Ret != K1_VI_SUCCESS) {
        error("rawdump layout calc failed, dev=%d chn=%d ret=%d\n", pstRawCtx->ViDev, pstRawCtx->ViChn, s32Ret);
        return s32Ret;
    }

    s32Ret = dma_alloc_buf(stFrameInfo.stVFrame.u32TotalSize, &s32Fd, &u64Phy, &pVir);
    if (s32Ret != 0 || s32Fd <= 0 || pVir == NULL) {
        error("rawdump dma_alloc_buf failed, dev=%d chn=%d size=%u ret=%d\n",
            pstRawCtx->ViDev, pstRawCtx->ViChn, stFrameInfo.stVFrame.u32TotalSize, s32Ret);
        return K1_VI_ERR_BUSY;
    }

    pstRawCtx->s32DmaFd = s32Fd;
    pstRawCtx->u32DmaSize = stFrameInfo.stVFrame.u32TotalSize;

    /* No VB round trip: the layout template is already in hand, fill the
     * frame info directly with the dma-buf fd and mapped virtual address. */
    memcpy(&pstRawCtx->stFrameInfo, &stFrameInfo, sizeof(stFrameInfo));
    pstRawCtx->stFrameInfo.eFrameType = FRAME_TYPE_VI;
    pstRawCtx->stFrameInfo.eModId = MPP_ID_VI;
    pstRawCtx->stFrameInfo.u32Idx = 0;
    pstRawCtx->stFrameInfo.ulPoolId = 0;
    pstRawCtx->stFrameInfo.ulBufferId = (UL)s32Fd; /* opaque identity token for release() */
    pstRawCtx->stFrameInfo.stVFrame.u32PrivateData = ((U32)pstRawCtx->ViDev << 16) | (U32)pstRawCtx->ViChn;
    pstRawCtx->stFrameInfo.stVFrame.ulPlaneVirAddr[0] = (UL)pVir;
    for (j = 0; j < pstRawCtx->stFrameInfo.stVFrame.u32PlaneNum && j < FRAME_MAX_PLANE; j++)
        pstRawCtx->stFrameInfo.stVFrame.u32Fd[j] = (UL)s32Fd;

    K1_VI_FillImageBufferFromVideoFrame(&pstRawCtx->stFrameInfo, &pstRawCtx->stImageBuffer);
    pstRawCtx->stImageBuffer.type = 2;
    pstRawCtx->bExternalBuf = MPP_FALSE;

    info("rawdump buffer allocated, dev=%d chn=%d fmt=%d w=%u h=%u total=%u stride0=%u\n",
        pstRawCtx->ViDev,
        pstRawCtx->ViChn,
        pstRawCtx->stAttr.ePixelFormat,
        pstRawCtx->stAttr.u32Width,
        pstRawCtx->stAttr.u32Height,
        pstRawCtx->stFrameInfo.stVFrame.u32TotalSize,
        pstRawCtx->stFrameInfo.stVFrame.u32PlaneStride[0]);

    return K1_VI_SUCCESS;
}

VOID K1_VI_FreeRawDumpBuffer(K1_VI_RAW_CTX_S *pstRawCtx) {
    if (pstRawCtx == NULL)
        return;

    if (pstRawCtx->s32DmaFd > 0)
        dma_free_buf(pstRawCtx->s32DmaFd,
            (void *)(uintptr_t)pstRawCtx->stFrameInfo.stVFrame.ulPlaneVirAddr[0], pstRawCtx->u32DmaSize);

    pstRawCtx->s32DmaFd = 0;
    pstRawCtx->u32DmaSize = 0;
    memset(&pstRawCtx->stImageBuffer, 0, sizeof(pstRawCtx->stImageBuffer));
    memset(&pstRawCtx->stFrameInfo, 0, sizeof(pstRawCtx->stFrameInfo));
    pstRawCtx->bExternalBuf = MPP_FALSE;
}

S32 K1_VI_GetRawDumpAttr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstRawAttr) {
    K1_VI_CHN_CTX_S *pstPhyChnCtx = NULL;
    ViRawType eRawType;
    MppPixelFormat eRawPixelFormat;
    S32 s32Ret = 0;

    if (pstRawAttr == NULL)
        return K1_VI_ERR_INVALID_PARAM;
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstPhyChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstPhyChnCtx->bCreated != MPP_TRUE || pstPhyChnCtx->stAttr.eChnType != VI_CHN_TYPE_PHYSICAL)
        return K1_VI_ERR_INVALID_PARAM;

    s32Ret = K1_VI_GetSensorRawType(&g_stK1ViCtx.astDevCtx[ViDev], &eRawType);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    eRawPixelFormat = K1_VI_GetRawDumpPixelFormat(eRawType);
    if (eRawPixelFormat >= MPP_PIXEL_FORMAT_MAX)
        return K1_VI_ERR_NOT_SUPPORT;

    memset(pstRawAttr, 0, sizeof(*pstRawAttr));
    pstRawAttr->eChnType = VI_CHN_TYPE_PHYSICAL;
    pstRawAttr->ePixelFormat = eRawPixelFormat;
    pstRawAttr->u32Width = g_stK1ViCtx.astDevCtx[ViDev].stAttr.u32Width;
    pstRawAttr->u32Height = g_stK1ViCtx.astDevCtx[ViDev].stAttr.u32Height;

    return K1_VI_SUCCESS;
}

K1_VI_RAW_CTX_S *K1_VI_GetOrCreateRawDumpCtx(VI_DEV ViDev, VI_CHN ViChn) {
    K1_VI_CHN_CTX_S *pstPhyChnCtx = NULL;
    K1_VI_RAW_CTX_S *pstRawCtx = NULL;

    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return NULL;

    pstPhyChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstPhyChnCtx->bCreated != MPP_TRUE)
        return NULL;
    if (pstPhyChnCtx->stAttr.eChnType != VI_CHN_TYPE_PHYSICAL)
        return NULL;

    pstRawCtx = K1_VI_GetRawCtx(ViDev, ViChn);
    if (pstRawCtx == NULL)
        return NULL;
    if (pstRawCtx->bCreated == MPP_TRUE)
        return pstRawCtx;

    if (K1_VI_InitRawDumpCtx(ViDev, ViChn, pstRawCtx, pstPhyChnCtx) != K1_VI_SUCCESS)
        return NULL;

    return pstRawCtx;
}

S32 K1_VI_StartRawCtx(K1_VI_RAW_CTX_S *pstRawCtx) {
    S32 s32Ret = 0;

    if (pstRawCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    if (pstRawCtx->bEnabled == MPP_TRUE)
        return K1_VI_SUCCESS;

    s32Ret = ASR_VI_SetCallback(pstRawCtx->u32AsrChn, K1_VI_BufferCallback);
    if (s32Ret != SUCCESS)
        return s32Ret;

    s32Ret = ASR_VI_EnableBayerDump((U32)pstRawCtx->ViDev);
    if (s32Ret != SUCCESS) {
        return s32Ret;
    }

    pstRawCtx->bEnabled = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_StopRawCtx(K1_VI_RAW_CTX_S *pstRawCtx) {
    S32 s32Ret = 0;

    if (pstRawCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    if (pstRawCtx->bEnabled == MPP_TRUE) {
        ASR_VI_DisableBayerDump((U32)pstRawCtx->ViDev);
    }

    pstRawCtx->bEnabled = MPP_FALSE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_QueueRawBuffer(K1_VI_RAW_CTX_S *pstRawCtx) {
    S32 s32Ret = 0;

    if (pstRawCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    /* End of the CPU read period opened at HandleRawDumpCallback: flush any
     * residual dirty lines so the HW sees a coherent buffer. */
    if (pstRawCtx->s32DmaFd > 0)
        (void)dma_sync_buf(pstRawCtx->s32DmaFd, DMA_SYNC_READ | DMA_SYNC_END);

    // scan0=%u len0=%u pfd0=%d state=%d ext=%d bufId=%lu\n",
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
        error("[rawdump-queue] ASR_VI_ChnQueueBuffer failed, asrChn=%u ret=%d\n", pstRawCtx->u32AsrChn, s32Ret);
        return s32Ret;
    }

    pstRawCtx->enState = K1_VI_BUF_STATE_IN_HW;
    return K1_VI_SUCCESS;
}

S32 K1_VI_HandleRawDumpCallback(K1_VI_RAW_CTX_S *pstRawCtx, const VI_IMAGE_BUFFER_S *vi_buffer) {
    if (pstRawCtx == NULL || vi_buffer == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    /* HW wrote the buffer through its own mapping — invalidate CPU cache
     * lines before the user reads the raw frame. */
    if (pstRawCtx->s32DmaFd > 0)
        (void)dma_sync_buf(pstRawCtx->s32DmaFd, DMA_SYNC_READ | DMA_SYNC_START);

    pstRawCtx->stFrameInfo.stVFrame.u64PTS = vi_buffer->timeStamp;
    pstRawCtx->stFrameInfo.stVFrame.u32PrivateData = vi_buffer->frameId;
    pstRawCtx->stImageBuffer.frameId = (int)vi_buffer->frameId;
    pstRawCtx->stImageBuffer.timeStamp = vi_buffer->timeStamp;

    pstRawCtx->enState = K1_VI_BUF_STATE_READY;
    pstRawCtx->bFrameValid = MPP_TRUE;
    pstRawCtx->bTriggered = MPP_FALSE;
    return K1_VI_SUCCESS;
}
