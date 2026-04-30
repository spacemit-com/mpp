/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_rtsp_server.c
 * @Date      :    2026-04-15
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Lightweight RTSP server for MUX module.
 *------------------------------------------------------------------------------
 */

#include "mux_rtsp_internal.h"

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

static S32 mux_rtsp_parse_url(const CHAR *pszUrl, CHAR *pszHost, U32 u32HostLen,
                              U16 *pu16Port, CHAR *pszPath, U32 u32PathLen)
{
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

static const CHAR *mux_rtsp_find_header(const CHAR *pszReq, const CHAR *pszKey)
{
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

static U32 mux_rtsp_get_cseq(const CHAR *pszReq)
{
    const CHAR *p = mux_rtsp_find_header(pszReq, "CSeq");
    if (!p) {
        return 1;
    }
    return (U32)atoi(p);
}

static VOID mux_rtsp_make_session_id(CHAR *pszBuf, U32 u32BufLen)
{
    unsigned int r = (unsigned int)rand();
    snprintf(pszBuf, u32BufLen, "%08x%08lx", r, (unsigned long)time(NULL));
}

static S32 mux_rtsp_send_response(MuxRtspClient *pstClient, const CHAR *pszBody, const CHAR *pszFmt, ...)
{
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

static S32 mux_rtsp_base64_encode(const U8 *pu8Src, U32 u32SrcLen, CHAR *pszDst, U32 u32DstLen)
{
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

static S32 mux_rtsp_make_sdp(const MuxChannel *pstChn, CHAR *pszSdp, U32 u32SdpLen)
{
    const CHAR *pszCodec;
    CHAR szFmtp[2048];
    const MuxRtspServer *pstServer;

    if (!pstChn || !pszSdp) {
        return ERR_MUX_NULL_PTR;
    }

    pstServer = &pstChn->stRtspServer;

    if (pstChn->stAttr.stStreamAttr.eCodecType == MUX_CODEC_H264) {
        pszCodec = "H264";
        if (pstServer->u32SpsLen > 0 && pstServer->u32PpsLen > 0) {
            CHAR szSpsB64[512], szPpsB64[512];
            mux_rtsp_base64_encode(pstServer->au8Sps, pstServer->u32SpsLen, szSpsB64, sizeof(szSpsB64));
            mux_rtsp_base64_encode(pstServer->au8Pps, pstServer->u32PpsLen, szPpsB64, sizeof(szPpsB64));
            snprintf(szFmtp, sizeof(szFmtp),
                     "a=fmtp:96 packetization-mode=1;profile-level-id=%02X%02X%02X;"
                     "sprop-parameter-sets=%s,%s\r\n",
                     pstServer->au8Sps[1], pstServer->au8Sps[2], pstServer->au8Sps[3],
                     szSpsB64, szPpsB64);
        } else {
            snprintf(szFmtp, sizeof(szFmtp), "a=fmtp:96 packetization-mode=1\r\n");
        }
    } else if (pstChn->stAttr.stStreamAttr.eCodecType == MUX_CODEC_H265) {
        pszCodec = "H265";
        if (pstServer->u32VpsLen > 0 && pstServer->u32SpsLen > 0 && pstServer->u32PpsLen > 0) {
            CHAR szVpsB64[512], szSpsB64[512], szPpsB64[512];
            mux_rtsp_base64_encode(pstServer->au8Vps, pstServer->u32VpsLen, szVpsB64, sizeof(szVpsB64));
            mux_rtsp_base64_encode(pstServer->au8Sps, pstServer->u32SpsLen, szSpsB64, sizeof(szSpsB64));
            mux_rtsp_base64_encode(pstServer->au8Pps, pstServer->u32PpsLen, szPpsB64, sizeof(szPpsB64));
            snprintf(szFmtp, sizeof(szFmtp),
                     "a=fmtp:96 sprop-vps=%s;sprop-sps=%s;sprop-pps=%s\r\n",
                     szVpsB64, szSpsB64, szPpsB64);
        } else {
            snprintf(szFmtp, sizeof(szFmtp), "a=fmtp:96\r\n");
        }
    } else {
        return ERR_MUX_OPEN_FAIL;
    }

    snprintf(pszSdp, u32SdpLen,
             "v=0\r\n"
             "o=- 0 0 IN IP4 %s\r\n"
             "s=SPACEMIT MUX RTSP Server\r\n"
             "t=0 0\r\n"
             "a=control:*\r\n"
             "m=video 0 RTP/AVP 96\r\n"
             "c=IN IP4 0.0.0.0\r\n"
             "a=rtpmap:96 %s/90000\r\n"
             "%s"
             "a=control:trackID=0\r\n",
             pstServer->szHost[0] ? pstServer->szHost : "0.0.0.0",
             pszCodec,
             szFmtp);
    return ERR_MUX_OK;
}

static VOID mux_rtsp_client_close(MuxRtspClient *pstClient)
{
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

static MuxRtspClient *mux_rtsp_client_alloc(MuxRtspServer *pstServer)
{
    for (S32 i = 0; i < MUX_RTSP_MAX_SESSIONS; ++i) {
        if (!pstServer->astClients[i].s32Used) {
            pstServer->astClients[i].s32Used = 1;
            pstServer->astClients[i].s32RtpSock = -1;
            pstServer->astClients[i].s32RtcpSock = -1;
            return &pstServer->astClients[i];
        }
    }
    return NULL;
}

static S32 mux_rtsp_handle_options(MuxRtspClient *pstClient, U32 u32CSeq)
{
    return mux_rtsp_send_response(pstClient, NULL,
                                  "RTSP/1.0 200 OK\r\n"
                                  "CSeq: %u\r\n"
                                  "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER, SET_PARAMETER\r\n",
                                  u32CSeq);
}

static S32 mux_rtsp_handle_describe(MuxChannel *pstChn, MuxRtspClient *pstClient, U32 u32CSeq)
{
    CHAR szSdp[1024];

    if (mux_rtsp_make_sdp(pstChn, szSdp, sizeof(szSdp)) != ERR_MUX_OK) {
        return mux_rtsp_send_response(pstClient, NULL,
                                      "RTSP/1.0 500 Internal Server Error\r\nCSeq: %u\r\n", u32CSeq);
    }

    return mux_rtsp_send_response(pstClient, szSdp,
                                  "RTSP/1.0 200 OK\r\n"
                                  "CSeq: %u\r\n"
                                  "Content-Type: application/sdp\r\n",
                                  u32CSeq);
}

static S32 mux_rtsp_parse_transport(MuxRtspClient *pstClient, const CHAR *pszTransport)
{
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

static S32 mux_rtsp_open_udp(MuxRtspClient *pstClient)
{
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

static S32 mux_rtsp_handle_setup(MuxRtspClient *pstClient, const CHAR *pszReq, U32 u32CSeq)
{
    const CHAR *pszTransport = mux_rtsp_find_header(pszReq, "Transport");

    if (!pszTransport || mux_rtsp_parse_transport(pstClient, pszTransport) != ERR_MUX_OK) {
        return mux_rtsp_send_response(pstClient, NULL,
                                      "RTSP/1.0 461 Unsupported Transport\r\nCSeq: %u\r\n", u32CSeq);
    }

    if (!pstClient->bInterleaved) {
        if (mux_rtsp_open_udp(pstClient) != ERR_MUX_OK) {
            return mux_rtsp_send_response(pstClient, NULL,
                                          "RTSP/1.0 500 Internal Server Error\r\nCSeq: %u\r\n", u32CSeq);
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
                                      u32CSeq, pstClient->u8RtpChannel, pstClient->u8RtcpChannel,
                                      pstClient->szSessionId);
    }

    return mux_rtsp_send_response(pstClient, NULL,
                                  "RTSP/1.0 200 OK\r\n"
                                  "CSeq: %u\r\n"
                                  "Transport: RTP/AVP;unicast;client_port=%u-%u;server_port=0-0\r\n"
                                  "Session: %s\r\n",
                                  u32CSeq, pstClient->u16ClientRtpPort, pstClient->u16ClientRtcpPort,
                                  pstClient->szSessionId);
}

static S32 mux_rtsp_handle_play(MuxRtspClient *pstClient, U32 u32CSeq)
{
    pstClient->eState = MUX_RTSP_CLIENT_PLAYING;
    return mux_rtsp_send_response(pstClient, NULL,
                                  "RTSP/1.0 200 OK\r\n"
                                  "CSeq: %u\r\n"
                                  "Session: %s\r\n"
                                  "Range: npt=0.000-\r\n",
                                  u32CSeq, pstClient->szSessionId);
}

static S32 mux_rtsp_handle_teardown(MuxRtspClient *pstClient, U32 u32CSeq)
{
    S32 ret = mux_rtsp_send_response(pstClient, NULL,
                                     "RTSP/1.0 200 OK\r\n"
                                     "CSeq: %u\r\n"
                                     "Session: %s\r\n",
                                     u32CSeq, pstClient->szSessionId);
    pstClient->eState = MUX_RTSP_CLIENT_INIT;
    return ret;
}

static S32 mux_rtsp_handle_get_parameter(MuxRtspClient *pstClient, U32 u32CSeq)
{
    return mux_rtsp_send_response(pstClient, NULL,
                                  "RTSP/1.0 200 OK\r\n"
                                  "CSeq: %u\r\n"
                                  "Session: %s\r\n",
                                  u32CSeq, pstClient->szSessionId);
}

static S32 mux_rtsp_handle_set_parameter(MuxRtspClient *pstClient, U32 u32CSeq)
{
    return mux_rtsp_send_response(pstClient, NULL,
                                  "RTSP/1.0 200 OK\r\n"
                                  "CSeq: %u\r\n"
                                  "Session: %s\r\n",
                                  u32CSeq, pstClient->szSessionId);
}

static S32 mux_rtsp_process_request(MuxChannel *pstChn, MuxRtspClient *pstClient, const CHAR *pszReq)
{
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
        return mux_rtsp_handle_describe(pstChn, pstClient, cseq);
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

    return mux_rtsp_send_response(pstClient, NULL,
                                  "RTSP/1.0 405 Method Not Allowed\r\nCSeq: %u\r\n", cseq);
}

static VOID *mux_rtsp_accept_thread(VOID *arg)
{
    MuxChannel *pstChn = (MuxChannel *)arg;
    MuxRtspServer *pstServer = &pstChn->stRtspServer;

    while (pstServer->s32Running) {
        fd_set rfds;
        S32 maxfd = pstServer->s32ListenFd;
        struct timeval tv;

        FD_ZERO(&rfds);
        FD_SET(pstServer->s32ListenFd, &rfds);

        pthread_mutex_lock(&pstServer->lock);
        for (S32 i = 0; i < MUX_RTSP_MAX_SESSIONS; ++i) {
            if (pstServer->astClients[i].s32Used) {
                FD_SET(pstServer->astClients[i].s32RtspFd, &rfds);
                if (pstServer->astClients[i].s32RtspFd > maxfd) {
                    maxfd = pstServer->astClients[i].s32RtspFd;
                }
            }
        }
        pthread_mutex_unlock(&pstServer->lock);

        tv.tv_sec = 1;
        tv.tv_usec = 0;
        if (select(maxfd + 1, &rfds, NULL, NULL, &tv) <= 0) {
            continue;
        }

        if (FD_ISSET(pstServer->s32ListenFd, &rfds)) {
            struct sockaddr_in peer;
            socklen_t len = sizeof(peer);
            S32 fd = accept(pstServer->s32ListenFd, (struct sockaddr *)&peer, &len);
            if (fd >= 0) {
                pthread_mutex_lock(&pstServer->lock);
                MuxRtspClient *pstClient = mux_rtsp_client_alloc(pstServer);
                if (pstClient) {
                    pstClient->s32RtspFd = fd;
                    pstClient->stPeerAddr = peer;
                    pstClient->socklen = len;
                    pstClient->eState = MUX_RTSP_CLIENT_INIT;
                    MUX_RTSP_LOGI("client connected: %s:%d", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
                } else {
                    close(fd);
                }
                pthread_mutex_unlock(&pstServer->lock);
            }
        }

        pthread_mutex_lock(&pstServer->lock);
        for (S32 i = 0; i < MUX_RTSP_MAX_SESSIONS; ++i) {
            MuxRtspClient *pstClient = &pstServer->astClients[i];
            if (!pstClient->s32Used) {
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
            if (mux_rtsp_process_request(pstChn, pstClient, pstClient->szRecvBuf) != 0) {
                mux_rtsp_client_close(pstClient);
            }
        }
        pthread_mutex_unlock(&pstServer->lock);
    }

    return NULL;
}

S32 mux_rtsp_server_start(MuxChannel *pstChn)
{
    MuxRtspServer *pstServer;
    struct sockaddr_in addr;
    S32 opt = 1;
    S32 ret;

    if (!pstChn) {
        return ERR_MUX_NULL_PTR;
    }

    pstServer = &pstChn->stRtspServer;
    memset(pstServer, 0, sizeof(*pstServer));
    pthread_mutex_init(&pstServer->lock, NULL);
    pstServer->u32Ssrc = (U32)rand();
    pstServer->u16Seq = 1;

    ret = mux_rtsp_parse_url(pstChn->stAttr.szUrl, pstServer->szHost, sizeof(pstServer->szHost),
                             &pstServer->u16ListenPort, pstServer->szPath, sizeof(pstServer->szPath));
    if (ret != ERR_MUX_OK) {
        pthread_mutex_destroy(&pstServer->lock);
        return ret;
    }

    pstServer->s32ListenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (pstServer->s32ListenFd < 0) {
        pthread_mutex_destroy(&pstServer->lock);
        return ERR_MUX_OPEN_FAIL;
    }

    setsockopt(pstServer->s32ListenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(pstServer->u16ListenPort);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(pstServer->s32ListenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(pstServer->s32ListenFd);
        pthread_mutex_destroy(&pstServer->lock);
        return ERR_MUX_OPEN_FAIL;
    }
    if (listen(pstServer->s32ListenFd, MUX_RTSP_MAX_SESSIONS) < 0) {
        close(pstServer->s32ListenFd);
        pthread_mutex_destroy(&pstServer->lock);
        return ERR_MUX_OPEN_FAIL;
    }

    pstServer->s32Running = 1;
    if (pthread_create(&pstServer->tidAccept, NULL, mux_rtsp_accept_thread, pstChn) != 0) {
        close(pstServer->s32ListenFd);
        pthread_mutex_destroy(&pstServer->lock);
        return ERR_MUX_OPEN_FAIL;
    }

    MUX_RTSP_LOGI("RTSP server started at rtsp://0.0.0.0:%u%s", pstServer->u16ListenPort, pstServer->szPath);
    return ERR_MUX_OK;
}

VOID mux_rtsp_server_stop(MuxChannel *pstChn)
{
    MuxRtspServer *pstServer;

    if (!pstChn) {
        return;
    }

    pstServer = &pstChn->stRtspServer;
    if (!pstServer->s32Running) {
        return;
    }

    pstServer->s32Running = 0;
    shutdown(pstServer->s32ListenFd, SHUT_RDWR);
    close(pstServer->s32ListenFd);
    pthread_join(pstServer->tidAccept, NULL);

    pthread_mutex_lock(&pstServer->lock);
    for (S32 i = 0; i < MUX_RTSP_MAX_SESSIONS; ++i) {
        mux_rtsp_client_close(&pstServer->astClients[i]);
    }
    pthread_mutex_unlock(&pstServer->lock);
    pthread_mutex_destroy(&pstServer->lock);
    memset(pstServer, 0, sizeof(*pstServer));
}

S32 mux_rtsp_server_send_packet(MuxChannel *pstChn, const MuxPacket *pstPkt)
{
    MuxRtspServer *pstServer;
    S32 ret = ERR_MUX_OK;
    U32 u32ActiveCnt = 0;

    if (!pstChn || !pstPkt) {
        return ERR_MUX_NULL_PTR;
    }

    pstServer = &pstChn->stRtspServer;

    /* cache SPS/PPS/VPS on every keyframe for SDP and late-joiner injection */
    if (pstPkt->bKeyFrame) {
        mux_rtsp_cache_param_sets(pstServer, pstPkt);
    }

    pthread_mutex_lock(&pstServer->lock);
    for (S32 i = 0; i < MUX_RTSP_MAX_SESSIONS; ++i) {
        MuxRtspClient *pstClient = &pstServer->astClients[i];
        if (!pstClient->s32Used || pstClient->eState != MUX_RTSP_CLIENT_PLAYING) {
            continue;
        }
        if (mux_rtsp_send_h26x_annexb(pstServer, pstClient, pstPkt) != 0) {
            MUX_RTSP_LOGE("send failed to client %d, closing", i);
            mux_rtsp_client_close(pstClient);
            ret = ERR_MUX_OPEN_FAIL;
        } else {
            ++u32ActiveCnt;
        }
    }
    pstServer->u32ActiveClients = u32ActiveCnt;
    pstServer->u64TotalPkts++;
    pstServer->u64TotalBytes += pstPkt->u32Size;
    pthread_mutex_unlock(&pstServer->lock);
    return ret;
}
