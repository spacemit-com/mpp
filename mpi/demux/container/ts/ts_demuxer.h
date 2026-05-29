/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    ts_demuxer.h
 * @Brief     :    MPEG-TS container demuxer.
 *------------------------------------------------------------------------------
 */

#ifndef TS_DEMUXER_H
#define TS_DEMUXER_H

#include "demux/demux_type.h"
#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _TsDemuxer TsDemuxer;

TsDemuxer *TsDemuxer_Create(VOID);
VOID TsDemuxer_Destroy(TsDemuxer *pDemux);
S32 TsDemuxer_Open(TsDemuxer *pDemux, const CHAR *pszPath);
VOID TsDemuxer_Close(TsDemuxer *pDemux);
S32 TsDemuxer_GetStreamInfo(TsDemuxer *pDemux, DemuxStreamInfo *pstInfo);
S32 TsDemuxer_ReadPacket(TsDemuxer *pDemux, DemuxPacket *pstPkt);
S32 TsDemuxer_Seek(TsDemuxer *pDemux, S64 s64PtsUs);

/* Feed data mode (for HLS) */
S32 TsDemuxer_FeedData(TsDemuxer *pDemux, const U8 *pu8Data, U32 u32Len);

#ifdef __cplusplus
}
#endif

#endif /* __TS_DEMUXER_H__ */
