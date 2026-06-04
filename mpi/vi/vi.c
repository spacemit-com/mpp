#include "vi_api.h"
#include "vi_al_ops.h"

#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>

#include "log.h"
#include "module.h"
#include "vi_buf_mgr.h"
#include "sys/sys_api.h"
#include "sys/vb_api.h"

#define VI_MPI_MAX_BUF_CNT 8
#define VI_MPI_MAX_DEPTH   8
#define MPI_VI_ERR_INVALID_PARAM (-1)
#define MPI_VI_ERR_BUSY (-4)
#define MPI_VI_ERR_NOT_SUPPORT (-3)

/* ============================================================
 * Internal structures
 * ============================================================ */

typedef struct {
    VI_DEV ViDev;
    VI_CHN ViChn;
} MpiViTaskArg;

typedef struct _MpiViDepthEntry {
    UL ulBufferId;
    VideoFrameInfo stFrameInfo;
} MpiViDepthEntry;

typedef struct _MpiViChnBufCtx {
    BOOL bEnabled;
    UL ulPoolId;
    U32 u32BufCnt;
    ViChnAttrS stChnAttr;

    /* Buffer metadata table — indexed by V4L2 slot */
    VideoFrameInfo stFrameTemplate;
    VideoFrameInfo astFrameInfo[VI_MPI_MAX_BUF_CNT];
    UL aulBufferId[VI_MPI_MAX_BUF_CNT];

    /* Push / recycle threads */
    VI_DEV ViDev;
    VI_CHN ViChn;
    volatile BOOL bTaskRun;
    pthread_t hPushTid;
    pthread_t hRecycleTid;
    U32 u32BadSlotMask;  /* bitmask of slots where QBUF has persistently failed */

    /* Depth queue (pull mode, used only when u32Depth > 0) */
    pthread_mutex_t depthLock;
    pthread_cond_t  depthNotEmpty;
    U32 u32DepthHead;
    U32 u32DepthTail;
    U32 u32DepthCount;
    MpiViDepthEntry astDepthQueue[VI_MPI_MAX_DEPTH];
} MpiViChnBufCtx;

typedef struct _MpiViOfflineInputCtx {
    BOOL bEnabled;
    UL ulPoolId;
    UL ulBufferId;
    VideoFrameInfo stFrameInfo;
} MpiViOfflineInputCtx;

typedef struct _MpiViRawDumpCtx {
    BOOL bConfigured;
    UL ulPoolId;
    UL ulBufferId;
    VideoFrameInfo stFrameInfo;
} MpiViRawDumpCtx;

static MpiViChnBufCtx g_astViBufCtx[VI_MAX_DEV_NUM][VI_MAX_CHN_NUM];
static MpiViOfflineInputCtx g_astViOfflineInputCtx[VI_MAX_DEV_NUM][VI_MAX_CHN_NUM];
static MpiViRawDumpCtx g_astViRawDumpCtx[VI_MAX_DEV_NUM][VI_MAX_CHN_NUM];

static BOOL mpi_vi_is_valid_dev_chn(VI_DEV ViDev, VI_CHN ViChn) {
    return (ViDev >= 0 && ViDev < VI_MAX_DEV_NUM && ViChn >= 0 && ViChn < VI_MAX_CHN_NUM) ? MPP_TRUE : MPP_FALSE;
}

#define MODULE_TAG "mpp_vi"

static MppModule *g_pViModule = NULL;
static const ViAlOps *g_pViOps = NULL;

static VOID mpi_vi_destroy_chn_buf_ctx(VI_DEV ViDev, VI_CHN ViChn);

/* ============================================================
 * Push thread: DQBUF → SYS_SendFrame (+ depth queue if depth>0)
 * ============================================================ */

static void *mpi_vi_push_task(void *arg) {
    MpiViTaskArg *pArg = (MpiViTaskArg *)arg;
    VI_DEV ViDev = pArg->ViDev;
    VI_CHN ViChn = pArg->ViChn;
    free(pArg);

    MpiViChnBufCtx *pstBufCtx = &g_astViBufCtx[ViDev][ViChn];

    MppNode stSrcNode;
    stSrcNode.eModId   = MPP_ID_VI;
    stSrcNode.s32DevId = ViDev;
    stSrcNode.s32ChnId = ViChn;

    info("vi push task started: dev=%d chn=%d depth=%u", ViDev, ViChn, pstBufCtx->stChnAttr.u32Depth);

    while (pstBufCtx->bTaskRun) {
        U32 u32Index = 0;
        S32 s32Ret = g_pViOps->dequeue_done_buffer(ViDev, ViChn, &u32Index, 100);
        if (s32Ret != MPP_OK)
            continue;
        if (u32Index >= pstBufCtx->u32BufCnt) {
            error("vi push task: bad buf index %u", u32Index);
            continue;
        }

        UL ulBuf = pstBufCtx->aulBufferId[u32Index];

        /* Build frame info: copy template, fill PTS/sequence from DQBUF metadata,
         * convert frame type so VENC can accept it directly. */
        VideoFrameInfo stFrame = pstBufCtx->astFrameInfo[u32Index];

        if (g_pViOps->query_dqbuf_meta != NULL) {
            U64 u64Pts = 0;
            U32 u32Seq = 0;
            U32 au32BytesUsed[VIDEO_MAX_PLANES] = {0};
            if (g_pViOps->query_dqbuf_meta(ViDev, ViChn, u32Index, &u64Pts, &u32Seq, au32BytesUsed) == MPP_OK) {
                stFrame.stVFrame.u64PTS = u64Pts;
                stFrame.stVFrame.u32PlaneSizeValid[0] = au32BytesUsed[0];
                if (stFrame.stVFrame.u32PlaneNum > 1)
                    stFrame.stVFrame.u32PlaneSizeValid[1] = au32BytesUsed[1];
            }
        }

        /* Convert to VENC frame type so VENC's frame input thread can handle it.
         * stViFrameInfo and stVencFrameInfo share the same union storage; save
         * stCommFrameInfo to a local copy before reinterpreting the union to
         * avoid a self-assignment that is fragile against layout changes. */
        CommonFrameInfo stCommInfo = stFrame.stViFrameInfo.stCommFrameInfo;
        stFrame.eFrameType = FRAME_TYPE_VENC;
        stFrame.eModId     = MPP_ID_VENC;
        stFrame.stVencFrameInfo.stCommFrameInfo = stCommInfo;

        VB_UpdateBufferFrameInfo(ulBuf, &stFrame);

        /* 1. Push to all SYS-bound sinks (no-op / NOT_FOUND if no bind) */
        (void)SYS_SendFrame(&stSrcNode, ulBuf);

        /* 2. If depth > 0, also push into the depth queue for VI_GetChnFrame */
        U32 u32Depth = pstBufCtx->stChnAttr.u32Depth;
        if (u32Depth > 0) {
            VB_RefAdd(ulBuf);

            pthread_mutex_lock(&pstBufCtx->depthLock);

            if (pstBufCtx->u32DepthCount >= u32Depth) {
                /* Queue full — evict oldest entry */
                MpiViDepthEntry *pOld = &pstBufCtx->astDepthQueue[pstBufCtx->u32DepthHead];
                VB_ReleaseBuffer(pOld->ulBufferId);
                pstBufCtx->u32DepthHead = (pstBufCtx->u32DepthHead + 1) % VI_MPI_MAX_DEPTH;
                pstBufCtx->u32DepthCount--;
            }

            MpiViDepthEntry *pNew = &pstBufCtx->astDepthQueue[pstBufCtx->u32DepthTail];
            pNew->ulBufferId  = ulBuf;
            pNew->stFrameInfo = stFrame;
            pstBufCtx->u32DepthTail = (pstBufCtx->u32DepthTail + 1) % VI_MPI_MAX_DEPTH;
            pstBufCtx->u32DepthCount++;

            pthread_cond_signal(&pstBufCtx->depthNotEmpty);
            pthread_mutex_unlock(&pstBufCtx->depthLock);
        }

        /* 3. Release the V4L2 base ref — recycle thread will QBUF when all refs drop */
        VB_ReleaseBuffer(ulBuf);
    }

    info("vi push task exiting: dev=%d chn=%d", ViDev, ViChn);
    return NULL;
}

/* ============================================================
 * Recycle thread: VB_GetBuffer → QBUF back to V4L2
 * Mirrors uvc_recycle_task (mpi/uvc/uvc.c:547-588).
 * ============================================================ */

static void *mpi_vi_recycle_task(void *arg) {
    MpiViTaskArg *pArg = (MpiViTaskArg *)arg;
    VI_DEV ViDev = pArg->ViDev;
    VI_CHN ViChn = pArg->ViChn;
    free(pArg);

    MpiViChnBufCtx *pstBufCtx = &g_astViBufCtx[ViDev][ViChn];

    info("vi recycle task started: dev=%d chn=%d pool=%lu", ViDev, ViChn, pstBufCtx->ulPoolId);

    while (pstBufCtx->bTaskRun) {
        /* Block until some consumer releases a buffer back to the pool */
        UL ulBuf = VB_GetBuffer(pstBufCtx->ulPoolId, 100);
        if (ulBuf == 0 || ulBuf == (UL)-1)
            continue;

        /* Re-check after the potentially long wait: shutdown may have started.
         * Do NOT call VB_ReleaseBuffer here — keep ref=1 so that
         * MPI_VI_DestroyOutBufPool can release every acquired buffer in one
         * consistent pass during channel teardown. */
        if (!pstBufCtx->bTaskRun)
            break;

        /* Find which V4L2 slot this VB handle belongs to */
        U32 slot = (U32)-1;
        for (U32 i = 0; i < pstBufCtx->u32BufCnt; i++) {
            if (pstBufCtx->aulBufferId[i] == ulBuf) {
                slot = i;
                break;
            }
        }

        if (slot == (U32)-1) {
            error("vi recycle task: unknown VB handle %lu", ulBuf);
            VB_ReleaseBuffer(ulBuf);
            continue;
        }

        /* If this slot previously failed QBUF, skip re-queuing it to V4L2.
         * Keep ref=1 (acquired via VB_GetBuffer above) — MPI_VI_DestroyOutBufPool
         * will release it during channel teardown.  Do not release here, as that
         * would return the slot to the free pool and cause it to be re-acquired
         * in the next VB_GetBuffer call, creating a tight loop. */
        if (pstBufCtx->u32BadSlotMask & (1u << slot))
            continue;

        /* QBUF back to V4L2; ref=1 from VB_GetBuffer represents "V4L2 owns it". */
        pstBufCtx->aulBufferId[slot] = ulBuf;
        if (g_pViOps->queue_buffer(ViDev, ViChn, slot) != MPP_OK) {
            /* Log only on the first failure per slot so a systematic driver
             * misconfiguration is visible, while teardown EINVAL noise is not
             * repeated.  Keep ref=1 — MPI_VI_DestroyOutBufPool releases it. */
            if (!(pstBufCtx->u32BadSlotMask & (1u << slot))) {
                error("vi recycle: slot %u (dev=%d chn=%d) QBUF failed; "
                    "marking bad, capture continues on remaining slots",
                    slot, ViDev, ViChn);
                pstBufCtx->u32BadSlotMask |= (1u << slot);
            }
        }
    }

    info("vi recycle task exiting: dev=%d chn=%d", ViDev, ViChn);
    return NULL;
}

/* ============================================================
 * Rawdump helpers (unchanged)
 * ============================================================ */

static VOID mpi_vi_destroy_rawdump_ctx(VI_DEV ViDev, VI_CHN ViChn) {
    MpiViRawDumpCtx *pstRawCtx = NULL;
    UL aulBufferId[1];

    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return;

    pstRawCtx = &g_astViRawDumpCtx[ViDev][ViChn];
    aulBufferId[0] = pstRawCtx->ulBufferId;
    if (pstRawCtx->ulPoolId != 0)
        MPI_VI_DestroyOutBufPool(pstRawCtx->ulPoolId, 1, aulBufferId);
    memset(pstRawCtx, 0, sizeof(*pstRawCtx));
}

static S32 mpi_vi_prepare_rawdump_ctx(VI_DEV ViDev, VI_CHN ViChn) {
    MpiViRawDumpCtx *pstRawCtx = NULL;
    ViChnAttrS stChnAttr;
    VideoFrameInfo stFrameTemplate;
    VbPoolCfg stPoolCfg;
    S32 s32Ret = 0;

    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    pstRawCtx = &g_astViRawDumpCtx[ViDev][ViChn];
    if (pstRawCtx->bConfigured == MPP_TRUE)
        return MPP_OK;

    memset(&stChnAttr, 0, sizeof(stChnAttr));
    memset(&stFrameTemplate, 0, sizeof(stFrameTemplate));
    memset(&stPoolCfg, 0, sizeof(stPoolCfg));
    s32Ret = g_pViOps->get_rawdump_attr(ViDev, ViChn, &stChnAttr);
    if (s32Ret != MPP_OK) {
        error("rawdump get attr failed, dev=%d chn=%d ret=%d", ViDev, ViChn, s32Ret);
        return s32Ret;
    }

    s32Ret = MPI_VI_CalcRawDumpFrameInfo(&stChnAttr, &stFrameTemplate);
    if (s32Ret != MPP_OK) {
        error(
            "rawdump calc frame failed, dev=%d chn=%d fmt=%d w=%u h=%u ret=%d",
            ViDev,
            ViChn,
            stChnAttr.ePixelFormat,
            stChnAttr.u32Width,
            stChnAttr.u32Height,
            s32Ret);
        return s32Ret;
    }

    info(
        "rawdump alloc, dev=%d chn=%d fmt=%d w=%u h=%u total=%u stride0=%u",
        ViDev,
        ViChn,
        stChnAttr.ePixelFormat,
        stFrameTemplate.stViFrameInfo.stCommFrameInfo.u32Width,
        stFrameTemplate.stViFrameInfo.stCommFrameInfo.u32Height,
        stFrameTemplate.stVFrame.u32TotalSize,
        stFrameTemplate.stVFrame.u32PlaneStride[0]);

    stPoolCfg.u32BufCnt = 1;
    stPoolCfg.u32BufSize = stFrameTemplate.stVFrame.u32TotalSize;
    stPoolCfg.eModId = MPP_ID_VI;
    stPoolCfg.eRemapMode = VBUF_REMAP_MODE_NOCACHE;

    pstRawCtx->ulPoolId = VB_CreatePool(&stPoolCfg);
    if (pstRawCtx->ulPoolId == 0 || pstRawCtx->ulPoolId == (UL)-1) {
        error(
            "rawdump VB_CreatePool failed, dev=%d chn=%d size=%u cnt=%u",
            ViDev,
            ViChn,
            stPoolCfg.u32BufSize,
            stPoolCfg.u32BufCnt);
        return MPI_VI_ERR_BUSY;
    }

    s32Ret = VB_SetFrameInfo(pstRawCtx->ulPoolId, &stFrameTemplate);
    if (s32Ret != MPP_OK) {
        error(
            "rawdump VB_SetFrameInfo failed, dev=%d chn=%d pool=%lu ret=%d", ViDev, ViChn, pstRawCtx->ulPoolId, s32Ret);
        mpi_vi_destroy_rawdump_ctx(ViDev, ViChn);
        return s32Ret;
    }

    pstRawCtx->ulBufferId = VB_GetBuffer(pstRawCtx->ulPoolId, 0);
    if (pstRawCtx->ulBufferId == 0 || pstRawCtx->ulBufferId == (UL)-1) {
        error("rawdump VB_GetBuffer failed, dev=%d chn=%d pool=%lu", ViDev, ViChn, pstRawCtx->ulPoolId);
        mpi_vi_destroy_rawdump_ctx(ViDev, ViChn);
        return MPI_VI_ERR_BUSY;
    }

    s32Ret = VB_GetFrameInfo(pstRawCtx->ulBufferId, &pstRawCtx->stFrameInfo);
    if (s32Ret != MPP_OK) {
        error(
            "rawdump VB_GetFrameInfo failed, dev=%d chn=%d buf=%lu ret=%d",
            ViDev, ViChn, pstRawCtx->ulBufferId, s32Ret);
        mpi_vi_destroy_rawdump_ctx(ViDev, ViChn);
        return s32Ret;
    }

    pstRawCtx->stFrameInfo.eFrameType = FRAME_TYPE_VI;
    pstRawCtx->stFrameInfo.eModId = MPP_ID_VI;
    pstRawCtx->stFrameInfo.u32Idx = 0;
    pstRawCtx->stFrameInfo.ulPoolId = pstRawCtx->ulPoolId;
    pstRawCtx->stFrameInfo.ulBufferId = pstRawCtx->ulBufferId;
    pstRawCtx->stFrameInfo.stVFrame.u32PrivateData = ((U32)ViDev << 16) | (U32)ViChn;

    {
        S32 s32Fd = -1;
        U32 j;
        (void)VB_GetVirAddr(pstRawCtx->ulBufferId, (void **)&pstRawCtx->stFrameInfo.stVFrame.ulPlaneVirAddr[0]);
        if (VB_GetDmaBufFd(pstRawCtx->ulBufferId, &s32Fd) == MPP_OK) {
            for (j = 0; j < pstRawCtx->stFrameInfo.stVFrame.u32PlaneNum && j < FRAME_MAX_PLANE; j++)
                pstRawCtx->stFrameInfo.stVFrame.u32Fd[j] = (UL)s32Fd;
        }
    }

    s32Ret = g_pViOps->set_rawdump_buf(ViDev, ViChn, &pstRawCtx->stFrameInfo);
    if (s32Ret != MPP_OK) {
        error("rawdump import buffer to al failed, dev=%d chn=%d ret=%d", ViDev, ViChn, s32Ret);
        mpi_vi_destroy_rawdump_ctx(ViDev, ViChn);
        return s32Ret;
    }

    pstRawCtx->bConfigured = MPP_TRUE;
    return MPP_OK;
}

/* ============================================================
 * Channel buffer context helpers
 * ============================================================ */

static VOID mpi_vi_reset_chn_buf_ctx(VI_DEV ViDev, VI_CHN ViChn) {
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return;
    memset(&g_astViBufCtx[ViDev][ViChn], 0, sizeof(g_astViBufCtx[ViDev][ViChn]));
}

static VOID mpi_vi_destroy_chn_buf_ctx(VI_DEV ViDev, VI_CHN ViChn) {
    MpiViChnBufCtx *pstBufCtx = NULL;

    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return;

    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];
    if (pstBufCtx->ulPoolId != 0)
        MPI_VI_DestroyOutBufPool(pstBufCtx->ulPoolId, pstBufCtx->u32BufCnt, pstBufCtx->aulBufferId);

    mpi_vi_reset_chn_buf_ctx(ViDev, ViChn);
}

static VOID mpi_vi_destroy_offline_input_ctx(VI_DEV ViDev, VI_CHN ViChn) {
    MpiViOfflineInputCtx *pstInputCtx = NULL;
    UL aulBufferId[1] = {0};

    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return;

    pstInputCtx = &g_astViOfflineInputCtx[ViDev][ViChn];
    if (pstInputCtx->ulPoolId != 0) {
        aulBufferId[0] = pstInputCtx->ulBufferId;
        MPI_VI_DestroyOutBufPool(pstInputCtx->ulPoolId, 1, aulBufferId);
    }

    memset(pstInputCtx, 0, sizeof(*pstInputCtx));
}

static S32 mpi_vi_rebuild_offline_input_ctx(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr) {
    MpiViOfflineInputCtx *pstInputCtx = NULL;
    VideoFrameInfo stFrameTemplate;
    S32 s32Ret;

    if (pstChnAttr == NULL)
        return MPP_NULL_POINTER;
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    pstInputCtx = &g_astViOfflineInputCtx[ViDev][ViChn];
    if (pstInputCtx->bEnabled == MPP_TRUE)
        return MPI_VI_ERR_BUSY;

    if (pstInputCtx->ulPoolId != 0)
        mpi_vi_destroy_offline_input_ctx(ViDev, ViChn);

    s32Ret = MPI_VI_CreateOutBufPool(
        ViDev, ViChn, pstChnAttr, 1,
        &pstInputCtx->ulPoolId, &stFrameTemplate,
        &pstInputCtx->stFrameInfo, &pstInputCtx->ulBufferId);
    if (s32Ret != MPP_OK)
        return s32Ret;

    pstInputCtx->bEnabled = MPP_TRUE;
    return MPP_OK;
}

static S32 mpi_vi_rebuild_chn_buf_ctx(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr) {
    MpiViChnBufCtx *pstBufCtx = NULL;
    U32 u32BufCnt = 4;
    S32 s32Ret;

    if (pstChnAttr == NULL)
        return MPP_NULL_POINTER;
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;
    if (pstChnAttr->eChnType != VI_CHN_TYPE_PHYSICAL && pstChnAttr->eChnType != VI_CHN_TYPE_VIRTUAL)
        return MPI_VI_ERR_INVALID_PARAM;

    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];
    if (pstBufCtx->bEnabled == MPP_TRUE)
        return MPI_VI_ERR_BUSY;

    if (pstBufCtx->ulPoolId != 0)
        mpi_vi_destroy_chn_buf_ctx(ViDev, ViChn);

    s32Ret = MPI_VI_CreateOutBufPool(
        ViDev, ViChn, pstChnAttr, u32BufCnt,
        &pstBufCtx->ulPoolId, &pstBufCtx->stFrameTemplate,
        pstBufCtx->astFrameInfo, pstBufCtx->aulBufferId);
    if (s32Ret != MPP_OK)
        return s32Ret;

    pstBufCtx->u32BufCnt = u32BufCnt;
    pstBufCtx->stChnAttr = *pstChnAttr;
    return MPP_OK;
}

/* ============================================================
 * Plugin loader
 * ============================================================ */

static S32 vi_load_plugin(VOID) {
    PFN_al_vi_get_ops pfn_get_ops;
    void *handle;

    if (g_pViOps != NULL)
        return MPP_OK;

    g_pViModule = module_init(VI_K3_CAM);
    if (g_pViModule == NULL)
        g_pViModule = module_init(VI_K1_CAM);
    if (g_pViModule == NULL) {
        error("module_init failed for all VI plugins");
        return MPP_INIT_FAILED;
    }

    handle = module_get_so_path(g_pViModule);
    if (handle == NULL) {
        error("module handle is NULL after module_init");
        module_destory(g_pViModule);
        g_pViModule = NULL;
        return MPP_INIT_FAILED;
    }

    dlerror();
    pfn_get_ops = (PFN_al_vi_get_ops)dlsym(handle, "al_vi_get_ops");
    if (pfn_get_ops == NULL) {
        error("al_vi_get_ops symbol not found in VI plugin");
        module_destory(g_pViModule);
        g_pViModule = NULL;
        return MPP_INIT_FAILED;
    }

    g_pViOps = pfn_get_ops();
    if (g_pViOps == NULL) {
        error("al_vi_get_ops returned NULL");
        module_destory(g_pViModule);
        g_pViModule = NULL;
        return MPP_INIT_FAILED;
    }

    if (g_pViOps->init == NULL || g_pViOps->deinit == NULL || g_pViOps->set_dev_attr == NULL ||
        g_pViOps->get_dev_attr == NULL || g_pViOps->enable_dev == NULL || g_pViOps->disable_dev == NULL ||
        g_pViOps->set_chn_attr == NULL || g_pViOps->get_chn_attr == NULL || g_pViOps->set_chn_framerate == NULL ||
        g_pViOps->get_chn_framerate == NULL || g_pViOps->enable_chn == NULL || g_pViOps->disable_chn == NULL ||
        g_pViOps->dequeue_done_buffer == NULL || g_pViOps->queue_buffer == NULL || g_pViOps->attach_bind_sink == NULL ||
        g_pViOps->detach_bind_sink == NULL || g_pViOps->set_external_buf_pool == NULL) {
        error("required VI ops missing from plugin");
        module_destory(g_pViModule);
        g_pViModule = NULL;
        g_pViOps = NULL;
        return MPP_INIT_FAILED;
    }

    return MPP_OK;
}

/* ============================================================
 * Public VI API
 * ============================================================ */

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

S32 VI_Init(VOID) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->init();
}

S32 VI_DeInit(VOID) {
    S32 s32Ret;

    if (g_pViModule == NULL || g_pViOps == NULL)
        return MPP_OK;

    s32Ret = g_pViOps->deinit();
    module_destory(g_pViModule);
    g_pViModule = NULL;
    g_pViOps = NULL;

    return s32Ret;
}

S32 VI_SetDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->set_dev_attr(ViDev, pstDevAttr);
}

S32 VI_GetDevAttr(VI_DEV ViDev, ViDevAttrS *pstDevAttr) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->get_dev_attr(ViDev, pstDevAttr);
}

S32 VI_EnableDev(VI_DEV ViDev) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->enable_dev(ViDev);
}

S32 VI_DisableDev(VI_DEV ViDev) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->disable_dev(ViDev);
}

S32 VI_SetChnAttr(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr) {
    S32 s32Ret;
    MpiViChnBufCtx *pstBufCtx = NULL;

    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (pstChnAttr == NULL || mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    s32Ret = g_pViOps->set_chn_attr(ViDev, ViChn, pstChnAttr);
    if (s32Ret != MPP_OK)
        return s32Ret;

    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];
    pstBufCtx->stChnAttr = *pstChnAttr;

    return mpi_vi_rebuild_chn_buf_ctx(ViDev, ViChn, pstChnAttr);
}

S32 VI_GetChnAttr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstChnAttr) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->get_chn_attr(ViDev, ViChn, pstChnAttr);
}

S32 VI_SetChnFrameRate(VI_DEV ViDev, VI_CHN ViChn, const ViFrameRateCtrlS *pstFrameRateCtrl) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->set_chn_framerate(ViDev, ViChn, pstFrameRateCtrl);
}

S32 VI_GetChnFrameRate(VI_DEV ViDev, VI_CHN ViChn, ViFrameRateCtrlS *pstFrameRateCtrl) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->get_chn_framerate(ViDev, ViChn, pstFrameRateCtrl);
}

S32 VI_EnableChn(VI_DEV ViDev, VI_CHN ViChn) {
    MpiViChnBufCtx *pstBufCtx = NULL;
    ViChnAttrS stChnAttr;
    S32 s32Ret = 0;

    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    memset(&stChnAttr, 0, sizeof(stChnAttr));
    s32Ret = g_pViOps->get_chn_attr(ViDev, ViChn, &stChnAttr);
    if (s32Ret != MPP_OK)
        return s32Ret;

    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];
    if (pstBufCtx->ulPoolId == 0) {
        s32Ret = mpi_vi_rebuild_chn_buf_ctx(ViDev, ViChn, &stChnAttr);
        if (s32Ret != MPP_OK)
            return s32Ret;
    }

    s32Ret = g_pViOps->set_external_buf_pool(
        ViDev, ViChn, pstBufCtx->ulPoolId, pstBufCtx->u32BufCnt,
        pstBufCtx->aulBufferId, pstBufCtx->astFrameInfo);
    if (s32Ret != MPP_OK) {
        mpi_vi_destroy_chn_buf_ctx(ViDev, ViChn);
        return s32Ret;
    }

    s32Ret = g_pViOps->enable_chn(ViDev, ViChn);
    if (s32Ret != MPP_OK) {
        mpi_vi_destroy_chn_buf_ctx(ViDev, ViChn);
        return s32Ret;
    }

    pstBufCtx->bEnabled = MPP_TRUE;
    pstBufCtx->ViDev    = ViDev;
    pstBufCtx->ViChn    = ViChn;

    /* Clamp depth to depth-queue array size */
    if (pstBufCtx->stChnAttr.u32Depth > VI_MPI_MAX_DEPTH)
        pstBufCtx->stChnAttr.u32Depth = VI_MPI_MAX_DEPTH;

    pthread_mutex_init(&pstBufCtx->depthLock, NULL);
    pthread_cond_init(&pstBufCtx->depthNotEmpty, NULL);
    pstBufCtx->u32DepthHead  = 0;
    pstBufCtx->u32DepthTail  = 0;
    pstBufCtx->u32DepthCount = 0;

    pstBufCtx->bTaskRun = MPP_TRUE;

    MpiViTaskArg *pPushArg = (MpiViTaskArg *)malloc(sizeof(MpiViTaskArg));
    MpiViTaskArg *pRecycleArg = (MpiViTaskArg *)malloc(sizeof(MpiViTaskArg));
    if (pPushArg == NULL || pRecycleArg == NULL) {
        free(pPushArg);
        free(pRecycleArg);
        pstBufCtx->bTaskRun = MPP_FALSE;
        s32Ret = MPI_VI_ERR_BUSY;
        goto err_threads;
    }

    pPushArg->ViDev    = pRecycleArg->ViDev = ViDev;
    pPushArg->ViChn    = pRecycleArg->ViChn = ViChn;

    if (pthread_create(&pstBufCtx->hPushTid, NULL, mpi_vi_push_task, pPushArg) != 0) {
        free(pPushArg);
        free(pRecycleArg);
        pstBufCtx->bTaskRun = MPP_FALSE;
        s32Ret = MPP_INIT_FAILED;
        goto err_threads;
    }

    if (pthread_create(&pstBufCtx->hRecycleTid, NULL, mpi_vi_recycle_task, pRecycleArg) != 0) {
        free(pRecycleArg);
        pstBufCtx->bTaskRun = MPP_FALSE;
        pthread_join(pstBufCtx->hPushTid, NULL);
        s32Ret = MPP_INIT_FAILED;
        goto err_threads;
    }

    return MPP_OK;

err_threads:
    pthread_mutex_destroy(&pstBufCtx->depthLock);
    pthread_cond_destroy(&pstBufCtx->depthNotEmpty);
    pstBufCtx->bTaskRun = MPP_FALSE;
    g_pViOps->disable_chn(ViDev, ViChn);
    mpi_vi_destroy_chn_buf_ctx(ViDev, ViChn);
    return s32Ret;
}

S32 VI_DisableChn(VI_DEV ViDev, VI_CHN ViChn) {
    MpiViChnBufCtx *pstBufCtx = NULL;
    S32 s32Ret = 0;

    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];

    /* Stop push/recycle threads before disabling V4L2 streaming */
    if (pstBufCtx->bTaskRun) {
        pstBufCtx->bTaskRun = MPP_FALSE;
        pthread_join(pstBufCtx->hPushTid,    NULL);
        pthread_join(pstBufCtx->hRecycleTid, NULL);
    }

    pthread_mutex_destroy(&pstBufCtx->depthLock);
    pthread_cond_destroy(&pstBufCtx->depthNotEmpty);

    s32Ret = g_pViOps->disable_chn(ViDev, ViChn);
    pstBufCtx->bEnabled = MPP_FALSE;

    if (pstBufCtx->ulPoolId != 0)
        mpi_vi_destroy_chn_buf_ctx(ViDev, ViChn);

    mpi_vi_destroy_rawdump_ctx(ViDev, ViChn);
    mpi_vi_destroy_offline_input_ctx(ViDev, ViChn);
    return s32Ret;
}

S32 VI_GetChnFrame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec) {
    MpiViChnBufCtx *pstBufCtx = NULL;

    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (pstVideoFrame == NULL)
        return MPP_NULL_POINTER;
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];
    if (pstBufCtx->bEnabled != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    if (pstBufCtx->stChnAttr.u32Depth == 0)
        return MPI_VI_ERR_NOT_SUPPORT;  /* bind-only mode, use SYS_Bind + VENC_GetStream */

    pthread_mutex_lock(&pstBufCtx->depthLock);

    if (s32MilliSec == 0) {
        /* Non-blocking */
        if (pstBufCtx->u32DepthCount == 0) {
            pthread_mutex_unlock(&pstBufCtx->depthLock);
            return MPI_VI_ERR_BUSY;
        }
    } else {
        /* Timed wait */
        while (pstBufCtx->u32DepthCount == 0 && pstBufCtx->bTaskRun) {
            if (s32MilliSec < 0) {
                pthread_cond_wait(&pstBufCtx->depthNotEmpty, &pstBufCtx->depthLock);
            } else {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec  += s32MilliSec / 1000;
                ts.tv_nsec += (s32MilliSec % 1000) * 1000000L;
                if (ts.tv_nsec >= 1000000000L) {
                    ts.tv_sec++;
                    ts.tv_nsec -= 1000000000L;
                }
                int rc = pthread_cond_timedwait(&pstBufCtx->depthNotEmpty, &pstBufCtx->depthLock, &ts);
                if (rc == ETIMEDOUT)
                    break;
            }
        }
        if (pstBufCtx->u32DepthCount == 0) {
            pthread_mutex_unlock(&pstBufCtx->depthLock);
            return MPI_VI_ERR_BUSY;
        }
    }

    MpiViDepthEntry *pEntry = &pstBufCtx->astDepthQueue[pstBufCtx->u32DepthHead];
    *pstVideoFrame = pEntry->stFrameInfo;
    pstBufCtx->u32DepthHead = (pstBufCtx->u32DepthHead + 1) % VI_MPI_MAX_DEPTH;
    pstBufCtx->u32DepthCount--;

    pthread_mutex_unlock(&pstBufCtx->depthLock);
    return MPP_OK;
}

S32 VI_ReleaseChnFrame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (pstVideoFrame == NULL)
        return MPP_NULL_POINTER;
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    /* Release the depth-queue reference; recycle thread re-QBUFs to V4L2
     * once all consumers (SYS sinks + depth queue) have released. */
    return VB_ReleaseBuffer(pstVideoFrame->ulBufferId);
}

S32 VI_TriggerRawDump(VI_DEV ViDev, VI_CHN ViChn) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (g_pViOps->trigger_raw_dump == NULL)
        return MPP_NOT_SUPPORTED;
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;
    if (mpi_vi_prepare_rawdump_ctx(ViDev, ViChn) != MPP_OK)
        return MPI_VI_ERR_BUSY;
    return g_pViOps->trigger_raw_dump(ViDev, ViChn);
}

S32 VI_GetRawDumpFrame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (g_pViOps->get_raw_dump_frame == NULL)
        return MPP_NOT_SUPPORTED;
    return g_pViOps->get_raw_dump_frame(ViDev, ViChn, pstVideoFrame, s32MilliSec);
}

S32 VI_ReleaseRawDumpFrame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (g_pViOps->release_raw_dump_frame == NULL)
        return MPP_NOT_SUPPORTED;
    return g_pViOps->release_raw_dump_frame(ViDev, ViChn, pstVideoFrame);
}

S32 VI_OfflineSetInputAddr(VI_DEV ViDev, VI_CHN ViChn, const U8 *pu8RawVirAddr, U32 u32RawSize) {
    MpiViOfflineInputCtx *pstInputCtx = NULL;
    ViChnAttrS stChnAttr;
    S32 s32Ret = 0;
    U32 u32ExpectedSize = 0;

    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (g_pViOps->offline_set_input_addr == NULL)
        return MPP_NOT_SUPPORTED;
    if (pu8RawVirAddr == NULL || mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    memset(&stChnAttr, 0, sizeof(stChnAttr));
    s32Ret = g_pViOps->get_chn_attr(ViDev, ViChn, &stChnAttr);
    if (s32Ret != MPP_OK)
        return s32Ret;

    pstInputCtx = &g_astViOfflineInputCtx[ViDev][ViChn];
    if (pstInputCtx->ulPoolId == 0) {
        s32Ret = mpi_vi_rebuild_offline_input_ctx(ViDev, ViChn, &stChnAttr);
        if (s32Ret != MPP_OK)
            return s32Ret;
    }

    u32ExpectedSize = pstInputCtx->stFrameInfo.stVFrame.u32TotalSize;
    if (u32RawSize > u32ExpectedSize || pstInputCtx->stFrameInfo.stVFrame.ulPlaneVirAddr[0] == 0)
        return MPI_VI_ERR_INVALID_PARAM;

    memcpy((void *)pstInputCtx->stFrameInfo.stVFrame.ulPlaneVirAddr[0], pu8RawVirAddr, u32RawSize);
    pstInputCtx->stFrameInfo.stVFrame.u32PlaneSizeValid[0] = u32RawSize;
    pstInputCtx->stFrameInfo.stVFrame.u32TotalSize = u32RawSize;

    return g_pViOps->offline_set_input_addr(
        ViDev, ViChn,
        pstInputCtx->ulPoolId, pstInputCtx->ulBufferId, &pstInputCtx->stFrameInfo,
        pu8RawVirAddr, u32RawSize);
}

S32 VI_AttachBindSink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    /* Threads already running from VI_EnableChn; plugin op is a no-op for K3.
     * Caller should use SYS_Bind() directly to establish the binding. */
    return g_pViOps->attach_bind_sink(ViDev, ViChn, pstSinkNode);
}

S32 VI_DetachBindSink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->detach_bind_sink(ViDev, ViChn, pstSinkNode);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
