/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_k1_common.h
 * @Date      :    2026-3-30
 * @Author    :    SPACEMIT
 * @Brief     :    K1 VI common helpers.
 *------------------------------------------------------------------------------
 */

#ifndef VI_K1_COMMON_H
#define VI_K1_COMMON_H

#include "vi_k1_ctx.h"

S32 K1_VI_GetAsrChnId(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr, U32 *pu32AsrChn);
K1_VI_CHN_CTX_S *K1_VI_FindChnCtxByAsrChn(U32 u32AsrChn);
K1_VI_RAW_CTX_S *K1_VI_FindRawCtxByAsrChn(U32 u32AsrChn);
K1_VI_RAW_CTX_S *K1_VI_GetRawCtx(VI_DEV ViDev, VI_CHN ViChn);
K1_VI_RAW_CTX_S *K1_VI_GetOrCreateRawDumpCtx(VI_DEV ViDev, VI_CHN ViChn);

S32 K1_VI_ToAsrWorkMode(ViWorkMode eWorkMode, CAM_VI_WORK_MODE_E *peAsrWorkMode);
S32 K1_VI_FromAsrWorkMode(CAM_VI_WORK_MODE_E eAsrWorkMode, ViWorkMode *peWorkMode);
S32 K1_VI_ToAsrRawType(ViRawType eRawType, CAM_SENSOR_RAWTYPE_E *peAsrRawType);
S32 K1_VI_FromAsrRawType(CAM_SENSOR_RAWTYPE_E eAsrRawType, ViRawType *peRawType);
S32 K1_VI_GetSensorRawType(const K1_VI_DEV_CTX_S *pstDevCtx, ViRawType *peRawType);
S32 K1_VI_ToAsrPixelFormat(MppPixelFormat ePixelFormat, CAM_VI_PIXEL_FORMAT_E *peAsrPixelFormat);
S32 K1_VI_FromAsrPixelFormat(CAM_VI_PIXEL_FORMAT_E eAsrPixelFormat, MppPixelFormat *pePixelFormat);
S32 K1_VI_ToAsrDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr, K1_ASR_VI_DEV_ATTR_S *pstAsrDevAttr);
S32 K1_VI_FromAsrDevAttr(const K1_ASR_VI_DEV_ATTR_S *pstAsrDevAttr, ViDevAttrS *pstDevAttr);
S32 K1_VI_ToAsrChnAttr(const ViChnAttrS *pstChnAttr, K1_ASR_VI_CHN_ATTR_S *pstAsrChnAttr);
S32 K1_VI_FromAsrChnAttr(const K1_ASR_VI_CHN_ATTR_S *pstAsrChnAttr, ViChnAttrS *pstChnAttr);
S32 K1_VI_ToAsrCcicDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr, K1_ASR_CCIC_DEV_ATTR_S *pstCcicDevAttr);
S32 K1_VI_ToAsrCcicChnAttr(const ViChnAttrS *pstChnAttr, K1_ASR_CCIC_CHN_ATTR_S *pstCcicChnAttr);
S32 K1_VI_GetCcicChnId(VI_DEV ViDev, VI_CHN ViChn, U32 *pu32CcicChn);

#endif /* VI_K1_COMMON_H */
