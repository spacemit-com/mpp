/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "h265_utils.h"

#include <string.h>

#include "common/bit_reader.h"

S32 H265_ParseSps(const U8 *pu8Sps, U32 u32Len, U32 *pu32Width, U32 *pu32Height) {
    if (!pu8Sps || u32Len < 4 || !pu32Width || !pu32Height)
        return -1;

    BitReader br;
    BitReader_Init(&br, pu8Sps + 2, u32Len - 2); /* Skip NAL header (2 bytes) */

    /* sps_video_parameter_set_id */
    BitReader_ReadBits(&br, 4);

    /* sps_max_sub_layers_minus1 */
    U32 maxSubLayers = BitReader_ReadBits(&br, 3);

    /* sps_temporal_id_nesting_flag */
    BitReader_Skip(&br, 1);

    /* profile_tier_level */
    /* general_profile_space, tier_flag, profile_idc */
    BitReader_Skip(&br, 2 + 1 + 5);
    /* general_profile_compatibility_flags */
    BitReader_Skip(&br, 32);
    /* general_progressive_source_flag ... */
    BitReader_Skip(&br, 48);
    /* general_level_idc */
    BitReader_Skip(&br, 8);

    /* sub_layer flags */
    for (U32 i = 0; i < maxSubLayers; i++) {
        BitReader_Skip(&br, 2);
    }
    if (maxSubLayers > 0) {
        for (U32 i = maxSubLayers; i < 8; i++) {
            BitReader_Skip(&br, 2);
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

        picWidth -= (leftOff + rightOff) * subWidthC;
        picHeight -= (topOff + bottomOff) * subHeightC;
    }

    *pu32Width = picWidth;
    *pu32Height = picHeight;

    return 0;
}
