/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    sdp_parser.c
 * @Brief     :    SDP (Session Description Protocol) parser implementation.
 *------------------------------------------------------------------------------
 */

#include "sdp_parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Base64 decoding table */
static const U8 base64_table[256] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 62, 255, 255, 255, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 255, 255, 255, 0, 255, 255, 255,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 255, 255, 255, 255,
    255, 255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};

static S32 base64_decode(const CHAR *pszIn, U8 *pu8Out, U32 *pu32OutLen) {
    U32 u32InLen = strlen(pszIn);
    U32 u32Pos = 0;
    U32 u32OutPos = 0;
    U8 au8Buf[4];

    while (u32Pos < u32InLen) {
        /* Skip whitespace */
        while (u32Pos < u32InLen && isspace(pszIn[u32Pos]))
            u32Pos++;
        if (u32Pos >= u32InLen)
            break;

        /* Read 4 chars */
        for (int i = 0; i < 4 && u32Pos < u32InLen; i++, u32Pos++) {
            if (pszIn[u32Pos] == '=') {
                au8Buf[i] = 0;
            } else {
                au8Buf[i] = base64_table[(U8)pszIn[u32Pos]];
                if (au8Buf[i] == 255)
                    return -1;
            }
        }

        /* Decode 3 bytes */
        pu8Out[u32OutPos++] = (au8Buf[0] << 2) | (au8Buf[1] >> 4);
        if (pszIn[u32Pos - 2] != '=')
            pu8Out[u32OutPos++] = (au8Buf[1] << 4) | (au8Buf[2] >> 2);
        if (pszIn[u32Pos - 1] != '=')
            pu8Out[u32OutPos++] = (au8Buf[2] << 6) | au8Buf[3];
    }

    *pu32OutLen = u32OutPos;
    return 0;
}

/* Parse fmtp sprop-parameter-sets for H264 */
static S32 parse_h264_fmtp(const CHAR *pszFmtp, SdpInfo *pstInfo) {
    const CHAR *pSprop = strstr(pszFmtp, "sprop-parameter-sets=");
    if (!pSprop)
        return 0;

    pSprop += 21; /* Skip "sprop-parameter-sets=" */

    /* Format: base64(SPS),base64(PPS) */
    const CHAR *pComma = strchr(pSprop, ',');
    if (!pComma)
        return -1;

    /* Decode SPS */
    CHAR szSps[256];
    size_t sps_len = pComma - pSprop;
    if (sps_len >= sizeof(szSps))
        return -1;
    strncpy(szSps, pSprop, sps_len);
    szSps[sps_len] = '\0';

    if (base64_decode(szSps, pstInfo->au8Sps, &pstInfo->u32SpsLen) != 0) {
        return -1;
    }

    /* Decode PPS */
    pComma++;
    const CHAR *pEnd = pComma;
    while (*pEnd && *pEnd != ';' && *pEnd != ' ' && *pEnd != '\r' && *pEnd != '\n') {
        pEnd++;
    }

    CHAR szPps[64];
    size_t pps_len = pEnd - pComma;
    if (pps_len >= sizeof(szPps))
        return -1;
    strncpy(szPps, pComma, pps_len);
    szPps[pps_len] = '\0';

    if (base64_decode(szPps, pstInfo->au8Pps, &pstInfo->u32PpsLen) != 0) {
        return -1;
    }

    return 0;
}

/* Parse fmtp sprop-vps/sps/pps for H265 */
static S32 parse_h265_fmtp(const CHAR *pszFmtp, SdpInfo *pstInfo) {
    const CHAR *pVps = strstr(pszFmtp, "sprop-vps=");
    const CHAR *pSps = strstr(pszFmtp, "sprop-sps=");
    const CHAR *pPps = strstr(pszFmtp, "sprop-pps=");

    if (pVps) {
        pVps += 10;
        const CHAR *pEnd = pVps;
        while (*pEnd && *pEnd != ';' && *pEnd != ' ')
            pEnd++;
        CHAR szVps[256];
        strncpy(szVps, pVps, pEnd - pVps);
        szVps[pEnd - pVps] = '\0';
        base64_decode(szVps, pstInfo->au8Vps, &pstInfo->u32VpsLen);
    }

    if (pSps) {
        pSps += 10;
        const CHAR *pEnd = pSps;
        while (*pEnd && *pEnd != ';' && *pEnd != ' ')
            pEnd++;
        CHAR szSps[256];
        strncpy(szSps, pSps, pEnd - pSps);
        szSps[pEnd - pSps] = '\0';
        base64_decode(szSps, pstInfo->au8Sps, &pstInfo->u32SpsLen);
    }

    if (pPps) {
        pPps += 10;
        const CHAR *pEnd = pPps;
        while (*pEnd && *pEnd != ';' && *pEnd != ' ')
            pEnd++;
        CHAR szPps[64];
        strncpy(szPps, pPps, pEnd - pPps);
        szPps[pEnd - pPps] = '\0';
        base64_decode(szPps, pstInfo->au8Pps, &pstInfo->u32PpsLen);
    }

    return 0;
}

S32 Sdp_Parse(const CHAR *pszSdp, SdpInfo *pstInfo) {
    if (!pszSdp || !pstInfo)
        return -1;

    memset(pstInfo, 0, sizeof(*pstInfo));
    pstInfo->eCodec = SDP_CODEC_UNKNOWN;
    pstInfo->s32VideoTrackId = -1;
    pstInfo->s32AudioTrackId = -1;
    pstInfo->u32ClockRate = 90000; /* Default for video */

    const CHAR *pLine = pszSdp;
    BOOL bInVideo = MPP_FALSE;
    BOOL bInAudio = MPP_FALSE;

    while (pLine && *pLine) {
        /* Find end of line */
        const CHAR *pEnd = strstr(pLine, "\r\n");
        if (!pEnd)
            pEnd = pLine + strlen(pLine);

        size_t line_len = pEnd - pLine;

        if (strncmp(pLine, "m=video ", 8) == 0) {
            bInVideo = MPP_TRUE;
            bInAudio = MPP_FALSE;
        } else if (strncmp(pLine, "m=audio ", 8) == 0) {
            bInVideo = MPP_FALSE;
            bInAudio = MPP_TRUE;
        } else if (strncmp(pLine, "a=rtpmap:", 9) == 0) {
            int pt, clock;
            CHAR codec[32];

            if (sscanf(pLine + 9, "%d %31[^/]/%d", &pt, codec, &clock) >= 2) {
                if (bInVideo) {
                    if (strcasecmp(codec, "H264") == 0) {
                        pstInfo->eCodec = SDP_CODEC_H264;
                        pstInfo->u32ClockRate = clock;
                    } else if (strcasecmp(codec, "H265") == 0 || strcasecmp(codec, "HEVC") == 0) {
                        pstInfo->eCodec = SDP_CODEC_H265;
                        pstInfo->u32ClockRate = clock;
                    }
                } else if (bInAudio) {
                    if (strcasecmp(codec, "MPEG4-GENERIC") == 0) {
                        pstInfo->u32ClockRate = clock;
                    }
                }
            }
        } else if (strncmp(pLine, "a=fmtp:", 7) == 0 && bInVideo) {
            if (pstInfo->eCodec == SDP_CODEC_H264) {
                parse_h264_fmtp(pLine, pstInfo);
            } else if (pstInfo->eCodec == SDP_CODEC_H265) {
                parse_h265_fmtp(pLine, pstInfo);
            }
        } else if (strncmp(pLine, "a=control:", 10) == 0) {
            /* Capture the raw control string verbatim so SETUP can build the
             * exact URI the server advertised (mediamtx uses "trackID=0",
             * many IP cams use "trackID=N" or an absolute rtsp:// URL). */
            const CHAR *pCtrl = pLine + 10;
            size_t ctrl_len = (size_t)(pEnd - pCtrl);
            CHAR *pDst = bInVideo ? pstInfo->szVideoControl : (bInAudio ? pstInfo->szAudioControl : NULL);
            size_t dst_cap = sizeof(pstInfo->szVideoControl);
            if (pDst) {
                if (ctrl_len >= dst_cap) {
                    ctrl_len = dst_cap - 1;
                }
                memcpy(pDst, pCtrl, ctrl_len);
                pDst[ctrl_len] = '\0';
            }
            const CHAR *pTrack = strstr(pLine + 10, "trackID=");
            if (pTrack) {
                int track_id = atoi(pTrack + 8);
                if (bInVideo) {
                    pstInfo->s32VideoTrackId = track_id;
                } else if (bInAudio) {
                    pstInfo->s32AudioTrackId = track_id;
                }
            }
        } else if (strncmp(pLine, "a=framesize:", 12) == 0) {
            int pt, w, h;
            if (sscanf(pLine + 12, "%d %d-%d", &pt, &w, &h) == 3) {
                pstInfo->u32Width = w;
                pstInfo->u32Height = h;
            }
        } else if (strncmp(pLine, "a=framerate:", 12) == 0) {
            pstInfo->u32Fps = atoi(pLine + 12);
        }

        /* Move to next line */
        pLine = (*pEnd) ? pEnd + 2 : NULL;
    }

    /* Default track ID if not found */
    if (pstInfo->s32VideoTrackId < 0) {
        pstInfo->s32VideoTrackId = 0;
    }

    return 0;
}
