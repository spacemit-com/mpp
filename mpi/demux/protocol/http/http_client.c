/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/socket_utils.h"
#include "common/url_parser.h"

#define HTTP_RECV_BUF_SIZE (64 * 1024)
#define HTTP_HEADER_MAX 4096

struct _HttpClient {
    S32 s32Socket;
    U32 u32TimeoutMs;
    char szLastError[256];
    char szHost[256];
    U16 u16Port;
};

HttpClient *HttpClient_Create(void) {
    HttpClient *pClient = (HttpClient *)calloc(1, sizeof(HttpClient));
    if (!pClient)
        return NULL;

    pClient->s32Socket = -1;
    pClient->u32TimeoutMs = 10000; /* 10s default */

    return pClient;
}

void HttpClient_Destroy(HttpClient *pClient) {
    if (!pClient)
        return;
    if (pClient->s32Socket >= 0) {
        Socket_Close(pClient->s32Socket);
    }
    free(pClient);
}

void HttpClient_SetTimeout(HttpClient *pClient, U32 u32TimeoutMs) {
    if (pClient) {
        pClient->u32TimeoutMs = u32TimeoutMs;
    }
}

static S32 ParseStatusLine(const char *pLine, S32 *pStatusCode) {
    /* HTTP/1.1 200 OK */
    if (strncmp(pLine, "HTTP/", 5) != 0)
        return -1;

    const char *p = strchr(pLine, ' ');
    if (!p)
        return -1;

    *pStatusCode = atoi(p + 1);
    return 0;
}

static S32 ParseHeader(const char *pLine, const char *pName, char *pValue, U32 u32MaxLen) {
    size_t nameLen = strlen(pName);
    if (strncasecmp(pLine, pName, nameLen) != 0)
        return -1;
    if (pLine[nameLen] != ':')
        return -1;

    const char *p = &pLine[nameLen + 1];
    while (*p == ' ')
        p++;

    strncpy(pValue, p, u32MaxLen - 1);
    pValue[u32MaxLen - 1] = '\0';

    /* Remove trailing \r\n */
    size_t len = strlen(pValue);
    while (len > 0 && (pValue[len - 1] == '\r' || pValue[len - 1] == '\n')) {
        pValue[--len] = '\0';
    }

    return 0;
}

S32 HttpClient_Get(HttpClient *pClient, const char *pszUrl, HttpResponse *pResp) {
    if (!pClient || !pszUrl || !pResp)
        return -1;

    memset(pResp, 0, sizeof(HttpResponse));

    /* Parse URL */
    UrlInfo url;
    if (Url_Parse(pszUrl, &url) != 0) {
        snprintf(pClient->szLastError, sizeof(pClient->szLastError), "Invalid URL");
        return -1;
    }

    /* Connect */
    pClient->s32Socket = Socket_TcpConnect(url.szHost, url.u16Port, pClient->u32TimeoutMs);
    if (pClient->s32Socket < 0) {
        snprintf(pClient->szLastError, sizeof(pClient->szLastError), "Connect failed");
        return -1;
    }

    /* Send request */
    char request[1024];
    int reqLen = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "User-Agent: mpp_demux/1.0\r\n"
        "\r\n",
        url.szPath[0] ? url.szPath : "/", url.szHost);

    if (Socket_SendAll(pClient->s32Socket, (U8 *)request, reqLen) != reqLen) {
        Socket_Close(pClient->s32Socket);
        pClient->s32Socket = -1;
        snprintf(pClient->szLastError, sizeof(pClient->szLastError), "Send failed");
        return -1;
    }

    /* Receive response */
    U8 *pBuf = (U8 *)malloc(HTTP_RECV_BUF_SIZE);
    if (!pBuf) {
        Socket_Close(pClient->s32Socket);
        pClient->s32Socket = -1;
        return -1;
    }

    U32 totalRecv = 0;
    U32 headerEnd = 0;
    BOOL headerParsed = MPP_FALSE;

    while (totalRecv < HTTP_RECV_BUF_SIZE - 1) {
        S32 n = Socket_RecvTimeout(
            pClient->s32Socket, pBuf + totalRecv, HTTP_RECV_BUF_SIZE - 1 - totalRecv, pClient->u32TimeoutMs);
        if (n <= 0)
            break;
        totalRecv += n;

        if (!headerParsed) {
            pBuf[totalRecv] = '\0';
            char *headerEndPtr = strstr((char *)pBuf, "\r\n\r\n");
            if (headerEndPtr) {
                headerEnd = (U32)(headerEndPtr - (char *)pBuf) + 4;
                headerParsed = MPP_TRUE;

                /* Parse headers */
                char *line = (char *)pBuf;
                char *next;
                int lineNum = 0;

                while (line && line < (char *)pBuf + headerEnd) {
                    next = strstr(line, "\r\n");
                    if (next)
                        *next = '\0';

                    if (lineNum == 0) {
                        ParseStatusLine(line, &pResp->s32StatusCode);
                    } else {
                        char value[256];
                        if (ParseHeader(line, "Content-Type", value, sizeof(value)) == 0) {
                            strncpy(pResp->szContentType, value, sizeof(pResp->szContentType) - 1);
                        } else if (ParseHeader(line, "Content-Length", value, sizeof(value)) == 0) {
                            pResp->s64ContentLength = atoll(value);
                        } else if (ParseHeader(line, "Transfer-Encoding", value, sizeof(value)) == 0) {
                            pResp->bChunked = (strstr(value, "chunked") != NULL);
                        }
                    }

                    line = next ? next + 2 : NULL;
                    lineNum++;
                }
            }
        }
    }

    Socket_Close(pClient->s32Socket);
    pClient->s32Socket = -1;

    if (!headerParsed) {
        free(pBuf);
        snprintf(pClient->szLastError, sizeof(pClient->szLastError), "No response");
        return -1;
    }

    /* Copy body */
    pResp->u32BodyLen = totalRecv - headerEnd;
    if (pResp->u32BodyLen > 0) {
        pResp->pu8Body = (U8 *)malloc(pResp->u32BodyLen + 1);
        if (pResp->pu8Body) {
            memcpy(pResp->pu8Body, pBuf + headerEnd, pResp->u32BodyLen);
            pResp->pu8Body[pResp->u32BodyLen] = '\0';
        }
    }

    free(pBuf);
    return 0;
}

S32 HttpClient_GetRange(HttpClient *pClient, const char *pszUrl, S64 s64Start, S64 s64End, HttpResponse *pResp) {
    /* TODO: Implement range request */
    (void)s64Start;
    (void)s64End;
    return HttpClient_Get(pClient, pszUrl, pResp);
}

void HttpClient_FreeResponse(HttpResponse *pResp) {
    if (!pResp)
        return;
    if (pResp->pu8Body) {
        free(pResp->pu8Body);
        pResp->pu8Body = NULL;
    }
    pResp->u32BodyLen = 0;
}

const char *HttpClient_GetLastError(HttpClient *pClient) { return pClient ? pClient->szLastError : "NULL client"; }
