/*
 *------------------------------------------------------------------------------
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * @File      :    aio_resample.c
 * @Date      :    2026-11-24
 * @Author    :    SPACEMIT
 * @Brief     :    Streaming band-limited sample rate converter.
 *
 * Band-limited interpolation with a Kaiser-windowed sinc kernel. The kernel
 * is tabulated once, oversampled, and read with linear interpolation, so
 * any ratio works without a per-ratio filter design. When decimating the
 * kernel is stretched by in/out, which moves its cutoff below the output
 * Nyquist frequency and makes it the anti-aliasing filter.
 *
 * The output position is tracked with an integer phase accumulator: the
 * number of output frames never drifts from in * out / in_rate, however
 * the input is cut into blocks. A ratio out/in = up/down only ever visits
 * `up` different phases, so their coefficients are worked out once per
 * converter and each output frame is a plain dot product.
 *------------------------------------------------------------------------------
 */

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "aio_resample.h"

#define RS_ZEROS 32   /* kernel zero crossings on each side */
#define RS_RES 512    /* table entries per zero crossing */
#define RS_BETA 7.857 /* Kaiser beta, about 80 dB stop band */
/* -6 dB point relative to the lower Nyquist frequency. With the transition
 * width of the window above, the stop band then starts right at Nyquist. */
#define RS_ROLLOFF 0.92
#define RS_TABLE_LEN (RS_ZEROS * RS_RES)
#define RS_MAX_CHANNELS 8
/* Largest per-phase coefficient table (floats); beyond it they are computed per output frame. */
#define RS_MAX_PHASE_COEFS (1U << 17)
#define RS_PI 3.14159265358979323846

struct _AioResampler {
    U32 u32Channels;
    U32 u32Up;     /* out_rate / gcd */
    U32 u32Down;   /* in_rate / gcd */
    U32 u32Half;   /* input frames the kernel spans on each side */
    U32 u32Taps;   /* 2 * u32Half */
    double dScale; /* kernel stretch, also its gain */
    double dStep;  /* table entries per input frame */
    float *pfCoef; /* u32Taps coefficients for each of the u32Up phases, or for one phase */
    BOOL bPhases;  /* pfCoef holds every phase */
    float *pfBuf;  /* input history, interleaved */
    U32 u32Cap;    /* frames pfBuf can hold */
    U32 u32Len;    /* frames in pfBuf */
    U32 u32Pos;    /* pfBuf index of the input frame at or before the next output */
    U32 u32Phase;  /* fraction of the next output position, in 1 / u32Up */
};

static float g_afRsTable[RS_TABLE_LEN + 2];
static pthread_once_t g_stRsTableOnce = PTHREAD_ONCE_INIT;

static double rs_bessel_i0(double x) {
    double dSum = 1.0;
    double dTerm = 1.0;
    U32 k;

    for (k = 1; k < 64; k++) {
        dTerm *= (x / (2.0 * k)) * (x / (2.0 * k));
        dSum += dTerm;
        if (dTerm < dSum * 1e-15)
            break;
    }

    return dSum;
}

static VOID rs_table_init(VOID) {
    double dI0Beta = rs_bessel_i0(RS_BETA);
    U32 i;

    for (i = 0; i <= RS_TABLE_LEN; i++) {
        double x = (double)i / RS_RES;
        double r = x / RS_ZEROS;
        double dWin = rs_bessel_i0(RS_BETA * sqrt(1.0 - r * r)) / dI0Beta;
        double dSinc = (i == 0) ? 1.0 : sin(RS_PI * x) / (RS_PI * x);

        g_afRsTable[i] = (float)(dSinc * dWin);
    }
    g_afRsTable[RS_TABLE_LEN + 1] = 0.0f;
}

static U32 rs_gcd(U32 a, U32 b) {
    while (b != 0) {
        U32 t = a % b;

        a = b;
        b = t;
    }

    return a;
}

/* Kernel value at table position dPos, already scaled; zero outside the kernel. */
static inline float rs_weight(const AioResampler *pstRs, double dPos) {
    U32 u32Idx;
    float fFrac;

    if (dPos >= RS_TABLE_LEN)
        return 0.0f;
    u32Idx = (U32)dPos;
    fFrac = (float)(dPos - u32Idx);

    return (g_afRsTable[u32Idx] + fFrac * (g_afRsTable[u32Idx + 1] - g_afRsTable[u32Idx])) * (float)pstRs->dScale;
}

/*
 * Coefficients of an output frame u32Phase / u32Up past input frame P.
 * pfCoef[k] belongs to input frame P - u32Half + 1 + k.
 */
static VOID rs_phase_coefs(const AioResampler *pstRs, U32 u32Phase, float *pfCoef) {
    const double dFrac = (double)u32Phase / pstRs->u32Up;
    const double dLeft = dFrac * pstRs->dStep;
    const double dRight = (1.0 - dFrac) * pstRs->dStep;
    U32 j;

    for (j = 0; j < pstRs->u32Half; j++) {
        pfCoef[pstRs->u32Half - 1 - j] = rs_weight(pstRs, dLeft + j * pstRs->dStep);
        pfCoef[pstRs->u32Half + j] = rs_weight(pstRs, dRight + j * pstRs->dStep);
    }
}

AioResampler *aio_resampler_create(U32 u32InRate, U32 u32OutRate, U32 u32Channels, U32 u32MaxInFrames) {
    AioResampler *pstRs;
    U32 u32Gcd, u32Phases, p;

    if (u32InRate == 0 || u32OutRate == 0 || u32Channels == 0 || u32Channels > RS_MAX_CHANNELS ||
        u32MaxInFrames == 0)
        return NULL;

    pthread_once(&g_stRsTableOnce, rs_table_init);

    pstRs = (AioResampler *)calloc(1, sizeof(*pstRs));
    if (pstRs == NULL)
        return NULL;

    u32Gcd = rs_gcd(u32InRate, u32OutRate);
    pstRs->u32Channels = u32Channels;
    pstRs->u32Up = u32OutRate / u32Gcd;
    pstRs->u32Down = u32InRate / u32Gcd;
    pstRs->dScale = RS_ROLLOFF * (u32OutRate < u32InRate ? (double)u32OutRate / u32InRate : 1.0);
    pstRs->dStep = pstRs->dScale * RS_RES;
    pstRs->u32Half = (U32)ceil(RS_ZEROS / pstRs->dScale);
    pstRs->u32Taps = 2 * pstRs->u32Half;
    pstRs->u32Cap = pstRs->u32Taps + u32MaxInFrames + 1;
    pstRs->bPhases = ((U64)pstRs->u32Up * pstRs->u32Taps <= RS_MAX_PHASE_COEFS);
    u32Phases = pstRs->bPhases ? pstRs->u32Up : 1;

    pstRs->pfBuf = (float *)calloc((size_t)pstRs->u32Cap * u32Channels, sizeof(float));
    pstRs->pfCoef = (float *)calloc((size_t)u32Phases * pstRs->u32Taps, sizeof(float));
    if (pstRs->pfBuf == NULL || pstRs->pfCoef == NULL) {
        aio_resampler_destroy(pstRs);
        return NULL;
    }
    if (pstRs->bPhases) {
        for (p = 0; p < u32Phases; p++)
            rs_phase_coefs(pstRs, p, pstRs->pfCoef + (size_t)p * pstRs->u32Taps);
    }
    aio_resampler_reset(pstRs);

    return pstRs;
}

VOID aio_resampler_destroy(AioResampler *pstRs) {
    if (pstRs == NULL)
        return;
    free(pstRs->pfCoef);
    free(pstRs->pfBuf);
    free(pstRs);
}

VOID aio_resampler_reset(AioResampler *pstRs) {
    /* Silence before the first input frame, which will land at u32Half. */
    memset(pstRs->pfBuf, 0, (size_t)pstRs->u32Half * pstRs->u32Channels * sizeof(float));
    pstRs->u32Len = pstRs->u32Half;
    pstRs->u32Pos = pstRs->u32Half;
    pstRs->u32Phase = 0;
}

U32 aio_resampler_max_out(const AioResampler *pstRs, U32 u32InFrames) {
    return (U32)(((U64)u32InFrames * pstRs->u32Up + pstRs->u32Down - 1) / pstRs->u32Down) + 1;
}

U32 aio_resampler_delay(const AioResampler *pstRs) {
    return pstRs->u32Half;
}

/* One output frame at pfBuf[u32Pos] + u32Phase / u32Up. */
static VOID rs_output(AioResampler *pstRs, float *pfOut) {
    const U32 u32Ch = pstRs->u32Channels;
    const U32 u32Taps = pstRs->u32Taps;
    const float *pfIn = pstRs->pfBuf + (size_t)(pstRs->u32Pos + 1 - pstRs->u32Half) * u32Ch;
    const float *pfCoef = pstRs->pfCoef;
    U32 k, c;

    if (pstRs->bPhases)
        pfCoef += (size_t)pstRs->u32Phase * u32Taps;
    else
        rs_phase_coefs(pstRs, pstRs->u32Phase, pstRs->pfCoef);

    /* Separate partial sums keep the multiply-adds independent of each other. */
    if (u32Ch == 1) {
        float f0 = 0.0f, f1 = 0.0f, f2 = 0.0f, f3 = 0.0f;

        for (k = 0; k + 4 <= u32Taps; k += 4) {
            f0 += pfCoef[k] * pfIn[k];
            f1 += pfCoef[k + 1] * pfIn[k + 1];
            f2 += pfCoef[k + 2] * pfIn[k + 2];
            f3 += pfCoef[k + 3] * pfIn[k + 3];
        }
        for (; k < u32Taps; k++)
            f0 += pfCoef[k] * pfIn[k];
        pfOut[0] = (f0 + f1) + (f2 + f3);
    } else if (u32Ch == 2) {
        float fL0 = 0.0f, fL1 = 0.0f, fR0 = 0.0f, fR1 = 0.0f;

        /* u32Taps is even. */
        for (k = 0; k < u32Taps; k += 2) {
            fL0 += pfCoef[k] * pfIn[2 * k];
            fR0 += pfCoef[k] * pfIn[2 * k + 1];
            fL1 += pfCoef[k + 1] * pfIn[2 * k + 2];
            fR1 += pfCoef[k + 1] * pfIn[2 * k + 3];
        }
        pfOut[0] = fL0 + fL1;
        pfOut[1] = fR0 + fR1;
    } else {
        float afAcc[RS_MAX_CHANNELS] = {0};

        for (k = 0; k < u32Taps; k++) {
            const float fW = pfCoef[k];

            for (c = 0; c < u32Ch; c++)
                afAcc[c] += fW * pfIn[(size_t)k * u32Ch + c];
        }
        for (c = 0; c < u32Ch; c++)
            pfOut[c] = afAcc[c];
    }
}

U32 aio_resampler_process(AioResampler *pstRs, const float *pfIn, U32 u32InFrames, float *pfOut, U32 u32OutCap) {
    const U32 u32Ch = pstRs->u32Channels;
    U32 u32Out = 0;

    while (u32InFrames > 0 || pstRs->u32Pos + pstRs->u32Half < pstRs->u32Len) {
        U32 u32Room = pstRs->u32Cap - pstRs->u32Len;
        U32 u32Take = u32InFrames < u32Room ? u32InFrames : u32Room;
        U32 u32Drop;

        memcpy(pstRs->pfBuf + (size_t)pstRs->u32Len * u32Ch, pfIn, (size_t)u32Take * u32Ch * sizeof(float));
        pstRs->u32Len += u32Take;
        pfIn += (size_t)u32Take * u32Ch;
        u32InFrames -= u32Take;

        while (pstRs->u32Pos + pstRs->u32Half < pstRs->u32Len && u32Out < u32OutCap) {
            rs_output(pstRs, pfOut + (size_t)u32Out * u32Ch);
            u32Out++;
            pstRs->u32Phase += pstRs->u32Down;
            pstRs->u32Pos += pstRs->u32Phase / pstRs->u32Up;
            pstRs->u32Phase %= pstRs->u32Up;
        }

        /* Frames older than u32Pos - u32Half + 1 are never read again. */
        u32Drop = pstRs->u32Pos + 1 > pstRs->u32Half ? pstRs->u32Pos + 1 - pstRs->u32Half : 0;
        if (u32Drop > pstRs->u32Len)
            u32Drop = pstRs->u32Len;
        if (u32Drop > 0) {
            memmove(pstRs->pfBuf, pstRs->pfBuf + (size_t)u32Drop * u32Ch,
                (size_t)(pstRs->u32Len - u32Drop) * u32Ch * sizeof(float));
            pstRs->u32Len -= u32Drop;
            pstRs->u32Pos -= u32Drop;
        }

        /* Output buffer full: the caller broke the max_out() contract. */
        if (u32Out >= u32OutCap)
            break;
        if (u32Take == 0 && u32Drop == 0)
            break;
    }

    return u32Out;
}
