/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    url_parser.h
 * @Brief     :    URL parsing utilities.
 *------------------------------------------------------------------------------
 */

#ifndef URL_PARSER_H
#define URL_PARSER_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define URL_PATH_LEN 512
#define URL_SCHEME_LEN 16
#define URL_HOST_LEN 128
#define URL_USER_LEN 256
#define URL_QUERY_LEN 64

typedef struct _UrlInfo {
    CHAR szScheme[URL_SCHEME_LEN]; /* rtsp, rtmp, http, file */
    CHAR szHost[URL_HOST_LEN];
    U16 u16Port;
    CHAR szPath[URL_PATH_LEN];
    CHAR szUser[URL_USER_LEN];
    CHAR szPass[URL_USER_LEN];
} UrlInfo;

/**
 * @brief  Parse URL into components
 * @return 0 on success, -1 on error
 */
S32 Url_Parse(const CHAR *pszUrl, UrlInfo *pstInfo);

/**
 * @brief  Get default port for scheme
 */
U16 Url_DefaultPort(const CHAR *pszScheme);

/**
 * @brief  Check if URL is a local file
 */
BOOL Url_IsFile(const CHAR *pszUrl);

/**
 * @brief  Get file extension from URL/path (returns the raw suffix as-is,
 *         without any case conversion). Returns an empty string when there
 *         is no extension.
 */
const CHAR *Url_GetExtension(const CHAR *pszUrl);

#ifdef __cplusplus
}
#endif

#endif /* __URL_PARSER_H__ */
