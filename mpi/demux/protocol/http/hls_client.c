/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    hls_client.c
 * @Brief     :    HLS (HTTP Live Streaming) client implementation.
 *------------------------------------------------------------------------------
 */

#include "hls_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/socket_utils.h"
#include "common/url_parser.h"
#include "container/ts/ts_demuxer.h"

#define HLS_MAX_SEGMENTS 100
#define HLS_MAX_URL_LEN 512
#define HLS_HTTP_BUF_SIZE (1024 * 1024)

typedef struct _HlsSegment {
    CHAR szUrl[HLS_MAX_URL_LEN];
    float fDuration;
    U64 u64SeqNum;
} HlsSegment;

typedef enum _HlsState { HLS_STATE_INIT, HLS_STATE_PLAYLIST, HLS_STATE_SEGMENT, HLS_STATE_STREAMING } HlsState;

struct _HlsClient {
    CHAR szPlaylistUrl[HLS_MAX_URL_LEN];
    CHAR szBaseUrl[HLS_MAX_URL_LEN];
    UrlInfo stUrl;
    HlsState eState;

    /* Playlist */
    HlsSegment astSegments[HLS_MAX_SEGMENTS];
    U32 u32SegmentCount;
    U32 u32CurrentSegment;
    U64 u64MediaSequence;
    float fTargetDuration;
    BOOL bLive;

    /* Current segment */
    TsDemuxer *pTsDemux;
    U8 *pu8SegmentBuf;
    U32 u32SegmentLen;

    /* HTTP */
    S32 s32Fd;
    U8 au8HttpBuf[HLS_HTTP_BUF_SIZE];

    /* Stream info */
    DemuxStreamInfo stStreamInfo;
};

static S32 hls_http_get(HlsClient *pClient, const CHAR *pszUrl, U8 *pu8Buf, U32 u32BufSize, U32 *pu32Len) {
    UrlInfo url;
    if (Url_Parse(pszUrl, &url) != 0)
        return -1;

    S32 s32Fd = Socket_TcpConnect(url.szHost, url.u16Port, 5000);
    if (s32Fd < 0)
        return -1;

    /* Send HTTP GET */
    CHAR szReq[1024];
    S32 reqLen = snprintf(szReq, sizeof(szReq),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        url.szPath, url.szHost);

    if (Socket_SendAll(s32Fd, (U8 *)szReq, reqLen) < 0) {
        Socket_Close(s32Fd);
        return -1;
    }

    /* Receive response */
    *pu32Len = 0;
    while (*pu32Len < u32BufSize) {
        S32 n = Socket_RecvTimeout(s32Fd, pu8Buf + *pu32Len, u32BufSize - *pu32Len, 5000);
        if (n <= 0)
            break;
        *pu32Len += n;
    }

    Socket_Close(s32Fd);

    /* Skip HTTP header */
    U8 *pBody = (U8 *)strstr((CHAR *)pu8Buf, "\r\n\r\n");
    if (pBody) {
        pBody += 4;
        U32 bodyLen = *pu32Len - (pBody - pu8Buf);
        memmove(pu8Buf, pBody, bodyLen);
        *pu32Len = bodyLen;
    }

    return 0;
}

static S32 hls_parse_playlist(HlsClient *pClient, const CHAR *pszPlaylist) {
    pClient->u32SegmentCount = 0;
    pClient->bLive = MPP_TRUE; /* Assume live until #EXT-X-ENDLIST */

    const CHAR *pLine = pszPlaylist;
    float fSegDuration = 0;

    while (pLine && *pLine) {
        const CHAR *pEnd = strstr(pLine, "\n");
        if (!pEnd)
            pEnd = pLine + strlen(pLine);

        size_t lineLen = pEnd - pLine;
        CHAR szLine[512];
        if (lineLen >= sizeof(szLine))
            lineLen = sizeof(szLine) - 1;
        strncpy(szLine, pLine, lineLen);
        szLine[lineLen] = '\0';

        /* Remove trailing \r */
        if (lineLen > 0 && szLine[lineLen - 1] == '\r') {
            szLine[lineLen - 1] = '\0';
        }

        if (strncmp(szLine, "#EXT-X-TARGETDURATION:", 22) == 0) {
            pClient->fTargetDuration = atof(szLine + 22);
        } else if (strncmp(szLine, "#EXT-X-MEDIA-SEQUENCE:", 22) == 0) {
            pClient->u64MediaSequence = atoll(szLine + 22);
        } else if (strncmp(szLine, "#EXTINF:", 8) == 0) {
            fSegDuration = atof(szLine + 8);
        } else if (strncmp(szLine, "#EXT-X-ENDLIST", 14) == 0) {
            pClient->bLive = MPP_FALSE;
        } else if (szLine[0] != '#' && szLine[0] != '\0') {
            /* Segment URL */
            if (pClient->u32SegmentCount < HLS_MAX_SEGMENTS) {
                HlsSegment *pSeg = &pClient->astSegments[pClient->u32SegmentCount];

                if (szLine[0] == '/' || strncmp(szLine, "http", 4) == 0) {
                    snprintf(pSeg->szUrl, HLS_MAX_URL_LEN, "%s", szLine);
                } else {
                    /* Combine base URL and segment path. If the combined
                     * length exceeds HLS_MAX_URL_LEN the URL is truncated
                     * which would cause an HTTP 404 — log a warning so the
                     * operator knows the URL limit was exceeded. */
                    CHAR szCombined[HLS_MAX_URL_LEN * 2];
                    int n = snprintf(szCombined, sizeof(szCombined), "%s/%s",
                        pClient->szBaseUrl, szLine);
                    if (n >= (int)HLS_MAX_URL_LEN) {
                        fprintf(stderr, "[HLS][WRN] segment URL truncated "
                            "(%d > %d): %s\n", n, HLS_MAX_URL_LEN - 1, szLine);
                    }
                    snprintf(pSeg->szUrl, HLS_MAX_URL_LEN, "%.*s",
                        (int)(HLS_MAX_URL_LEN - 1), szCombined);
                }

                pSeg->fDuration = fSegDuration;
                pSeg->u64SeqNum = pClient->u64MediaSequence + pClient->u32SegmentCount;
                pClient->u32SegmentCount++;
            }
        }

        pLine = (*pEnd) ? pEnd + 1 : NULL;
    }

    return 0;
}

HlsClient *HlsClient_Create(VOID) {
    HlsClient *pClient = (HlsClient *)calloc(1, sizeof(HlsClient));
    if (pClient) {
        pClient->s32Fd = -1;
        pClient->pTsDemux = TsDemuxer_Create();
    }
    return pClient;
}

VOID HlsClient_Destroy(HlsClient *pClient) {
    if (pClient) {
        HlsClient_Close(pClient);
        if (pClient->pTsDemux) {
            TsDemuxer_Destroy(pClient->pTsDemux);
        }
        free(pClient);
    }
}

S32 HlsClient_Open(HlsClient *pClient, const CHAR *pszUrl, U32 u32TimeoutMs) {
    if (!pClient || !pszUrl)
        return ERR_DEMUX_NULL_PTR;

    strncpy(pClient->szPlaylistUrl, pszUrl, HLS_MAX_URL_LEN - 1);

    /* Extract base URL */
    const CHAR *pLastSlash = strrchr(pszUrl, '/');
    if (pLastSlash) {
        size_t baseLen = pLastSlash - pszUrl;
        if (baseLen >= HLS_MAX_URL_LEN)
            baseLen = HLS_MAX_URL_LEN - 1;
        strncpy(pClient->szBaseUrl, pszUrl, baseLen);
        pClient->szBaseUrl[baseLen] = '\0';
    }

    /* Parse URL */
    if (Url_Parse(pszUrl, &pClient->stUrl) != 0) {
        return ERR_DEMUX_OPEN_FAIL;
    }

    /* Fetch playlist */
    U32 u32PlaylistLen = 0;
    if (hls_http_get(pClient, pszUrl, pClient->au8HttpBuf, sizeof(pClient->au8HttpBuf), &u32PlaylistLen) != 0) {
        return ERR_DEMUX_OPEN_FAIL;
    }

    pClient->au8HttpBuf[u32PlaylistLen] = '\0';

    /* Parse playlist */
    if (hls_parse_playlist(pClient, (CHAR *)pClient->au8HttpBuf) != 0) {
        return ERR_DEMUX_OPEN_FAIL;
    }

    if (pClient->u32SegmentCount == 0) {
        return ERR_DEMUX_OPEN_FAIL;
    }

    pClient->u32CurrentSegment = 0;
    pClient->eState = HLS_STATE_STREAMING;

    return 0;
}

VOID HlsClient_Close(HlsClient *pClient) {
    if (!pClient)
        return;

    if (pClient->s32Fd >= 0) {
        Socket_Close(pClient->s32Fd);
        pClient->s32Fd = -1;
    }

    if (pClient->pu8SegmentBuf) {
        free(pClient->pu8SegmentBuf);
        pClient->pu8SegmentBuf = NULL;
    }

    pClient->eState = HLS_STATE_INIT;
}

S32 HlsClient_GetStreamInfo(HlsClient *pClient, DemuxStreamInfo *pstInfo) {
    if (!pClient || !pstInfo)
        return ERR_DEMUX_NULL_PTR;
    *pstInfo = pClient->stStreamInfo;
    return 0;
}

S32 HlsClient_ReadPacket(HlsClient *pClient, DemuxPacket *pstPkt) {
    if (!pClient || !pstPkt)
        return ERR_DEMUX_NULL_PTR;
    if (pClient->eState != HLS_STATE_STREAMING)
        return ERR_DEMUX_NOT_STARTED;

    /* Try to read from current segment */
    if (pClient->pTsDemux) {
        S32 ret = TsDemuxer_ReadPacket(pClient->pTsDemux, pstPkt);
        if (ret == 0) {
            return 0;
        }
    }

    /* Fetch next segment */
    if (pClient->u32CurrentSegment >= pClient->u32SegmentCount) {
        if (pClient->bLive) {
            /* Refresh playlist for live stream */
            U32 u32PlaylistLen = 0;
            if (hls_http_get(pClient, pClient->szPlaylistUrl, pClient->au8HttpBuf, sizeof(pClient->au8HttpBuf),
                    &u32PlaylistLen) == 0) {
                pClient->au8HttpBuf[u32PlaylistLen] = '\0';
                hls_parse_playlist(pClient, (CHAR *)pClient->au8HttpBuf);
                /* TODO: Skip already played segments */
            }
        } else {
            return -1; /* EOF */
        }
    }

    if (pClient->u32CurrentSegment >= pClient->u32SegmentCount) {
        return -1;
    }

    /* Download segment */
    HlsSegment *pSeg = &pClient->astSegments[pClient->u32CurrentSegment];
    U32 u32SegLen = 0;

    if (hls_http_get(pClient, pSeg->szUrl, pClient->au8HttpBuf, sizeof(pClient->au8HttpBuf), &u32SegLen) != 0) {
        return ERR_DEMUX_NO_STREAM;
    }

    pClient->u32CurrentSegment++;

    /* Feed to TS demuxer */
    TsDemuxer_FeedData(pClient->pTsDemux, pClient->au8HttpBuf, u32SegLen);

    /* Try reading again */
    return TsDemuxer_ReadPacket(pClient->pTsDemux, pstPkt);
}
