/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * sample_ao_play: play a 16-bit WAV file, or a test tone, on a sound card
 * with the AO module.
 *
 *   sample_ao_play [-c card] [-r rate] [-n channels] [-p points] (-f file.wav | -t hz [-s rate] [-d seconds])
 *
 *   -c  sound card index, as in /proc/asound/cards           (default 0)
 *   -r  sample rate of the card, must be one it supports     (default 48000)
 *   -n  channels of the card                                 (default 2)
 *   -p  sample points per device frame                       (default 20 ms)
 *   -f  WAV file to play; AO resamples when its rate differs from -r.
 *       A mono file is copied to every channel of the card.
 *   -t  play a sine of this frequency instead of a file
 *   -s  sample rate the tone is generated at                 (default: -r)
 *   -d  seconds of tone                                      (default 3)
 *
 * Example, a 16 kHz mono recording on a card that only does 48 kHz stereo:
 *   sample_ao_play -c 1 -r 48000 -n 2 -f /tmp/rec.wav
 */

#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ao_api.h"

static volatile sig_atomic_t g_bStop;

static void on_signal(int sig) {
    g_bStop = 1;
}

static U32 get_le(const U8 *pu8Src, U32 u32Bytes) {
    U32 u32Val = 0, i;

    for (i = 0; i < u32Bytes; i++)
        u32Val |= (U32)pu8Src[i] << (8 * i);

    return u32Val;
}

/* Leave pf at the first sample of a 16-bit PCM WAV file. */
static int open_wav(FILE *pf, U32 *pu32Rate, U32 *pu32Channels, U32 *pu32DataBytes) {
    U8 au8Buf[16];
    BOOL bFmt = MPP_FALSE;

    if (fread(au8Buf, 1, 12, pf) != 12 || memcmp(au8Buf, "RIFF", 4) != 0 || memcmp(au8Buf + 8, "WAVE", 4) != 0)
        return -1;
    while (fread(au8Buf, 1, 8, pf) == 8) {
        U32 u32Size = get_le(au8Buf + 4, 4);

        if (memcmp(au8Buf, "fmt ", 4) == 0) {
            U8 au8Fmt[16];

            if (u32Size < 16 || fread(au8Fmt, 1, 16, pf) != 16)
                return -1;
            if (get_le(au8Fmt, 2) != 1 || get_le(au8Fmt + 14, 2) != 16)
                return -1; /* 16-bit PCM only */
            *pu32Channels = get_le(au8Fmt + 2, 2);
            *pu32Rate = get_le(au8Fmt + 4, 4);
            bFmt = MPP_TRUE;
            u32Size -= 16;
        } else if (memcmp(au8Buf, "data", 4) == 0) {
            *pu32DataBytes = u32Size;
            return bFmt ? 0 : -1;
        }
        if (fseek(pf, (long)(u32Size + (u32Size & 1)), SEEK_CUR) != 0)
            return -1;
    }

    return -1;
}

int main(int argc, char **argv) {
    const AO_DEV AoDev = 0;
    const AO_CHN AoChn = 0;
    U32 u32Rate = 48000, u32Channels = 2, u32Points = 0, u32SrcRate = 0, u32Seconds = 3;
    U32 u32SrcChannels = 1, u32DataBytes = 0, u32FramePoints, u32Sent = 0, u32Busy = 0, u32StalledMs = 0, i, c;
    BOOL bFailed = MPP_FALSE;
    int16_t *ps16Src = NULL, *ps16Frame = NULL;
    const char *pszFile = NULL;
    AioDevAttrS stDevAttr;
    AoChnStateS stStat;
    AudioFrameS stFrame;
    SpkAttrS stSpkAttr;
    U64 u64Sample = 0, u64Total;
    double dToneHz = 0;
    S32 s32Card = 0, ret;
    FILE *pf = NULL;
    int opt;

    while ((opt = getopt(argc, argv, "c:r:n:p:f:t:s:d:h")) != -1) {
        switch (opt) {
            case 'c': s32Card = atoi(optarg); break;
            case 'r': u32Rate = (U32)atoi(optarg); break;
            case 'n': u32Channels = (U32)atoi(optarg); break;
            case 'p': u32Points = (U32)atoi(optarg); break;
            case 'f': pszFile = optarg; break;
            case 't': dToneHz = atof(optarg); break;
            case 's': u32SrcRate = (U32)atoi(optarg); break;
            case 'd': u32Seconds = (U32)atoi(optarg); break;
            default: pszFile = NULL; dToneHz = 0; break;
        }
    }
    if (pszFile == NULL && dToneHz <= 0) {
        printf("usage: %s [-c card] [-r rate] [-n channels] [-p points] (-f file.wav | -t hz [-s rate] [-d seconds])\n",
            argv[0]);
        return 1;
    }
    if (u32Points == 0)
        u32Points = u32Rate / 50;
    if (pszFile != NULL) {
        pf = fopen(pszFile, "rb");
        if (pf == NULL || open_wav(pf, &u32SrcRate, &u32SrcChannels, &u32DataBytes) != 0) {
            printf("%s is not a 16-bit PCM WAV file\n", pszFile);
            return 1;
        }
        if (u32SrcChannels != 1 && u32SrcChannels != u32Channels) {
            printf("%s has %u channels, the card has %u\n", pszFile, u32SrcChannels, u32Channels);
            return 1;
        }
        u64Total = u32DataBytes / (u32SrcChannels * 2);
    } else {
        if (u32SrcRate == 0)
            u32SrcRate = u32Rate;
        u64Total = (U64)u32Seconds * u32SrcRate;
    }
    signal(SIGINT, on_signal);

    ret = AO_Init();
    if (ret != SYS_ERR_OK) {
        printf("AO_Init failed: %d\n", ret);
        return 1;
    }

    /* Which card, and what the card is to play. Both before AO_EnableDev. */
    memset(&stSpkAttr, 0, sizeof(stSpkAttr));
    stSpkAttr.stSpkAttr.s32Dev = s32Card;
    stSpkAttr.stSpkAttr.s32Chns = (S32)u32Channels;
    memset(&stDevAttr, 0, sizeof(stDevAttr));
    stDevAttr.eSampleRate = (AudioSampleRate)u32Rate;
    stDevAttr.eBitWidth = AUDIO_BIT_WIDTH_16;
    stDevAttr.eSoundMode = u32Channels == 1 ? AUDIO_SOUND_MODE_MONO
                                            : (u32Channels == 2 ? AUDIO_SOUND_MODE_STEREO : AUDIO_SOUND_MODE_RAW);
    stDevAttr.u32FrmNum = 4;
    stDevAttr.u32PtNumPerFrm = u32Points;
    stDevAttr.u32ChnCnt = u32Channels;
    ret = AO_SetSpkAttr(AoDev, &stSpkAttr);
    if (ret == SYS_ERR_OK)
        ret = AO_SetDevAttr(AoDev, &stDevAttr);
    /* Frames at another rate than the card: declared before AO_EnableChn. */
    if (ret == SYS_ERR_OK && u32SrcRate != u32Rate)
        ret = AO_EnableReSmp(AoDev, AoChn, (AudioSampleRate)u32SrcRate);
    if (ret == SYS_ERR_OK)
        ret = AO_EnableDev(AoDev);
    if (ret == SYS_ERR_OK)
        ret = AO_EnableChn(AoDev, AoChn);
    if (ret != SYS_ERR_OK) {
        printf("cannot start playback on card %d (%u Hz, %u ch, source %u Hz): %d\n", s32Card, u32Rate, u32Channels,
            u32SrcRate, ret);
        AO_DisableDev(AoDev);
        AO_DeInit();
        return 1;
    }

    /*
     * AO_SendFrame takes frames that last one device frame. At the device
     * rate that is u32PtNumPerFrm points; at another rate it is that many
     * points scaled by the rate ratio and rounded to the nearest integer.
     */
    u32FramePoints = (U32)(((U64)u32Points * u32SrcRate + u32Rate / 2) / u32Rate);
    ps16Src = (int16_t *)calloc((size_t)u32FramePoints * u32SrcChannels, sizeof(int16_t));
    ps16Frame = (int16_t *)calloc((size_t)u32FramePoints * u32Channels, sizeof(int16_t));
    if (ps16Src == NULL || ps16Frame == NULL) {
        printf("out of memory\n");
        g_bStop = 1;
    }
    printf("playing %.2f s on card %d: %u Hz %u ch source -> %u Hz %u ch, %u points per frame\n",
        (double)u64Total / u32SrcRate, s32Card, u32SrcRate, u32SrcChannels, u32Rate, u32Channels, u32FramePoints);

    memset(&stFrame, 0, sizeof(stFrame));
    stFrame.eBitWidth = AUDIO_BIT_WIDTH_16;
    stFrame.eSoundMode = stDevAttr.eSoundMode;
    stFrame.eSampleRate = (AudioSampleRate)u32SrcRate;
    stFrame.s32Channels = (S32)u32Channels;
    stFrame.pu8Data = (U8 *)ps16Frame;
    stFrame.u32Len = u32FramePoints * u32Channels * 2;

    while (!g_bStop && u64Sample < u64Total) {
        /* The last frame is padded with silence to full length. */
        memset(ps16Src, 0, (size_t)u32FramePoints * u32SrcChannels * sizeof(int16_t));
        if (pf != NULL) {
            /* Never read past the data chunk: other chunks may follow it. */
            U64 u64Left = u64Total - u64Sample;

            if (fread(ps16Src, (size_t)u32SrcChannels * 2, u64Left < u32FramePoints ? (size_t)u64Left : u32FramePoints,
                    pf) == 0)
                break;
        } else {
            for (i = 0; i < u32FramePoints && u64Sample + i < u64Total; i++)
                ps16Src[i] = (int16_t)lrint(32767.0 * 0.3 *
                    sin(2.0 * 3.14159265358979323846 * dToneHz * (double)(u64Sample + i) / u32SrcRate));
        }
        for (i = 0; i < u32FramePoints; i++) {
            for (c = 0; c < u32Channels; c++)
                ps16Frame[i * u32Channels + c] = ps16Src[i * u32SrcChannels + (u32SrcChannels == 1 ? 0 : c)];
        }

        /* Blocks while the send queue is full; the payload is copied. */
        ret = AO_SendFrame(AoDev, AoChn, &stFrame, AIO_BLOCK_INFINITE);
        if (ret != SYS_ERR_OK) {
            /* AO_ERR_ABORTED here means the sound card is gone (e.g. unplugged). */
            printf("AO_SendFrame failed: %d\n", ret);
            bFailed = MPP_TRUE;
            break;
        }
        u64Sample += u32FramePoints;
        u32Sent++;
    }

    /*
     * Let the queue and then the device buffer play out before closing. A
     * queue that has not moved for a second will not empty any more: the
     * card went away after the last frame was sent. A frame lasts 512 ms at
     * most, so a working card takes a frame out well within that second.
     */
    while (!g_bStop && !bFailed && AO_QueryChnStat(AoDev, AoChn, &stStat) == SYS_ERR_OK &&
        stStat.u32ChnBusyNum > 0) {
        u32StalledMs = stStat.u32ChnBusyNum == u32Busy ? u32StalledMs + 10 : 0;
        u32Busy = stStat.u32ChnBusyNum;
        if (u32StalledMs >= 1000) {
            printf("playback stalled with %u frame(s) queued, the sound card is gone\n", u32Busy);
            bFailed = MPP_TRUE;
        }
        usleep(10000);
    }
    if (!g_bStop && !bFailed)
        usleep((useconds_t)((stDevAttr.u32FrmNum + 2) * 1000000ULL * u32Points / u32Rate));
    printf("sent %u frame(s)\n", u32Sent);

    AO_DisableChn(AoDev, AoChn);
    AO_DisableDev(AoDev);
    AO_DeInit();
    free(ps16Src);
    free(ps16Frame);
    if (pf != NULL)
        fclose(pf);

    return bFailed ? 1 : 0;
}
