/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_rtmp.h
 * @Date      :    2026-06-11
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    RTMP publisher backend for MUX (push H264/H265 over FLV).
 *
 * Reuses the demux-side RTMP handshake, AMF0 codec, socket and URL helpers;
 * adds the publish-side chunk writer, the connect/createStream/publish command
 * sequence, and FLV video tag framing (AVC/HEVC sequence header + NALU).
 *------------------------------------------------------------------------------
 */

#ifndef MUX_RTMP_H
#define MUX_RTMP_H

#include "mux_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

typedef struct _MuxRtmp MuxRtmp;

/**
 * @brief  Create and connect an RTMP publisher.
 * @param  pszUrl    rtmp://host[:port]/app/streamKey
 * @param  pstStream stream attributes (codec/resolution/fps)
 * @return handle or NULL on failure.
 */
MuxRtmp *MuxRtmp_Create(const CHAR *pszUrl, const MuxStreamAttr *pstStream);

/**
 * @brief  Push one Annex-B access unit. The first key frame triggers the
 *         AVC/HEVC sequence header (decoder config) before the NALU tag.
 */
S32 MuxRtmp_Write(MuxRtmp *pRtmp, const U8 *pu8Data, U32 u32Size, BOOL bKeyFrame, U64 u64PtsUs);

/** @brief  Tear down the RTMP session. */
VOID MuxRtmp_Destroy(MuxRtmp *pRtmp);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* MUX_RTMP_H */
