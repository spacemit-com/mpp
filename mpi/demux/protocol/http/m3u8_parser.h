/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    m3u8_parser.h
 * @Brief     :    M3U8 playlist parser for HLS streaming.
 *------------------------------------------------------------------------------
 */

#ifndef M3U8_PARSER_H
#define M3U8_PARSER_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define M3U8_MAX_SEGMENTS 256
#define M3U8_MAX_URL_LEN 512

typedef struct _M3u8Segment {
    char szUrl[M3U8_MAX_URL_LEN];
    float fDuration;
    S64 s64ByteRangeStart;
    S64 s64ByteRangeLen;
    BOOL bDiscontinuity;
} M3u8Segment;

typedef struct _M3u8Playlist {
    BOOL bMasterPlaylist;
    BOOL bEndList;
    BOOL bLive;
    U32 u32Version;
    float fTargetDuration;
    U64 u64MediaSequence;

    U32 u32SegmentCount;
    M3u8Segment astSegments[M3U8_MAX_SEGMENTS];

    /* Master playlist streams */
    U32 u32StreamCount;
    struct {
        char szUrl[M3U8_MAX_URL_LEN];
        U32 u32Bandwidth;
        U32 u32Width;
        U32 u32Height;
        char szCodecs[64];
    } astStreams[8];
} M3u8Playlist;

/**
 * @brief  Parse M3U8 content
 * @return 0 on success
 */
S32 M3u8_Parse(const char *pContent, U32 u32Len, M3u8Playlist *pPlaylist);

/**
 * @brief  Resolve relative URL to absolute
 */
S32 M3u8_ResolveUrl(const char *pBaseUrl, const char *pRelUrl, char *pAbsUrl, U32 u32MaxLen);

/**
 * @brief  Get segment by index
 */
const M3u8Segment *M3u8_GetSegment(const M3u8Playlist *pPlaylist, U32 u32Index);

/**
 * @brief  Find stream by bandwidth
 */
S32 M3u8_FindStreamByBandwidth(const M3u8Playlist *pPlaylist, U32 u32MaxBandwidth);

#ifdef __cplusplus
}
#endif

#endif /* __M3U8_PARSER_H__ */
