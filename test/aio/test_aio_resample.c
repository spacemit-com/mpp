/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * Sample rate converter and PCM helper tests. No audio hardware involved:
 * the converter is fed synthetic signals and measured.
 */

#include "aio_internal.h"
#include "aio_resample.h"
#include "aio_test.h"

/* Limits the converter has to meet. */
#define PASSBAND_DB 0.5      /* gain error up to 0.8 x the lower Nyquist frequency */
#define REJECTION_DB (-60.0) /* aliasing, imaging and distortion */

static const U32 g_au32Rates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 64000, 96000};
#define RATE_CNT (sizeof(g_au32Rates) / sizeof(g_au32Rates[0]))

static U32 gcd_u32(U32 a, U32 b) {
    while (b != 0) {
        U32 t = a % b;

        a = b;
        b = t;
    }

    return a;
}

/* ceil(u64Frames * out / in): output frames located before input frame u64Frames. */
static U64 expected_out(U64 u64Frames, U32 u32InRate, U32 u32OutRate) {
    U32 g = gcd_u32(u32InRate, u32OutRate);
    U64 u64Up = u32OutRate / g, u64Down = u32InRate / g;

    return (u64Frames * u64Up + u64Down - 1) / u64Down;
}

/* Feed pfIn in blocks of u32Block frames, collect everything in pfOut. */
static U32 run_blocks(AioResampler *pstRs, const float *pfIn, U32 u32InFrames, U32 u32Ch, U32 u32Block, float *pfOut,
    U32 u32OutCap) {
    U32 u32Done = 0, u32Out = 0;

    while (u32Done < u32InFrames) {
        U32 u32Take = u32InFrames - u32Done < u32Block ? u32InFrames - u32Done : u32Block;

        REQUIRE(u32OutCap - u32Out >= aio_resampler_max_out(pstRs, u32Take));
        u32Out += aio_resampler_process(pstRs, pfIn + (size_t)u32Done * u32Ch, u32Take,
            pfOut + (size_t)u32Out * u32Ch, u32OutCap - u32Out);
        u32Done += u32Take;
    }

    return u32Out;
}

static float *make_tone(U32 u32Frames, double dCyclesPerSample, double dAmplitude) {
    float *pf = (float *)malloc((size_t)u32Frames * sizeof(float));
    U32 i;

    REQUIRE(pf != NULL);
    for (i = 0; i < u32Frames; i++)
        pf[i] = (float)(dAmplitude * sin(2.0 * TEST_PI * dCyclesPerSample * i));

    return pf;
}

static float *make_noise(U32 u32Samples) {
    float *pf = (float *)malloc((size_t)u32Samples * sizeof(float));
    U32 u32State = 12345, i;

    REQUIRE(pf != NULL);
    for (i = 0; i < u32Samples; i++) {
        u32State = u32State * 1664525U + 1013904223U;
        pf[i] = (float)((int32_t)u32State) * (0.5f / 2147483648.0f);
    }

    return pf;
}

/* ======================== Resampler ======================== */

static void test_create_invalid(void) {
    REQUIRE(aio_resampler_create(0, 16000, 1, 320) == NULL);
    REQUIRE(aio_resampler_create(16000, 0, 1, 320) == NULL);
    REQUIRE(aio_resampler_create(16000, 48000, 0, 320) == NULL);
    REQUIRE(aio_resampler_create(16000, 48000, 9, 320) == NULL);
    REQUIRE(aio_resampler_create(16000, 48000, 1, 0) == NULL);
    aio_resampler_destroy(NULL);
}

/* Every pair of supported rates: the number of output frames is exact. */
static void test_frame_count_all_rates(void) {
    U32 i, j;

    for (i = 0; i < RATE_CNT; i++) {
        for (j = 0; j < RATE_CNT; j++) {
            const U32 u32In = g_au32Rates[i], u32Out = g_au32Rates[j];
            const U32 u32Frames = u32In + 123, u32Block = 997;
            AioResampler *pstRs;
            float *pfIn, *pfOut;
            U32 u32Cap, u32Delay, u32Got;

            if (i == j)
                continue;
            pstRs = aio_resampler_create(u32In, u32Out, 1, u32Block);
            REQUIRE(pstRs != NULL);
            u32Delay = aio_resampler_delay(pstRs);
            u32Cap = (U32)expected_out(u32Frames + u32Delay, u32In, u32Out) + 64;
            pfIn = (float *)calloc(u32Frames + u32Delay, sizeof(float));
            pfOut = (float *)malloc((size_t)u32Cap * sizeof(float));
            REQUIRE(pfIn != NULL && pfOut != NULL);

            /* Output lags by the kernel half-width... */
            u32Got = run_blocks(pstRs, pfIn, u32Frames, 1, u32Block, pfOut, u32Cap);
            REQUIRE_EQ(u32Got, expected_out(u32Frames - u32Delay, u32In, u32Out));
            /* ...and that much silence flushes the rest out. */
            u32Got += run_blocks(pstRs, pfIn, u32Delay, 1, u32Block, pfOut + u32Got, u32Cap - u32Got);
            REQUIRE_EQ(u32Got, expected_out(u32Frames, u32In, u32Out));

            free(pfIn);
            free(pfOut);
            aio_resampler_destroy(pstRs);
        }
    }
}

/* Cutting the input into different blocks must not change a single bit. */
static void test_block_invariance(void) {
    static const U32 au32Pair[][2] = {{48000, 16000}, {16000, 48000}, {44100, 48000}, {48000, 44100}, {96000, 8000}};
    static const U32 au32Block[] = {1, 7, 160, 441};
    const U32 u32Frames = 6000, u32Ch = 2;
    float *pfIn = make_noise(u32Frames * u32Ch);
    U32 p, b;

    for (p = 0; p < sizeof(au32Pair) / sizeof(au32Pair[0]); p++) {
        const U32 u32Cap = (U32)expected_out(u32Frames, au32Pair[p][0], au32Pair[p][1]) + 64;
        float *pfRef = (float *)malloc((size_t)u32Cap * u32Ch * sizeof(float));
        float *pfOut = (float *)malloc((size_t)u32Cap * u32Ch * sizeof(float));
        AioResampler *pstRs = aio_resampler_create(au32Pair[p][0], au32Pair[p][1], u32Ch, u32Frames);
        U32 u32Ref;

        REQUIRE(pfRef != NULL && pfOut != NULL && pstRs != NULL);
        u32Ref = run_blocks(pstRs, pfIn, u32Frames, u32Ch, u32Frames, pfRef, u32Cap);
        REQUIRE(u32Ref > 0);
        for (b = 0; b < sizeof(au32Block) / sizeof(au32Block[0]); b++) {
            aio_resampler_reset(pstRs);
            REQUIRE_EQ(run_blocks(pstRs, pfIn, u32Frames, u32Ch, au32Block[b], pfOut, u32Cap), u32Ref);
            REQUIRE(memcmp(pfRef, pfOut, (size_t)u32Ref * u32Ch * sizeof(float)) == 0);
        }
        aio_resampler_destroy(pstRs);
        free(pfRef);
        free(pfOut);
    }
    free(pfIn);
}

static void test_dc_gain(void) {
    static const U32 au32Pair[][2] = {{48000, 16000}, {16000, 48000}, {44100, 48000}, {96000, 8000}, {8000, 96000}};
    U32 p, i;

    for (p = 0; p < sizeof(au32Pair) / sizeof(au32Pair[0]); p++) {
        const U32 u32In = au32Pair[p][0], u32Out = au32Pair[p][1], u32Frames = u32In / 2;
        const U32 u32Cap = (U32)expected_out(u32Frames, u32In, u32Out) + 64;
        AioResampler *pstRs = aio_resampler_create(u32In, u32Out, 1, u32Frames);
        float *pfIn = (float *)malloc((size_t)u32Frames * sizeof(float));
        float *pfOut = (float *)malloc((size_t)u32Cap * sizeof(float));
        U32 u32Got, u32Skip;

        REQUIRE(pstRs != NULL && pfIn != NULL && pfOut != NULL);
        for (i = 0; i < u32Frames; i++)
            pfIn[i] = 0.5f;
        u32Got = run_blocks(pstRs, pfIn, u32Frames, 1, u32Frames, pfOut, u32Cap);
        u32Skip = (U32)expected_out(2 * aio_resampler_delay(pstRs), u32In, u32Out);
        REQUIRE(u32Got > 2 * u32Skip);
        for (i = u32Skip; i < u32Got; i++)
            REQUIRE(fabs(pfOut[i] - 0.5) < 1e-3);
        aio_resampler_destroy(pstRs);
        free(pfIn);
        free(pfOut);
    }
}

/* Output frame k is the input at time k * in / out: no phase delay. */
static void test_impulse_alignment(void) {
    static const U32 au32Pair[][2] = {{48000, 16000}, {16000, 48000}, {44100, 48000}};
    const U32 u32Frames = 4000, u32At = 1500;
    U32 p, i;

    for (p = 0; p < sizeof(au32Pair) / sizeof(au32Pair[0]); p++) {
        const U32 u32In = au32Pair[p][0], u32Out = au32Pair[p][1];
        const U32 u32Cap = (U32)expected_out(u32Frames, u32In, u32Out) + 64;
        AioResampler *pstRs = aio_resampler_create(u32In, u32Out, 1, u32Frames);
        float *pfIn = (float *)calloc(u32Frames, sizeof(float));
        float *pfOut = (float *)malloc((size_t)u32Cap * sizeof(float));
        U32 u32Got, u32Peak = 0;
        long lExpect = lround((double)u32At * u32Out / u32In);

        REQUIRE(pstRs != NULL && pfIn != NULL && pfOut != NULL);
        pfIn[u32At] = 1.0f;
        u32Got = run_blocks(pstRs, pfIn, u32Frames, 1, 512, pfOut, u32Cap);
        for (i = 1; i < u32Got; i++) {
            if (fabsf(pfOut[i]) > fabsf(pfOut[u32Peak]))
                u32Peak = i;
        }
        REQUIRE(labs((long)u32Peak - lExpect) <= 1);
        aio_resampler_destroy(pstRs);
        free(pfIn);
        free(pfOut);
    }
}

/* Resample a tone and report its gain and everything that is not the tone. */
static void measure_tone(U32 u32In, U32 u32Out, double dFreq, double *pdGainDb, double *pdOtherDb) {
    const U32 u32Frames = u32In / 2, u32Block = 480;
    const U32 u32Cap = (U32)expected_out(u32Frames, u32In, u32Out) + 64;
    AioResampler *pstRs = aio_resampler_create(u32In, u32Out, 1, u32Block);
    float *pfIn = make_tone(u32Frames, dFreq / u32In, 0.5);
    float *pfOut = (float *)malloc((size_t)u32Cap * sizeof(float));
    double dAmp, dRes;
    U32 u32Got, u32Skip;

    REQUIRE(pstRs != NULL && pfOut != NULL);
    u32Got = run_blocks(pstRs, pfIn, u32Frames, 1, u32Block, pfOut, u32Cap);
    u32Skip = (U32)expected_out(2 * aio_resampler_delay(pstRs), u32In, u32Out) + 8;
    REQUIRE(u32Got > 4 * u32Skip);
    test_fit_tone(pfOut + u32Skip, u32Got - u32Skip, 1, dFreq / u32Out, &dAmp, &dRes);
    *pdGainDb = test_db(dAmp / 0.5);
    /* Relative to the RMS of the input tone. */
    *pdOtherDb = test_db(dRes / (0.5 / sqrt(2.0)));
    aio_resampler_destroy(pstRs);
    free(pfIn);
    free(pfOut);
}

/* RMS of the output for an input tone the converter has to remove. */
static double measure_leak_db(U32 u32In, U32 u32Out, double dFreq) {
    const U32 u32Frames = u32In / 2, u32Block = 480;
    const U32 u32Cap = (U32)expected_out(u32Frames, u32In, u32Out) + 64;
    AioResampler *pstRs = aio_resampler_create(u32In, u32Out, 1, u32Block);
    float *pfIn = make_tone(u32Frames, dFreq / u32In, 0.5);
    float *pfOut = (float *)malloc((size_t)u32Cap * sizeof(float));
    double dSum = 0;
    U32 u32Got, u32Skip, i;

    REQUIRE(pstRs != NULL && pfOut != NULL);
    u32Got = run_blocks(pstRs, pfIn, u32Frames, 1, u32Block, pfOut, u32Cap);
    u32Skip = (U32)expected_out(2 * aio_resampler_delay(pstRs), u32In, u32Out) + 8;
    REQUIRE(u32Got > 4 * u32Skip);
    for (i = u32Skip; i < u32Got; i++)
        dSum += (double)pfOut[i] * pfOut[i];
    aio_resampler_destroy(pstRs);
    free(pfIn);
    free(pfOut);

    return test_db(sqrt(dSum / (u32Got - u32Skip)) / (0.5 / sqrt(2.0)));
}

static const U32 g_au32QualityPair[][2] = {{48000, 16000}, {16000, 48000}, {44100, 48000}, {48000, 44100},
    {48000, 8000}, {8000, 48000}, {96000, 8000}, {8000, 96000}, {32000, 22050}, {11025, 64000}};
#define QUALITY_PAIR_CNT (sizeof(g_au32QualityPair) / sizeof(g_au32QualityPair[0]))

/* Flat pass band, and nothing but the tone comes out (images, distortion). */
static void test_passband(void) {
    double dWorstGain = 0, dWorstOther = -200;
    U32 p, k;

    for (p = 0; p < QUALITY_PAIR_CNT; p++) {
        const U32 u32In = g_au32QualityPair[p][0], u32Out = g_au32QualityPair[p][1];
        const double dNyquist = 0.5 * (u32In < u32Out ? u32In : u32Out);

        for (k = 1; k <= 8; k++) {
            double dGain, dOther;

            measure_tone(u32In, u32Out, 0.1 * k * dNyquist, &dGain, &dOther);
            if (fabs(dGain) > fabs(dWorstGain))
                dWorstGain = dGain;
            if (dOther > dWorstOther)
                dWorstOther = dOther;
            if (fabs(dGain) > PASSBAND_DB || dOther > REJECTION_DB) {
                fprintf(stderr, "%u -> %u Hz, tone %.0f Hz: gain %.3f dB, residual %.1f dB\n", u32In, u32Out,
                    0.1 * k * dNyquist, dGain, dOther);
                REQUIRE(0);
            }
        }
    }
    printf("       worst pass-band gain error %.3f dB, worst residual %.1f dB\n", dWorstGain, dWorstOther);
}

/* Decimation: what lies above the output Nyquist frequency must not fold back. */
static void test_alias_rejection(void) {
    static const double adRel[] = {1.02, 1.1, 1.3, 1.7, 2.5};
    double dWorst = -200;
    U32 p, k;

    for (p = 0; p < QUALITY_PAIR_CNT; p++) {
        const U32 u32In = g_au32QualityPair[p][0], u32Out = g_au32QualityPair[p][1];

        if (u32Out >= u32In)
            continue;
        for (k = 0; k < sizeof(adRel) / sizeof(adRel[0]); k++) {
            double dFreq = adRel[k] * 0.5 * u32Out, dLeak;

            if (dFreq >= 0.49 * u32In)
                continue;
            dLeak = measure_leak_db(u32In, u32Out, dFreq);
            if (dLeak > dWorst)
                dWorst = dLeak;
            if (dLeak > REJECTION_DB) {
                fprintf(stderr, "%u -> %u Hz, tone %.0f Hz leaks at %.1f dB\n", u32In, u32Out, dFreq, dLeak);
                REQUIRE(0);
            }
        }
    }
    printf("       worst alias leak %.1f dB\n", dWorst);
}

static void test_reset(void) {
    const U32 u32Frames = 3000, u32Cap = 1200;
    AioResampler *pstRs = aio_resampler_create(48000, 16000, 1, u32Frames);
    float *pfIn = make_noise(u32Frames);
    float afA[1200], afB[1200];
    U32 u32A, u32B;

    REQUIRE(pstRs != NULL);
    u32A = run_blocks(pstRs, pfIn, u32Frames, 1, 333, afA, u32Cap);
    aio_resampler_reset(pstRs);
    u32B = run_blocks(pstRs, pfIn, u32Frames, 1, 333, afB, u32Cap);
    REQUIRE_EQ(u32A, u32B);
    REQUIRE(memcmp(afA, afB, (size_t)u32A * sizeof(float)) == 0);
    aio_resampler_destroy(pstRs);
    free(pfIn);
}

/* ======================== PCM helpers ======================== */

static void test_pcm_convert(void) {
    static const int32_t as32S24[] = {-8388608, -8388607, -1, 0, 1, 8388607};
    static const int32_t as32S32[] = {INT32_MIN, -65536, -256, 0, 256, 65536, 0x7FFFFF00};
    float fVal, afOut[8];
    int16_t s16In, s16Out;
    int32_t s32In, s32Out;
    uint32_t u32Raw;
    U8 au8Buf[8];
    S32 v;
    U32 i;

    /* 16 bit: every value survives the round trip. */
    for (v = -32768; v <= 32767; v++) {
        s16In = (int16_t)v;
        aio_pcm_to_float(&s16In, &fVal, 1, AUDIO_PCM_S16_LE);
        aio_float_to_pcm(&fVal, &s16Out, 1, AUDIO_PCM_S16_LE);
        REQUIRE_EQ(s16Out, s16In);
    }
    for (i = 0; i < sizeof(as32S24) / sizeof(as32S24[0]); i++) {
        s32In = as32S24[i];
        aio_pcm_to_float(&s32In, &fVal, 1, AUDIO_PCM_S24_4LE);
        aio_float_to_pcm(&fVal, &s32Out, 1, AUDIO_PCM_S24_4LE);
        REQUIRE_EQ(s32Out, s32In);
    }
    /* Only the low 24 bits count, whatever the top byte holds. */
    u32Raw = 0x00800000U;
    aio_pcm_to_float(&u32Raw, &fVal, 1, AUDIO_PCM_S24_4LE);
    REQUIRE(fVal == -1.0f);
    u32Raw = 0xAB7FFFFFU;
    aio_pcm_to_float(&u32Raw, &fVal, 1, AUDIO_PCM_S24_4LE);
    REQUIRE(fVal == 8388607.0f / 8388608.0f);
    for (i = 0; i < sizeof(as32S32) / sizeof(as32S32[0]); i++) {
        s32In = as32S32[i];
        aio_pcm_to_float(&s32In, &fVal, 1, AUDIO_PCM_S32_LE);
        aio_float_to_pcm(&fVal, &s32Out, 1, AUDIO_PCM_S32_LE);
        REQUIRE_EQ(s32Out, s32In);
    }
    fVal = 0.123f;
    aio_float_to_pcm(&fVal, au8Buf, 1, AUDIO_PCM_F32_LE);
    aio_pcm_to_float(au8Buf, afOut, 1, AUDIO_PCM_F32_LE);
    REQUIRE(afOut[0] == 0.123f);

    /* Out of range saturates, it never wraps; NaN becomes silence. */
    fVal = 2.0f;
    aio_float_to_pcm(&fVal, &s16Out, 1, AUDIO_PCM_S16_LE);
    REQUIRE_EQ(s16Out, 32767);
    aio_float_to_pcm(&fVal, &s32Out, 1, AUDIO_PCM_S24_4LE);
    REQUIRE_EQ(s32Out, 8388607);
    aio_float_to_pcm(&fVal, &s32Out, 1, AUDIO_PCM_S32_LE);
    REQUIRE_EQ(s32Out, INT32_MAX);
    fVal = -2.0f;
    aio_float_to_pcm(&fVal, &s16Out, 1, AUDIO_PCM_S16_LE);
    REQUIRE_EQ(s16Out, -32768);
    aio_float_to_pcm(&fVal, &s32Out, 1, AUDIO_PCM_S24_4LE);
    REQUIRE_EQ(s32Out, -8388608);
    aio_float_to_pcm(&fVal, &s32Out, 1, AUDIO_PCM_S32_LE);
    REQUIRE_EQ(s32Out, INT32_MIN);
    fVal = nanf("");
    aio_float_to_pcm(&fVal, &s16Out, 1, AUDIO_PCM_S16_LE);
    REQUIRE_EQ(s16Out, 0);
    aio_float_to_pcm(&fVal, &s32Out, 1, AUDIO_PCM_S32_LE);
    REQUIRE_EQ(s32Out, 0);

    memset(au8Buf, 0x55, sizeof(au8Buf));
    aio_fill_silence(au8Buf, 4, AUDIO_PCM_U8);
    REQUIRE(au8Buf[0] == 0x80 && au8Buf[3] == 0x80 && au8Buf[4] == 0x55);
    aio_fill_silence(au8Buf, 2, AUDIO_PCM_S24_3LE);
    REQUIRE(au8Buf[0] == 0 && au8Buf[5] == 0 && au8Buf[6] == 0x55);
}

static void test_format_helpers(void) {
    AudioPcmFormat eFormat;

    REQUIRE_EQ(aio_format_bytes(AUDIO_PCM_U8), 1);
    REQUIRE_EQ(aio_format_bytes(AUDIO_PCM_S16_LE), 2);
    REQUIRE_EQ(aio_format_bytes(AUDIO_PCM_S24_3LE), 3);
    REQUIRE_EQ(aio_format_bytes(AUDIO_PCM_S24_4LE), 4);
    REQUIRE_EQ(aio_format_bytes(AUDIO_PCM_F32_LE), 4);

    REQUIRE_EQ(aio_resolve_format(AUDIO_BIT_WIDTH_8, AUDIO_PCM_DEFAULT, &eFormat), SYS_ERR_OK);
    REQUIRE_EQ(eFormat, AUDIO_PCM_U8);
    REQUIRE_EQ(aio_resolve_format(AUDIO_BIT_WIDTH_16, AUDIO_PCM_DEFAULT, &eFormat), SYS_ERR_OK);
    REQUIRE_EQ(eFormat, AUDIO_PCM_S16_LE);
    REQUIRE_EQ(aio_resolve_format(AUDIO_BIT_WIDTH_24, AUDIO_PCM_DEFAULT, &eFormat), SYS_ERR_OK);
    REQUIRE_EQ(eFormat, AUDIO_PCM_S24_4LE);
    REQUIRE_EQ(aio_resolve_format(AUDIO_BIT_WIDTH_32, AUDIO_PCM_DEFAULT, &eFormat), SYS_ERR_OK);
    REQUIRE_EQ(eFormat, AUDIO_PCM_S32_LE);
    REQUIRE_EQ(aio_resolve_format(AUDIO_BIT_WIDTH_32, AUDIO_PCM_F32_LE, &eFormat), SYS_ERR_OK);
    REQUIRE_EQ(aio_resolve_format(AUDIO_BIT_WIDTH_24, AUDIO_PCM_S24_3LE, &eFormat), SYS_ERR_OK);
    REQUIRE_EQ(aio_resolve_format(AUDIO_BIT_WIDTH_16, AUDIO_PCM_S32_LE, &eFormat), SYS_ERR_INVAL);
    REQUIRE_EQ(aio_resolve_format(AUDIO_BIT_WIDTH_MAX, AUDIO_PCM_DEFAULT, &eFormat), SYS_ERR_INVAL);

    /* Channel frame size next to a device frame, see the resampling rules. */
    REQUIRE_EQ(aio_scale_frames(960, 48000, 16000), 320);
    REQUIRE_EQ(aio_scale_frames(1024, 48000, 16000), 341);
    REQUIRE_EQ(aio_scale_frames(960, 48000, 44100), 882);
    REQUIRE_EQ(aio_scale_frames(441, 44100, 48000), 480);
    REQUIRE_EQ(aio_scale_frames(320, 16000, 48000), 960);

    /* About eight months of audio must not overflow the nanosecond math. */
    REQUIRE(aio_frames_to_ns(1000000000000ULL, 48000) == 20833333333333333ULL);
    REQUIRE(aio_frames_to_ns(16000, 16000) == AIO_NS_PER_SEC);
}

int main(void) {
    RUN(test_create_invalid);
    RUN(test_frame_count_all_rates);
    RUN(test_block_invariance);
    RUN(test_dc_gain);
    RUN(test_impulse_alignment);
    RUN(test_passband);
    RUN(test_alias_rejection);
    RUN(test_reset);
    RUN(test_pcm_convert);
    RUN(test_format_helpers);

    return 0;
}
