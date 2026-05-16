#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <unistd.h>

#include "vi_api.h"
#include "sys_api.h"
#include "vb_api.h"

#define CCIC_DEMO_DEV                 0
#define CCIC_DEMO_PHY_CHN             0
#define CCIC_DEMO_DEFAULT_WIDTH       3864
#define CCIC_DEMO_DEFAULT_HEIGHT      2192
#define CCIC_DEMO_DEFAULT_MIPI_LANES  4
#define CCIC_DEMO_DEFAULT_MBPS        800
#define CCIC_DEMO_DEFAULT_TIMEOUT_MS  1000
#define CCIC_DEMO_DEFAULT_FRAME_CNT   30
#define CCIC_DEMO_DEFAULT_PIXEL_FMT   MPP_PIXEL_FORMAT_RGB_BAYER_10BITS

#ifndef FRAME_MAX_PLANE
#define FRAME_MAX_PLANE 3
#endif

typedef struct _CCIC_DEMO_CONFIG_S {
    VI_DEV ViDev;
    VI_CHN ViChn;
    U32 u32Width;
    U32 u32Height;
    U32 u32MipiLaneNum;
    U32 u32Mbps;
    U32 u32FrameCount;
    S32 s32TimeoutMs;
    MppPixelFormat ePixelFormat;
    const char    *pszDumpPath;
    BOOL bDumpLastFrame;
    ViDevAttrS stDevAttr;
    ViChnAttrS stChnAttr;
} CCIC_DEMO_CONFIG_S;

static void ccic_demo_usage(const char *prog)
{
    printf("Usage: %s [frame_count] [width] [height] [bitdepth] [dump_file]\n", prog);
    printf("Example: %s 30 3864 2192 10 ccic_last_frame.raw\n", prog);
    printf("bitdepth supports: 8 / 10 / 12 / 14 / 16 / 20\n");
}

static MppPixelFormat ccic_demo_bitdepth_to_pixel_format(U32 u32BitDepth)
{
    switch (u32BitDepth) {
    case 8:
        return MPP_PIXEL_FORMAT_RGB_BAYER_8BITS;
    case 10:
        return MPP_PIXEL_FORMAT_RGB_BAYER_10BITS;
    case 12:
        return MPP_PIXEL_FORMAT_RGB_BAYER_12BITS;
    case 14:
        return MPP_PIXEL_FORMAT_RGB_BAYER_14BITS;
    case 16:
        return MPP_PIXEL_FORMAT_RGB_BAYER_16BITS;
    case 20:
        return MPP_PIXEL_FORMAT_RGB_BAYER_20BITS;
    default:
        return MPP_PIXEL_FORMAT_MAX;
    }
}

static const char *ccic_demo_pixel_format_name(MppPixelFormat ePixelFormat)
{
    switch (ePixelFormat) {
    case MPP_PIXEL_FORMAT_RGB_BAYER_8BITS:
        return "bayer8";
    case MPP_PIXEL_FORMAT_RGB_BAYER_10BITS:
        return "bayer10";
    case MPP_PIXEL_FORMAT_RGB_BAYER_12BITS:
        return "bayer12";
    case MPP_PIXEL_FORMAT_RGB_BAYER_14BITS:
        return "bayer14";
    case MPP_PIXEL_FORMAT_RGB_BAYER_16BITS:
        return "bayer16";
    case MPP_PIXEL_FORMAT_RGB_BAYER_20BITS:
        return "bayer20";
    default:
        return "unknown";
    }
}

static void ccic_demo_dump_frame_info(const VideoFrameInfo *pstFrame)
{
    const CommonFrameInfo *pstComm = NULL;

    if (pstFrame == NULL){
        return;
    }

    pstComm = &pstFrame->stViFrameInfo.stCommFrameInfo;
    printf("[ccic-demo] idx=%u pool=%lu buf=%lu pts=%llu size=%ux%u fmt=%d total=%u planes=%u fd0=%lu vir0=%p valid0=%u\n",
        pstFrame->u32Idx,
        pstFrame->ulPoolId,
        pstFrame->ulBufferId,
        (unsigned long long)pstFrame->stVFrame.u64PTS,
        pstComm->u32Width,
        pstComm->u32Height,
        pstComm->ePixelFormat,
        pstFrame->stVFrame.u32TotalSize,
        pstFrame->stVFrame.u32PlaneNum,
        pstFrame->stVFrame.u32Fd[0],
        (void *)(uintptr_t)pstFrame->stVFrame.ulPlaneVirAddr[0],
        pstFrame->stVFrame.u32PlaneSizeValid[0]);
}

static S32 ccic_demo_save_frame(const char *pszPath, const VideoFrameInfo *pstFrame)
{
    FILE *fp = NULL;
    U32 u32Plane = 0;

    if (pszPath == NULL || pstFrame == NULL){
        return -1;
    }

    fp = fopen(pszPath, "wb");
    if (fp == NULL) {
        printf("[ccic-demo] fopen failed: %s\n", pszPath);
        return -1;
    }

    for (u32Plane = 0; u32Plane < pstFrame->stVFrame.u32PlaneNum && u32Plane < FRAME_MAX_PLANE; ++u32Plane) {
        const void *pVirAddr = (const void *)(uintptr_t)pstFrame->stVFrame.ulPlaneVirAddr[u32Plane];
        U32 u32WriteSize = pstFrame->stVFrame.u32PlaneSizeValid[u32Plane];

        if (pVirAddr == NULL || u32WriteSize == 0){
            continue;
        }

        if (fwrite(pVirAddr, 1, u32WriteSize, fp) != u32WriteSize) {
            printf("[ccic-demo] fwrite failed: %s plane=%u size=%u\n", pszPath, u32Plane, u32WriteSize);
            (void)fclose(fp);
            return -1;
        }
    }

    (void)fclose(fp);
    printf("[ccic-demo] saved last frame to %s\n", pszPath);
    return 0;
}

static void ccic_demo_fill_default_config(CCIC_DEMO_CONFIG_S *pstCfg)
{
    if (pstCfg == NULL){
        return;
    }

    memset(pstCfg, 0, sizeof(*pstCfg));
    pstCfg->ViDev = CCIC_DEMO_DEV;
    pstCfg->ViChn = CCIC_DEMO_PHY_CHN;
    pstCfg->u32Width = CCIC_DEMO_DEFAULT_WIDTH;
    pstCfg->u32Height = CCIC_DEMO_DEFAULT_HEIGHT;
    pstCfg->u32MipiLaneNum = CCIC_DEMO_DEFAULT_MIPI_LANES;
    pstCfg->u32Mbps = CCIC_DEMO_DEFAULT_MBPS;
    pstCfg->u32FrameCount = CCIC_DEMO_DEFAULT_FRAME_CNT;
    pstCfg->s32TimeoutMs = CCIC_DEMO_DEFAULT_TIMEOUT_MS;
    pstCfg->ePixelFormat = CCIC_DEMO_DEFAULT_PIXEL_FMT;
    pstCfg->pszDumpPath = "ccic_last_frame.raw";
    pstCfg->bDumpLastFrame = MPP_TRUE;

    pstCfg->stDevAttr.eWorkMode = VI_WORK_MODE_ISP_BYPASS;
    pstCfg->stDevAttr.u32Width = pstCfg->u32Width;
    pstCfg->stDevAttr.u32Height = pstCfg->u32Height;
    pstCfg->stDevAttr.u32MipiLaneNum = pstCfg->u32MipiLaneNum;
    pstCfg->stDevAttr.u32mbps = pstCfg->u32Mbps;
    pstCfg->stDevAttr.bCapture2Preview = MPP_FALSE;

    pstCfg->stChnAttr.eChnType = VI_CHN_TYPE_PHYSICAL;
    pstCfg->stChnAttr.ePixelFormat = pstCfg->ePixelFormat;
    pstCfg->stChnAttr.u32Width = pstCfg->u32Width;
    pstCfg->stChnAttr.u32Height = pstCfg->u32Height;
}

static S32 ccic_demo_parse_args(int argc, char *argv[], CCIC_DEMO_CONFIG_S *pstCfg)
{
    U32 u32BitDepth = 10;
    MppPixelFormat ePixelFormat;

    if (pstCfg == NULL){
        return -1;
    }

    ccic_demo_fill_default_config(pstCfg);

    if (argc > 1){
        pstCfg->u32FrameCount = (U32)atoi(argv[1]);
    }
    if (argc > 2){
        pstCfg->u32Width = (U32)atoi(argv[2]);
    }
    if (argc > 3){
        pstCfg->u32Height = (U32)atoi(argv[3]);
    }
    if (argc > 4){
        u32BitDepth = (U32)atoi(argv[4]);
    }
    if (argc > 5) {
        pstCfg->pszDumpPath = argv[5];
        pstCfg->bDumpLastFrame = MPP_TRUE;
    }

    ePixelFormat = ccic_demo_bitdepth_to_pixel_format(u32BitDepth);
    if (ePixelFormat == MPP_PIXEL_FORMAT_MAX) {
        printf("[ccic-demo] unsupported bitdepth=%u\n", u32BitDepth);
        return -1;
    }

    pstCfg->ePixelFormat = ePixelFormat;
    pstCfg->stDevAttr.u32Width = pstCfg->u32Width;
    pstCfg->stDevAttr.u32Height = pstCfg->u32Height;
    pstCfg->stChnAttr.u32Width = pstCfg->u32Width;
    pstCfg->stChnAttr.u32Height = pstCfg->u32Height;
    pstCfg->stChnAttr.ePixelFormat = pstCfg->ePixelFormat;
    return 0;
}

static S32 ccic_demo_start_vi(const CCIC_DEMO_CONFIG_S *pstCfg)
{
    S32 s32Ret = 0;

    s32Ret = SYS_Init();
    if (s32Ret != 0) {
        printf("[ccic-demo] SYS_Init failed, ret=%d\n", s32Ret);
        return s32Ret;
    }

    s32Ret = VB_Init();
    if (s32Ret != 0) {
        printf("[ccic-demo] VB_Init failed, ret=%d\n", s32Ret);
        SYS_Exit();
        return s32Ret;
    }

    s32Ret = VI_Init();
    if (s32Ret != 0) {
        printf("[ccic-demo] VI_Init failed, ret=%d\n", s32Ret);
        VB_Exit();
        SYS_Exit();
        return s32Ret;
    }

    s32Ret = VI_SetDevAttr(pstCfg->ViDev, &pstCfg->stDevAttr);
    if (s32Ret != 0) {
        printf("[ccic-demo] VI_SetDevAttr failed, ret=%d\n", s32Ret);
        VI_DeInit();
        VB_Exit();
        SYS_Exit();
        return s32Ret;
    }

    s32Ret = VI_SetChnAttr(pstCfg->ViDev, pstCfg->ViChn, &pstCfg->stChnAttr);
    if (s32Ret != 0) {
        printf("[ccic-demo] VI_SetChnAttr failed, ret=%d\n", s32Ret);
        VI_DeInit();
        VB_Exit();
        SYS_Exit();
        return s32Ret;
    }

    s32Ret = VI_EnableDev(pstCfg->ViDev);
    if (s32Ret != 0) {
        printf("[ccic-demo] VI_EnableDev failed, ret=%d\n", s32Ret);
        VI_DeInit();
        VB_Exit();
        SYS_Exit();
        return s32Ret;
    }

    s32Ret = VI_EnableChn(pstCfg->ViDev, pstCfg->ViChn);
    if (s32Ret != 0) {
        printf("[ccic-demo] VI_EnableChn failed, ret=%d\n", s32Ret);
        (void)VI_DisableDev(pstCfg->ViDev);
        VI_DeInit();
        VB_Exit();
        SYS_Exit();
        return s32Ret;
    }

    printf("[ccic-demo] start ok: dev=%d chn=%d mode=%d size=%ux%u fmt=%s frames=%u timeout=%dms\n",
        pstCfg->ViDev,
        pstCfg->ViChn,
        pstCfg->stDevAttr.eWorkMode,
        pstCfg->u32Width,
        pstCfg->u32Height,
        ccic_demo_pixel_format_name(pstCfg->ePixelFormat),
        pstCfg->u32FrameCount,
        pstCfg->s32TimeoutMs);
    return 0;
}

static void ccic_demo_stop_vi(const CCIC_DEMO_CONFIG_S *pstCfg)
{
    if (pstCfg == NULL){
        return;
    }

    (void)VI_DisableChn(pstCfg->ViDev, pstCfg->ViChn);
    (void)VI_DisableDev(pstCfg->ViDev);
    (void)VI_DeInit();
    (void)VB_Exit();
    (void)SYS_Exit();
}

int main(int argc, char *argv[])
{
    CCIC_DEMO_CONFIG_S stCfg;
    VideoFrameInfo stFrame;
    U32 u32FrameIdx = 0;
    S32 s32Ret = 0;

    if (argc > 1 && strcmp(argv[1], "-h") == 0) {
        ccic_demo_usage(argv[0]);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        ccic_demo_usage(argv[0]);
        return 0;
    }

    s32Ret = ccic_demo_parse_args(argc, argv, &stCfg);
    if (s32Ret != 0) {
        ccic_demo_usage(argv[0]);
        return s32Ret;
    }

    s32Ret = ccic_demo_start_vi(&stCfg);
    if (s32Ret != 0){
        return s32Ret;
    }

    for (u32FrameIdx = 0; u32FrameIdx < stCfg.u32FrameCount; ++u32FrameIdx) {
        memset(&stFrame, 0, sizeof(stFrame));
        s32Ret = VI_GetChnFrame(stCfg.ViDev, stCfg.ViChn, &stFrame, stCfg.s32TimeoutMs);
        if (s32Ret != 0) {
            if (s32Ret == -4) {
                printf("[ccic-demo] frame %u/%u not ready yet\n", u32FrameIdx + 1, stCfg.u32FrameCount);
                usleep(100 * 1000);
                continue;
            }

            printf("[ccic-demo] VI_GetChnFrame failed, ret=%d at iter=%u\n", s32Ret, u32FrameIdx);
            break;
        }

        ccic_demo_dump_frame_info(&stFrame);

        if (stCfg.bDumpLastFrame == MPP_TRUE && (u32FrameIdx + 1U) == stCfg.u32FrameCount){
            (void)ccic_demo_save_frame(stCfg.pszDumpPath, &stFrame);
        }

        s32Ret = VI_ReleaseChnFrame(stCfg.ViDev, stCfg.ViChn, &stFrame);
        if (s32Ret != 0) {
            printf("[ccic-demo] VI_ReleaseChnFrame failed, ret=%d at iter=%u\n", s32Ret, u32FrameIdx);
            break;
        }
    }

    ccic_demo_stop_vi(&stCfg);
    return s32Ret;
}
