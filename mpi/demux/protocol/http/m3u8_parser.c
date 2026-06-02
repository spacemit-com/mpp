/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    m3u8_parser.c
 * @Brief     :    M3U8 playlist parser for HLS streaming.
 *------------------------------------------------------------------------------
 */

#include "m3u8_parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* M3U8 Tags */
#define M3U8_TAG_EXTM3U "#EXTM3U"
#define M3U8_TAG_EXT_X_VERSION "#EXT-X-VERSION"
#define M3U8_TAG_EXT_X_TARGETDURATION "#EXT-X-TARGETDURATION"
#define M3U8_TAG_EXT_X_MEDIA_SEQUENCE "#EXT-X-MEDIA-SEQUENCE"
#define M3U8_TAG_EXTINF "#EXTINF"
#define M3U8_TAG_EXT_X_ENDLIST "#EXT-X-ENDLIST"
#define M3U8_TAG_EXT_X_STREAM_INF "#EXT-X-STREAM-INF"
#define M3U8_TAG_EXT_X_DISCONTINUITY "#EXT-X-DISCONTINUITY"
#define M3U8_TAG_EXT_X_BYTERANGE "#EXT-X-BYTERANGE"

/* Skip whitespace */
static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p))
        p++;
    return p;
}

/* Get line from buffer, returns next line position */
static const char *get_line(const char *p, char *line, U32 maxLen) {
    U32 i = 0;

    while (*p && *p != '\n' && *p != '\r' && i < maxLen - 1) {
        line[i++] = *p++;
    }
    line[i] = '\0';

    /* Skip line ending */
    if (*p == '\r')
        p++;
    if (*p == '\n')
        p++;

    return p;
}

/* Parse attribute value: NAME=VALUE or NAME="VALUE" */
static const char *parse_attr_value(const char *p, const char *name, char *value, U32 maxLen) {
    U32 nameLen = strlen(name);
    const char *found = strstr(p, name);

    if (!found)
        return NULL;

    found += nameLen;
    if (*found != '=')
        return NULL;
    found++;

    U32 i = 0;
    if (*found == '"') {
        found++;
        while (*found && *found != '"' && i < maxLen - 1) {
            value[i++] = *found++;
        }
    } else {
        while (*found && *found != ',' && !isspace((unsigned char)*found) && i < maxLen - 1) {
            value[i++] = *found++;
        }
    }
    value[i] = '\0';

    return found;
}

/* Parse integer attribute */
static U32 parse_int_attr(const char *p, const char *name) {
    char value[32];
    if (parse_attr_value(p, name, value, sizeof(value))) {
        return (U32)atoi(value);
    }
    return 0;
}

/* Parse resolution WIDTHxHEIGHT */
static void parse_resolution(const char *p, U32 *width, U32 *height) {
    char value[32];
    if (parse_attr_value(p, "RESOLUTION", value, sizeof(value))) {
        char *x = strchr(value, 'x');
        if (x) {
            *x = '\0';
            *width = (U32)atoi(value);
            *height = (U32)atoi(x + 1);
        }
    }
}

S32 M3u8_Parse(const char *pContent, U32 u32Len, M3u8Playlist *pPlaylist) {
    char line[M3U8_MAX_URL_LEN];
    const char *p;
    const char *end;
    float fDuration = 0.0f;
    BOOL bDiscontinuity = MPP_FALSE;
    S64 s64ByteRangeStart = -1;
    S64 s64ByteRangeLen = -1;

    if (!pContent || !pPlaylist) {
        return -1;
    }

    memset(pPlaylist, 0, sizeof(M3u8Playlist));
    pPlaylist->bLive = MPP_TRUE; /* Assume live until #EXT-X-ENDLIST */

    p = pContent;
    end = pContent + u32Len;

    /* First line must be #EXTM3U */
    p = get_line(p, line, sizeof(line));
    if (strncmp(line, M3U8_TAG_EXTM3U, strlen(M3U8_TAG_EXTM3U)) != 0) {
        return -1; /* Not a valid M3U8 */
    }

    while (p < end && *p) {
        p = skip_ws(p);
        if (!*p)
            break;

        p = get_line(p, line, sizeof(line));

        if (line[0] == '\0') {
            continue;
        }

        if (strncmp(line, M3U8_TAG_EXT_X_VERSION, strlen(M3U8_TAG_EXT_X_VERSION)) == 0) {
            pPlaylist->u32Version = (U32)atoi(line + strlen(M3U8_TAG_EXT_X_VERSION) + 1);
        } else if (strncmp(line, M3U8_TAG_EXT_X_TARGETDURATION, strlen(M3U8_TAG_EXT_X_TARGETDURATION)) == 0) {
            pPlaylist->fTargetDuration = (float)atof(line + strlen(M3U8_TAG_EXT_X_TARGETDURATION) + 1);
        } else if (strncmp(line, M3U8_TAG_EXT_X_MEDIA_SEQUENCE, strlen(M3U8_TAG_EXT_X_MEDIA_SEQUENCE)) == 0) {
            pPlaylist->u64MediaSequence = (U64)atoll(line + strlen(M3U8_TAG_EXT_X_MEDIA_SEQUENCE) + 1);
        } else if (strncmp(line, M3U8_TAG_EXT_X_ENDLIST, strlen(M3U8_TAG_EXT_X_ENDLIST)) == 0) {
            pPlaylist->bEndList = MPP_TRUE;
            pPlaylist->bLive = MPP_FALSE;
        } else if (strncmp(line, M3U8_TAG_EXT_X_DISCONTINUITY, strlen(M3U8_TAG_EXT_X_DISCONTINUITY)) == 0) {
            bDiscontinuity = MPP_TRUE;
        } else if (strncmp(line, M3U8_TAG_EXT_X_BYTERANGE, strlen(M3U8_TAG_EXT_X_BYTERANGE)) == 0) {
            const char *val = line + strlen(M3U8_TAG_EXT_X_BYTERANGE) + 1;
            s64ByteRangeLen = atoll(val);
            const char *at = strchr(val, '@');
            if (at) {
                s64ByteRangeStart = atoll(at + 1);
            }
        } else if (strncmp(line, M3U8_TAG_EXT_X_STREAM_INF, strlen(M3U8_TAG_EXT_X_STREAM_INF)) == 0) {
            pPlaylist->bMasterPlaylist = MPP_TRUE;

            if (pPlaylist->u32StreamCount < 8) {
                U32 idx = pPlaylist->u32StreamCount;

                pPlaylist->astStreams[idx].u32Bandwidth = parse_int_attr(line, "BANDWIDTH");
                parse_resolution(line, &pPlaylist->astStreams[idx].u32Width, &pPlaylist->astStreams[idx].u32Height);
                parse_attr_value(
                    line, "CODECS", pPlaylist->astStreams[idx].szCodecs, sizeof(pPlaylist->astStreams[idx].szCodecs));

                /* Next line is URL */
                p = skip_ws(p);
                p = get_line(p, line, sizeof(line));
                if (line[0] && line[0] != '#') {
                    strncpy(pPlaylist->astStreams[idx].szUrl, line, M3U8_MAX_URL_LEN - 1);
                    pPlaylist->u32StreamCount++;
                }
            }
        } else if (strncmp(line, M3U8_TAG_EXTINF, strlen(M3U8_TAG_EXTINF)) == 0) {
            const char *val = line + strlen(M3U8_TAG_EXTINF) + 1;
            fDuration = (float)atof(val);
        } else if (line[0] != '#' && line[0] != '\0') {
            if (pPlaylist->u32SegmentCount < M3U8_MAX_SEGMENTS && fDuration > 0) {
                M3u8Segment *seg = &pPlaylist->astSegments[pPlaylist->u32SegmentCount];

                strncpy(seg->szUrl, line, M3U8_MAX_URL_LEN - 1);
                seg->fDuration = fDuration;
                seg->bDiscontinuity = bDiscontinuity;
                seg->s64ByteRangeStart = s64ByteRangeStart;
                seg->s64ByteRangeLen = s64ByteRangeLen;

                pPlaylist->u32SegmentCount++;

                /* Reset for next segment */
                fDuration = 0.0f;
                bDiscontinuity = MPP_FALSE;
                s64ByteRangeStart = -1;
                s64ByteRangeLen = -1;
            }
        }
    }

    return 0;
}

S32 M3u8_ResolveUrl(const char *pBaseUrl, const char *pRelUrl, char *pAbsUrl, U32 u32MaxLen) {
    if (!pBaseUrl || !pRelUrl || !pAbsUrl) {
        return -1;
    }

    /* Already absolute URL */
    if (strncmp(pRelUrl, "http://", 7) == 0 || strncmp(pRelUrl, "https://", 8) == 0) {
        strncpy(pAbsUrl, pRelUrl, u32MaxLen - 1);
        pAbsUrl[u32MaxLen - 1] = '\0';
        return 0;
    }

    /* Find base path (remove filename) */
    const char *lastSlash = strrchr(pBaseUrl, '/');
    if (!lastSlash) {
        strncpy(pAbsUrl, pRelUrl, u32MaxLen - 1);
        return 0;
    }

    /* Build absolute URL */
    U32 baseLen = (U32)(lastSlash - pBaseUrl + 1);
    if (baseLen >= u32MaxLen) {
        return -1;
    }

    memcpy(pAbsUrl, pBaseUrl, baseLen);
    strncpy(pAbsUrl + baseLen, pRelUrl, u32MaxLen - baseLen - 1);
    pAbsUrl[u32MaxLen - 1] = '\0';

    return 0;
}

const M3u8Segment *M3u8_GetSegment(const M3u8Playlist *pPlaylist, U32 u32Index) {
    if (!pPlaylist || u32Index >= pPlaylist->u32SegmentCount) {
        return NULL;
    }
    return &pPlaylist->astSegments[u32Index];
}

S32 M3u8_FindStreamByBandwidth(const M3u8Playlist *pPlaylist, U32 u32MaxBandwidth) {
    if (!pPlaylist || !pPlaylist->bMasterPlaylist) {
        return -1;
    }

    S32 bestIdx = -1;
    U32 bestBandwidth = 0;

    for (U32 i = 0; i < pPlaylist->u32StreamCount; i++) {
        U32 bw = pPlaylist->astStreams[i].u32Bandwidth;
        if (bw <= u32MaxBandwidth && bw > bestBandwidth) {
            bestBandwidth = bw;
            bestIdx = (S32)i;
        }
    }

    /* If no stream fits, return highest quality stream */
    if (bestIdx < 0 && pPlaylist->u32StreamCount > 0) {
        bestIdx = 0;
        for (U32 i = 1; i < pPlaylist->u32StreamCount; i++) {
            if (pPlaylist->astStreams[i].u32Bandwidth > pPlaylist->astStreams[bestIdx].u32Bandwidth) {
                bestIdx = (S32)i;
            }
        }
    }

    return bestIdx;
}
