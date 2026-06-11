/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *
 * @File      :    mux_common.c
 * @Date      :    2026-06-11
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Shared helpers for MUX backends.
 *------------------------------------------------------------------------------
 */

#include "mux_common.h"

#include <stdio.h>
#include <string.h>

#include "codec/h264_utils.h"
#include "codec/h265_utils.h"

/**
 * MuxFindStartCode - Locate the first Annex-B start code in a byte buffer.
 *
 * Returns the byte offset of the first byte of the start code pattern:
 *   - 3-byte (00 00 01): offset of the first 00, *pu32Prefix = 3.
 *   - 4-byte (00 00 00 01): offset of the leading 00, *pu32Prefix = 4.
 *
 * Design:
 *   - A pre-loop check handles the 4-byte start code at offset 0 (the main
 *     loop's i-1 look-back cannot reach behind index 0).
 *   - The main loop handles all start codes at offset >= 0.  When it finds
 *     00 00 01 at position i, it checks pu8Data[i-1] == 0x00 (for i >= 1)
 *     to detect the 4-byte variant.
 *
 * @return >= 0 offset on success, -1 if no start code is found.
 */
S32 MuxFindStartCode(const U8 *pu8Data, U32 u32Size, U32 *pu32Prefix) {
    U32 i;

    if (!pu8Data || u32Size < 3) {
        return -1;
    }

    /* Detect 4-byte start code at buffer position 0 (the main loop's i-1
     * look-back cannot reach behind index 0). */
    if (u32Size >= 4 &&
        pu8Data[0] == 0x00 && pu8Data[1] == 0x00 &&
        pu8Data[2] == 0x00 && pu8Data[3] == 0x01) {
        if (pu32Prefix) {
            *pu32Prefix = 4;
        }
        return 0;
    }

    for (i = 0; i + 3 <= u32Size; ++i) {
        if (pu8Data[i] == 0x00 && pu8Data[i + 1] == 0x00 && pu8Data[i + 2] == 0x01) {
            if (i >= 1 && pu8Data[i - 1] == 0x00) {
                /* 4-byte start code at offset (i-1). */
                if (pu32Prefix) {
                    *pu32Prefix = 4;
                }
                return (S32)(i - 1);
            }
            if (pu32Prefix) {
                *pu32Prefix = 3;
            }
            return (S32)i;
        }
    }

    return -1;
}

S32 MuxNextNal(const U8 *pu8Data, U32 u32Size, U32 *pu32Off, MuxCodecType eCodec, MuxNalUnit *pstNal) {
    U32 u32Prefix = 0;
    U32 u32NextPrefix = 0;
    U32 u32Start = 0;
    S32 s32Start;
    S32 s32Next;
    const U8 *pu8Scan;
    const U8 *pu8NalStart;
    const U8 *pu8NalEnd;
    U32 u32Remain;

    (void)eCodec;

    if (!pu8Data || !pu32Off || !pstNal || *pu32Off >= u32Size) {
        return -1;
    }

    pu8Scan = pu8Data + *pu32Off;
    u32Remain = u32Size - *pu32Off;

    s32Start = MuxFindStartCode(pu8Scan, u32Remain, &u32Prefix);
    if (s32Start < 0) {
        *pu32Off = u32Size;
        return -1;
    }

    /* Defensive: MuxFindStartCode always returns >= 0 here, but guard
     * against any future change that might yield a negative offset. */
    u32Start = (U32)s32Start;

    /* Move past the start code to the NAL header byte. Keep the absolute NAL
     * pointer and derive all offsets from pu8Data, so leading zero bytes before
     * a 4-byte start code never get folded into the previous NAL size. */
    pu8NalStart = pu8Scan + u32Start + u32Prefix;
    u32Remain = u32Size - (U32)(pu8NalStart - pu8Data);
    if (u32Remain == 0) {
        *pu32Off = u32Size;
        return -1;
    }

    /* Find the next start code to bound this NAL unit. The returned offset
     * is relative to pu8NalStart; for a 4-byte start code MuxFindStartCode
     * returns the position of the leading 00 (prefix=4). Set *pu32Off to
     * that absolute position so the next MuxNextNal call starts exactly at
     * the next start code boundary — MuxFindStartCode will re-discover it
     * at offset 0 with the correct prefix. */
    s32Next = MuxFindStartCode(pu8NalStart, u32Remain, &u32NextPrefix);
    if (s32Next < 0) {
        pu8NalEnd = pu8Data + u32Size;
        *pu32Off = u32Size;
    } else {
        pu8NalEnd = pu8NalStart + (U32)s32Next;
        *pu32Off = (U32)(pu8NalEnd - pu8Data);
    }
    /* Strip trailing zero-padding bytes that may sit between the NAL payload
     * and the next start code. These zeros are NOT part of the NAL content
     * but are part of the start code prefix for the next NAL (they were
     * already accounted for in *pu32Off above). */
    while (pu8NalEnd > pu8NalStart && pu8NalEnd[-1] == 0x00) {
        pu8NalEnd--;
    }

    pstNal->pu8Data = pu8NalStart;
    pstNal->u32Size = (U32)(pu8NalEnd - pu8NalStart);
    if (eCodec == MUX_CODEC_H265) {
        pstNal->u8Type = H265_GetNalType(pu8NalStart);
    } else {
        pstNal->u8Type = H264_GetNalType(pu8NalStart[0]);
    }
    return 0;
}

S32 MuxAnnexBToAvccVcl(const U8 *pu8In, U32 u32InLen, MuxCodecType eCodec, U8 *pu8Out, U32 u32OutCap,
    U32 *pu32OutLen) {
    U32 u32Off = 0;
    U32 u32OutPos = 0;
    MuxNalUnit stNal;

    if (!pu8In || !pu8Out || !pu32OutLen) {
        return -1;
    }

    while (MuxNextNal(pu8In, u32InLen, &u32Off, eCodec, &stNal) == 0) {
        /* H265_IsVcl: NAL types 0-31 are VCL per ISO/IEC 23008-2 Table 7-1.
         * All non-VCL (VPS=32, SPS=33, PPS=34, SEI=39/40, etc.) are >31. */
        BOOL bVcl = (eCodec == MUX_CODEC_H265) ? H265_IsVcl(stNal.u8Type) : H264_IsVcl(stNal.u8Type);

        if (!bVcl || stNal.u32Size == 0) {
            continue;
        }
        if (u32OutPos + 4 + stNal.u32Size > u32OutCap) {
            return -1;
        }

        MuxPutBe32(pu8Out + u32OutPos, stNal.u32Size);
        u32OutPos += 4;
        memcpy(pu8Out + u32OutPos, stNal.pu8Data, stNal.u32Size);
        u32OutPos += stNal.u32Size;
    }

    *pu32OutLen = u32OutPos;
    return 0;
}

S32 MuxCollectParamSets(const U8 *pu8Data, U32 u32Size, MuxCodecType eCodec, MuxParamSets *pstSets) {
    U32 u32Off = 0;
    MuxNalUnit stNal;

    if (!pu8Data || !pstSets) {
        return -1;
    }

    while (MuxNextNal(pu8Data, u32Size, &u32Off, eCodec, &stNal) == 0) {
        if (stNal.u32Size == 0 || stNal.u32Size > MUX_PARAM_SET_MAX_SIZE) {
            continue;
        }

        if (eCodec == MUX_CODEC_H265) {
            if (stNal.u8Type == H265_NAL_VPS) {
                memcpy(pstSets->au8Vps, stNal.pu8Data, stNal.u32Size);
                pstSets->u32VpsLen = stNal.u32Size;
            } else if (stNal.u8Type == H265_NAL_SPS) {
                memcpy(pstSets->au8Sps, stNal.pu8Data, stNal.u32Size);
                pstSets->u32SpsLen = stNal.u32Size;
            } else if (stNal.u8Type == H265_NAL_PPS) {
                memcpy(pstSets->au8Pps, stNal.pu8Data, stNal.u32Size);
                pstSets->u32PpsLen = stNal.u32Size;
            }
        } else {
            if (stNal.u8Type == H264_NAL_SPS) {
                memcpy(pstSets->au8Sps, stNal.pu8Data, stNal.u32Size);
                pstSets->u32SpsLen = stNal.u32Size;
            } else if (stNal.u8Type == H264_NAL_PPS) {
                memcpy(pstSets->au8Pps, stNal.pu8Data, stNal.u32Size);
                pstSets->u32PpsLen = stNal.u32Size;
            }
        }
    }

    return 0;
}

S32 MuxHevcExtractPtl(const U8 *pu8Sps, U32 u32SpsLen, U8 *pu8Ptl) {
    U8 au8Rbsp[32];
    U32 u32Out = 0;
    U32 u32Zeros = 0;
    U32 i;

    if (!pu8Sps || !pu8Ptl || u32SpsLen < 4) {
        return -1;
    }

    /* De-emulate just enough RBSP to cover the NAL header (2 bytes), the byte
     * holding sps_video_parameter_set_id/sps_max_sub_layers_minus1/
     * sps_temporal_id_nesting_flag (1 byte) and the 12-byte general
     * profile_tier_level block, i.e. 15 bytes total. Stop early once we have
     * enough so a long SPS never overflows the small scratch buffer.
     *
     * RBSP emulation-prevention (ITU-T H.265 §7.4.2.2):
     * Within a NAL unit, the byte sequence 00 00 03 XX (where XX ∈ {00, 01,
     * 02, 03}) is an escape: the 0x03 byte is removed during decoding. The
     * condition `pu8Sps[i + 1] <= 0x03` correctly identifies the escape per
     * spec — any other trailing byte means the 0x03 is literal data, not an
     * emulation-prevention byte.  The constraint-flag values 0x00/0x01 in the
     * PTL block do NOT trigger false removal because they only appear AFTER
     * de-emulation; in the encoded bitstream, any 00 00 00/01 within a NAL
     * unit will have been escaped with an intervening 03 by the encoder. */
    for (i = 0; i < u32SpsLen && u32Out < sizeof(au8Rbsp); ++i) {
        U8 b = pu8Sps[i];
        if (u32Zeros >= 2 && b == 0x03 && (i + 1 < u32SpsLen) && pu8Sps[i + 1] <= 0x03) {
            u32Zeros = 0; /* drop the emulation-prevention byte */
            continue;
        }
        au8Rbsp[u32Out++] = b;
        u32Zeros = (b == 0x00) ? (u32Zeros + 1) : 0;
    }

    /* Layout after de-emulation: [0..1] NAL header, [2] sps ids/flags byte,
     * [3..14] the 12-byte general profile_tier_level fixed block. */
    if (u32Out < 3 + MUX_HEVC_PTL_LEN) {
        return -1;
    }
    memcpy(pu8Ptl, au8Rbsp + 3, MUX_HEVC_PTL_LEN);
    return 0;
}
