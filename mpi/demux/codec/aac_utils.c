/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 * BSD-style license.
 *------------------------------------------------------------------------------
 */

#include "aac_utils.h"
#include <string.h>

static const U32 s_au32SampleRates[] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350, 0, 0, 0};

U32 AAC_GetSampleRate(U8 u8Idx) {
    if (u8Idx >= 16)
        return 0;
    return s_au32SampleRates[u8Idx];
}

S32 AAC_ParseAdts(const U8 *pu8Data, U32 u32Len, AdtsHeader *pstHeader) {
    if (!pu8Data || u32Len < 7 || !pstHeader)
        return -1;

    /* Check syncword (0xFFF) */
    if (pu8Data[0] != 0xFF || (pu8Data[1] & 0xF0) != 0xF0) {
        return -1;
    }

    /* ID, Layer, protection_absent */
    /* U8 id = (pu8Data[1] >> 3) & 0x01; */
    /* U8 layer = (pu8Data[1] >> 1) & 0x03; */
    /* U8 protectionAbsent = pu8Data[1] & 0x01; */

    pstHeader->u8Profile = ((pu8Data[2] >> 6) & 0x03) + 1; /* Object type - 1 */
    pstHeader->u8SampleRateIdx = (pu8Data[2] >> 2) & 0x0F;
    /* private_bit */
    pstHeader->u8ChannelConfig = ((pu8Data[2] & 0x01) << 2) | ((pu8Data[3] >> 6) & 0x03);

    /* original_copy, home, copyright bits */

    pstHeader->u16FrameLen = ((pu8Data[3] & 0x03) << 11) | (pu8Data[4] << 3) | ((pu8Data[5] >> 5) & 0x07);

    /*
     * Sanity-check the frame length. It is a 13-bit field that, per the ADTS
     * spec, counts the whole frame including the header, so it must be at
     * least ADTS_HEADER_SIZE bytes. It must also not exceed the data we were
     * given, otherwise a caller that trusts u16FrameLen would read past the
     * end of the buffer.
     */
    if (pstHeader->u16FrameLen < 7 || pstHeader->u16FrameLen > u32Len) {
        return -1;
    }

    pstHeader->u32SampleRate = AAC_GetSampleRate(pstHeader->u8SampleRateIdx);

    return 0;
}

S32 AAC_CreateAdts(U8 *pu8Header, U8 u8Profile, U8 u8SampleRateIdx, U8 u8ChannelConfig, U16 u16DataLen) {
    if (!pu8Header)
        return -1;

    U16 frameLen = u16DataLen + 7; /* Include header */

    /* Syncword */
    pu8Header[0] = 0xFF;
    pu8Header[1] = 0xF1; /* MPEG-4, Layer 0, protection absent */

    /* Profile, sample rate, channel */
    pu8Header[2] = ((u8Profile - 1) << 6) | (u8SampleRateIdx << 2) | ((u8ChannelConfig >> 2) & 0x01);
    pu8Header[3] = ((u8ChannelConfig & 0x03) << 6) | ((frameLen >> 11) & 0x03);
    pu8Header[4] = (frameLen >> 3) & 0xFF;
    pu8Header[5] = ((frameLen & 0x07) << 5) | 0x1F;
    pu8Header[6] = 0xFC;

    return 7;
}

S32 AAC_ParseAsc(const U8 *pu8Data, U32 u32Len, U8 *pu8Profile, U8 *pu8SampleRateIdx, U8 *pu8ChannelConfig) {
    if (!pu8Data || u32Len < 2)
        return -1;

    /* AudioSpecificConfig */
    U8 objectType = (pu8Data[0] >> 3) & 0x1F;
    U8 sampleRateIdx = ((pu8Data[0] & 0x07) << 1) | ((pu8Data[1] >> 7) & 0x01);
    U8 channelConfig = (pu8Data[1] >> 3) & 0x0F;

    if (pu8Profile)
        *pu8Profile = objectType;
    if (pu8SampleRateIdx)
        *pu8SampleRateIdx = sampleRateIdx;
    if (pu8ChannelConfig)
        *pu8ChannelConfig = channelConfig;

    return 0;
}
