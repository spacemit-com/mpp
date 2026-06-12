/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <string.h>

#include "mux_common.h"

#define CHECK_TRUE(cond) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            return 1; \
        } \
    } while (0)

static int test_next_nal_skips_leading_zero_and_trims_prefix_padding(void) {
    const U8 au8Data[] = {
        0x00, 0x00, 0x00, 0x00, 0x01, 0x67, 0x11,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x22
    };
    U32 u32Off = 0;
    MuxNalUnit stNal;

    CHECK_TRUE(MuxNextNal(au8Data, sizeof(au8Data), &u32Off, MUX_CODEC_H264, &stNal) == 0);
    CHECK_TRUE(stNal.pu8Data == &au8Data[5]);
    CHECK_TRUE(stNal.u32Size == 2);
    CHECK_TRUE(stNal.u8Type == 7);
    CHECK_TRUE(u32Off == 7);

    CHECK_TRUE(MuxNextNal(au8Data, sizeof(au8Data), &u32Off, MUX_CODEC_H264, &stNal) == 0);
    CHECK_TRUE(stNal.pu8Data == &au8Data[11]);
    CHECK_TRUE(stNal.u32Size == 2);
    CHECK_TRUE(stNal.u8Type == 5);
    CHECK_TRUE(u32Off == sizeof(au8Data));

    return 0;
}

static int test_find_start_code_4byte_at_offset_zero(void) {
    /* Buffer starts directly with 00 00 00 01 — MuxFindStartCode must return
     * offset 0 with prefix 4, not confuse it with a 3-byte start code. */
    const U8 au8Data[] = {0x00, 0x00, 0x00, 0x01, 0x65, 0xAA};
    U32 u32Prefix = 0;
    S32 s32Off;

    s32Off = MuxFindStartCode(au8Data, sizeof(au8Data), &u32Prefix);
    CHECK_TRUE(s32Off == 0);
    CHECK_TRUE(u32Prefix == 4);

    return 0;
}

static int test_next_nal_4byte_start_code_at_buffer_start(void) {
    /* Ensure MuxNextNal does not infinitely loop when the buffer starts
     * with a 4-byte start code (00 00 00 01). Two consecutive NALs, both
     * using 4-byte start codes. */
    const U8 au8Data[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x11, 0x22,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x33
    };
    U32 u32Off = 0;
    MuxNalUnit stNal;

    /* First NAL: SPS (type 7) at offset 4, payload = 0x67 0x11 0x22 */
    CHECK_TRUE(MuxNextNal(au8Data, sizeof(au8Data), &u32Off,
        MUX_CODEC_H264, &stNal) == 0);
    CHECK_TRUE(stNal.pu8Data == &au8Data[4]);
    CHECK_TRUE(stNal.u32Size == 3);
    CHECK_TRUE(stNal.u8Type == 7);
    /* u32Off must advance past first NAL to next start code boundary */
    CHECK_TRUE(u32Off == 7);

    /* Second NAL: IDR (type 5) at offset 11, payload = 0x65 0x33 */
    CHECK_TRUE(MuxNextNal(au8Data, sizeof(au8Data), &u32Off,
        MUX_CODEC_H264, &stNal) == 0);
    CHECK_TRUE(stNal.pu8Data == &au8Data[11]);
    CHECK_TRUE(stNal.u32Size == 2);
    CHECK_TRUE(stNal.u8Type == 5);
    CHECK_TRUE(u32Off == sizeof(au8Data));

    /* No more NALs */
    CHECK_TRUE(MuxNextNal(au8Data, sizeof(au8Data), &u32Off,
        MUX_CODEC_H264, &stNal) < 0);

    return 0;
}

static int test_h264_vcl_conversion_filters_parameter_sets(void) {
    const U8 au8Data[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x11,
        0x00, 0x00, 0x01, 0x68, 0x22,
        0x00, 0x00, 0x01, 0x65, 0x33
    };
    const U8 au8Expected[] = {0x00, 0x00, 0x00, 0x02, 0x65, 0x33};
    U8 au8Out[32];
    U32 u32OutLen = 0;

    CHECK_TRUE(MuxAnnexBToAvccVcl(au8Data, sizeof(au8Data), MUX_CODEC_H264, au8Out, sizeof(au8Out),
        &u32OutLen) == 0);
    CHECK_TRUE(u32OutLen == sizeof(au8Expected));
    CHECK_TRUE(memcmp(au8Out, au8Expected, sizeof(au8Expected)) == 0);

    return 0;
}

static int test_h265_vcl_conversion_filters_parameter_sets(void) {
    const U8 au8Data[] = {
        0x00, 0x00, 0x01, 0x40, 0x01, 0xaa,
        0x00, 0x00, 0x01, 0x42, 0x01, 0xbb,
        0x00, 0x00, 0x01, 0x44, 0x01, 0xcc,
        0x00, 0x00, 0x01, 0x26, 0x01, 0xdd
    };
    const U8 au8Expected[] = {0x00, 0x00, 0x00, 0x03, 0x26, 0x01, 0xdd};
    U8 au8Out[32];
    U32 u32OutLen = 0;

    CHECK_TRUE(MuxAnnexBToAvccVcl(au8Data, sizeof(au8Data), MUX_CODEC_H265, au8Out, sizeof(au8Out),
        &u32OutLen) == 0);
    CHECK_TRUE(u32OutLen == sizeof(au8Expected));
    CHECK_TRUE(memcmp(au8Out, au8Expected, sizeof(au8Expected)) == 0);

    return 0;
}

int main(void) {
    CHECK_TRUE(test_next_nal_skips_leading_zero_and_trims_prefix_padding() == 0);
    CHECK_TRUE(test_find_start_code_4byte_at_offset_zero() == 0);
    CHECK_TRUE(test_next_nal_4byte_start_code_at_buffer_start() == 0);
    CHECK_TRUE(test_h264_vcl_conversion_filters_parameter_sets() == 0);
    CHECK_TRUE(test_h265_vcl_conversion_filters_parameter_sets() == 0);
    return 0;
}
