/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_k1.c
 * @Date      :    2026-3-26
 * @Author    :    SPACEMIT
 * @Brief     :    K1 platform VI adaptation layer skeleton implementation.
 *------------------------------------------------------------------------------
 */

#include "vi_k1_defs.h"
#include "vi_k1_ctx.h"
#include "vi_k1_flow.h"
#include "vi_k1_buffer.h"
#include "vi_k1_common.h"
#include "vi_k1_sensor.h"
#include "vi_k1_isp.h"
#include "vi_k1_ccic.h"
#include "vi_k1_virtual.h"
#include "vi_k1_raw.h"

#include <stdio.h>

extern VOID K1_VI_ResetOfflineCfg(VI_DEV ViDev);

static VOID K1_VI_DumpPlaneToFile(const CHAR *pszPath, const IMAGE_BUFFER_PLANE_S *pstPlane)
{
    FILE *fp = NULL;

    if (pszPath == NULL || pstPlane == NULL || pstPlane->virAddr == NULL || pstPlane->length == 0)
        return;

    fp = fopen(pszPath, "wb");
    if (fp == NULL) {
        info("[K1_VI_CB_DUMP] failed to open %s\n", pszPath);
        return;
    }

    (void)fwrite(pstPlane->virAddr, 1, pstPlane->length, fp);
    (void)fclose(fp);
}

static VOID K1_VI_DumpPlaneArray(const CHAR *pszPrefix,
                                 U32 u32Level,
                                 const IMAGE_BUFFER_PLANE_S *pstPlanes,
                                 U32 u32PlaneCnt)
{
    CHAR szPath[256] = {0};
    U32 i = 0;

    if (pszPrefix == NULL || pstPlanes == NULL)
        return;

    for (i = 0; i < u32PlaneCnt; ++i) {
        const IMAGE_BUFFER_PLANE_S *pstPlane = &pstPlanes[i];

        if (pstPlane->virAddr == NULL || pstPlane->length == 0)
            continue;

        if (u32Level == 0)
            (void)snprintf(szPath, sizeof(szPath), "k1_vi_cb_dump_%s_p%u.bin", pszPrefix, i);
        else
            (void)snprintf(szPath, sizeof(szPath), "k1_vi_cb_dump_%s%u_p%u.bin", pszPrefix, u32Level, i);

        info("[K1_VI_CB_DUMP] dump %s: vir=%p fd=%d off=%u len=%u stride=%u scanline=%u %ux%u\n",
               szPath,
               pstPlane->virAddr,
               pstPlane->fd,
               pstPlane->offset,
               pstPlane->length,
               pstPlane->stride,
               pstPlane->scanline,
               pstPlane->width,
               pstPlane->height);
        K1_VI_DumpPlaneToFile(szPath, pstPlane);
    }
}

static VOID K1_VI_DumpFirstCallbackFrame(U32 u32AsrChn,
                                         const K1_VI_CHN_CTX_S *pstChnCtx,
                                         const VI_IMAGE_BUFFER_S *pstViBuffer,
                                         const K1_VI_BUF_NODE_S *pstBufNode)
{
    static BOOL sbDumped = MPP_FALSE;

    if (sbDumped == MPP_TRUE)
        return;
    if (pstChnCtx == NULL || pstViBuffer == NULL || pstBufNode == NULL)
        return;

    sbDumped = MPP_TRUE;

    info("[K1_VI_CB_DUMP] asrChn=%u viDev=%d viChn=%d bufIndex=%u frameId=%d fmt=%d numPlanes=%u timestamp=%llu\n",
           u32AsrChn,
           pstChnCtx->ViDev,
           pstChnCtx->ViChn,
           pstBufNode->u32Index,
           pstBufNode->stImageBuffer.frameId,
           pstBufNode->stImageBuffer.format,
           pstBufNode->stImageBuffer.numPlanes,
           (unsigned long long)pstBufNode->stImageBuffer.timeStamp);
    info("[K1_VI_CB_DUMP] image size=%ux%u m.fd=%d pool=%llu block=%llu\n",
           pstBufNode->stImageBuffer.size.width,
           pstBufNode->stImageBuffer.size.height,
           pstBufNode->stImageBuffer.m.fd,
           (unsigned long long)pstBufNode->ulPoolId,
           (unsigned long long)pstBufNode->ulBufferId);

    // K1_VI_DumpPlaneArray("main", 0, pstBufNode->stImageBuffer.planes, pstBufNode->stImageBuffer.numPlanes);
    // K1_VI_DumpPlaneArray("dwt", 1, pstBufNode->stImageBuffer.dwt1, DWT_MAX_PLANES);
    // K1_VI_DumpPlaneArray("dwt", 2, pstBufNode->stImageBuffer.dwt2, DWT_MAX_PLANES);
    // K1_VI_DumpPlaneArray("dwt", 3, pstBufNode->stImageBuffer.dwt3, DWT_MAX_PLANES);
    // K1_VI_DumpPlaneArray("dwt", 4, pstBufNode->stImageBuffer.dwt4, DWT_MAX_PLANES);
}

static BOOL K1_VI_IsBindSupported(const K1_VI_CHN_CTX_S *pstChnCtx)
{
    if (pstChnCtx == NULL)
        return MPP_FALSE;

    if (pstChnCtx->stAttr.eChnType == VI_CHN_TYPE_PHYSICAL)
        return MPP_TRUE;

    if (pstChnCtx->stAttr.eChnType == VI_CHN_TYPE_VIRTUAL)
        return MPP_TRUE;

    return MPP_FALSE;
}

static VOID K1_VI_InitFrameRateCtrl(K1_VI_CHN_CTX_S *pstChnCtx)
{
    if (pstChnCtx == NULL)
        return;

    pstChnCtx->stFrameRateCtrl.u32InputFrameStep = 1;
    pstChnCtx->stFrameRateCtrl.u32OutputFrameStep = 1;
    pstChnCtx->u32FrameRateSeq = 0;
}

static BOOL K1_VI_ShouldKeepFrame(K1_VI_CHN_CTX_S *pstChnCtx)
{
    U32 u32InputStep;
    U32 u32OutputStep;
    U32 u32CurIdx;

    if (pstChnCtx == NULL)
        return MPP_FALSE;

    u32InputStep = pstChnCtx->stFrameRateCtrl.u32InputFrameStep;
    u32OutputStep = pstChnCtx->stFrameRateCtrl.u32OutputFrameStep;

    if (u32InputStep == 0 || u32OutputStep == 0)
        return MPP_FALSE;

    if (u32OutputStep >= u32InputStep)
        return MPP_TRUE;

    u32CurIdx = pstChnCtx->u32FrameRateSeq % u32InputStep;
    pstChnCtx->u32FrameRateSeq++;

    return (u32CurIdx < u32OutputStep) ? MPP_TRUE : MPP_FALSE;
}

static S32 K1_VI_HandleBoundFrame(K1_VI_CHN_CTX_S *pstChnCtx, K1_VI_BUF_NODE_S *pstBufNode)
{
    if (pstChnCtx == NULL || pstBufNode == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    /*
     * SYS bind data forwarding hook.
     * Real sink delivery will be added by SYS/process-flow implementation.
     * Stub stage: simulate sink consumed frame immediately so buffers are
     * not leaked while bind forwarding is still unimplemented.
     */
    if (pstChnCtx->bIsVirtual == MPP_TRUE) {
        pstBufNode->enState = K1_VI_BUF_STATE_IDLE;
        return K1_VI_SUCCESS;
    }

    return K1_VI_QueueBufNode(pstChnCtx, pstBufNode);
}

static S32 K1_VI_HandleDroppedFrame(K1_VI_CHN_CTX_S *pstChnCtx, K1_VI_BUF_NODE_S *pstBufNode)
{
    if (pstChnCtx == NULL || pstBufNode == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    if (pstChnCtx->bIsVirtual == MPP_TRUE) {
        pstBufNode->enState = K1_VI_BUF_STATE_IDLE;
        return K1_VI_SUCCESS;
    }

    return K1_VI_QueueBufNode(pstChnCtx, pstBufNode);
}

S32 K1_VI_HandleNormalCallback(K1_VI_CHN_CTX_S *pstChnCtx, K1_VI_BUF_NODE_S *pstBufNode)
{
    if (pstChnCtx == NULL || pstBufNode == NULL)
        return K1_VI_ERR_INVALID_PARAM; 

    if (K1_VI_ShouldKeepFrame(pstChnCtx) != MPP_TRUE) {
        return K1_VI_HandleDroppedFrame(pstChnCtx, pstBufNode);
    }

    pstBufNode->enState = K1_VI_BUF_STATE_READY;
    return K1_VI_DonePush(pstChnCtx, pstBufNode->u32Index);
}

S32 K1_VI_StartChnCtx(VI_DEV ViDev, VI_CHN ViChn, K1_VI_CHN_CTX_S *pstChnCtx)
{
    S32 s32Ret = 0;
    ViWorkMode eWorkMode;

    if (pstChnCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    eWorkMode = g_stK1ViCtx.astDevCtx[ViDev].stAttr.eWorkMode;

    if (eWorkMode == VI_WORK_MODE_ISP_BYPASS)
        return K1_VI_StartCcicChnCtx(ViDev, ViChn, pstChnCtx);

    if (pstChnCtx->ulVbPool == 0)
        return K1_VI_ERR_INVALID_PARAM;

    if (eWorkMode != VI_WORK_MODE_OFFLINE)
        s32Ret = K1_VI_InitIsp(pstChnCtx);

    if (eWorkMode != VI_WORK_MODE_OFFLINE) {
        if (s32Ret != K1_VI_SUCCESS) {
            return s32Ret;
        }
    }

	s32Ret = ASR_VI_SetCallback(pstChnCtx->u32AsrChn, K1_VI_BufferCallback);
    if (s32Ret != SUCCESS) {
        return s32Ret;
    }

	s32Ret = ASR_VI_EnableDev(ViDev);
    if (s32Ret != 0) {
        if (eWorkMode != VI_WORK_MODE_OFFLINE)
            (void)K1_VI_DeInitIsp(pstChnCtx);
        return s32Ret;
    }

    s32Ret = ASR_VI_EnableChn(pstChnCtx->u32AsrChn);
    if (s32Ret != SUCCESS) {
        if (eWorkMode != VI_WORK_MODE_OFFLINE)
            (void)K1_VI_DeInitIsp(pstChnCtx);
        return s32Ret;
    }

	/* Queue all buffers before starting ISP/sensor so hardware has
     * buffers available from the very first frame. */
    s32Ret = K1_VI_QueueAllBuffers(pstChnCtx);
    if (s32Ret != K1_VI_SUCCESS) {
        return s32Ret;
    }

    if (eWorkMode != VI_WORK_MODE_OFFLINE)
        s32Ret = K1_VI_StartIsp(pstChnCtx);
    if (eWorkMode != VI_WORK_MODE_OFFLINE) {
        if (s32Ret != K1_VI_SUCCESS) {
            (void)K1_VI_StopSensor(ViDev);
            ASR_VI_DisableBayerDump((U32)ViDev);
            ASR_VI_DisableChn(pstChnCtx->u32AsrChn);
            ASR_VI_DisableDev((U32)ViDev);
            (void)K1_VI_DeInitIsp(pstChnCtx);
            return s32Ret;
        }
    }

    if (eWorkMode != VI_WORK_MODE_OFFLINE) {
	    s32Ret = K1_VI_StartSensor(ViDev);
	    if (s32Ret != K1_VI_SUCCESS) {
            ASR_VI_DisableChn(pstChnCtx->u32AsrChn);
            ASR_VI_DisableDev((U32)ViDev);
            (void)K1_VI_DeInitIsp(pstChnCtx);
            return s32Ret;
        }
    }

    pstChnCtx->bEnabled = MPP_TRUE;
    return K1_VI_SUCCESS;
}


S32 K1_VI_StopChnCtx(VI_DEV ViDev, K1_VI_CHN_CTX_S *pstChnCtx, BOOL bDestroyPool)
{
    S32 s32Ret = 0;
    ViWorkMode eWorkMode;

    if (pstChnCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    eWorkMode = g_stK1ViCtx.astDevCtx[ViDev].stAttr.eWorkMode;

    if (eWorkMode == VI_WORK_MODE_ISP_BYPASS)
        return K1_VI_StopCcicChnCtx(ViDev, pstChnCtx, bDestroyPool);

    if (pstChnCtx->bIsVirtual == MPP_TRUE) {
        if (bDestroyPool == MPP_TRUE)
            K1_VI_DestroyOutBufPool(pstChnCtx);
        pstChnCtx->bEnabled = MPP_FALSE;
        return K1_VI_SUCCESS;
    }

    if (pstChnCtx->bEnabled == MPP_TRUE) {
        s32Ret = ASR_VI_DisableChn(pstChnCtx->u32AsrChn);
        if (s32Ret != SUCCESS)
            return s32Ret;

        if (eWorkMode != VI_WORK_MODE_OFFLINE) {
            s32Ret = K1_VI_StopSensor(ViDev);
            if (s32Ret != K1_VI_SUCCESS)
                return s32Ret;
        }

        if (eWorkMode != VI_WORK_MODE_OFFLINE)
            (void)K1_VI_StopIsp(pstChnCtx);
    }

    if (eWorkMode != VI_WORK_MODE_OFFLINE)
        (void)K1_VI_DeInitIsp(pstChnCtx);

    if (bDestroyPool == MPP_TRUE)
        K1_VI_DestroyOutBufPool(pstChnCtx);

    pstChnCtx->bEnabled = MPP_FALSE;
    return K1_VI_SUCCESS;
}

int32_t K1_VI_BufferCallback(uint32_t nChn, VI_IMAGE_BUFFER_S *vi_buffer)
{
	//info("Received buffer callback for ASR VI channel %u\n", nChn);
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;
    K1_VI_RAW_CTX_S *pstRawCtx = NULL;
    K1_VI_BUF_NODE_S *pstBufNode = NULL;
    IMAGE_BUFFER_S *pstImageBuffer = NULL;

    if (vi_buffer == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    pstRawCtx = K1_VI_FindRawCtxByAsrChn(nChn);
    if (pstRawCtx != NULL)
	{
		error("Handling raw dump callback for ASR VI channel %u\n", nChn);
		return K1_VI_HandleRawDumpCallback(pstRawCtx, vi_buffer);
	}
        

    pstImageBuffer = vi_buffer->buffer;
    pstChnCtx = K1_VI_FindChnCtxByAsrChn(nChn);
    if (pstChnCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    pstBufNode = K1_VI_FindBufNodeByImageBuffer(pstChnCtx, pstImageBuffer);
    if (pstBufNode == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    K1_VI_UpdateBufNodeMeta(pstBufNode, vi_buffer);
    //K1_VI_DumpFirstCallbackFrame(nChn, pstChnCtx, vi_buffer, pstBufNode);

    K1_VI_DispatchVirtualFrames(pstChnCtx, vi_buffer, &pstBufNode->stFrameInfo);

    if (pstChnCtx->bSysBound == MPP_TRUE)
        return K1_VI_HandleBoundFrame(pstChnCtx, pstBufNode);

    return K1_VI_HandleNormalCallback(pstChnCtx, pstBufNode);
}

S32 K1_VI_Init(VOID)
{
    S32 s32Ret = 0;

    if (g_stK1ViCtx.bInit == MPP_TRUE)
        return K1_VI_SUCCESS;

    s32Ret = ASR_VI_Init();
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    s32Ret = K1_VI_CcicInit();
    if (s32Ret != K1_VI_SUCCESS) {
        return s32Ret;
    }

    memset(&g_stK1ViCtx, 0, sizeof(g_stK1ViCtx));
    g_stK1ViCtx.bInit = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_DeInit(VOID)
{
    S32 s32Ret = 0;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_SUCCESS;

    s32Ret = ASR_VI_Deinit();
    (void)K1_VI_CcicDeInit();
    memset(&g_stK1ViCtx, 0, sizeof(g_stK1ViCtx));
    return s32Ret;
}

S32 K1_VI_SetDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr)
{
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;
    K1_ASR_VI_DEV_ATTR_S stAsrDevAttr;
    S32 s32Ret = 0;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (pstDevAttr == NULL || K1_VI_IsValidDev(ViDev) != MPP_TRUE)	
        return K1_VI_ERR_INVALID_PARAM;
    if (K1_VI_IsValidSize(pstDevAttr->u32Width, pstDevAttr->u32Height) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstDevCtx = &g_stK1ViCtx.astDevCtx[ViDev];
    if (pstDevCtx->bEnabled == MPP_TRUE)
        return K1_VI_ERR_BUSY;

    memcpy(&pstDevCtx->stAttr, pstDevAttr, sizeof(*pstDevAttr));

    if (pstDevCtx->stAttr.eWorkMode != VI_WORK_MODE_OFFLINE) {
        s32Ret = K1_VI_TryAutoInitSensor(ViDev);
        if (s32Ret != K1_VI_SUCCESS) {
            memset(&pstDevCtx->stAttr, 0, sizeof(pstDevCtx->stAttr));
            return s32Ret;
        }
    }

    if (pstDevCtx->stAttr.eWorkMode == VI_WORK_MODE_ISP_BYPASS) {
        s32Ret = K1_VI_CcicSetDevAttr(ViDev, &pstDevCtx->stAttr);
        if (s32Ret != K1_VI_SUCCESS)
            return s32Ret;

        pstDevCtx->bCreated = MPP_TRUE;
        return K1_VI_SUCCESS;
    }

    s32Ret = K1_VI_ToAsrDevAttr(ViDev, &pstDevCtx->stAttr, &stAsrDevAttr);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    s32Ret = ASR_VI_SetDevAttr((U32)ViDev, &stAsrDevAttr);
	// info("%s: ASR_VI_SetDevAttr devId %d, workMode %d, rawType %d, %dx%d, lane_num %d, bindSensorIdx %d, ret = %d\n",
    //        __func__, ViDev, stAsrDevAttr.enWorkMode, stAsrDevAttr.enRawType, stAsrDevAttr.width, stAsrDevAttr.height,
    //        stAsrDevAttr.mipi_lane_num, stAsrDevAttr.bindSensorIdx, s32Ret);
    if (s32Ret != SUCCESS)
        return s32Ret;

    pstDevCtx->bCreated = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_GetDevAttr(VI_DEV ViDev, ViDevAttrS *pstDevAttr)
{
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;
    K1_ASR_VI_DEV_ATTR_S stAsrDevAttr;
    S32 s32Ret = 0;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (pstDevAttr == NULL || K1_VI_IsValidDev(ViDev) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstDevCtx = &g_stK1ViCtx.astDevCtx[ViDev];
    if (pstDevCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    s32Ret = ASR_VI_GetDevAttr((U32)ViDev, &stAsrDevAttr);
    if (s32Ret != SUCCESS)
        return s32Ret;

    s32Ret = K1_VI_FromAsrDevAttr(&stAsrDevAttr, pstDevAttr);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    memcpy(&pstDevCtx->stAttr, pstDevAttr, sizeof(*pstDevAttr));
    return K1_VI_SUCCESS;
}

S32 K1_VI_EnableDev(VI_DEV ViDev)
{
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstDevCtx = &g_stK1ViCtx.astDevCtx[ViDev];
    if (pstDevCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstDevCtx->bEnabled = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_DisableDev(VI_DEV ViDev)
{
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;
    U32 i = 0;
    S32 s32Ret = 0;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstDevCtx = &g_stK1ViCtx.astDevCtx[ViDev];
    if (pstDevCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    if (pstDevCtx->stAttr.eWorkMode == VI_WORK_MODE_ISP_BYPASS) {
        (void)ASR_CCIC_DisableDev((U32)ViDev);
    } else {
        s32Ret = ASR_VI_DisableDev((U32)ViDev);
        if (s32Ret != SUCCESS)
            return s32Ret;

        (void)ASR_VI_DisableBayerDump((U32)ViDev);
        (void)ASR_VI_FlushDev((U32)ViDev);
    }

    if (pstDevCtx->stAttr.eWorkMode == VI_WORK_MODE_OFFLINE)
        K1_VI_ResetOfflineCfg(ViDev);

    for (i = 0; i < VI_MAX_CHN_NUM; i++) {
        if (pstDevCtx->stAttr.eWorkMode == VI_WORK_MODE_OFFLINE)
            (void)K1_VI_DeInitOfflineIsp(&g_stK1ViCtx.astChnCtx[ViDev][i]);
        else if (pstDevCtx->stAttr.eWorkMode == VI_WORK_MODE_ONLINE)
            (void)K1_VI_DeInitIsp(&g_stK1ViCtx.astChnCtx[ViDev][i]);
        g_stK1ViCtx.astChnCtx[ViDev][i].bEnabled = MPP_FALSE;
        memset(&g_stK1ViCtx.astRawCtx[ViDev][i], 0, sizeof(g_stK1ViCtx.astRawCtx[ViDev][i]));
    }

    (void)K1_VI_DeInitSensor(ViDev);
    pstDevCtx->bEnabled = MPP_FALSE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_SetChnAttr(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr)
{
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;
    K1_VI_CHN_CTX_S *pstPhyChnCtx = NULL;
    K1_ASR_VI_CHN_ATTR_S stAsrChnAttr;
    U32 u32AsrChn = 0;
    S32 s32Ret = 0;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (pstChnAttr == NULL || K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstChnAttr->eChnType < VI_CHN_TYPE_PHYSICAL || pstChnAttr->eChnType >= VI_CHN_TYPE_MAX)
        return K1_VI_ERR_INVALID_PARAM;
    if (K1_VI_IsValidSize(pstChnAttr->u32Width, pstChnAttr->u32Height) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstChnAttr->eStrideAlign != VI_STRIDE_ALIGN_DEFAULT &&
        pstChnAttr->eStrideAlign != VI_STRIDE_ALIGN_16 &&
        pstChnAttr->eStrideAlign != VI_STRIDE_ALIGN_32 &&
        pstChnAttr->eStrideAlign != VI_STRIDE_ALIGN_64)
        return K1_VI_ERR_INVALID_PARAM;
    if (g_stK1ViCtx.astDevCtx[ViDev].bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bEnabled == MPP_TRUE)
        return K1_VI_ERR_BUSY;

    if (pstChnAttr->eChnType == VI_CHN_TYPE_VIRTUAL) {
        pstPhyChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][0];
        if (ViChn == 0)
            return K1_VI_ERR_INVALID_PARAM;
        if (pstPhyChnCtx->bCreated != MPP_TRUE || pstPhyChnCtx->stAttr.eChnType != VI_CHN_TYPE_PHYSICAL)
            return K1_VI_ERR_INVALID_PARAM;
        if (pstChnAttr->u32Width > pstPhyChnCtx->stAttr.u32Width ||
            pstChnAttr->u32Height > pstPhyChnCtx->stAttr.u32Height)
            return K1_VI_ERR_INVALID_PARAM;

        memset(pstChnCtx, 0, sizeof(*pstChnCtx));
        memcpy(&pstChnCtx->stAttr, pstChnAttr, sizeof(*pstChnAttr));
        pstChnCtx->ViDev = ViDev;
        pstChnCtx->ViChn = ViChn;
        pstChnCtx->ViSrcChn = 0;
        pstChnCtx->bIsVirtual = MPP_TRUE;
        pstChnCtx->bSysBound = MPP_FALSE;
        memset(&pstChnCtx->stBindSink, 0, sizeof(pstChnCtx->stBindSink));
        K1_VI_InitFrameRateCtrl(pstChnCtx);
        pstChnCtx->bCreated = MPP_TRUE;
        return K1_VI_SUCCESS;
    }

    if (g_stK1ViCtx.astDevCtx[ViDev].stAttr.eWorkMode == VI_WORK_MODE_ISP_BYPASS) {
        s32Ret = K1_VI_CcicSetChnAttr(ViDev, ViChn, pstChnAttr, &u32AsrChn);
        if (s32Ret != K1_VI_SUCCESS)
            return s32Ret;

        memset(pstChnCtx, 0, sizeof(*pstChnCtx));
        memcpy(&pstChnCtx->stAttr, pstChnAttr, sizeof(*pstChnAttr));
        pstChnCtx->ViDev = ViDev;
        pstChnCtx->ViChn = ViChn;
        pstChnCtx->ViSrcChn = ViChn;
        pstChnCtx->bIsVirtual = MPP_FALSE;
        pstChnCtx->bSysBound = MPP_FALSE;
        memset(&pstChnCtx->stBindSink, 0, sizeof(pstChnCtx->stBindSink));
        pstChnCtx->u32AsrChn = u32AsrChn;
        pstChnCtx->u32IspPipeline = (U32)ViDev;
        K1_VI_InitFrameRateCtrl(pstChnCtx);
        pstChnCtx->bCreated = MPP_TRUE;
        return K1_VI_SUCCESS;
    }

    s32Ret = K1_VI_ToAsrChnAttr(pstChnAttr, &stAsrChnAttr);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    s32Ret = K1_VI_GetAsrChnId(ViDev, ViChn, pstChnAttr, &u32AsrChn);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    s32Ret = ASR_VI_SetChnAttr(u32AsrChn, &stAsrChnAttr);
	// info("%s: ASR_VI_SetChnAttr chnId %d, %dx%d, pixFormat %d, ret = %d\n",
    //        __func__, u32AsrChn, stAsrChnAttr.width, stAsrChnAttr.height, stAsrChnAttr.enPixFormat, s32Ret);
    if (s32Ret != SUCCESS)
        return s32Ret;

    memcpy(&pstChnCtx->stAttr, pstChnAttr, sizeof(*pstChnAttr));
    pstChnCtx->ViDev = ViDev;
    pstChnCtx->ViChn = ViChn;
    pstChnCtx->ViSrcChn = ViChn;
    pstChnCtx->bIsVirtual = MPP_FALSE;
    pstChnCtx->bSysBound = MPP_FALSE;
    memset(&pstChnCtx->stBindSink, 0, sizeof(pstChnCtx->stBindSink));
    pstChnCtx->u32AsrChn = u32AsrChn;
    pstChnCtx->u32IspPipeline = (U32)ViDev;
    K1_VI_InitFrameRateCtrl(pstChnCtx);
    pstChnCtx->bCreated = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_SetChnFrameRate(VI_DEV ViDev, VI_CHN ViChn, const ViFrameRateCtrlS *pstFrameRateCtrl)
{
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (pstFrameRateCtrl == NULL || K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstFrameRateCtrl->u32InputFrameStep == 0 || pstFrameRateCtrl->u32OutputFrameStep == 0)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstFrameRateCtrl->u32OutputFrameStep > pstFrameRateCtrl->u32InputFrameStep)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx->stFrameRateCtrl = *pstFrameRateCtrl;
    pstChnCtx->u32FrameRateSeq = 0;
    return K1_VI_SUCCESS;
}

S32 K1_VI_GetChnFrameRate(VI_DEV ViDev, VI_CHN ViChn, ViFrameRateCtrlS *pstFrameRateCtrl)
{
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (pstFrameRateCtrl == NULL || K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    *pstFrameRateCtrl = pstChnCtx->stFrameRateCtrl;
    return K1_VI_SUCCESS;
}

S32 K1_VI_GetChnAttr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstChnAttr)
{
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;
    K1_ASR_VI_CHN_ATTR_S stAsrChnAttr;
    S32 s32Ret = 0;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (pstChnAttr == NULL || K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    if (pstChnCtx->bIsVirtual == MPP_TRUE) {
        memcpy(pstChnAttr, &pstChnCtx->stAttr, sizeof(*pstChnAttr));
        return K1_VI_SUCCESS;
    }

    s32Ret = ASR_VI_GetChnAttr(pstChnCtx->u32AsrChn, &stAsrChnAttr);
    if (s32Ret != SUCCESS)
        return s32Ret;

    s32Ret = K1_VI_FromAsrChnAttr(&stAsrChnAttr, pstChnAttr);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    memcpy(&pstChnCtx->stAttr, pstChnAttr, sizeof(*pstChnAttr));
    return K1_VI_SUCCESS;
}

S32 K1_VI_EnableChn(VI_DEV ViDev, VI_CHN ViChn)
{
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (g_stK1ViCtx.astDevCtx[ViDev].bEnabled != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    if (pstChnCtx->bIsVirtual == MPP_TRUE)
        return K1_VI_StartVirtualChnCtx(ViDev, ViChn, pstChnCtx);

    if (pstChnCtx->stAttr.eChnType != VI_CHN_TYPE_PHYSICAL)
        return K1_VI_ERR_NOT_SUPPORT;

    return K1_VI_StartChnCtx(ViDev, ViChn, pstChnCtx);
}

S32 K1_VI_SetExternalBufPool(VI_DEV ViDev, VI_CHN ViChn,
                             UL ulPoolId, U32 u32BufCnt,
                             const UL *paulBufferId,
                             const VideoFrameInfo *pastFrameInfo,
                             const IMAGE_BUFFER_S *pastImageBuffer)
{
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstChnCtx->bEnabled == MPP_TRUE)
        return K1_VI_ERR_BUSY;
    if (pstChnCtx->ulVbPool != 0)
        return K1_VI_ERR_BUSY;

    return K1_VI_ImportExternalBufPool(ViDev,
                                       ViChn,
                                       pstChnCtx,
                                       ulPoolId,
                                       u32BufCnt,
                                       paulBufferId,
                                       pastFrameInfo,
                                       pastImageBuffer);
}

S32 K1_VI_DisableChn(VI_DEV ViDev, VI_CHN ViChn)
{
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;
    K1_VI_RAW_CTX_S *pstRawCtx = NULL;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    if (K1_VI_StopChnCtx(ViDev, pstChnCtx, MPP_TRUE) != K1_VI_SUCCESS)
        return K1_VI_ERR_BUSY;

    pstRawCtx = &g_stK1ViCtx.astRawCtx[ViDev][ViChn];
    if (pstRawCtx->bCreated == MPP_TRUE) {
        if (K1_VI_StopRawCtx(pstRawCtx) != K1_VI_SUCCESS)
            return K1_VI_ERR_BUSY;

        memset(pstRawCtx, 0, sizeof(*pstRawCtx));
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_DequeueDoneBuffer(VI_DEV ViDev, VI_CHN ViChn, U32 *pu32Index, S32 s32MilliSec)
{
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;
    S32 s32Ret = 0;
    (void)s32MilliSec;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (pu32Index == NULL || K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (g_stK1ViCtx.astDevCtx[ViDev].bEnabled != MPP_TRUE || pstChnCtx->bEnabled != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstChnCtx->bSysBound == MPP_TRUE)
        return K1_VI_ERR_NOT_SUPPORT;

    s32Ret = K1_VI_DonePop(pstChnCtx, pu32Index);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;
    return K1_VI_SUCCESS;
}

S32 K1_VI_QueueBufferByIndex(VI_DEV ViDev, VI_CHN ViChn, U32 u32Index)
{
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;
    K1_VI_BUF_NODE_S *pstBufNode = NULL;
    S32 s32Ret = 0;
    ViWorkMode eWorkMode;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bEnabled != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    eWorkMode = g_stK1ViCtx.astDevCtx[ViDev].stAttr.eWorkMode;

    pstBufNode = K1_VI_GetBufNodeByIndex(pstChnCtx, u32Index);
    if (pstBufNode == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    if (pstChnCtx->bIsVirtual == MPP_TRUE) {
        pstBufNode->enState = K1_VI_BUF_STATE_IDLE;
        return K1_VI_SUCCESS;
    }

    if (eWorkMode == VI_WORK_MODE_ISP_BYPASS)
        s32Ret = K1_VI_CcicQueueBufNode(pstChnCtx, pstBufNode);
    else
        s32Ret = K1_VI_QueueBufNode(pstChnCtx, pstBufNode);

    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    return K1_VI_SUCCESS;
}

S32 al_vi_init(VOID)
{
    return K1_VI_Init();
}

S32 al_vi_deinit(VOID)
{
    return K1_VI_DeInit();
}

S32 al_vi_set_dev_attr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr)
{
    return K1_VI_SetDevAttr(ViDev, pstDevAttr);
}

S32 al_vi_get_dev_attr(VI_DEV ViDev, ViDevAttrS *pstDevAttr)
{
    return K1_VI_GetDevAttr(ViDev, pstDevAttr);
}

S32 al_vi_enable_dev(VI_DEV ViDev)
{
    return K1_VI_EnableDev(ViDev);
}

S32 al_vi_disable_dev(VI_DEV ViDev)
{
    return K1_VI_DisableDev(ViDev);
}

S32 al_vi_set_chn_attr(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr)
{
    return K1_VI_SetChnAttr(ViDev, ViChn, pstChnAttr);
}

S32 al_vi_get_chn_attr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstChnAttr)
{
    return K1_VI_GetChnAttr(ViDev, ViChn, pstChnAttr);
}

S32 al_vi_set_chn_framerate(VI_DEV ViDev, VI_CHN ViChn, const ViFrameRateCtrlS *pstFrameRateCtrl)
{
    return K1_VI_SetChnFrameRate(ViDev, ViChn, pstFrameRateCtrl);
}

S32 al_vi_get_chn_framerate(VI_DEV ViDev, VI_CHN ViChn, ViFrameRateCtrlS *pstFrameRateCtrl)
{
    return K1_VI_GetChnFrameRate(ViDev, ViChn, pstFrameRateCtrl);
}

S32 al_vi_enable_chn(VI_DEV ViDev, VI_CHN ViChn)
{
    return K1_VI_EnableChn(ViDev, ViChn);
}

S32 al_vi_disable_chn(VI_DEV ViDev, VI_CHN ViChn)
{
    return K1_VI_DisableChn(ViDev, ViChn);
}

S32 al_vi_set_external_buf_pool(VI_DEV ViDev, VI_CHN ViChn,
                                UL ulPoolId, U32 u32BufCnt,
                                const UL *paulBufferId,
                                const VideoFrameInfo *pastFrameInfo,
                                const IMAGE_BUFFER_S *pastImageBuffer)
{
    return K1_VI_SetExternalBufPool(ViDev,
                                    ViChn,
                                    ulPoolId,
                                    u32BufCnt,
                                    paulBufferId,
                                    pastFrameInfo,
                                    pastImageBuffer);
}

S32 al_vi_set_rawdump_buf(VI_DEV ViDev, VI_CHN ViChn,
                          const VideoFrameInfo *pstFrameInfo,
                          const IMAGE_BUFFER_S *pstImageBuffer)
{
    K1_VI_RAW_CTX_S *pstRawCtx = NULL;
    K1_VI_CHN_CTX_S *pstPhyChnCtx = NULL;
    S32 s32Ret = 0;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (pstFrameInfo == NULL || pstImageBuffer == NULL ||
        K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstPhyChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstPhyChnCtx->bCreated != MPP_TRUE || pstPhyChnCtx->stAttr.eChnType != VI_CHN_TYPE_PHYSICAL)
        return K1_VI_ERR_INVALID_PARAM;

    pstRawCtx = K1_VI_GetRawCtx(ViDev, ViChn);
    if (pstRawCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    if (pstRawCtx->bCreated != MPP_TRUE) {
        s32Ret = K1_VI_InitRawDumpCtx(ViDev, ViChn, pstRawCtx, pstPhyChnCtx);
        if (s32Ret != K1_VI_SUCCESS)
            return s32Ret;
    }

    return K1_VI_ImportRawDumpBuffer(ViDev, ViChn, pstRawCtx, pstFrameInfo, pstImageBuffer);
}

S32 al_vi_get_rawdump_attr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstRawAttr)
{
    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;

    return K1_VI_GetRawDumpAttr(ViDev, ViChn, pstRawAttr);
}

S32 al_vi_dequeue_done_buffer(VI_DEV ViDev, VI_CHN ViChn, U32 *pu32Index, S32 s32MilliSec)
{
    return K1_VI_DequeueDoneBuffer(ViDev, ViChn, pu32Index, s32MilliSec);
}

S32 al_vi_queue_buffer(VI_DEV ViDev, VI_CHN ViChn, U32 u32Index)
{
    return K1_VI_QueueBufferByIndex(ViDev, ViChn, u32Index);
}

S32 al_vi_trigger_raw_dump(VI_DEV ViDev, VI_CHN ViChn)
{
    return K1_VI_TriggerRawDump(ViDev, ViChn);
}

S32 al_vi_get_raw_dump_frame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec)
{
    return K1_VI_GetRawDumpFrame(ViDev, ViChn, pstVideoFrame, s32MilliSec);
}

S32 al_vi_release_raw_dump_frame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame)
{
    return K1_VI_ReleaseRawDumpFrame(ViDev, ViChn, pstVideoFrame);
}

S32 al_vi_offline_set_input_addr(VI_DEV ViDev,
                             VI_CHN ViChn,
                             UL ulPoolId,
                             UL ulBufferId,
                             const VideoFrameInfo *pstFrameInfo,
                             const IMAGE_BUFFER_S *pstImageBuffer,
                             const U8 *pu8RawVirAddr,
                             U32 u32RawSize)
{
    return K1_VI_OfflineSetInputAddr(ViDev, ViChn, ulPoolId, ulBufferId, pstFrameInfo,
                                     pstImageBuffer, pu8RawVirAddr, u32RawSize);
}

S32 al_vi_attach_bind_sink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode)
{
    return K1_VI_AttachBindSink(ViDev, ViChn, pstSinkNode);
}

S32 al_vi_detach_bind_sink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode)
{
    return K1_VI_DetachBindSink(ViDev, ViChn, pstSinkNode);
}

S32 K1_VI_AttachBindSink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode)
{
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (pstSinkNode == NULL || K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (K1_VI_IsBindSupported(pstChnCtx) != MPP_TRUE)
        return K1_VI_ERR_NOT_SUPPORT;
    if (pstChnCtx->bSysBound == MPP_TRUE)
        return K1_VI_ERR_BUSY;

    memcpy(&pstChnCtx->stBindSink, pstSinkNode, sizeof(*pstSinkNode));
    pstChnCtx->bSysBound = MPP_TRUE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_DetachBindSink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode)
{
    K1_VI_CHN_CTX_S *pstChnCtx = NULL;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (pstSinkNode == NULL || K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];
    if (pstChnCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstChnCtx->bSysBound != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (memcmp(&pstChnCtx->stBindSink, pstSinkNode, sizeof(*pstSinkNode)) != 0)
        return K1_VI_ERR_INVALID_PARAM;

    memset(&pstChnCtx->stBindSink, 0, sizeof(pstChnCtx->stBindSink));
    pstChnCtx->bSysBound = MPP_FALSE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_GetRawDumpFrame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec)
{
    K1_VI_RAW_CTX_S *pstRawCtx = NULL;
    (void)s32MilliSec;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (pstVideoFrame == NULL || K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstRawCtx = K1_VI_GetOrCreateRawDumpCtx(ViDev, ViChn);
    if (pstRawCtx == NULL || pstRawCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    if (g_stK1ViCtx.astDevCtx[ViDev].bEnabled != MPP_TRUE || pstRawCtx->bEnabled != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstRawCtx->bFrameValid != MPP_TRUE || pstRawCtx->enState != K1_VI_BUF_STATE_READY)
        return K1_VI_ERR_BUSY;

    *pstVideoFrame = pstRawCtx->stFrameInfo;
    pstRawCtx->enState = K1_VI_BUF_STATE_USER;
    return K1_VI_SUCCESS;
}

S32 K1_VI_TriggerRawDump(VI_DEV ViDev, VI_CHN ViChn)
{
    K1_VI_CHN_CTX_S *pstPhyChnCtx = NULL;
    K1_VI_RAW_CTX_S *pstRawCtx = NULL;
    S32 s32Ret = 0;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstRawCtx = K1_VI_GetOrCreateRawDumpCtx(ViDev, ViChn);
    if (pstRawCtx == NULL)
        return K1_VI_ERR_INVALID_PARAM;
    if (g_stK1ViCtx.astDevCtx[ViDev].bEnabled != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstRawCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstPhyChnCtx = &g_stK1ViCtx.astChnCtx[ViDev][ViChn];

    if (pstRawCtx->bEnabled != MPP_TRUE) {
        if (pstPhyChnCtx->bEnabled != MPP_TRUE)
            return K1_VI_ERR_INVALID_PARAM;

        s32Ret = K1_VI_StartRawCtx(pstRawCtx);
        if (s32Ret != K1_VI_SUCCESS)
            return s32Ret;
    }

    if (pstRawCtx->bTriggered == MPP_TRUE || pstRawCtx->enState == K1_VI_BUF_STATE_IN_HW ||
        pstRawCtx->enState == K1_VI_BUF_STATE_USER)
        return K1_VI_ERR_BUSY;

    s32Ret = K1_VI_QueueRawBuffer(pstRawCtx);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    pstRawCtx->bTriggered = MPP_TRUE;
    pstRawCtx->bFrameValid = MPP_FALSE;

    return K1_VI_SUCCESS;
}

S32 K1_VI_ReleaseRawDumpFrame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame)
{
    K1_VI_RAW_CTX_S *pstRawCtx = NULL;

    if (g_stK1ViCtx.bInit != MPP_TRUE)
        return K1_VI_ERR_NOT_INIT;
    if (pstVideoFrame == NULL || K1_VI_IsValidDev(ViDev) != MPP_TRUE || K1_VI_IsValidChn(ViChn) != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;

    pstRawCtx = K1_VI_GetRawCtx(ViDev, ViChn);
    if (pstRawCtx == NULL || pstRawCtx->bCreated != MPP_TRUE)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstRawCtx->enState != K1_VI_BUF_STATE_USER)
        return K1_VI_ERR_INVALID_PARAM;
    if (pstVideoFrame->ulBufferId != pstRawCtx->stFrameInfo.ulBufferId)
        return K1_VI_ERR_INVALID_PARAM;

    pstRawCtx->bFrameValid = MPP_FALSE;
    pstRawCtx->enState = K1_VI_BUF_STATE_IDLE;
    return K1_VI_SUCCESS;
}

