/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * AI/AO checks on a real sound card. Hardware-only: built, but not
 * registered with CTest. Run on the board, for example:
 *
 *   MPP_PLUGIN_DIR=$PWD/build/lib ./build/test/test_aio_hw errors  -C 2 -r 16000 -n 2 -P 2 -R 48000 -m 2
 *   MPP_PLUGIN_DIR=$PWD/build/lib ./build/test/test_aio_hw capture -C 2 -r 16000 -n 2 -d 10 -w /tmp/cap.wav
 *   MPP_PLUGIN_DIR=$PWD/build/lib ./build/test/test_aio_hw play    -P 2 -R 48000 -m 2 -i 16000 -d 5
 *   MPP_PLUGIN_DIR=$PWD/build/lib ./build/test/test_aio_hw duplex  -C 2 -r 16000 -n 2 -P 2 -R 48000 -m 2 -d 10
 *   MPP_PLUGIN_DIR=$PWD/build/lib ./build/test/test_aio_hw cycle   -C 2 -r 16000 -n 2 -P 2 -R 48000 -m 2 -N 200
 *
 * Options
 *   -C / -P   capture / playback card            -r / -R   capture / playback device rate
 *   -n / -m   capture / playback channels        -p / -q   capture / playback points per frame (default 20 ms)
 *   -o        AI output rate (AI_EnableReSmp)    -i        AO input rate (AO_EnableReSmp)
 *   -d        seconds                            -t        tone in Hz (default 1000)
 *   -w        save the capture as a WAV file     -N        cycles
 *   -e        duplex: fail unless the played tone is found in the capture
 *
 * Every check prints [PASS] or [FAIL]; the exit code is the number of failures.
 */

#include <dirent.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ai_api.h"
#include "ao_api.h"

#define HW_PI 3.14159265358979323846
#define HW_FRM_NUM 4
#define HW_TONE_AMP 0.3
#define HW_KEEP_SECONDS 30 /* audio kept for analysis / the WAV file; longer runs only count */

typedef struct _HwCfg {
    S32 s32CapCard, s32PlayCard;
    U32 u32CapRate, u32PlayRate;
    U32 u32CapCh, u32PlayCh;
    U32 u32CapPoints, u32PlayPoints;
    U32 u32AiOutRate, u32AoInRate;
    U32 u32Seconds, u32Cycles;
    double dToneHz;
    const char *pszWav;
    BOOL bExpectTone;
} HwCfg;

static HwCfg g_stCfg;
static int g_iFailures;

#define CHECK(cond, fmt, ...)                                                   \
    do {                                                                        \
        if (cond) {                                                             \
            printf("[PASS] " fmt "\n", ##__VA_ARGS__);                          \
        } else {                                                                \
            printf("[FAIL] " fmt "   (%s)\n", ##__VA_ARGS__, #cond);            \
            g_iFailures++;                                                      \
        }                                                                       \
        fflush(stdout);                                                         \
    } while (0)

static U64 now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (U64)ts.tv_sec * 1000ULL + (U64)ts.tv_nsec / 1000000ULL;
}

static AudioSoundMode sound_mode(U32 u32Ch) {
    return u32Ch == 1 ? AUDIO_SOUND_MODE_MONO : (u32Ch == 2 ? AUDIO_SOUND_MODE_STEREO : AUDIO_SOUND_MODE_RAW);
}

static U32 scale_points(U32 u32Points, U32 u32From, U32 u32To) {
    return (U32)(((U64)u32Points * u32To + u32From / 2) / u32From);
}

static AioDevAttrS dev_attr(U32 u32Rate, U32 u32Ch, U32 u32Points) {
    AioDevAttrS stAttr;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eSampleRate = (AudioSampleRate)u32Rate;
    stAttr.eBitWidth = AUDIO_BIT_WIDTH_16;
    stAttr.eSoundMode = sound_mode(u32Ch);
    stAttr.u32FrmNum = HW_FRM_NUM;
    stAttr.u32PtNumPerFrm = u32Points;
    stAttr.u32ChnCnt = u32Ch;

    return stAttr;
}

/* ======================== Bring-up helpers ======================== */

static S32 ai_start(void) {
    AioDevAttrS stAttr = dev_attr(g_stCfg.u32CapRate, g_stCfg.u32CapCh, g_stCfg.u32CapPoints);
    MicAttrS stMic;
    S32 ret;

    memset(&stMic, 0, sizeof(stMic));
    stMic.stMicAttr.s32Dev = g_stCfg.s32CapCard;
    stMic.stMicAttr.s32Chns = (S32)g_stCfg.u32CapCh;
    if ((ret = AI_SetMicAttr(0, &stMic)) != SYS_ERR_OK || (ret = AI_SetDevAttr(0, &stAttr)) != SYS_ERR_OK)
        return ret;
    ret = g_stCfg.u32AiOutRate != g_stCfg.u32CapRate ? AI_EnableReSmp(0, 0, (AudioSampleRate)g_stCfg.u32AiOutRate)
                                                     : AI_DisableReSmp(0, 0);
    if (ret != SYS_ERR_OK || (ret = AI_EnableDev(0)) != SYS_ERR_OK)
        return ret;
    if ((ret = AI_EnableChn(0, 0)) != SYS_ERR_OK)
        AI_DisableDev(0);

    return ret;
}

static S32 ao_start(void) {
    AioDevAttrS stAttr = dev_attr(g_stCfg.u32PlayRate, g_stCfg.u32PlayCh, g_stCfg.u32PlayPoints);
    SpkAttrS stSpk;
    S32 ret;

    memset(&stSpk, 0, sizeof(stSpk));
    stSpk.stSpkAttr.s32Dev = g_stCfg.s32PlayCard;
    stSpk.stSpkAttr.s32Chns = (S32)g_stCfg.u32PlayCh;
    if ((ret = AO_SetSpkAttr(0, &stSpk)) != SYS_ERR_OK || (ret = AO_SetDevAttr(0, &stAttr)) != SYS_ERR_OK)
        return ret;
    ret = g_stCfg.u32AoInRate != g_stCfg.u32PlayRate ? AO_EnableReSmp(0, 0, (AudioSampleRate)g_stCfg.u32AoInRate)
                                                     : AO_DisableReSmp(0, 0);
    if (ret != SYS_ERR_OK || (ret = AO_EnableDev(0)) != SYS_ERR_OK)
        return ret;
    if ((ret = AO_EnableChn(0, 0)) != SYS_ERR_OK)
        AO_DisableDev(0);

    return ret;
}

/* Points per frame AO_SendFrame expects on the test channel. */
static U32 ao_points(void) {
    return scale_points(g_stCfg.u32PlayPoints, g_stCfg.u32PlayRate, g_stCfg.u32AoInRate);
}

static S32 ao_send_tone(U64 u64FirstSample, S32 s32MilliSec) {
    static int16_t as16[MAX_AUDIO_FRAME_LEN / 2];
    const U32 u32Points = ao_points(), u32Ch = g_stCfg.u32PlayCh;
    AudioFrameS stFrame;
    U32 i, c;

    for (i = 0; i < u32Points; i++) {
        int16_t s16 = (int16_t)lrint(32767.0 * HW_TONE_AMP *
            sin(2.0 * HW_PI * g_stCfg.dToneHz * (double)(u64FirstSample + i) / g_stCfg.u32AoInRate));

        for (c = 0; c < u32Ch; c++)
            as16[i * u32Ch + c] = s16;
    }
    memset(&stFrame, 0, sizeof(stFrame));
    stFrame.eBitWidth = AUDIO_BIT_WIDTH_16;
    stFrame.eSoundMode = sound_mode(u32Ch);
    stFrame.eSampleRate = (AudioSampleRate)g_stCfg.u32AoInRate;
    stFrame.s32Channels = (S32)u32Ch;
    stFrame.pu8Data = (U8 *)as16;
    stFrame.u32Len = u32Points * u32Ch * 2;

    return AO_SendFrame(0, 0, &stFrame, s32MilliSec);
}

/* ======================== Capture ======================== */

typedef struct _CapResult {
    int16_t *ps16Data; /* interleaved, the first HW_KEEP_SECONDS at most */
    U32 u32Kept;       /* sample points per channel in ps16Data */
    U32 u32Frames;     /* sample points per channel received */
    U32 u32Got;        /* AI frames received */
    U32 u32SeqGaps;
    U32 u32Discont;
    U32 u32BadPts;
    U32 u32BadFormat;
    S64 s64LatMinMs, s64LatMaxMs, s64LatSumMs;
    U64 u64ElapsedMs;
    S32 s32Err;
} CapResult;

/* Capture u32Seconds of audio from AI dev 0 / chn 0 and check each frame. */
static VOID capture_run(CapResult *pstRes, U32 u32Seconds) {
    const U32 u32Rate = g_stCfg.u32AiOutRate, u32Ch = g_stCfg.u32CapCh;
    const U32 u32Points = scale_points(g_stCfg.u32CapPoints, g_stCfg.u32CapRate, u32Rate);
    const U32 u32Want = u32Seconds * u32Rate;
    const U32 u32Keep = (u32Seconds < HW_KEEP_SECONDS ? u32Seconds : HW_KEEP_SECONDS) * u32Rate;
    const double dFrameMs = 1000.0 * u32Points / u32Rate;
    U64 u64Start = now_ms(), u64PrevPts = 0;
    U32 u32PrevSeq = 0;
    AudioFrameS stFrame;

    memset(pstRes, 0, sizeof(*pstRes));
    pstRes->s64LatMinMs = INT64_MAX;
    pstRes->s64LatMaxMs = INT64_MIN;
    pstRes->ps16Data = (int16_t *)malloc((size_t)(u32Keep + u32Points) * u32Ch * 2);
    if (pstRes->ps16Data == NULL) {
        pstRes->s32Err = SYS_ERR_NOMEM;
        return;
    }

    while (pstRes->u32Frames < u32Want) {
        S32 ret = AI_GetFrame(0, 0, &stFrame, NULL, 2000);
        S64 s64Lat;

        if (ret != SYS_ERR_OK) {
            pstRes->s32Err = ret;
            break;
        }
        if ((U32)stFrame.eSampleRate != u32Rate || stFrame.s32Channels != (S32)u32Ch ||
            stFrame.u32Len != u32Points * u32Ch * 2 || stFrame.ePcmFormat != AUDIO_PCM_S16_LE ||
            stFrame.eBitWidth != AUDIO_BIT_WIDTH_16)
            pstRes->u32BadFormat++;
        if (stFrame.u32Flags & AUDIO_FRAME_DISCONTINUITY)
            pstRes->u32Discont++;
        if (pstRes->u32Got > 0) {
            double dDelta = (double)stFrame.u64PTS - (double)u64PrevPts;

            if (stFrame.u32Seq != u32PrevSeq + 1)
                pstRes->u32SeqGaps++;
            /* PTS is in whole milliseconds: allow the rounding of both ends. */
            else if (!(stFrame.u32Flags & AUDIO_FRAME_DISCONTINUITY) && fabs(dDelta - dFrameMs) > 2.0)
                pstRes->u32BadPts++;
        }
        /* The last sample of the frame was captured dFrameMs after its PTS. */
        s64Lat = (S64)now_ms() - (S64)stFrame.u64PTS - (S64)dFrameMs;
        if (s64Lat < pstRes->s64LatMinMs)
            pstRes->s64LatMinMs = s64Lat;
        if (s64Lat > pstRes->s64LatMaxMs)
            pstRes->s64LatMaxMs = s64Lat;
        pstRes->s64LatSumMs += s64Lat;

        if (pstRes->u32Kept < u32Keep) {
            memcpy(pstRes->ps16Data + (size_t)pstRes->u32Kept * u32Ch, stFrame.pu8Data, stFrame.u32Len);
            pstRes->u32Kept += stFrame.u32Len / (u32Ch * 2);
        }
        pstRes->u32Frames += stFrame.u32Len / (u32Ch * 2);
        pstRes->u32Got++;
        u32PrevSeq = stFrame.u32Seq;
        u64PrevPts = stFrame.u64PTS;
        if (AI_ReleaseFrame(0, 0, &stFrame, NULL) != SYS_ERR_OK) {
            pstRes->s32Err = SYS_ERR_INVAL;
            break;
        }
    }
    pstRes->u64ElapsedMs = now_ms() - u64Start;
}

static VOID save_wav(const char *pszPath, const int16_t *ps16Data, U32 u32Frames, U32 u32Rate, U32 u32Ch) {
    const U32 u32Bytes = u32Frames * u32Ch * 2;
    U8 au8Hdr[44] = "RIFF____WAVEfmt ";
    uint32_t u32;
    uint16_t u16;
    FILE *pf = fopen(pszPath, "wb");

    if (pf == NULL) {
        printf("cannot write %s\n", pszPath);
        return;
    }
    u32 = 36 + u32Bytes;
    memcpy(au8Hdr + 4, &u32, 4);
    u32 = 16;
    memcpy(au8Hdr + 16, &u32, 4);
    u16 = 1;
    memcpy(au8Hdr + 20, &u16, 2);
    u16 = (uint16_t)u32Ch;
    memcpy(au8Hdr + 22, &u16, 2);
    u32 = u32Rate;
    memcpy(au8Hdr + 24, &u32, 4);
    u32 = u32Rate * u32Ch * 2;
    memcpy(au8Hdr + 28, &u32, 4);
    u16 = (uint16_t)(u32Ch * 2);
    memcpy(au8Hdr + 32, &u16, 2);
    u16 = 16;
    memcpy(au8Hdr + 34, &u16, 2);
    memcpy(au8Hdr + 36, "data", 4);
    memcpy(au8Hdr + 40, &u32Bytes, 4);
    if (fwrite(au8Hdr, 1, sizeof(au8Hdr), pf) != sizeof(au8Hdr) || fwrite(ps16Data, 1, u32Bytes, pf) != u32Bytes)
        printf("short write to %s\n", pszPath);
    fclose(pf);
    printf("       saved %s (%u Hz, %u ch, %.2f s)\n", pszPath, u32Rate, u32Ch, (double)u32Frames / u32Rate);
}

/* RMS and peak of one channel in dBFS. */
static VOID level_db(const int16_t *ps16Data, U32 u32Frames, U32 u32Ch, U32 u32Sel, double *pdRms, double *pdPeak) {
    double dSum = 0, dPeak = 0;
    U32 i;

    for (i = 0; i < u32Frames; i++) {
        double d = ps16Data[(size_t)i * u32Ch + u32Sel] / 32768.0;

        dSum += d * d;
        if (fabs(d) > dPeak)
            dPeak = fabs(d);
    }
    *pdRms = 20.0 * log10(sqrt(dSum / (u32Frames ? u32Frames : 1)) + 1e-9);
    *pdPeak = 20.0 * log10(dPeak + 1e-9);
}

static VOID capture_report(const CapResult *pstRes, U32 u32Seconds) {
    const U32 u32Rate = g_stCfg.u32AiOutRate, u32Ch = g_stCfg.u32CapCh;
    AiChnStateS stStat;
    U32 c;

    memset(&stStat, 0, sizeof(stStat));
    CHECK(pstRes->s32Err == SYS_ERR_OK, "capture ran to the end (error %d)", pstRes->s32Err);
    CHECK(pstRes->u32Frames >= u32Seconds * u32Rate, "captured %u points in %u frames at %u Hz", pstRes->u32Frames,
        pstRes->u32Got, u32Rate);
    CHECK(pstRes->u32BadFormat == 0, "frame descriptors match the channel (%u wrong)", pstRes->u32BadFormat);
    CHECK(pstRes->u32SeqGaps == 0, "no gap in u32Seq (%u)", pstRes->u32SeqGaps);
    CHECK(pstRes->u32Discont == 0, "no discontinuity, i.e. no overrun (%u)", pstRes->u32Discont);
    CHECK(pstRes->u32BadPts == 0, "PTS advances by one frame duration (%u off by more than 2 ms)", pstRes->u32BadPts);
    /* The sound card clock, not ours, sets the pace: 1 %% covers any real crystal. */
    CHECK(llabs((long long)pstRes->u64ElapsedMs - 1000LL * u32Seconds) <= 10LL * u32Seconds + 150,
        "%u s of audio took %llu ms of wall clock", u32Seconds, (unsigned long long)pstRes->u64ElapsedMs);
    if (pstRes->u32Got > 0)
        CHECK(pstRes->s64LatMinMs >= -5 && pstRes->s64LatMaxMs < 500,
            "PTS is the capture time: frames arrive %lld..%lld ms (avg %lld) after their last sample",
            (long long)pstRes->s64LatMinMs, (long long)pstRes->s64LatMaxMs,
            (long long)(pstRes->s64LatSumMs / pstRes->u32Got));
    CHECK(AI_QueryChnStat(0, 0, &stStat) == SYS_ERR_OK && stStat.u64DroppedFrames == 0 && stStat.u32ChnBusyNum == 0,
        "channel statistics: %llu dropped, %u held, peak queue %u", (unsigned long long)stStat.u64DroppedFrames,
        stStat.u32ChnBusyNum, stStat.u32PeakQueuedFrames);
    for (c = 0; c < u32Ch; c++) {
        double dRms, dPeak;

        level_db(pstRes->ps16Data, pstRes->u32Kept, u32Ch, c, &dRms, &dPeak);
        printf("       channel %u level: rms %.1f dBFS, peak %.1f dBFS\n", c, dRms, dPeak);
    }
    if (g_stCfg.pszWav != NULL && pstRes->ps16Data != NULL)
        save_wav(g_stCfg.pszWav, pstRes->ps16Data, pstRes->u32Kept, u32Rate, u32Ch);
}

static VOID cmd_capture(void) {
    CapResult stRes;
    S32 ret = ai_start();

    CHECK(ret == SYS_ERR_OK, "AI up: card %d, %u Hz, %u ch, %u points -> channel at %u Hz (ret %d)",
        g_stCfg.s32CapCard, g_stCfg.u32CapRate, g_stCfg.u32CapCh, g_stCfg.u32CapPoints, g_stCfg.u32AiOutRate, ret);
    if (ret != SYS_ERR_OK)
        return;
    capture_run(&stRes, g_stCfg.u32Seconds);
    capture_report(&stRes, g_stCfg.u32Seconds);
    free(stRes.ps16Data);
    CHECK(AI_DisableDev(0) == SYS_ERR_OK, "AI down");
}

/* ======================== Playback ======================== */

typedef struct _PlayResult {
    U32 u32Sent;
    U64 u64UnderrunsBeforeEnd;
    U64 u64ElapsedMs;
    S32 s32Err;
} PlayResult;

static atomic_bool g_bPlayStop;

/* Play u32Seconds of tone on AO dev 0 / chn 0, then wait for the queue to empty. */
static VOID play_run(PlayResult *pstRes, U32 u32Seconds) {
    const U32 u32Points = ao_points();
    const U32 u32Frames = (U32)(((U64)u32Seconds * g_stCfg.u32AoInRate + u32Points - 1) / u32Points);
    U64 u64Start = now_ms();
    AoChnStateS stStat;
    U32 i;

    memset(pstRes, 0, sizeof(*pstRes));
    memset(&stStat, 0, sizeof(stStat));
    for (i = 0; i < u32Frames && !atomic_load(&g_bPlayStop); i++) {
        S32 ret = ao_send_tone((U64)i * u32Points, 2000);

        if (ret != SYS_ERR_OK) {
            pstRes->s32Err = ret;
            break;
        }
        pstRes->u32Sent++;
    }
    if (AO_QueryChnStat(0, 0, &stStat) == SYS_ERR_OK)
        pstRes->u64UnderrunsBeforeEnd = stStat.u64UnderrunPeriods;
    /* There is no drain call yet: watch the queue, then give the device buffer time. */
    while (AO_QueryChnStat(0, 0, &stStat) == SYS_ERR_OK && stStat.u32ChnBusyNum > 0)
        usleep(5000);
    pstRes->u64ElapsedMs = now_ms() - u64Start;
    usleep((HW_FRM_NUM + 2) * 1000000ULL * g_stCfg.u32PlayPoints / g_stCfg.u32PlayRate);
}

static VOID play_report(const PlayResult *pstRes, U32 u32Seconds) {
    CHECK(pstRes->s32Err == SYS_ERR_OK, "playback ran to the end (error %d)", pstRes->s32Err);
    CHECK(pstRes->u32Sent > 0, "sent %u frames of %u points at %u Hz", pstRes->u32Sent, ao_points(),
        g_stCfg.u32AoInRate);
    CHECK(pstRes->u64UnderrunsBeforeEnd == 0, "the channel never starved while sending (%llu)",
        (unsigned long long)pstRes->u64UnderrunsBeforeEnd);
    /* The queue only empties as fast as the device plays. */
    CHECK(llabs((long long)pstRes->u64ElapsedMs - 1000LL * u32Seconds) <= 10LL * u32Seconds + 250,
        "%u s of audio took %llu ms of wall clock", u32Seconds, (unsigned long long)pstRes->u64ElapsedMs);
}

static VOID cmd_play(void) {
    PlayResult stRes;
    S32 ret = ao_start();

    CHECK(ret == SYS_ERR_OK, "AO up: card %d, %u Hz, %u ch, %u points <- channel at %u Hz (ret %d)",
        g_stCfg.s32PlayCard, g_stCfg.u32PlayRate, g_stCfg.u32PlayCh, g_stCfg.u32PlayPoints, g_stCfg.u32AoInRate, ret);
    if (ret != SYS_ERR_OK)
        return;
    printf("       playing a %.0f Hz tone for %u s, listen for dropouts\n", g_stCfg.dToneHz, g_stCfg.u32Seconds);
    play_run(&stRes, g_stCfg.u32Seconds);
    play_report(&stRes, g_stCfg.u32Seconds);
    CHECK(AO_DisableDev(0) == SYS_ERR_OK, "AO down");
}

/* ======================== Duplex ======================== */

static void *duplex_play_thread(void *pArg) {
    play_run((PlayResult *)pArg, g_stCfg.u32Seconds + 1);
    return NULL;
}

/* Strongest frequency in one channel, by a coarse scan followed by a fine one. */
static double peak_frequency(const int16_t *ps16Data, U32 u32Frames, U32 u32Ch, U32 u32Rate, double *pdLevelDb) {
    double dBest = 0, dBestMag = -1, dFrom = 50, dTo = u32Rate / 2.0 - 50, dStep = 25;
    U32 u32Pass, i;

    for (u32Pass = 0; u32Pass < 2; u32Pass++) {
        double f;

        for (f = dFrom; f <= dTo; f += dStep) {
            double dRe = 0, dIm = 0, dMag;

            for (i = 0; i < u32Frames; i++) {
                double dPhase = 2.0 * HW_PI * f * i / u32Rate;
                double d = ps16Data[(size_t)i * u32Ch] / 32768.0;

                dRe += d * cos(dPhase);
                dIm += d * sin(dPhase);
            }
            dMag = 2.0 * sqrt(dRe * dRe + dIm * dIm) / u32Frames;
            if (dMag > dBestMag) {
                dBestMag = dMag;
                dBest = f;
            }
        }
        dFrom = dBest - 25;
        dTo = dBest + 25;
        dStep = 1;
    }
    *pdLevelDb = 20.0 * log10(dBestMag + 1e-9);

    return dBest;
}

static VOID cmd_duplex(void) {
    PlayResult stPlay;
    CapResult stCap;
    pthread_t stThread;
    S32 ret;

    ret = ao_start();
    CHECK(ret == SYS_ERR_OK, "AO up (ret %d)", ret);
    if (ret != SYS_ERR_OK)
        return;
    ret = ai_start();
    CHECK(ret == SYS_ERR_OK, "AI up (ret %d)", ret);
    if (ret != SYS_ERR_OK) {
        AO_DisableDev(0);
        return;
    }

    atomic_store(&g_bPlayStop, MPP_FALSE);
    pthread_create(&stThread, NULL, duplex_play_thread, &stPlay);
    capture_run(&stCap, g_stCfg.u32Seconds);
    capture_report(&stCap, g_stCfg.u32Seconds);
    atomic_store(&g_bPlayStop, MPP_TRUE);
    pthread_join(stThread, NULL);
    CHECK(stPlay.s32Err == SYS_ERR_OK && stPlay.u64UnderrunsBeforeEnd == 0,
        "playback alongside: %u frames sent, %llu underruns (error %d)", stPlay.u32Sent,
        (unsigned long long)stPlay.u64UnderrunsBeforeEnd, stPlay.s32Err);

    if (stCap.ps16Data != NULL && stCap.u32Kept > g_stCfg.u32AiOutRate) {
        /*
         * Look for the played tone in 200 ms windows. Speakerphones cancel
         * their own output after a moment, so the tone may only show early.
         */
        const U32 u32Rate = g_stCfg.u32AiOutRate, u32Ch = g_stCfg.u32CapCh;
        const U32 u32Win = u32Rate / 5, u32Hop = u32Rate / 10;
        double dBestTone = -200, dFreq = 0, dLevel = -200;
        U32 u32Best = 0, u32At, i;

        for (u32At = 0; u32At + u32Win <= stCap.u32Kept; u32At += u32Hop) {
            double dRe = 0, dIm = 0, dTone;

            for (i = 0; i < u32Win; i++) {
                double dPhase = 2.0 * HW_PI * g_stCfg.dToneHz * i / u32Rate;
                double d = stCap.ps16Data[(size_t)(u32At + i) * u32Ch] / 32768.0;

                dRe += d * cos(dPhase);
                dIm += d * sin(dPhase);
            }
            dTone = 20.0 * log10(2.0 * sqrt(dRe * dRe + dIm * dIm) / u32Win + 1e-9);
            if (dTone > dBestTone) {
                dBestTone = dTone;
                u32Best = u32At;
            }
        }
        dFreq = peak_frequency(stCap.ps16Data + (size_t)u32Best * u32Ch, u32Win, u32Ch, u32Rate, &dLevel);
        printf("       %.0f Hz is strongest %.1f s into the capture: %.1f dBFS; the peak of that window is %.0f Hz "
               "at %.1f dBFS\n",
            g_stCfg.dToneHz, (double)u32Best / u32Rate, dBestTone, dFreq, dLevel);
        if (g_stCfg.bExpectTone)
            /* A wrong rate on either side would move the tone; a 200 ms window resolves 5 Hz. */
            CHECK(dBestTone > -60.0 && fabs(dFreq - g_stCfg.dToneHz) <= 10.0,
                "the %.0f Hz tone played by AO is what AI captured", g_stCfg.dToneHz);
    }
    free(stCap.ps16Data);
    CHECK(AI_DisableDev(0) == SYS_ERR_OK, "AI down");
    CHECK(AO_DisableDev(0) == SYS_ERR_OK, "AO down");
}

/* ======================== Enable / disable cycles ======================== */

static U32 count_fds(void) {
    DIR *pstDir = opendir("/proc/self/fd");
    U32 u32Cnt = 0;

    if (pstDir == NULL)
        return 0;
    while (readdir(pstDir) != NULL)
        u32Cnt++;
    closedir(pstDir);

    return u32Cnt;
}

static long rss_kb(void) {
    FILE *pf = fopen("/proc/self/status", "r");
    char szLine[128];
    long lKb = 0;

    if (pf == NULL)
        return 0;
    while (fgets(szLine, sizeof(szLine), pf) != NULL) {
        if (sscanf(szLine, "VmRSS: %ld kB", &lKb) == 1)
            break;
    }
    fclose(pf);

    return lKb;
}

static VOID cmd_cycle(void) {
    U32 u32Fds = 0, u32Failed = 0, i, k;
    AudioFrameS stFrame;
    long lRss = 0;

    for (i = 0; i < g_stCfg.u32Cycles; i++) {
        if (i == 10) { /* past the one-time allocations of alsa-lib and libc */
            u32Fds = count_fds();
            lRss = rss_kb();
        }
        if (ai_start() != SYS_ERR_OK || ao_start() != SYS_ERR_OK) {
            u32Failed++;
            AI_DisableDev(0);
            AO_DisableDev(0);
            continue;
        }
        for (k = 0; k < 3; k++) {
            if (AI_GetFrame(0, 0, &stFrame, NULL, 2000) != SYS_ERR_OK ||
                AI_ReleaseFrame(0, 0, &stFrame, NULL) != SYS_ERR_OK || ao_send_tone((U64)k * ao_points(), 2000) != SYS_ERR_OK)
                u32Failed++;
        }
        if (AI_DisableDev(0) != SYS_ERR_OK || AO_DisableDev(0) != SYS_ERR_OK)
            u32Failed++;
    }
    CHECK(u32Failed == 0, "%u enable / transfer / disable cycles (%u failed steps)", g_stCfg.u32Cycles, u32Failed);
    if (g_stCfg.u32Cycles > 10) {
        CHECK(count_fds() == u32Fds, "file descriptors: %u after 10 cycles, %u at the end", u32Fds, count_fds());
        CHECK(rss_kb() - lRss < 1024, "resident memory: %ld kB after 10 cycles, %ld kB at the end", lRss, rss_kb());
    }
}

/* ======================== Error paths on real hardware ======================== */

static VOID cmd_errors(void) {
    AioDevAttrS stAttr = dev_attr(g_stCfg.u32CapRate, g_stCfg.u32CapCh, g_stCfg.u32CapPoints);
    AioDevAttrS stBad;
    MicAttrS stMic;
    SpkAttrS stSpk;
    S32 ret;

    memset(&stMic, 0, sizeof(stMic));
    memset(&stSpk, 0, sizeof(stSpk));

    /* A card that does not exist. */
    stMic.stMicAttr.s32Dev = 31;
    stMic.stMicAttr.s32Chns = (S32)g_stCfg.u32CapCh;
    AI_SetMicAttr(0, &stMic);
    AI_SetDevAttr(0, &stAttr);
    ret = AI_EnableDev(0);
    CHECK(ret == SYS_ERR_NOT_FOUND, "capture card 31 does not exist: AI_EnableDev returns %d", ret);
    AI_DisableDev(0);

    /* A rate the hardware does not have is refused, not converted behind our back. */
    stMic.stMicAttr.s32Dev = g_stCfg.s32CapCard;
    AI_SetMicAttr(0, &stMic);
    stBad = dev_attr(g_stCfg.u32CapRate == 11025 ? 22050 : 11025, g_stCfg.u32CapCh, 220);
    AI_SetDevAttr(0, &stBad);
    ret = AI_EnableDev(0);
    CHECK(ret == SYS_ERR_INVAL, "capture at %d Hz is not supported by the card: AI_EnableDev returns %d",
        (S32)stBad.eSampleRate, ret);
    AI_DisableDev(0);

    stSpk.stSpkAttr.s32Dev = g_stCfg.s32PlayCard;
    stSpk.stSpkAttr.s32Chns = (S32)g_stCfg.u32PlayCh;
    AO_SetSpkAttr(0, &stSpk);
    stBad = dev_attr(g_stCfg.u32PlayRate == 11025 ? 22050 : 11025, g_stCfg.u32PlayCh, 220);
    AO_SetDevAttr(0, &stBad);
    ret = AO_EnableDev(0);
    CHECK(ret == SYS_ERR_INVAL, "playback at %d Hz is not supported by the card: AO_EnableDev returns %d",
        (S32)stBad.eSampleRate, ret);
    AO_DisableDev(0);

    /* The PCM is exclusive: a second logical device on the same card is told it is busy. */
    ret = ai_start();
    CHECK(ret == SYS_ERR_OK, "AI dev 0 up (ret %d)", ret);
    AI_SetMicAttr(1, &stMic);
    AI_SetDevAttr(1, &stAttr);
    ret = AI_EnableDev(1);
    CHECK(ret == SYS_ERR_BUSY, "AI dev 1 on the same card: AI_EnableDev returns %d", ret);
    AI_DisableDev(1);
    CHECK(AI_SetDevAttr(0, &stAttr) == SYS_ERR_BUSY, "the attribute is read-only while the device is enabled");
    CHECK(AI_DeInit() == SYS_ERR_BUSY, "AI_DeInit refuses while a device is enabled");
    CHECK(AI_DisableDev(0) == SYS_ERR_OK, "AI dev 0 down");
}

static VOID usage(void) {
    printf("usage: test_aio_hw <capture|play|duplex|cycle|errors> [-C card] [-P card] [-r Hz] [-R Hz] [-n ch] [-m ch]\n"
           "                   [-p points] [-q points] [-o Hz] [-i Hz] [-d sec] [-t Hz] [-w file.wav] [-N cycles] [-e]\n");
}

int main(int argc, char **argv) {
    const char *pszCmd = argc > 1 ? argv[1] : "";
    S32 ret;
    int opt;

    g_stCfg.u32CapRate = 16000;
    g_stCfg.u32PlayRate = 48000;
    g_stCfg.u32CapCh = 1;
    g_stCfg.u32PlayCh = 2;
    g_stCfg.u32Seconds = 5;
    g_stCfg.u32Cycles = 100;
    g_stCfg.dToneHz = 1000;
    optind = 2;
    while ((opt = getopt(argc, argv, "C:P:r:R:n:m:p:q:o:i:d:t:w:N:e")) != -1) {
        switch (opt) {
            case 'C': g_stCfg.s32CapCard = atoi(optarg); break;
            case 'P': g_stCfg.s32PlayCard = atoi(optarg); break;
            case 'r': g_stCfg.u32CapRate = (U32)atoi(optarg); break;
            case 'R': g_stCfg.u32PlayRate = (U32)atoi(optarg); break;
            case 'n': g_stCfg.u32CapCh = (U32)atoi(optarg); break;
            case 'm': g_stCfg.u32PlayCh = (U32)atoi(optarg); break;
            case 'p': g_stCfg.u32CapPoints = (U32)atoi(optarg); break;
            case 'q': g_stCfg.u32PlayPoints = (U32)atoi(optarg); break;
            case 'o': g_stCfg.u32AiOutRate = (U32)atoi(optarg); break;
            case 'i': g_stCfg.u32AoInRate = (U32)atoi(optarg); break;
            case 'd': g_stCfg.u32Seconds = (U32)atoi(optarg); break;
            case 't': g_stCfg.dToneHz = atof(optarg); break;
            case 'w': g_stCfg.pszWav = optarg; break;
            case 'N': g_stCfg.u32Cycles = (U32)atoi(optarg); break;
            case 'e': g_stCfg.bExpectTone = MPP_TRUE; break;
            default: usage(); return 2;
        }
    }
    if (g_stCfg.u32CapPoints == 0)
        g_stCfg.u32CapPoints = g_stCfg.u32CapRate / 50;
    if (g_stCfg.u32PlayPoints == 0)
        g_stCfg.u32PlayPoints = g_stCfg.u32PlayRate / 50;
    if (g_stCfg.u32AiOutRate == 0)
        g_stCfg.u32AiOutRate = g_stCfg.u32CapRate;
    if (g_stCfg.u32AoInRate == 0)
        g_stCfg.u32AoInRate = g_stCfg.u32PlayRate;

    ret = AI_Init();
    CHECK(ret == SYS_ERR_OK, "AI_Init (ret %d)", ret);
    ret = AO_Init();
    CHECK(ret == SYS_ERR_OK, "AO_Init (ret %d)", ret);
    if (g_iFailures != 0)
        return g_iFailures;

    if (strcmp(pszCmd, "capture") == 0) {
        cmd_capture();
    } else if (strcmp(pszCmd, "play") == 0) {
        cmd_play();
    } else if (strcmp(pszCmd, "duplex") == 0) {
        cmd_duplex();
    } else if (strcmp(pszCmd, "cycle") == 0) {
        cmd_cycle();
    } else if (strcmp(pszCmd, "errors") == 0) {
        cmd_errors();
    } else {
        usage();
        g_iFailures++;
    }

    CHECK(AI_DeInit() == SYS_ERR_OK, "AI_DeInit");
    CHECK(AO_DeInit() == SYS_ERR_OK, "AO_DeInit");
    printf("%s: %d failure(s)\n", pszCmd, g_iFailures);

    return g_iFailures;
}
