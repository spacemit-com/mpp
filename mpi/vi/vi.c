#include "vi_api.h"
#include "vi_al_ops.h"

#include <dlfcn.h>
#include <string.h>

#include "log.h"
#include "module.h"
#include "vi_buf_mgr.h"

#define VI_MPI_MAX_BUF_CNT 8
#define MPI_VI_ERR_INVALID_PARAM (-1)
#define MPI_VI_ERR_BUSY          (-4)

typedef struct _MpiViChnBufCtx {
    BOOL bEnabled;
    UL ulPoolId;
    U32 u32BufCnt;
    ViChnAttrS stChnAttr;
    U32 u32ReadyHead;
    U32 u32ReadyTail;
    U32 u32ReadyNum;
    U32 au32ReadyQueue[VI_MPI_MAX_BUF_CNT];
    U8 au8NodeState[VI_MPI_MAX_BUF_CNT];
    VideoFrameInfo stFrameTemplate;
    VideoFrameInfo astFrameInfo[VI_MPI_MAX_BUF_CNT];
    UL aulBufferId[VI_MPI_MAX_BUF_CNT];
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

typedef enum _MpiViBufNodeState {
    MPI_VI_BUF_NODE_IDLE = 0,
    MPI_VI_BUF_NODE_IN_HW,
    MPI_VI_BUF_NODE_READY,
    MPI_VI_BUF_NODE_USER,
} MpiViBufNodeState;

static MpiViChnBufCtx g_astViBufCtx[VI_MAX_DEV_NUM][VI_MAX_CHN_NUM];
static MpiViOfflineInputCtx g_astViOfflineInputCtx[VI_MAX_DEV_NUM][VI_MAX_CHN_NUM];
static MpiViRawDumpCtx g_astViRawDumpCtx[VI_MAX_DEV_NUM][VI_MAX_CHN_NUM];

static BOOL mpi_vi_is_valid_dev_chn(VI_DEV ViDev, VI_CHN ViChn)
{
    return (ViDev >= 0 && ViDev < VI_MAX_DEV_NUM && ViChn >= 0 && ViChn < VI_MAX_CHN_NUM) ? MPP_TRUE : MPP_FALSE;
}

#define MODULE_TAG "mpp_vi"

static MppModule *g_pViModule = NULL;
static const ViAlOps *g_pViOps = NULL;

static VOID mpi_vi_destroy_chn_buf_ctx(VI_DEV ViDev, VI_CHN ViChn);

static S32 mpi_vi_find_buffer_index(const MpiViChnBufCtx *pstBufCtx, UL ulBufferId)
{
    U32 i;

    if (pstBufCtx == NULL)
        return MPI_VI_ERR_INVALID_PARAM;

    for (i = 0; i < pstBufCtx->u32BufCnt; ++i) {
        if (pstBufCtx->aulBufferId[i] == ulBufferId)
            return (S32)i;
    }

    return MPI_VI_ERR_INVALID_PARAM;
}

static VOID mpi_vi_destroy_rawdump_ctx(VI_DEV ViDev, VI_CHN ViChn)
{
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

static S32 mpi_vi_prepare_rawdump_ctx(VI_DEV ViDev, VI_CHN ViChn)
{
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
        error("rawdump calc frame failed, dev=%d chn=%d fmt=%d w=%u h=%u ret=%d",
            ViDev, ViChn, stChnAttr.ePixelFormat, stChnAttr.u32Width, stChnAttr.u32Height, s32Ret);
        return s32Ret;
    }

    info("rawdump alloc, dev=%d chn=%d fmt=%d w=%u h=%u total=%u stride0=%u",
        ViDev, ViChn,
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
        error("rawdump VB_CreatePool failed, dev=%d chn=%d size=%u cnt=%u", ViDev, ViChn,
            stPoolCfg.u32BufSize, stPoolCfg.u32BufCnt);
        return MPI_VI_ERR_BUSY;
    }

    s32Ret = VB_SetFrameInfo(pstRawCtx->ulPoolId, &stFrameTemplate);
    if (s32Ret != MPP_OK) {
        error("rawdump VB_SetFrameInfo failed, dev=%d chn=%d pool=%lu ret=%d", ViDev, ViChn,
            pstRawCtx->ulPoolId, s32Ret);
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
        error("rawdump VB_GetFrameInfo failed, dev=%d chn=%d buf=%lu ret=%d", ViDev, ViChn,
            pstRawCtx->ulBufferId, s32Ret);
        mpi_vi_destroy_rawdump_ctx(ViDev, ViChn);
        return s32Ret;
    }

    pstRawCtx->stFrameInfo.eFrameType = FRAME_TYPE_VI;
    pstRawCtx->stFrameInfo.eModId = MPP_ID_VI;
    pstRawCtx->stFrameInfo.u32Idx = 0;
    pstRawCtx->stFrameInfo.ulPoolId = pstRawCtx->ulPoolId;
    pstRawCtx->stFrameInfo.ulBufferId = pstRawCtx->ulBufferId;
    pstRawCtx->stFrameInfo.stVFrame.u32PrivateData = ((U32)ViDev << 16) | (U32)ViChn;

    /* Populate vir addr and dma-buf fd so the plugin can extract them. */
    {
        S32 s32Fd = -1;
        U32 j;
        (void)VB_GetVirAddr(pstRawCtx->ulBufferId,
            (void **)&pstRawCtx->stFrameInfo.stVFrame.ulPlaneVirAddr[0]);
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

static VOID mpi_vi_reset_ready_queue(MpiViChnBufCtx *pstBufCtx)
{
    if (pstBufCtx == NULL)
        return;

    pstBufCtx->u32ReadyHead = 0;
    pstBufCtx->u32ReadyTail = 0;
    pstBufCtx->u32ReadyNum = 0;
}

static VOID mpi_vi_set_buf_state(MpiViChnBufCtx *pstBufCtx, U32 u32Index, MpiViBufNodeState eState)
{
    if (pstBufCtx == NULL || u32Index >= pstBufCtx->u32BufCnt)
        return;

    pstBufCtx->au8NodeState[u32Index] = (U8)eState;
}

static S32 mpi_vi_ready_push(MpiViChnBufCtx *pstBufCtx, U32 u32Index)
{
    if (pstBufCtx == NULL || u32Index >= pstBufCtx->u32BufCnt)
        return MPI_VI_ERR_INVALID_PARAM;
    if (pstBufCtx->u32ReadyNum >= VI_MPI_MAX_BUF_CNT)
        return MPI_VI_ERR_BUSY;

    pstBufCtx->au32ReadyQueue[pstBufCtx->u32ReadyTail] = u32Index;
    pstBufCtx->u32ReadyTail = (pstBufCtx->u32ReadyTail + 1U) % VI_MPI_MAX_BUF_CNT;
    pstBufCtx->u32ReadyNum++;
    return MPP_OK;
}

static S32 mpi_vi_ready_pop(MpiViChnBufCtx *pstBufCtx, U32 *pu32Index)
{
    if (pstBufCtx == NULL || pu32Index == NULL)
        return MPP_NULL_POINTER;
    if (pstBufCtx->u32ReadyNum == 0)
        return MPI_VI_ERR_BUSY;

    *pu32Index = pstBufCtx->au32ReadyQueue[pstBufCtx->u32ReadyHead];
    pstBufCtx->u32ReadyHead = (pstBufCtx->u32ReadyHead + 1U) % VI_MPI_MAX_BUF_CNT;
    pstBufCtx->u32ReadyNum--;
    return MPP_OK;
}

static S32 mpi_vi_drain_done_buffers(VI_DEV ViDev, VI_CHN ViChn, S32 s32MilliSec)
{
    MpiViChnBufCtx *pstBufCtx = NULL;
    U32 u32Index = 0;
    S32 s32Ret = MPP_OK;
    S32 s32WaitMs = s32MilliSec;

    if (g_pViOps == NULL || g_pViOps->dequeue_done_buffer == NULL)
        return MPP_INIT_FAILED;
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];
    if (pstBufCtx->bEnabled != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    for (;;) {
        s32Ret = g_pViOps->dequeue_done_buffer(ViDev, ViChn, &u32Index, s32WaitMs);
        if (s32Ret != MPP_OK)
            break;
        if (u32Index >= pstBufCtx->u32BufCnt)
            return MPI_VI_ERR_INVALID_PARAM;

        mpi_vi_set_buf_state(pstBufCtx, u32Index, MPI_VI_BUF_NODE_READY);
        s32Ret = mpi_vi_ready_push(pstBufCtx, u32Index);
        if (s32Ret != MPP_OK)
            return s32Ret;
        s32WaitMs = 0;
    }

    return (s32Ret == MPI_VI_ERR_BUSY) ? MPP_OK : s32Ret;
}

static VOID mpi_vi_reset_chn_buf_ctx(VI_DEV ViDev, VI_CHN ViChn)
{
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return;

    memset(&g_astViBufCtx[ViDev][ViChn], 0, sizeof(g_astViBufCtx[ViDev][ViChn]));
}

static VOID mpi_vi_destroy_chn_buf_ctx(VI_DEV ViDev, VI_CHN ViChn)
{
    MpiViChnBufCtx *pstBufCtx = NULL;

    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return;

    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];
    if (pstBufCtx->ulPoolId != 0)
        MPI_VI_DestroyOutBufPool(pstBufCtx->ulPoolId, pstBufCtx->u32BufCnt, pstBufCtx->aulBufferId);

    mpi_vi_reset_chn_buf_ctx(ViDev, ViChn);
}

static VOID mpi_vi_reset_offline_input_ctx(VI_DEV ViDev, VI_CHN ViChn)
{
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return;

    memset(&g_astViOfflineInputCtx[ViDev][ViChn], 0, sizeof(g_astViOfflineInputCtx[ViDev][ViChn]));
}

static VOID mpi_vi_destroy_offline_input_ctx(VI_DEV ViDev, VI_CHN ViChn)
{
    MpiViOfflineInputCtx *pstInputCtx = NULL;
    UL aulBufferId[1] = {0};

    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return;

    pstInputCtx = &g_astViOfflineInputCtx[ViDev][ViChn];
    if (pstInputCtx->ulPoolId != 0) {
        aulBufferId[0] = pstInputCtx->ulBufferId;
        MPI_VI_DestroyOutBufPool(pstInputCtx->ulPoolId, 1, aulBufferId);
    }

    mpi_vi_reset_offline_input_ctx(ViDev, ViChn);
}

static S32 mpi_vi_rebuild_offline_input_ctx(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr)
{
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

    s32Ret = MPI_VI_CreateOutBufPool(ViDev, ViChn, pstChnAttr, 1,
        &pstInputCtx->ulPoolId,
        &stFrameTemplate,
        &pstInputCtx->stFrameInfo,
        &pstInputCtx->ulBufferId);
    if (s32Ret != MPP_OK)
        return s32Ret;

    pstInputCtx->bEnabled = MPP_TRUE;
    return MPP_OK;
}

static S32 mpi_vi_rebuild_chn_buf_ctx(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr)
{
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

    s32Ret = MPI_VI_CreateOutBufPool(ViDev, ViChn, pstChnAttr, u32BufCnt,
        &pstBufCtx->ulPoolId,
        &pstBufCtx->stFrameTemplate,
        pstBufCtx->astFrameInfo,
        pstBufCtx->aulBufferId);
    if (s32Ret != MPP_OK)
        return s32Ret;

    pstBufCtx->u32BufCnt = u32BufCnt;
    pstBufCtx->stChnAttr = *pstChnAttr;
    mpi_vi_reset_ready_queue(pstBufCtx);
    memset(pstBufCtx->au8NodeState, MPI_VI_BUF_NODE_IDLE, sizeof(pstBufCtx->au8NodeState));
    return MPP_OK;
}

static S32 vi_load_plugin(VOID)
{
    PFN_al_vi_get_ops pfn_get_ops;
    void *handle;

    if (g_pViOps != NULL)
        return MPP_OK;

    /* Try K3 (pure V4L2) first, fall back to K1 (full ISP). */
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

    /* Validate required ops. */
    if (g_pViOps->init == NULL || g_pViOps->deinit == NULL ||
        g_pViOps->set_dev_attr == NULL || g_pViOps->get_dev_attr == NULL ||
        g_pViOps->enable_dev == NULL || g_pViOps->disable_dev == NULL ||
        g_pViOps->set_chn_attr == NULL || g_pViOps->get_chn_attr == NULL ||
        g_pViOps->set_chn_framerate == NULL || g_pViOps->get_chn_framerate == NULL ||
        g_pViOps->enable_chn == NULL || g_pViOps->disable_chn == NULL ||
        g_pViOps->dequeue_done_buffer == NULL || g_pViOps->queue_buffer == NULL ||
        g_pViOps->attach_bind_sink == NULL || g_pViOps->detach_bind_sink == NULL ||
        g_pViOps->set_external_buf_pool == NULL) {
        error("required VI ops missing from plugin");
        module_destory(g_pViModule);
        g_pViModule = NULL;
        g_pViOps = NULL;
        return MPP_INIT_FAILED;
    }

    return MPP_OK;
}

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

S32 VI_Init(VOID)
{
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->init();
}

S32 VI_DeInit(VOID)
{
    S32 s32Ret;

    if (g_pViModule == NULL || g_pViOps == NULL)
        return MPP_OK;

    s32Ret = g_pViOps->deinit();
    module_destory(g_pViModule);
    g_pViModule = NULL;
    g_pViOps = NULL;

    return s32Ret;
}

S32 VI_SetDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr)
{
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->set_dev_attr(ViDev, pstDevAttr);
}

S32 VI_GetDevAttr(VI_DEV ViDev, ViDevAttrS *pstDevAttr)
{
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->get_dev_attr(ViDev, pstDevAttr);
}

S32 VI_EnableDev(VI_DEV ViDev)
{
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->enable_dev(ViDev);
}

S32 VI_DisableDev(VI_DEV ViDev)
{
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->disable_dev(ViDev);
}

S32 VI_SetChnAttr(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr)
{
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

S32 VI_GetChnAttr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstChnAttr)
{
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->get_chn_attr(ViDev, ViChn, pstChnAttr);
}

S32 VI_SetChnFrameRate(VI_DEV ViDev, VI_CHN ViChn, const ViFrameRateCtrlS *pstFrameRateCtrl)
{
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->set_chn_framerate(ViDev, ViChn, pstFrameRateCtrl);
}

S32 VI_GetChnFrameRate(VI_DEV ViDev, VI_CHN ViChn, ViFrameRateCtrlS *pstFrameRateCtrl)
{
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->get_chn_framerate(ViDev, ViChn, pstFrameRateCtrl);
}

S32 VI_EnableChn(VI_DEV ViDev, VI_CHN ViChn)
{
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

    s32Ret = g_pViOps->set_external_buf_pool(ViDev, ViChn,
        pstBufCtx->ulPoolId,
        pstBufCtx->u32BufCnt,
        pstBufCtx->aulBufferId,
        pstBufCtx->astFrameInfo);
    if (s32Ret != MPP_OK) {
        mpi_vi_destroy_chn_buf_ctx(ViDev, ViChn);
        return s32Ret;
    }

    s32Ret = g_pViOps->enable_chn(ViDev, ViChn);
    if (s32Ret != MPP_OK) {
        mpi_vi_destroy_chn_buf_ctx(ViDev, ViChn);
        return s32Ret;
    }

    for (U32 i = 0; i < pstBufCtx->u32BufCnt; ++i)
        mpi_vi_set_buf_state(pstBufCtx, i, MPI_VI_BUF_NODE_IN_HW);

    mpi_vi_reset_ready_queue(pstBufCtx);
    pstBufCtx->bEnabled = MPP_TRUE;
    return s32Ret;
}

S32 VI_DisableChn(VI_DEV ViDev, VI_CHN ViChn)
{
    MpiViChnBufCtx *pstBufCtx = NULL;
    S32 s32Ret = 0;

    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;
    s32Ret = g_pViOps->disable_chn(ViDev, ViChn);
    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];
    pstBufCtx->bEnabled = MPP_FALSE;
    if (pstBufCtx->ulPoolId != 0)
        mpi_vi_destroy_chn_buf_ctx(ViDev, ViChn);
    mpi_vi_destroy_rawdump_ctx(ViDev, ViChn);
    mpi_vi_destroy_offline_input_ctx(ViDev, ViChn);
    return s32Ret;
}

S32 VI_GetChnFrame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec)
{
    MpiViChnBufCtx *pstBufCtx = NULL;
    U32 u32Index;
    S32 s32Ret;

    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (pstVideoFrame == NULL)
        return MPP_NULL_POINTER;
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];
    if (pstBufCtx->bEnabled != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;
    s32Ret = mpi_vi_drain_done_buffers(ViDev, ViChn, s32MilliSec);
    if (s32Ret != MPP_OK)
        return s32Ret;
    s32Ret = mpi_vi_ready_pop(pstBufCtx, &u32Index);
    if (s32Ret != MPP_OK)
        return s32Ret;

    *pstVideoFrame = pstBufCtx->astFrameInfo[u32Index];
    mpi_vi_set_buf_state(pstBufCtx, u32Index, MPI_VI_BUF_NODE_USER);
    return MPP_OK;
}

S32 VI_TriggerRawDump(VI_DEV ViDev, VI_CHN ViChn)
{
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

S32 VI_GetRawDumpFrame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec)
{
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (g_pViOps->get_raw_dump_frame == NULL)
        return MPP_NOT_SUPPORTED;
    return g_pViOps->get_raw_dump_frame(ViDev, ViChn, pstVideoFrame, s32MilliSec);
}

S32 VI_ReleaseRawDumpFrame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame)
{
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (g_pViOps->release_raw_dump_frame == NULL)
        return MPP_NOT_SUPPORTED;
    return g_pViOps->release_raw_dump_frame(ViDev, ViChn, pstVideoFrame);
}

S32 VI_OfflineSetInputAddr(VI_DEV ViDev, VI_CHN ViChn, const U8 *pu8RawVirAddr, U32 u32RawSize)
{
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

    return g_pViOps->offline_set_input_addr(ViDev, ViChn,
        pstInputCtx->ulPoolId,
        pstInputCtx->ulBufferId,
        &pstInputCtx->stFrameInfo,
        pu8RawVirAddr,
        u32RawSize);
}

S32 VI_AttachBindSink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode)
{
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->attach_bind_sink(ViDev, ViChn, pstSinkNode);
}

S32 VI_DetachBindSink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode)
{
    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    return g_pViOps->detach_bind_sink(ViDev, ViChn, pstSinkNode);
}

S32 VI_ReleaseChnFrame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame)
{
    MpiViChnBufCtx *pstBufCtx = NULL;
    U32 i;

    if (vi_load_plugin() != MPP_OK)
        return MPP_INIT_FAILED;
    if (pstVideoFrame == NULL)
        return MPP_NULL_POINTER;
    if (mpi_vi_is_valid_dev_chn(ViDev, ViChn) != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;

    pstBufCtx = &g_astViBufCtx[ViDev][ViChn];
    if (pstBufCtx->bEnabled != MPP_TRUE)
        return MPI_VI_ERR_INVALID_PARAM;
    for (i = 0; i < pstBufCtx->u32BufCnt; ++i) {
        if (pstBufCtx->aulBufferId[i] != pstVideoFrame->ulBufferId)
            continue;

        mpi_vi_set_buf_state(pstBufCtx, i, MPI_VI_BUF_NODE_IN_HW);
        return g_pViOps->queue_buffer(ViDev, ViChn, i);
    }

    return MPI_VI_ERR_INVALID_PARAM;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
