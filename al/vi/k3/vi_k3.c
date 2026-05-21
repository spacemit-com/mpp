/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_k3.c
 * @Brief     :    K3 VI AL plugin — pure V4L2 synchronous wrapper.
 *                 No VB, no SYS, no pthread. Threads live in MPI layer.
 *------------------------------------------------------------------------------
 */

#include "vi_k3.h"
#include "vi_k3_ctx.h"

#include <errno.h>
#include <string.h>
#include <stdio.h>

K3_VI_CTX_S g_stK3ViCtx;

S32 K3_VI_Init(VOID)
{
    S32 i, j, k;

    memset(&g_stK3ViCtx, 0, sizeof(g_stK3ViCtx));

    /* All dma-buf fds default to -1 (uninitialised) */
    for (i = 0; i < VI_MAX_DEV_NUM; i++) {
        for (j = 0; j < VI_MAX_CHN_NUM; j++) {
            K3_VI_CHN_CTX_S *pChn = &g_stK3ViCtx.astChnCtx[i][j];
            pChn->s32Fd = -1;
            for (k = 0; k < K3_VI_MAX_BUF_CNT; k++) {
                pChn->as32DmaBufFd[k] = -1;
            }
        }
    }

    g_stK3ViCtx.bInit = MPP_TRUE;
    return K3_VI_SUCCESS;
}

S32 K3_VI_DeInit(VOID)
{
    S32 d, c;

    /* Warn if any channel is still enabled */
    for (d = 0; d < VI_MAX_DEV_NUM; d++) {
        for (c = 0; c < VI_MAX_CHN_NUM; c++) {
            if (g_stK3ViCtx.astChnCtx[d][c].bEnabled) {
                error("K3_VI_DeInit: dev %d chn %d still enabled\n", d, c);
            }
        }
    }

    memset(&g_stK3ViCtx, 0, sizeof(g_stK3ViCtx));
    return K3_VI_SUCCESS;
}

S32 K3_VI_SetDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr)
{
    if (ViDev < 0 || ViDev >= K3_VI_MAX_DEV_NUM || pstDevAttr == NULL)
        return K3_VI_ERR_INVALID_PARAM;
    g_stK3ViCtx.astDevCtx[ViDev].stAttr = *pstDevAttr;
    g_stK3ViCtx.astDevCtx[ViDev].bCreated = MPP_TRUE;
    return K3_VI_SUCCESS;
}

S32 K3_VI_GetDevAttr(VI_DEV ViDev, ViDevAttrS *pstDevAttr)
{
    if (ViDev < 0 || ViDev >= K3_VI_MAX_DEV_NUM || pstDevAttr == NULL)
        return K3_VI_ERR_INVALID_PARAM;
    *pstDevAttr = g_stK3ViCtx.astDevCtx[ViDev].stAttr;
    return K3_VI_SUCCESS;
}

S32 K3_VI_EnableDev(VI_DEV ViDev)
{
    if (ViDev < 0 || ViDev >= K3_VI_MAX_DEV_NUM)
        return K3_VI_ERR_INVALID_PARAM;
    g_stK3ViCtx.astDevCtx[ViDev].bEnabled = MPP_TRUE;
    return K3_VI_SUCCESS;
}

S32 K3_VI_DisableDev(VI_DEV ViDev)
{
    if (ViDev < 0 || ViDev >= K3_VI_MAX_DEV_NUM)
        return K3_VI_ERR_INVALID_PARAM;
    g_stK3ViCtx.astDevCtx[ViDev].bEnabled = MPP_FALSE;
    return K3_VI_SUCCESS;
}

S32 K3_VI_SetChnAttr(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr)
{
    if (ViDev < 0 || ViDev >= K3_VI_MAX_DEV_NUM || ViChn < 0 || ViChn >= K3_VI_MAX_CHN_NUM || pstChnAttr == NULL)
        return K3_VI_ERR_INVALID_PARAM;
    g_stK3ViCtx.astChnCtx[ViDev][ViChn].stChnAttr = *pstChnAttr;
    g_stK3ViCtx.astChnCtx[ViDev][ViChn].ViDev = ViDev;
    g_stK3ViCtx.astChnCtx[ViDev][ViChn].ViChn = ViChn;
    g_stK3ViCtx.astChnCtx[ViDev][ViChn].bCreated = MPP_TRUE;
    return K3_VI_SUCCESS;
}

S32 K3_VI_GetChnAttr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstChnAttr)
{
    if (ViDev < 0 || ViDev >= K3_VI_MAX_DEV_NUM || ViChn < 0 || ViChn >= K3_VI_MAX_CHN_NUM || pstChnAttr == NULL)
        return K3_VI_ERR_INVALID_PARAM;
    *pstChnAttr = g_stK3ViCtx.astChnCtx[ViDev][ViChn].stChnAttr;
    return K3_VI_SUCCESS;
}

S32 K3_VI_EnableChn(VI_DEV ViDev, VI_CHN ViChn)
{
    S32 s32Ret;
    ViChnAttrS stChnAttr;
    BOOL bCreated;
    K3_VI_CHN_CTX_S *pChn;
    S32 i;

    info("K3_VI_EnableChn: ViDev=%d, ViChn=%d\n", ViDev, ViChn);
    if (ViDev < 0 || ViDev >= K3_VI_MAX_DEV_NUM || ViChn < 0 || ViChn >= K3_VI_MAX_CHN_NUM)
        return K3_VI_ERR_INVALID_PARAM;

    pChn = &g_stK3ViCtx.astChnCtx[ViDev][ViChn];
    if (pChn->bEnabled == MPP_TRUE)
        return K3_VI_SUCCESS;

    /* Preserve attr / created state, but reset V4L2 / dma-buf state */
    stChnAttr = pChn->stChnAttr;
    bCreated  = pChn->bCreated;

    /* Preserve dma-buf fds (set by al_vi_set_external_buf_pool already) */
    S32 as32DmaBufFd[K3_VI_MAX_BUF_CNT];
    U32 au32PlaneSize[K3_VI_MAX_BUF_CNT][VIDEO_MAX_PLANES];
    U32 u32SavedBufCnt = pChn->u32BufCnt;
    memcpy(as32DmaBufFd,  pChn->as32DmaBufFd,  sizeof(as32DmaBufFd));
    memcpy(au32PlaneSize, pChn->au32PlaneSize, sizeof(au32PlaneSize));

    memset(pChn, 0, sizeof(*pChn));
    pChn->s32Fd     = -1;
    pChn->ViDev     = ViDev;
    pChn->ViChn     = ViChn;
    pChn->stChnAttr = stChnAttr;
    pChn->bCreated  = bCreated;

    /* Restore dma-buf info that MPI populated via set_external_buf_pool */
    pChn->u32BufCnt = u32SavedBufCnt;
    memcpy(pChn->as32DmaBufFd,  as32DmaBufFd,  sizeof(pChn->as32DmaBufFd));
    memcpy(pChn->au32PlaneSize, au32PlaneSize, sizeof(pChn->au32PlaneSize));
    /* Default unset slots back to -1 */
    for (i = (S32)u32SavedBufCnt; i < K3_VI_MAX_BUF_CNT; i++) {
        pChn->as32DmaBufFd[i] = -1;
    }

    s32Ret = K3_V4L2_Open(ViDev, ViChn, pChn);
    if (s32Ret != K3_VI_SUCCESS)
        return s32Ret;

    s32Ret = K3_V4L2_Config(ViDev, ViChn, pChn);
    if (s32Ret != K3_VI_SUCCESS)
        goto fail;

    s32Ret = K3_V4L2_Start(ViDev, ViChn, pChn);
    if (s32Ret != K3_VI_SUCCESS)
        goto fail;

    pChn->bEnabled = MPP_TRUE;
    return K3_VI_SUCCESS;

fail:
    (void)K3_V4L2_Close(ViDev, ViChn, pChn);
    /* Keep dma-buf fds so a retry of EnableChn still has them; reset only V4L2 state */
    pChn->bStreaming = MPP_FALSE;
    return s32Ret;
}

S32 K3_VI_DisableChn(VI_DEV ViDev, VI_CHN ViChn)
{
    K3_VI_CHN_CTX_S *pChn;

    if (ViDev < 0 || ViDev >= K3_VI_MAX_DEV_NUM || ViChn < 0 || ViChn >= K3_VI_MAX_CHN_NUM)
        return K3_VI_ERR_INVALID_PARAM;

    pChn = &g_stK3ViCtx.astChnCtx[ViDev][ViChn];
    if (pChn->bEnabled == MPP_TRUE) {
        (void)K3_V4L2_Stop(ViDev, ViChn, pChn);
    }
    (void)K3_V4L2_Close(ViDev, ViChn, pChn);

    /* Clear runtime V4L2 state but keep stChnAttr / bCreated for re-enable */
    pChn->bEnabled   = MPP_FALSE;
    pChn->bStreaming = MPP_FALSE;
    pChn->u32PlaneCnt = 0;
    /* Note: as32DmaBufFd / au32PlaneSize / u32BufCnt are owned by MPI;
     * MPI will re-populate via al_vi_set_external_buf_pool before next EnableChn. */
    return K3_VI_SUCCESS;
}

/*============================================================================
 * AL plugin entry points (resolved by mpi/vi/vi.c via dlsym)
 *
 * These thin wrappers translate the generic al_vi_* symbol names that
 * the upper MPI layer expects into K3-specific implementations.
 *============================================================================*/

S32 al_vi_init(VOID)
{
    return K3_VI_Init();
}

S32 al_vi_deinit(VOID)
{
    return K3_VI_DeInit();
}

S32 al_vi_set_dev_attr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr)
{
    return K3_VI_SetDevAttr(ViDev, pstDevAttr);
}

S32 al_vi_get_dev_attr(VI_DEV ViDev, ViDevAttrS *pstDevAttr)
{
    return K3_VI_GetDevAttr(ViDev, pstDevAttr);
}

S32 al_vi_enable_dev(VI_DEV ViDev)
{
    return K3_VI_EnableDev(ViDev);
}

S32 al_vi_disable_dev(VI_DEV ViDev)
{
    return K3_VI_DisableDev(ViDev);
}

S32 al_vi_set_chn_attr(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr)
{
    return K3_VI_SetChnAttr(ViDev, ViChn, pstChnAttr);
}

S32 al_vi_get_chn_attr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstChnAttr)
{
    return K3_VI_GetChnAttr(ViDev, ViChn, pstChnAttr);
}

S32 al_vi_set_chn_framerate(VI_DEV ViDev, VI_CHN ViChn, const ViFrameRateCtrlS *pstFrameRateCtrl)
{
    (void)ViDev;
    (void)ViChn;
    (void)pstFrameRateCtrl;
    return K3_VI_SUCCESS;
}

S32 al_vi_get_chn_framerate(VI_DEV ViDev, VI_CHN ViChn, ViFrameRateCtrlS *pstFrameRateCtrl)
{
    (void)ViDev;
    (void)ViChn;
    if (pstFrameRateCtrl != NULL) {
        pstFrameRateCtrl->u32InputFrameStep = 1;
        pstFrameRateCtrl->u32OutputFrameStep = 1;
    }
    return K3_VI_SUCCESS;
}

S32 al_vi_enable_chn(VI_DEV ViDev, VI_CHN ViChn)
{
    return K3_VI_EnableChn(ViDev, ViChn);
}

S32 al_vi_disable_chn(VI_DEV ViDev, VI_CHN ViChn)
{
    return K3_VI_DisableChn(ViDev, ViChn);
}

S32 al_vi_dequeue_done_buffer(VI_DEV ViDev, VI_CHN ViChn, U32 *pu32Index, S32 s32MilliSec)
{
    K3_VI_CHN_CTX_S *pChn;

    if (ViDev < 0 || ViDev >= K3_VI_MAX_DEV_NUM || ViChn < 0 || ViChn >= K3_VI_MAX_CHN_NUM)
        return K3_VI_ERR_INVALID_PARAM;
    if (pu32Index == NULL)
        return K3_VI_ERR_INVALID_PARAM;

    pChn = &g_stK3ViCtx.astChnCtx[ViDev][ViChn];
    if (!pChn->bEnabled || !pChn->bStreaming)
        return K3_VI_ERR_BAD_STATE;

    return K3_V4L2_DQBuf_Wait(ViDev, ViChn, pChn, s32MilliSec, pu32Index);
}

S32 al_vi_queue_buffer(VI_DEV ViDev, VI_CHN ViChn, U32 u32Index)
{
    K3_VI_CHN_CTX_S *pChn;

    if (ViDev < 0 || ViDev >= K3_VI_MAX_DEV_NUM || ViChn < 0 || ViChn >= K3_VI_MAX_CHN_NUM)
        return K3_VI_ERR_INVALID_PARAM;

    pChn = &g_stK3ViCtx.astChnCtx[ViDev][ViChn];
    if (!pChn->bEnabled)
        return K3_VI_ERR_BAD_STATE;

    return K3_V4L2_QBuf_DmaBuf(ViDev, ViChn, pChn, u32Index);
}

S32 al_vi_set_external_buf_pool(VI_DEV ViDev, VI_CHN ViChn,
    UL ulPoolId, U32 u32BufCnt, const UL *paulBufferId,
    const VideoFrameInfo *pastFrameInfo)
{
    K3_VI_CHN_CTX_S *pChn;
    U32 i, p;

    if (ViDev < 0 || ViDev >= K3_VI_MAX_DEV_NUM || ViChn < 0 || ViChn >= K3_VI_MAX_CHN_NUM)
        return K3_VI_ERR_INVALID_PARAM;
    if (pastFrameInfo == NULL || u32BufCnt == 0 || u32BufCnt > K3_VI_MAX_BUF_CNT)
        return K3_VI_ERR_INVALID_PARAM;

    pChn = &g_stK3ViCtx.astChnCtx[ViDev][ViChn];

    /* Extract dma-buf fds and plane sizes from VideoFrameInfo provided by MPI.
     * MPI fills stVFrame.u32Fd[0] with the dma-buf fd for the buffer,
     * and stVFrame.u32PlaneSize[p] with each plane's size. */
    pChn->u32BufCnt = u32BufCnt;
    for (i = 0; i < u32BufCnt; i++) {
        pChn->as32DmaBufFd[i] = (S32)pastFrameInfo[i].stVFrame.u32Fd[0];
        for (p = 0; p < VIDEO_MAX_PLANES; p++) {
            pChn->au32PlaneSize[i][p] = pastFrameInfo[i].stVFrame.u32PlaneSize[p];
        }
    }

    (void)ulPoolId;
    (void)paulBufferId;

    info("al_vi_set_external_buf_pool: dev=%d chn=%d bufCnt=%u fd[0]=%d\n",
         ViDev, ViChn, u32BufCnt, pChn->as32DmaBufFd[0]);
    return K3_VI_SUCCESS;
}

/**
 * @brief Query ISP frame metadata.
 *
 * K3 V4L2 captures don't expose ISP statistics (exposure / WB / gains), so
 * we just zero the struct and return success. The DQBUF timestamp /
 * sequence / bytesused are kept internally in pChn->astSlotMeta and
 * exposed to MPI via al_vi_query_dqbuf_meta() (K3-only extension).
 */
S32 al_vi_query_frame_meta(VI_DEV ViDev, VI_CHN ViChn, U32 u32FrameId, ViFrameMetaInfo *pstFrameInfo)
{
    (void)ViDev;
    (void)ViChn;
    (void)u32FrameId;

    if (pstFrameInfo == NULL)
        return K3_VI_ERR_INVALID_PARAM;

    memset(pstFrameInfo, 0, sizeof(*pstFrameInfo));
    pstFrameInfo->u32FrameId = u32FrameId;
    return K3_VI_SUCCESS;
}

/**
 * @brief K3-only extension: query DQBUF runtime metadata for a slot.
 *
 * MPI loads this via dlsym (optional symbol). Returns timestamp / sequence /
 * bytesused captured at the last DQBUF for the given slot index.
 *
 * @param u32FrameId   buffer slot index returned by al_vi_dequeue_done_buffer
 * @param pu64PtsUs    [out] V4L2 timestamp in microseconds (may be NULL)
 * @param pu32Sequence [out] V4L2 sequence number (may be NULL)
 * @param pau32BytesUsed [out] per-plane bytesused, length VIDEO_MAX_PLANES (may be NULL)
 */
S32 al_vi_query_dqbuf_meta(VI_DEV ViDev, VI_CHN ViChn, U32 u32FrameId,
                           U64 *pu64PtsUs, U32 *pu32Sequence, U32 *pau32BytesUsed)
{
    K3_VI_CHN_CTX_S *pChn;

    if (ViDev < 0 || ViDev >= K3_VI_MAX_DEV_NUM || ViChn < 0 || ViChn >= K3_VI_MAX_CHN_NUM)
        return K3_VI_ERR_INVALID_PARAM;

    pChn = &g_stK3ViCtx.astChnCtx[ViDev][ViChn];
    if (u32FrameId >= pChn->u32BufCnt)
        return K3_VI_ERR_INVALID_PARAM;

    if (pu64PtsUs != NULL)
        *pu64PtsUs = pChn->astSlotMeta[u32FrameId].u64TimestampUs;
    if (pu32Sequence != NULL)
        *pu32Sequence = pChn->astSlotMeta[u32FrameId].u32Sequence;
    if (pau32BytesUsed != NULL) {
        U32 p;
        for (p = 0; p < VIDEO_MAX_PLANES; p++)
            pau32BytesUsed[p] = pChn->astSlotMeta[u32FrameId].au32BytesUsed[p];
    }

    return K3_VI_SUCCESS;
}

/* K3 no longer provides get/release_chn_frame — MPI manages the depth queue.
 * These return NOT_SUPPORT so MPI knows to use its own drain/pop path. */
S32 al_vi_get_chn_frame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstFrameInfo, S32 s32MilliSec)
{
    (void)ViDev;
    (void)ViChn;
    (void)pstFrameInfo;
    (void)s32MilliSec;
    return K3_VI_ERR_NOT_SUPPORT;
}

S32 al_vi_release_chn_frame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstFrameInfo)
{
    (void)ViDev;
    (void)ViChn;
    (void)pstFrameInfo;
    return K3_VI_ERR_NOT_SUPPORT;
}

/* The following entry points are not used by K3 yet; they are kept as
 * no-op stubs so the upper-level dlsym() resolution still succeeds. */

S32 al_vi_trigger_raw_dump(VI_DEV ViDev, VI_CHN ViChn)
{
    (void)ViDev;
    (void)ViChn;
    return K3_VI_ERR_NOT_SUPPORT;
}

S32 al_vi_get_raw_dump_frame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec)
{
    (void)ViDev;
    (void)ViChn;
    (void)pstVideoFrame;
    (void)s32MilliSec;
    return K3_VI_ERR_NOT_SUPPORT;
}

S32 al_vi_release_raw_dump_frame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame)
{
    (void)ViDev;
    (void)ViChn;
    (void)pstVideoFrame;
    return K3_VI_ERR_NOT_SUPPORT;
}

S32 al_vi_get_rawdump_attr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstRawAttr)
{
    (void)ViDev;
    (void)ViChn;
    (void)pstRawAttr;
    return K3_VI_ERR_NOT_SUPPORT;
}

S32 al_vi_set_rawdump_buf(VI_DEV ViDev, VI_CHN ViChn,
    const VideoFrameInfo *pstFrameInfo)
{
    (void)ViDev;
    (void)ViChn;
    (void)pstFrameInfo;
    return K3_VI_ERR_NOT_SUPPORT;
}

S32 al_vi_offline_set_input_addr(VI_DEV ViDev, VI_CHN ViChn,
    UL ulPoolId, UL ulBufferId,
    const VideoFrameInfo *pstFrameInfo,
    const U8 *pu8RawVirAddr, U32 u32RawSize)
{
    (void)ViDev;
    (void)ViChn;
    (void)ulPoolId;
    (void)ulBufferId;
    (void)pstFrameInfo;
    (void)pu8RawVirAddr;
    (void)u32RawSize;
    return K3_VI_ERR_NOT_SUPPORT;
}

S32 al_vi_attach_bind_sink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode)
{
    (void)ViDev;
    (void)ViChn;
    (void)pstSinkNode;
    return K3_VI_SUCCESS;
}

S32 al_vi_detach_bind_sink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode)
{
    (void)ViDev;
    (void)ViChn;
    (void)pstSinkNode;
    return K3_VI_SUCCESS;
}
