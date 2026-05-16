/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
* Use of this source code is governed by a BSD-style license
* that can be found in the LICENSE file.
*------------------------------------------------------------------------------
*/

#ifndef VI_BUF_MGR_H
#define VI_BUF_MGR_H

#include "vi_type.h"
#include "vb_api.h"
#include "image_buffer.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

S32 MPI_VI_CalcFrameInfo(const ViChnAttrS *pstChnAttr, VideoFrameInfo *pstFrameInfo);
S32 MPI_VI_CalcRawDumpFrameInfo(const ViChnAttrS *pstChnAttr, VideoFrameInfo *pstFrameInfo);
S32 MPI_VI_FillImageBufferFromFrameInfo(const VideoFrameInfo *pstFrameInfo, ImageBuffer *pstImageBuffer);
S32 MPI_VI_CreateOutBufPool(VI_DEV ViDev, VI_CHN ViChn, const ViChnAttrS *pstChnAttr,
    U32 u32BufCnt, UL *pulPoolId, VideoFrameInfo *pstFrameTemplate,
    VideoFrameInfo *pastFrameInfo, ImageBuffer *pastImageBuffer,
    UL *paulBufferId);
VOID MPI_VI_DestroyOutBufPool(UL ulPoolId, U32 u32BufCnt, UL *paulBufferId,
    VideoFrameInfo *pastFrameInfo, ImageBuffer *pastImageBuffer);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* VI_BUF_MGR_H */
