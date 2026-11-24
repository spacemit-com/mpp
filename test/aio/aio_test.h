/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * Shared helpers of the AI/AO tests.
 */

#ifndef AIO_TEST_H
#define AIO_TEST_H

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "aio_type.h"
#include "sys_type.h"

#define REQUIRE(cond)                                                       \
    do {                                                                    \
        if (!(cond)) {                                                      \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                        \
        }                                                                   \
    } while (0)

#define REQUIRE_EQ(actual, expected)                                                                      \
    do {                                                                                                  \
        long long llActual = (long long)(actual);                                                         \
        long long llExpected = (long long)(expected);                                                     \
        if (llActual != llExpected) {                                                                     \
            fprintf(stderr, "FAIL %s:%d: %s is %lld, expected %s = %lld\n", __FILE__, __LINE__, #actual, \
                llActual, #expected, llExpected);                                                         \
            exit(1);                                                                                      \
        }                                                                                                 \
    } while (0)

#define RUN(test)                     \
    do {                              \
        test();                       \
        printf("[PASS] %s\n", #test); \
        fflush(stdout);               \
    } while (0)

#define TEST_PI 3.14159265358979323846

static inline int test_gettid(void) {
    return (int)syscall(SYS_gettid);
}

/*
 * Wait until the thread that stored its test_gettid() in *piTid sleeps in
 * the kernel. A thread that does so right before a call that is expected to
 * block is then known to be blocked in it, without guessing a delay.
 */
static inline BOOL test_wait_blocked(atomic_int *piTid) {
    char szPath[64], szStat[512];
    int i;

    for (i = 0; i < 5000; i++) {
        int iTid = atomic_load(piTid);
        const char *pszState = NULL;
        FILE *pf;

        if (iTid != 0) {
            snprintf(szPath, sizeof(szPath), "/proc/self/task/%d/stat", iTid);
            pf = fopen(szPath, "r");
            if (pf != NULL) {
                /* "<tid> (<comm>) <state> ...": the state follows the last ')'. */
                if (fgets(szStat, sizeof(szStat), pf) != NULL)
                    pszState = strrchr(szStat, ')');
                fclose(pf);
            }
            if (pszState != NULL && pszState[1] == ' ' && pszState[2] == 'S')
                return MPP_TRUE;
        }
        usleep(1000);
    }

    return MPP_FALSE;
}

static inline AioDevAttrS test_dev_attr(U32 u32Rate, U32 u32Channels, U32 u32Period, U32 u32FrmNum) {
    AioDevAttrS stAttr;

    memset(&stAttr, 0, sizeof(stAttr));
    stAttr.eSampleRate = (AudioSampleRate)u32Rate;
    stAttr.eBitWidth = AUDIO_BIT_WIDTH_16;
    stAttr.eSoundMode = u32Channels == 1 ? AUDIO_SOUND_MODE_MONO
                                         : (u32Channels == 2 ? AUDIO_SOUND_MODE_STEREO : AUDIO_SOUND_MODE_RAW);
    stAttr.u32FrmNum = u32FrmNum;
    stAttr.u32PtNumPerFrm = u32Period;
    stAttr.u32ChnCnt = u32Channels;
    stAttr.ePcmFormat = AUDIO_PCM_DEFAULT;

    return stAttr;
}

/*
 * Least-squares fit of a sinusoid of dCyclesPerSample to pfX (every
 * u32Stride-th sample). Gives its amplitude and the RMS of what is left.
 */
static inline VOID test_fit_tone(const float *pfX, U32 u32Count, U32 u32Stride, double dCyclesPerSample,
    double *pdAmplitude, double *pdResidualRms) {
    double dCc = 0, dSs = 0, dCs = 0, dXc = 0, dXs = 0, dA, dB, dDet, dErr = 0;
    U32 i;

    for (i = 0; i < u32Count; i++) {
        double dPhase = 2.0 * TEST_PI * dCyclesPerSample * i;
        double dC = cos(dPhase), dS = sin(dPhase), dX = pfX[(size_t)i * u32Stride];

        dCc += dC * dC;
        dSs += dS * dS;
        dCs += dC * dS;
        dXc += dX * dC;
        dXs += dX * dS;
    }
    dDet = dCc * dSs - dCs * dCs;
    dA = (dXc * dSs - dXs * dCs) / dDet;
    dB = (dXs * dCc - dXc * dCs) / dDet;
    for (i = 0; i < u32Count; i++) {
        double dPhase = 2.0 * TEST_PI * dCyclesPerSample * i;
        double dDiff = pfX[(size_t)i * u32Stride] - (dA * cos(dPhase) + dB * sin(dPhase));

        dErr += dDiff * dDiff;
    }
    *pdAmplitude = sqrt(dA * dA + dB * dB);
    *pdResidualRms = sqrt(dErr / u32Count);
}

static inline double test_db(double dRatio) {
    return 20.0 * log10(dRatio > 1e-12 ? dRatio : 1e-12);
}

#endif /* AIO_TEST_H */
