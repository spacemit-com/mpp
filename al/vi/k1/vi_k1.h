/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    vi_k1.h
 * @Date      :    2026-3-26
 * @Author    :    SPACEMIT
 * @Brief     :    K1 platform VI adaptation layer internal declarations.
 *------------------------------------------------------------------------------
 */

#ifndef __AL_VI_K1_H__
#define __AL_VI_K1_H__

#include "vi_type.h"
#include "sys_type.h"
#include "vb_type.h"
#include "cam_module_interface.h"
#include "spm_cam_isp.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

S32 K1_VI_Init(VOID);
S32 K1_VI_DeInit(VOID);

S32 K1_VI_SetDevAttr(VI_DEV ViDev, const ViDevAttrS *pstDevAttr);
S32 K1_VI_GetDevAttr(VI_DEV ViDev, ViDevAttrS *pstDevAttr);
S32 K1_VI_EnableDev(VI_DEV ViDev);
S32 K1_VI_DisableDev(VI_DEV ViDev);

S32 K1_VI_SetChnAttr(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr);
S32 K1_VI_GetChnAttr(VI_DEV ViDev, VI_CHN ViChn, ViChnAttrS *pstChnAttr);
S32 K1_VI_SetChnFrameRate(VI_DEV ViDev, VI_CHN ViChn, const ViFrameRateCtrlS *pstFrameRateCtrl);
S32 K1_VI_GetChnFrameRate(VI_DEV ViDev, VI_CHN ViChn, ViFrameRateCtrlS *pstFrameRateCtrl);
S32 K1_VI_EnableChn(VI_DEV ViDev, VI_CHN ViChn);
S32 K1_VI_DisableChn(VI_DEV ViDev, VI_CHN ViChn);
S32 K1_VI_SetExternalBufPool(VI_DEV ViDev, VI_CHN ViChn,
							 UL ulPoolId, U32 u32BufCnt,
							 const UL *paulBufferId,
							 const VideoFrameInfo *pastFrameInfo,
							 const IMAGE_BUFFER_S *pastImageBuffer);
S32 K1_VI_DequeueDoneBuffer(VI_DEV ViDev, VI_CHN ViChn, U32 *pu32Index, S32 s32MilliSec);
S32 K1_VI_QueueBufferByIndex(VI_DEV ViDev, VI_CHN ViChn, U32 u32Index);
S32 K1_VI_TriggerRawDump(VI_DEV ViDev, VI_CHN ViChn);
S32 K1_VI_GetRawDumpFrame(VI_DEV ViDev, VI_CHN ViChn, VideoFrameInfo *pstVideoFrame, S32 s32MilliSec);
S32 K1_VI_ReleaseRawDumpFrame(VI_DEV ViDev, VI_CHN ViChn, const VideoFrameInfo *pstVideoFrame);
S32 K1_VI_OfflineSetInputAddr(VI_DEV ViDev,
							  VI_CHN ViChn,
							  UL ulPoolId,
							  UL ulBufferId,
							  const VideoFrameInfo *pstFrameInfo,
							  const IMAGE_BUFFER_S *pstImageBuffer,
							  const U8 *pu8RawVirAddr,
							  U32 u32RawSize);
S32 K1_VI_AttachBindSink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode);
S32 K1_VI_DetachBindSink(VI_DEV ViDev, VI_CHN ViChn, const MppNode *pstSinkNode);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* __AL_VI_K1_H__ */
