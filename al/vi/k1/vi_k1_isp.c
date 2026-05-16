/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*
* @File      :    vi_k1_isp.c
* @Date      :    2026-3-28
* @Author    :    SPACEMIT
* @Brief     :    K1 VI ISP lifecycle helpers.
*------------------------------------------------------------------------------
*/

#include "include/vi_k1_defs.h"
#include "include/vi_k1_ctx.h"
#include "include/vi_k1_common.h"
#include "include/vi_k1_isp.h"
#include "include/vi_k1_sensor.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct _K1_VI_OFFLINE_CFG_VIEW_S {
    BOOL bConfigured;
    BOOL bStarted;
    VI_DEV ViDev;
    VI_CHN ViChn;
    U8                *pu8RawVirAddr;
    U32 u32RawSize;
    ISP_PUB_ATTR_S stPubAttr;
    ISP_OFFLINE_ATTR_S stOfflineAttr;
} K1_VI_OFFLINE_CFG_VIEW_S;

extern const VOID *K1_VI_OfflineGetCfg(VI_DEV ViDev);

static VOID K1_VI_ResetMetaCache(K1_VI_CHN_CTX_S *pstChnCtx)
{
    if (pstChnCtx == NULL){
        return;
    }

    memset(pstChnCtx->astMetaCache, 0, sizeof(pstChnCtx->astMetaCache));
    pstChnCtx->u32MetaWritePos = 0;
}

S32 K1_VI_CreateIspFrameInfoPool(K1_VI_CHN_CTX_S *pstChnCtx)
{
    U32 i;
    U32 u32FrameInfoSize;

    if (pstChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    u32FrameInfoSize = (U32)sizeof(FRAME_INFO_S) + (U32)ASR_ISP_GetFwFrameInfoSize();
    if (u32FrameInfoSize == 0U){
        u32FrameInfoSize = (U32)sizeof(FRAME_INFO_S);
    }

    for (i = 0; i < K1_VI_META_CACHE_DEPTH; ++i) {
        K1_VI_ISP_FRAMEINFO_NODE_S *pstNode = &pstChnCtx->astIspFrameInfoNode[i];

        if (pstNode->bAllocated == MPP_TRUE){
            continue;
        }

        memset(pstNode, 0, sizeof(*pstNode));
        pstNode->stImageBuffer.numPlanes = 1;
        pstNode->stImageBuffer.type = 0;
        pstNode->stImageBuffer.planes[0].length = u32FrameInfoSize;
        pstNode->stImageBuffer.planes[0].virAddr = malloc(u32FrameInfoSize);
        if (pstNode->stImageBuffer.planes[0].virAddr == NULL) {
            K1_VI_DestroyIspFrameInfoPool(pstChnCtx);
            return K1_VI_ERR_BUSY;
        }

        memset(pstNode->stImageBuffer.planes[0].virAddr, 0, u32FrameInfoSize);
        pstNode->bAllocated = MPP_TRUE;
    }

    K1_VI_ResetMetaCache(pstChnCtx);
    return K1_VI_SUCCESS;
}

VOID K1_VI_DestroyIspFrameInfoPool(K1_VI_CHN_CTX_S *pstChnCtx)
{
    U32 i;

    if (pstChnCtx == NULL){
        return;
    }

    for (i = 0; i < K1_VI_META_CACHE_DEPTH; ++i) {
        K1_VI_ISP_FRAMEINFO_NODE_S *pstNode = &pstChnCtx->astIspFrameInfoNode[i];
        if (pstNode->stImageBuffer.planes[0].virAddr != NULL) {
            free(pstNode->stImageBuffer.planes[0].virAddr);
        }
        memset(pstNode, 0, sizeof(*pstNode));
    }

    K1_VI_ResetMetaCache(pstChnCtx);
}

S32 K1_VI_QueueIspFrameInfoBuffer(K1_VI_CHN_CTX_S *pstChnCtx, IMAGE_BUFFER_S *pstFrameInfoBuf)
{
    S32 s32Ret;

    if (pstChnCtx == NULL || pstFrameInfoBuf == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    s32Ret = ASR_ISP_QueueFrameinfoBuffer(pstChnCtx->u32IspPipeline, pstFrameInfoBuf);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_QueueAllIspFrameInfoBuffers(K1_VI_CHN_CTX_S *pstChnCtx)
{
    U32 i;
    S32 s32Ret;

    if (pstChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    for (i = 0; i < K1_VI_META_CACHE_DEPTH; ++i) {
        K1_VI_ISP_FRAMEINFO_NODE_S *pstNode = &pstChnCtx->astIspFrameInfoNode[i];
        if (pstNode->bAllocated != MPP_TRUE){
            continue;
        }

        s32Ret = K1_VI_QueueIspFrameInfoBuffer(pstChnCtx, &pstNode->stImageBuffer);
        if (s32Ret != K1_VI_SUCCESS){
            return s32Ret;
        }
    }

    return K1_VI_SUCCESS;
}

static VOID K1_VI_MetaCacheInsert(K1_VI_CHN_CTX_S *pstChnCtx, const FRAME_INFO_S *pstFrameInfo)
{
    K1_VI_FRAME_META_NODE_S *pstNode = NULL;
    U32 u32Pos;

    if (pstChnCtx == NULL || pstFrameInfo == NULL){
        return;
    }

    u32Pos = pstChnCtx->u32MetaWritePos % K1_VI_META_CACHE_DEPTH;
    pstNode = &pstChnCtx->astMetaCache[u32Pos];

    memset(pstNode, 0, sizeof(*pstNode));
    pstNode->bValid = MPP_TRUE;
    pstNode->u32FrameId = (U32)pstFrameInfo->frameId;
    memcpy(&pstNode->stFrameInfo, pstFrameInfo, sizeof(*pstFrameInfo));

    pstChnCtx->u32MetaWritePos = (pstChnCtx->u32MetaWritePos + 1U) % K1_VI_META_CACHE_DEPTH;

    // info("[debug] MetaCacheInsert: frameId=%u stored at pos=%u\n", pstNode->u32FrameId, u32Pos);
}

static VOID K1_VI_FillPublicFrameMeta(const FRAME_INFO_S *pstSrc, ViFrameMetaInfo *pstDst)
{
    if (pstSrc == NULL || pstDst == NULL){
        return;
    }

    memset(pstDst, 0, sizeof(*pstDst));

    pstDst->u32FrameId = (U32)pstSrc->frameId;
    pstDst->u32ExpTime[0] = (U32)pstSrc->sensorExposureTime;
    pstDst->u32ExpLine[0] = (U32)pstSrc->sensorVts;
    pstDst->u32Again[0] = (U32)pstSrc->snsAGain;
    pstDst->u32Dgain[0] = (U32)pstSrc->snsDGain;
    pstDst->u32IspDgain[0] = (U32)pstSrc->imageTGain;
    pstDst->u32ColorTemp = (U32)pstSrc->curCorrelationCT;
    pstDst->u32RGain = (U32)pstSrc->awbRgain;
    pstDst->u32BGain = (U32)pstSrc->awbBgain;
    pstDst->u32CCM[0] = (U32)pstSrc->currentCcm00;
    pstDst->u32CCM[1] = (U32)pstSrc->currentCcm01;
    pstDst->u32CCM[2] = (U32)pstSrc->currentCcm02;
    pstDst->u32CCM[3] = (U32)pstSrc->currentCcm10;
    pstDst->u32CCM[4] = (U32)pstSrc->currentCcm11;
    pstDst->u32CCM[5] = (U32)pstSrc->currentCcm12;
    pstDst->u32CCM[6] = (U32)pstSrc->currentCcm20;
    pstDst->u32CCM[7] = (U32)pstSrc->currentCcm21;
    pstDst->u32CCM[8] = (U32)pstSrc->currentCcm22;
    pstDst->u32BlackLevel[0] = (U32)pstSrc->blc12b;
    pstDst->u8AeStable = (U8)pstSrc->aeStableFlag;
    pstDst->u8AwbStable = (U8)pstSrc->awbStableFlag;
}

static S32 K1_VI_MetaCacheQuery(const K1_VI_CHN_CTX_S *pstChnCtx,
    U32 u32FrameId,
    ViFrameMetaInfo *pstFrameInfo)
{
    U32 i;

    if (pstChnCtx == NULL || pstFrameInfo == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    for (i = 0; i < K1_VI_META_CACHE_DEPTH; ++i) {
        const K1_VI_FRAME_META_NODE_S *pstNode = &pstChnCtx->astMetaCache[i];
        if (pstNode->bValid != MPP_TRUE){
            continue;
        }
        if (pstNode->u32FrameId != u32FrameId){
            continue;
        }

        K1_VI_FillPublicFrameMeta(&pstNode->stFrameInfo, pstFrameInfo);
        return K1_VI_SUCCESS;
    }

    return K1_VI_ERR_BUSY;
}

int32_t K1_VI_IspFrameInfoCallback(uint32_t pipelineID, void *pstFrameinfoBuf)
{
    IMAGE_BUFFER_S *pstBuffer = NULL;
    FRAME_INFO_S *pstMeta = NULL;
    VI_DEV ViDev = (VI_DEV)pipelineID;
    VI_CHN ViChn = 0;
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;
    int32_t s32Ret;

    if (pstFrameinfoBuf == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstBuffer = (IMAGE_BUFFER_S *)pstFrameinfoBuf;
    if (pstBuffer->planes[0].virAddr == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstMeta = (FRAME_INFO_S *)pstBuffer->planes[0].virAddr;
    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bCreated == MPP_TRUE && pstChnCtx->bEnabled == MPP_TRUE &&
        pstChnCtx->bIspInit == MPP_TRUE && pstChnCtx->bIspStreaming == MPP_TRUE) {
        K1_VI_MetaCacheInsert(pstChnCtx, pstMeta);
    }

    if (pstChnCtx->bCreated != MPP_TRUE || pstChnCtx->bEnabled != MPP_TRUE ||
        pstChnCtx->bIspInit != MPP_TRUE || pstChnCtx->bIspStreaming != MPP_TRUE) {
        return K1_VI_SUCCESS;
    }

    s32Ret = ASR_ISP_QueueFrameinfoBuffer(pipelineID, pstBuffer);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_QueryFrameMeta(VI_DEV ViDev, VI_CHN ViChn, U32 u32FrameId, ViFrameMetaInfo *pstFrameInfo)
{
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;

    if (g_stK1ViCtx.bInit != MPP_TRUE){
        return K1_VI_ERR_NOT_INIT;
    }
    if (pstFrameInfo == NULL || K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bCreated != MPP_TRUE || pstChnCtx->bEnabled != MPP_TRUE){
        return K1_VI_ERR_INVALID_PARAM;
    }

    return K1_VI_MetaCacheQuery(pstChnCtx, u32FrameId, pstFrameInfo);
}

static S32 K1_VI_ToCamRawType(ViRawType eRawType, CAM_SENSOR_RAWTYPE_E *penRawType)
{
    if (penRawType == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    switch (eRawType) {
    case VI_RAW_TYPE_8BIT:
        *penRawType = CAM_SENSOR_RAWTYPE_RAW8;
        break;
    case VI_RAW_TYPE_10BIT:
        *penRawType = CAM_SENSOR_RAWTYPE_RAW10;
        break;
    case VI_RAW_TYPE_12BIT:
        *penRawType = CAM_SENSOR_RAWTYPE_RAW12;
        break;
    case VI_RAW_TYPE_14BIT:
        *penRawType = CAM_SENSOR_RAWTYPE_RAW14;
        break;
    default:
        return K1_VI_ERR_NOT_SUPPORT;
    }

    return K1_VI_SUCCESS;
}

static S32 K1_VI_GetOfflineIspInitAttr(const K1_VI_DEV_CTX_S *pstDevCtx,
    ISP_PUB_ATTR_S *pstPubAttr,
    ISP_OFFLINE_ATTR_S *pstOfflineAttr)
{
    const K1_VI_OFFLINE_CFG_VIEW_S *pstOfflineCfg = NULL;
    CAM_SENSOR_RAWTYPE_E enRawType;
    S32 s32Ret;

    if (pstDevCtx == NULL || pstPubAttr == NULL || pstOfflineAttr == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstOfflineCfg = (const K1_VI_OFFLINE_CFG_VIEW_S *)K1_VI_OfflineGetCfg((VI_DEV)(pstDevCtx - g_stK1ViCtx.astDevCtx));
    if (pstOfflineCfg != NULL && pstOfflineCfg->bConfigured == MPP_TRUE) {
        memcpy(pstPubAttr, &pstOfflineCfg->stPubAttr, sizeof(*pstPubAttr));
        memcpy(pstOfflineAttr, &pstOfflineCfg->stOfflineAttr, sizeof(*pstOfflineAttr));
        return K1_VI_SUCCESS;
    }

    memset(pstPubAttr, 0, sizeof(*pstPubAttr));
    pstPubAttr->stInputSize.width = pstDevCtx->stAttr.u32Width;
    pstPubAttr->stInputSize.height = pstDevCtx->stAttr.u32Height;
    pstPubAttr->stOutSize.width = pstDevCtx->stAttr.u32Width;
    pstPubAttr->stOutSize.height = pstDevCtx->stAttr.u32Height;
    pstPubAttr->enBayerFmt = ISP_BAYER_PATTERN_BGGR;

    s32Ret = K1_VI_ToCamRawType(
        pstDevCtx->eOfflineRawType != VI_RAW_TYPE_UNKNOWN ? pstDevCtx->eOfflineRawType : VI_RAW_TYPE_12BIT,
        &enRawType);
    if (s32Ret != K1_VI_SUCCESS){
        return s32Ret;
    }
    pstPubAttr->enRawType = enRawType;

    memset(pstOfflineAttr, 0, sizeof(*pstOfflineAttr));
    return K1_VI_SUCCESS;
}

S32 K1_VI_ToIspBayerPattern(U32 u32BayerPattern, ISP_BAYER_PATTERN_E *penBayerPattern)
{
    ISP_BAYER_PATTERN_E enPattern;

    switch (u32BayerPattern) {
    case ISP_BAYER_PATTERN_RGGB:
        enPattern = ISP_BAYER_PATTERN_RGGB;
        break;
    case ISP_BAYER_PATTERN_GRBG:
        enPattern = ISP_BAYER_PATTERN_GRBG;
        break;
    case ISP_BAYER_PATTERN_GBRG:
        enPattern = ISP_BAYER_PATTERN_GBRG;
        break;
    case ISP_BAYER_PATTERN_BGGR:
        enPattern = ISP_BAYER_PATTERN_BGGR;
        break;
    case ISP_BAYER_PATTERN_MONO:
        enPattern = ISP_BAYER_PATTERN_MONO;
        break;
    default:
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (penBayerPattern != NULL){
        *penBayerPattern = enPattern;
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_InitIsp(K1_VI_CHN_CTX_S *pstChnCtx)
{
    ISP_SENSOR_ATTR_S stSensorAttr;
    ISP_PUB_ATTR_S stIspPubAttr;
    ISP_TUNING_ATTRS_S stTuningAttr;
    ISP_BAYER_PATTERN_E enBayerPattern;
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;
    ViDevAttrS *pstDevAttr = NULL;
    ViRawType eRawType;
    S32 s32Ret = 0;

    if (pstChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstChnCtx->bIspInit == MPP_TRUE){
        return K1_VI_SUCCESS;
    }

    pstDevCtx = &g_stK1ViCtx.astDevCtx[pstChnCtx->ViDev];
    if (pstDevCtx->bSensorInfoValid != MPP_TRUE || pstDevCtx->pstSensorCfg == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstDevAttr = &pstDevCtx->stAttr;
    pstChnCtx->u32IspPipeline = (U32)pstChnCtx->ViDev;

    s32Ret = K1_VI_ToIspBayerPattern((U32)pstDevCtx->pstSensorCfg->pattern, &enBayerPattern);
    if (s32Ret != K1_VI_SUCCESS){
        return s32Ret;
    }

    memset(&stIspPubAttr, 0, sizeof(stIspPubAttr));
    stIspPubAttr.stInputSize.width = pstDevAttr->u32Width;
    stIspPubAttr.stInputSize.height = pstDevAttr->u32Height;
    stIspPubAttr.stOutSize.width = pstChnCtx->stAttr.u32Width;
    stIspPubAttr.stOutSize.height = pstChnCtx->stAttr.u32Height;
    stIspPubAttr.enBayerFmt = enBayerPattern;

    s32Ret = K1_VI_GetSensorRawType(pstDevCtx, &eRawType);
    if (s32Ret != K1_VI_SUCCESS){
        return s32Ret;
    }

    switch (eRawType) {
    case VI_RAW_TYPE_8BIT:
        stIspPubAttr.enRawType = CAM_SENSOR_RAWTYPE_RAW8;
        break;
    case VI_RAW_TYPE_10BIT:
        stIspPubAttr.enRawType = CAM_SENSOR_RAWTYPE_RAW10;
        break;
    case VI_RAW_TYPE_12BIT:
        stIspPubAttr.enRawType = CAM_SENSOR_RAWTYPE_RAW12;
        break;
    case VI_RAW_TYPE_14BIT:
        stIspPubAttr.enRawType = CAM_SENSOR_RAWTYPE_RAW14;
        break;
    default:
        return K1_VI_ERR_NOT_SUPPORT;
    }

    s32Ret = ASR_ISP_Construct(pstChnCtx->u32IspPipeline);

    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    memset(&stSensorAttr, 0, sizeof(stSensorAttr));
    stSensorAttr.u32SensorId = (U32)pstChnCtx->ViDev;
    s32Ret = ASR_ISP_RegSensorCallBack(pstChnCtx->u32IspPipeline, &stSensorAttr, &pstDevCtx->stSensorOps);
    // info("%s: ASR_ISP_RegSensorCallBack firmwareId %d, sensorId %d, ret = %d\n", __func__, pstChnCtx->u32IspPipeline, stSensorAttr.u32SensorId, s32Ret);
    if (s32Ret != SUCCESS){
        goto fail_destruct;
    }

    if (memcmp(&pstDevCtx->stAfOps, &(ISP_AF_MOTOR_REGISTER_S){0}, sizeof(pstDevCtx->stAfOps)) != 0) {
        s32Ret = ASR_ISP_RegAfMotorCallBack(pstChnCtx->u32IspPipeline, &pstDevCtx->stAfOps);
        if (s32Ret != SUCCESS){
            goto fail_unreg_sensor;
        }
    }

    s32Ret = ASR_ISP_SetPubAttr(pstChnCtx->u32IspPipeline, CAM_ISP_CH_ID_PREVIEW, &stIspPubAttr);
    // info("%s: ASR_ISP_SetPubAttr firmwareId %d, CH_ID_PREVIEW, input %dx%d, output %dx%d, bayerFmt %d, rawType %d, ret = %d\n",
    //     __func__, pstChnCtx->u32IspPipeline, stIspPubAttr.stInputSize.width, stIspPubAttr.stInputSize.height,
    //     stIspPubAttr.stOutSize.width, stIspPubAttr.stOutSize.height, stIspPubAttr.enBayerFmt, stIspPubAttr.enRawType, s32Ret);
    if (s32Ret != SUCCESS){
        goto fail_unreg_sensor;
    }

    s32Ret = ASR_ISP_SetChHwPipeID(pstChnCtx->u32IspPipeline, CAM_ISP_CH_ID_PREVIEW,
        pstChnCtx->u32IspPipeline == 0 ? ISP_HW_PIPE_ID_ID_0 : ISP_HW_PIPE_ID_ID_1);
    // info("%s: ASR_ISP_SetChHwPipeID firmwareId %d, CH_ID_PREVIEW, pipeID %d, ret = %d\n",
    //        __func__, pstChnCtx->u32IspPipeline, pstChnCtx->u32IspPipeline == 0 ? ISP_HW_PIPE_ID_ID_0 : ISP_HW_PIPE_ID_ID_1, s32Ret);
    if (s32Ret != SUCCESS){
        goto fail_unreg_sensor;
    }

    s32Ret = ASR_ISP_SetFrameinfoCallback(pstChnCtx->u32IspPipeline, K1_VI_IspFrameInfoCallback);
    if (s32Ret != SUCCESS){
        goto fail_unreg_sensor;
    }

    s32Ret = K1_VI_CreateIspFrameInfoPool(pstChnCtx);
    if (s32Ret != K1_VI_SUCCESS){
        goto fail_unreg_sensor;
    }

    s32Ret = K1_VI_QueueAllIspFrameInfoBuffers(pstChnCtx);
    if (s32Ret != K1_VI_SUCCESS){
        goto fail_destroy_frameinfo_pool;
    }

    memset(&stTuningAttr, 0, sizeof(stTuningAttr));
    s32Ret = ASR_ISP_SetTuningParams(pstChnCtx->u32IspPipeline, &stTuningAttr);
    if (s32Ret != SUCCESS){
        goto fail_unreg_sensor;
    }

    s32Ret = ASR_ISP_SetFps(pstChnCtx->u32IspPipeline,
        (int)pstDevCtx->pstSensorCfg->minFps,
        (int)pstDevCtx->pstSensorCfg->maxFps);
    // info("%s: ASR_ISP_SetFps %d, minFps: %f, maxFps: %f, ret = %d\n", __func__, pstChnCtx->u32IspPipeline, pstDevCtx->pstSensorCfg->minFps,
    //     pstDevCtx->pstSensorCfg->maxFps, s32Ret);

    if (s32Ret != SUCCESS){
        goto fail_unreg_sensor;
    }

    s32Ret = ASR_ISP_Init(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        goto fail_unreg_sensor;
    }

    pstChnCtx->bIspInit = MPP_TRUE;
    return K1_VI_SUCCESS;

fail_destroy_frameinfo_pool:
    K1_VI_DestroyIspFrameInfoPool(pstChnCtx);
fail_unreg_sensor:
    (void)ASR_ISP_UnRegSensorCallBack(pstChnCtx->u32IspPipeline, &stSensorAttr);
fail_destruct:
    (void)ASR_ISP_Destruct(pstChnCtx->u32IspPipeline);
    return s32Ret;
}

S32 K1_VI_InitOfflineIsp(K1_VI_CHN_CTX_S *pstChnCtx)
{
    ISP_PUB_ATTR_S stIspPubAttr;
    ISP_TUNING_ATTRS_S stTuningAttr;
    ISP_OFFLINE_ATTR_S stOfflineAttr;
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;
    ViDevAttrS *pstDevAttr = NULL;
    S32 s32Ret = 0;

    if (pstChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstChnCtx->bIspInit == MPP_TRUE){
        return K1_VI_SUCCESS;
    }

    pstDevCtx = &g_stK1ViCtx.astDevCtx[pstChnCtx->ViDev];
    if (pstDevCtx->stAttr.eWorkMode != VI_WORK_MODE_OFFLINE){
        return K1_VI_ERR_INVALID_PARAM;
    }

    pstDevAttr = &pstDevCtx->stAttr;
    pstChnCtx->u32IspPipeline = (U32)pstChnCtx->ViDev;

    s32Ret = K1_VI_GetOfflineIspInitAttr(pstDevCtx, &stIspPubAttr, &stOfflineAttr);
    if (s32Ret != K1_VI_SUCCESS){
        return s32Ret;
    }

    stIspPubAttr.stInputSize.width = pstDevAttr->u32Width;
    stIspPubAttr.stInputSize.height = pstDevAttr->u32Height;
    stIspPubAttr.stOutSize.width = pstChnCtx->stAttr.u32Width;
    stIspPubAttr.stOutSize.height = pstChnCtx->stAttr.u32Height;

    s32Ret = ASR_ISP_Construct(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    memset(&stTuningAttr, 0, sizeof(stTuningAttr));
    stTuningAttr.camScene = CAM_ISP_SCENE_PREVIEW;
    (void)ASR_ISP_SetTuningParams(pstChnCtx->u32IspPipeline, &stTuningAttr);

    s32Ret = ASR_ISP_SetPubAttr(pstChnCtx->u32IspPipeline, CAM_ISP_CH_ID_PREVIEW, &stIspPubAttr);
    if (s32Ret != SUCCESS){
        goto fail_destruct;
    }

    s32Ret = ASR_ISP_SetChHwPipeID(pstChnCtx->u32IspPipeline, CAM_ISP_CH_ID_PREVIEW,
        pstChnCtx->u32IspPipeline == 0 ? ISP_HW_PIPE_ID_ID_0 : ISP_HW_PIPE_ID_ID_1);
    if (s32Ret != SUCCESS){
        goto fail_destruct;
    }

    // s32Ret = ASR_ISP_SetFrameinfoCallback(pstChnCtx->u32IspPipeline, K1_VI_IspFrameInfoCallback);
    // if (s32Ret != SUCCESS)
    //     goto fail_destruct;

    s32Ret = ASR_ISP_EnableOfflineMode(pstChnCtx->u32IspPipeline, 1, &stOfflineAttr);
    if (s32Ret != SUCCESS){
        goto fail_destruct;
    }

    s32Ret = ASR_ISP_Init(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        goto fail_destruct;
    }

    pstChnCtx->bIspInit = MPP_TRUE;
    return K1_VI_SUCCESS;

fail_destruct:
    (void)ASR_ISP_Destruct(pstChnCtx->u32IspPipeline);
    return s32Ret;
}

S32 K1_VI_DeInitIsp(K1_VI_CHN_CTX_S *pstChnCtx)
{
    ISP_SENSOR_ATTR_S stSensorAttr;
    S32 s32Ret = 0;

    if (pstChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstChnCtx->bIspInit != MPP_TRUE){
        return K1_VI_SUCCESS;
    }

    if (pstChnCtx->bIspStreaming == MPP_TRUE) {
        s32Ret = K1_VI_StopIsp(pstChnCtx);
        if (s32Ret != K1_VI_SUCCESS){
            return s32Ret;
        }
    }

    s32Ret = ASR_ISP_DeInit(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    K1_VI_DestroyIspFrameInfoPool(pstChnCtx);

    memset(&stSensorAttr, 0, sizeof(stSensorAttr));
    stSensorAttr.u32SensorId = (U32)pstChnCtx->ViDev;
    (void)ASR_ISP_UnRegSensorCallBack(pstChnCtx->u32IspPipeline, &stSensorAttr);

    s32Ret = ASR_ISP_Destruct(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    pstChnCtx->bIspInit = MPP_FALSE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_DeInitOfflineIsp(K1_VI_CHN_CTX_S *pstChnCtx)
{
    S32 s32Ret = 0;

    if (pstChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstChnCtx->bIspInit != MPP_TRUE){
        return K1_VI_SUCCESS;
    }

    if (pstChnCtx->bIspStreaming == MPP_TRUE) {
        s32Ret = K1_VI_StopOfflineIsp(pstChnCtx);
        if (s32Ret != K1_VI_SUCCESS){
            return s32Ret;
        }
    }

    s32Ret = ASR_ISP_DeInit(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    s32Ret = ASR_ISP_Destruct(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    pstChnCtx->bIspInit = MPP_FALSE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_StartIsp(K1_VI_CHN_CTX_S *pstChnCtx)
{
    S32 s32Ret = 0;

    if (pstChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstChnCtx->bIspStreaming == MPP_TRUE){
        return K1_VI_SUCCESS;
    }

    s32Ret = ASR_ISP_Streamon(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    pstChnCtx->bIspStreaming = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_StartOfflineIsp(K1_VI_CHN_CTX_S *pstChnCtx)
{
    S32 s32Ret = 0;

    if (pstChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstChnCtx->bIspStreaming == MPP_TRUE){
        return K1_VI_SUCCESS;
    }

    s32Ret = ASR_ISP_Streamon(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    pstChnCtx->bIspStreaming = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_StopIsp(K1_VI_CHN_CTX_S *pstChnCtx)
{
    S32 s32Ret = 0;

    if (pstChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstChnCtx->bIspStreaming != MPP_TRUE){
        return K1_VI_SUCCESS;
    }

    pstChnCtx->bIspStreaming = MPP_FALSE;

    s32Ret = ASR_ISP_FlushFrameinfoBuffer(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    s32Ret = ASR_ISP_Streamoff(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_StopOfflineIsp(K1_VI_CHN_CTX_S *pstChnCtx)
{
    S32 s32Ret = 0;

    if (pstChnCtx == NULL){
        return K1_VI_ERR_INVALID_PARAM;
    }

    if (pstChnCtx->bIspStreaming != MPP_TRUE){
        return K1_VI_SUCCESS;
    }

    pstChnCtx->bIspStreaming = MPP_FALSE;

    s32Ret = ASR_ISP_FlushFrameinfoBuffer(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    s32Ret = ASR_ISP_Streamoff(pstChnCtx->u32IspPipeline);
    if (s32Ret != SUCCESS){
        return s32Ret;
    }

    return K1_VI_SUCCESS;
}
