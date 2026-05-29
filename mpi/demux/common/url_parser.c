/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "url_parser.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

S32 Url_Parse(const CHAR *pszUrl, UrlInfo *pstInfo) {
    const CHAR *p = pszUrl;
    const CHAR *pEnd;
    size_t len;

    memset(pstInfo, 0, sizeof(*pstInfo));

    /* Parse scheme */
    pEnd = strstr(p, "://");
    if (pEnd) {
        len = pEnd - p;
        if (len >= URL_SCHEME_LEN)
            len = URL_SCHEME_LEN - 1;
        strncpy(pstInfo->szScheme, p, len);
        pstInfo->szScheme[len] = '\0';
        p = pEnd + 3;
    } else {
        /* Assume file path */
        strncpy(pstInfo->szScheme, "file", URL_SCHEME_LEN);
        strncpy(pstInfo->szPath, pszUrl, URL_PATH_LEN - 1);
        return 0;
    }

    /* Parse user:pass@ if present */
    pEnd = strchr(p, '@');
    if (pEnd) {
        const CHAR *pColon = strchr(p, ':');
        if (pColon && pColon < pEnd) {
            len = pColon - p;
            if (len >= URL_USER_LEN)
                len = URL_USER_LEN - 1;
            strncpy(pstInfo->szUser, p, len);

            len = pEnd - pColon - 1;
            if (len >= URL_USER_LEN)
                len = URL_USER_LEN - 1;
            strncpy(pstInfo->szPass, pColon + 1, len);
        } else {
            len = pEnd - p;
            if (len >= URL_USER_LEN)
                len = URL_USER_LEN - 1;
            strncpy(pstInfo->szUser, p, len);
        }
        p = pEnd + 1;
    }

    /* Parse host:port */
    pEnd = strchr(p, '/');
    if (!pEnd) {
        pEnd = p + strlen(p);
    }

    const CHAR *pColon = strchr(p, ':');
    if (pColon && pColon < pEnd) {
        len = pColon - p;
        if (len >= URL_HOST_LEN)
            len = URL_HOST_LEN - 1;
        strncpy(pstInfo->szHost, p, len);
        pstInfo->u16Port = (U16)atoi(pColon + 1);
    } else {
        len = pEnd - p;
        if (len >= URL_HOST_LEN)
            len = URL_HOST_LEN - 1;
        strncpy(pstInfo->szHost, p, len);
        pstInfo->u16Port = Url_DefaultPort(pstInfo->szScheme);
    }

    /* Parse path */
    if (*pEnd == '/') {
        strncpy(pstInfo->szPath, pEnd, URL_PATH_LEN - 1);
    } else {
        pstInfo->szPath[0] = '/';
    }

    return 0;
}

U16 Url_DefaultPort(const CHAR *pszScheme) {
    if (strcasecmp(pszScheme, "rtsp") == 0)
        return 554;
    if (strcasecmp(pszScheme, "rtmp") == 0)
        return 1935;
    if (strcasecmp(pszScheme, "http") == 0)
        return 80;
    if (strcasecmp(pszScheme, "https") == 0)
        return 443;
    return 0;
}

BOOL Url_IsFile(const CHAR *pszUrl) {
    if (strncasecmp(pszUrl, "file://", 7) == 0)
        return MPP_TRUE;
    if (pszUrl[0] == '/')
        return MPP_TRUE;
    if (pszUrl[0] == '.')
        return MPP_TRUE;
    return MPP_FALSE;
}

const CHAR *Url_GetExtension(const CHAR *pszUrl) {
    const CHAR *pDot = strrchr(pszUrl, '.');
    if (pDot) {
        return pDot + 1;
    }
    return "";
}
