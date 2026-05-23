/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
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

typedef struct _K1_VI_OFFLINE_CFG_S {
    BOOL bConfigured;
    BOOL bStarted;
    VI_DEV ViDev;
    VI_CHN ViChn;
    U8 *pu8RawVirAddr;
    U32 u32RawSize;
    ISP_PUB_ATTR_S stPubAttr;
    ISP_OFFLINE_ATTR_S stOfflineAttr;
    UL ulVbPool;
    UL ulBufferId;
    VideoFrameInfo stInputFrame;
    IMAGE_BUFFER_S stInputImageBuffer;
} K1_VI_OFFLINE_CFG_S;

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

    pstOfflineCfg->ulBufferId = 0;
    pstOfflineCfg->ulVbPool = 0;
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

static S32 K1_VI_FillOfflineInputBuffer(K1_VI_OFFLINE_CFG_S *pstOfflineCfg) {
    VOID *pVirAddr = NULL;

    if (pstOfflineCfg == NULL)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstOfflineCfg->stInputFrame.stVFrame.ulPlaneVirAddr[0] == 0) {
        error(
            "%s: offline input vir addr is null, bufId=%lu pool=%lu total=%u\n",
            __func__,
            pstOfflineCfg->ulBufferId,
            pstOfflineCfg->ulVbPool,
            pstOfflineCfg->u32RawSize);
        return K1_VI_ERR_INVALID_PARAM;
    }

    pVirAddr = (VOID *)(uintptr_t)pstOfflineCfg->stInputFrame.stVFrame.ulPlaneVirAddr[0];

    if (pstOfflineCfg->pu8RawVirAddr == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    memcpy(pVirAddr, pstOfflineCfg->pu8RawVirAddr, pstOfflineCfg->u32RawSize);
    return K1_VI_SUCCESS;
}

static S32 K1_VI_ToOfflineInputImageBuffer(const K1_VI_OFFLINE_CFG_S *pstOfflineCfg, IMAGE_BUFFER_S *pstImageBuffer) {
    if (pstOfflineCfg == NULL || pstImageBuffer == NULL)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstOfflineCfg->stInputFrame.stVFrame.ulPlaneVirAddr[0] == 0)
        return K1_VI_ERR_INVALID_PARAM;

    memcpy(pstImageBuffer, &pstOfflineCfg->stInputImageBuffer, sizeof(*pstImageBuffer));

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

S32 K1_VI_OfflineSetInputAddr(
    VI_DEV ViDev,
    VI_CHN ViChn,
    UL ulPoolId,
    UL ulBufferId,
    const VideoFrameInfo *pstFrameInfo,
    const IMAGE_BUFFER_S *pstImageBuffer,
    const U8 *pu8RawVirAddr,
    U32 u32RawSize
) {
    K1_VI_OFFLINE_CFG_S *pstOfflineCfg = NULL;
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;
    IMAGE_BUFFER_S stInputImageBuffer;
    U32 u32RawReadChn = 0;
    S32 s32Ret = 0;
    BOOL bBayerReadPrepared = MPP_FALSE;
    BOOL bOfflineIspStarted = MPP_FALSE;
    BOOL bOfflineIspInited = MPP_FALSE;

    if (pu8RawVirAddr == NULL || u32RawSize == 0 || pstFrameInfo == NULL || pstImageBuffer == NULL)
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
    pstOfflineCfg->pu8RawVirAddr = (U8 *)pu8RawVirAddr;
    pstOfflineCfg->u32RawSize = u32RawSize;
    pstOfflineCfg->ulVbPool = ulPoolId;
    pstOfflineCfg->ulBufferId = ulBufferId;
    memcpy(&pstOfflineCfg->stInputFrame, pstFrameInfo, sizeof(*pstFrameInfo));
    memcpy(&pstOfflineCfg->stInputImageBuffer, pstImageBuffer, sizeof(*pstImageBuffer));
    if (pstDevCtx->eOfflineRawType == VI_RAW_TYPE_UNKNOWN)
        pstDevCtx->eOfflineRawType = VI_RAW_TYPE_12BIT;
    s32Ret = K1_VI_FillDefaultOfflinePubAttr(pstDevCtx, &pstOfflineCfg->stPubAttr);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail_reset;
    s32Ret = K1_VI_FillDefaultOfflineAttr(&pstOfflineCfg->stOfflineAttr);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail_reset;

    s32Ret = K1_VI_InitOfflineIsp(pstChnCtx);
    if (s32Ret != K1_VI_SUCCESS) {
        error("%s: K1_VI_InitOfflineIsp failed, dev=%d chn=%d ret=%d\n", __func__, ViDev, pstOfflineCfg->ViChn, s32Ret);
        return s32Ret;
    }
    bOfflineIspInited = MPP_TRUE;

    s32Ret = K1_VI_PrepareOfflineBayerRead(pstChnCtx);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail;
    bBayerReadPrepared = MPP_TRUE;

    s32Ret = K1_VI_StartOfflineIsp(pstChnCtx);
    if (s32Ret != K1_VI_SUCCESS) {
        error(
            "%s: K1_VI_StartOfflineIsp failed, dev=%d chn=%d ret=%d\n", __func__, ViDev, pstOfflineCfg->ViChn, s32Ret);
        goto fail;
    }
    bOfflineIspStarted = MPP_TRUE;

    s32Ret = K1_VI_FillOfflineInputBuffer(pstOfflineCfg);
    if (s32Ret != K1_VI_SUCCESS) {
        error(
            "%s: K1_VI_FillOfflineInputBuffer failed, dev=%d chn=%d bufId=%lu vir0=0x%llx fd0=%lu ret=%d\n",
            __func__,
            ViDev,
            ViChn,
            pstOfflineCfg->ulBufferId,
            (uint64_t)pstOfflineCfg->stInputFrame.stVFrame.ulPlaneVirAddr[0],
            pstOfflineCfg->stInputFrame.stVFrame.u32Fd[0],
            s32Ret);
        goto fail;
    }

    s32Ret = K1_VI_ToOfflineInputImageBuffer(pstOfflineCfg, &stInputImageBuffer);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail;

    s32Ret = K1_VI_QueueAllIspFrameInfoBuffers(pstChnCtx);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail;

    s32Ret = K1_VI_GetOfflineRawReadChn(ViDev, &u32RawReadChn);
    if (s32Ret != K1_VI_SUCCESS)
        goto fail;

    s32Ret = ASR_VI_ChnQueueBuffer(u32RawReadChn, &stInputImageBuffer);
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
    if (bBayerReadPrepared == MPP_TRUE || bOfflineIspStarted == MPP_TRUE || bOfflineIspInited == MPP_TRUE ||
        pstOfflineCfg->ulVbPool != 0 || pstOfflineCfg->ulBufferId != 0)
        K1_VI_ResetOfflineCfg(ViDev);

fail_reset:
    if (pstOfflineCfg != NULL)
        memset(pstOfflineCfg, 0, sizeof(*pstOfflineCfg));

    return s32Ret;
}
