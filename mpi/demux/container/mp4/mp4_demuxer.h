/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    mp4_demuxer.h
 * @Brief     :    MP4/MOV container demuxer.
 *------------------------------------------------------------------------------
 */

#ifndef MP4_DEMUXER_H
#define MP4_DEMUXER_H

#include "demux/demux_type.h"
#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _Mp4Demuxer Mp4Demuxer;

/**
 * @brief  Create MP4 demuxer
 */
Mp4Demuxer *Mp4Demuxer_Create(VOID);

/**
 * @brief  Destroy MP4 demuxer
 */
VOID Mp4Demuxer_Destroy(Mp4Demuxer *pDemux);

/**
 * @brief  Open MP4 file
 */
S32 Mp4Demuxer_Open(Mp4Demuxer *pDemux, const CHAR *pszPath);

/**
 * @brief  Close MP4 file
 */
VOID Mp4Demuxer_Close(Mp4Demuxer *pDemux);

/**
 * @brief  Get stream info
 */
S32 Mp4Demuxer_GetStreamInfo(Mp4Demuxer *pDemux, DemuxStreamInfo *pstInfo);

/**
 * @brief  Read one sample (packet)
 */
S32 Mp4Demuxer_ReadPacket(Mp4Demuxer *pDemux, DemuxPacket *pstPkt);

/**
 * @brief  Seek to timestamp (microseconds)
 */
S32 Mp4Demuxer_Seek(Mp4Demuxer *pDemux, S64 s64PtsUs);

/**
 * @brief  Get duration (microseconds)
 */
S64 Mp4Demuxer_GetDuration(Mp4Demuxer *pDemux);

#ifdef __cplusplus
}
#endif

#endif /* __MP4_DEMUXER_H__ */
