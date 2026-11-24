/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * AO (audio output) interface tests against the scripted pcm backend:
 * life cycle, playback, resampling and recovery from device faults.
 * No audio hardware involved.
 */

#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "ai_api.h"
#include "ao_api.h"
#include "aio_fake_backend.h"
#include "aio_internal.h"
#include "aio_test.h"

#define RATE 16000U
#define PERIOD 320U /* 20 ms */
#define FRM_NUM 4U
#define PREFILL (FRM_NUM * PERIOD)
#define QUEUE_CAP (AIO_AO_QUEUE_FACTOR * FRM_NUM)

static void ao_up(U32 u32Rate, U32 u32Channels, U32 u32Period) {
    AioDevAttrS stAttr = test_dev_attr(u32Rate, u32Channels, u32Period, FRM_NUM);

    fake_reset();
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
}

static void ao_down(void) {
    REQUIRE_EQ(AO_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_DeInit(), SYS_ERR_OK);
    REQUIRE_EQ(fake_stats()->u32Opens, fake_stats()->u32Closes);
}

static AudioFrameS make_frame(U8 *pu8Data, U32 u32Rate, U32 u32Channels, U32 u32Points) {
    AudioFrameS stFrame;

    memset(&stFrame, 0, sizeof(stFrame));
    stFrame.eBitWidth = AUDIO_BIT_WIDTH_16;
    stFrame.eSoundMode = u32Channels == 1 ? AUDIO_SOUND_MODE_MONO : AUDIO_SOUND_MODE_STEREO;
    stFrame.eSampleRate = (AudioSampleRate)u32Rate;
    stFrame.s32Channels = (S32)u32Channels;
    stFrame.pu8Data = pu8Data;
    stFrame.u32Len = u32Points * u32Channels * 2;

    return stFrame;
}

/* Send a mono 16-bit frame holding the ramp u32First, u32First + 1, ... */
static S32 send_ramp(U32 u32First, S32 s32MilliSec) {
    int16_t as16[PERIOD];
    AudioFrameS stFrame = make_frame((U8 *)as16, RATE, 1, PERIOD);
    S32 ret;
    U32 i;

    for (i = 0; i < PERIOD; i++)
        as16[i] = (int16_t)(u32First + i);
    ret = AO_SendFrame(0, 0, &stFrame, s32MilliSec);
    /* The payload was copied: scribbling over it must not reach the speaker. */
    memset(as16, 0x5A, sizeof(as16));

    return ret;
}

static int16_t played_s16(U64 u64Sample) {
    int16_t s16;

    memcpy(&s16, fake_playback_data() + u64Sample * 2, 2);
    return s16;
}

/* ======================== Life cycle ======================== */

static void test_init_deinit(void) {
    fake_reset();
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK); /* idempotent */
    REQUIRE_EQ(fake_stats()->u32Inits, 1);
    REQUIRE_EQ(AO_DeInit(), SYS_ERR_OK);
    REQUIRE_EQ(AO_DeInit(), SYS_ERR_OK);
    REQUIRE_EQ(fake_stats()->u32Deinits, 1);

    /* AI and AO share one backend instance; it lives until both are done. */
    fake_reset();
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(fake_stats()->u32Loads, 1);
    REQUIRE_EQ(fake_stats()->u32Inits, 1);
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);
    REQUIRE_EQ(fake_stats()->u32Unloads, 0);
    REQUIRE_EQ(AO_DeInit(), SYS_ERR_OK);
    REQUIRE_EQ(fake_stats()->u32Deinits, 1);
    REQUIRE_EQ(fake_stats()->u32Unloads, 1);

    fake_reset();
    fake_set_load_error(SYS_ERR_NOT_FOUND);
    REQUIRE_EQ(AO_Init(), SYS_ERR_NOT_FOUND);
    fake_set_load_error(0);
}

static void test_not_initialized(void) {
    AioDevAttrS stAttr = test_dev_attr(RATE, 1, PERIOD, FRM_NUM);
    int16_t as16[PERIOD] = {0};
    AudioFrameS stFrame = make_frame((U8 *)as16, RATE, 1, PERIOD);
    AoChnStateS stStat;
    SpkAttrS stSpk;

    memset(&stSpk, 0, sizeof(stSpk));
    fake_reset();
    REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_GetDevAttr(0, &stAttr), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_SetSpkAttr(0, &stSpk), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_GetSpkAttr(0, &stSpk), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_DisableDev(0), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_DisableChn(0, 0), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_SendFrame(0, 0, &stFrame, AIO_NO_BLOCK), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_DisableReSmp(0, 0), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(fake_stats()->u32Loads, 0);
}

static void test_dev_and_spk_attr(void) {
    AioDevAttrS stAttr = test_dev_attr(48000, 2, 960, FRM_NUM);
    AioDevAttrS stGot, stBad;
    const AioAlPcmCfg *pstCfg;
    SpkAttrS stSpk, stSpkGot;

    fake_reset();
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_GetDevAttr(0, &stGot), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_INVAL); /* no attribute yet */
    REQUIRE_EQ(AO_SetDevAttr(AO_MAX_DEV_NUM, &stAttr), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SetDevAttr(0, NULL), SYS_ERR_INVAL);
    stBad = stAttr;
    stBad.u32FrmNum = 11;
    REQUIRE_EQ(AO_SetDevAttr(0, &stBad), SYS_ERR_INVAL);
    stBad = stAttr;
    stBad.eSoundMode = AUDIO_SOUND_MODE_MONO;
    REQUIRE_EQ(AO_SetDevAttr(0, &stBad), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AO_GetDevAttr(0, &stGot), SYS_ERR_OK);
    REQUIRE(memcmp(&stGot, &stAttr, sizeof(stAttr)) == 0);

    REQUIRE_EQ(AO_GetSpkAttr(0, &stSpkGot), SYS_ERR_OK); /* defaults: card 0, one SPK */
    REQUIRE_EQ(stSpkGot.stSpkAttr.s32Dev, 0);
    REQUIRE_EQ(stSpkGot.stSpkAttr.s32Chns, 1);
    memset(&stSpk, 0, sizeof(stSpk));
    stSpk.stSpkAttr.s32Dev = 2;
    stSpk.stSpkAttr.s32Chns = 0;
    REQUIRE_EQ(AO_SetSpkAttr(0, &stSpk), SYS_ERR_INVAL);
    stSpk.stSpkAttr.s32Chns = 1; /* disagrees with u32ChnCnt = 2 */
    REQUIRE_EQ(AO_SetSpkAttr(0, &stSpk), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_INVAL);
    stSpk.stSpkAttr.s32Chns = 2;
    REQUIRE_EQ(AO_SetSpkAttr(0, &stSpk), SYS_ERR_OK);
    REQUIRE_EQ(AO_GetSpkAttr(0, &stSpkGot), SYS_ERR_OK);
    REQUIRE(memcmp(&stSpkGot, &stSpk, sizeof(stSpk)) == 0);

    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
    pstCfg = &fake_stats()->astLastCfg[AIO_AL_DIR_PLAYBACK];
    REQUIRE_EQ(pstCfg->eDir, AIO_AL_DIR_PLAYBACK);
    REQUIRE_EQ(pstCfg->s32Card, 2);
    REQUIRE_EQ(pstCfg->u32Rate, 48000);
    REQUIRE_EQ(pstCfg->u32Channels, 2);
    REQUIRE_EQ(pstCfg->ePcmFormat, AUDIO_PCM_S16_LE);
    REQUIRE_EQ(pstCfg->u32PeriodFrames, 960);
    REQUIRE_EQ(pstCfg->u32BufferFrames, FRM_NUM * 960);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(fake_stats()->u32Opens, 1);
    REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_BUSY); /* read-only now */
    REQUIRE_EQ(AO_SetSpkAttr(0, &stSpk), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_DeInit(), SYS_ERR_BUSY);
    ao_down();

    fake_reset();
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    fake_set_open_error(AIO_AL_ERR_NOENT);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_NOT_FOUND);
    fake_set_open_error(AIO_AL_ERR_BUSY);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_BUSY);
    fake_set_open_error(AIO_AL_ERR_UNSUP);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
    ao_down();
}

/* EnableDev starts the playback clock with u32FrmNum frames of silence. */
static void test_prefill(void) {
    U32 i;

    ao_up(RATE, 1, PERIOD);
    REQUIRE(fake_wait_idle(AIO_AL_DIR_PLAYBACK));
    REQUIRE_EQ(fake_playback_frames(), PREFILL);
    for (i = 0; i < PREFILL; i++)
        REQUIRE_EQ(played_s16(i), 0);
    REQUIRE(fake_stats()->u32Starts >= 1);

    /* With nothing to play the device still gets a period every period. */
    REQUIRE(fake_playback_advance(5));
    REQUIRE_EQ(fake_playback_frames(), PREFILL + 5 * PERIOD);
    REQUIRE_EQ(fake_stats()->u32Recovers, 0);
    ao_down();

    /* Unsigned 8-bit silence is 0x80, not zero. */
    {
        AioDevAttrS stAttr = test_dev_attr(RATE, 1, PERIOD, FRM_NUM);

        stAttr.eBitWidth = AUDIO_BIT_WIDTH_8;
        fake_reset();
        REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
        REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_OK);
        REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
        REQUIRE(fake_playback_advance(1));
        REQUIRE_EQ(fake_playback_frames(), PREFILL + PERIOD);
        REQUIRE_EQ(fake_playback_data()[0], 0x80);
        REQUIRE_EQ(fake_playback_data()[PREFILL + PERIOD - 1], 0x80);
        ao_down();
    }
}

static void test_chn_enable(void) {
    AoChnStateS stStat;

    fake_reset();
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_NOT_INIT); /* device not enabled */
    REQUIRE_EQ(AO_DeInit(), SYS_ERR_OK);

    ao_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AO_EnableChn(0, -1), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_EnableChn(0, AO_MAX_CHN_NUM), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_DisableChn(0, 0), SYS_ERR_OK);

    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AO_QueryChnStat(0, 0, NULL), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnTotalNum, QUEUE_CAP); /* 4 x u32FrmNum */
    REQUIRE_EQ(stStat.u32ChnFreeNum, QUEUE_CAP);
    REQUIRE_EQ(stStat.u32ChnBusyNum, 0);
    REQUIRE_EQ(stStat.u32ValidMask, AUDIO_STATS_QUEUE | AUDIO_STATS_UNDERRUN);

    /* One channel per device until the mixer exists. */
    REQUIRE_EQ(AO_EnableChn(0, 1), SYS_ERR_BUSY);
    REQUIRE_EQ(AO_DisableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 1), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_BUSY);

    /* DisableDev takes the channel down with it. */
    REQUIRE_EQ(AO_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_QueryChnStat(0, 1, &stStat), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AO_DeInit(), SYS_ERR_OK);
}

/* ======================== Playback ======================== */

static void test_play_exact(void) {
    AoChnStateS stStat;
    U32 i;

    ao_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    for (i = 0; i < 5; i++)
        REQUIRE_EQ(send_ramp(i * PERIOD, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnBusyNum, 5);
    REQUIRE_EQ(stStat.u32PeakQueuedFrames, 5);

    /* One frame leaves the queue per period the device plays. */
    REQUIRE(fake_playback_advance(2));
    REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnBusyNum, 3);
    REQUIRE(fake_playback_advance(3));
    REQUIRE_EQ(fake_playback_frames(), PREFILL + 5 * PERIOD);
    for (i = 0; i < 5 * PERIOD; i++)
        REQUIRE_EQ(played_s16(PREFILL + i), (int16_t)i); /* bit exact, in order */
    REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnBusyNum, 0);
    REQUIRE_EQ(stStat.u64UnderrunPeriods, 0);
    ao_down();
}

/* A starving channel plays silence; the device itself never underruns. */
static void test_underrun(void) {
    AoChnStateS stStat;
    U32 i;

    ao_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE(fake_playback_advance(3)); /* nothing sent yet: not an underrun */
    REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u64UnderrunPeriods, 0);

    REQUIRE_EQ(send_ramp(1, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(send_ramp(1 + PERIOD, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE(fake_playback_advance(10));
    REQUIRE_EQ(fake_playback_frames(), PREFILL + 13 * PERIOD);
    for (i = 0; i < 2 * PERIOD; i++)
        REQUIRE_EQ(played_s16(PREFILL + 3 * PERIOD + i), (int16_t)(1 + i));
    for (i = 2 * PERIOD; i < 10 * PERIOD; i++)
        REQUIRE_EQ(played_s16(PREFILL + 3 * PERIOD + i), 0);
    REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u64UnderrunPeriods, 1); /* counted once per interruption */
    REQUIRE_EQ(fake_stats()->u32Recovers, 0);

    /* Playback picks up again as soon as data arrives. */
    REQUIRE_EQ(send_ramp(5000, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE(fake_playback_advance(2));
    for (i = 0; i < PERIOD; i++)
        REQUIRE_EQ(played_s16(PREFILL + 13 * PERIOD + i), (int16_t)(5000 + i));
    REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u64UnderrunPeriods, 2);
    ao_down();
}

static void test_send_full_and_timeout(void) {
    struct timespec stStart, stNow;
    AoChnStateS stStat;
    U32 i;

    ao_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    for (i = 0; i < QUEUE_CAP; i++)
        REQUIRE_EQ(send_ramp(i * PERIOD, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(send_ramp(0, AIO_NO_BLOCK), SYS_ERR_FULL);
    clock_gettime(CLOCK_MONOTONIC, &stStart);
    REQUIRE_EQ(send_ramp(0, 50), SYS_ERR_TIMEOUT);
    clock_gettime(CLOCK_MONOTONIC, &stNow);
    REQUIRE((stNow.tv_sec - stStart.tv_sec) * 1000 + (stNow.tv_nsec - stStart.tv_nsec) / 1000000 >= 49);
    REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnBusyNum, QUEUE_CAP);
    REQUIRE_EQ(stStat.u32ChnFreeNum, 0);
    REQUIRE_EQ(stStat.u32PeakQueuedFrames, QUEUE_CAP);

    /* Everything queued is played, nothing more. */
    REQUIRE(fake_playback_advance(QUEUE_CAP + 2));
    for (i = 0; i < QUEUE_CAP * PERIOD; i++)
        REQUIRE_EQ(played_s16(PREFILL + i), (int16_t)i);
    for (i = QUEUE_CAP * PERIOD; i < (QUEUE_CAP + 2) * PERIOD; i++)
        REQUIRE_EQ(played_s16(PREFILL + i), 0);
    ao_down();
}

typedef struct _Sender {
    pthread_t stThread;
    S32 s32MilliSec;
    S32 s32Ret;
} Sender;

static void *sender_thread(void *pArg) {
    Sender *pstSender = (Sender *)pArg;

    pstSender->s32Ret = send_ramp(30000, pstSender->s32MilliSec);
    return NULL;
}

static void sender_start(Sender *pstSender, S32 s32MilliSec) {
    pstSender->s32MilliSec = s32MilliSec;
    pstSender->s32Ret = 12345;
    REQUIRE(pthread_create(&pstSender->stThread, NULL, sender_thread, pstSender) == 0);
    /* Give it time to block; the outcome does not depend on it. */
    usleep(20000);
}

static void fill_queue(void) {
    U32 i;

    for (i = 0; i < QUEUE_CAP; i++)
        REQUIRE_EQ(send_ramp(i * PERIOD, AIO_NO_BLOCK), SYS_ERR_OK);
}

static void test_send_blocking(void) {
    Sender stSender;

    ao_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);

    /* Woken by room in the queue. */
    fill_queue();
    sender_start(&stSender, AIO_BLOCK_INFINITE);
    REQUIRE(fake_playback_advance(1));
    pthread_join(stSender.stThread, NULL);
    REQUIRE_EQ(stSender.s32Ret, SYS_ERR_OK);

    /* Woken by DisableChn. */
    sender_start(&stSender, AIO_BLOCK_INFINITE);
    REQUIRE_EQ(AO_DisableChn(0, 0), SYS_ERR_OK);
    pthread_join(stSender.stThread, NULL);
    REQUIRE_EQ(stSender.s32Ret, AO_ERR_ABORTED);

    /* Woken by DisableDev, with a timeout that would otherwise run long. */
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    fill_queue();
    sender_start(&stSender, 60000);
    REQUIRE_EQ(AO_DisableDev(0), SYS_ERR_OK);
    pthread_join(stSender.stThread, NULL);
    REQUIRE_EQ(stSender.s32Ret, AO_ERR_ABORTED);
    REQUIRE_EQ(AO_DeInit(), SYS_ERR_OK);
}

static void test_frame_validation(void) {
    int16_t as16[PERIOD * 2] = {0};
    AudioFrameS stGood = make_frame((U8 *)as16, RATE, 1, PERIOD), stBad;
    AoChnStateS stStat;

    ao_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AO_SendFrame(0, 0, &stGood, AIO_NO_BLOCK), SYS_ERR_NOT_INIT); /* channel not enabled */
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AO_SendFrame(0, 0, NULL, AIO_NO_BLOCK), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SendFrame(0, 0, &stGood, -2), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SendFrame(0, AO_MAX_CHN_NUM, &stGood, AIO_NO_BLOCK), SYS_ERR_INVAL);

    stBad = stGood;
    stBad.pu8Data = NULL;
    REQUIRE_EQ(AO_SendFrame(0, 0, &stBad, AIO_NO_BLOCK), SYS_ERR_INVAL);
    stBad = stGood;
    stBad.eBitWidth = AUDIO_BIT_WIDTH_32;
    REQUIRE_EQ(AO_SendFrame(0, 0, &stBad, AIO_NO_BLOCK), SYS_ERR_INVAL);
    stBad = stGood;
    stBad.ePcmFormat = AUDIO_PCM_S24_3LE;
    REQUIRE_EQ(AO_SendFrame(0, 0, &stBad, AIO_NO_BLOCK), SYS_ERR_INVAL);
    stBad = stGood;
    stBad.eSampleRate = AUDIO_SAMPLE_RATE_48000;
    REQUIRE_EQ(AO_SendFrame(0, 0, &stBad, AIO_NO_BLOCK), SYS_ERR_INVAL);
    stBad = make_frame((U8 *)as16, RATE, 2, PERIOD);
    REQUIRE_EQ(AO_SendFrame(0, 0, &stBad, AIO_NO_BLOCK), SYS_ERR_INVAL);
    stBad = stGood;
    stBad.eSoundMode = AUDIO_SOUND_MODE_STEREO;
    REQUIRE_EQ(AO_SendFrame(0, 0, &stBad, AIO_NO_BLOCK), SYS_ERR_INVAL);
    stBad = stGood;
    stBad.u32Len -= 2; /* not one device frame */
    REQUIRE_EQ(AO_SendFrame(0, 0, &stBad, AIO_NO_BLOCK), SYS_ERR_INVAL);
    stBad.u32Len = stGood.u32Len * 2;
    REQUIRE_EQ(AO_SendFrame(0, 0, &stBad, AIO_NO_BLOCK), SYS_ERR_INVAL);
    stBad = stGood;
    stBad.u32Flags = 0x80; /* unknown flag bits must be zero */
    REQUIRE_EQ(AO_SendFrame(0, 0, &stBad, AIO_NO_BLOCK), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnBusyNum, 0);

    /* An explicit format that agrees, and AI-only fields, are fine. */
    stBad = stGood;
    stBad.ePcmFormat = AUDIO_PCM_S16_LE;
    stBad.u32Flags = AUDIO_FRAME_DISCONTINUITY;
    stBad.u32SlotId = 77;
    stBad.u64Epoch = 99;
    REQUIRE_EQ(AO_SendFrame(0, 0, &stBad, AIO_NO_BLOCK), SYS_ERR_OK);
    ao_down();
}

/* 32-bit stereo passes through untouched. */
static void test_format_passthrough(void) {
    AioDevAttrS stAttr = test_dev_attr(48000, 2, 480, FRM_NUM);
    int32_t as32[480 * 2];
    AudioFrameS stFrame;
    U32 i;

    stAttr.eBitWidth = AUDIO_BIT_WIDTH_32;
    fake_reset();
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    for (i = 0; i < 480 * 2; i++)
        as32[i] = (int32_t)(0x12345678U + i * 0x01010101U);
    stFrame = make_frame((U8 *)as32, 48000, 2, 480);
    stFrame.eBitWidth = AUDIO_BIT_WIDTH_32;
    stFrame.u32Len = sizeof(as32);
    REQUIRE_EQ(AO_SendFrame(0, 0, &stFrame, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE(fake_playback_advance(1));
    REQUIRE(memcmp(fake_playback_data() + (size_t)FRM_NUM * 480 * 8, as32, sizeof(as32)) == 0);
    ao_down();
}

/* ======================== Resampling ======================== */

#define TONE_HZ 1000.0
#define TONE_AMP 0.5

static S32 send_tone(U32 u32Rate, U32 u32Points, U32 u32Index, S32 s32MilliSec) {
    int16_t as16[MAX_AIO_POINT_NUM];
    AudioFrameS stFrame = make_frame((U8 *)as16, u32Rate, 1, u32Points);
    U32 i;

    for (i = 0; i < u32Points; i++)
        as16[i] = (int16_t)lrint(32768.0 * TONE_AMP *
            sin(2.0 * TEST_PI * TONE_HZ * ((double)u32Index * u32Points + i) / u32Rate));

    return AO_SendFrame(0, 0, &stFrame, s32MilliSec);
}

/* The tone must come out of the 48 kHz device clean between the two samples. */
static void require_clean_tone(U64 u64From, U64 u64To) {
    U32 u32Count = (U32)(u64To - u64From), i;
    float *pf = (float *)malloc((size_t)u32Count * sizeof(float));
    double dAmp, dRes;

    REQUIRE(pf != NULL);
    for (i = 0; i < u32Count; i++)
        pf[i] = (float)played_s16(u64From + i) / 32768.0f;
    test_fit_tone(pf, u32Count, 1, TONE_HZ / 48000.0, &dAmp, &dRes);
    if (fabs(test_db(dAmp / TONE_AMP)) >= 0.5 || test_db(dRes / (TONE_AMP / sqrt(2.0))) >= -60.0) {
        fprintf(stderr, "tone gain %.3f dB, residual %.1f dB\n", test_db(dAmp / TONE_AMP),
            test_db(dRes / (TONE_AMP / sqrt(2.0))));
        REQUIRE(0);
    }
    free(pf);
}

static void require_silence(U64 u64From, U64 u64To) {
    U64 i;

    for (i = u64From; i < u64To; i++)
        REQUIRE_EQ(played_s16(i), 0);
}

static void test_resample(void) {
    const U32 u32Prefill = FRM_NUM * 960;
    AioDevAttrS stAttr;
    AoChnStateS stStat;
    int16_t as16[960] = {0};
    AudioFrameS stFrame;
    U32 i;

    /* 16 kHz frames on a 48 kHz device, integer ratio: 320 points per frame. */
    ao_up(48000, 1, 960);
    REQUIRE_EQ(AO_EnableReSmp(0, 0, (AudioSampleRate)44000), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_8000), SYS_ERR_INVAL); /* before EnableChn only */
    REQUIRE_EQ(AO_DisableReSmp(0, 0), SYS_ERR_INVAL);
    stFrame = make_frame((U8 *)as16, 48000, 1, 960); /* a frame at the device rate no longer fits */
    REQUIRE_EQ(AO_SendFrame(0, 0, &stFrame, AIO_NO_BLOCK), SYS_ERR_INVAL);
    stFrame = make_frame((U8 *)as16, 16000, 1, 321);
    REQUIRE_EQ(AO_SendFrame(0, 0, &stFrame, AIO_NO_BLOCK), SYS_ERR_INVAL);

    for (i = 0; i < 8; i++)
        REQUIRE_EQ(send_tone(16000, 320, i, AIO_NO_BLOCK), SYS_ERR_OK);
    for (i = 8; i < 60; i++) {
        REQUIRE(fake_playback_advance(1));
        REQUIRE_EQ(send_tone(16000, 320, i, AIO_NO_BLOCK), SYS_ERR_OK);
    }
    REQUIRE(fake_playback_advance(12));
    /* 60 frames of 320 points become exactly 60 x 960 samples, then silence. */
    require_clean_tone(u32Prefill + 960, u32Prefill + 59 * 960);
    REQUIRE(played_s16(u32Prefill + 60 * 960 - 20) != 0 || played_s16(u32Prefill + 60 * 960 - 21) != 0);
    require_silence(u32Prefill + 60 * 960, fake_playback_frames());
    REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u64UnderrunPeriods, 1);
    ao_down();

    /* A sender that delivers one frame per period, just in time, is not cut up:
     * the first period waits for enough input, then the tone runs through. */
    ao_up(48000, 1, 960);
    REQUIRE_EQ(AO_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    for (i = 0; i < 50; i++) {
        REQUIRE_EQ(send_tone(16000, 320, i, AIO_NO_BLOCK), SYS_ERR_OK);
        REQUIRE(fake_playback_advance(1));
    }
    REQUIRE(fake_playback_advance(3));
    require_silence(u32Prefill, u32Prefill + 960);
    require_clean_tone(u32Prefill + 2 * 960, u32Prefill + 50 * 960);
    require_silence(u32Prefill + 51 * 960, fake_playback_frames());
    ao_down();

    /* Non-integer ratio: 1024 points at 48 kHz are 940.8 at 44.1 kHz, so the
     * channel takes 941-point frames and the remainder rolls over. */
    fake_reset();
    stAttr = test_dev_attr(48000, 1, 1024, FRM_NUM);
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_44100), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    for (i = 0; i < 300; i++) {
        while (send_tone(44100, 941, i, AIO_NO_BLOCK) == SYS_ERR_FULL)
            REQUIRE(fake_playback_advance(1));
    }
    REQUIRE(fake_playback_advance(QUEUE_CAP + 4));
    /* 300 x 941 input samples are 307265.3 at 48 kHz. */
    require_clean_tone(FRM_NUM * 1024 + 1024, FRM_NUM * 1024 + 307000);
    require_silence(FRM_NUM * 1024 + 307266, fake_playback_frames());
    ao_down();

    /* The same rate as the device is a plain copy. */
    ao_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AO_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(send_ramp(100, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE(fake_playback_advance(1));
    for (i = 0; i < PERIOD; i++)
        REQUIRE_EQ(played_s16(PREFILL + i), (int16_t)(100 + i));
    REQUIRE_EQ(AO_DisableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AO_DisableReSmp(0, 0), SYS_ERR_OK);
    ao_down();

    /* 8-bit PCM cannot be resampled. */
    stAttr = test_dev_attr(RATE, 1, PERIOD, FRM_NUM);
    stAttr.eBitWidth = AUDIO_BIT_WIDTH_8;
    fake_reset();
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_8000), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_DisableReSmp(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    ao_down();
}

/* ======================== Device faults ======================== */

static void test_xrun_recovery(void) {
    U32 i;

    ao_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    for (i = 0; i < 3; i++)
        REQUIRE_EQ(send_ramp(i * PERIOD, AIO_NO_BLOCK), SYS_ERR_OK);

    /* Device underrun: playback restarts with the start silence and the
     * queued audio is still played in full. */
    fake_inject(AIO_AL_DIR_PLAYBACK, AIO_AL_ERR_XRUN);
    REQUIRE(fake_wait_idle(AIO_AL_DIR_PLAYBACK));
    REQUIRE_EQ(fake_stats()->u32Recovers, 1);
    REQUIRE_EQ(fake_playback_frames(), 2 * PREFILL);
    REQUIRE(fake_playback_advance(4));
    for (i = 0; i < PREFILL; i++)
        REQUIRE_EQ(played_s16(PREFILL + i), 0);
    for (i = 0; i < 3 * PERIOD; i++)
        REQUIRE_EQ(played_s16(2 * PREFILL + i), (int16_t)i);

    fake_inject(AIO_AL_DIR_PLAYBACK, AIO_AL_ERR_SUSPEND);
    REQUIRE(fake_wait_idle(AIO_AL_DIR_PLAYBACK));
    REQUIRE_EQ(fake_stats()->u32Recovers, 2);
    REQUIRE_EQ(send_ramp(700, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE(fake_playback_advance(1));
    REQUIRE_EQ(played_s16(fake_playback_frames() - PERIOD), 700);
    ao_down();
}

static void test_device_lost(void) {
    Sender stSender;

    ao_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    fill_queue();

    /* The device goes away under a blocked sender. */
    sender_start(&stSender, AIO_BLOCK_INFINITE);
    fake_inject(AIO_AL_DIR_PLAYBACK, AIO_AL_ERR_NODEV);
    pthread_join(stSender.stThread, NULL);
    REQUIRE_EQ(stSender.s32Ret, AO_ERR_ABORTED);
    REQUIRE_EQ(send_ramp(0, AIO_NO_BLOCK), AO_ERR_ABORTED);
    REQUIRE_EQ(AO_EnableChn(0, 1), SYS_ERR_NOT_FOUND);

    /* Disable + enable brings it back. */
    REQUIRE_EQ(AO_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(send_ramp(40, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE(fake_playback_advance(1));
    REQUIRE_EQ(played_s16(PREFILL), 40);

    /* An underrun that cannot be recovered ends the same way. */
    fill_queue();
    sender_start(&stSender, AIO_BLOCK_INFINITE);
    fake_set_recover_error(AIO_AL_ERR_NODEV);
    fake_inject(AIO_AL_DIR_PLAYBACK, AIO_AL_ERR_XRUN);
    pthread_join(stSender.stThread, NULL);
    REQUIRE_EQ(stSender.s32Ret, AO_ERR_ABORTED);
    ao_down();
}

static void test_hardware_limits(void) {
    AioDevAttrS stAttr = test_dev_attr(RATE, 1, PERIOD, FRM_NUM);
    U32 i;

    /* The card only does 48 kHz stereo: other settings fail, they are not faked. */
    fake_reset();
    fake_set_support(48000, 2, AUDIO_PCM_S16_LE);
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_DeInit(), SYS_ERR_OK);

    /* A larger hardware buffer is not filled beyond u32FrmNum frames: the
     * playback delay stays what the attribute asked for. */
    fake_reset();
    fake_set_geometry(1024, 8192);
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE(fake_wait_idle(AIO_AL_DIR_PLAYBACK));
    REQUIRE_EQ(fake_playback_frames(), PREFILL);
    REQUIRE_EQ(send_ramp(0, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE(fake_playback_advance(3));
    REQUIRE_EQ(fake_playback_frames(), PREFILL + 3 * PERIOD);
    for (i = 0; i < PERIOD; i++)
        REQUIRE_EQ(played_s16(PREFILL + i), (int16_t)i);
    ao_down();
}

/* ======================== Not implemented yet ======================== */

static void test_stubs(void) {
    AoPlaybackPositionS stPos;
    AudioControlCapsS stCaps;
    AudioAnalogGainS stGain;
    AudioChnVolumeS stChnVol;
    AudioChnMuteS stChnMute;
    AioDeviceInfoS stInfo;
    AudioTrackMode eTrack;
    AoDecChnAttrS stDecAttr;
    AudioStreamS stStream;
    AoDecoderS stDecoder;
    AudioEventS stEvent;
    AoVqeConfigS stVqe;
    AoChnParamS stParam;
    AudioFadeS stFade;
    AoDelayS stDelay;
    U32 u32Count = 0;
    S32 s32Val = 0;
    BOOL bMute;

    memset(&stParam, 0, sizeof(stParam));
    memset(&stDecAttr, 0, sizeof(stDecAttr));
    memset(&stDecoder, 0, sizeof(stDecoder));
    memset(&stStream, 0, sizeof(stStream));
    memset(&stGain, 0, sizeof(stGain));
    memset(&stVqe, 0, sizeof(stVqe));
    ao_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);

    REQUIRE_EQ(AO_EnumDevices(&stInfo, 1, &u32Count), SYS_ERR_INVAL);
    REQUIRE(AO_GetEventFd(0) < 0);
    REQUIRE_EQ(AO_GetEvent(0, &stEvent, AIO_NO_BLOCK), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_QueryControlCaps(0, &stCaps, 1, &u32Count), SYS_ERR_INVAL);
    REQUIRE(AO_GetFd(0, 0) < 0);
    REQUIRE_EQ(AO_SetChnParam(0, 1, &stParam), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_GetChnParam(0, 0, &stParam), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_PauseChn(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_ResumeChn(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_ClearChnBuf(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_DrainChn(0, 0, AIO_NO_BLOCK), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_CancelDrainChn(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_QueryDelay(0, 0, &stDelay), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_QueryPlaybackPosition(0, 0, &stPos), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_RegisterDecoder(&s32Val, &stDecoder), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_UnRegisterDecoder(0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_EnableDecChn(0, 0, &stDecAttr), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_DisableDecChn(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SendStream(0, 0, &stStream, MPP_FALSE), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SendEndOfStream(0, 0, MPP_TRUE), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SendStreamEx(0, 0, &stStream, AIO_NO_BLOCK, &u32Count), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SetChnVolume(0, 0, 0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_GetChnVolume(0, 0, &stChnVol), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SetChnMute(0, 0, MPP_TRUE, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_GetChnMute(0, 0, &stChnMute), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SetVolume(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_GetVolume(0, &s32Val), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SetAnalogGain(0, &stGain), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_GetAnalogGain(0, &stGain), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SetMute(0, MPP_TRUE, NULL), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_GetMute(0, &bMute, &stFade), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SetTrackMode(0, AUDIO_TRACK_NORMAL), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_GetTrackMode(0, &eTrack), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_SetVqeAttr(0, 1, &stVqe), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_GetVqeAttr(0, 0, &stVqe), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_EnableVqe(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AO_DisableVqe(0, 0), SYS_ERR_INVAL);

    /* None of them disturbed the channel. */
    REQUIRE_EQ(send_ramp(9, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE(fake_playback_advance(1));
    REQUIRE_EQ(played_s16(PREFILL), 9);
    ao_down();
}

int main(void) {
    RUN(test_init_deinit);
    RUN(test_not_initialized);
    RUN(test_dev_and_spk_attr);
    RUN(test_prefill);
    RUN(test_chn_enable);
    RUN(test_play_exact);
    RUN(test_underrun);
    RUN(test_send_full_and_timeout);
    RUN(test_send_blocking);
    RUN(test_frame_validation);
    RUN(test_format_passthrough);
    RUN(test_resample);
    RUN(test_xrun_recovery);
    RUN(test_device_lost);
    RUN(test_hardware_limits);
    RUN(test_stubs);

    return 0;
}
