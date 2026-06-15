/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "rtsp_client.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "codec/h264_utils.h"
#include "codec/h265_utils.h"
#include "common/socket_utils.h"
#include "common/url_parser.h"
#include "rtp/h264_depacketizer.h"
#include "rtp/h265_depacketizer.h"
#include "rtp_session.h"
#include "sdp_parser.h"

#define RTSP_BUF_SIZE 4096
#define RTSP_UDP_BASE_PORT 50000 /* Base port for RTP UDP */

/* Atomic counter for unique UDP port allocation per client */
static atomic_int g_rtspPortOffset = 0;
#define RTSP_MAX_SDP_SIZE 8192

typedef enum _RtspState {
    RTSP_STATE_INIT = 0,
    RTSP_STATE_OPTIONS,
    RTSP_STATE_DESCRIBE,
    RTSP_STATE_SETUP,
    RTSP_STATE_PLAY,
    RTSP_STATE_STREAMING
} RtspState;

typedef enum _RtspAuthType {
    RTSP_AUTH_NONE = 0,
    RTSP_AUTH_BASIC,
    RTSP_AUTH_DIGEST
} RtspAuthType;

struct _RtspClient {
    /* Connection */
    S32 s32Fd;
    UrlInfo stUrl;
    CHAR szSession[64];
    U32 u32CSeq;
    RtspState eState;
    BOOL bTcpInterleaved;
    U32 u32TimeoutMs;

    /* Auth (Basic / Digest) */
    RtspAuthType eAuthType;
    CHAR szRealm[128];
    CHAR szNonce[128];
    CHAR szQop[32];   /* "auth" if server requires qop (RFC 2617) */
    U32 u32NonceCount; /* nc counter for qop=auth */
    BOOL bAuthRequired;

    /* SDP parsed info */
    SdpInfo stSdp;
    DemuxStreamInfo stStreamInfo;

    /* RTP session */
    RtpSession *pRtpSession;
    RtpDepacketizer *pDepack;

    /* Receive buffer */
    U8 au8RxBuf[RTSP_BUF_SIZE];
    U8 au8FrameBuf[512 * 1024]; /* Max NAL size */
    U32 u32FrameLen;
    BOOL bFrameReady;
    BOOL bKeyFrame;
    U64 u64FramePts;

    pthread_mutex_t lock;

    /* UDP port for this client (unique per instance) */
    U16 u16LocalRtpPort;
};

/* Forward declarations */
static S32 rtsp_send_options(RtspClient *pClient);
static S32 rtsp_send_describe(RtspClient *pClient);
static S32 rtsp_send_setup(RtspClient *pClient);
static S32 rtsp_send_play(RtspClient *pClient);
static S32 rtsp_send_teardown(RtspClient *pClient);
static S32 rtsp_recv_response(RtspClient *pClient, CHAR *pszBuf, U32 u32MaxLen);
static void rtsp_parse_www_authenticate(RtspClient *pClient, const CHAR *pszResponse);

/* Parse SPS from Annex-B stream to get resolution */
static void rtsp_parse_sps_from_annexb(RtspClient *pClient, const U8 *pu8Data, U32 u32Len) {
    /* Already have resolution */
    if (pClient->stStreamInfo.u32Width > 0 && pClient->stStreamInfo.u32Height > 0) {
        return;
    }

    /* Scan for SPS NAL unit in Annex-B format */
    U32 i = 0;
    while (i + 4 < u32Len) {
        /* Find start code (00 00 00 01 or 00 00 01) */
        if ((pu8Data[i] == 0 && pu8Data[i + 1] == 0 && pu8Data[i + 2] == 0 && pu8Data[i + 3] == 1) ||
            (pu8Data[i] == 0 && pu8Data[i + 1] == 0 && pu8Data[i + 2] == 1)) {
            U32 offset = (pu8Data[i + 2] == 1) ? 3 : 4;
            U32 nalStart = i + offset;
            if (nalStart >= u32Len)
                break;

            U8 nalType;
            U32 w = 0, h = 0;

            if (pClient->stStreamInfo.eCodecType == DEMUX_CODEC_H264) {
                nalType = pu8Data[nalStart] & 0x1F;
                if (nalType == 7) { /* SPS */
                    /* Find end of this NAL */
                    U32 nalEnd = nalStart + 1;
                    while (nalEnd + 3 < u32Len) {
                        if (pu8Data[nalEnd] == 0 && pu8Data[nalEnd + 1] == 0 &&
                            (pu8Data[nalEnd + 2] == 1 || (pu8Data[nalEnd + 2] == 0 && pu8Data[nalEnd + 3] == 1))) {
                            break;
                        }
                        nalEnd++;
                    }
                    if (H264_ParseSps(pu8Data + nalStart, nalEnd - nalStart, &w, &h) == 0 && w > 0 && h > 0) {
                        pClient->stStreamInfo.u32Width = w;
                        pClient->stStreamInfo.u32Height = h;
                        fprintf(stdout, "[RTSP] Parsed resolution from stream SPS: %ux%u\n", w, h);
                        return;
                    }
                }
            } else if (pClient->stStreamInfo.eCodecType == DEMUX_CODEC_H265) {
                nalType = (pu8Data[nalStart] >> 1) & 0x3F;
                if (nalType == 33) { /* SPS */
                    U32 nalEnd = nalStart + 2;
                    while (nalEnd + 3 < u32Len) {
                        if (pu8Data[nalEnd] == 0 && pu8Data[nalEnd + 1] == 0 &&
                            (pu8Data[nalEnd + 2] == 1 || (pu8Data[nalEnd + 2] == 0 && pu8Data[nalEnd + 3] == 1))) {
                            break;
                        }
                        nalEnd++;
                    }
                    if (H265_ParseSps(pu8Data + nalStart, nalEnd - nalStart, &w, &h) == 0 && w > 0 && h > 0) {
                        pClient->stStreamInfo.u32Width = w;
                        pClient->stStreamInfo.u32Height = h;
                        fprintf(stdout, "[RTSP] Parsed resolution from stream SPS: %ux%u\n", w, h);
                        return;
                    }
                }
            }
            i = nalStart;
        }
        i++;
    }
}

/* Callback from RTP depacketizer */
static void rtsp_frame_callback(void *pPriv, const U8 *pu8Data, U32 u32Len, U64 u64Pts, BOOL bKeyFrame) {
    RtspClient *pClient = (RtspClient *)pPriv;

    pthread_mutex_lock(&pClient->lock);
    if (u32Len <= sizeof(pClient->au8FrameBuf)) {
        memcpy(pClient->au8FrameBuf, pu8Data, u32Len);
        pClient->u32FrameLen = u32Len;
        pClient->bFrameReady = MPP_TRUE;
        pClient->bKeyFrame = bKeyFrame;
        pClient->u64FramePts = u64Pts;

        /* Try to parse resolution from keyframe SPS if not yet available */
        if (bKeyFrame && (pClient->stStreamInfo.u32Width == 0 || pClient->stStreamInfo.u32Height == 0)) {
            rtsp_parse_sps_from_annexb(pClient, pu8Data, u32Len);
        }
    }
    pthread_mutex_unlock(&pClient->lock);
}

RtspClient *RtspClient_Create(VOID) {
    RtspClient *pClient = (RtspClient *)calloc(1, sizeof(RtspClient));
    if (!pClient)
        return NULL;

    pClient->s32Fd = -1;
    pClient->eState = RTSP_STATE_INIT;
    pthread_mutex_init(&pClient->lock, NULL);

    /* Allocate unique UDP port pair for this client (offset by 2 for RTP+RTCP) */
    int offset = atomic_fetch_add(&g_rtspPortOffset, 1);
    pClient->u16LocalRtpPort = RTSP_UDP_BASE_PORT + (offset * 2);
    fprintf(stdout, "[RTSP] Allocated UDP ports %u-%u for client\n", pClient->u16LocalRtpPort,
        pClient->u16LocalRtpPort + 1);

    return pClient;
}

VOID RtspClient_Destroy(RtspClient *pClient) {
    if (!pClient)
        return;

    RtspClient_Disconnect(pClient);
    pthread_mutex_destroy(&pClient->lock);
    free(pClient);
}

S32 RtspClient_Connect(RtspClient *pClient, const CHAR *pszUrl, BOOL bPreferTcp, U32 u32TimeoutMs) {
    S32 s32Ret;
    CHAR szResponse[RTSP_BUF_SIZE];

    if (!pClient || !pszUrl)
        return ERR_DEMUX_NULL_PTR;

    /* Parse URL */
    if (Url_Parse(pszUrl, &pClient->stUrl) != 0) {
        return ERR_DEMUX_OPEN_FAIL;
    }

    pClient->bTcpInterleaved = bPreferTcp;
    pClient->u32TimeoutMs = u32TimeoutMs;
    pClient->u32CSeq = 1;

    /* TCP connect */
    fprintf(stdout, "[RTSP] Connecting to %s:%u...\n", pClient->stUrl.szHost, pClient->stUrl.u16Port);
    pClient->s32Fd = Socket_TcpConnect(pClient->stUrl.szHost, pClient->stUrl.u16Port, u32TimeoutMs);
    if (pClient->s32Fd < 0) {
        fprintf(stderr, "[RTSP] TCP connect failed\n");
        return ERR_DEMUX_OPEN_FAIL;
    }
    fprintf(stdout, "[RTSP] TCP connected (fd=%d)\n", pClient->s32Fd);

    Socket_SetRecvTimeout(pClient->s32Fd, u32TimeoutMs);
    Socket_SetSendTimeout(pClient->s32Fd, u32TimeoutMs);

    /* RTSP handshake: OPTIONS (no auth required) */
    fprintf(stdout, "[RTSP] Sending OPTIONS...\n");
    s32Ret = rtsp_send_options(pClient);
    if (s32Ret != 0) {
        fprintf(stderr, "[RTSP] OPTIONS send failed\n");
        goto fail;
    }

    s32Ret = rtsp_recv_response(pClient, szResponse, sizeof(szResponse));
    if (s32Ret < 0) {
        fprintf(stderr, "[RTSP] OPTIONS response failed\n");
        goto fail;
    }
    if (s32Ret != 200) {
        fprintf(stderr, "[RTSP] OPTIONS returned %d\n", s32Ret);
        goto fail;
    }
    fprintf(stdout, "[RTSP] OPTIONS OK\n");

    /* DESCRIBE - first without auth to get challenge */
    fprintf(stdout, "[RTSP] Sending DESCRIBE...\n");
    s32Ret = rtsp_send_describe(pClient);
    if (s32Ret != 0) {
        fprintf(stderr, "[RTSP] DESCRIBE send failed\n");
        goto fail;
    }

    s32Ret = rtsp_recv_response(pClient, szResponse, sizeof(szResponse));
    if (s32Ret < 0) {
        fprintf(stderr, "[RTSP] DESCRIBE response failed\n");
        goto fail;
    }

    /* Handle 401 Unauthorized - parse challenge and retry with auth */
    if (s32Ret == 401) {
        fprintf(stdout, "[RTSP] Got 401, parsing WWW-Authenticate...\n");
        rtsp_parse_www_authenticate(pClient, szResponse);

        if (!pClient->bAuthRequired) {
            fprintf(stderr, "[RTSP] 401 but no auth challenge found\n");
            goto fail;
        }
        if (!pClient->stUrl.szUser[0]) {
            fprintf(stderr, "[RTSP] Server requires auth but no credentials in URL\n");
            goto fail;
        }
        if (pClient->eAuthType == RTSP_AUTH_DIGEST) {
            fprintf(stdout, "[RTSP] Digest auth: realm=%s, nonce=%s, qop=%s\n", pClient->szRealm, pClient->szNonce,
                pClient->szQop[0] ? pClient->szQop : "(none)");
        } else {
            fprintf(stdout, "[RTSP] Basic auth: realm=%s\n", pClient->szRealm);
        }

        /* Retry DESCRIBE with auth */
        fprintf(stdout, "[RTSP] Sending DESCRIBE with auth...\n");
        s32Ret = rtsp_send_describe(pClient);
        if (s32Ret != 0) {
            fprintf(stderr, "[RTSP] DESCRIBE (auth) send failed\n");
            goto fail;
        }

        s32Ret = rtsp_recv_response(pClient, szResponse, sizeof(szResponse));
        if (s32Ret < 0) {
            fprintf(stderr, "[RTSP] DESCRIBE (auth) response failed\n");
            goto fail;
        }
    }

    if (s32Ret != 200) {
        fprintf(stderr, "[RTSP] DESCRIBE returned %d\n", s32Ret);
        goto fail;
    }
    fprintf(stdout, "[RTSP] DESCRIBE OK\n");

    /* Parse SDP from response body */
    const CHAR *pSdp = strstr(szResponse, "\r\n\r\n");
    if (pSdp) {
        pSdp += 4;
        if (Sdp_Parse(pSdp, &pClient->stSdp) != 0) {
            goto fail;
        }
    }

    /* Fill stream info */
    pClient->stStreamInfo.u32Width = pClient->stSdp.u32Width;
    pClient->stStreamInfo.u32Height = pClient->stSdp.u32Height;
    pClient->stStreamInfo.u32Fps = pClient->stSdp.u32Fps ? pClient->stSdp.u32Fps : 25;

    if (pClient->stSdp.eCodec == SDP_CODEC_H264) {
        pClient->stStreamInfo.eCodecType = DEMUX_CODEC_H264;
    } else if (pClient->stSdp.eCodec == SDP_CODEC_H265) {
        pClient->stStreamInfo.eCodecType = DEMUX_CODEC_H265;
    }

    /* SETUP */
    fprintf(stdout, "[RTSP] Sending SETUP...\n");
    s32Ret = rtsp_send_setup(pClient);
    if (s32Ret != 0) {
        fprintf(stderr, "[RTSP] SETUP send failed\n");
        goto fail;
    }

    s32Ret = rtsp_recv_response(pClient, szResponse, sizeof(szResponse));
    if (s32Ret < 0) {
        fprintf(stderr, "[RTSP] SETUP response failed\n");
        goto fail;
    }
    if (s32Ret != 200) {
        fprintf(stderr, "[RTSP] SETUP returned %d\n", s32Ret);
        goto fail;
    }
    fprintf(stdout, "[RTSP] SETUP OK\n");

    /* Extract Session ID */
    const CHAR *pSession = strstr(szResponse, "Session:");
    if (pSession) {
        sscanf(pSession + 8, "%63[^;\r\n]", pClient->szSession);
    }
    fprintf(stdout, "[RTSP] Session: %s\n", pClient->szSession);

    /* Create RTP session */
    pClient->pRtpSession = RtpSession_Create(pClient->bTcpInterleaved, pClient->s32Fd);
    if (!pClient->pRtpSession)
        goto fail;

    /* For UDP mode, bind the local ports (unique per client) */
    if (!pClient->bTcpInterleaved) {
        if (RtpSession_SetUdpPorts(pClient->pRtpSession, pClient->u16LocalRtpPort, pClient->u16LocalRtpPort + 1) != 0) {
            fprintf(stderr, "[RTSP] Failed to bind UDP ports %u-%u\n", pClient->u16LocalRtpPort,
                pClient->u16LocalRtpPort + 1);
            goto fail;
        }
    }

    /* Create depacketizer */
    if (pClient->stSdp.eCodec == SDP_CODEC_H264) {
        pClient->pDepack = H264Depack_Create();
    } else if (pClient->stSdp.eCodec == SDP_CODEC_H265) {
        pClient->pDepack = H265Depack_Create();
    }

    if (pClient->pDepack) {
        RtpDepack_SetCallback(pClient->pDepack, rtsp_frame_callback, pClient);

        /* Set SPS/PPS from SDP */
        if (pClient->stSdp.u32SpsLen > 0) {
            if (pClient->stSdp.eCodec == SDP_CODEC_H265) {
                H265Depack_SetSps(pClient->pDepack, pClient->stSdp.au8Sps, pClient->stSdp.u32SpsLen);
            } else {
                RtpDepack_SetSps(pClient->pDepack, pClient->stSdp.au8Sps, pClient->stSdp.u32SpsLen);
            }

            /* Parse resolution from SPS if not available in SDP */
            if (pClient->stStreamInfo.u32Width == 0 || pClient->stStreamInfo.u32Height == 0) {
                U32 w = 0, h = 0;
                if (pClient->stSdp.eCodec == SDP_CODEC_H264) {
                    if (H264_ParseSps(pClient->stSdp.au8Sps, pClient->stSdp.u32SpsLen, &w, &h) == 0) {
                        pClient->stStreamInfo.u32Width = w;
                        pClient->stStreamInfo.u32Height = h;
                        fprintf(stdout, "[RTSP] Parsed resolution from SPS: %ux%u\n", w, h);
                    }
                } else if (pClient->stSdp.eCodec == SDP_CODEC_H265) {
                    if (H265_ParseSps(pClient->stSdp.au8Sps, pClient->stSdp.u32SpsLen, &w, &h) == 0) {
                        pClient->stStreamInfo.u32Width = w;
                        pClient->stStreamInfo.u32Height = h;
                        fprintf(stdout, "[RTSP] Parsed resolution from SPS: %ux%u\n", w, h);
                    }
                }
            }
        }
        if (pClient->stSdp.u32PpsLen > 0) {
            if (pClient->stSdp.eCodec == SDP_CODEC_H265) {
                H265Depack_SetPps(pClient->pDepack, pClient->stSdp.au8Pps, pClient->stSdp.u32PpsLen);
            } else {
                RtpDepack_SetPps(pClient->pDepack, pClient->stSdp.au8Pps, pClient->stSdp.u32PpsLen);
            }
        }
        if (pClient->stSdp.eCodec == SDP_CODEC_H265 && pClient->stSdp.u32VpsLen > 0) {
            H265Depack_SetVps(pClient->pDepack, pClient->stSdp.au8Vps, pClient->stSdp.u32VpsLen);
        }
    }

    /* PLAY */
    fprintf(stdout, "[RTSP] Sending PLAY...\n");
    s32Ret = rtsp_send_play(pClient);
    if (s32Ret != 0) {
        fprintf(stderr, "[RTSP] PLAY send failed\n");
        goto fail;
    }

    s32Ret = rtsp_recv_response(pClient, szResponse, sizeof(szResponse));
    if (s32Ret < 0) {
        fprintf(stderr, "[RTSP] PLAY response failed\n");
        goto fail;
    }
    if (s32Ret != 200) {
        fprintf(stderr, "[RTSP] PLAY returned %d\n", s32Ret);
        goto fail;
    }
    fprintf(stdout, "[RTSP] PLAY OK, streaming started\n");

    pClient->eState = RTSP_STATE_STREAMING;
    return 0;

fail:
    RtspClient_Disconnect(pClient);
    return ERR_DEMUX_OPEN_FAIL;
}

VOID RtspClient_Disconnect(RtspClient *pClient) {
    if (!pClient)
        return;

    if (pClient->eState == RTSP_STATE_STREAMING) {
        rtsp_send_teardown(pClient);
    }

    if (pClient->pDepack) {
        RtpDepack_Destroy(pClient->pDepack);
        pClient->pDepack = NULL;
    }

    if (pClient->pRtpSession) {
        RtpSession_Destroy(pClient->pRtpSession);
        pClient->pRtpSession = NULL;
    }

    if (pClient->s32Fd >= 0) {
        Socket_Close(pClient->s32Fd);
        pClient->s32Fd = -1;
    }

    pClient->eState = RTSP_STATE_INIT;
}

S32 RtspClient_GetStreamInfo(RtspClient *pClient, DemuxStreamInfo *pstInfo) {
    if (!pClient || !pstInfo)
        return ERR_DEMUX_NULL_PTR;
    *pstInfo = pClient->stStreamInfo;
    return 0;
}

S32 RtspClient_ReadPacket(RtspClient *pClient, DemuxPacket *pstPkt) {
    if (!pClient || !pstPkt)
        return ERR_DEMUX_NULL_PTR;
    if (pClient->eState != RTSP_STATE_STREAMING)
        return ERR_DEMUX_NOT_STARTED;

    /* Clear previous frame */
    pthread_mutex_lock(&pClient->lock);
    pClient->bFrameReady = MPP_FALSE;
    pthread_mutex_unlock(&pClient->lock);

    /* Read RTP packets until we have a complete frame */
    while (!pClient->bFrameReady) {
        U8 au8RtpBuf[2048];
        U32 u32RtpLen;

        S32 s32Ret = RtpSession_RecvPacket(
            pClient->pRtpSession, au8RtpBuf, sizeof(au8RtpBuf), &u32RtpLen, pClient->u32TimeoutMs);
        if (s32Ret != 0) {
            return ERR_DEMUX_NO_STREAM;
        }

        /* Feed to depacketizer */
        if (pClient->pDepack) {
            if (pClient->stSdp.eCodec == SDP_CODEC_H265) {
                H265Depack_Input(pClient->pDepack, au8RtpBuf, u32RtpLen);
            } else {
                RtpDepack_Input(pClient->pDepack, au8RtpBuf, u32RtpLen);
            }
        }
    }

    /* Fill output packet */
    pthread_mutex_lock(&pClient->lock);
    pstPkt->pu8Data = pClient->au8FrameBuf;
    pstPkt->u32Size = pClient->u32FrameLen;
    pstPkt->bKeyFrame = pClient->bKeyFrame;
    pstPkt->eCodecType = pClient->stStreamInfo.eCodecType;
    pstPkt->u64PTS = pClient->u64FramePts;
    pstPkt->u32Width = pClient->stStreamInfo.u32Width;
    pstPkt->u32Height = pClient->stStreamInfo.u32Height;
    pthread_mutex_unlock(&pClient->lock);

    return 0;
}

BOOL RtspClient_IsConnected(RtspClient *pClient) { return pClient && pClient->eState == RTSP_STATE_STREAMING; }

/* ======================== Internal Functions ======================== */

/* Simple MD5 implementation for Digest Auth */
typedef struct {
    U32 state[4];
    U32 count[2];
    U8 buffer[64];
} MD5_CTX;

#define MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~z)))
#define MD5_ROT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void md5_transform(U32 state[4], const U8 block[64]) {
    U32 a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    for (int i = 0; i < 16; i++)
        x[i] = block[i * 4] | (block[i * 4 + 1] << 8) | (block[i * 4 + 2] << 16) | (block[i * 4 + 3] << 24);

#define MD5_STEP(f, a, b, c, d, x, t, s)                                                                               \
    a += f(b, c, d) + x + t;                                                                                           \
    a = MD5_ROT(a, s) + b
    MD5_STEP(MD5_F, a, b, c, d, x[0], 0xd76aa478, 7);
    MD5_STEP(MD5_F, d, a, b, c, x[1], 0xe8c7b756, 12);
    MD5_STEP(MD5_F, c, d, a, b, x[2], 0x242070db, 17);
    MD5_STEP(MD5_F, b, c, d, a, x[3], 0xc1bdceee, 22);
    MD5_STEP(MD5_F, a, b, c, d, x[4], 0xf57c0faf, 7);
    MD5_STEP(MD5_F, d, a, b, c, x[5], 0x4787c62a, 12);
    MD5_STEP(MD5_F, c, d, a, b, x[6], 0xa8304613, 17);
    MD5_STEP(MD5_F, b, c, d, a, x[7], 0xfd469501, 22);
    MD5_STEP(MD5_F, a, b, c, d, x[8], 0x698098d8, 7);
    MD5_STEP(MD5_F, d, a, b, c, x[9], 0x8b44f7af, 12);
    MD5_STEP(MD5_F, c, d, a, b, x[10], 0xffff5bb1, 17);
    MD5_STEP(MD5_F, b, c, d, a, x[11], 0x895cd7be, 22);
    MD5_STEP(MD5_F, a, b, c, d, x[12], 0x6b901122, 7);
    MD5_STEP(MD5_F, d, a, b, c, x[13], 0xfd987193, 12);
    MD5_STEP(MD5_F, c, d, a, b, x[14], 0xa679438e, 17);
    MD5_STEP(MD5_F, b, c, d, a, x[15], 0x49b40821, 22);

    MD5_STEP(MD5_G, a, b, c, d, x[1], 0xf61e2562, 5);
    MD5_STEP(MD5_G, d, a, b, c, x[6], 0xc040b340, 9);
    MD5_STEP(MD5_G, c, d, a, b, x[11], 0x265e5a51, 14);
    MD5_STEP(MD5_G, b, c, d, a, x[0], 0xe9b6c7aa, 20);
    MD5_STEP(MD5_G, a, b, c, d, x[5], 0xd62f105d, 5);
    MD5_STEP(MD5_G, d, a, b, c, x[10], 0x02441453, 9);
    MD5_STEP(MD5_G, c, d, a, b, x[15], 0xd8a1e681, 14);
    MD5_STEP(MD5_G, b, c, d, a, x[4], 0xe7d3fbc8, 20);
    MD5_STEP(MD5_G, a, b, c, d, x[9], 0x21e1cde6, 5);
    MD5_STEP(MD5_G, d, a, b, c, x[14], 0xc33707d6, 9);
    MD5_STEP(MD5_G, c, d, a, b, x[3], 0xf4d50d87, 14);
    MD5_STEP(MD5_G, b, c, d, a, x[8], 0x455a14ed, 20);
    MD5_STEP(MD5_G, a, b, c, d, x[13], 0xa9e3e905, 5);
    MD5_STEP(MD5_G, d, a, b, c, x[2], 0xfcefa3f8, 9);
    MD5_STEP(MD5_G, c, d, a, b, x[7], 0x676f02d9, 14);
    MD5_STEP(MD5_G, b, c, d, a, x[12], 0x8d2a4c8a, 20);

    MD5_STEP(MD5_H, a, b, c, d, x[5], 0xfffa3942, 4);
    MD5_STEP(MD5_H, d, a, b, c, x[8], 0x8771f681, 11);
    MD5_STEP(MD5_H, c, d, a, b, x[11], 0x6d9d6122, 16);
    MD5_STEP(MD5_H, b, c, d, a, x[14], 0xfde5380c, 23);
    MD5_STEP(MD5_H, a, b, c, d, x[1], 0xa4beea44, 4);
    MD5_STEP(MD5_H, d, a, b, c, x[4], 0x4bdecfa9, 11);
    MD5_STEP(MD5_H, c, d, a, b, x[7], 0xf6bb4b60, 16);
    MD5_STEP(MD5_H, b, c, d, a, x[10], 0xbebfbc70, 23);
    MD5_STEP(MD5_H, a, b, c, d, x[13], 0x289b7ec6, 4);
    MD5_STEP(MD5_H, d, a, b, c, x[0], 0xeaa127fa, 11);
    MD5_STEP(MD5_H, c, d, a, b, x[3], 0xd4ef3085, 16);
    MD5_STEP(MD5_H, b, c, d, a, x[6], 0x04881d05, 23);
    MD5_STEP(MD5_H, a, b, c, d, x[9], 0xd9d4d039, 4);
    MD5_STEP(MD5_H, d, a, b, c, x[12], 0xe6db99e5, 11);
    MD5_STEP(MD5_H, c, d, a, b, x[15], 0x1fa27cf8, 16);
    MD5_STEP(MD5_H, b, c, d, a, x[2], 0xc4ac5665, 23);

    MD5_STEP(MD5_I, a, b, c, d, x[0], 0xf4292244, 6);
    MD5_STEP(MD5_I, d, a, b, c, x[7], 0x432aff97, 10);
    MD5_STEP(MD5_I, c, d, a, b, x[14], 0xab9423a7, 15);
    MD5_STEP(MD5_I, b, c, d, a, x[5], 0xfc93a039, 21);
    MD5_STEP(MD5_I, a, b, c, d, x[12], 0x655b59c3, 6);
    MD5_STEP(MD5_I, d, a, b, c, x[3], 0x8f0ccc92, 10);
    MD5_STEP(MD5_I, c, d, a, b, x[10], 0xffeff47d, 15);
    MD5_STEP(MD5_I, b, c, d, a, x[1], 0x85845dd1, 21);
    MD5_STEP(MD5_I, a, b, c, d, x[8], 0x6fa87e4f, 6);
    MD5_STEP(MD5_I, d, a, b, c, x[15], 0xfe2ce6e0, 10);
    MD5_STEP(MD5_I, c, d, a, b, x[6], 0xa3014314, 15);
    MD5_STEP(MD5_I, b, c, d, a, x[13], 0x4e0811a1, 21);
    MD5_STEP(MD5_I, a, b, c, d, x[4], 0xf7537e82, 6);
    MD5_STEP(MD5_I, d, a, b, c, x[11], 0xbd3af235, 10);
    MD5_STEP(MD5_I, c, d, a, b, x[2], 0x2ad7d2bb, 15);
    MD5_STEP(MD5_I, b, c, d, a, x[9], 0xeb86d391, 21);
#undef MD5_STEP

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void md5_init(MD5_CTX *ctx) {
    ctx->count[0] = ctx->count[1] = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

static void md5_update(MD5_CTX *ctx, const U8 *input, U32 len) {
    U32 i, idx = (ctx->count[0] >> 3) & 0x3F;
    if ((ctx->count[0] += (len << 3)) < (len << 3))
        ctx->count[1]++;
    ctx->count[1] += (len >> 29);
    U32 partLen = 64 - idx;
    if (len >= partLen) {
        memcpy(&ctx->buffer[idx], input, partLen);
        md5_transform(ctx->state, ctx->buffer);
        for (i = partLen; i + 63 < len; i += 64)
            md5_transform(ctx->state, &input[i]);
        idx = 0;
    } else {
        i = 0;
    }
    memcpy(&ctx->buffer[idx], &input[i], len - i);
}

static void md5_final(U8 digest[16], MD5_CTX *ctx) {
    static const U8 padding[64] = {0x80};
    U8 bits[8];
    for (int i = 0; i < 4; i++) {
        bits[i] = ctx->count[0] >> (i * 8);
        bits[i + 4] = ctx->count[1] >> (i * 8);
    }
    U32 idx = (ctx->count[0] >> 3) & 0x3f;
    U32 padLen = (idx < 56) ? (56 - idx) : (120 - idx);
    md5_update(ctx, padding, padLen);
    md5_update(ctx, bits, 8);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            digest[i * 4 + j] = ctx->state[i] >> (j * 8);
}

static void md5_hash_string(const CHAR *str, CHAR *out) {
    MD5_CTX ctx;
    U8 digest[16];
    md5_init(&ctx);
    md5_update(&ctx, (const U8 *)str, strlen(str));
    md5_final(digest, &ctx);
    for (int i = 0; i < 16; i++) {
        CHAR *pHex = &out[i * 2];
        snprintf(pHex, sizeof("00"), "%02x", digest[i]);
    }
}

/* Case-insensitive strstr (RTSP header names are case-insensitive) */
static const CHAR *rtsp_stristr(const CHAR *pszHaystack, const CHAR *pszNeedle) {
    size_t needleLen = strlen(pszNeedle);
    if (needleLen == 0)
        return pszHaystack;
    for (const CHAR *p = pszHaystack; *p; p++) {
        if (strncasecmp(p, pszNeedle, needleLen) == 0)
            return p;
    }
    return NULL;
}

/* Extract a parameter value within a single header line.
 * pszKey should include the parameter name plus '=' (for example, "realm=").
 * The value may be quoted or unquoted; optional whitespace and a leading
 * opening quote are skipped so headers like realm= "value" still parse. */
static BOOL rtsp_auth_get_param(
    const CHAR *pszLine, const CHAR *pszLineEnd, const CHAR *pszKey, CHAR *pszOut, U32 u32MaxLen) {
    const CHAR *p = rtsp_stristr(pszLine, pszKey);
    if (!p || p >= pszLineEnd)
        return MPP_FALSE;
    p += strlen(pszKey);
    while (p < pszLineEnd && (*p == ' ' || *p == '\t'))
        p++;
    if (p < pszLineEnd && *p == '"')
        p++;

    const CHAR *end = p;
    while (end < pszLineEnd && *end != '"' && *end != ',' && *end != ';' && *end != '\r' && *end != '\n')
        end++;
    if (end <= p)
        return MPP_FALSE;
    size_t len = (size_t)(end - p);
    if (len >= u32MaxLen)
        len = u32MaxLen - 1;
    memcpy(pszOut, p, len);
    pszOut[len] = '\0';
    return MPP_TRUE;
}

/*
 * Parse 401 response to extract the auth challenge.
 * Servers may send multiple WWW-Authenticate headers (e.g. both Digest and
 * Basic); Digest is preferred when available. Header name matching must be
 * case-insensitive per RFC 2326/7826.
 */
static void rtsp_parse_www_authenticate(RtspClient *pClient, const CHAR *pszResponse) {
    const CHAR *p = pszResponse;

    pClient->eAuthType = RTSP_AUTH_NONE;
    pClient->bAuthRequired = MPP_FALSE;
    pClient->szRealm[0] = '\0';
    pClient->szNonce[0] = '\0';
    pClient->szQop[0] = '\0';
    pClient->u32NonceCount = 0;

    while ((p = rtsp_stristr(p, "WWW-Authenticate:")) != NULL) {
        const CHAR *pLine = p + strlen("WWW-Authenticate:");
        const CHAR *pLineEnd = strstr(pLine, "\r\n");
        if (!pLineEnd)
            pLineEnd = pLine + strlen(pLine);

        while (pLine < pLineEnd && (*pLine == ' ' || *pLine == '\t'))
            pLine++;

        if (strncasecmp(pLine, "Digest", 6) == 0) {
            CHAR szRealm[sizeof(pClient->szRealm)] = {0};
            CHAR szNonce[sizeof(pClient->szNonce)] = {0};
            CHAR szQop[sizeof(pClient->szQop)] = {0};

            if (rtsp_auth_get_param(pLine, pLineEnd, "realm=", szRealm, sizeof(szRealm)) &&
                rtsp_auth_get_param(pLine, pLineEnd, "nonce=", szNonce, sizeof(szNonce))) {
                snprintf(pClient->szRealm, sizeof(pClient->szRealm), "%s", szRealm);
                snprintf(pClient->szNonce, sizeof(pClient->szNonce), "%s", szNonce);
                /* qop is optional; when present we must use RFC 2617 mode */
                if (rtsp_auth_get_param(pLine, pLineEnd, "qop=", szQop, sizeof(szQop))) {
                    /* qop may be a list like "auth,auth-int"; pick "auth" */
                    if (rtsp_stristr(szQop, "auth") != NULL) {
                        strncpy(pClient->szQop, "auth", sizeof(pClient->szQop) - 1);
                    }
                }
                pClient->eAuthType = RTSP_AUTH_DIGEST;
                pClient->bAuthRequired = MPP_TRUE;
                /* Digest preferred: stop searching */
                return;
            }
        } else if (strncasecmp(pLine, "Basic", 5) == 0) {
            /* Remember Basic as fallback but keep scanning for Digest */
            if (pClient->eAuthType == RTSP_AUTH_NONE) {
                rtsp_auth_get_param(pLine, pLineEnd, "realm=", pClient->szRealm, sizeof(pClient->szRealm));
                pClient->eAuthType = RTSP_AUTH_BASIC;
                pClient->bAuthRequired = MPP_TRUE;
            }
        }

        p = pLineEnd;
    }

    if (!pClient->bAuthRequired) {
        fprintf(stderr, "[RTSP] Unrecognized auth challenge, response was:\n%s\n", pszResponse);
    }
}

/* Base64 encoder for Basic auth */
static void rtsp_base64_encode(const U8 *pu8In, U32 u32InLen, CHAR *pszOut, U32 u32MaxLen) {
    static const CHAR tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    U32 i, o = 0;

    for (i = 0; i + 2 < u32InLen && o + 4 < u32MaxLen; i += 3) {
        U32 v = (pu8In[i] << 16) | (pu8In[i + 1] << 8) | pu8In[i + 2];
        pszOut[o++] = tbl[(v >> 18) & 0x3F];
        pszOut[o++] = tbl[(v >> 12) & 0x3F];
        pszOut[o++] = tbl[(v >> 6) & 0x3F];
        pszOut[o++] = tbl[v & 0x3F];
    }
    if (i < u32InLen && o + 4 < u32MaxLen) {
        U32 rem = u32InLen - i;
        U32 v = pu8In[i] << 16;
        if (rem == 2)
            v |= pu8In[i + 1] << 8;
        pszOut[o++] = tbl[(v >> 18) & 0x3F];
        pszOut[o++] = tbl[(v >> 12) & 0x3F];
        pszOut[o++] = (rem == 2) ? tbl[(v >> 6) & 0x3F] : '=';
        pszOut[o++] = '=';
    }
    pszOut[o] = '\0';
}

/*
 * Generate Authorization header (Basic or Digest, with optional qop=auth).
 * Writes an empty string when no auth is required/possible.
 */
static void rtsp_get_auth_header(
    RtspClient *pClient, const CHAR *pszMethod, const CHAR *pszUri, CHAR *pszAuth, U32 u32MaxLen) {
    if (!pClient->bAuthRequired || !pClient->stUrl.szUser[0]) {
        pszAuth[0] = '\0';
        return;
    }

    if (pClient->eAuthType == RTSP_AUTH_BASIC) {
        CHAR szCred[512];
        CHAR szB64[768];
        S32 s32Len = snprintf(szCred, sizeof(szCred), "%s:%s", pClient->stUrl.szUser, pClient->stUrl.szPass);
        if (s32Len < 0 || (size_t)s32Len >= sizeof(szCred)) {
            pszAuth[0] = '\0';
            return;
        }
        rtsp_base64_encode((const U8 *)szCred, (U32)s32Len, szB64, sizeof(szB64));
        snprintf(pszAuth, u32MaxLen, "Authorization: Basic %s\r\n", szB64);
        return;
    }

    /* HA1 = MD5(username:realm:password) */
    CHAR szHA1Input[1024], szHA1[33];
    snprintf(
        szHA1Input, sizeof(szHA1Input), "%s:%s:%s", pClient->stUrl.szUser, pClient->szRealm, pClient->stUrl.szPass);
    md5_hash_string(szHA1Input, szHA1);

    /* HA2 = MD5(method:uri) */
    CHAR szHA2Input[1024], szHA2[33];
    snprintf(szHA2Input, sizeof(szHA2Input), "%s:%s", pszMethod, pszUri);
    md5_hash_string(szHA2Input, szHA2);

    CHAR szRespInput[512], szResponse[33];
    if (pClient->szQop[0]) {
        /* RFC 2617: response = MD5(HA1:nonce:nc:cnonce:qop:HA2) */
        CHAR szCNonce[17];
        CHAR szNc[9];
        pClient->u32NonceCount++;
        snprintf(szNc, sizeof(szNc), "%08x", pClient->u32NonceCount);
        /* Generate cnonce with nanosecond entropy (RFC 2617 requires
         * unpredictable value). Both clock_gettime and u32CSeq read are
         * performed under lock to prevent a data race with the packet-
         * read path that may increment u32CSeq concurrently. */
        {
            struct timespec ts;
            U32 u32Seq;
            pthread_mutex_lock(&pClient->lock);
            clock_gettime(CLOCK_MONOTONIC, &ts);
            u32Seq = pClient->u32CSeq;
            pthread_mutex_unlock(&pClient->lock);
            snprintf(szCNonce, sizeof(szCNonce), "%08x%08x",
                (U32)(ts.tv_nsec ^ (ts.tv_sec << 16)),
                (U32)(uintptr_t)pClient ^ u32Seq);
        }

        snprintf(szRespInput, sizeof(szRespInput), "%s:%s:%s:%s:%s:%s", szHA1, pClient->szNonce, szNc, szCNonce,
            pClient->szQop, szHA2);
        md5_hash_string(szRespInput, szResponse);

        snprintf(pszAuth, u32MaxLen,
            "Authorization: Digest username=\"%s\", realm=\"%s\", "
            "nonce=\"%s\", uri=\"%s\", response=\"%s\", "
            "qop=%s, nc=%s, cnonce=\"%s\"\r\n",
            pClient->stUrl.szUser, pClient->szRealm, pClient->szNonce, pszUri, szResponse, pClient->szQop, szNc,
            szCNonce);
    } else {
        /* RFC 2069 (legacy): response = MD5(HA1:nonce:HA2) */
        snprintf(szRespInput, sizeof(szRespInput), "%s:%s:%s", szHA1, pClient->szNonce, szHA2);
        md5_hash_string(szRespInput, szResponse);

        snprintf(pszAuth, u32MaxLen,
            "Authorization: Digest username=\"%s\", realm=\"%s\", "
            "nonce=\"%s\", uri=\"%s\", response=\"%s\"\r\n",
            pClient->stUrl.szUser, pClient->szRealm, pClient->szNonce, pszUri, szResponse);
    }
}

static S32 rtsp_send_request(RtspClient *pClient, const CHAR *pszMethod, const CHAR *pszExtra) {
    CHAR szReq[4096];
    CHAR szAuth[2048];
    CHAR szUri[1024];
    S32 s32Len;

    s32Len = snprintf(
        szUri, sizeof(szUri), "rtsp://%s:%u%s", pClient->stUrl.szHost, pClient->stUrl.u16Port, pClient->stUrl.szPath);
    if (s32Len < 0 || (size_t)s32Len >= sizeof(szUri)) {
        return -1;
    }
    rtsp_get_auth_header(pClient, pszMethod, szUri, szAuth, sizeof(szAuth));

    s32Len = snprintf(szReq, sizeof(szReq),
        "%s %s RTSP/1.0\r\n"
        "CSeq: %u\r\n"
        "User-Agent: SpacemiT-MPP/1.0\r\n"
        "%s"
        "%s%s"
        "\r\n",
        pszMethod, szUri, pClient->u32CSeq++, szAuth, pClient->szSession[0] ? "Session: " : "",
        pClient->szSession[0] ? pClient->szSession : "");
    if (s32Len < 0 || (size_t)s32Len >= sizeof(szReq)) {
        return -1;
    }

    return Socket_SendAll(pClient->s32Fd, (U8 *)szReq, s32Len) > 0 ? 0 : -1;
}

static S32 rtsp_send_options(RtspClient *pClient) { return rtsp_send_request(pClient, "OPTIONS", ""); }

static S32 rtsp_send_describe(RtspClient *pClient) {
    CHAR szReq[4096];
    CHAR szAuth[2048];
    CHAR szUri[1024];
    S32 s32Len;

    s32Len = snprintf(
        szUri, sizeof(szUri), "rtsp://%s:%u%s", pClient->stUrl.szHost, pClient->stUrl.u16Port, pClient->stUrl.szPath);
    if (s32Len < 0 || (size_t)s32Len >= sizeof(szUri)) {
        return -1;
    }
    rtsp_get_auth_header(pClient, "DESCRIBE", szUri, szAuth, sizeof(szAuth));

    s32Len = snprintf(szReq, sizeof(szReq),
        "DESCRIBE %s RTSP/1.0\r\n"
        "CSeq: %u\r\n"
        "User-Agent: SpacemiT-MPP/1.0\r\n"
        "%s"
        "Accept: application/sdp\r\n"
        "\r\n",
        szUri, pClient->u32CSeq++, szAuth);
    if (s32Len < 0 || (size_t)s32Len >= sizeof(szReq)) {
        return -1;
    }

    return Socket_SendAll(pClient->s32Fd, (U8 *)szReq, s32Len) > 0 ? 0 : -1;
}

static S32 rtsp_send_setup(RtspClient *pClient) {
    CHAR szReq[4096];
    CHAR szAuth[2048];
    CHAR szUri[1024];
    S32 s32Len;
    const CHAR *pszTransport;

    /* Use track0 instead of trackID=N to match server format */
    s32Len = snprintf(szUri, sizeof(szUri), "rtsp://%s:%u%s/track%d", pClient->stUrl.szHost, pClient->stUrl.u16Port,
        pClient->stUrl.szPath, pClient->stSdp.s32VideoTrackId);
    if (s32Len < 0 || (size_t)s32Len >= sizeof(szUri)) {
        return -1;
    }
    rtsp_get_auth_header(pClient, "SETUP", szUri, szAuth, sizeof(szAuth));

    CHAR szTransport[128];
    if (pClient->bTcpInterleaved) {
        pszTransport = "RTP/AVP/TCP;unicast;interleaved=0-1";
    } else {
        snprintf(szTransport, sizeof(szTransport), "RTP/AVP/UDP;unicast;client_port=%u-%u", pClient->u16LocalRtpPort,
            pClient->u16LocalRtpPort + 1);
        pszTransport = szTransport;
    }

    s32Len = snprintf(szReq, sizeof(szReq),
        "SETUP %s RTSP/1.0\r\n"
        "CSeq: %u\r\n"
        "User-Agent: SpacemiT-MPP/1.0\r\n"
        "%s"
        "Transport: %s\r\n"
        "\r\n",
        szUri, pClient->u32CSeq++, szAuth, pszTransport);
    if (s32Len < 0 || (size_t)s32Len >= sizeof(szReq)) {
        return -1;
    }

    return Socket_SendAll(pClient->s32Fd, (U8 *)szReq, s32Len) > 0 ? 0 : -1;
}

static S32 rtsp_send_play(RtspClient *pClient) {
    CHAR szReq[4096];
    CHAR szAuth[2048];
    CHAR szUri[1024];
    S32 s32Len;

    s32Len = snprintf(
        szUri, sizeof(szUri), "rtsp://%s:%u%s", pClient->stUrl.szHost, pClient->stUrl.u16Port, pClient->stUrl.szPath);
    if (s32Len < 0 || (size_t)s32Len >= sizeof(szUri)) {
        return -1;
    }
    rtsp_get_auth_header(pClient, "PLAY", szUri, szAuth, sizeof(szAuth));

    s32Len = snprintf(szReq, sizeof(szReq),
        "PLAY %s RTSP/1.0\r\n"
        "CSeq: %u\r\n"
        "User-Agent: SpacemiT-MPP/1.0\r\n"
        "%s"
        "Session: %s\r\n"
        "Range: npt=0.000-\r\n"
        "\r\n",
        szUri, pClient->u32CSeq++, szAuth, pClient->szSession);
    if (s32Len < 0 || (size_t)s32Len >= sizeof(szReq)) {
        return -1;
    }

    return Socket_SendAll(pClient->s32Fd, (U8 *)szReq, s32Len) > 0 ? 0 : -1;
}

static S32 rtsp_send_teardown(RtspClient *pClient) { return rtsp_send_request(pClient, "TEARDOWN", ""); }

/* Returns: status code (200, 401, etc.) on success, negative on error */
static S32 rtsp_recv_response(RtspClient *pClient, CHAR *pszBuf, U32 u32MaxLen) {
    U32 u32Pos = 0;
    S32 s32ContentLen = 0;
    S32 s32StatusCode = 0;

    /* Read headers */
    while (u32Pos < u32MaxLen - 1) {
        S32 n = Socket_RecvTimeout(pClient->s32Fd, (U8 *)&pszBuf[u32Pos], 1, pClient->u32TimeoutMs);
        if (n <= 0)
            return -1;
        u32Pos++;

        /* Check for end of headers */
        if (u32Pos >= 4 && strncmp(&pszBuf[u32Pos - 4], "\r\n\r\n", 4) == 0) {
            break;
        }
    }
    pszBuf[u32Pos] = '\0';

    /* Parse status code from "RTSP/1.0 XXX" */
    const CHAR *pStatus = strstr(pszBuf, "RTSP/1.0 ");
    if (pStatus) {
        s32StatusCode = atoi(pStatus + 9);
    }

    /* Get Content-Length if present */
    const CHAR *pCL = strstr(pszBuf, "Content-Length:");
    if (!pCL)
        pCL = strstr(pszBuf, "Content-length:");
    if (pCL) {
        s32ContentLen = atoi(pCL + 15);
    }

    /* Read body */
    if (s32ContentLen > 0 && u32Pos + s32ContentLen < u32MaxLen) {
        if (Socket_RecvExact(pClient->s32Fd, (U8 *)&pszBuf[u32Pos], s32ContentLen, pClient->u32TimeoutMs) != 0) {
            return -1;
        }
        u32Pos += s32ContentLen;
        pszBuf[u32Pos] = '\0';
    }

    return s32StatusCode;
}
