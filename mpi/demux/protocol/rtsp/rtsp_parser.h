/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    rtsp_parser.h
 * @Brief     :    RTSP protocol message parser.
 *------------------------------------------------------------------------------
 */

#ifndef RTSP_PARSER_H
#define RTSP_PARSER_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTSP_MAX_HEADERS 256
#define RTSP_MAX_METHOD_LEN 32
#define RTSP_MAX_HEADER_LEN 512

/* RTSP Status Codes */
#define RTSP_STATUS_OK 200
#define RTSP_STATUS_UNAUTHORIZED 401
#define RTSP_STATUS_NOT_FOUND 404
#define RTSP_STATUS_SESSION_NOT_FOUND 454
#define RTSP_STATUS_INTERNAL_ERROR 500

typedef struct _RtspHeader {
    char szName[64];
    char szValue[RTSP_MAX_HEADER_LEN];
} RtspHeader;

typedef struct _RtspResponse {
    /* Status line */
    U32 u32StatusCode;
    char szReasonPhrase[64];
    char szVersion[16];

    /* Headers */
    U32 u32HeaderCount;
    RtspHeader astHeaders[RTSP_MAX_HEADERS];

    /* Body */
    const char *pszBody;
    U32 u32BodyLen;

    /* Common headers (parsed) */
    U32 u32CSeq;
    U32 u32ContentLength;
    char szContentType[64];
    char szSession[64];
    char szTransport[256];
    char szPublic[256];
} RtspResponse;

/**
 * @brief  Parse RTSP response message
 * @param  pszData    Response data buffer
 * @param  u32Len     Data length
 * @param  pResponse  Output parsed response
 * @return 0 on success, -1 on error, 1 if need more data
 */
S32 Rtsp_ParseResponse(const char *pszData, U32 u32Len, RtspResponse *pResponse);

/**
 * @brief  Find header value by name (case-insensitive)
 * @return Header value or NULL if not found
 */
const char *Rtsp_GetHeader(const RtspResponse *pResponse, const char *pszName);

/**
 * @brief  Parse Transport header for RTSP SETUP response
 * @param  pszTransport  Transport header value
 * @param  pu16RtpPort   Output: client RTP port (for UDP)
 * @param  pu16RtcpPort  Output: client RTCP port (for UDP)
 * @param  pbInterleaved Output: TRUE if RTP over TCP
 * @param  pu8RtpChannel Output: RTP interleaved channel (for TCP)
 * @param  pu8RtcpChannel Output: RTCP interleaved channel (for TCP)
 * @return 0 on success
 */
S32 Rtsp_ParseTransport(const char *pszTransport, U16 *pu16RtpPort, U16 *pu16RtcpPort, BOOL *pbInterleaved,
    U8 *pu8RtpChannel, U8 *pu8RtcpChannel);

/**
 * @brief  Extract Session ID from Session header
 * @param  pszSession   Session header value
 * @param  pszSessionId Output buffer
 * @param  u32MaxLen    Buffer size
 * @return 0 on success
 */
S32 Rtsp_ParseSession(const char *pszSession, char *pszSessionId, U32 u32MaxLen);

/**
 * @brief  Check if response is complete (has full headers + body)
 * @return TRUE if complete
 */
BOOL Rtsp_IsResponseComplete(const char *pszData, U32 u32Len);

#ifdef __cplusplus
}
#endif

#endif /* __RTSP_PARSER_H__ */
