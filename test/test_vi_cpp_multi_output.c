#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "vi_api.h"
#include "vb_api.h"
#include "cpp_api.h"
#include "sys_api.h"

#define DEMO_VI_DEV 0
#define DEMO_VI_CHN 0
#define DEMO_CPP_GRP_BASE 0
#define DEMO_MULTI_GRP_MAX 4
#define DEMO_FRAME_COUNT 30
#define DEMO_TIMEOUT_MS 30
#define DEMO_SAVE_LAST_FRAME 1

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

typedef struct _CPP_MULTI_OUT_GRP_S {
    BOOL bEnable;
    CPP_GRP CppGrp;
    U32 u32Width;
    U32 u32Height;
    MppPixelFormat ePixelFormat;
    CppGrpAttrS stCppGrpAttr;
    CppChnAttrS stCppChnAttr;
    BOOL bEnableFrameRateCtrl;
    CppFrameRateCtrlS stFrameRateCtrl;
    U32 u32DoneCount;
    U32 u32DropCount;
    U32 u32ErrCount;
    BOOL bSavedLastCppFrame;
    CHAR szLastCppFramePath[320];
    double dFps;
} CPP_MULTI_OUT_GRP_S;

typedef struct _CPP_MULTI_CONFIG_S {
    VI_DEV ViDev;
    VI_CHN ViChn;
    U32 u32FrameCount;
    S32 s32TimeoutMs;
    U32 u32Width;
    U32 u32Height;
    MppPixelFormat ePixelFormat;
    CHAR szDumpPath[256];
    ViDevAttrS stViDevAttr;
    ViChnAttrS stViChnAttr;
    U32 u32OutGrpCount;
    CPP_MULTI_OUT_GRP_S astOutGrp[DEMO_MULTI_GRP_MAX];
} CPP_MULTI_CONFIG_S;

typedef struct _CPP_MULTI_RUN_CTX_S {
    CPP_MULTI_CONFIG_S stCfg;
    U32 u32DoneCount;
    U32 u32ErrCount;
} CPP_MULTI_RUN_CTX_S;

static CPP_MULTI_RUN_CTX_S g_stCppMultiCtx;

static S32 DemoSaveFrameToYuvFile(const VideoFrameInfo *pstFrame, const CHAR *pszFilePath);

static double DemoTimeDiffMs(const struct timeval *pstStart, const struct timeval *pstEnd) {
    double dMs = 0.0;

    if (pstStart == NULL || pstEnd == NULL)
        return 0.0;

    dMs = (double)(pstEnd->tv_sec - pstStart->tv_sec) * 1000.0;
    dMs += (double)(pstEnd->tv_usec - pstStart->tv_usec) / 1000.0;
    return dMs;
}

static double DemoCalcFps(U32 u32FrameCount, const struct timeval *pstStart, const struct timeval *pstEnd) {
    double dElapsedMs = DemoTimeDiffMs(pstStart, pstEnd);

    if ((u32FrameCount == 0U) || (dElapsedMs <= 0.0))
        return 0.0;

    return ((double)u32FrameCount * 1000.0) / dElapsedMs;
}

static U32 DemoCalcNv12BufferSize(U32 u32Width, U32 u32Height) {
    return u32Width * u32Height * 3 / 2;
}

static VOID DemoPrintFrameBrief(const CHAR *pszTag, const VideoFrameInfo *pstFrame) {
    if (pszTag == NULL || pstFrame == NULL)
        return;

    printf(
        "[%s] idx=%u pool=%lu buf=%lu size=%ux%u fmt=%d planes=%u pts=%llu fd0=%lu vir0=%p\n",
        pszTag,
        pstFrame->u32Idx,
        pstFrame->ulPoolId,
        pstFrame->ulBufferId,
        pstFrame->stCommFrameInfo.u32Width,
        pstFrame->stCommFrameInfo.u32Height,
        pstFrame->stCommFrameInfo.ePixelFormat,
        pstFrame->stVFrame.u32PlaneNum,
        (uint64_t)pstFrame->stVFrame.u64PTS,
        pstFrame->stVFrame.u32Fd[0],
        (void *)pstFrame->stVFrame.ulPlaneVirAddr[0]);
}

static VOID DemoPrintFrameMetaBrief(const CHAR *pszTag, const ViFrameMetaInfo *pstMeta) {
    if (pszTag == NULL || pstMeta == NULL)
        return;

    printf(
        "[%s] frameId=%u aeStable=%u awbStable=%u ct=%u expTime=[%u,%u,%u] again=[%u,%u,%u] dgain=[%u,%u,%u]\n",
        pszTag,
        pstMeta->u32FrameId,
        pstMeta->u8AeStable,
        pstMeta->u8AwbStable,
        pstMeta->u32ColorTemp,
        pstMeta->u32ExpTime[0],
        pstMeta->u32ExpTime[1],
        pstMeta->u32ExpTime[2],
        pstMeta->u32Again[0],
        pstMeta->u32Again[1],
        pstMeta->u32Again[2],
        pstMeta->u32Dgain[0],
        pstMeta->u32Dgain[1],
        pstMeta->u32Dgain[2]);
}

static S32 DemoInitConfig(CPP_MULTI_CONFIG_S *pstCfg) {
    U32 i;
    U32 au32Widths[DEMO_MULTI_GRP_MAX] = {1920, 1920, 1920, 1920};
    U32 au32Heights[DEMO_MULTI_GRP_MAX] = {1080, 1080, 1080, 1080};

    if (pstCfg == NULL)
        return -1;

    memset(pstCfg, 0, sizeof(*pstCfg));
    pstCfg->ViDev = DEMO_VI_DEV;
    pstCfg->ViChn = DEMO_VI_CHN;
    pstCfg->u32FrameCount = DEMO_FRAME_COUNT;
    pstCfg->s32TimeoutMs = DEMO_TIMEOUT_MS;
    pstCfg->u32Width = 1920;
    pstCfg->u32Height = 1080;
    pstCfg->ePixelFormat = MPP_PIXEL_FORMAT_NV12;
    pstCfg->u32OutGrpCount = DEMO_MULTI_GRP_MAX;
    snprintf(pstCfg->szDumpPath, sizeof(pstCfg->szDumpPath), "/tmp/mpp_cpp_multi_output_dump");

    pstCfg->stViDevAttr.eWorkMode = VI_WORK_MODE_ONLINE;
    pstCfg->stViDevAttr.u32Width = 3864;
    pstCfg->stViDevAttr.u32Height = 2192;
    pstCfg->stViDevAttr.u32MipiLaneNum = 4;
    pstCfg->stViDevAttr.u32mbps = 800;
    pstCfg->stViDevAttr.bCapture2Preview = MPP_FALSE;

    pstCfg->stViChnAttr.eChnType = VI_CHN_TYPE_PHYSICAL;
    pstCfg->stViChnAttr.ePixelFormat = pstCfg->ePixelFormat;
    pstCfg->stViChnAttr.u32Width = pstCfg->u32Width;
    pstCfg->stViChnAttr.u32Height = pstCfg->u32Height;

    for (i = 0; i < DEMO_MULTI_GRP_MAX; ++i) {
        CPP_MULTI_OUT_GRP_S *pstOutGrp = &pstCfg->astOutGrp[i];
        pstOutGrp->bEnable = MPP_TRUE;
        pstOutGrp->CppGrp = (CPP_GRP)(DEMO_CPP_GRP_BASE + i);
        pstOutGrp->u32Width = au32Widths[i];
        pstOutGrp->u32Height = au32Heights[i];
        pstOutGrp->ePixelFormat = pstCfg->ePixelFormat;
    }
    for (i = 1; i < DEMO_MULTI_GRP_MAX; ++i) {
        CPP_MULTI_OUT_GRP_S *pstOutGrp = &pstCfg->astOutGrp[i];
        pstOutGrp->bEnableFrameRateCtrl = MPP_TRUE;
        pstOutGrp->stFrameRateCtrl.u32InputFrameStep = 2;
        pstOutGrp->stFrameRateCtrl.u32OutputFrameStep = 1;
    }

    return 0;
}

static VOID DemoUpdateOutputAttrs(CPP_MULTI_CONFIG_S *pstCfg) {
    U32 i;

    if (pstCfg == NULL)
        return;

    pstCfg->stViChnAttr.u32Width = pstCfg->u32Width;
    pstCfg->stViChnAttr.u32Height = pstCfg->u32Height;
    pstCfg->stViChnAttr.ePixelFormat = pstCfg->ePixelFormat;

    for (i = 0; i < pstCfg->u32OutGrpCount; ++i) {
        CPP_MULTI_OUT_GRP_S *pstOutGrp = &pstCfg->astOutGrp[i];

        if (pstOutGrp->u32Width > pstCfg->u32Width)
            pstOutGrp->u32Width = pstCfg->u32Width;
        if (pstOutGrp->u32Height > pstCfg->u32Height)
            pstOutGrp->u32Height = pstCfg->u32Height;

        pstOutGrp->ePixelFormat = pstCfg->ePixelFormat;
        pstOutGrp->stCppGrpAttr.u32Width = pstOutGrp->u32Width;
        pstOutGrp->stCppGrpAttr.u32Height = pstOutGrp->u32Height;
        pstOutGrp->stCppGrpAttr.ePixelFormat = pstOutGrp->ePixelFormat;
        pstOutGrp->stCppGrpAttr.eProcessMode = CPP_PROCESS_MODE_FRAME;
        pstOutGrp->stCppChnAttr.bEnable = MPP_TRUE;
        pstOutGrp->stCppChnAttr.u32Width = pstOutGrp->u32Width;
        pstOutGrp->stCppChnAttr.u32Height = pstOutGrp->u32Height;
        pstOutGrp->stCppChnAttr.ePixelFormat = pstOutGrp->ePixelFormat;
        pstOutGrp->u32DoneCount = 0;
        pstOutGrp->u32DropCount = 0;
        pstOutGrp->u32ErrCount = 0;
        pstOutGrp->bSavedLastCppFrame = MPP_FALSE;
        pstOutGrp->dFps = 0.0;
        snprintf(
            pstOutGrp->szLastCppFramePath,
            sizeof(pstOutGrp->szLastCppFramePath),
            "%s_grp%d_last_frame.yuv",
            pstCfg->szDumpPath,
            pstOutGrp->CppGrp);
    }
}

static S32 DemoSetupVi(const CPP_MULTI_CONFIG_S *pstCfg) {
    S32 s32Ret;

    s32Ret = VI_Init();
    if (s32Ret != 0) {
        printf("VI_Init failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    s32Ret = VI_SetDevAttr(pstCfg->ViDev, &pstCfg->stViDevAttr);
    if (s32Ret != 0) {
        printf("VI_SetDevAttr failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    s32Ret = VI_SetChnAttr(pstCfg->ViDev, pstCfg->ViChn, &pstCfg->stViChnAttr);
    if (s32Ret != 0) {
        printf("VI_SetChnAttr failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    s32Ret = VI_EnableDev(pstCfg->ViDev);
    if (s32Ret != 0) {
        printf("VI_EnableDev failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    s32Ret = VI_EnableChn(pstCfg->ViDev, pstCfg->ViChn);
    if (s32Ret != 0) {
        printf("VI_EnableChn failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    return 0;
}

static VOID DemoTeardownVi(const CPP_MULTI_CONFIG_S *pstCfg) {
    (void)VI_DisableChn(pstCfg->ViDev, pstCfg->ViChn);
    (void)VI_DisableDev(pstCfg->ViDev);
    (void)VI_DeInit();
}

static S32 DemoSetupCpp(const CPP_MULTI_CONFIG_S *pstCfg) {
    U32 i;
    S32 s32Ret;

    s32Ret = CPP_Init();
    if (s32Ret != 0) {
        printf("CPP_Init failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    for (i = 0; i < pstCfg->u32OutGrpCount; ++i) {
        const CPP_MULTI_OUT_GRP_S *pstOutGrp = &pstCfg->astOutGrp[i];

        if (!pstOutGrp->bEnable)
            continue;

        s32Ret = CPP_CreateGrp(pstOutGrp->CppGrp);
        if (s32Ret != 0) {
            printf("CPP_CreateGrp grp=%d failed, ret=%d\n", pstOutGrp->CppGrp, s32Ret);
            return s32Ret;
        }

        s32Ret = CPP_SetGrpAttr(pstOutGrp->CppGrp, &pstOutGrp->stCppGrpAttr);
        if (s32Ret != 0) {
            printf("CPP_SetGrpAttr grp=%d failed, ret=%d\n", pstOutGrp->CppGrp, s32Ret);
            return s32Ret;
        }

        s32Ret = CPP_SetAttr(pstOutGrp->CppGrp, &pstOutGrp->stCppChnAttr);
        if (s32Ret != 0) {
            printf("CPP_SetAttr grp=%d failed, ret=%d\n", pstOutGrp->CppGrp, s32Ret);
            return s32Ret;
        }

        if (pstOutGrp->bEnableFrameRateCtrl == MPP_TRUE) {
            s32Ret = CPP_SetFrameRate(pstOutGrp->CppGrp, &pstOutGrp->stFrameRateCtrl);
            if (s32Ret != 0) {
                printf("CPP_SetFrameRate grp=%d failed, ret=%d\n", pstOutGrp->CppGrp, s32Ret);
                return s32Ret;
            }

            printf(
                "[demo][grp%d] CPP frame rate ctrl enabled: input_step=%u output_step=%u\n",
                pstOutGrp->CppGrp,
                pstOutGrp->stFrameRateCtrl.u32InputFrameStep,
                pstOutGrp->stFrameRateCtrl.u32OutputFrameStep);
        }

        s32Ret = CPP_Enable(pstOutGrp->CppGrp);
        if (s32Ret != 0) {
            printf("CPP_Enable grp=%d failed, ret=%d\n", pstOutGrp->CppGrp, s32Ret);
            return s32Ret;
        }

        s32Ret = CPP_StartGrp(pstOutGrp->CppGrp);
        if (s32Ret != 0) {
            printf("CPP_StartGrp grp=%d failed, ret=%d\n", pstOutGrp->CppGrp, s32Ret);
            return s32Ret;
        }
    }

    return 0;
}

static VOID DemoTeardownCpp(const CPP_MULTI_CONFIG_S *pstCfg) {
    U32 i;

    for (i = 0; i < pstCfg->u32OutGrpCount; ++i) {
        const CPP_MULTI_OUT_GRP_S *pstOutGrp = &pstCfg->astOutGrp[i];

        if (!pstOutGrp->bEnable)
            continue;

        (void)CPP_StopGrp(pstOutGrp->CppGrp);
        (void)CPP_Disable(pstOutGrp->CppGrp);
        (void)CPP_DestroyGrp(pstOutGrp->CppGrp);
    }

    (void)CPP_DeInit();
}

static S32 DemoSaveFrameToYuvFile(const VideoFrameInfo *pstFrame, const CHAR *pszFilePath) {
    FILE *fp;
    size_t uWriteSize;
    U32 uExpectedSize;
    U32 uPlane0Size;
    U32 uPlane1Size;

    if ((pstFrame == NULL) || (pszFilePath == NULL) || (pszFilePath[0] == '\0'))
        return -1;

    if (pstFrame->stVFrame.ulPlaneVirAddr[0] == 0U) {
        printf("DemoSaveFrameToYuvFile: frame Y addr is NULL\n");
        return -1;
    }

    if ((pstFrame->stVFrame.u32PlaneNum > 1U) && (pstFrame->stVFrame.ulPlaneVirAddr[1] == 0U)) {
        printf("DemoSaveFrameToYuvFile: frame UV addr is NULL\n");
        return -1;
    }

    uPlane0Size = pstFrame->stVFrame.u32PlaneSize[0];
    uPlane1Size = (pstFrame->stVFrame.u32PlaneNum > 1U) ? pstFrame->stVFrame.u32PlaneSize[1] : 0U;
    uExpectedSize = uPlane0Size + uPlane1Size;

    fp = fopen(pszFilePath, "wb");
    if (fp == NULL) {
        printf("DemoSaveFrameToYuvFile: fopen failed for %s\n", pszFilePath);
        return -1;
    }

    uWriteSize = fwrite((const void *)pstFrame->stVFrame.ulPlaneVirAddr[0], 1, uPlane0Size, fp);
    if (uWriteSize != uPlane0Size) {
        fclose(fp);
        printf(
            "DemoSaveFrameToYuvFile: fwrite Y short, expect=%u actual=%u path=%s\n",
            uPlane0Size,
            (U32)uWriteSize,
            pszFilePath);
        return -1;
    }

    if (uPlane1Size > 0U) {
        uWriteSize = fwrite((const void *)pstFrame->stVFrame.ulPlaneVirAddr[1], 1, uPlane1Size, fp);
        if (uWriteSize != uPlane1Size) {
            fclose(fp);
            printf(
                "DemoSaveFrameToYuvFile: fwrite UV short, expect=%u actual=%u path=%s\n",
                uPlane1Size,
                (U32)uWriteSize,
                pszFilePath);
            return -1;
        }
    }

    fclose(fp);

    printf("[demo] saved frame yuv: %s (%u bytes)\n", pszFilePath, uExpectedSize);
    return 0;
}

static S32 DemoRunViToCpp(CPP_MULTI_RUN_CTX_S *pstCtx) {
    U32 i;
    U32 j;
    U32 u32FrameId;
    S32 s32Ret;
    struct timeval stStartTv;
    struct timeval stEndTv;
    struct timeval stNowTv;
    struct timeval stLastStatTv;
    VideoFrameInfo stInFrame;
    VideoFrameInfo stOutFrame;
    const ViFrameMetaInfo *pstFrameMeta;
    if (pstCtx == NULL)
        return -1;

    pstCtx->u32DoneCount = 0;
    pstCtx->u32ErrCount = 0;
    for (j = 0; j < pstCtx->stCfg.u32OutGrpCount; ++j) {
        pstCtx->stCfg.astOutGrp[j].u32DoneCount = 0;
        pstCtx->stCfg.astOutGrp[j].u32DropCount = 0;
        pstCtx->stCfg.astOutGrp[j].u32ErrCount = 0;
        pstCtx->stCfg.astOutGrp[j].bSavedLastCppFrame = MPP_FALSE;
        pstCtx->stCfg.astOutGrp[j].dFps = 0.0;
    }

    (void)gettimeofday(&stStartTv, NULL);
    stLastStatTv = stStartTv;
    for (i = 0; i < pstCtx->stCfg.u32FrameCount; ++i) {
        memset(&stInFrame, 0, sizeof(stInFrame));
        memset(&stOutFrame, 0, sizeof(stOutFrame));
        pstFrameMeta = NULL;

        s32Ret = VI_GetChnFrame(pstCtx->stCfg.ViDev, pstCtx->stCfg.ViChn, &stInFrame, pstCtx->stCfg.s32TimeoutMs);
        if (s32Ret != 0) {
            if (s32Ret == -4)
                printf("[vi_cpp_multi_output_demo] no frame yet\n");
            else
                printf("[vi_cpp_multi_output_demo] VI_GetChnFrame failed, ret=%d, skip this round\n", s32Ret);
            continue;
        }

        u32FrameId = stInFrame.stVFrame.u32PrivateData;
        pstFrameMeta = &stInFrame.stViFrameInfo.stFrameMetaInfo;
        // DemoPrintFrameBrief("vi_in", &stInFrame);

        // DemoPrintFrameMetaBrief("vi_meta", pstFrameMeta);

        for (j = 0; j < pstCtx->stCfg.u32OutGrpCount; ++j) {
            CPP_MULTI_OUT_GRP_S *pstOutGrp = &pstCtx->stCfg.astOutGrp[j];

            if (!pstOutGrp->bEnable)
                continue;

            memset(&stOutFrame, 0, sizeof(stOutFrame));
            s32Ret = CPP_SendFrame(pstOutGrp->CppGrp, &stInFrame, u32FrameId, (VOID *)pstFrameMeta);
            if (s32Ret != 0) {
                printf(
                    "CPP_SendFrame failed at frame %u, grp=%d, frameId=%u, ret=%d\n",
                    i,
                    pstOutGrp->CppGrp,
                    u32FrameId,
                    s32Ret);
                (void)VI_ReleaseChnFrame(pstCtx->stCfg.ViDev, pstCtx->stCfg.ViChn, &stInFrame);
                return s32Ret;
            }

            s32Ret = CPP_GetFrame(pstOutGrp->CppGrp, &stOutFrame, pstCtx->stCfg.s32TimeoutMs);
            if (s32Ret != 0) {
                if ((pstOutGrp->bEnableFrameRateCtrl == MPP_TRUE) && (s32Ret == -6)) {
                    pstOutGrp->u32DropCount++;
                    printf(
                        "[demo][grp%d] no cpp output for submit_idx=%u frameId=%u due to frame-rate ctrl\n",
                        pstOutGrp->CppGrp,
                        i,
                        u32FrameId);
                    continue;
                }

                printf(
                    "CPP_GetFrame failed at frame %u, grp=%d, frameId=%u ret=%d\n",
                    i,
                    pstOutGrp->CppGrp,
                    u32FrameId,
                    s32Ret);
                pstCtx->u32ErrCount++;
                pstOutGrp->u32ErrCount++;
                (void)VI_ReleaseChnFrame(pstCtx->stCfg.ViDev, pstCtx->stCfg.ViChn, &stInFrame);
                return s32Ret;
            }

            pstCtx->u32DoneCount++;
            pstOutGrp->u32DoneCount++;
            DemoPrintFrameBrief("cpp_out", &stOutFrame);

            (void)gettimeofday(&stNowTv, NULL);
            pstOutGrp->dFps = DemoCalcFps(pstOutGrp->u32DoneCount, &stStartTv, &stNowTv);

#if DEMO_SAVE_LAST_FRAME
            if (i + 1U == pstCtx->stCfg.u32FrameCount) {
                if (DemoSaveFrameToYuvFile(&stOutFrame, pstOutGrp->szLastCppFramePath) == 0)
                    pstOutGrp->bSavedLastCppFrame = MPP_TRUE;
            }
#endif

            s32Ret = CPP_ReleaseFrame(pstOutGrp->CppGrp, &stOutFrame);
            if (s32Ret != 0) {
                printf("CPP_ReleaseFrame failed at frame %u, grp=%d, ret=%d\n", i, pstOutGrp->CppGrp, s32Ret);
                (void)VI_ReleaseChnFrame(pstCtx->stCfg.ViDev, pstCtx->stCfg.ViChn, &stInFrame);
                return s32Ret;
            }
        }

        (void)gettimeofday(&stNowTv, NULL);
        if ((pstCtx->u32DoneCount > 0U) &&
            (((i + 1U) == 1U) || (((i + 1U) % 30U) == 0U) || (DemoTimeDiffMs(&stLastStatTv, &stNowTv) >= 1000.0))) {
            printf("[fps] total_done=%u elapsed=%.2f ms\n", pstCtx->u32DoneCount, DemoTimeDiffMs(&stStartTv, &stNowTv));
            for (j = 0; j < pstCtx->stCfg.u32OutGrpCount; ++j) {
                CPP_MULTI_OUT_GRP_S *pstOutGrp = &pstCtx->stCfg.astOutGrp[j];

                if (!pstOutGrp->bEnable)
                    continue;

                pstOutGrp->dFps = DemoCalcFps(pstOutGrp->u32DoneCount, &stStartTv, &stNowTv);
                printf(
                    "[fps][grp%d] size=%ux%u done=%u drop=%u fps=%.2f\n",
                    pstOutGrp->CppGrp,
                    pstOutGrp->u32Width,
                    pstOutGrp->u32Height,
                    pstOutGrp->u32DoneCount,
                    pstOutGrp->u32DropCount,
                    pstOutGrp->dFps);
            }
            stLastStatTv = stNowTv;
        }

        (void)VI_ReleaseChnFrame(pstCtx->stCfg.ViDev, pstCtx->stCfg.ViChn, &stInFrame);
        usleep(1 * 1000);
    }

    (void)gettimeofday(&stEndTv, NULL);
    printf(
        "[summary] submit_frames=%u out_grps=%u done=%u err=%u elapsed=%.2f ms\n",
        pstCtx->stCfg.u32FrameCount,
        pstCtx->stCfg.u32OutGrpCount,
        pstCtx->u32DoneCount,
        pstCtx->u32ErrCount,
        DemoTimeDiffMs(&stStartTv, &stEndTv));

    for (j = 0; j < pstCtx->stCfg.u32OutGrpCount; ++j) {
        CPP_MULTI_OUT_GRP_S *pstOutGrp = &pstCtx->stCfg.astOutGrp[j];
        pstOutGrp->dFps = DemoCalcFps(pstOutGrp->u32DoneCount, &stStartTv, &stEndTv);
        printf(
            "[summary][grp%d] size=%ux%u done=%u drop=%u err=%u\n",
            pstOutGrp->CppGrp,
            pstOutGrp->u32Width,
            pstOutGrp->u32Height,
            pstOutGrp->u32DoneCount,
            pstOutGrp->u32DropCount,
            pstOutGrp->u32ErrCount);
        printf(
            "[summary][grp%d] fps=%.2f last_frame=%s\n",
            pstOutGrp->CppGrp,
            pstOutGrp->dFps,
            pstOutGrp->bSavedLastCppFrame == MPP_TRUE ? pstOutGrp->szLastCppFramePath : "not_saved");
    }

    return 0;
}

static VOID DemoPrintUsage(const char *prog) {
    printf("Usage: %s [frame_count] [input_width] [input_height] [dump_path] [out_grp_count]\n", prog);
    printf("Example: %s 30 1920 1080 /tmp/mpp_cpp_multi_output_dump 4\n", prog);
    printf("Default output groups: 1920x1080, 1280x720, 960x540, 640x360\n");
}

int main(int argc, char *argv[]) {
    U32 i;
    S32 s32Ret;

    SYS_Init();
    VB_Init();

    memset(&g_stCppMultiCtx, 0, sizeof(g_stCppMultiCtx));
    s32Ret = DemoInitConfig(&g_stCppMultiCtx.stCfg);
    if (s32Ret != 0)
        return s32Ret;

    if (argc > 1) {
        if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
            DemoPrintUsage(argv[0]);
            return 0;
        }
        g_stCppMultiCtx.stCfg.u32FrameCount = (U32)atoi(argv[1]);
    }
    if (argc > 2)
        g_stCppMultiCtx.stCfg.u32Width = (U32)atoi(argv[2]);
    if (argc > 3)
        g_stCppMultiCtx.stCfg.u32Height = (U32)atoi(argv[3]);
    if (argc > 4)
        snprintf(g_stCppMultiCtx.stCfg.szDumpPath, sizeof(g_stCppMultiCtx.stCfg.szDumpPath), "%s", argv[4]);
    if (argc > 5) {
        U32 u32OutGrpCount = (U32)atoi(argv[5]);
        if ((u32OutGrpCount > 0U) && (u32OutGrpCount <= DEMO_MULTI_GRP_MAX))
            g_stCppMultiCtx.stCfg.u32OutGrpCount = u32OutGrpCount;
    }

    DemoUpdateOutputAttrs(&g_stCppMultiCtx.stCfg);

    printf("==== VI -> CPP multi output demo start ====\n");
    printf(
        "frames=%u input=%ux%u dump=%s out_grp_count=%u\n",
        g_stCppMultiCtx.stCfg.u32FrameCount,
        g_stCppMultiCtx.stCfg.u32Width,
        g_stCppMultiCtx.stCfg.u32Height,
        g_stCppMultiCtx.stCfg.szDumpPath,
        g_stCppMultiCtx.stCfg.u32OutGrpCount);
    for (i = 0; i < g_stCppMultiCtx.stCfg.u32OutGrpCount; ++i) {
        printf(
            "out[%u]: grp=%d chn=%d size=%ux%u fmt=%d frc=%s%u/%u\n",
            i,
            g_stCppMultiCtx.stCfg.astOutGrp[i].CppGrp,
            0,
            g_stCppMultiCtx.stCfg.astOutGrp[i].u32Width,
            g_stCppMultiCtx.stCfg.astOutGrp[i].u32Height,
            g_stCppMultiCtx.stCfg.astOutGrp[i].ePixelFormat,
            g_stCppMultiCtx.stCfg.astOutGrp[i].bEnableFrameRateCtrl ? "" : "off:",
            g_stCppMultiCtx.stCfg.astOutGrp[i].stFrameRateCtrl.u32InputFrameStep,
            g_stCppMultiCtx.stCfg.astOutGrp[i].stFrameRateCtrl.u32OutputFrameStep);
    }

    s32Ret = DemoSetupVi(&g_stCppMultiCtx.stCfg);
    if (s32Ret != 0)
        goto exit0;

    s32Ret = DemoSetupCpp(&g_stCppMultiCtx.stCfg);
    if (s32Ret != 0)
        goto exit1;

    sleep(1);

    s32Ret = DemoRunViToCpp(&g_stCppMultiCtx);
    if (s32Ret != 0)
        goto exit2;

    for (i = 0; i < g_stCppMultiCtx.stCfg.u32OutGrpCount; ++i)
        (void)CPP_DumpFrame(g_stCppMultiCtx.stCfg.astOutGrp[i].CppGrp, g_stCppMultiCtx.stCfg.szDumpPath, 1);

exit2:
    DemoTeardownCpp(&g_stCppMultiCtx.stCfg);
exit1:
    DemoTeardownVi(&g_stCppMultiCtx.stCfg);
exit0:
    printf("==== VI -> CPP multi output demo end, ret=%d ====\n", s32Ret);
    return s32Ret;
}
