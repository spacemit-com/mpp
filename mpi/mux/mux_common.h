/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_common.h
 * @Date      :    2026-06-11
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Shared helpers for MUX recording/streaming backends:
 *                 big-endian writers and Annex-B NAL iteration.
 *------------------------------------------------------------------------------
 */

#ifndef MUX_COMMON_H
#define MUX_COMMON_H

#include <stdio.h>
#include "mux/mux_type.h"
#include "sys/type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

#ifndef MUX_LOGE
#define MUX_LOGE(fmt, ...) fprintf(stderr, "[MUX][ERR] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef MUX_LOGW
#define MUX_LOGW(fmt, ...) fprintf(stderr, "[MUX][WRN] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef MUX_LOGI
#define MUX_LOGI(fmt, ...) fprintf(stdout, "[MUX][INF] " fmt "\n", ##__VA_ARGS__)
#endif

/* Single NAL unit referenced inside an Annex-B access unit. */
typedef struct _MuxNalUnit {
    const U8 *pu8Data; /* points to NAL header byte (after start code) */
    U32 u32Size;       /* NAL payload size, excluding start code */
    U8 u8Type;         /* codec-specific NAL type */
} MuxNalUnit;

/* Maximum size of a single parameter set (VPS/SPS/PPS). 4K H265 SPS can
 * exceed 256 bytes, so keep this generous. The NAL collector limit and the
 * buffers below must stay in sync, hence the shared macro. */
#define MUX_PARAM_SET_MAX_SIZE 512

/* Parsed parameter sets extracted from an Annex-B access unit. */
typedef struct _MuxParamSets {
    U8 au8Vps[MUX_PARAM_SET_MAX_SIZE];
    U32 u32VpsLen;
    U8 au8Sps[MUX_PARAM_SET_MAX_SIZE];
    U32 u32SpsLen;
    U8 au8Pps[MUX_PARAM_SET_MAX_SIZE];
    U32 u32PpsLen;
} MuxParamSets;

/* ---- Big-endian byte writers (return number of bytes written) ---- */
static inline U32 MuxPutU8(U8 *p, U8 v) {
    p[0] = v;
    return 1;
}

static inline U32 MuxPutBe16(U8 *p, U16 v) {
    p[0] = (U8)(v >> 8);
    p[1] = (U8)(v);
    return 2;
}

static inline U32 MuxPutBe24(U8 *p, U32 v) {
    p[0] = (U8)(v >> 16);
    p[1] = (U8)(v >> 8);
    p[2] = (U8)(v);
    return 3;
}

static inline U32 MuxPutBe32(U8 *p, U32 v) {
    p[0] = (U8)(v >> 24);
    p[1] = (U8)(v >> 16);
    p[2] = (U8)(v >> 8);
    p[3] = (U8)(v);
    return 4;
}

static inline U32 MuxPutBe64(U8 *p, U64 v) {
    MuxPutBe32(p, (U32)(v >> 32));
    MuxPutBe32(p + 4, (U32)(v & 0xFFFFFFFFu));
    return 8;
}

/* FourCC packed big-endian, e.g. MUX_FOURCC('m','o','o','f'). */
#define MUX_FOURCC(a, b, c, d) \
    (((U32)(a) << 24) | ((U32)(b) << 16) | ((U32)(c) << 8) | ((U32)(d)))

/**
 * @brief  Find the next Annex-B start code (00 00 01 or 00 00 00 01).
 * @param  pu8Data    buffer
 * @param  u32Size    buffer size
 * @param  pu32Prefix [out] start code length found (3 or 4)
 * @return offset to first byte of the start code, or -1 if none.
 */
S32 MuxFindStartCode(const U8 *pu8Data, U32 u32Size, U32 *pu32Prefix);

/**
 * @brief  Iterate Annex-B NAL units in an access unit.
 *
 * Typical loop:
 * @code
 *   U32 off = 0;
 *   MuxNalUnit nal;
 *   while (MuxNextNal(pu8Data, u32Size, &off, eCodec, &nal) == 0) { ... }
 * @endcode
 *
 * @param  pu8Data   Annex-B buffer
 * @param  u32Size   buffer size
 * @param  pu32Off   [in/out] scan cursor, init to 0
 * @param  eCodec    MUX_CODEC_H264 / MUX_CODEC_H265
 * @param  pstNal    [out] next NAL unit
 * @return 0 on success, -1 when no more NAL units.
 */
S32 MuxNextNal(const U8 *pu8Data, U32 u32Size, U32 *pu32Off, MuxCodecType eCodec, MuxNalUnit *pstNal);

/**
 * @brief  Convert Annex-B access unit to length-prefixed AVCC/HVCC payload,
 *         keeping only VCL NAL units. Parameter sets stay in avcC/hvcC or
 *         RTMP sequence headers and are not duplicated in media payloads.
 */
S32 MuxAnnexBToAvccVcl(const U8 *pu8In, U32 u32InLen, MuxCodecType eCodec, U8 *pu8Out, U32 u32OutCap,
    U32 *pu32OutLen);

/**
 * @brief  Extract VPS/SPS/PPS from an Annex-B access unit into pstSets.
 *         Fields not present in the access unit are left untouched, so it
 *         may be called repeatedly to accumulate parameter sets.
 * @return 0 always (best-effort).
 */
S32 MuxCollectParamSets(const U8 *pu8Data, U32 u32Size, MuxCodecType eCodec, MuxParamSets *pstSets);

/* Number of bytes in the H.265 general profile_tier_level fixed block:
 * profile_space/tier/idc(1) + profile_compatibility_flags(4) +
 * constraint_indicator_flags(6) + general_level_idc(1). */
#define MUX_HEVC_PTL_LEN 12

/**
 * @brief  Extract the 12-byte general profile_tier_level block from an H.265
 *         SPS so an hvcC record can advertise the stream's real
 *         profile/tier/level/constraints instead of placeholder constants.
 *         The SPS must be the raw Annex-B NAL payload (2-byte NAL header
 *         included); emulation-prevention bytes are stripped internally.
 * @param  pu8Sps   SPS NAL payload (starting at the 2-byte NAL header).
 * @param  u32SpsLen Length of the SPS NAL payload.
 * @param  pu8Ptl   Output buffer receiving MUX_HEVC_PTL_LEN bytes.
 * @return 0 on success, -1 if the SPS is too short to contain the PTL block.
 */
S32 MuxHevcExtractPtl(const U8 *pu8Sps, U32 u32SpsLen, U8 *pu8Ptl);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* MUX_COMMON_H */
