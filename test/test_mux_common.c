/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file.
 *------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mux_common.h"
#include "mux_writer.h"

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

static U32 read_be32(const U8 *pData) {
    return ((U32)pData[0] << 24) | ((U32)pData[1] << 16) |
        ((U32)pData[2] << 8) | pData[3];
}

static int find_box(const U8 *pData, U32 u32Start, U32 u32End,
    U32 u32Type, U32 *pu32Offset, U32 *pu32Size) {
    U32 u32Pos = u32Start;

    while (u32End - u32Pos >= 8) {
        U32 u32Size = read_be32(pData + u32Pos);
        U32 u32BoxType = read_be32(pData + u32Pos + 4);
        if (u32Size < 8 || u32Size > u32End - u32Pos)
            return -1;
        if (u32BoxType == u32Type) {
            *pu32Offset = u32Pos;
            *pu32Size = u32Size;
            return 0;
        }
        u32Pos += u32Size;
    }
    return -1;
}

static int test_mp4_trun_flags_match_serialized_fields(void) {
    static const U8 au8KeyFrame[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e,
        0x00, 0x00, 0x00, 0x01, 0x68, 0xce, 0x06, 0xe2,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84,
    };
    FILE *pFile = tmpfile();
    MuxWriter *pWriter;
    U8 *pData;
    off_t off;
    U32 u32MoofOff, u32MoofSize;
    U32 u32TrafOff, u32TrafSize;
    U32 u32TrunOff, u32TrunSize;
    U32 u32Flags, u32SampleCount;
    U32 u32ExpectedSize = 16;
    U32 u32FieldsPerSample = 0;

    CHECK_TRUE(pFile != NULL);
    pWriter = MuxWriter_Create(MUX_FILE_FMP4, pFile, MUX_CODEC_H264,
        640, 480, 30, 0);
    CHECK_TRUE(pWriter != NULL);
    CHECK_TRUE(MuxWriter_Start(pWriter) == 0);
    CHECK_TRUE(MuxWriter_Write(pWriter, au8KeyFrame, sizeof(au8KeyFrame),
        MPP_TRUE, 0) == 0);
    CHECK_TRUE(MuxWriter_Finish(pWriter) == 0);
    CHECK_TRUE(fflush(pFile) == 0);
    off = ftello(pFile);
    CHECK_TRUE(off > 0);
    CHECK_TRUE(fseek(pFile, 0, SEEK_SET) == 0);
    pData = (U8 *)malloc((size_t)off);
    CHECK_TRUE(pData != NULL);
    CHECK_TRUE(fread(pData, 1, (size_t)off, pFile) ==
        (size_t)off);

    CHECK_TRUE(find_box(pData, 0, (U32)off,
        MUX_FOURCC('m', 'o', 'o', 'f'), &u32MoofOff, &u32MoofSize) == 0);
    CHECK_TRUE(find_box(pData, u32MoofOff + 8, u32MoofOff + u32MoofSize,
        MUX_FOURCC('t', 'r', 'a', 'f'), &u32TrafOff, &u32TrafSize) == 0);
    CHECK_TRUE(find_box(pData, u32TrafOff + 8, u32TrafOff + u32TrafSize,
        MUX_FOURCC('t', 'r', 'u', 'n'), &u32TrunOff, &u32TrunSize) == 0);

    u32Flags = read_be32(pData + u32TrunOff + 8) & 0x00ffffff;
    u32SampleCount = read_be32(pData + u32TrunOff + 12);
    if (u32Flags & 0x000001)
        u32ExpectedSize += 4;
    if (u32Flags & 0x000004)
        u32ExpectedSize += 4;
    if (u32Flags & 0x000100)
        u32FieldsPerSample++;
    if (u32Flags & 0x000200)
        u32FieldsPerSample++;
    if (u32Flags & 0x000400)
        u32FieldsPerSample++;
    if (u32Flags & 0x000800)
        u32FieldsPerSample++;
    u32ExpectedSize += u32SampleCount * u32FieldsPerSample * 4;
    CHECK_TRUE(u32TrunSize == u32ExpectedSize);

    free(pData);
    MuxWriter_Destroy(pWriter);
    fclose(pFile);
    return 0;
}

int main(void) {
    CHECK_TRUE(test_next_nal_skips_leading_zero_and_trims_prefix_padding() == 0);
    CHECK_TRUE(test_find_start_code_4byte_at_offset_zero() == 0);
    CHECK_TRUE(test_next_nal_4byte_start_code_at_buffer_start() == 0);
    CHECK_TRUE(test_h264_vcl_conversion_filters_parameter_sets() == 0);
    CHECK_TRUE(test_h265_vcl_conversion_filters_parameter_sets() == 0);
    CHECK_TRUE(test_mp4_trun_flags_match_serialized_fields() == 0);
    return 0;
}
