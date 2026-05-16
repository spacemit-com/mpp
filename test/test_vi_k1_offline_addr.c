#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "vi_api.h"
#include "sys_api.h"
#include "vb_api.h"
#include <unistd.h>

#define OFFLINE_ADDR_DEMO_DEV              0
#define OFFLINE_ADDR_DEMO_PHY_CHN          0
#define OFFLINE_ADDR_DEMO_TIMEOUT_MS       1000
#define OFFLINE_ADDR_DEMO_DEFAULT_WIDTH    1920
#define OFFLINE_ADDR_DEMO_DEFAULT_HEIGHT   1080
#define OFFLINE_ADDR_DEMO_DEFAULT_OUT_FMT  MPP_PIXEL_FORMAT_NV12

static S32 offline_addr_demo_dump_plane(const char *pszPath,
    const void *pVirAddr,
    U32 u32Size)
{
    FILE *fp = NULL;

    if (pszPath == NULL || pVirAddr == NULL || u32Size == 0){
        return -1;
    }

    fp = fopen(pszPath, "wb");
    if (fp == NULL) {
        printf("failed to open dump file %s\n", pszPath);
        return -1;
    }

    if (fwrite(pVirAddr, 1, u32Size, fp) != u32Size) {
        printf("failed to write dump file %s, size=%u\n", pszPath, u32Size);
        (void)fclose(fp);
        return -1;
    }

    (void)fclose(fp);
    return 0;
}

static S32 offline_addr_demo_dump_nv12_yuv(const VideoFrameInfo *pstFrame)
{
    char szPath[256] = {0};
    FILE *fp = NULL;
    const U8 *pu8Y = NULL;
    const U8 *pu8UV = NULL;
    U32 u32Width = 0;
    U32 u32Height = 0;
    U32 u32YStride = 0;
    U32 u32UVStride = 0;
    U32 y = 0;

    if (pstFrame == NULL){
        return -1;
    }

    u32Width = pstFrame->stViFrameInfo.stCommFrameInfo.u32Width;
    u32Height = pstFrame->stViFrameInfo.stCommFrameInfo.u32Height;
    pu8Y = (const U8 *)(uintptr_t)pstFrame->stVFrame.ulPlaneVirAddr[0];
    pu8UV = (const U8 *)(uintptr_t)pstFrame->stVFrame.ulPlaneVirAddr[1];
    u32YStride = pstFrame->stVFrame.u32PlaneStride[0];
    u32UVStride = pstFrame->stVFrame.u32PlaneStride[1];

    if (pu8Y == NULL || pu8UV == NULL || u32Width == 0 || u32Height == 0){
        return -1;
    }

    if (u32YStride < u32Width || u32UVStride < u32Width) {
        printf("invalid NV12 stride, yStride=%u uvStride=%u width=%u\n",
            u32YStride,
            u32UVStride,
            u32Width);
        return -1;
    }

    (void)snprintf(szPath,
        sizeof(szPath),
        "offline_addr_frame_%ux%u_nv12.yuv",
        u32Width,
        u32Height);

    fp = fopen(szPath, "wb");
    if (fp == NULL) {
        printf("failed to open dump file %s\n", szPath);
        return -1;
    }

    for (y = 0; y < u32Height; ++y) {
        if (fwrite(pu8Y + (size_t)y * u32YStride, 1, u32Width, fp) != u32Width) {
            printf("failed to write Y plane to %s at row %u\n", szPath, y);
            (void)fclose(fp);
            return -1;
        }
    }

    for (y = 0; y < (u32Height / 2); ++y) {
        if (fwrite(pu8UV + (size_t)y * u32UVStride, 1, u32Width, fp) != u32Width) {
            printf("failed to write UV plane to %s at row %u\n", szPath, y);
            (void)fclose(fp);
            return -1;
        }
    }

    (void)fclose(fp);
    printf("dumped NV12 frame to %s, y=%p uv=%p\n", szPath, pu8Y, pu8UV);
    return 0;
}

static S32 offline_addr_demo_dump_frame(const VideoFrameInfo *pstFrame)
{
    char szPath[256] = {0};
    U32 i = 0;

    if (pstFrame == NULL){
        return -1;
    }

    if (pstFrame->stViFrameInfo.stCommFrameInfo.ePixelFormat == MPP_PIXEL_FORMAT_NV12){
        return offline_addr_demo_dump_nv12_yuv(pstFrame);
    }

    for (i = 0; i < pstFrame->stVFrame.u32PlaneNum; ++i) {
        const void *pVirAddr = (const void *)(uintptr_t)pstFrame->stVFrame.ulPlaneVirAddr[i];
        U32 u32Size = pstFrame->stVFrame.u32PlaneSizeValid[i];

        if (pVirAddr == NULL || u32Size == 0){
            continue;
        }

        (void)snprintf(szPath,
            sizeof(szPath),
            "offline_addr_frame_%ux%u_fmt%d_plane%u.bin",
            pstFrame->stViFrameInfo.stCommFrameInfo.u32Width,
            pstFrame->stViFrameInfo.stCommFrameInfo.u32Height,
            pstFrame->stViFrameInfo.stCommFrameInfo.ePixelFormat,
            i);

        if (offline_addr_demo_dump_plane(szPath, pVirAddr, u32Size) != 0){
            return -1;
        }

        printf("dumped plane%u to %s, vir=%p size=%u\n", i, szPath, pVirAddr, u32Size);
    }

    return 0;
}

static void offline_addr_demo_print_frame_meta(const ViFrameMetaInfo *pstMeta)
{
    if (pstMeta == NULL){
        return;
    }

    printf("frame meta: frameId=%u aeStable=%u awbStable=%u ct=%u expTime=[%u,%u,%u] again=[%u,%u,%u]\n",
        pstMeta->u32FrameId,
        pstMeta->u8AeStable,
        pstMeta->u8AwbStable,
        pstMeta->u32ColorTemp,
        pstMeta->u32ExpTime[0],
        pstMeta->u32ExpTime[1],
        pstMeta->u32ExpTime[2],
        pstMeta->u32Again[0],
        pstMeta->u32Again[1],
        pstMeta->u32Again[2]);
}

static void offline_addr_demo_usage(const char *prog)
{
    printf("Usage: %s <raw_file> [width] [height]\n", prog);
    printf("Example: %s test.raw 1920 1080\n", prog);
    printf("Flow: user first saves raw data to a userspace buffer address, then calls VI_OfflineSetInputAddr.\n");
    printf("Note: offline raw bit depth now uses VI internal default configuration.\n");
}

static S32 offline_addr_demo_load_file_to_buffer(const char *pszRawFile,
    U8 **ppu8RawBuf,
    U32 *pu32RawSize)
{
    FILE *fp = NULL;
    long s32FileSize = 0;
    U8 *pu8RawBuf = NULL;
    size_t uReadSize = 0;

    if (pszRawFile == NULL || ppu8RawBuf == NULL || pu32RawSize == NULL){
        return -1;
    }

    fp = fopen(pszRawFile, "rb");
    if (fp == NULL) {
        printf("failed to open raw file %s\n", pszRawFile);
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        printf("failed to seek raw file %s\n", pszRawFile);
        (void)fclose(fp);
        return -1;
    }

    s32FileSize = ftell(fp);
    if (s32FileSize <= 0) {
        printf("invalid raw file size %ld, file=%s\n", s32FileSize, pszRawFile);
        (void)fclose(fp);
        return -1;
    }

    if (fseek(fp, 0, SEEK_SET) != 0) {
        printf("failed to rewind raw file %s\n", pszRawFile);
        (void)fclose(fp);
        return -1;
    }

    pu8RawBuf = (U8 *)malloc((size_t)s32FileSize);
    if (pu8RawBuf == NULL) {
        printf("failed to allocate raw buffer, size=%ld\n", s32FileSize);
        (void)fclose(fp);
        return -1;
    }

    uReadSize = fread(pu8RawBuf, 1, (size_t)s32FileSize, fp);
    (void)fclose(fp);
    if (uReadSize != (size_t)s32FileSize) {
        printf("failed to read raw file %s, read=%zu size=%ld\n", pszRawFile, uReadSize, s32FileSize);
        free(pu8RawBuf);
        return -1;
    }

    *ppu8RawBuf = pu8RawBuf;
    *pu32RawSize = (U32)s32FileSize;
    printf("raw file loaded to user buffer addr=%p size=%u\n", pu8RawBuf, *pu32RawSize);
    return 0;
}

int main(int argc, char *argv[])
{
    const char *pszRawFile = NULL;
    U32 u32Width = OFFLINE_ADDR_DEMO_DEFAULT_WIDTH;
    U32 u32Height = OFFLINE_ADDR_DEMO_DEFAULT_HEIGHT;
    U8 *pu8RawBuf = NULL;
    U32 u32RawSize = 0;
    ViDevAttrS stDevAttr;
    ViChnAttrS stChnAttr;
    VideoFrameInfo stFrame;
    ViFrameMetaInfo stFrameMeta;
    S32 s32Ret = 0;

    if (argc < 2) {
        offline_addr_demo_usage(argv[0]);
        return -1;
    }

    pszRawFile = argv[1];
    if (argc > 2){
        u32Width = (U32)atoi(argv[2]);
    }
    if (argc > 3){
        u32Height = (U32)atoi(argv[3]);
    }

    memset(&stDevAttr, 0, sizeof(stDevAttr));
    memset(&stChnAttr, 0, sizeof(stChnAttr));
    memset(&stFrame, 0, sizeof(stFrame));
    memset(&stFrameMeta, 0, sizeof(stFrameMeta));

    s32Ret = offline_addr_demo_load_file_to_buffer(pszRawFile, &pu8RawBuf, &u32RawSize);
    if (s32Ret != 0){
        return s32Ret;
    }

    s32Ret = SYS_Init();
    if (s32Ret != 0) {
        printf("SYS_Init failed, ret=%d\n", s32Ret);
        goto free_buf;
    }

    s32Ret = VB_Init();
    if (s32Ret != 0) {
        printf("VB_Init failed, ret=%d\n", s32Ret);
        SYS_Exit();
        goto free_buf;
    }

    s32Ret = VI_Init();
    if (s32Ret != 0) {
        printf("VI_Init failed, ret=%d\n", s32Ret);
        VB_Exit();
        SYS_Exit();
        goto free_buf;
    }

    stDevAttr.eWorkMode = VI_WORK_MODE_OFFLINE;
    stDevAttr.u32Width = u32Width;
    stDevAttr.u32Height = u32Height;
    stDevAttr.bCapture2Preview = MPP_FALSE;

    s32Ret = VI_SetDevAttr(OFFLINE_ADDR_DEMO_DEV, &stDevAttr);
    if (s32Ret != 0) {
        printf("VI_SetDevAttr failed, ret=%d\n", s32Ret);
        goto exit_vi;
    }

    s32Ret = VI_EnableDev(OFFLINE_ADDR_DEMO_DEV);
    if (s32Ret != 0) {
        printf("VI_EnableDev failed, ret=%d\n", s32Ret);
        goto exit_vi;
    }

    stChnAttr.eChnType = VI_CHN_TYPE_PHYSICAL;
    stChnAttr.ePixelFormat = OFFLINE_ADDR_DEMO_DEFAULT_OUT_FMT;
    stChnAttr.u32Width = u32Width;
    stChnAttr.u32Height = u32Height;
    stChnAttr.eStrideAlign = VI_STRIDE_ALIGN_16;

    s32Ret = VI_SetChnAttr(OFFLINE_ADDR_DEMO_DEV, OFFLINE_ADDR_DEMO_PHY_CHN, &stChnAttr);
    if (s32Ret != 0) {
        printf("VI_SetChnAttr failed, ret=%d\n", s32Ret);
        goto disable_dev;
    }

    s32Ret = VI_EnableChn(OFFLINE_ADDR_DEMO_DEV, OFFLINE_ADDR_DEMO_PHY_CHN);
    if (s32Ret != 0) {
        printf("VI_EnableChn failed, ret=%d\n", s32Ret);
        goto disable_dev;
    }

    s32Ret = VI_OfflineSetInputAddr(OFFLINE_ADDR_DEMO_DEV,
        OFFLINE_ADDR_DEMO_PHY_CHN,
        pu8RawBuf,
        u32RawSize);
    if (s32Ret != 0) {
        printf("VI_OfflineSetInputAddr failed, ret=%d\n", s32Ret);
        goto disable_chn;
    }

    sleep(1); // 可换成实际帧率配置对应的延时 ，避免拿不到帧退出


    s32Ret = VI_GetChnFrame(OFFLINE_ADDR_DEMO_DEV,
        OFFLINE_ADDR_DEMO_PHY_CHN,
        &stFrame,
        OFFLINE_ADDR_DEMO_TIMEOUT_MS);
    if (s32Ret != 0) {
        printf("VI_GetChnFrame failed, ret=%d\n", s32Ret);
        goto disable_chn;
    }

    printf("offline addr frame received: %ux%u fmt=%d pts=%llu vir0=0x%llx srcBuf=%p srcSize=%u\n",
        stFrame.stViFrameInfo.stCommFrameInfo.u32Width,
        stFrame.stViFrameInfo.stCommFrameInfo.u32Height,
        stFrame.stViFrameInfo.stCommFrameInfo.ePixelFormat,
        (unsigned long long)stFrame.stVFrame.u64PTS,
        (unsigned long long)stFrame.stVFrame.ulPlaneVirAddr[0],
        pu8RawBuf,
        u32RawSize);

    // s32Ret = VI_QueryFrameMeta(OFFLINE_ADDR_DEMO_DEV,
    //                            OFFLINE_ADDR_DEMO_PHY_CHN,
    //                            stFrame.stVFrame.u32PrivateData,
    //                            &stFrameMeta);
    // if (s32Ret == 0)
    //     offline_addr_demo_print_frame_meta(&stFrameMeta);
    // else
    //     printf("VI_QueryFrameMeta failed, frameId=%u ret=%d\n",
    //            stFrame.stVFrame.u32PrivateData,
    //            s32Ret);

    if (offline_addr_demo_dump_frame(&stFrame) != 0){
        printf("offline addr frame dump failed\n");
    }

    (void)VI_ReleaseChnFrame(OFFLINE_ADDR_DEMO_DEV, OFFLINE_ADDR_DEMO_PHY_CHN, &stFrame);
disable_chn:
    (void)VI_DisableChn(OFFLINE_ADDR_DEMO_DEV, OFFLINE_ADDR_DEMO_PHY_CHN);
disable_dev:
    (void)VI_DisableDev(OFFLINE_ADDR_DEMO_DEV);
exit_vi:
    (void)VI_DeInit();
    (void)VB_Exit();
free_buf:
    free(pu8RawBuf);
    return s32Ret;
}
