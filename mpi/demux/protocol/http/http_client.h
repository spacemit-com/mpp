/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    http_client.h
 * @Brief     :    Lightweight HTTP/1.1 client for HLS/HTTP streaming.
 *------------------------------------------------------------------------------
 */

#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _HttpResponse {
    S32 s32StatusCode;
    U8 *pu8Body;
    U32 u32BodyLen;
    char szContentType[64];
    S64 s64ContentLength;
    BOOL bChunked;
} HttpResponse;

typedef struct _HttpClient HttpClient;

/**
 * @brief  Create HTTP client
 */
HttpClient *HttpClient_Create(void);

/**
 * @brief  Destroy HTTP client
 */
void HttpClient_Destroy(HttpClient *pClient);

/**
 * @brief  Set connection timeout
 */
void HttpClient_SetTimeout(HttpClient *pClient, U32 u32TimeoutMs);

/**
 * @brief  HTTP GET request
 * @return 0 on success
 */
S32 HttpClient_Get(HttpClient *pClient, const char *pszUrl, HttpResponse *pResp);

/**
 * @brief  HTTP GET with range request (for partial content)
 */
S32 HttpClient_GetRange(HttpClient *pClient, const char *pszUrl, S64 s64Start, S64 s64End, HttpResponse *pResp);

/**
 * @brief  Free response body
 */
void HttpClient_FreeResponse(HttpResponse *pResp);

/**
 * @brief  Get last error string
 */
const char *HttpClient_GetLastError(HttpClient *pClient);

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_CLIENT_H__ */
