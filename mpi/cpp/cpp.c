#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#include "cpp_api.h"
#include "vi_type.h"
#include "vb_api.h"

#define CppCallback CamCppCallback
#include "cam_cpp.h"
#undef CppCallback

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#define CPP_SUCCESS               0
#define CPP_ERR_INVALID_PARAM    (-1)
#define CPP_ERR_NOT_INIT         (-2)
#define CPP_ERR_NOT_EXIST        (-3)
#define CPP_ERR_ALREADY_EXIST    (-4)
#define CPP_ERR_BAD_STATE        (-5)
#define CPP_ERR_TIMEOUT          (-6)
#define CPP_ERR_NO_BUF           (-7)

#define CPP_PENDING_MAX_PER_GRP  64
#define CPP_DONE_MAX_PER_CHN     64
#define CPP_DEFAULT_OUTBUF_COUNT 4

typedef enum _CppFrameStateE {
    CPP_FRAME_STATE_IDLE = 0,
    CPP_FRAME_STATE_PENDING,
    CPP_FRAME_STATE_DONE,
    CPP_FRAME_STATE_BORROWED,
} CppFrameStateE;

typedef struct _CppPendingRecordS {
    BOOL           bUsed;
    CPP_CHN        CppChn;
    U32            u32FrameId;
    UL             ulOutBufferId;
    VideoFrameInfo stOutFrame;
    VOID          *pUserData;
    S32            s32Result;
    CppFrameStateE eState;
} CppPendingRecordS;

typedef struct _CppDoneNodeS {
    BOOL bValid;
    U32  u32PendingIdx;
} CppDoneNodeS;

typedef struct _CppDoneQueueS {
    CppDoneNodeS    astNodes[CPP_DONE_MAX_PER_CHN];
    U32             u32ReadPos;
    U32             u32WritePos;
    U32             u32Count;
    pthread_mutex_t stLock;
    pthread_cond_t  stCond;
} CppDoneQueueS;

typedef struct _CppGrpCtxS {
    BOOL             bCreated;
    BOOL             bStarted;
    CppGrpAttrS      stGrpAttr;
    CppChnAttrS      stChnAttr[CPP_MAX_CHN_NUM];
    BOOL             bChnEnabled[CPP_MAX_CHN_NUM];
    CppFrameRateCtrlS stFrameRateCtrl[CPP_MAX_CHN_NUM];
    U32              u32FrameRateSeq[CPP_MAX_CHN_NUM];
    CppProcCfgS      stProcCfg;
    CppCallback      pfnCallback;
    pthread_mutex_t  stPendingLock;
    CppPendingRecordS astPending[CPP_PENDING_MAX_PER_GRP];
    CppDoneQueueS    stDoneQueue;
    UL               ulOutPool;
    VbPoolCfg        stOutPoolCfg;
    VideoFrameInfo   stOutFrameTemplate;
} CppGrpCtxS;

static BOOL g_bCppInited = MPP_FALSE;
static CppGrpCtxS g_stCppGrpCtx[CPP_MAX_GRP_NUM];

static U32 cpp_align_up(U32 u32Value, U32 u32Align)
{
    if (u32Align == 0U)
        return u32Value;

    return (u32Value + u32Align - 1U) & ~(u32Align - 1U);
}

static U32 cpp_calc_dwt_plane_length(U32 u32Width, U32 u32Height, U32 u32Level, U32 u32Plane)
{
    U32 u32Divisor = 1U << u32Level;
    U32 u32AlignedWidth = cpp_align_up(u32Width, 64U);
    U32 u32AlignedHeight = cpp_align_up(u32Height, 32U);
    U32 u32PlaneWidth = ((u32AlignedWidth / u32Divisor) * 10U + 7U) / 8U;
    U32 u32PlaneHeight = (u32AlignedHeight / u32Divisor);

    if (u32Plane != 0U)
        u32PlaneHeight /= 2U;

    return cpp_align_up(u32PlaneWidth * u32PlaneHeight, 4096U);
}

static VOID cpp_fill_dwt_planes(IMAGE_BUFFER_S *pstImageBuf, U32 u32BaseWidth, U32 u32BaseHeight,
                                int iFd, void *pBaseVir, U32 *pu32Offset)
{
    U32 u32Level;
    IMAGE_BUFFER_PLANE_S *pastDwt[4] = {
        pstImageBuf->dwt1,
        pstImageBuf->dwt2,
        pstImageBuf->dwt3,
        pstImageBuf->dwt4,
    };

    if ((pstImageBuf == NULL) || (pu32Offset == NULL))
        return;

    for (u32Level = 1U; u32Level <= 4U; ++u32Level) {
        IMAGE_BUFFER_PLANE_S *pstPlane = pastDwt[u32Level - 1U];
        U32 u32Divisor = 1U << u32Level;
        U32 u32Width = ((cpp_align_up(u32BaseWidth, 64U) / u32Divisor) * 10U + 7U) / 8U;
        U32 u32Height = (cpp_align_up(u32BaseHeight, 32U) / u32Divisor);
        U32 u32Plane;

        for (u32Plane = 0U; u32Plane < DWT_MAX_PLANES; ++u32Plane) {
            U32 u32PlaneHeight = (u32Plane == 0U) ? u32Height : (u32Height / 2U);
            U32 u32Length = cpp_calc_dwt_plane_length(u32BaseWidth, u32BaseHeight, u32Level, u32Plane);

            pstPlane[u32Plane].width = u32Width;
            pstPlane[u32Plane].height = u32PlaneHeight;
            pstPlane[u32Plane].stride = u32Width;
            pstPlane[u32Plane].scanline = u32PlaneHeight;
            pstPlane[u32Plane].offset = *pu32Offset;
            pstPlane[u32Plane].length = u32Length;
            pstPlane[u32Plane].virAddr = (pBaseVir != NULL) ? ((char *)pBaseVir + *pu32Offset) : NULL;
            pstPlane[u32Plane].fd = iFd;
            *pu32Offset += u32Length;
        }
    }
}

static PIXEL_FORMAT_E cpp_mpp_to_cam_format(MppPixelFormat ePixelFormat, BOOL bForInput)
{
    switch (ePixelFormat) {
    case MPP_PIXEL_FORMAT_NV12:
        return PIXEL_FORMAT_NV12_DWT;
    case MPP_PIXEL_FORMAT_NV12_P010:
        return PIXEL_FORMAT_P010;
    case MPP_PIXEL_FORMAT_RGB_565:
        return PIXEL_FORMAT_RGB565;
    case MPP_PIXEL_FORMAT_RGB_888:
        return PIXEL_FORMAT_RGB888;
    case MPP_PIXEL_FORMAT_YUYV:
        return PIXEL_FORMAT_YUYV;
    case MPP_PIXEL_FORMAT_YVYU:
        return PIXEL_FORMAT_YVYU;
    case MPP_PIXEL_FORMAT_AFBC_YUV420_8:
        return PIXEL_FORMAT_FBC_DWT;
    default:
        return PIXEL_FORMAT_NV12_DWT;
    }
}

static MppPixelFormat cpp_cam_to_mpp_format(PIXEL_FORMAT_E ePixelFormat)
{
    switch (ePixelFormat) {
    case PIXEL_FORMAT_NV12:
    case PIXEL_FORMAT_NV12_DWT:
        return MPP_PIXEL_FORMAT_NV12;
    case PIXEL_FORMAT_P010:
        return MPP_PIXEL_FORMAT_NV12_P010;
    case PIXEL_FORMAT_RGB565:
        return MPP_PIXEL_FORMAT_RGB_565;
    case PIXEL_FORMAT_RGB888:
        return MPP_PIXEL_FORMAT_RGB_888;
    case PIXEL_FORMAT_YUYV:
        return MPP_PIXEL_FORMAT_YUYV;
    case PIXEL_FORMAT_YVYU:
        return MPP_PIXEL_FORMAT_YVYU;
    case PIXEL_FORMAT_FBC:
        return MPP_PIXEL_FORMAT_AFBC_YUV420_8;
    default:
        return MPP_PIXEL_FORMAT_UNKNOWN;
    }
}

static BOOL cpp_check_grp(CPP_GRP CppGrp)
{
    return (CppGrp >= 0) && (CppGrp < CPP_MAX_GRP_NUM);
}

static BOOL cpp_check_chn(CPP_CHN CppChn)
{
    return (CppChn >= 0) && (CppChn < CPP_MAX_CHN_NUM);
}

static VOID cpp_destroy_out_pool(CppGrpCtxS *pstGrpCtx)
{
    if ((pstGrpCtx == NULL) || (pstGrpCtx->ulOutPool == 0U)) {
        return;
    }

    (void)VB_DestroyPool(pstGrpCtx->ulOutPool);
    pstGrpCtx->ulOutPool = 0U;
    memset(&pstGrpCtx->stOutPoolCfg, 0, sizeof(pstGrpCtx->stOutPoolCfg));
    memset(&pstGrpCtx->stOutFrameTemplate, 0, sizeof(pstGrpCtx->stOutFrameTemplate));
}

static U32 cpp_calc_output_buffer_size(const CppChnAttrS *pstChnAttr)
{
    U32 u32Width;
    U32 u32Height;

    if (pstChnAttr == NULL) {
        return 0U;
    }

    u32Width = pstChnAttr->u32Width;
    u32Height = pstChnAttr->u32Height;

    switch (pstChnAttr->ePixelFormat) {
    case MPP_PIXEL_FORMAT_NV12:
        return cpp_calc_dwt_plane_length(u32Width, u32Height, 0U, 0U) + cpp_calc_dwt_plane_length(u32Width, u32Height, 0U, 1U) + \
               cpp_calc_dwt_plane_length(u32Width, u32Height, 1U, 0U) + cpp_calc_dwt_plane_length(u32Width, u32Height, 1U, 1U) + \
               cpp_calc_dwt_plane_length(u32Width, u32Height, 2U, 0U) + cpp_calc_dwt_plane_length(u32Width, u32Height, 2U, 1U) + \
               cpp_calc_dwt_plane_length(u32Width, u32Height, 3U, 0U) + cpp_calc_dwt_plane_length(u32Width, u32Height, 3U, 1U) + \
               cpp_calc_dwt_plane_length(u32Width, u32Height, 4U, 0U) + cpp_calc_dwt_plane_length(u32Width, u32Height, 4U, 1U);
    case MPP_PIXEL_FORMAT_NV12_P010:
        return u32Width * u32Height * 3U;
    case MPP_PIXEL_FORMAT_RGB_565:
        return u32Width * u32Height * 2U;
    case MPP_PIXEL_FORMAT_RGB_888:
        return u32Width * u32Height * 3U;
    case MPP_PIXEL_FORMAT_YUYV:
    case MPP_PIXEL_FORMAT_YVYU:
        return u32Width * u32Height * 2U;
    default:
        return u32Width * u32Height * 3U / 2U;
    }
}

static VOID cpp_prepare_output_frame_template(const CppChnAttrS *pstChnAttr, VideoFrameInfo *pstFrameTemplate)
{
    U32 u32Stride;
    U32 u32Plane0Size;
    U32 u32Plane1Size;

    if ((pstChnAttr == NULL) || (pstFrameTemplate == NULL)) {
        return;
    }

    memset(pstFrameTemplate, 0, sizeof(*pstFrameTemplate));
    pstFrameTemplate->eFrameType = FRAME_TYPE_CPP;
    pstFrameTemplate->eModId = MPP_ID_CPP;
    pstFrameTemplate->stCommFrameInfo.u32Width = pstChnAttr->u32Width;
    pstFrameTemplate->stCommFrameInfo.u32Height = pstChnAttr->u32Height;
    pstFrameTemplate->stCommFrameInfo.u32Align = 64U;
    pstFrameTemplate->stCommFrameInfo.ePixelFormat = pstChnAttr->ePixelFormat;
    pstFrameTemplate->stCommFrameInfo.eCompressMode = COMPRESS_MODE_NONE;
    pstFrameTemplate->stCppFrameInfo.stCommFrameInfo = pstFrameTemplate->stCommFrameInfo;
    pstFrameTemplate->stVFrame.u32PlaneNum = 2U;
    u32Stride = cpp_align_up(pstChnAttr->u32Width, 64U);
    pstFrameTemplate->stVFrame.u32PlaneStride[0] = u32Stride;
    pstFrameTemplate->stVFrame.u32PlaneStride[1] = u32Stride;
    u32Plane0Size = u32Stride * pstChnAttr->u32Height;
    u32Plane1Size = u32Plane0Size / 2U;
    pstFrameTemplate->stVFrame.u32PlaneSize[0] = u32Plane0Size;
    pstFrameTemplate->stVFrame.u32PlaneSize[1] = u32Plane1Size;
    pstFrameTemplate->stVFrame.u32PlaneSizeValid[0] = u32Plane0Size;
    pstFrameTemplate->stVFrame.u32PlaneSizeValid[1] = u32Plane1Size;
    pstFrameTemplate->stVFrame.u32TotalSize = cpp_calc_output_buffer_size(pstChnAttr);
}

static S32 cpp_create_out_pool(CppGrpCtxS *pstGrpCtx)
{
    if (pstGrpCtx == NULL) {
        return CPP_ERR_INVALID_PARAM;
    }

    if (pstGrpCtx->ulOutPool != 0U) {
        return CPP_SUCCESS;
    }

    pstGrpCtx->stOutPoolCfg.u32BufCnt = CPP_DEFAULT_OUTBUF_COUNT;
    pstGrpCtx->stOutPoolCfg.u32BufSize = cpp_calc_output_buffer_size(&pstGrpCtx->stChnAttr[0]);
    pstGrpCtx->stOutPoolCfg.eRemapMode = VBUF_REMAP_MODE_NOCACHE;
    pstGrpCtx->stOutPoolCfg.eModId = MPP_ID_CPP;

    if (pstGrpCtx->stOutPoolCfg.u32BufSize == 0U) {
        return CPP_ERR_INVALID_PARAM;
    }

    pstGrpCtx->ulOutPool = VB_CreatePool(&pstGrpCtx->stOutPoolCfg);
    if (pstGrpCtx->ulOutPool == 0U) {
        return CPP_ERR_NO_BUF;
    }

    cpp_prepare_output_frame_template(&pstGrpCtx->stChnAttr[0], &pstGrpCtx->stOutFrameTemplate);
    pstGrpCtx->stOutFrameTemplate.ulPoolId = pstGrpCtx->ulOutPool;

    if (VB_SetFrameInfo(pstGrpCtx->ulOutPool, &pstGrpCtx->stOutFrameTemplate) != 0) {
        VB_DestroyPool(pstGrpCtx->ulOutPool);
        pstGrpCtx->ulOutPool = 0U;
        return CPP_ERR_BAD_STATE;
    }

    return CPP_SUCCESS;
}

static VOID cpp_reset_grp_ctx(CppGrpCtxS *pstGrpCtx)
{
    if (pstGrpCtx == NULL) {
        return;
    }

    memset(pstGrpCtx, 0, sizeof(*pstGrpCtx));
    pstGrpCtx->stGrpAttr.eProcessMode = CPP_PROCESS_MODE_FRAME;
    pstGrpCtx->stProcCfg.eRotation = ROTATION_0;
    pstGrpCtx->stChnAttr[0].bEnable = MPP_TRUE;
    pstGrpCtx->bChnEnabled[0] = MPP_TRUE;
    pstGrpCtx->stFrameRateCtrl[0].u32InputFrameStep = 1U;
    pstGrpCtx->stFrameRateCtrl[0].u32OutputFrameStep = 1U;
}

static VOID cpp_init_frame_rate_ctrl(CppGrpCtxS *pstGrpCtx, CPP_CHN CppChn)
{
    if ((pstGrpCtx == NULL) || !cpp_check_chn(CppChn)) {
        return;
    }

    pstGrpCtx->stFrameRateCtrl[CppChn].u32InputFrameStep = 1U;
    pstGrpCtx->stFrameRateCtrl[CppChn].u32OutputFrameStep = 1U;
    pstGrpCtx->u32FrameRateSeq[CppChn] = 0U;
}

static BOOL cpp_should_keep_frame(CppGrpCtxS *pstGrpCtx, CPP_CHN CppChn)
{
    U32 u32InputStep;
    U32 u32OutputStep;
    U32 u32CurIdx;

    if ((pstGrpCtx == NULL) || !cpp_check_chn(CppChn)) {
        return MPP_FALSE;
    }

    u32InputStep = pstGrpCtx->stFrameRateCtrl[CppChn].u32InputFrameStep;
    u32OutputStep = pstGrpCtx->stFrameRateCtrl[CppChn].u32OutputFrameStep;

    if ((u32InputStep == 0U) || (u32OutputStep == 0U)) {
        return MPP_FALSE;
    }

    if (u32OutputStep >= u32InputStep) {
        return MPP_TRUE;
    }

    u32CurIdx = pstGrpCtx->u32FrameRateSeq[CppChn] % u32InputStep;
    pstGrpCtx->u32FrameRateSeq[CppChn]++;

    return (u32CurIdx < u32OutputStep) ? MPP_TRUE : MPP_FALSE;
}

static VOID cpp_init_grp_runtime(CppGrpCtxS *pstGrpCtx)
{
    if (pstGrpCtx == NULL) {
        return;
    }

    pthread_mutex_init(&pstGrpCtx->stPendingLock, NULL);
    memset(&pstGrpCtx->stDoneQueue, 0, sizeof(pstGrpCtx->stDoneQueue));
    pthread_mutex_init(&pstGrpCtx->stDoneQueue.stLock, NULL);
    pthread_cond_init(&pstGrpCtx->stDoneQueue.stCond, NULL);
}

static VOID cpp_flush_grp_records(CppGrpCtxS *pstGrpCtx)
{
    U32 i;

    if (pstGrpCtx == NULL) {
        return;
    }

    pthread_mutex_lock(&pstGrpCtx->stPendingLock);
    for (i = 0; i < CPP_PENDING_MAX_PER_GRP; ++i) {
        if (pstGrpCtx->astPending[i].bUsed && pstGrpCtx->astPending[i].ulOutBufferId != 0U) {
            (void)VB_ReleaseBuffer(pstGrpCtx->astPending[i].ulOutBufferId);
        }
    }
    memset(pstGrpCtx->astPending, 0, sizeof(pstGrpCtx->astPending));
    pthread_mutex_unlock(&pstGrpCtx->stPendingLock);

    pthread_mutex_lock(&pstGrpCtx->stDoneQueue.stLock);
    memset(pstGrpCtx->stDoneQueue.astNodes, 0, sizeof(pstGrpCtx->stDoneQueue.astNodes));
    pstGrpCtx->stDoneQueue.u32ReadPos = 0U;
    pstGrpCtx->stDoneQueue.u32WritePos = 0U;
    pstGrpCtx->stDoneQueue.u32Count = 0U;
    pthread_cond_broadcast(&pstGrpCtx->stDoneQueue.stCond);
    pthread_mutex_unlock(&pstGrpCtx->stDoneQueue.stLock);
}

static S32 cpp_pending_alloc(CppGrpCtxS *pstGrpCtx, U32 *pu32Idx)
{
    U32 i;

    if ((pstGrpCtx == NULL) || (pu32Idx == NULL)) {
        return CPP_ERR_INVALID_PARAM;
    }

    for (i = 0; i < CPP_PENDING_MAX_PER_GRP; ++i) {
        if (pstGrpCtx->astPending[i].bUsed != MPP_TRUE) {
            memset(&pstGrpCtx->astPending[i], 0, sizeof(pstGrpCtx->astPending[i]));
            pstGrpCtx->astPending[i].bUsed = MPP_TRUE;
            pstGrpCtx->astPending[i].eState = CPP_FRAME_STATE_PENDING;
            *pu32Idx = i;
            return CPP_SUCCESS;
        }
    }

    return CPP_ERR_NO_BUF;
}

static CppPendingRecordS *cpp_pending_find_by_frameid(CppGrpCtxS *pstGrpCtx, U32 u32FrameId)
{
    U32 i;

    if (pstGrpCtx == NULL) {
        return NULL;
    }

    for (i = 0; i < CPP_PENDING_MAX_PER_GRP; ++i) {
        if ((pstGrpCtx->astPending[i].bUsed == MPP_TRUE) &&
            (pstGrpCtx->astPending[i].u32FrameId == u32FrameId) &&
            (pstGrpCtx->astPending[i].eState == CPP_FRAME_STATE_PENDING)) {
            return &pstGrpCtx->astPending[i];
        }
    }

    return NULL;
}

static CppPendingRecordS *cpp_pending_find_by_outfd(CppGrpCtxS *pstGrpCtx, const IMAGE_BUFFER_S *cppBuf)
{
    U32 i;
    int iFd;

    if ((pstGrpCtx == NULL) || (cppBuf == NULL) || (cppBuf->numPlanes == 0U)) {
        return NULL;
    }

    iFd = cppBuf->planes[0].fd;
    if (iFd < 0) {
        return NULL;
    }

    for (i = 0; i < CPP_PENDING_MAX_PER_GRP; ++i) {
        if ((pstGrpCtx->astPending[i].bUsed == MPP_TRUE) &&
            (pstGrpCtx->astPending[i].eState == CPP_FRAME_STATE_PENDING) &&
            ((int)pstGrpCtx->astPending[i].stOutFrame.stVFrame.u32Fd[0] == iFd)) {
            return &pstGrpCtx->astPending[i];
        }
    }

    return NULL;
}

static CppPendingRecordS *cpp_pending_find_borrowed(CppGrpCtxS *pstGrpCtx, const VideoFrameInfo *pstVideoFrame)
{
    U32 i;
    int iFd = -1;

    if ((pstGrpCtx == NULL) || (pstVideoFrame == NULL)) {
        return NULL;
    }

    iFd = (int)pstVideoFrame->stVFrame.u32Fd[0];

    for (i = 0; i < CPP_PENDING_MAX_PER_GRP; ++i) {
        if ((pstGrpCtx->astPending[i].bUsed == MPP_TRUE) &&
            (pstGrpCtx->astPending[i].eState == CPP_FRAME_STATE_BORROWED) &&
            ((iFd >= 0) && ((int)pstGrpCtx->astPending[i].stOutFrame.stVFrame.u32Fd[0] == iFd))) {
            return &pstGrpCtx->astPending[i];
        }
    }

    return NULL;
}

static S32 cpp_done_queue_push(CppDoneQueueS *pstQueue, U32 u32PendingIdx)
{
    if (pstQueue == NULL) {
        return CPP_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&pstQueue->stLock);
    if (pstQueue->u32Count >= CPP_DONE_MAX_PER_CHN) {
        pthread_mutex_unlock(&pstQueue->stLock);
        return CPP_ERR_NO_BUF;
    }

    pstQueue->astNodes[pstQueue->u32WritePos].bValid = MPP_TRUE;
    pstQueue->astNodes[pstQueue->u32WritePos].u32PendingIdx = u32PendingIdx;
    pstQueue->u32WritePos = (pstQueue->u32WritePos + 1U) % CPP_DONE_MAX_PER_CHN;
    pstQueue->u32Count++;
    pthread_cond_signal(&pstQueue->stCond);
    pthread_mutex_unlock(&pstQueue->stLock);
    return CPP_SUCCESS;
}

static S32 cpp_done_queue_pop(CppDoneQueueS *pstQueue, U32 *pu32PendingIdx, S32 s32MilliSec)
{
    S32 s32Ret = CPP_SUCCESS;

    if ((pstQueue == NULL) || (pu32PendingIdx == NULL)) {
        return CPP_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&pstQueue->stLock);
    while (pstQueue->u32Count == 0U) {
        if (s32MilliSec == 0) {
            pthread_mutex_unlock(&pstQueue->stLock);
            return CPP_ERR_TIMEOUT;
        }

        if (s32MilliSec < 0) {
            (void)pthread_cond_wait(&pstQueue->stCond, &pstQueue->stLock);
            continue;
        }

        {
            struct timespec stAbsTime;
            struct timespec stNow;
            clock_gettime(CLOCK_REALTIME, &stNow);
            stAbsTime.tv_sec = stNow.tv_sec + (s32MilliSec / 1000);
            stAbsTime.tv_nsec = stNow.tv_nsec + ((long)(s32MilliSec % 1000) * 1000000L);
            if (stAbsTime.tv_nsec >= 1000000000L) {
                stAbsTime.tv_sec += 1;
                stAbsTime.tv_nsec -= 1000000000L;
            }

            s32Ret = pthread_cond_timedwait(&pstQueue->stCond, &pstQueue->stLock, &stAbsTime);
            if (s32Ret == ETIMEDOUT) {
                pthread_mutex_unlock(&pstQueue->stLock);
                return CPP_ERR_TIMEOUT;
            }
        }
    }

    *pu32PendingIdx = pstQueue->astNodes[pstQueue->u32ReadPos].u32PendingIdx;
    pstQueue->astNodes[pstQueue->u32ReadPos].bValid = MPP_FALSE;
    pstQueue->u32ReadPos = (pstQueue->u32ReadPos + 1U) % CPP_DONE_MAX_PER_CHN;
    pstQueue->u32Count--;
    pthread_mutex_unlock(&pstQueue->stLock);
    return CPP_SUCCESS;
}

static S32 cpp_check_inited(VOID)
{
    return g_bCppInited ? CPP_SUCCESS : CPP_ERR_NOT_INIT;
}

static S32 cpp_check_grp_created(CPP_GRP CppGrp, CppGrpCtxS **ppstGrpCtx)
{
    if (!cpp_check_grp(CppGrp)) {
        return CPP_ERR_INVALID_PARAM;
    }

    if (cpp_check_inited() != CPP_SUCCESS) {
        return CPP_ERR_NOT_INIT;
    }

    if (!g_stCppGrpCtx[CppGrp].bCreated) {
        return CPP_ERR_NOT_EXIST;
    }

    if (ppstGrpCtx != NULL) {
        *ppstGrpCtx = &g_stCppGrpCtx[CppGrp];
    }

    return CPP_SUCCESS;
}

static S32 cpp_validate_grp_attr(const CppGrpAttrS *pstGrpAttr)
{
    if (pstGrpAttr == NULL) {
        return CPP_ERR_INVALID_PARAM;
    }

    if ((pstGrpAttr->u32Width < CPP_MIN_WIDTH) || (pstGrpAttr->u32Width > CPP_MAX_WIDTH) ||
        (pstGrpAttr->u32Height < CPP_MIN_HEIGHT) || (pstGrpAttr->u32Height > CPP_MAX_HEIGHT)) {
        return CPP_ERR_INVALID_PARAM;
    }

    if ((pstGrpAttr->eProcessMode <= CPP_PROCESS_MODE_INVALID) ||
        (pstGrpAttr->eProcessMode >= CPP_PROCESS_MODE_MAX)) {
        return CPP_ERR_INVALID_PARAM;
    }

    return CPP_SUCCESS;
}

static S32 cpp_validate_chn_attr(const CppChnAttrS *pstChnAttr)
{
    if (pstChnAttr == NULL) {
        return CPP_ERR_INVALID_PARAM;
    }

    if ((pstChnAttr->u32Width == 0U) || (pstChnAttr->u32Height == 0U)) {
        return CPP_ERR_INVALID_PARAM;
    }

    return CPP_SUCCESS;
}

static VOID cpp_fill_cam_buffer_from_frame(const VideoFrameInfo *pstFrame,
                                           IMAGE_BUFFER_S *pstImageBuf,
                                           BOOL bForInput)
{
    U32 i;
    U32 u32PlaneOffset = 0;

    memset(pstImageBuf, 0, sizeof(*pstImageBuf));
    pstImageBuf->size.width = pstFrame->stCommFrameInfo.u32Width;
    pstImageBuf->size.height = pstFrame->stCommFrameInfo.u32Height;
    pstImageBuf->format = cpp_mpp_to_cam_format(pstFrame->stCommFrameInfo.ePixelFormat, bForInput);
    pstImageBuf->numPlanes = pstFrame->stVFrame.u32PlaneNum;
    pstImageBuf->frameId = (int)pstFrame->u32Idx;
    pstImageBuf->timeStamp = pstFrame->stVFrame.u64PTS;
    pstImageBuf->type = 2;

    for (i = 0; (i < pstImageBuf->numPlanes) && (i < IMAGE_BUFFER_MAX_PLANES); ++i) {
        pstImageBuf->planes[i].width = pstFrame->stCommFrameInfo.u32Width;
        pstImageBuf->planes[i].height = (i == 0U) ?
            pstFrame->stCommFrameInfo.u32Height :
            (pstFrame->stCommFrameInfo.u32Height / 2U);
        pstImageBuf->planes[i].stride = pstFrame->stVFrame.u32PlaneStride[i];
        pstImageBuf->planes[i].scanline = pstImageBuf->planes[i].height;
        pstImageBuf->planes[i].length = pstFrame->stVFrame.u32PlaneSize[i];
        if (pstFrame->stVFrame.ulPlaneVirAddr[i] != 0U) {
            pstImageBuf->planes[i].virAddr = (void *)pstFrame->stVFrame.ulPlaneVirAddr[i];
        } else if (pstFrame->stVFrame.ulPlaneVirAddr[0] != 0U) {
            pstImageBuf->planes[i].virAddr = (void *)((uintptr_t)pstFrame->stVFrame.ulPlaneVirAddr[0] + u32PlaneOffset);
        }
        pstImageBuf->planes[i].fd = (int)pstFrame->stVFrame.u32Fd[0];
        pstImageBuf->planes[i].offset = u32PlaneOffset;
        u32PlaneOffset += pstFrame->stVFrame.u32PlaneSize[i];
    }

    pstImageBuf->m.fd = (int)pstFrame->stVFrame.u32Fd[0];

    if ((pstImageBuf->format == PIXEL_FORMAT_NV12_DWT) || (pstImageBuf->format == PIXEL_FORMAT_FBC_DWT)) {
        cpp_fill_dwt_planes(pstImageBuf,
                            pstImageBuf->size.width,
                            pstImageBuf->size.height,
                            pstImageBuf->m.fd,
                            pstImageBuf->planes[0].virAddr,
                            &u32PlaneOffset);
    }
}

static VOID cpp_fill_frame_from_cam_buffer(const IMAGE_BUFFER_S *pstImageBuf,
                                           const VideoFrameInfo *pstRefFrame,
                                           VideoFrameInfo *pstFrame)
{
    U32 i;

    memset(pstFrame, 0, sizeof(*pstFrame));
    if (pstRefFrame != NULL) {
        *pstFrame = *pstRefFrame;
    }

    pstFrame->eFrameType = FRAME_TYPE_CPP;
    pstFrame->eModId = MPP_ID_CPP;
    pstFrame->stCommFrameInfo.u32Width = pstImageBuf->size.width;
    pstFrame->stCommFrameInfo.u32Height = pstImageBuf->size.height;
    pstFrame->stCommFrameInfo.ePixelFormat = cpp_cam_to_mpp_format(pstImageBuf->format);
    pstFrame->stCppFrameInfo.stCommFrameInfo = pstFrame->stCommFrameInfo;
    pstFrame->stVFrame.u32PlaneNum = pstImageBuf->numPlanes;
    pstFrame->stVFrame.u64PTS = pstImageBuf->timeStamp;

    for (i = 0; (i < pstImageBuf->numPlanes) && (i < FRAME_MAX_PLANE); ++i) {
        pstFrame->stVFrame.u32PlaneStride[i] = pstImageBuf->planes[i].stride;
        pstFrame->stVFrame.u32PlaneSize[i] = pstImageBuf->planes[i].length;
        pstFrame->stVFrame.u32PlaneSizeValid[i] = pstImageBuf->planes[i].length;
        pstFrame->stVFrame.ulPlaneVirAddr[i] = (UL)pstImageBuf->planes[i].virAddr;
        pstFrame->stVFrame.u32Fd[i] = (UL)pstImageBuf->planes[i].fd;
    }
}

static VOID cpp_dump_cam_buffer(const char *pszTag, const IMAGE_BUFFER_S *pstImageBuf)
{
    U32 i;
    U32 u32Level;
    const IMAGE_BUFFER_PLANE_S *pastDwt[4];

    if ((pszTag == NULL) || (pstImageBuf == NULL)) {
        return;
    }

    pastDwt[0] = pstImageBuf->dwt1;
    pastDwt[1] = pstImageBuf->dwt2;
    pastDwt[2] = pstImageBuf->dwt3;
    pastDwt[3] = pstImageBuf->dwt4;

        printf("[cpp][%s] frameId=%d ts=%llu fmt=%d type=%d size=%ux%u planes=%u\n",
           pszTag,
           pstImageBuf->frameId,
           (unsigned long long)pstImageBuf->timeStamp,
           pstImageBuf->format,
            pstImageBuf->type,
           pstImageBuf->size.width,
           pstImageBuf->size.height,
           pstImageBuf->numPlanes);

    for (i = 0; (i < pstImageBuf->numPlanes) && (i < IMAGE_BUFFER_MAX_PLANES); ++i) {
        printf("[cpp][%s][plane%u] wxh=%ux%u stride=%u scanline=%u len=%u fd=%d vir=%p off=%u\n",
               pszTag,
               i,
               pstImageBuf->planes[i].width,
               pstImageBuf->planes[i].height,
               pstImageBuf->planes[i].stride,
               pstImageBuf->planes[i].scanline,
               pstImageBuf->planes[i].length,
               pstImageBuf->planes[i].fd,
               pstImageBuf->planes[i].virAddr,
               pstImageBuf->planes[i].offset);
    }

    if ((pstImageBuf->format == PIXEL_FORMAT_NV12_DWT) || (pstImageBuf->format == PIXEL_FORMAT_FBC_DWT)) {
        for (u32Level = 0U; u32Level < 4U; ++u32Level) {
            for (i = 0U; i < DWT_MAX_PLANES; ++i) {
                printf("[cpp][%s][dwt%u][plane%u] wxh=%ux%u stride=%u scanline=%u len=%u fd=%d vir=%p off=%u\n",
                       pszTag,
                       u32Level + 1U,
                       i,
                       pastDwt[u32Level][i].width,
                       pastDwt[u32Level][i].height,
                       pastDwt[u32Level][i].stride,
                       pastDwt[u32Level][i].scanline,
                       pastDwt[u32Level][i].length,
                       pastDwt[u32Level][i].fd,
                       pastDwt[u32Level][i].virAddr,
                       pastDwt[u32Level][i].offset);
            }
        }
    }
}

static VOID cpp_dump_grp_attr(const char *pszTag, const CPP_GRP_ATTR_S *pstGrpAttr)
{
    if ((pszTag == NULL) || (pstGrpAttr == NULL)) {
        return;
    }
}

static VOID cpp_dump_frame_info(const char *pszTag, const FRAME_INFO_S *pstFrameInfo)
{
    if ((pszTag == NULL) || (pstFrameInfo == NULL)) {
        return;
    }
}

static VOID cpp_fill_cam_frame_info(FRAME_INFO_S *pstDst, const ViFrameMetaInfo *pstSrc, U32 u32FrameId)
{
    if (pstDst == NULL) {
        return;
    }

    memset(pstDst, 0, sizeof(*pstDst));
    pstDst->frameId = (int)u32FrameId;

    if (pstSrc == NULL) {
        return;
    }

    pstDst->frameId = (int)pstSrc->u32FrameId;
    pstDst->sensorExposureTime = (S64)pstSrc->u32ExpTime[0];
    pstDst->sensorVts = (S32)pstSrc->u32ExpLine[0];
    pstDst->snsAGain = (S32)pstSrc->u32Again[0];
    pstDst->snsDGain = (S32)pstSrc->u32Dgain[0];
    pstDst->imageTGain = (S32)pstSrc->u32IspDgain[0];
    pstDst->curCorrelationCT = (S32)pstSrc->u32ColorTemp;
    pstDst->awbRgain = (S32)pstSrc->u32RGain;
    pstDst->awbBgain = (S32)pstSrc->u32BGain;
    pstDst->currentCcm00 = (S32)pstSrc->u32CCM[0];
    pstDst->currentCcm01 = (S32)pstSrc->u32CCM[1];
    pstDst->currentCcm02 = (S32)pstSrc->u32CCM[2];
    pstDst->currentCcm10 = (S32)pstSrc->u32CCM[3];
    pstDst->currentCcm11 = (S32)pstSrc->u32CCM[4];
    pstDst->currentCcm12 = (S32)pstSrc->u32CCM[5];
    pstDst->currentCcm20 = (S32)pstSrc->u32CCM[6];
    pstDst->currentCcm21 = (S32)pstSrc->u32CCM[7];
    pstDst->currentCcm22 = (S32)pstSrc->u32CCM[8];
    pstDst->blc12b = (S32)pstSrc->u32BlackLevel[0];
    pstDst->aeStableFlag = pstSrc->u8AeStable;
    pstDst->awbStableFlag = pstSrc->u8AwbStable;
}

static int32_t cpp_internal_callback(MPP_CHN_S mppCpp, const IMAGE_BUFFER_S *cppBuf, char success)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    CppPendingRecordS *pstPending = NULL;
    U32 u32PendingIdx;
    S32 s32Ret;

    if ((cppBuf == NULL) || !cpp_check_grp((CPP_GRP)mppCpp.devId)) {
        return CPP_ERR_INVALID_PARAM;
    }

    s32Ret = cpp_check_grp_created((CPP_GRP)mppCpp.devId, &pstGrpCtx);
    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    pthread_mutex_lock(&pstGrpCtx->stPendingLock);
    pstPending = cpp_pending_find_by_outfd(pstGrpCtx, cppBuf);
    if (pstPending == NULL) {
        pthread_mutex_unlock(&pstGrpCtx->stPendingLock);
        return CPP_ERR_NOT_EXIST;
    }

    cpp_fill_frame_from_cam_buffer(cppBuf, &pstPending->stOutFrame, &pstPending->stOutFrame);
    pstPending->stOutFrame.ulBufferId = pstPending->ulOutBufferId;
    pstPending->s32Result = success ? CPP_SUCCESS : CPP_ERR_BAD_STATE;
    pstPending->eState = CPP_FRAME_STATE_DONE;
    u32PendingIdx = (U32)(pstPending - pstGrpCtx->astPending);
    pthread_mutex_unlock(&pstGrpCtx->stPendingLock);

    s32Ret = cpp_done_queue_push(&pstGrpCtx->stDoneQueue, u32PendingIdx);
    if (s32Ret != CPP_SUCCESS) {
        pthread_mutex_lock(&pstGrpCtx->stPendingLock);
        if (pstPending->ulOutBufferId != 0U) {
            (void)VB_ReleaseBuffer(pstPending->ulOutBufferId);
        }
        memset(pstPending, 0, sizeof(*pstPending));
        pthread_mutex_unlock(&pstGrpCtx->stPendingLock);
        return s32Ret;
    }

    return CPP_SUCCESS;
}

S32 CPP_Init(VOID)
{
    S32 i;

    if (g_bCppInited) {
        return CPP_SUCCESS;
    }

    for (i = 0; i < CPP_MAX_GRP_NUM; ++i) {
        cpp_reset_grp_ctx(&g_stCppGrpCtx[i]);
        cpp_init_grp_runtime(&g_stCppGrpCtx[i]);
    }

    g_bCppInited = MPP_TRUE;
    return CPP_SUCCESS;
}

S32 CPP_DeInit(VOID)
{
    S32 i;

    if (!g_bCppInited) {
        return CPP_SUCCESS;
    }

    for (i = 0; i < CPP_MAX_GRP_NUM; ++i) {
        cpp_flush_grp_records(&g_stCppGrpCtx[i]);
        cpp_reset_grp_ctx(&g_stCppGrpCtx[i]);
        cpp_init_grp_runtime(&g_stCppGrpCtx[i]);
    }

    g_bCppInited = MPP_FALSE;
    return CPP_SUCCESS;
}

S32 CPP_CreateGrp(CPP_GRP CppGrp)
{
    S32 s32Ret;

    if (cpp_check_inited() != CPP_SUCCESS) {
        return CPP_ERR_NOT_INIT;
    }

    if (!cpp_check_grp(CppGrp)) {
        return CPP_ERR_INVALID_PARAM;
    }

    if (g_stCppGrpCtx[CppGrp].bCreated) {
        return CPP_ERR_ALREADY_EXIST;
    }

    cpp_reset_grp_ctx(&g_stCppGrpCtx[CppGrp]);
    cpp_init_grp_runtime(&g_stCppGrpCtx[CppGrp]);

    s32Ret = cam_cpp_create_grp((uint32_t)CppGrp);
    if (s32Ret != 0) {
        return s32Ret;
    }

    s32Ret = cam_cpp_set_callback((uint32_t)CppGrp, (CamCppCallback)cpp_internal_callback);
    if (s32Ret != 0) {
        (void)cam_cpp_destroy_grp((uint32_t)CppGrp);
        return s32Ret;
    }

    g_stCppGrpCtx[CppGrp].bCreated = MPP_TRUE;
    return CPP_SUCCESS;
}

S32 CPP_DestroyGrp(CPP_GRP CppGrp)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    S32 s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);

    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    s32Ret = cam_cpp_destroy_grp((uint32_t)CppGrp);
    if (s32Ret != 0) {
        return s32Ret;
    }

    cpp_flush_grp_records(pstGrpCtx);
    cpp_destroy_out_pool(pstGrpCtx);
    cpp_reset_grp_ctx(pstGrpCtx);
    return CPP_SUCCESS;
}

S32 CPP_SetGrpAttr(CPP_GRP CppGrp, const CppGrpAttrS *pstGrpAttr)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    CPP_GRP_ATTR_S stCamAttr;
    S32 s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);

    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    s32Ret = cpp_validate_grp_attr(pstGrpAttr);
    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    memset(&stCamAttr, 0, sizeof(stCamAttr));
    stCamAttr.width = pstGrpAttr->u32Width;
    stCamAttr.height = pstGrpAttr->u32Height;
    stCamAttr.format = cpp_mpp_to_cam_format(pstGrpAttr->ePixelFormat, MPP_TRUE);
    stCamAttr.mode = (pstGrpAttr->eProcessMode == CPP_PROCESS_MODE_SLICE) ? CPP_GRP_SLICE_MODE : CPP_GRP_FRAME_MODE;

    s32Ret = cam_cpp_set_grp_attr((uint32_t)CppGrp, &stCamAttr);
    if (s32Ret != 0) {
        return s32Ret;
    }

    pstGrpCtx->stGrpAttr = *pstGrpAttr;
    return CPP_SUCCESS;
}

S32 CPP_GetGrpAttr(CPP_GRP CppGrp, CppGrpAttrS *pstGrpAttr)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    CPP_GRP_ATTR_S stCamAttr;
    S32 s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);

    if ((s32Ret != CPP_SUCCESS) || (pstGrpAttr == NULL)) {
        return (s32Ret == CPP_SUCCESS) ? CPP_ERR_INVALID_PARAM : s32Ret;
    }

    memset(&stCamAttr, 0, sizeof(stCamAttr));
    s32Ret = cam_cpp_get_grp_attr((uint32_t)CppGrp, &stCamAttr);
    if (s32Ret != 0) {
        return s32Ret;
    }

    pstGrpCtx->stGrpAttr.u32Width = stCamAttr.width;
    pstGrpCtx->stGrpAttr.u32Height = stCamAttr.height;
    pstGrpCtx->stGrpAttr.ePixelFormat = cpp_cam_to_mpp_format(stCamAttr.format);
    pstGrpCtx->stGrpAttr.eProcessMode = (stCamAttr.mode == CPP_GRP_SLICE_MODE) ? CPP_PROCESS_MODE_SLICE : CPP_PROCESS_MODE_FRAME;
    *pstGrpAttr = pstGrpCtx->stGrpAttr;
    return CPP_SUCCESS;
}

S32 CPP_SetCallback(CPP_GRP CppGrp, CppCallback pfnCallback)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    S32 s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);

    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    pstGrpCtx->pfnCallback = pfnCallback;
    return CPP_SUCCESS;
}

S32 CPP_StartGrp(CPP_GRP CppGrp)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    S32 s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);

    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    s32Ret = cam_cpp_start_grp((uint32_t)CppGrp);
    if (s32Ret != 0) {
        return s32Ret;
    }

    pstGrpCtx->bStarted = MPP_TRUE;
    return CPP_SUCCESS;
}

S32 CPP_StopGrp(CPP_GRP CppGrp)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    S32 s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);

    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    s32Ret = cam_cpp_stop_grp((uint32_t)CppGrp);
    if (s32Ret != 0) {
        return s32Ret;
    }

    pstGrpCtx->bStarted = MPP_FALSE;
    cpp_flush_grp_records(pstGrpCtx);
    return CPP_SUCCESS;
}

S32 CPP_SetAttr(CPP_GRP CppGrp, const CppChnAttrS *pstChnAttr)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    S32 s32Ret;

    s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);
    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    s32Ret = cpp_validate_chn_attr(pstChnAttr);
    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    if (pstGrpCtx->bStarted) {
        return CPP_ERR_BAD_STATE;
    }

    pstGrpCtx->stChnAttr[0] = *pstChnAttr;
    pstGrpCtx->bChnEnabled[0] = pstChnAttr->bEnable;

    if (pstChnAttr->bEnable) {
        s32Ret = cpp_create_out_pool(pstGrpCtx);
        if (s32Ret != CPP_SUCCESS) {
            return s32Ret;
        }
    }

    return CPP_SUCCESS;
}

S32 CPP_GetAttr(CPP_GRP CppGrp, CppChnAttrS *pstChnAttr)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    S32 s32Ret;

    if (pstChnAttr == NULL) {
        return CPP_ERR_INVALID_PARAM;
    }

    s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);
    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    *pstChnAttr = pstGrpCtx->stChnAttr[0];
    return CPP_SUCCESS;
}

S32 CPP_SetFrameRate(CPP_GRP CppGrp, const CppFrameRateCtrlS *pstFrameRateCtrl)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    S32 s32Ret;

    if (pstFrameRateCtrl == NULL) {
        return CPP_ERR_INVALID_PARAM;
    }

    if ((pstFrameRateCtrl->u32InputFrameStep == 0U) ||
        (pstFrameRateCtrl->u32OutputFrameStep == 0U) ||
        (pstFrameRateCtrl->u32OutputFrameStep > pstFrameRateCtrl->u32InputFrameStep)) {
        return CPP_ERR_INVALID_PARAM;
    }

    s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);
    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    pstGrpCtx->stFrameRateCtrl[0] = *pstFrameRateCtrl;
    pstGrpCtx->u32FrameRateSeq[0] = 0U;
    return CPP_SUCCESS;
}

S32 CPP_GetFrameRate(CPP_GRP CppGrp, CppFrameRateCtrlS *pstFrameRateCtrl)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    S32 s32Ret;

    if (pstFrameRateCtrl == NULL) {
        return CPP_ERR_INVALID_PARAM;
    }

    s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);
    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    *pstFrameRateCtrl = pstGrpCtx->stFrameRateCtrl[0];
    return CPP_SUCCESS;
}

S32 CPP_Enable(CPP_GRP CppGrp)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    S32 s32Ret;

    s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);
    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    if (pstGrpCtx->ulOutPool == 0U) {
        s32Ret = cpp_create_out_pool(pstGrpCtx);
        if (s32Ret != CPP_SUCCESS) {
            return s32Ret;
        }
    }

    pstGrpCtx->bChnEnabled[0] = MPP_TRUE;
    pstGrpCtx->stChnAttr[0].bEnable = MPP_TRUE;
    return CPP_SUCCESS;
}

S32 CPP_Disable(CPP_GRP CppGrp)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    S32 s32Ret;

    s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);
    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    pstGrpCtx->bChnEnabled[0] = MPP_FALSE;
    pstGrpCtx->stChnAttr[0].bEnable = MPP_FALSE;
    return CPP_SUCCESS;
}

S32 CPP_SendFrame(CPP_GRP CppGrp,
                  const VideoFrameInfo *pstInFrame,
                  U32 u32FrameId,
                  VOID *pUserData)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    IMAGE_BUFFER_S stInBuf;
    IMAGE_BUFFER_S stOutBuf;
    VideoFrameInfo stOutFrame;
    FRAME_INFO_S stFrameInfo;
    const ViFrameMetaInfo *pstFrameInfo = NULL;
    U32 u32PendingIdx;
    UL ulOutBuffer;
    VOID *pOutVirAddr = NULL;
    S32 s32OutFd = -1;
    S32 s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);

    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    if (!pstGrpCtx->bStarted || !pstGrpCtx->bChnEnabled[0]) {
        return CPP_ERR_BAD_STATE;
    }

    if (pstInFrame == NULL) {
        return CPP_ERR_INVALID_PARAM;
    }

    if (cpp_should_keep_frame(pstGrpCtx, 0) != MPP_TRUE) {
        return CPP_SUCCESS;
    }

    if (pstGrpCtx->ulOutPool == 0U) {
        s32Ret = cpp_create_out_pool(pstGrpCtx);
        if (s32Ret != CPP_SUCCESS) {
            return s32Ret;
        }
    }

    ulOutBuffer = VB_GetBuffer(pstGrpCtx->ulOutPool, 0);
    if (ulOutBuffer == 0U) {
        return CPP_ERR_NO_BUF;
    }

    memset(&stOutFrame, 0, sizeof(stOutFrame));
    s32Ret = VB_GetFrameInfo(ulOutBuffer, &stOutFrame);
    if (s32Ret != 0) {
        (void)VB_ReleaseBuffer(ulOutBuffer);
        return s32Ret;
    }

    stOutFrame.eFrameType = FRAME_TYPE_CPP;
    stOutFrame.eModId = MPP_ID_CPP;
    stOutFrame.u32Idx = u32FrameId;
    stOutFrame.ulPoolId = pstGrpCtx->ulOutPool;
    stOutFrame.ulBufferId = ulOutBuffer;
    stOutFrame.stCppFrameInfo.stCommFrameInfo = stOutFrame.stCommFrameInfo;

    s32Ret = VB_GetDmaBufFd(ulOutBuffer, &s32OutFd);
    if (s32Ret != 0) {
        (void)VB_ReleaseBuffer(ulOutBuffer);
        return s32Ret;
    }

    s32Ret = VB_GetVirAddr(ulOutBuffer, &pOutVirAddr);
    if (s32Ret != 0) {
        (void)VB_ReleaseBuffer(ulOutBuffer);
        return s32Ret;
    }

    if (stOutFrame.stVFrame.u32PlaneNum > 0U) {
        stOutFrame.stVFrame.u32Fd[0] = (U32)s32OutFd;
        stOutFrame.stVFrame.ulPlaneVirAddr[0] = (UL)pOutVirAddr;
    }
    if (stOutFrame.stVFrame.u32PlaneNum > 1U) {
        stOutFrame.stVFrame.u32Fd[1] = (U32)s32OutFd;
        stOutFrame.stVFrame.ulPlaneVirAddr[1] = (UL)((U8 *)pOutVirAddr + stOutFrame.stVFrame.u32PlaneSize[0]);
    }

    cpp_fill_cam_buffer_from_frame(pstInFrame, &stInBuf, MPP_TRUE);
    cpp_fill_cam_buffer_from_frame(&stOutFrame, &stOutBuf, MPP_FALSE);
    stInBuf.frameId = (int)u32FrameId;
    stOutBuf.frameId = (int)u32FrameId;

    pthread_mutex_lock(&pstGrpCtx->stPendingLock);
    s32Ret = cpp_pending_alloc(pstGrpCtx, &u32PendingIdx);
    if (s32Ret != CPP_SUCCESS) {
        pthread_mutex_unlock(&pstGrpCtx->stPendingLock);
        (void)VB_ReleaseBuffer(ulOutBuffer);
        return s32Ret;
    }

    pstGrpCtx->astPending[u32PendingIdx].CppChn = 0;
    pstGrpCtx->astPending[u32PendingIdx].u32FrameId = u32FrameId;
    pstGrpCtx->astPending[u32PendingIdx].ulOutBufferId = ulOutBuffer;
    pstGrpCtx->astPending[u32PendingIdx].stOutFrame = stOutFrame;
    pstGrpCtx->astPending[u32PendingIdx].stOutFrame.u32Idx = u32FrameId;
    pstGrpCtx->astPending[u32PendingIdx].pUserData = pUserData;
    pthread_mutex_unlock(&pstGrpCtx->stPendingLock);

    pstFrameInfo = (const ViFrameMetaInfo *)pUserData;
    cpp_fill_cam_frame_info(&stFrameInfo, pstFrameInfo, u32FrameId);

    s32Ret = cam_cpp_post_buffer((uint32_t)CppGrp, &stInBuf, &stOutBuf, &stFrameInfo);
    if (s32Ret != 0) {
        pthread_mutex_lock(&pstGrpCtx->stPendingLock);
        if (pstGrpCtx->astPending[u32PendingIdx].ulOutBufferId != 0U) {
            (void)VB_ReleaseBuffer(pstGrpCtx->astPending[u32PendingIdx].ulOutBufferId);
        }
        memset(&pstGrpCtx->astPending[u32PendingIdx], 0, sizeof(pstGrpCtx->astPending[u32PendingIdx]));
        pthread_mutex_unlock(&pstGrpCtx->stPendingLock);
        return s32Ret;
    }

    return CPP_SUCCESS;
}

S32 CPP_GetFrame(CPP_GRP CppGrp, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    U32 u32PendingIdx;
    S32 s32Ret;

    if (pstVideoFrame == NULL) {
        return CPP_ERR_INVALID_PARAM;
    }

    s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);
    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    if (!pstGrpCtx->bStarted) {
        return CPP_ERR_BAD_STATE;
    }

    s32Ret = cpp_done_queue_pop(&pstGrpCtx->stDoneQueue, &u32PendingIdx, s32MilliSec);
    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    pthread_mutex_lock(&pstGrpCtx->stPendingLock);
    if ((u32PendingIdx >= CPP_PENDING_MAX_PER_GRP) ||
        (pstGrpCtx->astPending[u32PendingIdx].bUsed != MPP_TRUE) ||
        (pstGrpCtx->astPending[u32PendingIdx].eState != CPP_FRAME_STATE_DONE)) {
        pthread_mutex_unlock(&pstGrpCtx->stPendingLock);
        return CPP_ERR_BAD_STATE;
    }

    *pstVideoFrame = pstGrpCtx->astPending[u32PendingIdx].stOutFrame;
    pstGrpCtx->astPending[u32PendingIdx].eState = CPP_FRAME_STATE_BORROWED;
    s32Ret = pstGrpCtx->astPending[u32PendingIdx].s32Result;
    pthread_mutex_unlock(&pstGrpCtx->stPendingLock);

    return s32Ret;
}

S32 CPP_ReleaseFrame(CPP_GRP CppGrp, const VideoFrameInfo *pstVideoFrame)
{
    CppGrpCtxS *pstGrpCtx = NULL;
    CppPendingRecordS *pstPending = NULL;
    S32 s32Ret;

    if (pstVideoFrame == NULL) {
        return CPP_ERR_INVALID_PARAM;
    }

    s32Ret = cpp_check_grp_created(CppGrp, &pstGrpCtx);
    if (s32Ret != CPP_SUCCESS) {
        return s32Ret;
    }

    pthread_mutex_lock(&pstGrpCtx->stPendingLock);
    pstPending = cpp_pending_find_borrowed(pstGrpCtx, pstVideoFrame);
    if (pstPending == NULL) {
        pthread_mutex_unlock(&pstGrpCtx->stPendingLock);
        return CPP_ERR_NOT_EXIST;
    }

    if (pstPending->ulOutBufferId != 0U) {
        s32Ret = VB_ReleaseBuffer(pstPending->ulOutBufferId);
        if (s32Ret != 0) {
            pthread_mutex_unlock(&pstGrpCtx->stPendingLock);
            return s32Ret;
        }
    }

    memset(pstPending, 0, sizeof(*pstPending));
    pthread_mutex_unlock(&pstGrpCtx->stPendingLock);
    return CPP_SUCCESS;
}

S32 CPP_DumpFrame(CPP_GRP CppGrp, const CHAR *pszPath, U32 u32Count)
{
    if ((cpp_check_grp_created(CppGrp, NULL) != CPP_SUCCESS) || (pszPath == NULL)) {
        return CPP_ERR_INVALID_PARAM;
    }

    return cam_cpp_dump_frame((uint32_t)CppGrp, pszPath, u32Count);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
