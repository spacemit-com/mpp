#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "vi_api.h"
#include "vb_api.h"
#include "cpp_api.h"
#include "sys_api.h"

#define DEMO_VI_DEV       0
#define DEMO_VI_CHN       0
#define DEMO_CPP_GRP      0
#define DEMO_FRAME_COUNT  30
#define DEMO_TIMEOUT_MS   1000
#define DEMO_MAX_INFLIGHT 4
#define DEMO_SAVE_LAST_FRAME 1

typedef struct _CPP_DEMO_CONFIG_S {
    VI_DEV ViDev;
    VI_CHN ViChn;
    CPP_GRP CppGrp;
    U32 u32FrameCount;
    S32 s32TimeoutMs;
    U32 u32Width;
    U32 u32Height;
    MppPixelFormat ePixelFormat;
    CHAR szDumpPath[256];
    ViDevAttrS stViDevAttr;
    ViChnAttrS stViChnAttr;
    CppGrpAttrS stCppGrpAttr;
    CppChnAttrS stCppChnAttr;
    BOOL bEnableCppFrameRateCtrl;
    CppFrameRateCtrlS stCppFrameRateCtrl;
} CPP_DEMO_CONFIG_S;

typedef struct _CPP_DEMO_RUN_CTX_S {
    CPP_DEMO_CONFIG_S stCfg;
    U32 u32DoneCount;
    U32 u32ErrCount;
    BOOL bSavedLastCppFrame;
    CHAR szLastCppFramePath[320];
    double dPipelineFps;
} CPP_DEMO_RUN_CTX_S;

static CPP_DEMO_RUN_CTX_S g_stCppDemoCtx;

static S32 DemoSaveFrameToYuvFile(const VideoFrameInfo *pstFrame, const CHAR *pszFilePath)
{
    FILE *fp;
    size_t uWriteSize;
    U32 uExpectedSize;
    U32 uPlane0Size;
    U32 uPlane1Size;

    if ((pstFrame == NULL) || (pszFilePath == NULL) || (pszFilePath[0] == '\0')){
        return -1;
    }

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
        printf("DemoSaveFrameToYuvFile: fwrite Y short, expect=%u actual=%u path=%s\n",
            uPlane0Size,
            (U32)uWriteSize,
            pszFilePath);
        return -1;
    }

    if (uPlane1Size > 0U) {
        uWriteSize = fwrite((const void *)pstFrame->stVFrame.ulPlaneVirAddr[1], 1, uPlane1Size, fp);
        if (uWriteSize != uPlane1Size) {
            fclose(fp);
            printf("DemoSaveFrameToYuvFile: fwrite UV short, expect=%u actual=%u path=%s\n",
                uPlane1Size,
                (U32)uWriteSize,
                pszFilePath);
            return -1;
        }
    }

    fclose(fp);
    printf("[demo] saved frame: %s (%u bytes)\n", pszFilePath, uExpectedSize);
    return 0;
}

static double DemoTimeDiffMs(const struct timeval *pstStart, const struct timeval *pstEnd)
{
    double dMs = 0.0;

    if ((pstStart == NULL) || (pstEnd == NULL)){
        return 0.0;
    }

    dMs = (double)(pstEnd->tv_sec - pstStart->tv_sec) * 1000.0;
    dMs += (double)(pstEnd->tv_usec - pstStart->tv_usec) / 1000.0;
    return dMs;
}

static double DemoCalcFps(U32 u32FrameCount, const struct timeval *pstStart, const struct timeval *pstEnd)
{
    double dElapsedMs = DemoTimeDiffMs(pstStart, pstEnd);

    if ((u32FrameCount == 0U) || (dElapsedMs <= 0.0)){
        return 0.0;
    }

    return ((double)u32FrameCount * 1000.0) / dElapsedMs;
}

static VOID DemoPrintFrameBrief(const CHAR *pszTag, const VideoFrameInfo *pstFrame)
{
    if ((pszTag == NULL) || (pstFrame == NULL)){
        return;
    }

    printf("[%s] idx=%u pool=%lu buf=%lu size=%ux%u fmt=%d planes=%u pts=%llu fd0=%lu vir0=%p\n",
        pszTag,
        pstFrame->u32Idx,
        pstFrame->ulPoolId,
        pstFrame->ulBufferId,
        pstFrame->stCommFrameInfo.u32Width,
        pstFrame->stCommFrameInfo.u32Height,
        pstFrame->stCommFrameInfo.ePixelFormat,
        pstFrame->stVFrame.u32PlaneNum,
        (unsigned long long)pstFrame->stVFrame.u64PTS,
        pstFrame->stVFrame.u32Fd[0],
        (void *)pstFrame->stVFrame.ulPlaneVirAddr[0]);
}

static VOID DemoPrintFrameMetaBrief(const CHAR *pszTag, const ViFrameMetaInfo *pstMeta)
{
    if ((pszTag == NULL) || (pstMeta == NULL)){
        return;
    }

    printf("[%s] frameId=%u aeStable=%u awbStable=%u ct=%u expTime=[%u,%u,%u] again=[%u,%u,%u] dgain=[%u,%u,%u]\n",
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

static S32 DemoInitConfig(CPP_DEMO_CONFIG_S *pstCfg)
{
    if (pstCfg == NULL){
        return -1;
    }

    memset(pstCfg, 0, sizeof(*pstCfg));
    pstCfg->ViDev = DEMO_VI_DEV;
    pstCfg->ViChn = DEMO_VI_CHN;
    pstCfg->CppGrp = DEMO_CPP_GRP;
    pstCfg->u32FrameCount = DEMO_FRAME_COUNT;
    pstCfg->s32TimeoutMs = DEMO_TIMEOUT_MS;
    pstCfg->u32Width = 1920;
    pstCfg->u32Height = 1080;
    pstCfg->ePixelFormat = MPP_PIXEL_FORMAT_NV12;
    snprintf(pstCfg->szDumpPath, sizeof(pstCfg->szDumpPath), "/tmp/mpp_cpp_dump");

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

    pstCfg->stCppGrpAttr.u32Width = pstCfg->u32Width;
    pstCfg->stCppGrpAttr.u32Height = pstCfg->u32Height;
    pstCfg->stCppGrpAttr.ePixelFormat = pstCfg->ePixelFormat;
    pstCfg->stCppGrpAttr.eProcessMode = CPP_PROCESS_MODE_FRAME;

    pstCfg->stCppChnAttr.bEnable = MPP_TRUE;
    pstCfg->stCppChnAttr.u32Width = pstCfg->u32Width;
    pstCfg->stCppChnAttr.u32Height = pstCfg->u32Height;
    pstCfg->stCppChnAttr.ePixelFormat = pstCfg->ePixelFormat;
    pstCfg->bEnableCppFrameRateCtrl = MPP_FALSE;
    pstCfg->stCppFrameRateCtrl.u32InputFrameStep = 1U;
    pstCfg->stCppFrameRateCtrl.u32OutputFrameStep = 1U;
    return 0;
}

static S32 DemoSetupVi(const CPP_DEMO_CONFIG_S *pstCfg)
{
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

static VOID DemoTeardownVi(const CPP_DEMO_CONFIG_S *pstCfg)
{
    (void)VI_DisableChn(pstCfg->ViDev, pstCfg->ViChn);
    (void)VI_DisableDev(pstCfg->ViDev);
    (void)VI_DeInit();
}

static S32 DemoSetupCpp(const CPP_DEMO_CONFIG_S *pstCfg)
{
    S32 s32Ret;

    s32Ret = CPP_Init();
    if (s32Ret != 0) {
        printf("CPP_Init failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    s32Ret = CPP_CreateGrp(pstCfg->CppGrp);
    if (s32Ret != 0) {
        printf("CPP_CreateGrp failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    s32Ret = CPP_SetGrpAttr(pstCfg->CppGrp, &pstCfg->stCppGrpAttr);
    if (s32Ret != 0) {
        printf("CPP_SetGrpAttr failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    s32Ret = CPP_SetAttr(pstCfg->CppGrp, &pstCfg->stCppChnAttr);
    if (s32Ret != 0) {
        printf("CPP_SetAttr failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    if (pstCfg->bEnableCppFrameRateCtrl == MPP_TRUE) {
        s32Ret = CPP_SetFrameRate(pstCfg->CppGrp, &pstCfg->stCppFrameRateCtrl);
        if (s32Ret != 0) {
            printf("CPP_SetFrameRate failed, ret=%d\n", s32Ret);
            return s32Ret;
        }

        printf("[demo] CPP frame rate ctrl enabled: input_step=%u output_step=%u\n",
            pstCfg->stCppFrameRateCtrl.u32InputFrameStep,
            pstCfg->stCppFrameRateCtrl.u32OutputFrameStep);
    }

    s32Ret = CPP_Enable(pstCfg->CppGrp);
    if (s32Ret != 0) {
        printf("CPP_Enable failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    s32Ret = CPP_StartGrp(pstCfg->CppGrp);
    if (s32Ret != 0) {
        printf("CPP_StartGrp failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    return 0;
}

static VOID DemoTeardownCpp(const CPP_DEMO_CONFIG_S *pstCfg)
{
    (void)CPP_StopGrp(pstCfg->CppGrp);
    (void)CPP_Disable(pstCfg->CppGrp);
    (void)CPP_DestroyGrp(pstCfg->CppGrp);
    (void)CPP_DeInit();
}

static S32 DemoRunViToCpp(CPP_DEMO_RUN_CTX_S *pstCtx)
{
    U32 u32FrameIdx = 0;
    U32 u32FrameId;
    S32 s32Ret;
    struct timeval stStartTv;
    struct timeval stEndTv;
    struct timeval stLastStatTv;
    struct timeval stNowTv;
    VideoFrameInfo stInFrame;
    VideoFrameInfo stOutFrame;
    const ViFrameMetaInfo *pstFrameMeta;
    if (pstCtx == NULL){
        return -1;
    }

    pstCtx->u32DoneCount = 0;
    pstCtx->u32ErrCount = 0;
    pstCtx->bSavedLastCppFrame = MPP_FALSE;
    pstCtx->dPipelineFps = 0.0;
    snprintf(pstCtx->szLastCppFramePath, sizeof(pstCtx->szLastCppFramePath),
        "%s_last_cpp_frame.yuv", pstCtx->stCfg.szDumpPath);

    (void)gettimeofday(&stStartTv, NULL);
    stLastStatTv = stStartTv;
    for (u32FrameIdx = 0; u32FrameIdx < pstCtx->stCfg.u32FrameCount; ++u32FrameIdx) {
        memset(&stInFrame, 0, sizeof(stInFrame));
        memset(&stOutFrame, 0, sizeof(stOutFrame));
        pstFrameMeta = NULL;

        s32Ret = VI_GetChnFrame(pstCtx->stCfg.ViDev,
            pstCtx->stCfg.ViChn,
            &stInFrame,
            pstCtx->stCfg.s32TimeoutMs);
        if (s32Ret != 0) {
            if (s32Ret == -4){
                printf("[vi_cpp_demo] no frame yet\n");
            }else{
                printf("[vi_cpp_demo] VI_GetChnFrame failed, ret=%d, skip this round\n", s32Ret);
            }
            continue;
        }

        u32FrameId = stInFrame.stVFrame.u32PrivateData;
        pstFrameMeta = &stInFrame.stViFrameInfo.stFrameMetaInfo;
        DemoPrintFrameBrief("vi_in", &stInFrame);

        DemoPrintFrameMetaBrief("vi_meta", pstFrameMeta);

        s32Ret = CPP_SendFrame(pstCtx->stCfg.CppGrp, &stInFrame, u32FrameId, (VOID *)pstFrameMeta);

        if (s32Ret != 0) {
            printf("CPP_SendFrame failed at frame %u, frameId=%u, ret=%d\n",
                u32FrameIdx,
                u32FrameId,
                s32Ret);
            return s32Ret;
        }

        if ((pstCtx->stCfg.bEnableCppFrameRateCtrl == MPP_TRUE) &&
            (pstCtx->stCfg.stCppFrameRateCtrl.u32OutputFrameStep < pstCtx->stCfg.stCppFrameRateCtrl.u32InputFrameStep) &&
            ((u32FrameIdx % pstCtx->stCfg.stCppFrameRateCtrl.u32InputFrameStep) >= pstCtx->stCfg.stCppFrameRateCtrl.u32OutputFrameStep)) {
            printf("[demo] cpp drop by frame-rate ctrl: submit_idx=%u frameId=%u\n",
                u32FrameIdx,
                u32FrameId);
            (void)VI_ReleaseChnFrame(pstCtx->stCfg.ViDev, pstCtx->stCfg.ViChn, &stInFrame);
            usleep(25000);
            continue;
        }

        s32Ret = CPP_GetFrame(pstCtx->stCfg.CppGrp, &stOutFrame, pstCtx->stCfg.s32TimeoutMs);
        if (s32Ret != 0) {
            printf("CPP_GetFrame failed at frame %u, frameId=%u ret=%d\n",
                u32FrameIdx,
                u32FrameId,
                s32Ret);
            pstCtx->u32ErrCount++;
            return s32Ret;
        }

        (void)VI_ReleaseChnFrame(pstCtx->stCfg.ViDev, pstCtx->stCfg.ViChn, &stInFrame);

        pstCtx->u32DoneCount++;
        DemoPrintFrameBrief("cpp_out", &stOutFrame);

        (void)gettimeofday(&stNowTv, NULL);
        if ((pstCtx->u32DoneCount == 1U) ||
            ((pstCtx->u32DoneCount % 30U) == 0U) ||
            (DemoTimeDiffMs(&stLastStatTv, &stNowTv) >= 1000.0)) {
            double dCurFps = DemoCalcFps(pstCtx->u32DoneCount, &stStartTv, &stNowTv);
            printf("[fps] pipeline done=%u elapsed=%.2f ms fps=%.2f\n",
                pstCtx->u32DoneCount,
                DemoTimeDiffMs(&stStartTv, &stNowTv),
                dCurFps);
            stLastStatTv = stNowTv;
        }

    #if DEMO_SAVE_LAST_FRAME
        if (u32FrameIdx + 1U == pstCtx->stCfg.u32FrameCount) {
            if (DemoSaveFrameToYuvFile(&stOutFrame, pstCtx->szLastCppFramePath) == 0) {
                pstCtx->bSavedLastCppFrame = MPP_TRUE;
            }
        }
    #endif

        s32Ret = CPP_ReleaseFrame(pstCtx->stCfg.CppGrp, &stOutFrame);
        if (s32Ret != 0) {
            printf("CPP_ReleaseFrame failed at frame %u, ret=%d\n", u32FrameIdx, s32Ret);
            return s32Ret;
        }
        usleep(25000);// 延时因为camera 出帧是30fps

    }

    (void)gettimeofday(&stEndTv, NULL);
    pstCtx->dPipelineFps = DemoCalcFps(pstCtx->u32DoneCount, &stStartTv, &stEndTv);
    printf("[summary] submit=%u done=%u err=%u elapsed=%.2f ms\n",
        pstCtx->stCfg.u32FrameCount,
        pstCtx->u32DoneCount,
        pstCtx->u32ErrCount,
        DemoTimeDiffMs(&stStartTv, &stEndTv));
    printf("[summary] pipeline_fps=%.2f\n", pstCtx->dPipelineFps);
    printf("[summary] last_cpp_frame=%s\n",
        pstCtx->bSavedLastCppFrame == MPP_TRUE ? pstCtx->szLastCppFramePath : "not_saved");
    return 0;
}

static VOID DemoPrintUsage(const char *prog)
{
    printf("Usage: %s [frame_count] [width] [height] [dump_path] [cpp_input_step] [cpp_output_step]\n", prog);
    printf("Example: %s 30 1920 1080 /tmp/mpp_cpp_dump 2 1\n", prog);
}

int main(int argc, char *argv[])
{
    S32 s32Ret;

    (void)SYS_Init();
    (void)VB_Init();

    memset(&g_stCppDemoCtx, 0, sizeof(g_stCppDemoCtx));
    s32Ret = DemoInitConfig(&g_stCppDemoCtx.stCfg);
    if (s32Ret != 0){
        return s32Ret;
    }

    if (argc > 1) {
        if ((strcmp(argv[1], "-h") == 0) || (strcmp(argv[1], "--help") == 0)) {
            DemoPrintUsage(argv[0]);
            return 0;
        }
        g_stCppDemoCtx.stCfg.u32FrameCount = (U32)atoi(argv[1]);
    }
    if (argc > 2){
        g_stCppDemoCtx.stCfg.u32Width = (U32)atoi(argv[2]);
    }
    if (argc > 3){
        g_stCppDemoCtx.stCfg.u32Height = (U32)atoi(argv[3]);
    }
    if (argc > 4){
        snprintf(g_stCppDemoCtx.stCfg.szDumpPath, sizeof(g_stCppDemoCtx.stCfg.szDumpPath), "%s", argv[4]);
    }
    if (argc > 5){
        g_stCppDemoCtx.stCfg.stCppFrameRateCtrl.u32InputFrameStep = (U32)atoi(argv[5]);
    }
    if (argc > 6){
        g_stCppDemoCtx.stCfg.stCppFrameRateCtrl.u32OutputFrameStep = (U32)atoi(argv[6]);
    }

    g_stCppDemoCtx.stCfg.stViChnAttr.u32Width = g_stCppDemoCtx.stCfg.u32Width;
    g_stCppDemoCtx.stCfg.stViChnAttr.u32Height = g_stCppDemoCtx.stCfg.u32Height;
    g_stCppDemoCtx.stCfg.stCppGrpAttr.u32Width = g_stCppDemoCtx.stCfg.u32Width;
    g_stCppDemoCtx.stCfg.stCppGrpAttr.u32Height = g_stCppDemoCtx.stCfg.u32Height;
    g_stCppDemoCtx.stCfg.stCppChnAttr.u32Width = g_stCppDemoCtx.stCfg.u32Width;
    g_stCppDemoCtx.stCfg.stCppChnAttr.u32Height = g_stCppDemoCtx.stCfg.u32Height;

    printf("==== VI -> CPP demo start ====\n");
    printf("frame_count=%u width=%u height=%u dump_path=%s cpp_frc=%u/%u\n",
        g_stCppDemoCtx.stCfg.u32FrameCount,
        g_stCppDemoCtx.stCfg.u32Width,
        g_stCppDemoCtx.stCfg.u32Height,
        g_stCppDemoCtx.stCfg.szDumpPath,
        g_stCppDemoCtx.stCfg.stCppFrameRateCtrl.u32InputFrameStep,
        g_stCppDemoCtx.stCfg.stCppFrameRateCtrl.u32OutputFrameStep);

    s32Ret = DemoSetupVi(&g_stCppDemoCtx.stCfg);
    if (s32Ret != 0){
        goto exit1;
    }

    s32Ret = DemoSetupCpp(&g_stCppDemoCtx.stCfg);
    if (s32Ret != 0){
        goto exit2;
    }

    sleep(1);
    s32Ret = DemoRunViToCpp(&g_stCppDemoCtx);

    DemoTeardownCpp(&g_stCppDemoCtx.stCfg);
exit2:
    DemoTeardownVi(&g_stCppDemoCtx.stCfg);
exit1:
    printf("==== VI -> CPP demo end, ret=%d ====\n", s32Ret);
    return s32Ret;
}
