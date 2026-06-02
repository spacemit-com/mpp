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

/*
 * Parse a port string (the part after ':' up to pEnd) into 1..65535.
 * atoi() would silently truncate an out-of-range value into the 16-bit
 * field (e.g. "70000" -> 4464) or accept non-numeric junk, so validate
 * strictly and report -1 on any malformed or out-of-range port.
 */
static S32 Url_ParsePort(const CHAR *pStart, const CHAR *pEnd) {
    S32 s32Port = 0;

    if (pStart >= pEnd) {
        return -1; /* empty port, e.g. "host:/path" */
    }
    for (const CHAR *q = pStart; q < pEnd; q++) {
        if (!isdigit((unsigned char)*q)) {
            return -1; /* non-numeric port */
        }
        s32Port = s32Port * 10 + (*q - '0');
        if (s32Port > 65535) {
            return -1; /* out of range */
        }
    }
    if (s32Port == 0) {
        return -1; /* port 0 is not a valid TCP/UDP port */
    }
    return s32Port;
}

S32 Url_Parse(const CHAR *pszUrl, UrlInfo *pstInfo) {
    const CHAR *p;
    const CHAR *pEnd;
    size_t len;

    /* Reject NULL inputs before any dereference; both pointers are used
     * immediately below (memset/strstr) and would otherwise crash. */
    if (!pszUrl || !pstInfo) {
        return -1;
    }

    p = pszUrl;
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

    /* Determine where the authority component ends. userinfo ("user:pass@")
     * may only appear inside the authority, so the '@' search must be bounded
     * by the first '/', '?' or '#'; otherwise a '@' in the path (e.g.
     * "rtsp://host/path@x") would be mistaken for userinfo. */
    const CHAR *pAuthEnd = p + strcspn(p, "/?#");

    /* Parse user:pass@ if present (only within the authority). */
    pEnd = memchr(p, '@', (size_t)(pAuthEnd - p));
    if (pEnd) {
        const CHAR *pColon = memchr(p, ':', (size_t)(pEnd - p));
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

    /* Parse host:port (host segment ends at the first '/', '?' or '#'). */
    pEnd = p + strcspn(p, "/?#");

    if (*p == '[') {
        /*
         * IPv6 literal: per RFC 3986 the address is wrapped in brackets,
         * e.g. "rtsp://[::1]:554/live". The host is everything between '['
         * and ']' (brackets stripped); an optional ":port" may follow ']'.
         * The colons inside the address must NOT be treated as the port
         * separator, which is exactly why brackets are required.
         */
        const CHAR *pClose = memchr(p, ']', (size_t)(pEnd - p));
        if (!pClose) {
            return -1; /* malformed: '[' without matching ']' */
        }
        len = pClose - (p + 1);
        if (len >= URL_HOST_LEN)
            len = URL_HOST_LEN - 1;
        strncpy(pstInfo->szHost, p + 1, len);

        if (pClose + 1 < pEnd && pClose[1] == ':') {
            S32 s32Port = Url_ParsePort(pClose + 2, pEnd);
            if (s32Port < 0)
                return -1; /* invalid/out-of-range port */
            pstInfo->u16Port = (U16)s32Port;
        } else {
            pstInfo->u16Port = Url_DefaultPort(pstInfo->szScheme);
        }
    } else {
        const CHAR *pColon = memchr(p, ':', (size_t)(pEnd - p));
        if (pColon) {
            len = pColon - p;
            if (len >= URL_HOST_LEN)
                len = URL_HOST_LEN - 1;
            strncpy(pstInfo->szHost, p, len);
            S32 s32Port = Url_ParsePort(pColon + 1, pEnd);
            if (s32Port < 0)
                return -1; /* invalid/out-of-range port */
            pstInfo->u16Port = (U16)s32Port;
        } else {
            len = pEnd - p;
            if (len >= URL_HOST_LEN)
                len = URL_HOST_LEN - 1;
            strncpy(pstInfo->szHost, p, len);
            pstInfo->u16Port = Url_DefaultPort(pstInfo->szScheme);
        }
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
    /* Any URL carrying a network scheme ("scheme://") is not a local file. */
    if (strstr(pszUrl, "://") != NULL)
        return MPP_FALSE;
    /* No scheme present: treat it as a local path. This covers absolute
     * paths ("/a/b.mp4"), explicit relative paths ("./a.mp4", "../a.mp4")
     * and plain relative paths ("a.mp4", "dir/a.mp4"). */
    return MPP_TRUE;
}

const CHAR *Url_GetExtension(const CHAR *pszUrl) {
    /* Locate the extension within the path only, i.e. before any query ('?')
     * or fragment ('#'); otherwise "video.m3u8?token=a.b" would pick the dot
     * in the query and return "b". The returned pointer still points into the
     * original string, so a trailing query/fragment (if any) remains attached;
     * callers must compare with a query-aware bound (see ext_matches). */
    size_t pathLen = strcspn(pszUrl, "?#");
    const CHAR *pDot = NULL;
    size_t i;
    for (i = 0; i < pathLen; i++) {
        if (pszUrl[i] == '.')
            pDot = pszUrl + i;
    }
    if (pDot) {
        return pDot + 1;
    }
    return "";
}
