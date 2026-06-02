/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "h265_utils.h"

#include <string.h>

#include "common/bit_reader.h"

/* Strip H.265 emulation prevention bytes: any 0x000003 sequence has the 0x03
 * removed to recover the raw RBSP. Returns the RBSP length written to pu8Out. */
static U32 nal_to_rbsp(const U8 *pu8In, U32 u32InLen, U8 *pu8Out, U32 u32OutCap) {
    U32 u32OutLen = 0;
    U32 u32Zeros = 0;

    for (U32 i = 0; i < u32InLen; i++) {
        U8 b = pu8In[i];
        if (u32Zeros >= 2 && b == 0x03 && (i + 1 < u32InLen) && pu8In[i + 1] <= 0x03) {
            u32Zeros = 0;
            continue;
        }
        if (u32OutLen >= u32OutCap)
            break;
        pu8Out[u32OutLen++] = b;
        u32Zeros = (b == 0x00) ? (u32Zeros + 1) : 0;
    }
    return u32OutLen;
}

S32 H265_ParseSps(const U8 *pu8Sps, U32 u32Len, U32 *pu32Width, U32 *pu32Height) {
    if (!pu8Sps || u32Len < 4 || !pu32Width || !pu32Height)
        return -1;

    /* Strip emulation prevention bytes before bitstream parsing. The 2-byte
     * NAL header is preserved at indices 0..1 and skipped by BitReader_Init. */
    U8 au8Rbsp[512];
    U32 u32CopyLen = (u32Len < sizeof(au8Rbsp)) ? u32Len : sizeof(au8Rbsp);
    U32 u32RbspLen = nal_to_rbsp(pu8Sps, u32CopyLen, au8Rbsp, sizeof(au8Rbsp));
    if (u32RbspLen < 4)
        return -1;

    BitReader br;
    BitReader_Init(&br, au8Rbsp + 2, u32RbspLen - 2); /* Skip NAL header (2 bytes) */

    /* sps_video_parameter_set_id */
    BitReader_ReadBits(&br, 4);

    /* sps_max_sub_layers_minus1 */
    U32 maxSubLayers = BitReader_ReadBits(&br, 3);

    /* sps_temporal_id_nesting_flag */
    BitReader_Skip(&br, 1);

    /* profile_tier_level( 1, sps_max_sub_layers_minus1 ) per H.265 §7.3.3.
     * general_profile_space(2) + general_tier_flag(1) + general_profile_idc(5) */
    BitReader_Skip(&br, 2 + 1 + 5);
    /* general_profile_compatibility_flags */
    BitReader_Skip(&br, 32);
    /* general_progressive_source_flag ... + general_reserved_zero_43bits +
     * general_inbld_flag/reserved (48 bits total) */
    BitReader_Skip(&br, 48);
    /* general_level_idc */
    BitReader_Skip(&br, 8);

    /* For each sub-layer below the top one, read the two presence flags. The
     * spec then, for i in [maxSubLayers, 8), inserts reserved_zero_2bits to
     * byte-align the flag block before the optional sub-layer PTL data. */
    U8 au8SubProfilePresent[8] = {0};
    U8 au8SubLevelPresent[8] = {0};
    for (U32 i = 0; i < maxSubLayers; i++) {
        au8SubProfilePresent[i] = (U8)BitReader_ReadBits(&br, 1);
        au8SubLevelPresent[i] = (U8)BitReader_ReadBits(&br, 1);
    }
    if (maxSubLayers > 0) {
        for (U32 i = maxSubLayers; i < 8; i++) {
            BitReader_Skip(&br, 2); /* reserved_zero_2bits */
        }
    }
    /* Now skip the variable-length per-sub-layer profile/level fields that the
     * previous implementation ignored. Missing these shifts every subsequent
     * field (chroma_format_idc, pic_width/height_in_luma_samples), which is
     * why multi-sub-layer HEVC decoded with wrong dimensions. */
    for (U32 i = 0; i < maxSubLayers; i++) {
        if (au8SubProfilePresent[i]) {
            /* sub_layer_profile_space(2)+tier_flag(1)+profile_idc(5) +
             * profile_compatibility_flags(32) + constraint/reserved(48) */
            BitReader_Skip(&br, 2 + 1 + 5);
            BitReader_Skip(&br, 32);
            BitReader_Skip(&br, 48);
        }
        if (au8SubLevelPresent[i]) {
            BitReader_Skip(&br, 8); /* sub_layer_level_idc */
        }
    }

    /* sps_seq_parameter_set_id */
    BitReader_ReadUE(&br);

    /* chroma_format_idc */
    U32 chromaFormat = BitReader_ReadUE(&br);
    if (chromaFormat == 3) {
        BitReader_Skip(&br, 1); /* separate_colour_plane_flag */
    }

    /* pic_width_in_luma_samples */
    U32 picWidth = BitReader_ReadUE(&br);

    /* pic_height_in_luma_samples */
    U32 picHeight = BitReader_ReadUE(&br);

    /* conformance_window_flag */
    U32 confWin = BitReader_ReadBits(&br, 1);
    if (confWin) {
        U32 subWidthC = (chromaFormat == 1 || chromaFormat == 2) ? 2 : 1;
        U32 subHeightC = (chromaFormat == 1) ? 2 : 1;

        U32 leftOff = BitReader_ReadUE(&br);
        U32 rightOff = BitReader_ReadUE(&br);
        U32 topOff = BitReader_ReadUE(&br);
        U32 bottomOff = BitReader_ReadUE(&br);

        U64 widthOffset = (U64)(leftOff + rightOff) * subWidthC;
        U64 heightOffset = (U64)(topOff + bottomOff) * subHeightC;
        if (widthOffset > picWidth || heightOffset > picHeight) {
            return -1;
        }

        picWidth -= (U32)widthOffset;
        picHeight -= (U32)heightOffset;
    }

    *pu32Width = picWidth;
    *pu32Height = picHeight;

    return 0;
}
