/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    flv_demuxer.h
 * @Brief     :    FLV container demuxer.
 *------------------------------------------------------------------------------
 */

#ifndef FLV_DEMUXER_H
#define FLV_DEMUXER_H

#include "demux/demux_type.h"
#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _FlvDemuxer FlvDemuxer;

FlvDemuxer *FlvDemuxer_Create(VOID);
VOID FlvDemuxer_Destroy(FlvDemuxer *pDemux);
S32 FlvDemuxer_Open(FlvDemuxer *pDemux, const CHAR *pszPath);
VOID FlvDemuxer_Close(FlvDemuxer *pDemux);
S32 FlvDemuxer_GetStreamInfo(FlvDemuxer *pDemux, DemuxStreamInfo *pstInfo);
S32 FlvDemuxer_ReadPacket(FlvDemuxer *pDemux, DemuxPacket *pstPkt);

#ifdef __cplusplus
}
#endif

#endif /* __FLV_DEMUXER_H__ */
