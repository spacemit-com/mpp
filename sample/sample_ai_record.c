/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * sample_ai_record: capture from a sound card with the AI module and save
 * the PCM as a 16-bit WAV file.
 *
 *   sample_ai_record [-c card] [-r rate] [-n channels] [-p points] [-o out_rate] [-d seconds] [-f file.wav]
 *
 *   -c  sound card index, as in /proc/asound/cards          (default 0)
 *   -r  sample rate of the card, must be one it supports    (default 16000)
 *   -n  channels of the card                                (default 1)
 *   -p  sample points per frame                             (default 20 ms)
 *   -o  rate of the recording; AI resamples when it differs (default: -r)
 *   -d  seconds to record                                   (default 5)
 *   -f  output file                                         (default ai_record.wav)
 *
 * Example, a USB microphone that only does 48 kHz stereo, recorded at 16 kHz:
 *   sample_ai_record -c 1 -r 48000 -n 2 -o 16000 -d 10 -f /tmp/rec.wav
 */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ai_api.h"

static volatile sig_atomic_t g_bStop;

static void on_signal(int sig) {
    g_bStop = 1;
}

static void put_le(U8 *pu8Dst, U32 u32Val, U32 u32Bytes) {
    U32 i;

    for (i = 0; i < u32Bytes; i++)
        pu8Dst[i] = (U8)(u32Val >> (8 * i));
}

static int write_wav_header(FILE *pf, U32 u32Rate, U32 u32Channels, U32 u32DataBytes) {
    U8 au8Hdr[44];

    memcpy(au8Hdr, "RIFF", 4);
    put_le(au8Hdr + 4, 36 + u32DataBytes, 4);
    memcpy(au8Hdr + 8, "WAVEfmt ", 8);
    put_le(au8Hdr + 16, 16, 4);
    put_le(au8Hdr + 20, 1, 2); /* PCM */
    put_le(au8Hdr + 22, u32Channels, 2);
    put_le(au8Hdr + 24, u32Rate, 4);
    put_le(au8Hdr + 28, u32Rate * u32Channels * 2, 4);
    put_le(au8Hdr + 32, u32Channels * 2, 2);
    put_le(au8Hdr + 34, 16, 2);
    memcpy(au8Hdr + 36, "data", 4);
    put_le(au8Hdr + 40, u32DataBytes, 4);

    return fseek(pf, 0, SEEK_SET) == 0 && fwrite(au8Hdr, 1, sizeof(au8Hdr), pf) == sizeof(au8Hdr) ? 0 : -1;
}

int main(int argc, char **argv) {
    const AI_DEV AiDev = 0;
    const AI_CHN AiChn = 0;
    U32 u32Rate = 16000, u32Channels = 1, u32Points = 0, u32OutRate = 0, u32Seconds = 5;
    const char *pszFile = "ai_record.wav";
    U32 u32DataBytes = 0, u32WantBytes;
    AioDevAttrS stDevAttr;
    AiChnStateS stStat;
    AudioFrameS stFrame;
    MicAttrS stMicAttr;
    S32 s32Card = 0, ret;
    BOOL bFailed = MPP_FALSE;
    FILE *pf;
    int opt;

    while ((opt = getopt(argc, argv, "c:r:n:p:o:d:f:h")) != -1) {
        switch (opt) {
            case 'c': s32Card = atoi(optarg); break;
            case 'r': u32Rate = (U32)atoi(optarg); break;
            case 'n': u32Channels = (U32)atoi(optarg); break;
            case 'p': u32Points = (U32)atoi(optarg); break;
            case 'o': u32OutRate = (U32)atoi(optarg); break;
            case 'd': u32Seconds = (U32)atoi(optarg); break;
            case 'f': pszFile = optarg; break;
            default:
                printf("usage: %s [-c card] [-r rate] [-n channels] [-p points] [-o out_rate] [-d seconds] "
                       "[-f file.wav]\n", argv[0]);
                return 1;
        }
    }
    if (u32Points == 0)
        u32Points = u32Rate / 50;
    if (u32OutRate == 0)
        u32OutRate = u32Rate;
    signal(SIGINT, on_signal);

    pf = fopen(pszFile, "wb");
    if (pf == NULL || write_wav_header(pf, u32OutRate, u32Channels, 0) != 0) {
        printf("cannot write %s\n", pszFile);
        return 1;
    }

    ret = AI_Init();
    if (ret != SYS_ERR_OK) {
        printf("AI_Init failed: %d\n", ret);
        fclose(pf);
        return 1;
    }

    /* Which card, and what the card is to capture. Both before AI_EnableDev. */
    memset(&stMicAttr, 0, sizeof(stMicAttr));
    stMicAttr.stMicAttr.s32Dev = s32Card;
    stMicAttr.stMicAttr.s32Chns = (S32)u32Channels;
    memset(&stDevAttr, 0, sizeof(stDevAttr));
    stDevAttr.eSampleRate = (AudioSampleRate)u32Rate;
    stDevAttr.eBitWidth = AUDIO_BIT_WIDTH_16;
    stDevAttr.eSoundMode = u32Channels == 1 ? AUDIO_SOUND_MODE_MONO
                                            : (u32Channels == 2 ? AUDIO_SOUND_MODE_STEREO : AUDIO_SOUND_MODE_RAW);
    stDevAttr.u32FrmNum = 4;
    stDevAttr.u32PtNumPerFrm = u32Points;
    stDevAttr.u32ChnCnt = u32Channels;
    ret = AI_SetMicAttr(AiDev, &stMicAttr);
    if (ret == SYS_ERR_OK)
        ret = AI_SetDevAttr(AiDev, &stDevAttr);
    /* A channel at another rate than the card: declared before AI_EnableChn. */
    if (ret == SYS_ERR_OK && u32OutRate != u32Rate)
        ret = AI_EnableReSmp(AiDev, AiChn, (AudioSampleRate)u32OutRate);
    if (ret == SYS_ERR_OK)
        ret = AI_EnableDev(AiDev);
    if (ret == SYS_ERR_OK)
        ret = AI_EnableChn(AiDev, AiChn);
    if (ret != SYS_ERR_OK) {
        printf("cannot start capture on card %d (%u Hz, %u ch): %d\n", s32Card, u32Rate, u32Channels, ret);
        AI_DisableDev(AiDev);
        AI_DeInit();
        fclose(pf);
        return 1;
    }
    printf("recording %u s from card %d: %u Hz %u ch -> %s at %u Hz\n", u32Seconds, s32Card, u32Rate, u32Channels,
        pszFile, u32OutRate);

    u32WantBytes = u32Seconds * u32OutRate * u32Channels * 2;
    while (!g_bStop && u32DataBytes < u32WantBytes) {
        ret = AI_GetFrame(AiDev, AiChn, &stFrame, NULL, 1000);
        if (ret == SYS_ERR_TIMEOUT)
            continue;
        if (ret != SYS_ERR_OK) {
            /* AI_ERR_ABORTED here means the sound card is gone (e.g. unplugged). */
            printf("AI_GetFrame failed: %d\n", ret);
            bFailed = MPP_TRUE;
            break;
        }
        /* pu8Data belongs to AI and is only valid until AI_ReleaseFrame. */
        if (fwrite(stFrame.pu8Data, 1, stFrame.u32Len, pf) != stFrame.u32Len) {
            printf("write to %s failed\n", pszFile);
            g_bStop = 1;
        }
        u32DataBytes += stFrame.u32Len;
        AI_ReleaseFrame(AiDev, AiChn, &stFrame, NULL);
    }

    if (AI_QueryChnStat(AiDev, AiChn, &stStat) == SYS_ERR_OK)
        printf("recorded %.2f s, %llu frame(s) dropped\n", (double)u32DataBytes / (u32OutRate * u32Channels * 2),
            (unsigned long long)stStat.u64DroppedFrames);
    write_wav_header(pf, u32OutRate, u32Channels, u32DataBytes);
    fclose(pf);

    AI_DisableChn(AiDev, AiChn);
    AI_DisableDev(AiDev);
    AI_DeInit();

    return bFailed ? 1 : 0;
}
