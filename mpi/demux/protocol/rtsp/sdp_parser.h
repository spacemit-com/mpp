/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *
 * @File      :    sdp_parser.h
 * @Brief     :    SDP (Session Description Protocol) parser.
 *------------------------------------------------------------------------------
 */

#ifndef SDP_PARSER_H
#define SDP_PARSER_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum _SdpCodecType { SDP_CODEC_H264 = 0, SDP_CODEC_H265, SDP_CODEC_AAC, SDP_CODEC_UNKNOWN } SdpCodecType;

typedef struct _SdpInfo {
    SdpCodecType eCodec;
    S32 s32VideoTrackId;
    S32 s32AudioTrackId;
    /* Raw "a=control:" value per media section (RFC 2326). May be a bare
     * token ("trackID=0", "streamid=0"), a relative path, or an absolute
     * "rtsp://" URL. Empty string means the SDP had no control attribute. */
    CHAR szVideoControl[256];
    CHAR szAudioControl[256];
    U32 u32Width;
    U32 u32Height;
    U32 u32Fps;
    U32 u32ClockRate;
    U8 au8Sps[256];
    U32 u32SpsLen;
    U8 au8Pps[64];
    U32 u32PpsLen;
    U8 au8Vps[256]; /* H265 only */
    U32 u32VpsLen;
} SdpInfo;

/**
 * @brief  Parse SDP content
 * @return 0 on success, -1 on error
 */
S32 Sdp_Parse(const CHAR *pszSdp, SdpInfo *pstInfo);

#ifdef __cplusplus
}
#endif

#endif /* __SDP_PARSER_H__ */
