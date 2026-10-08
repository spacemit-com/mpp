/* Copyright 2026 SPACEMIT. All rights reserved.
 * BSD-style license. */
#ifndef MKV_DEMUXER_H
#define MKV_DEMUXER_H

#include "demux/demux_type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _MkvDemuxer MkvDemuxer;

MkvDemuxer *MkvDemuxer_Create(VOID);
VOID MkvDemuxer_Destroy(MkvDemuxer *pDemux);
S32 MkvDemuxer_Open(MkvDemuxer *pDemux, const CHAR *pszPath, U32 u32TimeoutMs);
VOID MkvDemuxer_Close(MkvDemuxer *pDemux);
S32 MkvDemuxer_GetStreamInfo(MkvDemuxer *pDemux, DemuxStreamInfo *pstInfo);
S32 MkvDemuxer_ReadPacket(MkvDemuxer *pDemux, DemuxPacket *pstPkt);
S32 MkvDemuxer_Seek(MkvDemuxer *pDemux, S64 s64PtsUs);
S64 MkvDemuxer_GetDuration(MkvDemuxer *pDemux);

#ifdef __cplusplus
}
#endif
#endif
