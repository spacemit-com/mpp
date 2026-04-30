/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *------------------------------------------------------------------------------
 */

#include "include/vi_k1_defs.h"
#include "include/vi_k1_ctx.h"
#include "include/vi_k1_common.h"

K1_VI_CTX_S g_stK1ViCtx;

S32 K1_VI_GetAsrChnId(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr, U32 *pu32AsrChn)
{
    if (pu32AsrChn == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    if (pstChnAttr != NULL && pstChnAttr->ePixelFormat >= MPP_PIXEL_FORMAT_RGB_BAYER_8BITS &&
        pstChnAttr->ePixelFormat <= MPP_PIXEL_FORMAT_RGB_BAYER_16BITS) {
        VIU_GET_RAW_CHN(ViDev, *pu32AsrChn);
    } else if (ViChn >= 0) {
        *pu32AsrChn = (U32)ViChn;
    } else {
        return K1_VI_ERR_INVALID_PARAM;
    }

    return K1_VI_SUCCESS;
}

K1_VI_CHN_CTX_S *K1_VI_FindChnCtxByAsrChn(U32 u32AsrChn)
{
    U32 i = 0;
    U32 j = 0;

    for (i = 0; i < VI_MAX_DEV_NUM; i++) {
        for (j = 0; j < VI_MAX_CHN_NUM; j++) {
            K1_VI_CHN_CTX_S *pstChnCtx = &g_stK1ViCtx.astChnCtx[i][j];
            if (pstChnCtx->bCreated == MPP_TRUE && pstChnCtx->bIsVirtual != MPP_TRUE &&
                pstChnCtx->u32AsrChn == u32AsrChn)
                return pstChnCtx;
        }
    }

    return NULL;
}

K1_VI_RAW_CTX_S *K1_VI_FindRawCtxByAsrChn(U32 u32AsrChn)
{
    U32 i = 0;
    U32 j = 0;

    for (i = 0; i < VI_MAX_DEV_NUM; i++) {
        for (j = 0; j < VI_MAX_CHN_NUM; j++) {
            K1_VI_RAW_CTX_S *pstRawCtx = &g_stK1ViCtx.astRawCtx[i][j];
            if (pstRawCtx->bCreated == MPP_TRUE && pstRawCtx->u32AsrChn == u32AsrChn)
                return pstRawCtx;
        }
    }

    return NULL;
}

BOOL K1_VI_IsValidDev(VI_DEV ViDev)
{
    return (ViDev >= 0 && ViDev < VI_MAX_DEV_NUM) ? MPP_TRUE : MPP_FALSE;
}

BOOL K1_VI_IsValidChn(VI_CHN ViChn)
{
    return (ViChn >= 0 && ViChn < VI_MAX_CHN_NUM) ? MPP_TRUE : MPP_FALSE;
}

BOOL K1_VI_IsValidSize(U32 u32Width, U32 u32Height)
{
    if (u32Width < VI_MIN_WIDTH || u32Width > VI_MAX_WIDTH)
        return MPP_FALSE;
    if (u32Height < VI_MIN_HEIGHT || u32Height > VI_MAX_HEIGHT)
        return MPP_FALSE;
    return MPP_TRUE;
}

S32 K1_VI_ToAsrWorkMode(ViWorkMode eWorkMode, CAM_VI_WORK_MODE_E *peAsrWorkMode)
{
    if (peAsrWorkMode == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    switch (eWorkMode) {
    case VI_WORK_MODE_ONLINE:
    case VI_WORK_MODE_ISP_BYPASS:
        *peAsrWorkMode = CAM_VI_WORK_MODE_ONLINE;
        break;
    case VI_WORK_MODE_OFFLINE:
        *peAsrWorkMode = CAM_VI_WORK_MODE_OFFLINE;
        break;
    default:
        return K1_VI_ERR_NOT_SUPPORT;
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_GetCcicChnId(VI_DEV ViDev, VI_CHN ViChn, U32 *pu32CcicChn)
{
    if (pu32CcicChn == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    if (ViChn != 0)
        return K1_VI_ERR_NOT_SUPPORT;

    CCU_GET_CCIC_MAIN_CHN(ViDev, *pu32CcicChn);
    return K1_VI_SUCCESS;
}

S32 K1_VI_ToAsrCcicDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr, K1_ASR_CCIC_DEV_ATTR_S *pstCcicDevAttr)
{
    if (pstDevAttr == NULL || pstCcicDevAttr == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    memset(pstCcicDevAttr, 0, sizeof(*pstCcicDevAttr));
    pstCcicDevAttr->mipi_lane_num = pstDevAttr->u32MipiLaneNum;
    pstCcicDevAttr->mode = 0;
    pstCcicDevAttr->main_vc = 0;
    pstCcicDevAttr->sub_vc = 0;
    pstCcicDevAttr->main_dt = 0x2b;
    pstCcicDevAttr->sub_dt = 0x2b;
    (void)ViDev;
    return K1_VI_SUCCESS;
}

S32 K1_VI_ToAsrCcicChnAttr(const ViChnAttrS *pstChnAttr, K1_ASR_CCIC_CHN_ATTR_S *pstCcicChnAttr)
{
    if (pstChnAttr == NULL || pstCcicChnAttr == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    memset(pstCcicChnAttr, 0, sizeof(*pstCcicChnAttr));
    switch (pstChnAttr->ePixelFormat) {
    case MPP_PIXEL_FORMAT_RGB_BAYER_8BITS:
        pstCcicChnAttr->enPixFormat = CAM_CCIC_PIXEL_FORMAT_RGB_BAYER_8BPP;
        break;
    case MPP_PIXEL_FORMAT_RGB_BAYER_10BITS:
        pstCcicChnAttr->enPixFormat = CAM_CCIC_PIXEL_FORMAT_RGB_BAYER_10BPP;
        break;
    case MPP_PIXEL_FORMAT_RGB_BAYER_12BITS:
        pstCcicChnAttr->enPixFormat = CAM_CCIC_PIXEL_FORMAT_RGB_BAYER_12BPP;
        break;
    case MPP_PIXEL_FORMAT_RGB_BAYER_14BITS:
        return K1_VI_ERR_NOT_SUPPORT;
        break;
    default:
        return K1_VI_ERR_NOT_SUPPORT;
    }

    pstCcicChnAttr->width = pstChnAttr->u32Width;
    pstCcicChnAttr->height = pstChnAttr->u32Height;
    return K1_VI_SUCCESS;
}

S32 K1_VI_FromAsrWorkMode(CAM_VI_WORK_MODE_E eAsrWorkMode, ViWorkMode *peWorkMode)
{
    if (peWorkMode == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    switch (eAsrWorkMode) {
    case CAM_VI_WORK_MODE_ONLINE:
        *peWorkMode = VI_WORK_MODE_ONLINE;
        break;
    case CAM_VI_WORK_MODE_OFFLINE:
        *peWorkMode = VI_WORK_MODE_OFFLINE;
        break;
    default:
        return K1_VI_ERR_NOT_SUPPORT;
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_ToAsrRawType(ViRawType eRawType, CAM_SENSOR_RAWTYPE_E *peAsrRawType)
{
    if (peAsrRawType == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    switch (eRawType) {
    case VI_RAW_TYPE_8BIT:
        *peAsrRawType = CAM_SENSOR_RAWTYPE_RAW8;
        break;
    case VI_RAW_TYPE_10BIT:
        *peAsrRawType = CAM_SENSOR_RAWTYPE_RAW10;
        break;
    case VI_RAW_TYPE_12BIT:
        *peAsrRawType = CAM_SENSOR_RAWTYPE_RAW12;
        break;
    case VI_RAW_TYPE_14BIT:
        *peAsrRawType = CAM_SENSOR_RAWTYPE_RAW14;
        break;
    default:
        return K1_VI_ERR_NOT_SUPPORT;
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_FromAsrRawType(CAM_SENSOR_RAWTYPE_E eAsrRawType, ViRawType *peRawType)
{
    if (peRawType == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    switch (eAsrRawType) {
    case CAM_SENSOR_RAWTYPE_RAW8:
        *peRawType = VI_RAW_TYPE_8BIT;
        break;
    case CAM_SENSOR_RAWTYPE_RAW10:
        *peRawType = VI_RAW_TYPE_10BIT;
        break;
    case CAM_SENSOR_RAWTYPE_RAW12:
        *peRawType = VI_RAW_TYPE_12BIT;
        break;
    case CAM_SENSOR_RAWTYPE_RAW14:
        *peRawType = VI_RAW_TYPE_14BIT;
        break;
    default:
        return K1_VI_ERR_NOT_SUPPORT;
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_ToAsrPixelFormat(MppPixelFormat ePixelFormat, CAM_VI_PIXEL_FORMAT_E *peAsrPixelFormat)
{
    if (peAsrPixelFormat == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    switch (ePixelFormat) {
    case MPP_PIXEL_FORMAT_NV12:
        *peAsrPixelFormat = CAM_VI_PIXEL_FORMAT_YUV_SEMIPLANAR_420;
        break;
    case MPP_PIXEL_FORMAT_NV21:
        *peAsrPixelFormat = CAM_VI_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        break;
    case MPP_PIXEL_FORMAT_RGB_565:
        *peAsrPixelFormat = CAM_VI_PIXEL_FORMAT_RGB_565;
        break;
    case MPP_PIXEL_FORMAT_RGB_888:
        *peAsrPixelFormat = CAM_VI_PIXEL_FORMAT_RGB_888;
        break;
    case MPP_PIXEL_FORMAT_RGB_BAYER_8BITS:
        *peAsrPixelFormat = CAM_VI_PIXEL_FORMAT_RGB_BAYER_8BPP;
        break;
    case MPP_PIXEL_FORMAT_RGB_BAYER_10BITS:
        *peAsrPixelFormat = CAM_VI_PIXEL_FORMAT_RGB_BAYER_10BPP;
        break;
    case MPP_PIXEL_FORMAT_RGB_BAYER_12BITS:
        *peAsrPixelFormat = CAM_VI_PIXEL_FORMAT_RGB_BAYER_12BPP;
        break;
    case MPP_PIXEL_FORMAT_RGB_BAYER_14BITS:
        *peAsrPixelFormat = CAM_VI_PIXEL_FORMAT_RGB_BAYER_14BPP;
        break;
    case MPP_PIXEL_FORMAT_YUYV:
    case MPP_PIXEL_FORMAT_YVYU:
    case MPP_PIXEL_FORMAT_UYVY:
    case MPP_PIXEL_FORMAT_VYUY:
        *peAsrPixelFormat = CAM_VI_PIXEL_FORMAT_Y210;
        break;
    case MPP_PIXEL_FORMAT_YUV422SP_P010:
        *peAsrPixelFormat = CAM_VI_PIXEL_FORMAT_P210;
        break;
    case MPP_PIXEL_FORMAT_NV12_P010:
        *peAsrPixelFormat = CAM_VI_PIXEL_FORMAT_P010;
        break;
    default:
        return K1_VI_ERR_NOT_SUPPORT;
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_FromAsrPixelFormat(CAM_VI_PIXEL_FORMAT_E eAsrPixelFormat, MppPixelFormat *pePixelFormat)
{
    if (pePixelFormat == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    switch (eAsrPixelFormat) {
    case CAM_VI_PIXEL_FORMAT_YUV_SEMIPLANAR_420:
        *pePixelFormat = MPP_PIXEL_FORMAT_NV12;
        break;
    case CAM_VI_PIXEL_FORMAT_YVU_SEMIPLANAR_420:
        *pePixelFormat = MPP_PIXEL_FORMAT_NV21;
        break;
    case CAM_VI_PIXEL_FORMAT_RGB_565:
        *pePixelFormat = MPP_PIXEL_FORMAT_RGB_565;
        break;
    case CAM_VI_PIXEL_FORMAT_RGB_888:
        *pePixelFormat = MPP_PIXEL_FORMAT_RGB_888;
        break;
    case CAM_VI_PIXEL_FORMAT_RGB_BAYER_8BPP:
        *pePixelFormat = MPP_PIXEL_FORMAT_RGB_BAYER_8BITS;
        break;
    case CAM_VI_PIXEL_FORMAT_RGB_BAYER_10BPP:
        *pePixelFormat = MPP_PIXEL_FORMAT_RGB_BAYER_10BITS;
        break;
    case CAM_VI_PIXEL_FORMAT_RGB_BAYER_12BPP:
        *pePixelFormat = MPP_PIXEL_FORMAT_RGB_BAYER_12BITS;
        break;
    case CAM_VI_PIXEL_FORMAT_RGB_BAYER_14BPP:
        *pePixelFormat = MPP_PIXEL_FORMAT_RGB_BAYER_14BITS;
        break;
    case CAM_VI_PIXEL_FORMAT_Y210:
        *pePixelFormat = MPP_PIXEL_FORMAT_YUYV;
        break;
    case CAM_VI_PIXEL_FORMAT_P210:
        *pePixelFormat = MPP_PIXEL_FORMAT_YUV422SP_P010;
        break;
    case CAM_VI_PIXEL_FORMAT_P010:
        *pePixelFormat = MPP_PIXEL_FORMAT_NV12_P010;
        break;
    default:
        return K1_VI_ERR_NOT_SUPPORT;
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_GetSensorRawType(const K1_VI_DEV_CTX_S *pstDevCtx, ViRawType *peRawType)
{
    if (pstDevCtx == NULL || peRawType == NULL || pstDevCtx->pstSensorCfg == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    switch (pstDevCtx->pstSensorCfg->bitDepth) {
    case 8:
        *peRawType = VI_RAW_TYPE_8BIT;
        break;
    case 10:
        *peRawType = VI_RAW_TYPE_10BIT;
        break;
    case 12:
        *peRawType = VI_RAW_TYPE_12BIT;
        break;
    case 14:
        *peRawType = VI_RAW_TYPE_14BIT;
        break;
    case 16:
        *peRawType = VI_RAW_TYPE_16BIT;
        break;
    default:
        return K1_VI_ERR_NOT_SUPPORT;
    }

    return K1_VI_SUCCESS;
}

S32 K1_VI_ToAsrDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr, K1_ASR_VI_DEV_ATTR_S *pstAsrDevAttr)
{
    S32 s32Ret = 0;
    K1_VI_DEV_CTX_S *pstDevCtx = NULL;
    ViRawType eRawType;

    if (pstDevAttr == NULL || pstAsrDevAttr == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    memset(pstAsrDevAttr, 0, sizeof(*pstAsrDevAttr));

    s32Ret = K1_VI_ToAsrWorkMode(pstDevAttr->eWorkMode, &pstAsrDevAttr->enWorkMode);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    pstDevCtx = &g_stK1ViCtx.astDevCtx[ViDev];
    if (pstDevAttr->eWorkMode == VI_WORK_MODE_OFFLINE) {
        if (pstDevCtx->eOfflineRawType != VI_RAW_TYPE_UNKNOWN)
            eRawType = pstDevCtx->eOfflineRawType;
        else
            eRawType = VI_RAW_TYPE_12BIT;
    } else {
        s32Ret = K1_VI_GetSensorRawType(pstDevCtx, &eRawType);
        if (s32Ret != K1_VI_SUCCESS)
            return s32Ret;
    }

    s32Ret = K1_VI_ToAsrRawType(eRawType, &pstAsrDevAttr->enRawType);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    pstAsrDevAttr->width = pstDevAttr->u32Width;
    pstAsrDevAttr->height = pstDevAttr->u32Height;
    pstAsrDevAttr->bindSensorIdx = (U32)ViDev;
    pstAsrDevAttr->mipi_lane_num = pstDevAttr->u32MipiLaneNum;
    pstAsrDevAttr->bCapture2Preview = pstDevAttr->bCapture2Preview ? true : false;
    pstAsrDevAttr->bOfflineSlice = (pstDevAttr->eWorkMode == VI_WORK_MODE_OFFLINE && pstDevAttr->bCapture2Preview) ? true : false;
    return K1_VI_SUCCESS;
}

S32 K1_VI_FromAsrDevAttr(const K1_ASR_VI_DEV_ATTR_S *pstAsrDevAttr, ViDevAttrS *pstDevAttr)
{
    S32 s32Ret = 0;

    if (pstAsrDevAttr == NULL || pstDevAttr == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    memset(pstDevAttr, 0, sizeof(*pstDevAttr));

    s32Ret = K1_VI_FromAsrWorkMode(pstAsrDevAttr->enWorkMode, &pstDevAttr->eWorkMode);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    pstDevAttr->u32Width = pstAsrDevAttr->width;
    pstDevAttr->u32Height = pstAsrDevAttr->height;
    pstDevAttr->u32mbps = 0;
    pstDevAttr->u32MipiLaneNum = pstAsrDevAttr->mipi_lane_num;
    pstDevAttr->bCapture2Preview = pstAsrDevAttr->bCapture2Preview ? MPP_TRUE : MPP_FALSE;
    return K1_VI_SUCCESS;
}

S32 K1_VI_ToAsrChnAttr(const ViChnAttrS *pstChnAttr, K1_ASR_VI_CHN_ATTR_S *pstAsrChnAttr)
{
    S32 s32Ret = 0;

    if (pstChnAttr == NULL || pstAsrChnAttr == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    memset(pstAsrChnAttr, 0, sizeof(*pstAsrChnAttr));
    s32Ret = K1_VI_ToAsrPixelFormat(pstChnAttr->ePixelFormat, &pstAsrChnAttr->enPixFormat);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    pstAsrChnAttr->width = pstChnAttr->u32Width;
    pstAsrChnAttr->height = pstChnAttr->u32Height;
    return K1_VI_SUCCESS;
}

S32 K1_VI_FromAsrChnAttr(const K1_ASR_VI_CHN_ATTR_S *pstAsrChnAttr, ViChnAttrS *pstChnAttr)
{
    S32 s32Ret = 0;

    if (pstAsrChnAttr == NULL || pstChnAttr == NULL)
        return K1_VI_ERR_INVALID_PARAM;

    memset(pstChnAttr, 0, sizeof(*pstChnAttr));
    s32Ret = K1_VI_FromAsrPixelFormat(pstAsrChnAttr->enPixFormat, &pstChnAttr->ePixelFormat);
    if (s32Ret != K1_VI_SUCCESS)
        return s32Ret;

    pstChnAttr->u32Width = pstAsrChnAttr->width;
    pstChnAttr->u32Height = pstAsrChnAttr->height;
    pstChnAttr->bMirror = MPP_FALSE;
    pstChnAttr->bFlip = MPP_FALSE;
    pstChnAttr->eRotateMode = VI_ROT_0;
    pstChnAttr->bCropEnable = MPP_FALSE;
    pstChnAttr->u32CropX = 0;
    pstChnAttr->u32CropY = 0;
    pstChnAttr->u32CropWidth = pstChnAttr->u32Width;
    pstChnAttr->u32CropHeight = pstChnAttr->u32Height;
    pstChnAttr->eStrideAlign = VI_STRIDE_ALIGN_DEFAULT;
    return K1_VI_SUCCESS;
}
