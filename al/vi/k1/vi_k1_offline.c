/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_k1_offline.c
 * @Date      :    2026-4-16
 * @Author    :    SPACEMIT
 * @Brief     :    K1 VI offline helper demo implementation.
 *------------------------------------------------------------------------------
 */

#include "include/vi_k1_defs.h"
#include "include/vi_k1_ctx.h"
#include "include/vi_k1_common.h"
#include "include/vi_k1_isp.h"
#include "include/vi_k1_buffer.h"
#include "include/vi_k1_raw.h"
#include "dma_alloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef VIU_GET_RAW_READ_CHN
#define VIU_GET_RAW_READ_CHN(viDev, rawChn)                        \
    do {                                                           \
        (rawChn) = VIU_MAX_CHN_NUM + VIU_MAX_RAWCHN_NUM + (viDev); \
    } while (0)
#endif

#define K1_VI_OFFLINE_MAX_DEV_NUM VI_MAX_DEV_NUM
#define K1_VI_RAW10_DUMP_SIZE(w, h) (((w) / 12 + ((w) % 12 ? 1 : 0)) * 16 * (h))
#define K1_VI_RAW12_DUMP_SIZE(w, h) (((w) / 10 + ((w) % 10 ? 1 : 0)) * 16 * (h))

static K1_VI_OFFLINE_CFG_S g_astK1ViOfflineCfg[K1_VI_OFFLINE_MAX_DEV_NUM] = {0};

static S32 K1_VI_GetOfflineRawReadChn(VI_DEV ViDev, U32 *pu32RawReadChn) {
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || pu32RawReadChn == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    VIU_GET_RAW_READ_CHN((U32)ViDev, *pu32RawReadChn);
    return K1_VI_SUCCESS;
}

static S32 K1_VI_PrepareOfflineBayerRead(K1_VI_CHN_CTX_S *pstChnCtx) {
    VI_BAYER_READ_ATTR_S stBayerReadAttr;
    S32 s32Ret;

    if (pstChnCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    memset(&stBayerReadAttr, 0, sizeof(stBayerReadAttr));
    stBayerReadAttr.bGenTiming = true;
    stBayerReadAttr.s32FrmRate = 30;

    s32Ret = ASR_VI_SetBayerReadAttr((U32)pstChnCtx->ViDev, &stBayerReadAttr);
    if (s32Ret != SUCCESS)
        return s32Ret;

    s32Ret = ASR_VI_EnableBayerRead((U32)pstChnCtx->ViDev);
    if (s32Ret != SUCCESS)
        return s32Ret;

    return K1_VI_SUCCESS;
}

static VOID K1_VI_CleanupOfflineBayerRead(VI_DEV ViDev) {
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE)
        return;

    (void)ASR_VI_DisableBayerRead((U32)ViDev);
}

static K1_VI_OFFLINE_CFG_S *K1_VI_GetOfflineCfg(VI_DEV ViDev) {
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE)
        return NULL;

    return &g_astK1ViOfflineCfg[ViDev];
}

const VOID *K1_VI_OfflineGetCfg(VI_DEV ViDev) {
    return (const VOID *)K1_VI_GetOfflineCfg(ViDev);
}

static MppPixelFormat K1_VI_OfflineRawTypeToPixelFormat(CAM_SENSOR_RAWTYPE_E enRawType) {
    switch (enRawType) {
        case CAM_SENSOR_RAWTYPE_RAW8:
            return MPP_PIXEL_FORMAT_RGB_BAYER_8BITS;
        case CAM_SENSOR_RAWTYPE_RAW10:
            return MPP_PIXEL_FORMAT_RGB_BAYER_10BITS;
        case CAM_SENSOR_RAWTYPE_RAW12:
            return MPP_PIXEL_FORMAT_RGB_BAYER_12BITS;
        case CAM_SENSOR_RAWTYPE_RAW14:
            return MPP_PIXEL_FORMAT_RGB_BAYER_14BITS;
        default:
            return MPP_PIXEL_FORMAT_MAX;
    }
}

static PIXEL_FORMAT_E K1_VI_OfflinePixelFormatToAsr(MppPixelFormat ePixelFormat) {
    switch (ePixelFormat) {
        case MPP_PIXEL_FORMAT_RGB_BAYER_8BITS:
            return PIXEL_FORMAT_RAW_8BPP;
        case MPP_PIXEL_FORMAT_RGB_BAYER_10BITS:
            return PIXEL_FORMAT_RAW_10BPP;
        case MPP_PIXEL_FORMAT_RGB_BAYER_12BITS:
            return PIXEL_FORMAT_RAW_12BPP;
        case MPP_PIXEL_FORMAT_RGB_BAYER_14BITS:
            return PIXEL_FORMAT_RAW_14BPP;
        default:
            return PIXEL_FORMAT_MAX;
    }
}

static VOID K1_VI_DestroyOfflineInputBuffer(K1_VI_OFFLINE_CFG_S *pstOfflineCfg) {
    if (pstOfflineCfg == NULL)
        return;

    /* The staging buffer is owned by the K1 backend — release it here. */
    if (pstOfflineCfg->s32DmaFd > 0)
        dma_free_buf(pstOfflineCfg->s32DmaFd,
            (void *)(uintptr_t)pstOfflineCfg->stInputFrame.stVFrame.ulPlaneVirAddr[0], pstOfflineCfg->u32DmaSize);

    pstOfflineCfg->s32DmaFd = 0;
    pstOfflineCfg->u32DmaSize = 0;
    memset(&pstOfflineCfg->stInputFrame, 0, sizeof(pstOfflineCfg->stInputFrame));
    memset(&pstOfflineCfg->stInputImageBuffer, 0, sizeof(pstOfflineCfg->stInputImageBuffer));
}

VOID K1_VI_ResetOfflineCfg(VI_DEV ViDev) {
    K1_VI_OFFLINE_CFG_S *pstOfflineCfg = NULL;

    pstOfflineCfg = K1_VI_GetOfflineCfg(ViDev);
    if (pstOfflineCfg == NULL)
        return;

    if (pstOfflineCfg->bStarted == MPP_TRUE && K1_VI_IsValidChn(pstOfflineCfg->ViChn) == MPP_TRUE) {
        K1_VI_CHN_CTX_S *pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][pstOfflineCfg->ViChn];
        (void)K1_VI_StopOfflineIsp(pstChnCtx);
        (void)K1_VI_DeInitOfflineIsp(pstChnCtx);
    }

    K1_VI_CleanupOfflineBayerRead(ViDev);
    K1_VI_DestroyOfflineInputBuffer(pstOfflineCfg);
    g_stK1ViCtx.astDevCtx[ViDev].eOfflineRawType = VI_RAW_TYPE_UNKNOWN;
    memset(pstOfflineCfg, 0, sizeof(*pstOfflineCfg));
}

/*
 * Allocate the offline staging buffer, sized by the device input raw
 * format.  The staging buffer is owned by the K1 backend.
 */
static S32 K1_VI_AllocOfflineInputBuffer(K1_VI_OFFLINE_CFG_S *pstOfflineCfg) {
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;
    ViChnAttrS stRawAttr;
    VideoFrameInfo stFrameInfo;
    S32 s32Fd = -1;
    U64 u64Phy = 0;
    VOID *pVir = NULL;
    S32 s32Ret = 0;
    U32 j;

    pstDevCtx = &g_stK1ViCtx.astDevCtx[pstOfflineCfg->ViDev];

    /* The offline input type follows eOfflineRawType (default 12BIT) — the
     * same source the ISP pub attr uses — so the staging buffer, the ISP
     * configuration and the driver's raw-read channel all agree.  The live
     * sensor's raw type is irrelevant here: offline input is CPU-fed. */
    memset(&stRawAttr, 0, sizeof(stRawAttr));
    stRawAttr.ePixelFormat = K1_VI_OfflineRawTypeToPixelFormat(
        pstDevCtx->eOfflineRawType == VI_RAW_TYPE_UNKNOWN ? VI_RAW_TYPE_12BIT : pstDevCtx->eOfflineRawType);
    stRawAttr.u32Width = pstDevCtx->stAttr.u32Width;
    stRawAttr.u32Height = pstDevCtx->stAttr.u32Height;

    s32Ret = K1_VI_CalcRawDumpFrameLayout(&stRawAttr, &stFrameInfo);
    if (s32Ret != K1_VI_SUCCESS) {
        error("%s: raw layout calc failed, dev=%d fmt=%d ret=%d\n",
            __func__, pstOfflineCfg->ViDev, stRawAttr.ePixelFormat, s32Ret);
        return s32Ret;
    }

    s32Ret = dma_alloc_buf(stFrameInfo.stVFrame.u32TotalSize, &s32Fd, &u64Phy, &pVir);
    if (s32Ret != 0 || s32Fd <= 0 || pVir == NULL) {
        error("%s: dma_alloc_buf failed, dev=%d size=%u ret=%d\n",
            __func__, pstOfflineCfg->ViDev, stFrameInfo.stVFrame.u32TotalSize, s32Ret);
        return K1_VI_ERR_BUSY;
    }

    pstOfflineCfg->s32DmaFd = s32Fd;
    pstOfflineCfg->u32DmaSize = stFrameInfo.stVFrame.u32TotalSize;

    /* No VB round trip: the layout template is already in hand, fill the
     * frame info directly with the dma-buf fd and mapped virtual address. */
    memcpy(&pstOfflineCfg->stInputFrame, &stFrameInfo, sizeof(stFrameInfo));
    pstOfflineCfg->stInputFrame.eFrameType = FRAME_TYPE_VI;
    pstOfflineCfg->stInputFrame.eModId = MPP_ID_VI;
    pstOfflineCfg->stInputFrame.u32Idx = 0;
    pstOfflineCfg->stInputFrame.ulPoolId = 0;
    pstOfflineCfg->stInputFrame.ulBufferId = (UL)s32Fd; /* opaque identity token */
    pstOfflineCfg->stInputFrame.stVFrame.u32PrivateData =
        ((U32)pstOfflineCfg->ViDev << 16) | (U32)pstOfflineCfg->ViChn;
    pstOfflineCfg->stInputFrame.stVFrame.ulPlaneVirAddr[0] = (UL)pVir;
    for (j = 0; j < pstOfflineCfg->stInputFrame.stVFrame.u32PlaneNum && j < FRAME_MAX_PLANE; j++)
        pstOfflineCfg->stInputFrame.stVFrame.u32Fd[j] = (UL)s32Fd;

    K1_VI_FillImageBufferFromVideoFrame(&pstOfflineCfg->stInputFrame, &pstOfflineCfg->stInputImageBuffer);
    pstOfflineCfg->stInputImageBuffer.type = 2;

    return K1_VI_SUCCESS;
}

static S32 K1_VI_FillOfflineInputBuffer(K1_VI_OFFLINE_CFG_S *pstOfflineCfg, const U8 *pu8RawVirAddr, U32 u32RawSize) {
    VOID *pVirAddr = NULL;
    S32 s32Fd = (S32)pstOfflineCfg->stInputFrame.ulBufferId;
    S32 s32Ret = 0;

    if (pstOfflineCfg == NULL || pu8RawVirAddr == NULL)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstOfflineCfg->stInputFrame.stVFrame.ulPlaneVirAddr[0] == 0)
        return K1_VI_ERR_INVALID_PARAM;

    /* Single copy: user buffer → backend-owned staging buffer.  The ISP
     * reads this buffer through its own mapping, so flush CPU dirty lines
     * after the copy (cached dma-buf mapping). */
    pVirAddr = (VOID *)(uintptr_t)pstOfflineCfg->stInputFrame.stVFrame.ulPlaneVirAddr[0];
    s32Ret = dma_sync_buf(s32Fd, DMA_SYNC_WRITE | DMA_SYNC_START);
    if (s32Ret != 0)
        return K1_VI_ERR_INVALID_PARAM;
    memcpy(pVirAddr, pu8RawVirAddr, u32RawSize);
    (void)dma_sync_buf(s32Fd, DMA_SYNC_WRITE | DMA_SYNC_END);
    pstOfflineCfg->stInputFrame.stVFrame.u32PlaneSizeValid[0] = u32RawSize;
    pstOfflineCfg->stInputFrame.stVFrame.u32TotalSize = u32RawSize;
    pstOfflineCfg->u32RawSize = u32RawSize;

    return K1_VI_SUCCESS;
}

static S32 K1_VI_FillDefaultOfflinePubAttr(const K1_VI_DEV_CTX_S *pstDevCtx, ISP_PUB_ATTR_S *pstPubAttr) {
    CAM_SENSOR_RAWTYPE_E enRawType;
    S32 s32Ret;

    if (pstDevCtx == NULL || pstPubAttr == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    memset(pstPubAttr, 0, sizeof(*pstPubAttr));
    pstPubAttr->stInputSize.width = pstDevCtx->stAttr.u32Width;
    pstPubAttr->stInputSize.height = pstDevCtx->stAttr.u32Height;
    pstPubAttr->stOutSize.width = pstDevCtx->stAttr.u32Width;
    pstPubAttr->stOutSize.height = pstDevCtx->stAttr.u32Height;
    pstPubAttr->enBayerFmt = ISP_BAYER_PATTERN_BGGR;

    switch (pstDevCtx->eOfflineRawType != VI_RAW_TYPE_UNKNOWN ? pstDevCtx->eOfflineRawType : VI_RAW_TYPE_12BIT) {
        case VI_RAW_TYPE_8BIT:
            enRawType = CAM_SENSOR_RAWTYPE_RAW8;
            break;
        case VI_RAW_TYPE_10BIT:
            enRawType = CAM_SENSOR_RAWTYPE_RAW10;
            break;
        case VI_RAW_TYPE_12BIT:
            enRawType = CAM_SENSOR_RAWTYPE_RAW12;
            break;
        case VI_RAW_TYPE_14BIT:
            enRawType = CAM_SENSOR_RAWTYPE_RAW14;
            break;
        default:
            return K1_VI_ERR_NOT_SUPPORT;
    }

    pstPubAttr->enRawType = enRawType;
    s32Ret = K1_VI_SUCCESS;
    return s32Ret;
}

static S32 K1_VI_FillDefaultOfflineAttr(ISP_OFFLINE_ATTR_S *pstOfflineAttr) {
    if (pstOfflineAttr == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    memset(pstOfflineAttr, 0, sizeof(*pstOfflineAttr));
    return K1_VI_SUCCESS;
}

S32 K1_VI_OfflineSetInputAddr(VI_DEV ViDev, VI_CHN ViChn, const U8 *pu8RawVirAddr, U32 u32RawSize) {
    K1_VI_OFFLINE_CFG_S *pstOfflineCfg = NULL;
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;
    U32 u32RawReadChn = 0;
    U32 u32StagingCapacity = 0;
    S32 s32Ret = 0;

    if (pu8RawVirAddr == NULL || u32RawSize == 0)
        return K1_VI_ERR_INVALID_PARAM;
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;

    pstOfflineCfg = K1_VI_GetOfflineCfg(ViDev);
    if (pstOfflineCfg == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    pstDevCtx = &g_stK1ViCtx.astDevCtx[ViDev];
    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bCreated != MPP_TRUE || pstChnCtx->bEnabled != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstDevCtx->stAttr.eWorkMode != VI_WORK_MODE_OFFLINE)
        return K1_VI_ERR_NOT_SUPPORT;
    if (pstOfflineCfg->bStarted == MPP_TRUE)
        return K1_VI_ERR_BUSY;

    memset(pstOfflineCfg, 0, sizeof(*pstOfflineCfg));
    pstOfflineCfg->bConfigured = MPP_TRUE;
    pstOfflineCfg->ViDev = ViDev;
    pstOfflineCfg->ViChn = ViChn;
    if (pstDevCtx->eOfflineRawType == VI_RAW_TYPE_UNKNOWN)
        pstDevCtx->eOfflineRawType = VI_RAW_TYPE_12BIT;

    /* Backend-owned staging buffer, sized by the device input raw format. */
    s32Ret = K1_VI_AllocOfflineInputBuffer(pstOfflineCfg);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail;
    u32StagingCapacity = pstOfflineCfg->stInputFrame.stVFrame.u32TotalSize;
    if (u32RawSize > u32StagingCapacity) {
        error("%s: raw size %u exceeds staging capacity %u, dev=%d\n",
            __func__, u32RawSize, u32StagingCapacity, ViDev);
        s32Ret = K1_VI_ERR_INVALID_PARAM;
        goto fail;
    }

    s32Ret = K1_VI_FillDefaultOfflinePubAttr(pstDevCtx, &pstOfflineCfg->stPubAttr);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail;
    s32Ret = K1_VI_FillDefaultOfflineAttr(&pstOfflineCfg->stOfflineAttr);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail;

    s32Ret = K1_VI_InitOfflineIsp(pstChnCtx);
    if (s32Ret != K1_VI_SUCCESS) {
        error("%s: K1_VI_InitOfflineIsp failed, dev=%d chn=%d ret=%d\n", __func__, ViDev, pstOfflineCfg->ViChn, s32Ret);
        goto fail;
    }

    s32Ret = K1_VI_PrepareOfflineBayerRead(pstChnCtx);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail;

    s32Ret = K1_VI_StartOfflineIsp(pstChnCtx);
    if (s32Ret != K1_VI_SUCCESS) {
        error(
            "%s: K1_VI_StartOfflineIsp failed, dev=%d chn=%d ret=%d\n", __func__, ViDev, ViChn, s32Ret);
        goto fail;
    }

    s32Ret = K1_VI_FillOfflineInputBuffer(pstOfflineCfg, pu8RawVirAddr, u32RawSize);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail;

    s32Ret = K1_VI_QueueAllIspFrameInfoBuffers(pstChnCtx);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail;

    s32Ret = K1_VI_GetOfflineRawReadChn(ViDev, &u32RawReadChn);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail;

    s32Ret = ASR_VI_ChnQueueBuffer(u32RawReadChn, &pstOfflineCfg->stInputImageBuffer);
    if (s32Ret != SUCCESS) {
        error(
            "%s: ASR_VI_ChnQueueBuffer(raw-read) failed, dev=%d chn=%d rawReadChn=%u ret=%d\n",
            __func__,
            ViDev,
            pstOfflineCfg->ViChn,
            u32RawReadChn,
            s32Ret);
        goto fail;
    }

    pstOfflineCfg->bStarted = MPP_TRUE;
    return K1_VI_SUCCESS;

fail:
    K1_VI_ResetOfflineCfg(ViDev);
    return s32Ret;
}
