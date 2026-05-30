/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    rtsp_parser.c
 * @Brief     :    RTSP protocol message parser implementation.
 *------------------------------------------------------------------------------
 */

#include "rtsp_parser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Case-insensitive string compare */
static int strcasecmp_local(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2)
            return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

/* Case-insensitive strncmp */
static int strncasecmp_local(const char *s1, const char *s2, size_t n) {
    while (n > 0 && *s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2)
            return c1 - c2;
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

/* Skip whitespace */
static const char *skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t'))
        p++;
    return p;
}

/* Skip to end of line */
static const char *skip_line(const char *p) {
    while (*p && *p != '\r' && *p != '\n')
        p++;
    if (*p == '\r')
        p++;
    if (*p == '\n')
        p++;
    return p;
}

/* Find end of headers (double CRLF) */
static const char *find_header_end(const char *p, U32 len) {
    const char *end = p + len;

    while (p < end - 3) {
        if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') {
            return p + 4; /* Point to start of body */
        }
        p++;
    }
    return NULL;
}

BOOL Rtsp_IsResponseComplete(const char *pszData, U32 u32Len) {
    const char *bodyStart = find_header_end(pszData, u32Len);
    if (!bodyStart) {
        return MPP_FALSE; /* Headers not complete */
    }

    /* Check Content-Length */
    const char *cl = strstr(pszData, "Content-Length:");
    if (!cl) {
        cl = strstr(pszData, "content-length:");
    }

    if (cl && cl < bodyStart) {
        U32 contentLen = (U32)atoi(cl + 15);
        U32 bodyLen = u32Len - (U32)(bodyStart - pszData);
        return bodyLen >= contentLen;
    }

    return MPP_TRUE; /* No body expected */
}

S32 Rtsp_ParseResponse(const char *pszData, U32 u32Len, RtspResponse *pResponse) {
    const char *p;
    const char *bodyStart;

    if (!pszData || !pResponse) {
        return -1;
    }

    memset(pResponse, 0, sizeof(RtspResponse));

    /* Check if response is complete */
    bodyStart = find_header_end(pszData, u32Len);
    if (!bodyStart) {
        return 1; /* Need more data */
    }

    p = pszData;

    /* Parse status line: RTSP/1.0 200 OK */
    if (strncmp(p, "RTSP/", 5) != 0) {
        return -1; /* Not RTSP response */
    }

    /* Version */
    const char *verStart = p;
    while (*p && *p != ' ')
        p++;
    size_t verLen = p - verStart;
    if (verLen >= sizeof(pResponse->szVersion)) {
        verLen = sizeof(pResponse->szVersion) - 1;
    }
    memcpy(pResponse->szVersion, verStart, verLen);
    pResponse->szVersion[verLen] = '\0';

    p = skip_ws(p);

    /* Status code */
    pResponse->u32StatusCode = (U32)atoi(p);
    while (*p && isdigit((unsigned char)*p))
        p++;

    p = skip_ws(p);

    /* Reason phrase */
    const char *reasonStart = p;
    while (*p && *p != '\r' && *p != '\n')
        p++;
    size_t reasonLen = p - reasonStart;
    if (reasonLen >= sizeof(pResponse->szReasonPhrase)) {
        reasonLen = sizeof(pResponse->szReasonPhrase) - 1;
    }
    memcpy(pResponse->szReasonPhrase, reasonStart, reasonLen);
    pResponse->szReasonPhrase[reasonLen] = '\0';

    p = skip_line(p);

    /* Parse headers */
    while (p < bodyStart && *p && *p != '\r') {
        if (pResponse->u32HeaderCount >= RTSP_MAX_HEADERS) {
            break;
        }

        RtspHeader *hdr = &pResponse->astHeaders[pResponse->u32HeaderCount];

        /* Header name */
        const char *nameStart = p;
        while (*p && *p != ':' && *p != '\r')
            p++;

        if (*p != ':') {
            p = skip_line(p);
            continue;
        }

        size_t nameLen = p - nameStart;
        if (nameLen >= sizeof(hdr->szName)) {
            nameLen = sizeof(hdr->szName) - 1;
        }
        memcpy(hdr->szName, nameStart, nameLen);
        hdr->szName[nameLen] = '\0';

        p++; /* Skip ':' */
        p = skip_ws(p);

        /* Header value */
        const char *valStart = p;
        while (*p && *p != '\r' && *p != '\n')
            p++;

        size_t valLen = p - valStart;
        if (valLen >= sizeof(hdr->szValue)) {
            valLen = sizeof(hdr->szValue) - 1;
        }
        memcpy(hdr->szValue, valStart, valLen);
        hdr->szValue[valLen] = '\0';

        /* Parse common headers */
        if (strcasecmp_local(hdr->szName, "CSeq") == 0) {
            pResponse->u32CSeq = (U32)atoi(hdr->szValue);
        } else if (strcasecmp_local(hdr->szName, "Content-Length") == 0) {
            pResponse->u32ContentLength = (U32)atoi(hdr->szValue);
        } else if (strcasecmp_local(hdr->szName, "Content-Type") == 0) {
            strncpy(pResponse->szContentType, hdr->szValue, sizeof(pResponse->szContentType) - 1);
        } else if (strcasecmp_local(hdr->szName, "Session") == 0) {
            strncpy(pResponse->szSession, hdr->szValue, sizeof(pResponse->szSession) - 1);
        } else if (strcasecmp_local(hdr->szName, "Transport") == 0) {
            strncpy(pResponse->szTransport, hdr->szValue, sizeof(pResponse->szTransport) - 1);
        } else if (strcasecmp_local(hdr->szName, "Public") == 0) {
            strncpy(pResponse->szPublic, hdr->szValue, sizeof(pResponse->szPublic) - 1);
        }

        pResponse->u32HeaderCount++;
        p = skip_line(p);
    }

    /* Body */
    if (bodyStart && pResponse->u32ContentLength > 0) {
        U32 availBody = u32Len - (U32)(bodyStart - pszData);
        if (availBody < pResponse->u32ContentLength) {
            return 1; /* Need more data */
        }
        pResponse->pszBody = bodyStart;
        pResponse->u32BodyLen = pResponse->u32ContentLength;
    }

    return 0;
}

const char *Rtsp_GetHeader(const RtspResponse *pResponse, const char *pszName) {
    if (!pResponse || !pszName)
        return NULL;

    for (U32 i = 0; i < pResponse->u32HeaderCount; i++) {
        if (strcasecmp_local(pResponse->astHeaders[i].szName, pszName) == 0) {
            return pResponse->astHeaders[i].szValue;
        }
    }
    return NULL;
}

S32 Rtsp_ParseTransport(const char *pszTransport, U16 *pu16RtpPort, U16 *pu16RtcpPort, BOOL *pbInterleaved,
    U8 *pu8RtpChannel, U8 *pu8RtcpChannel) {
    if (!pszTransport)
        return -1;

    if (pu16RtpPort)
        *pu16RtpPort = 0;
    if (pu16RtcpPort)
        *pu16RtcpPort = 0;
    if (pbInterleaved)
        *pbInterleaved = MPP_FALSE;
    if (pu8RtpChannel)
        *pu8RtpChannel = 0;
    if (pu8RtcpChannel)
        *pu8RtcpChannel = 1;

    /* Check for interleaved (TCP) */
    const char *interleaved = strstr(pszTransport, "interleaved=");
    if (interleaved) {
        if (pbInterleaved)
            *pbInterleaved = MPP_TRUE;

        interleaved += 12;
        U32 ch1 = (U32)atoi(interleaved);
        const char *dash = strchr(interleaved, '-');
        U32 ch2 = dash ? (U32)atoi(dash + 1) : ch1 + 1;

        if (pu8RtpChannel)
            *pu8RtpChannel = (U8)ch1;
        if (pu8RtcpChannel)
            *pu8RtcpChannel = (U8)ch2;

        return 0;
    }

    /* UDP: look for client_port or server_port */
    const char *clientPort = strstr(pszTransport, "client_port=");
    if (clientPort) {
        clientPort += 12;
        U16 port1 = (U16)atoi(clientPort);
        const char *dash = strchr(clientPort, '-');
        U16 port2 = dash ? (U16)atoi(dash + 1) : port1 + 1;

        if (pu16RtpPort)
            *pu16RtpPort = port1;
        if (pu16RtcpPort)
            *pu16RtcpPort = port2;
    }

    const char *serverPort = strstr(pszTransport, "server_port=");
    if (serverPort) {
        serverPort += 12;
        /* Server port info available if needed */
    }

    return 0;
}

S32 Rtsp_ParseSession(const char *pszSession, char *pszSessionId, U32 u32MaxLen) {
    if (!pszSession || !pszSessionId || u32MaxLen == 0) {
        return -1;
    }

    /* Session header format: "session-id;timeout=60" */
    const char *p = pszSession;
    U32 i = 0;

    while (*p && *p != ';' && *p != ' ' && i < u32MaxLen - 1) {
        pszSessionId[i++] = *p++;
    }
    pszSessionId[i] = '\0';

    return 0;
}
