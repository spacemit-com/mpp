#include "vi_api.h"
#include "vi_al_ops.h"

#include <dlfcn.h>
#include <linux/videodev2.h>
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
#include "v2d_api.h"

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

    /* V2D transform output pool — populated when bMirror/bFlip/eRotateMode is set */
    BOOL bNeedV2dTransform;
    UL   ulV2dOutPoolId;
    U32  u32V2dOutBufCnt;
    VideoFrameInfo astV2dOutFrameInfo[VI_MPI_MAX_BUF_CNT];
    UL   aulV2dOutBufferId[VI_MPI_MAX_BUF_CNT];

    /* Frame rate control — shared by K1 and K3, enforced in push_task */
    ViFrameRateCtrlS stFrameRateCtrl;
    U32 u32FrameRateSeq;

    /* Depth queue (pull mode, used only when u32Depth > 0) */
    pthread_mutex_t depthLock;
    pthread_cond_t  depthNotEmpty;
    U32 u32DepthHead;
    U32 u32DepthTail;
    U32 u32DepthCount;
    MpiViDepthEntry astDepthQueue[VI_MPI_MAX_DEPTH];
} MpiViChnBufCtx;

static MpiViChnBufCtx g_astViBufCtx[VI_MAX_DEV_NUM][VI_MAX_CHN_NUM];

static BOOL mpi_vi_is_valid_dev_chn(VI_DEV ViDev, VI_CHN ViChn) {
    return (ViDev >= 0 && ViDev < VI_MAX_DEV_NUM && ViChn >= 0 && ViChn < VI_MAX_CHN_NUM) ? MPP_TRUE : MPP_FALSE;
}

#define MODULE_TAG "mpp_vi"

static MppModule *g_pViModule = NULL;
static const ViAlOps *g_pViOps = NULL;

static VOID mpi_vi_destroy_chn_buf_ctx(VI_DEV ViDev, VI_CHN ViChn);

/* ============================================================
 * Frame rate control helper (shared by K1 and K3)
 * ============================================================ */

static BOOL mpi_vi_should_keep_frame(MpiViChnBufCtx *pstBufCtx) {
    U32 u32In  = pstBufCtx->stFrameRateCtrl.u32InputFrameStep;
    U32 u32Out = pstBufCtx->stFrameRateCtrl.u32OutputFrameStep;
    U32 u32Idx;

    if (u32In == 0 || u32Out == 0)
        return MPP_FALSE;
    if (u32Out >= u32In)
        return MPP_TRUE;

    u32Idx = pstBufCtx->u32FrameRateSeq % u32In;
    pstBufCtx->u32FrameRateSeq++;
    return (u32Idx < u32Out) ? MPP_TRUE : MPP_FALSE;
}

/* ============================================================
 * V2D transform helpers
 * ============================================================ */

static V2DRotateAngle mpi_vi_get_v2d_rotate(const ViChnAttrS *pstAttr) {
    if (pstAttr == NULL)
        return V2D_ROT_0;
    switch (pstAttr->eRotateMode) {
        case VI_ROT_0:      return V2D_ROT_0;
        case VI_ROT_90:     return V2D_ROT_90;
        case VI_ROT_180:    return V2D_ROT_180;
        case VI_ROT_270:    return V2D_ROT_270;
        case VI_ROT_MIRROR: return V2D_ROT_MIRROR;
        case VI_ROT_FLIP:   return V2D_ROT_FLIP;
        default:            break;
    }
    if (pstAttr->bMirror == MPP_TRUE && pstAttr->bFlip == MPP_TRUE) return V2D_ROT_180;
    if (pstAttr->bMirror == MPP_TRUE)  return V2D_ROT_MIRROR;
    if (pstAttr->bFlip == MPP_TRUE)    return V2D_ROT_FLIP;
    return V2D_ROT_0;
}

static BOOL mpi_vi_needs_v2d_transform(const ViChnAttrS *pstAttr) {
    if (pstAttr == NULL)
        return MPP_FALSE;
    return (pstAttr->bMirror || pstAttr->bFlip ||
            (pstAttr->eRotateMode != VI_ROT_0 && pstAttr->eRotateMode != VI_ROT_BUTT))
        ? MPP_TRUE : MPP_FALSE;
}

/* ============================================================
 * Output layout negotiation — the AL backend owns any platform
 * memory layout (K1 DWT tail, ISP raw packing, …).  MPI only
 * computes the neutral pixel-math base and allocates from the
 * negotiated result; it never derives platform layout itself.
 * ============================================================ */

static S32 mpi_vi_calc_chn_layout(
    VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr, VideoFrameInfo *pstFrameInfo) {
    S32 s32Ret;

    if (pstChnAttr == NULL || pstFrameInfo == NULL)
        return MPI_VI_ERR_INVALID_PARAM;

    s32Ret = MPI_VI_CalcFrameInfo(pstChnAttr, pstFrameInfo);
    if (s32Ret != MPP_OK)
        return s32Ret;

    /* NULL op = the neutral layout is already exact for this platform. */
    if (g_pViOps->calc_frame_layout != NULL)
        return g_pViOps->calc_frame_layout(ViDev, ViChn, pstChnAttr, pstFrameInfo);

    return MPP_OK;
}

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

        /* Frame rate control: drop frames not scheduled for output */
        if (mpi_vi_should_keep_frame(pstBufCtx) != MPP_TRUE) {
            VB_ModReleaseBuffer(ulBuf, MPP_ID_VI);
            continue;
        }

        /* Build frame info: copy the channel template and fill PTS/sequence
         * from the DQBUF metadata. */
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

        /* V2D transform path: raw capture buffer → V2D output buffer */
        if (pstBufCtx->bNeedV2dTransform) {
            UL ulV2dBuf = VB_ModGetBuffer(pstBufCtx->ulV2dOutPoolId, MPP_ID_VI, 200);
            if (ulV2dBuf == 0 || ulV2dBuf == (UL)-1) {
                error("vi push: v2d out pool exhausted, drop frame dev=%d chn=%d", ViDev, ViChn);
                VB_ModReleaseBuffer(ulBuf, MPP_ID_VI);
                continue;
            }

            U32 v2dSlot = (U32)-1;
            for (U32 i = 0; i < pstBufCtx->u32V2dOutBufCnt; i++) {
                if (pstBufCtx->aulV2dOutBufferId[i] == ulV2dBuf) {
                    v2dSlot = i;
                    break;
                }
            }
            if (v2dSlot == (U32)-1) {
                error("vi push: v2d buf %lu not found in slot table", ulV2dBuf);
                VB_ModReleaseBuffer(ulV2dBuf, MPP_ID_VI);
                VB_ModReleaseBuffer(ulBuf, MPP_ID_VI);
                continue;
            }

            VideoFrameInfo stV2dFrame = pstBufCtx->astV2dOutFrameInfo[v2dSlot];
            stV2dFrame.stVFrame.u64PTS       = stFrame.stVFrame.u64PTS;
            stV2dFrame.stVFrame.u32FrameFlag = stFrame.stVFrame.u32FrameFlag;

            V2DHandle hV2d = 0;
            S32 v2dRet = V2D_BeginJob(&hV2d);
            if (v2dRet == MPP_OK) {
                v2dRet = V2D_RotateFrame(hV2d, &stFrame, &stV2dFrame,
                    mpi_vi_get_v2d_rotate(&pstBufCtx->stChnAttr));
                if (v2dRet == MPP_OK)
                    v2dRet = V2D_EndJob(hV2d);
                else
                    (void)V2D_CancelJob(hV2d);
            } else {
                VB_ModReleaseBuffer(ulV2dBuf, MPP_ID_VI);
                VB_ModReleaseBuffer(ulBuf, MPP_ID_VI);
                continue;
            }
            if (v2dRet != MPP_OK) {
                error("vi push: V2D_RotateFrame failed ret=%d dev=%d chn=%d", v2dRet, ViDev, ViChn);
                VB_ModReleaseBuffer(ulV2dBuf, MPP_ID_VI);
                VB_ModReleaseBuffer(ulBuf, MPP_ID_VI);
                continue;
            }

            stV2dFrame.stVFrame.u32PlaneSizeValid[0] = stV2dFrame.stVFrame.u32PlaneSize[0];
            if (stV2dFrame.stVFrame.u32PlaneNum > 1)
                stV2dFrame.stVFrame.u32PlaneSizeValid[1] = stV2dFrame.stVFrame.u32PlaneSize[1];

            /* Release raw capture buffer — recycle thread re-queues it to V4L2 */
            VB_ModReleaseBuffer(ulBuf, MPP_ID_VI);

            VB_UpdateBufferFrameInfo(ulV2dBuf, &stV2dFrame);
            (void)SYS_SendFrame(&stSrcNode, ulV2dBuf);

            U32 u32Depth = pstBufCtx->stChnAttr.u32Depth;
            if (u32Depth > 0) {
                VB_ModRefAdd(ulV2dBuf, MPP_ID_VI);
                pthread_mutex_lock(&pstBufCtx->depthLock);
                if (pstBufCtx->u32DepthCount >= u32Depth) {
                    MpiViDepthEntry *pOld = &pstBufCtx->astDepthQueue[pstBufCtx->u32DepthHead];
                    VB_ModRefSub(pOld->ulBufferId, MPP_ID_VI);
                    pstBufCtx->u32DepthHead = (pstBufCtx->u32DepthHead + 1) % VI_MPI_MAX_DEPTH;
                    pstBufCtx->u32DepthCount--;
                }
                MpiViDepthEntry *pNew = &pstBufCtx->astDepthQueue[pstBufCtx->u32DepthTail];
                pNew->ulBufferId  = ulV2dBuf;
                pNew->stFrameInfo = stV2dFrame;
                pstBufCtx->u32DepthTail = (pstBufCtx->u32DepthTail + 1) % VI_MPI_MAX_DEPTH;
                pstBufCtx->u32DepthCount++;
                pthread_cond_signal(&pstBufCtx->depthNotEmpty);
                pthread_mutex_unlock(&pstBufCtx->depthLock);
            }

            VB_ModReleaseBuffer(ulV2dBuf, MPP_ID_VI);

            continue;
        }

        VB_UpdateBufferFrameInfo(ulBuf, &stFrame);

        /* 1. Push to all SYS-bound sinks (no-op / NOT_FOUND if no bind) */
        (void)SYS_SendFrame(&stSrcNode, ulBuf);

        /* 2. If depth > 0, also push into the depth queue for VI_GetChnFrame */
        U32 u32Depth = pstBufCtx->stChnAttr.u32Depth;
        if (u32Depth > 0) {
            VB_ModRefAdd(ulBuf, MPP_ID_VI);

            pthread_mutex_lock(&pstBufCtx->depthLock);

            if (pstBufCtx->u32DepthCount >= u32Depth) {
                /* Queue full — evict oldest entry */
                MpiViDepthEntry *pOld = &pstBufCtx->astDepthQueue[pstBufCtx->u32DepthHead];
                VB_ModRefSub(pOld->ulBufferId, MPP_ID_VI);
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
        VB_ModReleaseBuffer(ulBuf, MPP_ID_VI);
    }

    info("vi push task exiting: dev=%d chn=%d", ViDev, ViChn);
    return NULL;
}

/* ============================================================
 * Recycle thread: VB_ModGetBuffer → QBUF back to V4L2
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
        UL ulBuf = VB_ModGetBuffer(pstBufCtx->ulPoolId, MPP_ID_VI, 100);
        if (ulBuf == 0 || ulBuf == (UL)-1)
            continue;

        /* Re-check after the potentially long wait: shutdown may have started.
         * Keep the VI reference so that
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
            VB_ModReleaseBuffer(ulBuf, MPP_ID_VI);
            continue;
        }

        /* If this slot previously failed QBUF, skip re-queuing it to V4L2.
         * Keep ref=1 (acquired via VB_ModGetBuffer above) — MPI_VI_DestroyOutBufPool
         * will release it during channel teardown.  Do not release here, as that
         * would return the slot to the free pool and cause it to be re-acquired
         * in the next VB_ModGetBuffer call, creating a tight loop. */
        if (pstBufCtx->u32BadSlotMask & (1u << slot))
            continue;

        /* QBUF back to V4L2; the VI reference represents "V4L2 owns it". */
        pstBufCtx->aulBufferId[slot] = ulBuf;
        if (g_pViOps->queue_buffer(ViDev, ViChn, slot) != MPP_OK) {
            /* Log only on the first failure per slot so a systematic driver
             * misconfiguration is visible, while teardown EINVAL noise is not
             * repeated.  Keep ref=1 — MPI_VI_DestroyOutBufPool releases it. */
            if (!(pstBufCtx->u32BadSlotMask & (1u << slot))) {
                info("vi recycle: slot %u (dev=%d chn=%d) QBUF failed; "
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
 * Channel buffer context helpers
 * ============================================================ */

static VOID mpi_vi_destroy_chn_buf_ctx(VI_DEV ViDev, VI_CHN ViChn) {
    MpiViChnBufCtx *pstBufCtx = NULL;

    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return;

    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];
    if (pstBufCtx->ulV2dOutPoolId != 0)
        VB_DestroyPool(pstBufCtx->ulV2dOutPoolId);
    if (pstBufCtx->ulPoolId != 0)
        MPI_VI_DestroyOutBufPool(pstBufCtx->ulPoolId, pstBufCtx->u32BufCnt, pstBufCtx->aulBufferId);

    memset(pstBufCtx, 0, sizeof(*pstBufCtx));
}

static S32 mpi_vi_setup_chn_buf_ctx(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr) {
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

    s32Ret = mpi_vi_calc_chn_layout(ViDev, ViChn, pstChnAttr, &pstBufCtx->stFrameTemplate);
    if (s32Ret != MPP_OK)
        return s32Ret;

    s32Ret = MPI_VI_CreateOutBufPool(
        ViDev, ViChn, pstChnAttr, &pstBufCtx->stFrameTemplate, u32BufCnt,
        &pstBufCtx->ulPoolId,
        pstBufCtx->astFrameInfo, pstBufCtx->aulBufferId);
    if (s32Ret != MPP_OK)
        return s32Ret;

    pstBufCtx->u32BufCnt = u32BufCnt;
    pstBufCtx->stChnAttr = *pstChnAttr;

    /* Reset frame rate to 1:1 on every rebuild so seq and rate are consistent */
    pstBufCtx->stFrameRateCtrl.u32InputFrameStep  = 1;
    pstBufCtx->stFrameRateCtrl.u32OutputFrameStep = 1;
    pstBufCtx->u32FrameRateSeq = 0;

    /* Create V2D output pool if mirror/flip/rotate is configured */
    pstBufCtx->bNeedV2dTransform = mpi_vi_needs_v2d_transform(pstChnAttr);
    if (pstBufCtx->bNeedV2dTransform) {
        VideoFrameInfo stV2dTemplate;
        V2DRotateAngle enRot = mpi_vi_get_v2d_rotate(pstChnAttr);
        ViChnAttrS stOutAttr = *pstChnAttr;
        if (enRot == V2D_ROT_90 || enRot == V2D_ROT_270) {
            stOutAttr.u32Width  = pstChnAttr->u32Height;
            stOutAttr.u32Height = pstChnAttr->u32Width;
        }
        pstBufCtx->u32V2dOutBufCnt = u32BufCnt;
        s32Ret = mpi_vi_calc_chn_layout(ViDev, ViChn, &stOutAttr, &stV2dTemplate);
        if (s32Ret != MPP_OK) {
            mpi_vi_destroy_chn_buf_ctx(ViDev, ViChn);
            return s32Ret;
        }
        s32Ret = MPI_VI_CreateOutBufPool(
            ViDev, ViChn, &stOutAttr, &stV2dTemplate, pstBufCtx->u32V2dOutBufCnt,
            &pstBufCtx->ulV2dOutPoolId,
            pstBufCtx->astV2dOutFrameInfo, pstBufCtx->aulV2dOutBufferId);
        if (s32Ret != MPP_OK) {
            pstBufCtx->ulV2dOutPoolId  = 0;
            pstBufCtx->u32V2dOutBufCnt = 0;
            mpi_vi_destroy_chn_buf_ctx(ViDev, ViChn);
            return s32Ret;
        }
        info("vi v2d out pool created: dev=%d chn=%d rot=%d pool=%lu cnt=%u",
            ViDev, ViChn, (int)enRot, pstBufCtx->ulV2dOutPoolId, pstBufCtx->u32V2dOutBufCnt);

        /* MPI_VI_CreateOutBufPool acquires every buffer (ref=1). Release them
         * all so VB_ModGetBuffer in push_task can acquire them one by one. */
        for (U32 j = 0; j < pstBufCtx->u32V2dOutBufCnt; j++)
            VB_ModReleaseBuffer(pstBufCtx->aulV2dOutBufferId[j], MPP_ID_VI);
    }

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
        g_pViOps->set_chn_attr == NULL || g_pViOps->get_chn_attr == NULL || g_pViOps->enable_chn == NULL ||
        g_pViOps->disable_chn == NULL || g_pViOps->dequeue_done_buffer == NULL || g_pViOps->queue_buffer == NULL ||
        g_pViOps->set_external_buf_pool == NULL) {
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

    return mpi_vi_setup_chn_buf_ctx(ViDev, ViChn, pstChnAttr);
}

S32 VI_GetChnAttr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstChnAttr) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->get_chn_attr(ViDev, ViChn, pstChnAttr);
}

S32 VI_SetChnFrameRate(VI_DEV ViDev, VI_CHN ViChn, const ViFrameRateCtrlS *pstFrameRateCtrl) {
    MpiViChnBufCtx *pstBufCtx = NULL;

    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (pstFrameRateCtrl == NULL || mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;
    if (pstFrameRateCtrl->u32InputFrameStep == 0 || pstFrameRateCtrl->u32OutputFrameStep == 0)
        return MPI_VI_ERR_INVALID_PARAM;

    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];
    if (pstBufCtx->bEnabled != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;
    pstBufCtx->stFrameRateCtrl = *pstFrameRateCtrl;
    pstBufCtx->u32FrameRateSeq = 0;
    return MPP_OK;
}

S32 VI_GetChnFrameRate(VI_DEV ViDev, VI_CHN ViChn, ViFrameRateCtrlS *pstFrameRateCtrl) {
    if (pstFrameRateCtrl == NULL || mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;
    if (g_astViBufCtx[ViDev][ViChn].bEnabled != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;
    *pstFrameRateCtrl = g_astViBufCtx[ViDev][ViChn].stFrameRateCtrl;
    return MPP_OK;
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
        s32Ret = mpi_vi_setup_chn_buf_ctx(ViDev, ViChn, &stChnAttr);
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

    /* Rawdump/offline buffers are owned by the backend and are released
     * by the backend's own disable path. */
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
    if (pEntry->ulBufferId != 0) {
        S32 s32Ret = VB_RefAdd(pEntry->ulBufferId);
        if (s32Ret != MPP_OK) {
            pthread_mutex_unlock(&pstBufCtx->depthLock);
            return s32Ret;
        }
        s32Ret = VB_ModRefSub(pEntry->ulBufferId, MPP_ID_VI);
        if (s32Ret != MPP_OK) {
            VB_ReleaseBuffer(pEntry->ulBufferId);
            pthread_mutex_unlock(&pstBufCtx->depthLock);
            return s32Ret;
        }
    }
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
    return g_pViOps->trigger_raw_dump(ViDev, ViChn);
}

S32 VI_GetRawDumpFrame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (g_pViOps->get_raw_dump_frame == NULL)
        return MPP_NOT_SUPPORTED;
    if (pstVideoFrame == NULL)
        return MPP_NULL_POINTER;
    return g_pViOps->get_raw_dump_frame(ViDev, ViChn, pstVideoFrame, s32MilliSec);
}

S32 VI_ReleaseRawDumpFrame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (g_pViOps->release_raw_dump_frame == NULL)
        return MPP_NOT_SUPPORTED;
    if (pstVideoFrame == NULL)
        return MPP_NULL_POINTER;
    return g_pViOps->release_raw_dump_frame(ViDev, ViChn, pstVideoFrame);
}

S32 VI_OfflineSetInputAddr(VI_DEV ViDev, VI_CHN ViChn, const U8 *pu8RawVirAddr, U32 u32RawSize) {
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (g_pViOps->offline_set_input_addr == NULL)
        return MPP_NOT_SUPPORTED;
    if (pu8RawVirAddr == NULL || u32RawSize == 0)
        return MPI_VI_ERR_INVALID_PARAM;
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    /* The staging buffer is owned by the backend: it validates the size,
     * copies the user data once and submits it to the offline ISP. */
    return g_pViOps->offline_set_input_addr(ViDev, ViChn, pu8RawVirAddr, u32RawSize);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
