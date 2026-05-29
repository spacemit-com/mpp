/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "h264_utils.h"

#include <string.h>

#include "common/bit_reader.h"

static const U8 START_CODE_3[] = {0x00, 0x00, 0x01};
static const U8 START_CODE_4[] = {0x00, 0x00, 0x00, 0x01};

S32 H264_FindStartCode(const U8 *pu8Data, U32 u32Len) {
    for (U32 i = 0; i + 2 < u32Len; i++) {
        if (pu8Data[i] == 0x00 && pu8Data[i + 1] == 0x00) {
            if (pu8Data[i + 2] == 0x01) {
                return i;
            }
            if (i + 3 < u32Len && pu8Data[i + 2] == 0x00 && pu8Data[i + 3] == 0x01) {
                return i;
            }
        }
    }
    return -1;
}

S32 H264_ParseSps(const U8 *pu8Sps, U32 u32Len, U32 *pu32Width, U32 *pu32Height) {
    if (!pu8Sps || u32Len < 4 || !pu32Width || !pu32Height)
        return -1;

    BitReader br;
    BitReader_Init(&br, pu8Sps + 1, u32Len - 1); /* Skip NAL header */

    /* profile_idc */
    U32 profile = BitReader_ReadBits(&br, 8);

    /* constraint_set flags + reserved + level_idc */
    BitReader_Skip(&br, 16);

    /* seq_parameter_set_id */
    BitReader_ReadUE(&br);

    /* Handle high profile */
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 || profile == 83 ||
        profile == 86 || profile == 118 || profile == 128) {
        U32 chroma_format = BitReader_ReadUE(&br);
        if (chroma_format == 3) {
            BitReader_Skip(&br, 1); /* separate_colour_plane_flag */
        }
        BitReader_ReadUE(&br);  /* bit_depth_luma */
        BitReader_ReadUE(&br);  /* bit_depth_chroma */
        BitReader_Skip(&br, 1); /* qpprime_y_zero_transform_bypass */

        U32 seq_scaling = BitReader_ReadBits(&br, 1);
        if (seq_scaling) {
            for (int i = 0; i < ((chroma_format != 3) ? 8 : 12); i++) {
                U32 present = BitReader_ReadBits(&br, 1);
                if (present) {
                    /* Skip scaling list */
                    int size = (i < 6) ? 16 : 64;
                    int last = 8, next = 8;
                    for (int j = 0; j < size; j++) {
                        if (next != 0) {
                            S32 delta = BitReader_ReadSE(&br);
                            next = (last + delta + 256) % 256;
                        }
                        last = (next == 0) ? last : next;
                    }
                }
            }
        }
    }

    /* log2_max_frame_num */
    BitReader_ReadUE(&br);

    /* pic_order_cnt_type */
    U32 poc_type = BitReader_ReadUE(&br);
    if (poc_type == 0) {
        BitReader_ReadUE(&br); /* log2_max_pic_order_cnt_lsb */
    } else if (poc_type == 1) {
        BitReader_Skip(&br, 1);
        BitReader_ReadSE(&br);
        BitReader_ReadSE(&br);
        U32 num_ref = BitReader_ReadUE(&br);
        for (U32 i = 0; i < num_ref; i++) {
            BitReader_ReadSE(&br);
        }
    }

    /* max_num_ref_frames */
    BitReader_ReadUE(&br);

    /* gaps_in_frame_num_value_allowed */
    BitReader_Skip(&br, 1);

    /* pic_width_in_mbs_minus1 */
    U32 pic_width_mbs = BitReader_ReadUE(&br) + 1;

    /* pic_height_in_map_units_minus1 */
    U32 pic_height_map = BitReader_ReadUE(&br) + 1;

    /* frame_mbs_only_flag */
    U32 frame_mbs_only = BitReader_ReadBits(&br, 1);
    if (!frame_mbs_only) {
        BitReader_Skip(&br, 1); /* mb_adaptive_frame_field */
    }

    /* direct_8x8_inference_flag */
    BitReader_Skip(&br, 1);

    /* frame_cropping */
    U32 crop_left = 0, crop_right = 0, crop_top = 0, crop_bottom = 0;
    U32 frame_crop = BitReader_ReadBits(&br, 1);
    if (frame_crop) {
        crop_left = BitReader_ReadUE(&br);
        crop_right = BitReader_ReadUE(&br);
        crop_top = BitReader_ReadUE(&br);
        crop_bottom = BitReader_ReadUE(&br);
    }

    /* Calculate dimensions */
    *pu32Width = pic_width_mbs * 16 - (crop_left + crop_right) * 2;
    *pu32Height = (2 - frame_mbs_only) * pic_height_map * 16 - (crop_top + crop_bottom) * 2;

    return 0;
}

S32 H264_AnnexBToAvcc(const U8 *pu8In, U32 u32InLen, U8 *pu8Out, U32 *pu32OutLen) {
    if (!pu8In || !pu8Out || !pu32OutLen)
        return -1;

    U32 u32OutPos = 0;
    U32 u32Pos = 0;

    while (u32Pos < u32InLen) {
        /* Find start code */
        S32 scPos = H264_FindStartCode(pu8In + u32Pos, u32InLen - u32Pos);
        if (scPos < 0)
            break;

        u32Pos += scPos;

        /* Determine start code length (3 or 4) */
        U32 scLen = (pu8In[u32Pos + 2] == 0x01) ? 3 : 4;
        u32Pos += scLen;

        /* Find next start code or end */
        S32 nextSc = H264_FindStartCode(pu8In + u32Pos, u32InLen - u32Pos);
        U32 nalLen = (nextSc >= 0) ? (U32)nextSc : (u32InLen - u32Pos);

        /* Remove trailing zeros */
        while (nalLen > 0 && pu8In[u32Pos + nalLen - 1] == 0x00) {
            nalLen--;
        }

        /* Write length prefix (4 bytes big endian) */
        pu8Out[u32OutPos++] = (nalLen >> 24) & 0xFF;
        pu8Out[u32OutPos++] = (nalLen >> 16) & 0xFF;
        pu8Out[u32OutPos++] = (nalLen >> 8) & 0xFF;
        pu8Out[u32OutPos++] = nalLen & 0xFF;

        /* Write NAL data */
        memcpy(pu8Out + u32OutPos, pu8In + u32Pos, nalLen);
        u32OutPos += nalLen;

        u32Pos += nalLen;
    }

    *pu32OutLen = u32OutPos;
    return 0;
}

S32 H264_AvccToAnnexB(const U8 *pu8In, U32 u32InLen, U8 *pu8Out, U32 *pu32OutLen, U8 u8NalLenSize) {
    if (!pu8In || !pu8Out || !pu32OutLen)
        return -1;
    if (u8NalLenSize != 4 && u8NalLenSize != 3 && u8NalLenSize != 2 && u8NalLenSize != 1) {
        u8NalLenSize = 4;
    }

    U32 u32OutPos = 0;
    U32 u32Pos = 0;

    while (u32Pos + u8NalLenSize <= u32InLen) {
        /* Read NAL length */
        U32 nalLen = 0;
        for (int i = 0; i < u8NalLenSize; i++) {
            nalLen = (nalLen << 8) | pu8In[u32Pos + i];
        }
        u32Pos += u8NalLenSize;

        if (u32Pos + nalLen > u32InLen)
            break;

        /* Write start code */
        memcpy(pu8Out + u32OutPos, START_CODE_4, 4);
        u32OutPos += 4;

        /* Write NAL data */
        memcpy(pu8Out + u32OutPos, pu8In + u32Pos, nalLen);
        u32OutPos += nalLen;

        u32Pos += nalLen;
    }

    *pu32OutLen = u32OutPos;
    return 0;
}
