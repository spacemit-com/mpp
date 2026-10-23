/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *------------------------------------------------------------------------------
 */

#ifndef VI_BUF_MGR_H
#define VI_BUF_MGR_H

#include "vi_type.h"
#include "vb_api.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/*
 * Neutral pixel-math frame layout (no platform-specific tail such as K1
 * DWT).  Callers that need the platform-exact layout must ask the backend
 * via ViAlOps.calc_frame_layout.
 */
S32 MPI_VI_CalcFrameInfo(const ViChnAttrS *pstChnAttr, VideoFrameInfo *pstFrameInfo);

/*
 * Allocate the channel output VB pool from the caller-negotiated layout
 * (pstFrameTemplate, typically produced via the backend layout ops).
 * The pool buffer size is taken from pstFrameTemplate->stVFrame.u32TotalSize.
 */
S32 MPI_VI_CreateOutBufPool(
    VI_DEV ViDev,
    VI_CHN ViChn,
    const ViChnAttrS *pstChnAttr,
    const VideoFrameInfo *pstFrameTemplate,
    U32 u32BufCnt,
    UL *pulPoolId,
    VideoFrameInfo *pastFrameInfo,
    UL *paulBufferId);

VOID MPI_VI_DestroyOutBufPool(UL ulPoolId, U32 u32BufCnt, UL *paulBufferId);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* VI_BUF_MGR_H */
