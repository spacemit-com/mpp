#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

#include "vi_api.h"
#include "vb_api.h"
#include "sys_api.h"

#define DEMO_DEFAULT_DEV            0
#define DEMO_PHY_CHN                0
#define DEMO_VIRT_CHN_1             1
#define DEMO_VIRT_CHN_2             2
#define DEMO_VIRT_CHN_3             3
#define DEMO_VIRT_CHN_4             4
#define DEMO_VIRT_CHN_5             5
#define DEMO_DEFAULT_FRAME_COUNT    30
#define DEMO_DEFAULT_TIMEOUT_MS     1000
#define DEMO_SAVE_LAST_FRAME        1

typedef enum _VI_DEMO_LOG_LEVEL_E {
    VI_DEMO_LOG_LEVEL_QUIET = 0,    /* only errors and final results */
    VI_DEMO_LOG_LEVEL_NORMAL = 1,   /* + setup/teardown info */
    VI_DEMO_LOG_LEVEL_VERBOSE = 2,  /* + per-frame info */
    VI_DEMO_LOG_LEVEL_DEBUG = 3,    /* + detailed frame info */
} VI_DEMO_LOG_LEVEL_E;

static VI_DEMO_LOG_LEVEL_E g_enLogLevel = VI_DEMO_LOG_LEVEL_NORMAL;

#define LOG_ERROR(fmt, ...) \
    printf("[ERROR] " fmt, ##__VA_ARGS__)

#define LOG_INFO(fmt, ...) \
    do { if (g_enLogLevel >= VI_DEMO_LOG_LEVEL_NORMAL) printf("[INFO] " fmt, ##__VA_ARGS__); } while(0)

#define LOG_VERBOSE(fmt, ...) \
    do { if (g_enLogLevel >= VI_DEMO_LOG_LEVEL_VERBOSE) printf("[VERBOSE] " fmt, ##__VA_ARGS__); } while(0)

#define LOG_DEBUG(fmt, ...) \
    do { if (g_enLogLevel >= VI_DEMO_LOG_LEVEL_DEBUG) printf("[DEBUG] " fmt, ##__VA_ARGS__); } while(0)

typedef enum _VI_DEMO_MODE_E {
    VI_DEMO_MODE_BASIC = 0,
    VI_DEMO_MODE_VIRTUAL,
    VI_DEMO_MODE_RAWDUMP,
    VI_DEMO_MODE_ALL,
} VI_DEMO_MODE_E;

typedef struct _VI_DEMO_CONFIG_S {
    VI_DEV          ViDev;
    VI_CHN          ViPhyChn;
    VI_CHN          ViVirtChn1;
    VI_CHN          ViVirtChn2;
    VI_CHN          ViVirtChn3;
    VI_CHN          ViVirtChn4;
    VI_CHN          ViVirtChn5;
    U32             u32FrameCount;
    S32             s32TimeoutMs;
    BOOL            bEnableVirt1;
    BOOL            bEnableVirt2;
    BOOL            bEnableVirt3;
    BOOL            bEnableVirt4;
    BOOL            bEnableVirt5;
    BOOL            bDumpRaw;
    BOOL            bEnablePhyFrameRateCtrl;
    BOOL            bEnableVirt1FrameRateCtrl;
    BOOL            bEnableVirt2FrameRateCtrl;
    BOOL            bEnableVirt3FrameRateCtrl;
    BOOL            bEnableVirt4FrameRateCtrl;
    BOOL            bEnableVirt5FrameRateCtrl;
    ViFrameRateCtrlS stPhyFrameRateCtrl;
    ViFrameRateCtrlS stVirt1FrameRateCtrl;
    ViFrameRateCtrlS stVirt2FrameRateCtrl;
    ViFrameRateCtrlS stVirt3FrameRateCtrl;
    ViFrameRateCtrlS stVirt4FrameRateCtrl;
    ViFrameRateCtrlS stVirt5FrameRateCtrl;
    ViDevAttrS   stDevAttr;
    ViChnAttrS   stPhyAttr;
    ViChnAttrS   stVirt1Attr;
    ViChnAttrS   stVirt2Attr;
    ViChnAttrS   stVirt3Attr;
    ViChnAttrS   stVirt4Attr;
    ViChnAttrS   stVirt5Attr;
} VI_DEMO_CONFIG_S;

typedef struct _VI_DEMO_FPS_STAT_S {
    const char *tag;
    U32 u32FrameCount;
    struct timeval stStartTv;
    struct timeval stLastPrintTv;
    BOOL bStarted;
} VI_DEMO_FPS_STAT_S;

static double VI_DemoTimeDiffMs(const struct timeval *pstStart, const struct timeval *pstEnd)
{
    double dMs = 0.0;

    if (pstStart == NULL || pstEnd == NULL)
        return 0.0;

    dMs = (double)(pstEnd->tv_sec - pstStart->tv_sec) * 1000.0;
    dMs += (double)(pstEnd->tv_usec - pstStart->tv_usec) / 1000.0;
    return dMs;
}

static VOID VI_DemoFpsStatUpdate(VI_DEMO_FPS_STAT_S *pstStat)
{
    struct timeval stNow;
    double dTotalMs = 0.0;
    double dWindowMs = 0.0;
    double dFps = 0.0;

    if (pstStat == NULL)
        return;

    (void)gettimeofday(&stNow, NULL);

    if (pstStat->bStarted != MPP_TRUE) {
        pstStat->stStartTv = stNow;
        pstStat->stLastPrintTv = stNow;
        pstStat->u32FrameCount = 0;
        pstStat->bStarted = MPP_TRUE;
    }

    pstStat->u32FrameCount++;
    dTotalMs = VI_DemoTimeDiffMs(&pstStat->stStartTv, &stNow);
    dWindowMs = VI_DemoTimeDiffMs(&pstStat->stLastPrintTv, &stNow);

    if (dWindowMs >= 1000.0) {
        if (dTotalMs > 0.0)
            dFps = ((double)pstStat->u32FrameCount * 1000.0) / dTotalMs;

        LOG_VERBOSE("[%s][FPS] frames=%u elapsed=%.2f ms fps=%.2f\n",
               pstStat->tag != NULL ? pstStat->tag : "unknown",
               pstStat->u32FrameCount,
               dTotalMs,
               dFps);
        pstStat->stLastPrintTv = stNow;
    }
}

static VOID VI_DemoFpsStatPrintFinal(const VI_DEMO_FPS_STAT_S *pstStat)
{
    struct timeval stNow;
    double dTotalMs = 0.0;
    double dFps = 0.0;

    if (pstStat == NULL || pstStat->bStarted != MPP_TRUE)
        return;

    (void)gettimeofday(&stNow, NULL);
    dTotalMs = VI_DemoTimeDiffMs(&pstStat->stStartTv, &stNow);
    if (dTotalMs > 0.0)
        dFps = ((double)pstStat->u32FrameCount * 1000.0) / dTotalMs;

    LOG_INFO("[%s][FPS][FINAL] frames=%u elapsed=%.2f ms avg_fps=%.2f\n",
           pstStat->tag != NULL ? pstStat->tag : "unknown",
           pstStat->u32FrameCount,
           dTotalMs,
           dFps);
}

static const char *VI_DemoModeName(VI_DEMO_MODE_E enMode)
{
    switch (enMode) {
    case VI_DEMO_MODE_BASIC:
        return "basic";
    case VI_DEMO_MODE_VIRTUAL:
        return "virtual";
    case VI_DEMO_MODE_RAWDUMP:
        return "rawdump";
    case VI_DEMO_MODE_ALL:
        return "all";
    default:
        return "unknown";
    }
}

static void VI_DemoPrintUsage(const char *prog)
{
    printf("Usage: %s [basic|virtual|rawdump|all] [frame_count] [options...]\n", prog);
    printf("  basic   : validate sensor -> vi_dev -> physical vi_chn -> isp\n");
    printf("  virtual : validate one physical channel plus multiple virtual channels\n");
    printf("  rawdump : validate rawdump trigger/get/release\n");
    printf("  all     : run all validations in sequence (default)\n");
    printf("\n");
    printf("Options:\n");
    printf("  -v, --verbose LEVEL  set log level: quiet(0), normal(1), verbose(2), debug(3)\n");
    printf("                       default is normal(1)\n");
    printf("\n");
    printf("  frame-rate control examples:\n");
    printf("    phy=2:1       keep 1 of every 2 frames on physical channel\n");
    printf("    virt1=3:1     keep 1 of every 3 frames on virtual channel 1\n");
    printf("    virt2=5:2     keep first 2 of every 5 frames on virtual channel 2\n");
}

static VOID VI_DemoInitFrameRateCtrl(ViFrameRateCtrlS *pstCtrl)
{
    if (pstCtrl == NULL)
        return;

    pstCtrl->u32InputFrameStep = 1;
    pstCtrl->u32OutputFrameStep = 1;
}

static VOID VI_DemoPrintFrameRateCtrl(const char *tag, BOOL bEnabled, const ViFrameRateCtrlS *pstCtrl)
{
    if (tag == NULL || pstCtrl == NULL)
        return;

    LOG_INFO("[%s][FRC] %s ratio=%u:%u\n",
           tag,
           bEnabled == MPP_TRUE ? "enabled" : "disabled",
           pstCtrl->u32InputFrameStep,
           pstCtrl->u32OutputFrameStep);
}

static S32 VI_DemoParseFrameRateCtrlToken(const char *pszToken,
                                          BOOL *pbEnabled,
                                          ViFrameRateCtrlS *pstCtrl)
{
    U32 u32Input = 0;
    U32 u32Output = 0;

    if (pszToken == NULL || pbEnabled == NULL || pstCtrl == NULL)
        return -1;

    if (sscanf(pszToken, "%u:%u", &u32Input, &u32Output) != 2)
        return -1;
    if (u32Input == 0 || u32Output == 0 || u32Output > u32Input)
        return -1;

    pstCtrl->u32InputFrameStep = u32Input;
    pstCtrl->u32OutputFrameStep = u32Output;
    *pbEnabled = (u32Input == 1 && u32Output == 1) ? MPP_FALSE : MPP_TRUE;
    return 0;
}

static S32 VI_DemoApplyFrameRateArg(VI_DEMO_CONFIG_S *pstCfg, const char *pszArg)
{
    const char *pszEq = NULL;
    size_t uKeyLen;

    if (pstCfg == NULL || pszArg == NULL)
        return -1;

    pszEq = strchr(pszArg, '=');
    if (pszEq == NULL)
        return -1;

    uKeyLen = (size_t)(pszEq - pszArg);
    if (uKeyLen == 3 && strncmp(pszArg, "phy", 3) == 0)
        return VI_DemoParseFrameRateCtrlToken(pszEq + 1, &pstCfg->bEnablePhyFrameRateCtrl, &pstCfg->stPhyFrameRateCtrl);
    if (uKeyLen == 5 && strncmp(pszArg, "virt1", 5) == 0)
        return VI_DemoParseFrameRateCtrlToken(pszEq + 1, &pstCfg->bEnableVirt1FrameRateCtrl, &pstCfg->stVirt1FrameRateCtrl);
    if (uKeyLen == 5 && strncmp(pszArg, "virt2", 5) == 0)
        return VI_DemoParseFrameRateCtrlToken(pszEq + 1, &pstCfg->bEnableVirt2FrameRateCtrl, &pstCfg->stVirt2FrameRateCtrl);
    if (uKeyLen == 5 && strncmp(pszArg, "virt3", 5) == 0)
        return VI_DemoParseFrameRateCtrlToken(pszEq + 1, &pstCfg->bEnableVirt3FrameRateCtrl, &pstCfg->stVirt3FrameRateCtrl);
    if (uKeyLen == 5 && strncmp(pszArg, "virt4", 5) == 0)
        return VI_DemoParseFrameRateCtrlToken(pszEq + 1, &pstCfg->bEnableVirt4FrameRateCtrl, &pstCfg->stVirt4FrameRateCtrl);
    if (uKeyLen == 5 && strncmp(pszArg, "virt5", 5) == 0)
        return VI_DemoParseFrameRateCtrlToken(pszEq + 1, &pstCfg->bEnableVirt5FrameRateCtrl, &pstCfg->stVirt5FrameRateCtrl);

    return -1;
}

static void VI_DemoInitConfig(VI_DEMO_CONFIG_S *pstCfg, VI_DEMO_MODE_E enMode)
{
    if (pstCfg == NULL)
        return;

    memset(pstCfg, 0, sizeof(*pstCfg));
    pstCfg->ViDev = DEMO_DEFAULT_DEV;
    pstCfg->ViPhyChn = DEMO_PHY_CHN;
    pstCfg->ViVirtChn1 = DEMO_VIRT_CHN_1;
    pstCfg->ViVirtChn2 = DEMO_VIRT_CHN_2;
    pstCfg->ViVirtChn3 = DEMO_VIRT_CHN_3;
    pstCfg->ViVirtChn4 = DEMO_VIRT_CHN_4;
    pstCfg->ViVirtChn5 = DEMO_VIRT_CHN_5;
    pstCfg->u32FrameCount = DEMO_DEFAULT_FRAME_COUNT;
    pstCfg->s32TimeoutMs = DEMO_DEFAULT_TIMEOUT_MS;
    VI_DemoInitFrameRateCtrl(&pstCfg->stPhyFrameRateCtrl);
    VI_DemoInitFrameRateCtrl(&pstCfg->stVirt1FrameRateCtrl);
    VI_DemoInitFrameRateCtrl(&pstCfg->stVirt2FrameRateCtrl);
    VI_DemoInitFrameRateCtrl(&pstCfg->stVirt3FrameRateCtrl);
    VI_DemoInitFrameRateCtrl(&pstCfg->stVirt4FrameRateCtrl);
    VI_DemoInitFrameRateCtrl(&pstCfg->stVirt5FrameRateCtrl);

    pstCfg->stDevAttr.eWorkMode = VI_WORK_MODE_ONLINE;
    pstCfg->stDevAttr.u32Width = 3864;
    pstCfg->stDevAttr.u32Height = 2192;
    pstCfg->stDevAttr.u32MipiLaneNum = 4;
    pstCfg->stDevAttr.bCapture2Preview = MPP_FALSE;

    pstCfg->stPhyAttr.eChnType = VI_CHN_TYPE_PHYSICAL;
    pstCfg->stPhyAttr.ePixelFormat = MPP_PIXEL_FORMAT_NV12;
    pstCfg->stPhyAttr.u32Width = 1920;
    pstCfg->stPhyAttr.u32Height = 1080;
//    pstCfg->stPhyAttr.eStrideAlign = VI_STRIDE_ALIGN_64;

    pstCfg->stVirt1Attr = pstCfg->stPhyAttr;
    pstCfg->stVirt1Attr.eChnType = VI_CHN_TYPE_VIRTUAL;
    pstCfg->stVirt1Attr.u32Width = 1920;
    pstCfg->stVirt1Attr.u32Height = 1080;
    //pstCfg->stVirt1Attr.eRotateMode = VI_ROT_270;
    // pstCfg->stVirt1Attr.bCropEnable = MPP_FALSE;
    // pstCfg->stVirt1Attr.u32CropX = 0;
    // pstCfg->stVirt1Attr.u32CropY = 0;
    // pstCfg->stVirt1Attr.u32CropWidth = pstCfg->stVirt1Attr.u32Width;
    // pstCfg->stVirt1Attr.u32CropHeight = pstCfg->stVirt1Attr.u32Height;
    // pstCfg->stVirt1Attr.eStrideAlign = VI_STRIDE_ALIGN_32;

    pstCfg->stVirt2Attr = pstCfg->stPhyAttr;	
    pstCfg->stVirt2Attr.eChnType = VI_CHN_TYPE_VIRTUAL;
    pstCfg->stVirt2Attr.u32Width = 1920;
    pstCfg->stVirt2Attr.u32Height = 1080;
    //pstCfg->stVirt2Attr.eRotateMode = VI_ROT_FLIP;
    // pstCfg->stVirt2Attr.bCropEnable = MPP_TRUE;
    // pstCfg->stVirt2Attr.u32CropX = 160;
    // pstCfg->stVirt2Attr.u32CropY = 80;
    // pstCfg->stVirt2Attr.u32CropWidth = 320;
    // pstCfg->stVirt2Attr.u32CropHeight = 180;
//   pstCfg->stVirt2Attr.eStrideAlign = VI_STRIDE_ALIGN_64;

    pstCfg->stVirt3Attr = pstCfg->stPhyAttr;
    pstCfg->stVirt3Attr.eChnType = VI_CHN_TYPE_VIRTUAL;
    pstCfg->stVirt3Attr.u32Width = 1920;
    pstCfg->stVirt3Attr.u32Height = 1080;

    pstCfg->stVirt4Attr = pstCfg->stPhyAttr;
    pstCfg->stVirt4Attr.eChnType = VI_CHN_TYPE_VIRTUAL;
    pstCfg->stVirt4Attr.u32Width = 1920;
    pstCfg->stVirt4Attr.u32Height = 1080;

    pstCfg->stVirt5Attr = pstCfg->stPhyAttr;
    pstCfg->stVirt5Attr.eChnType = VI_CHN_TYPE_VIRTUAL;
    pstCfg->stVirt5Attr.u32Width = 1920;
    pstCfg->stVirt5Attr.u32Height = 1080;

    switch (enMode) {
    case VI_DEMO_MODE_BASIC:
        pstCfg->bEnableVirt1 = MPP_FALSE;
        pstCfg->bEnableVirt2 = MPP_FALSE;
        pstCfg->bEnableVirt3 = MPP_FALSE;
        pstCfg->bEnableVirt4 = MPP_FALSE;
        pstCfg->bEnableVirt5 = MPP_FALSE;
        pstCfg->bDumpRaw = MPP_FALSE;
        break;
    case VI_DEMO_MODE_VIRTUAL:
        pstCfg->bEnableVirt1 = MPP_TRUE;
        pstCfg->bEnableVirt2 = MPP_TRUE;
        pstCfg->bEnableVirt3 = MPP_TRUE;
        pstCfg->bEnableVirt4 = MPP_TRUE;
        pstCfg->bEnableVirt5 = MPP_TRUE;
        pstCfg->bDumpRaw = MPP_FALSE;
        break;
    case VI_DEMO_MODE_RAWDUMP:
        pstCfg->bEnableVirt1 = MPP_FALSE;
        pstCfg->bEnableVirt2 = MPP_FALSE;
        pstCfg->bEnableVirt3 = MPP_FALSE;
        pstCfg->bEnableVirt4 = MPP_FALSE;
        pstCfg->bEnableVirt5 = MPP_FALSE;
        pstCfg->bDumpRaw = MPP_TRUE;
        break;
    case VI_DEMO_MODE_ALL:
    default:
        pstCfg->bEnableVirt1 = MPP_TRUE;
        pstCfg->bEnableVirt2 = MPP_TRUE;
        pstCfg->bEnableVirt3 = MPP_TRUE;
        pstCfg->bEnableVirt4 = MPP_TRUE;
        pstCfg->bEnableVirt5 = MPP_TRUE;
        pstCfg->bDumpRaw = MPP_TRUE;
        break;
    }
}

static const char *VI_DemoGetDumpFileName(const char *tag)
{
    if (tag == NULL)
        return NULL;

    if (strcmp(tag, "phy0") == 0 || strcmp(tag, "phy0-before-raw") == 0)
        return "vi_phy0_last_frame";
    if (strcmp(tag, "virt1") == 0)
        return "vi_virt1_last_frame";
    if (strcmp(tag, "virt2") == 0)
        return "vi_virt2_last_frame";
    if (strcmp(tag, "virt3") == 0)
        return "vi_virt3_last_frame";
    if (strcmp(tag, "virt4") == 0)
        return "vi_virt4_last_frame";
    if (strcmp(tag, "virt5") == 0)
        return "vi_virt5_last_frame";
    if (strcmp(tag, "rawdump") == 0)
        return "vi_rawdump_last_frame";

    return "vi_unknown_last_frame";
}

static const char *VI_DemoGetDumpFileExt(const VideoFrameInfo *pstFrame, const char *tag)
{
    if (tag != NULL && strcmp(tag, "rawdump") == 0)
        return ".raw";

    if (pstFrame == NULL)
        return ".bin";

    switch (pstFrame->stViFrameInfo.stCommFrameInfo.ePixelFormat) {
    case MPP_PIXEL_FORMAT_NV12:
    case MPP_PIXEL_FORMAT_NV21:
        return ".yuv";
    default:
        return ".bin";
    }
}

static S32 VI_DemoSaveFrameToFile(const char *tag, const VideoFrameInfo *pstFrame)
{
    char szFileName[128];
    const char *pszBaseName = NULL;
    const char *pszExt = NULL;
    FILE *fp = NULL;
    U32 u32Plane = 0;

    if (tag == NULL || pstFrame == NULL)
        return -1;

    pszBaseName = VI_DemoGetDumpFileName(tag);
    pszExt = VI_DemoGetDumpFileExt(pstFrame, tag);
    if (pszBaseName == NULL || pszExt == NULL)
        return -1;

    if (snprintf(szFileName, sizeof(szFileName), "%s%s", pszBaseName, pszExt) >= (S32)sizeof(szFileName)) {
        LOG_ERROR("[%s] dump file name too long\n", tag);
        return -1;
    }

    fp = fopen(szFileName, "wb");
    if (fp == NULL) {
        LOG_ERROR("[%s] fopen %s failed\n", tag, szFileName);
        return -1;
    }

    for (u32Plane = 0; u32Plane < pstFrame->stVFrame.u32PlaneNum && u32Plane < FRAME_MAX_PLANE; u32Plane++) {
        const void *pVirAddr = (const void *)pstFrame->stVFrame.ulPlaneVirAddr[u32Plane];
        U32 u32WriteSize = pstFrame->stVFrame.u32PlaneSizeValid[u32Plane];

        if (pVirAddr == NULL || u32WriteSize == 0)
            continue;

        if (fwrite(pVirAddr, 1, u32WriteSize, fp) != u32WriteSize) {
            fclose(fp);
            LOG_ERROR("[%s] fwrite %s failed at plane %u\n", tag, szFileName, u32Plane);
            return -1;
        }
    }

    fclose(fp);
    LOG_INFO("[%s] last frame saved to %s\n", tag, szFileName);
    return 0;
}

static void VI_DemoDumpFrameInfo(const char *tag, VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstFrame)
{
    const CommonFrameInfo *pstComm = NULL;

    if (pstFrame == NULL)
        return;

    pstComm = &pstFrame->stViFrameInfo.stCommFrameInfo;
    LOG_DEBUG("[%s] dev=%d chn=%d idx=%u buf=%lu pool=%lu pts=%llu size=%ux%u fmt=%d total=%u planes=%u valid0=%u\n",
           tag,
           ViDev,
           ViChn,
           pstFrame->u32Idx,
           pstFrame->ulBufferId,
           pstFrame->ulPoolId,
           (unsigned long long)pstFrame->stVFrame.u64PTS,
           pstComm->u32Width,
           pstComm->u32Height,
           pstComm->ePixelFormat,
           pstFrame->stVFrame.u32TotalSize,
           pstFrame->stVFrame.u32PlaneNum,
           pstFrame->stVFrame.u32PlaneSizeValid[0]);
}

static S32 VI_DemoGetAndReleaseFrame(VI_DEV ViDev, VI_CHN ViChn, S32 s32TimeoutMs,
                                     const char *tag, const VI_DEMO_CONFIG_S *pstCfg,
                                     void *pstDumpState,
                                     BOOL bSaveThisFrame)
{
    VideoFrameInfo stFrame;
    S32 s32Ret = 0;

    memset(&stFrame, 0, sizeof(stFrame));
    s32Ret = VI_GetChnFrame(ViDev, ViChn, &stFrame, s32TimeoutMs);
    if (s32Ret != 0) {
        if (s32Ret == -4)
            LOG_VERBOSE("[%s] no frame yet\n", tag);
        else
            LOG_ERROR("[%s] VI_GetChnFrame failed, ret=%d, skip this round\n", tag, s32Ret);
        return s32Ret;
    }

    VI_DemoDumpFrameInfo(tag, ViDev, ViChn, &stFrame);

    (void)pstCfg;
    (void)pstDumpState;

#if DEMO_SAVE_LAST_FRAME
    if (bSaveThisFrame == MPP_TRUE)
        (void)VI_DemoSaveFrameToFile(tag, &stFrame);
#else
    (void)bSaveThisFrame;
#endif

    s32Ret = VI_ReleaseChnFrame(ViDev, ViChn, &stFrame);
    if (s32Ret != 0) {
        LOG_ERROR("[%s] VI_ReleaseChnFrame failed, ret=%d, skip this round\n", tag, s32Ret);
        return s32Ret;
    }

    return 0;
}

static S32 VI_DemoRunRawDump(VI_DEV ViDev, VI_CHN ViChn, S32 s32TimeoutMs,
                             const VI_DEMO_CONFIG_S *pstCfg,
                             void *pstDumpState)
{
    VideoFrameInfo stRawFrame;
    S32 s32Ret = 0;

    memset(&stRawFrame, 0, sizeof(stRawFrame));

    s32Ret = VI_TriggerRawDump(ViDev, ViChn);
    if (s32Ret != 0) {
        LOG_ERROR("[rawdump] VI_TriggerRawDump failed, ret=%d\n", s32Ret);
        return s32Ret;
    }
	usleep(200*1000); // sleep 200ms to wait raw frame ready, or directly call VI_GetRawDumpFrame may get -4 (frame not ready)

    s32Ret = VI_GetRawDumpFrame(ViDev, ViChn, &stRawFrame, s32TimeoutMs);
    if (s32Ret != 0) {
        LOG_ERROR("[rawdump] VI_GetRawDumpFrame failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    VI_DemoDumpFrameInfo("rawdump", ViDev, ViChn, &stRawFrame);

    (void)pstCfg;
    (void)pstDumpState;

#if DEMO_SAVE_LAST_FRAME
    (void)VI_DemoSaveFrameToFile("rawdump", &stRawFrame);
#endif

    s32Ret = VI_ReleaseRawDumpFrame(ViDev, ViChn, &stRawFrame);
    if (s32Ret != 0) {
        LOG_ERROR("[rawdump] VI_ReleaseRawDumpFrame failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    return 0;
}

static S32 VI_DemoSetup(const VI_DEMO_CONFIG_S *pstCfg)
{
    S32 s32Ret = 0;

    if (pstCfg == NULL)
        return -1;

    LOG_INFO("last-frame dump: %s\n",
            DEMO_SAVE_LAST_FRAME ? "enabled" : "disabled");
    VI_DemoPrintFrameRateCtrl("phy0", pstCfg->bEnablePhyFrameRateCtrl, &pstCfg->stPhyFrameRateCtrl);
    if (pstCfg->bEnableVirt1 == MPP_TRUE)
        VI_DemoPrintFrameRateCtrl("virt1", pstCfg->bEnableVirt1FrameRateCtrl, &pstCfg->stVirt1FrameRateCtrl);
    if (pstCfg->bEnableVirt2 == MPP_TRUE)
        VI_DemoPrintFrameRateCtrl("virt2", pstCfg->bEnableVirt2FrameRateCtrl, &pstCfg->stVirt2FrameRateCtrl);
    if (pstCfg->bEnableVirt3 == MPP_TRUE)
        VI_DemoPrintFrameRateCtrl("virt3", pstCfg->bEnableVirt3FrameRateCtrl, &pstCfg->stVirt3FrameRateCtrl);
    if (pstCfg->bEnableVirt4 == MPP_TRUE)
        VI_DemoPrintFrameRateCtrl("virt4", pstCfg->bEnableVirt4FrameRateCtrl, &pstCfg->stVirt4FrameRateCtrl);
    if (pstCfg->bEnableVirt5 == MPP_TRUE)
        VI_DemoPrintFrameRateCtrl("virt5", pstCfg->bEnableVirt5FrameRateCtrl, &pstCfg->stVirt5FrameRateCtrl);

    s32Ret = VI_Init();
    if (s32Ret != 0) {
        LOG_ERROR("VI_Init failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    s32Ret = VI_SetDevAttr(pstCfg->ViDev, &pstCfg->stDevAttr);
    if (s32Ret != 0) {
        LOG_ERROR("VI_SetDevAttr failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    s32Ret = VI_SetChnAttr(pstCfg->ViDev, pstCfg->ViPhyChn, &pstCfg->stPhyAttr);
    if (s32Ret != 0) {
        LOG_ERROR("VI_SetChnAttr phy failed, ret=%d\n", s32Ret);
        return s32Ret;
    }
    if (pstCfg->bEnablePhyFrameRateCtrl == MPP_TRUE) {
        s32Ret = VI_SetChnFrameRate(pstCfg->ViDev, pstCfg->ViPhyChn, &pstCfg->stPhyFrameRateCtrl);
        if (s32Ret != 0) {
            LOG_ERROR("VI_SetChnFrameRate phy failed, ret=%d\n", s32Ret);
            return s32Ret;
        }
    }

    LOG_INFO("phy attr: %ux%u mirror=%d flip=%d rotate=%d crop=%d[%u,%u,%u,%u] align=%d\n",
           pstCfg->stPhyAttr.u32Width,
           pstCfg->stPhyAttr.u32Height,
           pstCfg->stPhyAttr.bMirror,
            pstCfg->stPhyAttr.bFlip,
            pstCfg->stPhyAttr.eRotateMode,
            pstCfg->stPhyAttr.bCropEnable,
            pstCfg->stPhyAttr.u32CropX,
            pstCfg->stPhyAttr.u32CropY,
            pstCfg->stPhyAttr.u32CropWidth,
            pstCfg->stPhyAttr.u32CropHeight,
            pstCfg->stPhyAttr.eStrideAlign);

    if (pstCfg->bEnableVirt1 == MPP_TRUE) {
        s32Ret = VI_SetChnAttr(pstCfg->ViDev, pstCfg->ViVirtChn1, &pstCfg->stVirt1Attr);
        if (s32Ret != 0) {
            LOG_ERROR("VI_SetChnAttr virt1 failed, ret=%d\n", s32Ret);
            return s32Ret;
        }
        if (pstCfg->bEnableVirt1FrameRateCtrl == MPP_TRUE) {
            s32Ret = VI_SetChnFrameRate(pstCfg->ViDev, pstCfg->ViVirtChn1, &pstCfg->stVirt1FrameRateCtrl);
            if (s32Ret != 0) {
                LOG_ERROR("VI_SetChnFrameRate virt1 failed, ret=%d\n", s32Ret);
                return s32Ret;
            }
        }

        LOG_INFO("virt1 attr: %ux%u mirror=%d flip=%d rotate=%d crop=%d[%u,%u,%u,%u] align=%d\n",
               pstCfg->stVirt1Attr.u32Width,
               pstCfg->stVirt1Attr.u32Height,
               pstCfg->stVirt1Attr.bMirror,
             pstCfg->stVirt1Attr.bFlip,
                         pstCfg->stVirt1Attr.eRotateMode,
                         pstCfg->stVirt1Attr.bCropEnable,
                         pstCfg->stVirt1Attr.u32CropX,
                         pstCfg->stVirt1Attr.u32CropY,
                         pstCfg->stVirt1Attr.u32CropWidth,
                         pstCfg->stVirt1Attr.u32CropHeight,
                         pstCfg->stVirt1Attr.eStrideAlign);
    }

    if (pstCfg->bEnableVirt2 == MPP_TRUE) {
        s32Ret = VI_SetChnAttr(pstCfg->ViDev, pstCfg->ViVirtChn2, &pstCfg->stVirt2Attr);
        if (s32Ret != 0) {
            LOG_ERROR("VI_SetChnAttr virt2 failed, ret=%d\n", s32Ret);
            return s32Ret;
        }
        if (pstCfg->bEnableVirt2FrameRateCtrl == MPP_TRUE) {
            s32Ret = VI_SetChnFrameRate(pstCfg->ViDev, pstCfg->ViVirtChn2, &pstCfg->stVirt2FrameRateCtrl);
            if (s32Ret != 0) {
                LOG_ERROR("VI_SetChnFrameRate virt2 failed, ret=%d\n", s32Ret);
                return s32Ret;
            }
        }

        LOG_INFO("virt2 attr: %ux%u mirror=%d flip=%d rotate=%d crop=%d[%u,%u,%u,%u] align=%d\n",
               pstCfg->stVirt2Attr.u32Width,
               pstCfg->stVirt2Attr.u32Height,
               pstCfg->stVirt2Attr.bMirror,
             pstCfg->stVirt2Attr.bFlip,
                         pstCfg->stVirt2Attr.eRotateMode,
                         pstCfg->stVirt2Attr.bCropEnable,
                         pstCfg->stVirt2Attr.u32CropX,
                         pstCfg->stVirt2Attr.u32CropY,
                         pstCfg->stVirt2Attr.u32CropWidth,
                         pstCfg->stVirt2Attr.u32CropHeight,
                         pstCfg->stVirt2Attr.eStrideAlign);
    }

    if (pstCfg->bEnableVirt3 == MPP_TRUE) {
        s32Ret = VI_SetChnAttr(pstCfg->ViDev, pstCfg->ViVirtChn3, &pstCfg->stVirt3Attr);
        if (s32Ret != 0) {
            LOG_ERROR("VI_SetChnAttr virt3 failed, ret=%d\n", s32Ret);
            return s32Ret;
        }
        if (pstCfg->bEnableVirt3FrameRateCtrl == MPP_TRUE) {
            s32Ret = VI_SetChnFrameRate(pstCfg->ViDev, pstCfg->ViVirtChn3, &pstCfg->stVirt3FrameRateCtrl);
            if (s32Ret != 0) {
                LOG_ERROR("VI_SetChnFrameRate virt3 failed, ret=%d\n", s32Ret);
                return s32Ret;
            }
        }

        LOG_INFO("virt3 attr: %ux%u mirror=%d flip=%d rotate=%d crop=%d[%u,%u,%u,%u] align=%d\n",
               pstCfg->stVirt3Attr.u32Width,
               pstCfg->stVirt3Attr.u32Height,
               pstCfg->stVirt3Attr.bMirror,
               pstCfg->stVirt3Attr.bFlip,
               pstCfg->stVirt3Attr.eRotateMode,
               pstCfg->stVirt3Attr.bCropEnable,
               pstCfg->stVirt3Attr.u32CropX,
               pstCfg->stVirt3Attr.u32CropY,
               pstCfg->stVirt3Attr.u32CropWidth,
               pstCfg->stVirt3Attr.u32CropHeight,
               pstCfg->stVirt3Attr.eStrideAlign);
    }

    if (pstCfg->bEnableVirt4 == MPP_TRUE) {
        s32Ret = VI_SetChnAttr(pstCfg->ViDev, pstCfg->ViVirtChn4, &pstCfg->stVirt4Attr);
        if (s32Ret != 0) {
            LOG_ERROR("VI_SetChnAttr virt4 failed, ret=%d\n", s32Ret);
            return s32Ret;
        }
        if (pstCfg->bEnableVirt4FrameRateCtrl == MPP_TRUE) {
            s32Ret = VI_SetChnFrameRate(pstCfg->ViDev, pstCfg->ViVirtChn4, &pstCfg->stVirt4FrameRateCtrl);
            if (s32Ret != 0) {
                LOG_ERROR("VI_SetChnFrameRate virt4 failed, ret=%d\n", s32Ret);
                return s32Ret;
            }
        }

        LOG_INFO("virt4 attr: %ux%u mirror=%d flip=%d rotate=%d crop=%d[%u,%u,%u,%u] align=%d\n",
               pstCfg->stVirt4Attr.u32Width,
               pstCfg->stVirt4Attr.u32Height,
               pstCfg->stVirt4Attr.bMirror,
               pstCfg->stVirt4Attr.bFlip,
               pstCfg->stVirt4Attr.eRotateMode,
               pstCfg->stVirt4Attr.bCropEnable,
               pstCfg->stVirt4Attr.u32CropX,
               pstCfg->stVirt4Attr.u32CropY,
               pstCfg->stVirt4Attr.u32CropWidth,
               pstCfg->stVirt4Attr.u32CropHeight,
               pstCfg->stVirt4Attr.eStrideAlign);
    }

    if (pstCfg->bEnableVirt5 == MPP_TRUE) {
        s32Ret = VI_SetChnAttr(pstCfg->ViDev, pstCfg->ViVirtChn5, &pstCfg->stVirt5Attr);
        if (s32Ret != 0) {
            LOG_ERROR("VI_SetChnAttr virt5 failed, ret=%d\n", s32Ret);
            return s32Ret;
        }
        if (pstCfg->bEnableVirt5FrameRateCtrl == MPP_TRUE) {
            s32Ret = VI_SetChnFrameRate(pstCfg->ViDev, pstCfg->ViVirtChn5, &pstCfg->stVirt5FrameRateCtrl);
            if (s32Ret != 0) {
                LOG_ERROR("VI_SetChnFrameRate virt5 failed, ret=%d\n", s32Ret);
                return s32Ret;
            }
        }

        LOG_INFO("virt5 attr: %ux%u mirror=%d flip=%d rotate=%d crop=%d[%u,%u,%u,%u] align=%d\n",
               pstCfg->stVirt5Attr.u32Width,
               pstCfg->stVirt5Attr.u32Height,
               pstCfg->stVirt5Attr.bMirror,
               pstCfg->stVirt5Attr.bFlip,
               pstCfg->stVirt5Attr.eRotateMode,
               pstCfg->stVirt5Attr.bCropEnable,
               pstCfg->stVirt5Attr.u32CropX,
               pstCfg->stVirt5Attr.u32CropY,
               pstCfg->stVirt5Attr.u32CropWidth,
               pstCfg->stVirt5Attr.u32CropHeight,
               pstCfg->stVirt5Attr.eStrideAlign);
    }

    s32Ret = VI_EnableDev(pstCfg->ViDev);
    if (s32Ret != 0) {
        LOG_ERROR("VI_EnableDev failed, ret=%d\n", s32Ret);
        return s32Ret;
    }
	
    s32Ret = VI_EnableChn(pstCfg->ViDev, pstCfg->ViPhyChn);
    if (s32Ret != 0) {
        LOG_ERROR("VI_EnableChn phy failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    if (pstCfg->bEnableVirt1 == MPP_TRUE) {
        s32Ret = VI_EnableChn(pstCfg->ViDev, pstCfg->ViVirtChn1);
        if (s32Ret != 0) {
            LOG_ERROR("VI_EnableChn virt1 failed, ret=%d\n", s32Ret);
            return s32Ret;
        }
    }

    if (pstCfg->bEnableVirt2 == MPP_TRUE) {
        s32Ret = VI_EnableChn(pstCfg->ViDev, pstCfg->ViVirtChn2);
        if (s32Ret != 0) {
            LOG_ERROR("VI_EnableChn virt2 failed, ret=%d\n", s32Ret);
            return s32Ret;
        }
    }

    if (pstCfg->bEnableVirt3 == MPP_TRUE) {
        s32Ret = VI_EnableChn(pstCfg->ViDev, pstCfg->ViVirtChn3);
        if (s32Ret != 0) {
            LOG_ERROR("VI_EnableChn virt3 failed, ret=%d\n", s32Ret);
            return s32Ret;
        }
    }

    if (pstCfg->bEnableVirt4 == MPP_TRUE) {
        s32Ret = VI_EnableChn(pstCfg->ViDev, pstCfg->ViVirtChn4);
        if (s32Ret != 0) {
            LOG_ERROR("VI_EnableChn virt4 failed, ret=%d\n", s32Ret);
            return s32Ret;
        }
    }

    if (pstCfg->bEnableVirt5 == MPP_TRUE) {
        s32Ret = VI_EnableChn(pstCfg->ViDev, pstCfg->ViVirtChn5);
        if (s32Ret != 0) {
            LOG_ERROR("VI_EnableChn virt5 failed, ret=%d\n", s32Ret);
            return s32Ret;
        }
    }

    return 0;
}

static void VI_DemoTeardown(const VI_DEMO_CONFIG_S *pstCfg)
{
    if (pstCfg == NULL)
        return;

    if (pstCfg->bEnableVirt5 == MPP_TRUE)
        (void)VI_DisableChn(pstCfg->ViDev, pstCfg->ViVirtChn5);
    if (pstCfg->bEnableVirt4 == MPP_TRUE)
        (void)VI_DisableChn(pstCfg->ViDev, pstCfg->ViVirtChn4);
    if (pstCfg->bEnableVirt3 == MPP_TRUE)
        (void)VI_DisableChn(pstCfg->ViDev, pstCfg->ViVirtChn3);
    if (pstCfg->bEnableVirt2 == MPP_TRUE)
        (void)VI_DisableChn(pstCfg->ViDev, pstCfg->ViVirtChn2);
    if (pstCfg->bEnableVirt1 == MPP_TRUE)
        (void)VI_DisableChn(pstCfg->ViDev, pstCfg->ViVirtChn1);

    (void)VI_DisableChn(pstCfg->ViDev, pstCfg->ViPhyChn);
    (void)VI_DisableDev(pstCfg->ViDev);
    (void)VI_DeInit();
}

static S32 VI_DemoRunBasic(const VI_DEMO_CONFIG_S *pstCfg)
{
    U32 i = 0;
    S32 s32Ret = 0;
    VI_DEMO_FPS_STAT_S stFpsStat;

    memset(&stFpsStat, 0, sizeof(stFpsStat));
    stFpsStat.tag = "phy0";

    LOG_INFO("==== run basic pipeline test ====\n");
    for (i = 0; i < pstCfg->u32FrameCount; i++) {
        s32Ret = VI_DemoGetAndReleaseFrame(pstCfg->ViDev, pstCfg->ViPhyChn,
                           pstCfg->s32TimeoutMs, "phy0", pstCfg, NULL,
                           (i == (pstCfg->u32FrameCount - 1)) ? MPP_TRUE : MPP_FALSE);
        if (s32Ret != 0)
            return s32Ret;

        VI_DemoFpsStatUpdate(&stFpsStat);
		usleep(33 * 1000);
    }

    VI_DemoFpsStatPrintFinal(&stFpsStat);
    if (pstCfg->bEnablePhyFrameRateCtrl == MPP_TRUE) {
        LOG_INFO("[verify][phy0] expected fps ~= source_fps * %u / %u\n",
               pstCfg->stPhyFrameRateCtrl.u32OutputFrameStep,
               pstCfg->stPhyFrameRateCtrl.u32InputFrameStep);
    }

    return 0;
}

static S32 VI_DemoRunVirtual(const VI_DEMO_CONFIG_S *pstCfg)
{
    U32 i = 0;
    S32 s32Ret = 0;
    VI_DEMO_FPS_STAT_S stPhyFpsStat;
    VI_DEMO_FPS_STAT_S stVirt1FpsStat;
    VI_DEMO_FPS_STAT_S stVirt2FpsStat;
    VI_DEMO_FPS_STAT_S stVirt3FpsStat;
    VI_DEMO_FPS_STAT_S stVirt4FpsStat;
    VI_DEMO_FPS_STAT_S stVirt5FpsStat;

    memset(&stPhyFpsStat, 0, sizeof(stPhyFpsStat));
    memset(&stVirt1FpsStat, 0, sizeof(stVirt1FpsStat));
    memset(&stVirt2FpsStat, 0, sizeof(stVirt2FpsStat));
    memset(&stVirt3FpsStat, 0, sizeof(stVirt3FpsStat));
    memset(&stVirt4FpsStat, 0, sizeof(stVirt4FpsStat));
    memset(&stVirt5FpsStat, 0, sizeof(stVirt5FpsStat));
    stPhyFpsStat.tag = "phy0";
    stVirt1FpsStat.tag = "virt1";
    stVirt2FpsStat.tag = "virt2";
    stVirt3FpsStat.tag = "virt3";
    stVirt4FpsStat.tag = "virt4";
    stVirt5FpsStat.tag = "virt5";

    LOG_INFO("==== run virtual channel test ====\n");
    for (i = 0; i < pstCfg->u32FrameCount; i++) {
        s32Ret = VI_DemoGetAndReleaseFrame(pstCfg->ViDev, pstCfg->ViPhyChn,
                           pstCfg->s32TimeoutMs, "phy0", pstCfg, NULL,
                           (i == (pstCfg->u32FrameCount - 1)) ? MPP_TRUE : MPP_FALSE);

        if (s32Ret == 0)
            VI_DemoFpsStatUpdate(&stPhyFpsStat);
		

        if (pstCfg->bEnableVirt1 == MPP_TRUE) {
            s32Ret =  VI_DemoGetAndReleaseFrame(pstCfg->ViDev, pstCfg->ViVirtChn1,
                                           pstCfg->s32TimeoutMs, "virt1", pstCfg, NULL,
                                           (i == (pstCfg->u32FrameCount - 1)) ? MPP_TRUE : MPP_FALSE);
            if (s32Ret == 0)
                VI_DemoFpsStatUpdate(&stVirt1FpsStat);
        }

        if (pstCfg->bEnableVirt2 == MPP_TRUE) {
            s32Ret =  VI_DemoGetAndReleaseFrame(pstCfg->ViDev, pstCfg->ViVirtChn2,
                                           pstCfg->s32TimeoutMs, "virt2", pstCfg, NULL,
                                           (i == (pstCfg->u32FrameCount - 1)) ? MPP_TRUE : MPP_FALSE);
            if (s32Ret == 0)
                VI_DemoFpsStatUpdate(&stVirt2FpsStat);
        }

        if (pstCfg->bEnableVirt3 == MPP_TRUE) {
            s32Ret = VI_DemoGetAndReleaseFrame(pstCfg->ViDev, pstCfg->ViVirtChn3,
                                           pstCfg->s32TimeoutMs, "virt3", pstCfg, NULL,
                                           (i == (pstCfg->u32FrameCount - 1)) ? MPP_TRUE : MPP_FALSE);
            if (s32Ret == 0)
                VI_DemoFpsStatUpdate(&stVirt3FpsStat);
        }

        if (pstCfg->bEnableVirt4 == MPP_TRUE) {
            s32Ret = VI_DemoGetAndReleaseFrame(pstCfg->ViDev, pstCfg->ViVirtChn4,
                                           pstCfg->s32TimeoutMs, "virt4", pstCfg, NULL,
                                           (i == (pstCfg->u32FrameCount - 1)) ? MPP_TRUE : MPP_FALSE);
            if (s32Ret == 0)
                VI_DemoFpsStatUpdate(&stVirt4FpsStat);
        }

        if (pstCfg->bEnableVirt5 == MPP_TRUE) {
            s32Ret = VI_DemoGetAndReleaseFrame(pstCfg->ViDev, pstCfg->ViVirtChn5,
                                           pstCfg->s32TimeoutMs, "virt5", pstCfg, NULL,
                                           (i == (pstCfg->u32FrameCount - 1)) ? MPP_TRUE : MPP_FALSE);
            if (s32Ret == 0)
                VI_DemoFpsStatUpdate(&stVirt5FpsStat);
        }
		usleep(33 * 1000);
    }
	

    VI_DemoFpsStatPrintFinal(&stPhyFpsStat);
    if (pstCfg->bEnableVirt1 == MPP_TRUE)
        VI_DemoFpsStatPrintFinal(&stVirt1FpsStat);
    if (pstCfg->bEnableVirt2 == MPP_TRUE)
        VI_DemoFpsStatPrintFinal(&stVirt2FpsStat);
    if (pstCfg->bEnableVirt3 == MPP_TRUE)
        VI_DemoFpsStatPrintFinal(&stVirt3FpsStat);
    if (pstCfg->bEnableVirt4 == MPP_TRUE)
        VI_DemoFpsStatPrintFinal(&stVirt4FpsStat);
    if (pstCfg->bEnableVirt5 == MPP_TRUE)
        VI_DemoFpsStatPrintFinal(&stVirt5FpsStat);

    if (pstCfg->bEnablePhyFrameRateCtrl == MPP_TRUE) {
        LOG_INFO("[verify][phy0] expected fps ~= source_fps * %u / %u\n",
               pstCfg->stPhyFrameRateCtrl.u32OutputFrameStep,
               pstCfg->stPhyFrameRateCtrl.u32InputFrameStep);
    }
    if (pstCfg->bEnableVirt1FrameRateCtrl == MPP_TRUE) {
        LOG_INFO("[verify][virt1] expected fps ~= source_fps * %u / %u\n",
               pstCfg->stVirt1FrameRateCtrl.u32OutputFrameStep,
               pstCfg->stVirt1FrameRateCtrl.u32InputFrameStep);
    }
    if (pstCfg->bEnableVirt2FrameRateCtrl == MPP_TRUE) {
        LOG_INFO("[verify][virt2] expected fps ~= source_fps * %u / %u\n",
               pstCfg->stVirt2FrameRateCtrl.u32OutputFrameStep,
               pstCfg->stVirt2FrameRateCtrl.u32InputFrameStep);
    }
    if (pstCfg->bEnableVirt3FrameRateCtrl == MPP_TRUE) {
        LOG_INFO("[verify][virt3] expected fps ~= source_fps * %u / %u\n",
               pstCfg->stVirt3FrameRateCtrl.u32OutputFrameStep,
               pstCfg->stVirt3FrameRateCtrl.u32InputFrameStep);
    }
    if (pstCfg->bEnableVirt4FrameRateCtrl == MPP_TRUE) {
        LOG_INFO("[verify][virt4] expected fps ~= source_fps * %u / %u\n",
               pstCfg->stVirt4FrameRateCtrl.u32OutputFrameStep,
               pstCfg->stVirt4FrameRateCtrl.u32InputFrameStep);
    }
    if (pstCfg->bEnableVirt5FrameRateCtrl == MPP_TRUE) {
        LOG_INFO("[verify][virt5] expected fps ~= source_fps * %u / %u\n",
               pstCfg->stVirt5FrameRateCtrl.u32OutputFrameStep,
               pstCfg->stVirt5FrameRateCtrl.u32InputFrameStep);
    }

    return 0;
}

static S32 VI_DemoRunRawDumpOnly(const VI_DEMO_CONFIG_S *pstCfg)
{
    U32 i = 0;
    S32 s32Ret = 0;
    VI_DEMO_FPS_STAT_S stRawFpsStat;

    memset(&stRawFpsStat, 0, sizeof(stRawFpsStat));
    stRawFpsStat.tag = "rawdump";

    LOG_INFO("==== run rawdump test ====\n");
    for (i = 0; i < 3; i++) {
        s32Ret = VI_DemoGetAndReleaseFrame(pstCfg->ViDev, pstCfg->ViPhyChn,
                           pstCfg->s32TimeoutMs, "phy0-before-raw", pstCfg, NULL,
                           MPP_FALSE);
        if (s32Ret != 0)
            return s32Ret;
    }

    for (i = 0; i < pstCfg->u32FrameCount; i++) {
        s32Ret = VI_DemoRunRawDump(pstCfg->ViDev, pstCfg->ViPhyChn,
                                   pstCfg->s32TimeoutMs, pstCfg, NULL);
        if (s32Ret != 0)
            return s32Ret;

        VI_DemoFpsStatUpdate(&stRawFpsStat);
    }

    VI_DemoFpsStatPrintFinal(&stRawFpsStat);

    return 0;
}

static VI_DEMO_MODE_E VI_DemoParseMode(const char *arg)
{
    if (arg == NULL)
        return VI_DEMO_MODE_ALL;
    if (strcmp(arg, "basic") == 0)
        return VI_DEMO_MODE_BASIC;
    if (strcmp(arg, "virtual") == 0)
        return VI_DEMO_MODE_VIRTUAL;
    if (strcmp(arg, "rawdump") == 0)
        return VI_DEMO_MODE_RAWDUMP;
    if (strcmp(arg, "all") == 0)
        return VI_DEMO_MODE_ALL;

    return VI_DEMO_MODE_ALL;
}

int main(int argc, char **argv)
{
    VI_DEMO_MODE_E enMode = VI_DEMO_MODE_ALL;
    VI_DEMO_CONFIG_S stCfg;
    S32 s32Ret = 0;
    int i;

    printf("VI bringup demo start\n");
    s32Ret = SYS_Init();
    if (s32Ret != 0) {
        LOG_ERROR("SYS_Init failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    s32Ret = VB_Init();
    if (s32Ret != 0) {
        LOG_ERROR("VB_Init failed, ret=%d\n", s32Ret);
        (void)SYS_Exit();
        return s32Ret;
    }
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            VI_DemoPrintUsage(argv[0]);
            return 0;
        }
        enMode = VI_DemoParseMode(argv[1]);
    }

    VI_DemoInitConfig(&stCfg, enMode);
    if (argc > 2) {
        int count = atoi(argv[2]);
        if (count > 0)
            stCfg.u32FrameCount = (U32)count;
    }

    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            if (i + 1 < argc) {
                int level = atoi(argv[i + 1]);
                if (level >= 0 && level <= 3) {
                    g_enLogLevel = (VI_DEMO_LOG_LEVEL_E)level;
                    i++;
                    continue;
                }
            }
            LOG_ERROR("invalid verbose level, use 0-3\n");
            VI_DemoPrintUsage(argv[0]);
            return -1;
        }
        if (VI_DemoApplyFrameRateArg(&stCfg, argv[i]) != 0) {
            LOG_ERROR("invalid frame-rate argument: %s\n", argv[i]);
            VI_DemoPrintUsage(argv[0]);
            return -1;
        }
    }

    LOG_INFO("VI bringup demo mode=%s frame_count=%u log_level=%d\n",
           VI_DemoModeName(enMode), stCfg.u32FrameCount, g_enLogLevel);

    s32Ret = VI_DemoSetup(&stCfg);
    if (s32Ret != 0) {
        VI_DemoTeardown(&stCfg);
        return s32Ret;
    }
	sleep(1);

    switch (enMode) {
    case VI_DEMO_MODE_BASIC:
        s32Ret = VI_DemoRunBasic(&stCfg);
        break;
    case VI_DEMO_MODE_VIRTUAL:
        s32Ret = VI_DemoRunVirtual(&stCfg);
        break;
    case VI_DEMO_MODE_RAWDUMP:
        s32Ret = VI_DemoRunRawDumpOnly(&stCfg);
        break;
    case VI_DEMO_MODE_ALL:
    default:
        s32Ret = VI_DemoRunBasic(&stCfg);
        if (s32Ret == 0 && (stCfg.bEnableVirt1 == MPP_TRUE || stCfg.bEnableVirt2 == MPP_TRUE))
            s32Ret = VI_DemoRunVirtual(&stCfg);
        if (s32Ret == 0 && stCfg.bDumpRaw == MPP_TRUE)
            s32Ret = VI_DemoRunRawDumpOnly(&stCfg);
        break;
    }

    VI_DemoTeardown(&stCfg);

    if (s32Ret == 0)
        printf("VI bringup demo PASS\n");
    else
        printf("VI bringup demo FAIL ret=%d\n", s32Ret);

    return s32Ret;
}
