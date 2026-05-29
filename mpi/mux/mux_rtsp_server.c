/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_rtsp_server.c
 * @Date      :    2026-04-15
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Shared RTSP server for MUX module (multi-stream support).
 *
 * Architecture:
 *   - Single global server listening on one port (singleton)
 *   - Multiple streams registered with different paths (/live/0, /live/1, etc.)
 *   - Clients connect and request specific path, routed to correct stream
 *   - Standard MUX_* API unchanged, internal implementation handles sharing
 *------------------------------------------------------------------------------
 */

#include "mux_rtsp_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MUX_RTSP_LOGE(fmt, ...) fprintf(stderr, "[MUX][RTSP][ERR] " fmt "\n", ##__VA_ARGS__)
#define MUX_RTSP_LOGI(fmt, ...) fprintf(stdout, "[MUX][RTSP][INF] " fmt "\n", ##__VA_ARGS__)

/* ======================== Global Shared Server ======================== */

static MuxGlobalRtspServer g_stGlobalServer = {0};

static MuxRtspStream *mux_rtsp_find_stream_by_path(const CHAR *pszPath) {
    for (S32 i = 0; i < MUX_RTSP_MAX_STREAMS; i++) {
        if (g_stGlobalServer.astStreams[i].s32Used && strcmp(g_stGlobalServer.astStreams[i].szPath, pszPath) == 0) {
            return &g_stGlobalServer.astStreams[i];
        }
    }
    return NULL;
}

static MuxRtspStream *mux_rtsp_find_stream_by_chn(S32 s32ChnId) {
    for (S32 i = 0; i < MUX_RTSP_MAX_STREAMS; i++) {
        if (g_stGlobalServer.astStreams[i].s32Used && g_stGlobalServer.astStreams[i].s32ChnId == s32ChnId) {
            return &g_stGlobalServer.astStreams[i];
        }
    }
    return NULL;
}

static CHAR *mux_rtsp_extract_path_from_url(const CHAR *pszUrl) {
    static CHAR szPath[128];
    const CHAR *p = strstr(pszUrl, "://");
    if (p) {
        p = strchr(p + 3, '/');
        if (p) {
            /* Remove trackID suffix if present */
            const CHAR *track = strstr(p, "/trackID");
            if (!track)
                track = strstr(p, "/track");
            if (track) {
                snprintf(szPath, sizeof(szPath), "%.*s", (int)(track - p), p);
            } else {
                snprintf(szPath, sizeof(szPath), "%s", p);
            }
            return szPath;
        }
    }
    return NULL;
}

static S32 mux_rtsp_parse_url(
    const CHAR *pszUrl, CHAR *pszHost, U32 u32HostLen, U16 *pu16Port, CHAR *pszPath, U32 u32PathLen) {
    const CHAR *p;
    const CHAR *hostStart;
    const CHAR *pathStart;
    const CHAR *portStart = NULL;
    size_t hostLen;

    if (!pszUrl || !pszHost || !pu16Port || !pszPath) {
        return ERR_MUX_NULL_PTR;
    }

    p = strstr(pszUrl, "rtsp://");
    if (!p) {
        return ERR_MUX_OPEN_FAIL;
    }
    hostStart = p + 7;
    pathStart = strchr(hostStart, '/');
    if (!pathStart) {
        pathStart = hostStart + strlen(hostStart);
        snprintf(pszPath, u32PathLen, "/live");
    } else {
        snprintf(pszPath, u32PathLen, "%s", pathStart);
    }

    for (const CHAR *q = hostStart; q < pathStart; ++q) {
        if (*q == ':') {
            portStart = q + 1;
            hostLen = (size_t)(q - hostStart);
            goto found;
        }
    }
    hostLen = (size_t)(pathStart - hostStart);
    *pu16Port = 8554;
    goto out;

found:
    *pu16Port = (U16)atoi(portStart);

out:
    if (hostLen >= u32HostLen) {
        hostLen = u32HostLen - 1;
    }
    memcpy(pszHost, hostStart, hostLen);
    pszHost[hostLen] = '\0';
    return ERR_MUX_OK;
}

static const CHAR *mux_rtsp_find_header(const CHAR *pszReq, const CHAR *pszKey) {
    const CHAR *p = pszReq;
    size_t keyLen = strlen(pszKey);

    while (p && *p) {
        const CHAR *eol = strstr(p, "\r\n");
        if (!eol) {
            break;
        }
        if ((size_t)(eol - p) > keyLen && strncasecmp(p, pszKey, keyLen) == 0 && p[keyLen] == ':') {
            p += keyLen + 1;
            while (*p == ' ') {
                ++p;
            }
            return p;
        }
        p = eol + 2;
    }
    return NULL;
}

static U32 mux_rtsp_get_cseq(const CHAR *pszReq) {
    const CHAR *p = mux_rtsp_find_header(pszReq, "CSeq");
    if (!p) {
        return 1;
    }
    return (U32)atoi(p);
}

static VOID mux_rtsp_make_session_id(CHAR *pszBuf, U32 u32BufLen) {
    unsigned int r = (unsigned int)rand();
    snprintf(pszBuf, u32BufLen, "%08x%08llx", r, (uint64_t)time(NULL));
}

static S32 mux_rtsp_send_response(MuxRtspClient *pstClient, const CHAR *pszBody, const CHAR *pszFmt, ...) {
    CHAR szHdr[MUX_RTSP_SEND_BUF_SIZE];
    CHAR szMsg[MUX_RTSP_SEND_BUF_SIZE + 2048];
    va_list ap;
    int hdrLen;
    int bodyLen = pszBody ? (int)strlen(pszBody) : 0;

    va_start(ap, pszFmt);
    hdrLen = vsnprintf(szHdr, sizeof(szHdr), pszFmt, ap);
    va_end(ap);
    if (hdrLen < 0) {
        return -1;
    }

    if (bodyLen > 0) {
        snprintf(szMsg, sizeof(szMsg), "%sContent-Length: %d\r\n\r\n%s", szHdr, bodyLen, pszBody);
    } else {
        snprintf(szMsg, sizeof(szMsg), "%s\r\n", szHdr);
    }

    return send(pstClient->s32RtspFd, szMsg, strlen(szMsg), 0) < 0 ? -1 : 0;
}

static S32 mux_rtsp_base64_encode(const U8 *pu8Src, U32 u32SrcLen, CHAR *pszDst, U32 u32DstLen) {
    static const CHAR s_szTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    U32 i = 0, j = 0;

    if (!pu8Src || !pszDst || u32DstLen < ((u32SrcLen + 2) / 3) * 4 + 1) {
        return -1;
    }

    while (i < u32SrcLen) {
        U32 a = pu8Src[i++];
        U32 b = (i < u32SrcLen) ? pu8Src[i++] : 0;
        U32 c = (i < u32SrcLen) ? pu8Src[i++] : 0;
        U32 triple = (a << 16) | (b << 8) | c;

        pszDst[j++] = s_szTable[(triple >> 18) & 0x3f];
        pszDst[j++] = s_szTable[(triple >> 12) & 0x3f];
        pszDst[j++] = (i > u32SrcLen + 1) ? '=' : s_szTable[(triple >> 6) & 0x3f];
        pszDst[j++] = (i > u32SrcLen) ? '=' : s_szTable[triple & 0x3f];
    }
    pszDst[j] = '\0';
    return (S32)j;
}

/* Generate SDP using stream's cached parameters */
static S32 mux_rtsp_make_sdp_for_stream(MuxRtspStream *pStream, CHAR *pszSdp, U32 u32SdpLen) {
    const CHAR *pszCodec;
    CHAR szFmtp[2048];

    if (!pStream || !pszSdp) {
        return ERR_MUX_NULL_PTR;
    }

    if (pStream->eCodecType == MUX_CODEC_H264) {
        pszCodec = "H264";
        if (pStream->u32SpsLen > 0 && pStream->u32PpsLen > 0) {
            CHAR szSpsB64[512], szPpsB64[512];
            mux_rtsp_base64_encode(pStream->au8Sps, pStream->u32SpsLen, szSpsB64, sizeof(szSpsB64));
            mux_rtsp_base64_encode(pStream->au8Pps, pStream->u32PpsLen, szPpsB64, sizeof(szPpsB64));
            snprintf(szFmtp, sizeof(szFmtp),
                "a=fmtp:96 packetization-mode=1;profile-level-id=%02X%02X%02X;"
                "sprop-parameter-sets=%s,%s\r\n",
                pStream->au8Sps[1], pStream->au8Sps[2], pStream->au8Sps[3], szSpsB64, szPpsB64);
        } else {
            snprintf(szFmtp, sizeof(szFmtp), "a=fmtp:96 packetization-mode=1\r\n");
        }
    } else if (pStream->eCodecType == MUX_CODEC_H265) {
        pszCodec = "H265";
        if (pStream->u32VpsLen > 0 && pStream->u32SpsLen > 0 && pStream->u32PpsLen > 0) {
            CHAR szVpsB64[512], szSpsB64[512], szPpsB64[512];
            mux_rtsp_base64_encode(pStream->au8Vps, pStream->u32VpsLen, szVpsB64, sizeof(szVpsB64));
            mux_rtsp_base64_encode(pStream->au8Sps, pStream->u32SpsLen, szSpsB64, sizeof(szSpsB64));
            mux_rtsp_base64_encode(pStream->au8Pps, pStream->u32PpsLen, szPpsB64, sizeof(szPpsB64));
            snprintf(szFmtp, sizeof(szFmtp), "a=fmtp:96 sprop-vps=%s;sprop-sps=%s;sprop-pps=%s\r\n", szVpsB64, szSpsB64,
                szPpsB64);
        } else {
            snprintf(szFmtp, sizeof(szFmtp), "a=fmtp:96\r\n");
        }
    } else {
        return ERR_MUX_OPEN_FAIL;
    }

    snprintf(pszSdp, u32SdpLen,
        "v=0\r\n"
        "o=- 0 0 IN IP4 0.0.0.0\r\n"
        "s=SPACEMIT MUX RTSP Server\r\n"
        "t=0 0\r\n"
        "a=control:*\r\n"
        "m=video 0 RTP/AVP 96\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtpmap:96 %s/90000\r\n"
        "%s"
        "a=control:trackID=0\r\n",
        pszCodec, szFmtp);
    return ERR_MUX_OK;
}

static VOID mux_rtsp_client_close(MuxRtspClient *pstClient) {
    if (!pstClient || !pstClient->s32Used) {
        return;
    }

    if (pstClient->s32RtpSock > 0) {
        close(pstClient->s32RtpSock);
    }
    if (pstClient->s32RtcpSock > 0) {
        close(pstClient->s32RtcpSock);
    }
    if (pstClient->s32RtspFd > 0) {
        close(pstClient->s32RtspFd);
    }
    memset(pstClient, 0, sizeof(*pstClient));
}

static MuxRtspClient *mux_rtsp_client_alloc(void) {
    for (S32 i = 0; i < MUX_RTSP_MAX_CLIENTS; ++i) {
        if (!g_stGlobalServer.astClients[i].s32Used) {
            MuxRtspClient *pClient = &g_stGlobalServer.astClients[i];
            memset(pClient, 0, sizeof(*pClient));
            pClient->s32Used = 1;
            pClient->s32RtspFd = -1;
            pClient->s32RtpSock = -1;
            pClient->s32RtcpSock = -1;
            return pClient;
        }
    }
    return NULL;
}

static S32 mux_rtsp_handle_options(MuxRtspClient *pstClient, U32 u32CSeq) {
    return mux_rtsp_send_response(pstClient, NULL,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %u\r\n"
        "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER, SET_PARAMETER\r\n",
        u32CSeq);
}

static S32 mux_rtsp_handle_describe(MuxRtspClient *pstClient, const CHAR *pszReq, U32 u32CSeq) {
    CHAR szSdp[1024];
    CHAR szUrl[256] = {0};
    CHAR *pszPath;
    MuxRtspStream *pStream;

    /* Extract URL from request line */
    if (sscanf(pszReq, "DESCRIBE %255s", szUrl) != 1) {
        return mux_rtsp_send_response(pstClient, NULL, "RTSP/1.0 400 Bad Request\r\nCSeq: %u\r\n", u32CSeq);
    }

    /* Find stream by path */
    pszPath = mux_rtsp_extract_path_from_url(szUrl);
    pStream = pszPath ? mux_rtsp_find_stream_by_path(pszPath) : NULL;

    if (!pStream) {
        MUX_RTSP_LOGE("DESCRIBE: stream not found for path '%s'", pszPath ? pszPath : "(null)");
        return mux_rtsp_send_response(pstClient, NULL, "RTSP/1.0 404 Not Found\r\nCSeq: %u\r\n", u32CSeq);
    }

    /* Associate client with stream */
    pstClient->pStream = pStream;

    if (mux_rtsp_make_sdp_for_stream(pStream, szSdp, sizeof(szSdp)) != ERR_MUX_OK) {
        return mux_rtsp_send_response(pstClient, NULL, "RTSP/1.0 500 Internal Server Error\r\nCSeq: %u\r\n", u32CSeq);
    }

    return mux_rtsp_send_response(pstClient, szSdp,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %u\r\n"
        "Content-Type: application/sdp\r\n",
        u32CSeq);
}

static S32 mux_rtsp_parse_transport(MuxRtspClient *pstClient, const CHAR *pszTransport) {
    const CHAR *p;

    if (!pstClient || !pszTransport) {
        return ERR_MUX_NULL_PTR;
    }

    if (strstr(pszTransport, "RTP/AVP/TCP")) {
        pstClient->bInterleaved = MPP_TRUE;
        p = strstr(pszTransport, "interleaved=");
        if (p) {
            pstClient->u8RtpChannel = (U8)atoi(p + strlen("interleaved="));
            p = strchr(p, '-');
            pstClient->u8RtcpChannel = p ? (U8)atoi(p + 1) : (U8)(pstClient->u8RtpChannel + 1);
        } else {
            pstClient->u8RtpChannel = 0;
            pstClient->u8RtcpChannel = 1;
        }
        return ERR_MUX_OK;
    }

    pstClient->bInterleaved = MPP_FALSE;
    p = strstr(pszTransport, "client_port=");
    if (!p) {
        return ERR_MUX_OPEN_FAIL;
    }
    pstClient->u16ClientRtpPort = (U16)atoi(p + strlen("client_port="));
    p = strchr(p, '-');
    pstClient->u16ClientRtcpPort = p ? (U16)atoi(p + 1) : (U16)(pstClient->u16ClientRtpPort + 1);
    return ERR_MUX_OK;
}

static S32 mux_rtsp_open_udp(MuxRtspClient *pstClient) {
    pstClient->s32RtpSock = socket(AF_INET, SOCK_DGRAM, 0);
    pstClient->s32RtcpSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (pstClient->s32RtpSock < 0 || pstClient->s32RtcpSock < 0) {
        return ERR_MUX_OPEN_FAIL;
    }

    memset(&pstClient->stClientRtpAddr, 0, sizeof(pstClient->stClientRtpAddr));
    pstClient->stClientRtpAddr.sin_family = AF_INET;
    pstClient->stClientRtpAddr.sin_addr = pstClient->stPeerAddr.sin_addr;
    pstClient->stClientRtpAddr.sin_port = htons(pstClient->u16ClientRtpPort);

    memset(&pstClient->stClientRtcpAddr, 0, sizeof(pstClient->stClientRtcpAddr));
    pstClient->stClientRtcpAddr.sin_family = AF_INET;
    pstClient->stClientRtcpAddr.sin_addr = pstClient->stPeerAddr.sin_addr;
    pstClient->stClientRtcpAddr.sin_port = htons(pstClient->u16ClientRtcpPort);
    return ERR_MUX_OK;
}

static S32 mux_rtsp_handle_setup(MuxRtspClient *pstClient, const CHAR *pszReq, U32 u32CSeq) {
    const CHAR *pszTransport = mux_rtsp_find_header(pszReq, "Transport");

    if (!pszTransport || mux_rtsp_parse_transport(pstClient, pszTransport) != ERR_MUX_OK) {
        return mux_rtsp_send_response(pstClient, NULL, "RTSP/1.0 461 Unsupported Transport\r\nCSeq: %u\r\n", u32CSeq);
    }

    if (!pstClient->bInterleaved) {
        if (mux_rtsp_open_udp(pstClient) != ERR_MUX_OK) {
            return mux_rtsp_send_response(
                pstClient, NULL, "RTSP/1.0 500 Internal Server Error\r\nCSeq: %u\r\n", u32CSeq);
        }
    }

    mux_rtsp_make_session_id(pstClient->szSessionId, sizeof(pstClient->szSessionId));
    pstClient->eState = MUX_RTSP_CLIENT_READY;

    if (pstClient->bInterleaved) {
        return mux_rtsp_send_response(pstClient, NULL,
            "RTSP/1.0 200 OK\r\n"
            "CSeq: %u\r\n"
            "Transport: RTP/AVP/TCP;unicast;interleaved=%u-%u\r\n"
            "Session: %s\r\n",
            u32CSeq, pstClient->u8RtpChannel, pstClient->u8RtcpChannel, pstClient->szSessionId);
    }

    return mux_rtsp_send_response(pstClient, NULL,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %u\r\n"
        "Transport: RTP/AVP;unicast;client_port=%u-%u;server_port=0-0\r\n"
        "Session: %s\r\n",
        u32CSeq, pstClient->u16ClientRtpPort, pstClient->u16ClientRtcpPort, pstClient->szSessionId);
}

static S32 mux_rtsp_handle_play(MuxRtspClient *pstClient, U32 u32CSeq) {
    pstClient->eState = MUX_RTSP_CLIENT_PLAYING;
    pstClient->bNeedParamInject = MPP_TRUE; /* Inject SPS/PPS before first frame */

    if (pstClient->pStream) {
        MUX_RTSP_LOGI("client playing stream '%s' (chn=%d)", pstClient->pStream->szPath, pstClient->pStream->s32ChnId);
    }

    return mux_rtsp_send_response(pstClient, NULL,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %u\r\n"
        "Session: %s\r\n"
        "Range: npt=0.000-\r\n",
        u32CSeq, pstClient->szSessionId);
}

static S32 mux_rtsp_handle_teardown(MuxRtspClient *pstClient, U32 u32CSeq) {
    S32 ret = mux_rtsp_send_response(pstClient, NULL,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %u\r\n"
        "Session: %s\r\n",
        u32CSeq, pstClient->szSessionId);
    pstClient->eState = MUX_RTSP_CLIENT_INIT;
    return ret;
}

static S32 mux_rtsp_handle_get_parameter(MuxRtspClient *pstClient, U32 u32CSeq) {
    return mux_rtsp_send_response(pstClient, NULL,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %u\r\n"
        "Session: %s\r\n",
        u32CSeq, pstClient->szSessionId);
}

static S32 mux_rtsp_handle_set_parameter(MuxRtspClient *pstClient, U32 u32CSeq) {
    return mux_rtsp_send_response(pstClient, NULL,
        "RTSP/1.0 200 OK\r\n"
        "CSeq: %u\r\n"
        "Session: %s\r\n",
        u32CSeq, pstClient->szSessionId);
}

static S32 mux_rtsp_process_request(MuxRtspClient *pstClient, const CHAR *pszReq) {
    CHAR method[32] = {0};
    U32 cseq;

    if (sscanf(pszReq, "%31s", method) != 1) {
        return -1;
    }
    cseq = mux_rtsp_get_cseq(pszReq);

    if (strcmp(method, "OPTIONS") == 0) {
        return mux_rtsp_handle_options(pstClient, cseq);
    }
    if (strcmp(method, "DESCRIBE") == 0) {
        return mux_rtsp_handle_describe(pstClient, pszReq, cseq);
    }
    if (strcmp(method, "SETUP") == 0) {
        return mux_rtsp_handle_setup(pstClient, pszReq, cseq);
    }
    if (strcmp(method, "PLAY") == 0) {
        return mux_rtsp_handle_play(pstClient, cseq);
    }
    if (strcmp(method, "TEARDOWN") == 0) {
        return mux_rtsp_handle_teardown(pstClient, cseq);
    }
    if (strcmp(method, "GET_PARAMETER") == 0) {
        return mux_rtsp_handle_get_parameter(pstClient, cseq);
    }
    if (strcmp(method, "SET_PARAMETER") == 0) {
        return mux_rtsp_handle_set_parameter(pstClient, cseq);
    }

    return mux_rtsp_send_response(pstClient, NULL, "RTSP/1.0 405 Method Not Allowed\r\nCSeq: %u\r\n", cseq);
}

/* Accept thread for global shared server */
static VOID *mux_rtsp_accept_thread(VOID *arg) {
    MuxGlobalRtspServer *pServer = &g_stGlobalServer;
    (void)arg;

    while (pServer->s32Running) {
        fd_set rfds;
        S32 maxfd = pServer->s32ListenFd;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(pServer->s32ListenFd, &rfds);

        pthread_mutex_lock(&pServer->lock);
        for (S32 i = 0; i < MUX_RTSP_MAX_CLIENTS; ++i) {
            if (pServer->astClients[i].s32Used && pServer->astClients[i].s32RtspFd >= 0) {
                FD_SET(pServer->astClients[i].s32RtspFd, &rfds);
                if (pServer->astClients[i].s32RtspFd > maxfd) {
                    maxfd = pServer->astClients[i].s32RtspFd;
                }
            }
        }
        pthread_mutex_unlock(&pServer->lock);

        tv.tv_sec = 1;
        tv.tv_usec = 0;
        if (select(maxfd + 1, &rfds, NULL, NULL, &tv) <= 0) {
            continue;
        }

        /* Accept new connections */
        if (FD_ISSET(pServer->s32ListenFd, &rfds)) {
            struct sockaddr_in peer;
            socklen_t len = sizeof(peer);
            S32 fd = accept(pServer->s32ListenFd, (struct sockaddr *)&peer, &len);
            if (fd >= 0) {
                pthread_mutex_lock(&pServer->lock);
                MuxRtspClient *pstClient = mux_rtsp_client_alloc();
                if (pstClient) {
                    pstClient->s32RtspFd = fd;
                    pstClient->stPeerAddr = peer;
                    pstClient->socklen = len;
                    pstClient->eState = MUX_RTSP_CLIENT_INIT;
                    MUX_RTSP_LOGI("client connected: %s:%d", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
                } else {
                    close(fd);
                    MUX_RTSP_LOGE("no free client slots");
                }
                pthread_mutex_unlock(&pServer->lock);
            }
        }

        /* Handle client requests */
        pthread_mutex_lock(&pServer->lock);
        for (S32 i = 0; i < MUX_RTSP_MAX_CLIENTS; ++i) {
            MuxRtspClient *pstClient = &pServer->astClients[i];
            if (!pstClient->s32Used || pstClient->s32RtspFd < 0) {
                continue;
            }
            if (!FD_ISSET(pstClient->s32RtspFd, &rfds)) {
                continue;
            }

            ssize_t rd = recv(pstClient->s32RtspFd, pstClient->szRecvBuf, sizeof(pstClient->szRecvBuf) - 1, 0);
            if (rd <= 0) {
                mux_rtsp_client_close(pstClient);
                continue;
            }
            pstClient->szRecvBuf[rd] = '\0';
            if (mux_rtsp_process_request(pstClient, pstClient->szRecvBuf) != 0) {
                mux_rtsp_client_close(pstClient);
            }
        }
        pthread_mutex_unlock(&pServer->lock);
    }

    return NULL;
}

/* ======================== Global Server Init/Deinit ======================== */

static S32 mux_rtsp_global_server_init(U16 u16Port) {
    MuxGlobalRtspServer *pServer = &g_stGlobalServer;
    struct sockaddr_in addr;
    S32 opt = 1;

    if (pServer->s32Inited) {
        pServer->u32RefCount++;
        return ERR_MUX_OK; /* Already initialized */
    }

    memset(pServer, 0, sizeof(*pServer));
    pthread_mutex_init(&pServer->lock, NULL);
    pServer->u16Port = u16Port;

    pServer->s32ListenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (pServer->s32ListenFd < 0) {
        pthread_mutex_destroy(&pServer->lock);
        return ERR_MUX_OPEN_FAIL;
    }

    setsockopt(pServer->s32ListenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(u16Port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(pServer->s32ListenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        MUX_RTSP_LOGE("bind port %u failed: %s", u16Port, strerror(errno));
        close(pServer->s32ListenFd);
        pthread_mutex_destroy(&pServer->lock);
        return ERR_MUX_OPEN_FAIL;
    }

    if (listen(pServer->s32ListenFd, MUX_RTSP_MAX_CLIENTS) < 0) {
        close(pServer->s32ListenFd);
        pthread_mutex_destroy(&pServer->lock);
        return ERR_MUX_OPEN_FAIL;
    }

    pServer->s32Running = 1;
    pServer->s32Inited = 1;
    pServer->u32RefCount = 1;

    if (pthread_create(&pServer->tidAccept, NULL, mux_rtsp_accept_thread, NULL) != 0) {
        close(pServer->s32ListenFd);
        pthread_mutex_destroy(&pServer->lock);
        pServer->s32Inited = 0;
        return ERR_MUX_OPEN_FAIL;
    }

    MUX_RTSP_LOGI("Shared RTSP server started on port %u", u16Port);
    return ERR_MUX_OK;
}

static VOID mux_rtsp_global_server_deinit(void) {
    MuxGlobalRtspServer *pServer = &g_stGlobalServer;

    if (!pServer->s32Inited) {
        return;
    }

    pServer->u32RefCount--;
    if (pServer->u32RefCount > 0) {
        return; /* Still referenced */
    }

    pServer->s32Running = 0;
    shutdown(pServer->s32ListenFd, SHUT_RDWR);
    close(pServer->s32ListenFd);
    pthread_join(pServer->tidAccept, NULL);

    pthread_mutex_lock(&pServer->lock);
    for (S32 i = 0; i < MUX_RTSP_MAX_CLIENTS; ++i) {
        mux_rtsp_client_close(&pServer->astClients[i]);
    }
    pthread_mutex_unlock(&pServer->lock);
    pthread_mutex_destroy(&pServer->lock);

    memset(pServer, 0, sizeof(*pServer));
    MUX_RTSP_LOGI("Shared RTSP server stopped");
}

/* ======================== Stream Registration ======================== */

S32 mux_rtsp_server_start(MuxChannel *pstChn) {
    MuxGlobalRtspServer *pServer = &g_stGlobalServer;
    MuxRtspStream *pStream = NULL;
    CHAR szHost[64];
    U16 u16Port;
    CHAR szPath[128];
    S32 ret;

    if (!pstChn) {
        return ERR_MUX_NULL_PTR;
    }

    /* Parse URL to get port and path */
    ret = mux_rtsp_parse_url(pstChn->stAttr.szUrl, szHost, sizeof(szHost), &u16Port, szPath, sizeof(szPath));
    if (ret != ERR_MUX_OK) {
        return ret;
    }

    /* Initialize global server if needed */
    ret = mux_rtsp_global_server_init(u16Port);
    if (ret != ERR_MUX_OK) {
        return ret;
    }

    /* Register stream with global server */
    pthread_mutex_lock(&pServer->lock);

    /* Check if already registered */
    pStream = mux_rtsp_find_stream_by_chn(pstChn->s32ChnId);
    if (pStream) {
        pthread_mutex_unlock(&pServer->lock);
        MUX_RTSP_LOGI("Stream chn=%d already registered at '%s'", pstChn->s32ChnId, pStream->szPath);
        return ERR_MUX_OK;
    }

    /* Find free slot */
    for (S32 i = 0; i < MUX_RTSP_MAX_STREAMS; i++) {
        if (!pServer->astStreams[i].s32Used) {
            pStream = &pServer->astStreams[i];
            break;
        }
    }

    if (!pStream) {
        pthread_mutex_unlock(&pServer->lock);
        MUX_RTSP_LOGE("No free stream slots");
        return ERR_MUX_BUSY;
    }

    /* Initialize stream */
    memset(pStream, 0, sizeof(*pStream));
    pStream->s32Used = 1;
    pStream->s32ChnId = pstChn->s32ChnId;
    snprintf(pStream->szPath, sizeof(pStream->szPath), "%s", szPath);
    pStream->eCodecType = pstChn->stAttr.stStreamAttr.eCodecType;
    pStream->u32Width = pstChn->stAttr.stStreamAttr.u32Width;
    pStream->u32Height = pstChn->stAttr.stStreamAttr.u32Height;
    pStream->u32Ssrc = (U32)rand();
    pStream->u16Seq = 1;
    pthread_mutex_init(&pStream->lock, NULL);
    pServer->u32StreamCount++;

    pthread_mutex_unlock(&pServer->lock);

    MUX_RTSP_LOGI("Stream registered: chn=%d path='%s' codec=%d", pstChn->s32ChnId, szPath, pStream->eCodecType);
    return ERR_MUX_OK;
}

VOID mux_rtsp_server_stop(MuxChannel *pstChn) {
    MuxGlobalRtspServer *pServer = &g_stGlobalServer;
    MuxRtspStream *pStream;

    if (!pstChn || !pServer->s32Inited) {
        return;
    }

    pthread_mutex_lock(&pServer->lock);

    pStream = mux_rtsp_find_stream_by_chn(pstChn->s32ChnId);
    if (pStream) {
        /* Disconnect all clients watching this stream */
        for (S32 i = 0; i < MUX_RTSP_MAX_CLIENTS; i++) {
            if (pServer->astClients[i].s32Used && pServer->astClients[i].pStream == pStream) {
                mux_rtsp_client_close(&pServer->astClients[i]);
            }
        }

        MUX_RTSP_LOGI("Stream unregistered: chn=%d path='%s'", pstChn->s32ChnId, pStream->szPath);
        pthread_mutex_destroy(&pStream->lock);
        memset(pStream, 0, sizeof(*pStream));
        pServer->u32StreamCount--;
    }

    pthread_mutex_unlock(&pServer->lock);

    /* Deinit global server if no more streams */
    mux_rtsp_global_server_deinit();
}

/* Cache SPS/PPS/VPS to stream's parameter sets */
static void mux_rtsp_cache_param_sets_to_stream(MuxRtspStream *pStream, const MuxPacket *pstPkt) {
    /* Reuse existing function signature but with stream */
    MuxRtspServer tmpServer;
    memset(&tmpServer, 0, sizeof(tmpServer));
    mux_rtsp_cache_param_sets(&tmpServer, pstPkt);

    /* Copy to stream */
    if (tmpServer.u32SpsLen > 0) {
        memcpy(pStream->au8Sps, tmpServer.au8Sps, tmpServer.u32SpsLen);
        pStream->u32SpsLen = tmpServer.u32SpsLen;
    }
    if (tmpServer.u32PpsLen > 0) {
        memcpy(pStream->au8Pps, tmpServer.au8Pps, tmpServer.u32PpsLen);
        pStream->u32PpsLen = tmpServer.u32PpsLen;
    }
    if (tmpServer.u32VpsLen > 0) {
        memcpy(pStream->au8Vps, tmpServer.au8Vps, tmpServer.u32VpsLen);
        pStream->u32VpsLen = tmpServer.u32VpsLen;
    }
}

/* Adapter: send using stream's SSRC/seq instead of server's */
static S32 mux_rtsp_send_h26x_annexb_stream(MuxRtspStream *pStream, MuxRtspClient *pstClient, const MuxPacket *pstPkt) {
    MuxRtspServer tmpServer;
    S32 ret;

    /* Build temp server with stream's SSRC/seq */
    memset(&tmpServer, 0, sizeof(tmpServer));
    tmpServer.u32Ssrc = pStream->u32Ssrc;
    tmpServer.u16Seq = pStream->u16Seq;

    ret = mux_rtsp_send_h26x_annexb(&tmpServer, pstClient, pstPkt);

    /* Update stream's sequence number */
    pStream->u16Seq = tmpServer.u16Seq;

    return ret;
}

S32 mux_rtsp_server_send_packet(MuxChannel *pstChn, const MuxPacket *pstPkt) {
    MuxGlobalRtspServer *pServer = &g_stGlobalServer;
    MuxRtspStream *pStream;
    S32 ret = ERR_MUX_OK;
    U32 u32ActiveCnt = 0;

    if (!pstChn || !pstPkt) {
        return ERR_MUX_NULL_PTR;
    }

    if (!pServer->s32Inited) {
        return ERR_MUX_NOT_INIT;
    }

    /* Find stream for this channel */
    pthread_mutex_lock(&pServer->lock);
    pStream = mux_rtsp_find_stream_by_chn(pstChn->s32ChnId);
    if (!pStream) {
        pthread_mutex_unlock(&pServer->lock);
        return ERR_MUX_INVALID_CHN;
    }

    /* Cache SPS/PPS/VPS on keyframe */
    if (pstPkt->bKeyFrame) {
        mux_rtsp_cache_param_sets_to_stream(pStream, pstPkt);
        if (pStream->u32SpsLen > 0 && pStream->u32PpsLen > 0) {
            /* Per-stream logging (only once per stream) */
            static U32 s_loggedMask = 0;
            if (!(s_loggedMask & (1u << pstChn->s32ChnId))) {
                MUX_RTSP_LOGI("chn=%d SPS/PPS cached: SPS=%u PPS=%u codec=%d", pstChn->s32ChnId, pStream->u32SpsLen,
                    pStream->u32PpsLen, pstPkt->eCodecType);
                s_loggedMask |= (1u << pstChn->s32ChnId);
            }
        }
    }

    /* Send to all clients subscribed to this stream */
    for (S32 i = 0; i < MUX_RTSP_MAX_CLIENTS; ++i) {
        MuxRtspClient *pstClient = &pServer->astClients[i];
        if (!pstClient->s32Used || pstClient->pStream != pStream) {
            continue;
        }
        if (pstClient->eState != MUX_RTSP_CLIENT_PLAYING) {
            continue;
        }

        /* Inject cached SPS/PPS for new clients before first frame */
        if (pstClient->bNeedParamInject && pStream->u32SpsLen > 0 && pStream->u32PpsLen > 0) {
            MuxPacket paramPkt;
            U8 paramBuf[MUX_SPS_PPS_MAX_SIZE * 3 + 16];
            U32 offset = 0;

            /* Build Annex-B format: 00 00 00 01 SPS 00 00 00 01 PPS */
            paramBuf[offset++] = 0x00;
            paramBuf[offset++] = 0x00;
            paramBuf[offset++] = 0x00;
            paramBuf[offset++] = 0x01;
            memcpy(paramBuf + offset, pStream->au8Sps, pStream->u32SpsLen);
            offset += pStream->u32SpsLen;

            paramBuf[offset++] = 0x00;
            paramBuf[offset++] = 0x00;
            paramBuf[offset++] = 0x00;
            paramBuf[offset++] = 0x01;
            memcpy(paramBuf + offset, pStream->au8Pps, pStream->u32PpsLen);
            offset += pStream->u32PpsLen;

            memset(&paramPkt, 0, sizeof(paramPkt));
            paramPkt.pu8Data = paramBuf;
            paramPkt.u32Size = offset;
            paramPkt.eCodecType = pstPkt->eCodecType;
            paramPkt.bKeyFrame = MPP_TRUE;
            paramPkt.u64PTS = pstPkt->u64PTS;

            mux_rtsp_send_h26x_annexb_stream(pStream, pstClient, &paramPkt);
            pstClient->bNeedParamInject = MPP_FALSE;
            MUX_RTSP_LOGI("injected SPS(%u)/PPS(%u) for client on stream '%s'", pStream->u32SpsLen, pStream->u32PpsLen,
                pStream->szPath);
        }

        if (mux_rtsp_send_h26x_annexb_stream(pStream, pstClient, pstPkt) != 0) {
            MUX_RTSP_LOGE("send failed to client on stream '%s', closing", pStream->szPath);
            mux_rtsp_client_close(pstClient);
            ret = ERR_MUX_OPEN_FAIL;
        } else {
            ++u32ActiveCnt;
        }
    }

    pStream->u64TotalPkts++;
    pStream->u64TotalBytes += pstPkt->u32Size;
    pthread_mutex_unlock(&pServer->lock);

    return ret;
}
