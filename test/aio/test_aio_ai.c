/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * AI (audio input) interface tests against the scripted pcm backend:
 * life cycle, frame capture, ownership of lent frames, resampling and
 * recovery from device faults. No audio hardware involved.
 */

#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "ai_api.h"
#include "aio_fake_backend.h"
#include "aio_internal.h"
#include "aio_test.h"

#define RATE 16000U
#define PERIOD 320U /* 20 ms */
#define FRM_NUM 4U
#define WAIT_MS 2000

static void ai_up(U32 u32Rate, U32 u32Channels, U32 u32Period) {
    AioDevAttrS stAttr = test_dev_attr(u32Rate, u32Channels, u32Period, FRM_NUM);

    fake_reset();
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
}

static void ai_down(void) {
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);
    REQUIRE_EQ(fake_stats()->u32Opens, fake_stats()->u32Closes);
}

/* The frame holds the default ramp starting at capture frame u64First. */
static void require_ramp(const AudioFrameS *pstFrame, U64 u64First) {
    const U32 u32Ch = (U32)pstFrame->s32Channels;
    const U32 u32Points = pstFrame->u32Len / (2 * u32Ch);
    U32 i, c;

    for (i = 0; i < u32Points; i++) {
        for (c = 0; c < u32Ch; c++) {
            int16_t s16;

            memcpy(&s16, pstFrame->pu8Data + ((size_t)i * u32Ch + c) * 2, 2);
            REQUIRE_EQ(s16, (int16_t)(u64First + i + 1000ULL * c));
        }
    }
}

static U64 elapsed_ms(const struct timespec *pstStart) {
    struct timespec stNow;

    clock_gettime(CLOCK_MONOTONIC, &stNow);
    return (U64)(stNow.tv_sec - pstStart->tv_sec) * 1000ULL + (U64)(stNow.tv_nsec / 1000000L) -
        (U64)(pstStart->tv_nsec / 1000000L);
}

/* ======================== Life cycle ======================== */

static void test_init_deinit(void) {
    fake_reset();
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK); /* idempotent */
    REQUIRE_EQ(fake_stats()->u32Loads, 1);
    REQUIRE_EQ(fake_stats()->u32Inits, 1);
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);
    REQUIRE_EQ(fake_stats()->u32Deinits, 1);
    REQUIRE_EQ(fake_stats()->u32Unloads, 1);

    /* No backend plugin: Init says so and can be retried. */
    fake_reset();
    fake_set_load_error(SYS_ERR_NOT_FOUND);
    REQUIRE_EQ(AI_Init(), SYS_ERR_NOT_FOUND);
    fake_set_load_error(0);
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);
}

static void test_not_initialized(void) {
    AioDevAttrS stAttr = test_dev_attr(RATE, 1, PERIOD, FRM_NUM);
    AiChnStateS stStat;
    AudioFrameS stFrame;
    MicAttrS stMic;

    memset(&stFrame, 0, sizeof(stFrame));
    memset(&stMic, 0, sizeof(stMic));
    fake_reset();
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_GetDevAttr(0, &stAttr), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_SetMicAttr(0, &stMic), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_GetMicAttr(0, &stMic), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_DisableChn(0, 0), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_QueryChnStat(0, 0, &stStat), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_DisableReSmp(0, 0), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(fake_stats()->u32Loads, 0);
}

static void test_set_dev_attr(void) {
    AioDevAttrS stAttr = test_dev_attr(RATE, 1, PERIOD, FRM_NUM);
    AioDevAttrS stOther = test_dev_attr(48000, 2, 960, 2);
    AioDevAttrS stGot, stBad;

    fake_reset();
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_GetDevAttr(0, &stGot), SYS_ERR_INVAL); /* nothing set yet */
    REQUIRE_EQ(AI_SetDevAttr(-1, &stAttr), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_SetDevAttr(AI_MAX_DEV_NUM, &stAttr), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_SetDevAttr(0, NULL), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_GetDevAttr(0, NULL), SYS_ERR_INVAL);

    /* Settable any number of times before EnableDev; the last one counts. */
    REQUIRE_EQ(AI_SetDevAttr(0, &stOther), SYS_ERR_OK);
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AI_GetDevAttr(0, &stGot), SYS_ERR_OK);
    REQUIRE(memcmp(&stGot, &stAttr, sizeof(stAttr)) == 0);

    /* Each rejected value leaves the stored attribute alone. */
    stBad = stAttr;
    stBad.eSampleRate = (AudioSampleRate)44000;
    REQUIRE_EQ(AI_SetDevAttr(0, &stBad), SYS_ERR_INVAL);
    stBad = stAttr;
    stBad.u32FrmNum = 1;
    REQUIRE_EQ(AI_SetDevAttr(0, &stBad), SYS_ERR_INVAL);
    stBad.u32FrmNum = 11;
    REQUIRE_EQ(AI_SetDevAttr(0, &stBad), SYS_ERR_INVAL);
    stBad = stAttr;
    stBad.u32PtNumPerFrm = 0;
    REQUIRE_EQ(AI_SetDevAttr(0, &stBad), SYS_ERR_INVAL);
    stBad.u32PtNumPerFrm = MAX_AIO_POINT_NUM + 1;
    REQUIRE_EQ(AI_SetDevAttr(0, &stBad), SYS_ERR_INVAL);
    stBad = stAttr;
    stBad.u32ChnCnt = 0;
    REQUIRE_EQ(AI_SetDevAttr(0, &stBad), SYS_ERR_INVAL);
    stBad.u32ChnCnt = AUDIO_MAX_PCM_CHANNELS + 1;
    stBad.eSoundMode = AUDIO_SOUND_MODE_RAW;
    REQUIRE_EQ(AI_SetDevAttr(0, &stBad), SYS_ERR_INVAL);
    stBad = stAttr;
    stBad.u32ChnCnt = 2; /* sound mode still says mono */
    REQUIRE_EQ(AI_SetDevAttr(0, &stBad), SYS_ERR_INVAL);
    stBad = stAttr;
    stBad.ePcmFormat = AUDIO_PCM_S32_LE; /* bit width still says 16 */
    REQUIRE_EQ(AI_SetDevAttr(0, &stBad), SYS_ERR_INVAL);
    stBad = test_dev_attr(48000, 8, 1024, FRM_NUM); /* 8 ch x 1024 x 4 bytes > MAX_AUDIO_FRAME_LEN */
    stBad.eBitWidth = AUDIO_BIT_WIDTH_32;
    REQUIRE_EQ(AI_SetDevAttr(0, &stBad), SYS_ERR_INVAL);
    stBad.u32PtNumPerFrm = 512; /* exactly MAX_AUDIO_FRAME_LEN */
    REQUIRE_EQ(AI_SetDevAttr(0, &stBad), SYS_ERR_OK);
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AI_GetDevAttr(0, &stGot), SYS_ERR_OK);
    REQUIRE(memcmp(&stGot, &stAttr, sizeof(stAttr)) == 0);

    /* Read-only once the device is enabled. */
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_SetDevAttr(0, &stOther), SYS_ERR_BUSY);
    REQUIRE_EQ(AI_GetDevAttr(0, &stGot), SYS_ERR_OK);
    REQUIRE(memcmp(&stGot, &stAttr, sizeof(stAttr)) == 0);
    ai_down();
}

static void test_enable_dev(void) {
    AioDevAttrS stAttr = test_dev_attr(RATE, 1, PERIOD, FRM_NUM);
    const AioAlPcmCfg *pstCfg;
    U32 i;

    fake_reset();
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_INVAL); /* no attribute yet */
    REQUIRE_EQ(fake_stats()->u32Opens, 0);
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK); /* nothing to do */
    REQUIRE_EQ(AI_EnableDev(AI_MAX_DEV_NUM), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);

    /* What the backend is asked for. */
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
    pstCfg = &fake_stats()->astLastCfg[AIO_AL_DIR_CAPTURE];
    REQUIRE_EQ(pstCfg->eDir, AIO_AL_DIR_CAPTURE);
    REQUIRE_EQ(pstCfg->s32Card, 0);
    REQUIRE_EQ(pstCfg->u32Rate, RATE);
    REQUIRE_EQ(pstCfg->u32Channels, 1);
    REQUIRE_EQ(pstCfg->ePcmFormat, AUDIO_PCM_S16_LE);
    REQUIRE_EQ(pstCfg->u32PeriodFrames, PERIOD);
    REQUIRE_EQ(pstCfg->u32BufferFrames, FRM_NUM * PERIOD);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK); /* already enabled */
    REQUIRE_EQ(fake_stats()->u32Opens, 1);
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_BUSY); /* devices must be disabled first */
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(fake_stats()->u32Closes, 1);

    /* Open failures are told apart and leave the device ready for a retry. */
    fake_set_open_error(AIO_AL_ERR_NOENT);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_NOT_FOUND);
    fake_set_open_error(AIO_AL_ERR_BUSY);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_BUSY);
    fake_set_open_error(AIO_AL_ERR_UNSUP);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_INVAL);
    REQUIRE_EQ(fake_stats()->u32Opens, 1);

    for (i = 0; i < 100; i++) {
        REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
        REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
        REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
    }
    REQUIRE_EQ(fake_stats()->u32Opens, 101);
    REQUIRE_EQ(fake_stats()->u32Closes, 101);

    /* Two logical devices cannot share one exclusive capture PCM. */
    REQUIRE_EQ(AI_SetDevAttr(1, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableDev(1), SYS_ERR_BUSY);
    ai_down();
}

static void test_mic_attr(void) {
    AioDevAttrS stAttr = test_dev_attr(RATE, 2, PERIOD, FRM_NUM);
    MicAttrS stMic, stGot;

    fake_reset();
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_GetMicAttr(0, &stGot), SYS_ERR_OK); /* defaults: card 0, one MIC */
    REQUIRE_EQ(stGot.stMicAttr.s32Dev, 0);
    REQUIRE_EQ(stGot.stMicAttr.s32Chns, 1);
    REQUIRE_EQ(AI_SetMicAttr(0, NULL), SYS_ERR_INVAL);

    memset(&stMic, 0, sizeof(stMic));
    stMic.stMicAttr.s32Dev = -1;
    stMic.stMicAttr.s32Chns = 2;
    REQUIRE_EQ(AI_SetMicAttr(0, &stMic), SYS_ERR_INVAL);
    stMic.stMicAttr.s32Dev = 2;
    stMic.stMicAttr.s32Chns = 0;
    REQUIRE_EQ(AI_SetMicAttr(0, &stMic), SYS_ERR_INVAL);
    stMic.stMicAttr.s32Chns = 9;
    REQUIRE_EQ(AI_SetMicAttr(0, &stMic), SYS_ERR_INVAL);

    /* The MIC count has to agree with u32ChnCnt. */
    stMic.stMicAttr.s32Chns = 1;
    REQUIRE_EQ(AI_SetMicAttr(0, &stMic), SYS_ERR_OK);
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_INVAL);
    stMic.stMicAttr.s32Chns = 2;
    REQUIRE_EQ(AI_SetMicAttr(0, &stMic), SYS_ERR_OK);
    REQUIRE_EQ(AI_GetMicAttr(0, &stGot), SYS_ERR_OK);
    REQUIRE(memcmp(&stGot, &stMic, sizeof(stMic)) == 0);

    /* s32Dev selects the sound card. */
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(fake_stats()->astLastCfg[AIO_AL_DIR_CAPTURE].s32Card, 2);
    REQUIRE_EQ(fake_stats()->astLastCfg[AIO_AL_DIR_CAPTURE].u32Channels, 2);
    REQUIRE_EQ(AI_SetMicAttr(0, &stMic), SYS_ERR_INVAL); /* too late */
    ai_down();
}

static void test_chn_enable(void) {
    AiChnStateS stStat;

    fake_reset();
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_NOT_INIT); /* device not enabled */
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);

    ai_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AI_EnableChn(0, -1), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_EnableChn(0, AI_MAX_CHN_NUM), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_EnableChn(2, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_QueryChnStat(0, 0, &stStat), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_DisableChn(0, 0), SYS_ERR_OK); /* nothing to do */

    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 7), SYS_ERR_OK);
    REQUIRE_EQ(AI_QueryChnStat(0, 0, NULL), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnTotalNum, AIO_AI_QUEUE_FRAMES + AIO_AI_USR_FRM_DEPTH);
    REQUIRE_EQ(stStat.u32ChnFreeNum, stStat.u32ChnTotalNum);
    REQUIRE_EQ(stStat.u32ValidMask, AUDIO_STATS_QUEUE | AUDIO_STATS_DROP);

    /* DisableDev takes the enabled channels down with it. */
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_QueryChnStat(0, 0, &stStat), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_QueryChnStat(0, 7, &stStat), SYS_ERR_NOT_INIT);
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);
}

/* ======================== Frame capture ======================== */

static void test_get_frame(void) {
    AudioFrameS stFrame;
    AecFrameS stAec;
    struct timespec stStart;
    U32 i;

    ai_up(RATE, 2, PERIOD);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AI_GetFrame(0, 0, NULL, NULL, AIO_NO_BLOCK), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, -2), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_GetFrame(0, 1, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_NOT_INIT);

    /* Nothing captured yet. */
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), AI_ERR_BUF_EMPTY);
    clock_gettime(CLOCK_MONOTONIC, &stStart);
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, 50), SYS_ERR_TIMEOUT);
    REQUIRE(elapsed_ms(&stStart) >= 49);

    for (i = 0; i < 100; i++) {
        REQUIRE(fake_capture_advance(1));
        memset(&stAec, 0xFF, sizeof(stAec));
        REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, &stAec, AIO_NO_BLOCK), SYS_ERR_OK);
        REQUIRE_EQ(stFrame.eBitWidth, AUDIO_BIT_WIDTH_16);
        REQUIRE_EQ(stFrame.ePcmFormat, AUDIO_PCM_S16_LE);
        REQUIRE_EQ(stFrame.eSoundMode, AUDIO_SOUND_MODE_STEREO);
        REQUIRE_EQ(stFrame.eSampleRate, AUDIO_SAMPLE_RATE_16000);
        REQUIRE_EQ(stFrame.s32Channels, 2);
        REQUIRE_EQ(stFrame.u32Len, PERIOD * 2 * 2);
        REQUIRE_EQ(stFrame.u32Flags, 0);
        REQUIRE_EQ(stFrame.u32Seq, i);
        /* PTS of the first sample, continuous from frame to frame. */
        REQUIRE_EQ(stFrame.u64PTS, fake_capture_pts_ms((U64)i * PERIOD, RATE));
        require_ramp(&stFrame, (U64)i * PERIOD);
        REQUIRE_EQ(stAec.bValid, MPP_FALSE); /* no reference path yet */
        REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, &stAec), SYS_ERR_OK);
    }
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), AI_ERR_BUF_EMPTY);
    ai_down();
}

typedef struct _Reader {
    pthread_t stThread;
    atomic_int iTid;
    AudioFrameS stFrame;
    S32 s32MilliSec;
    S32 s32Ret;
} Reader;

static void *reader_thread(void *pArg) {
    Reader *pstReader = (Reader *)pArg;

    atomic_store(&pstReader->iTid, test_gettid());
    pstReader->s32Ret = AI_GetFrame(0, 0, &pstReader->stFrame, NULL, pstReader->s32MilliSec);
    return NULL;
}

static void reader_start(Reader *pstReader, S32 s32MilliSec) {
    pstReader->s32MilliSec = s32MilliSec;
    pstReader->s32Ret = 12345;
    atomic_store(&pstReader->iTid, 0);
    REQUIRE(pthread_create(&pstReader->stThread, NULL, reader_thread, pstReader) == 0);
    /* Go on only once it sleeps inside AI_GetFrame. */
    REQUIRE(test_wait_blocked(&pstReader->iTid));
}

static void test_get_frame_blocking(void) {
    Reader stReader;

    ai_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);

    /* Woken by data. */
    reader_start(&stReader, AIO_BLOCK_INFINITE);
    REQUIRE(fake_capture_advance(1));
    pthread_join(stReader.stThread, NULL);
    REQUIRE_EQ(stReader.s32Ret, SYS_ERR_OK);
    require_ramp(&stReader.stFrame, 0);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stReader.stFrame, NULL), SYS_ERR_OK);

    /* Woken by DisableChn. */
    reader_start(&stReader, AIO_BLOCK_INFINITE);
    REQUIRE_EQ(AI_DisableChn(0, 0), SYS_ERR_OK);
    pthread_join(stReader.stThread, NULL);
    REQUIRE_EQ(stReader.s32Ret, AI_ERR_ABORTED);

    /* Woken by DisableDev, with a timeout that would otherwise run long. */
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    reader_start(&stReader, 60000);
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
    pthread_join(stReader.stThread, NULL);
    REQUIRE_EQ(stReader.s32Ret, AI_ERR_ABORTED);
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);
}

/* A full queue drops its oldest pending frame; the gap shows in u32Seq. */
static void test_drop_oldest(void) {
    AudioFrameS stFrame;
    AiChnStateS stStat;
    U32 i;

    ai_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE(fake_capture_advance(6));

    REQUIRE_EQ(AI_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32QueuedFrames, 4);
    REQUIRE_EQ(stStat.u32PeakQueuedFrames, 4);
    REQUIRE_EQ(stStat.u64DroppedFrames, 2);
    REQUIRE_EQ(stStat.u32ChnBusyNum, 0);
    REQUIRE_EQ(stStat.u32ChnFreeNum, stStat.u32ChnTotalNum - 4);
    for (i = 2; i < 6; i++) {
        REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
        REQUIRE_EQ(stFrame.u32Seq, i);
        REQUIRE_EQ(stFrame.u64PTS, fake_capture_pts_ms((U64)i * PERIOD, RATE));
        require_ramp(&stFrame, (U64)i * PERIOD);
        REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
    }
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), AI_ERR_BUF_EMPTY);
    REQUIRE_EQ(AI_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnFreeNum, stStat.u32ChnTotalNum);
    ai_down();
}

/* A slow reader only loses its own frames. */
static void test_multi_chn(void) {
    AudioFrameS stA, stB;
    AiChnStateS stStat;
    U32 i;

    ai_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 1), SYS_ERR_OK);
    for (i = 0; i < 10; i++) {
        REQUIRE(fake_capture_advance(1));
        REQUIRE_EQ(AI_GetFrame(0, 0, &stA, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
        REQUIRE_EQ(stA.u32Seq, i);
        REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stA, NULL), SYS_ERR_OK);
    }
    REQUIRE_EQ(AI_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u64DroppedFrames, 0);
    REQUIRE_EQ(AI_QueryChnStat(0, 1, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u64DroppedFrames, 6);

    /* Both channels see the same capture. */
    REQUIRE(fake_capture_advance(1));
    REQUIRE_EQ(AI_GetFrame(0, 0, &stA, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    for (i = 0; i < 4; i++) {
        REQUIRE_EQ(AI_GetFrame(0, 1, &stB, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
        if (i < 3)
            REQUIRE_EQ(AI_ReleaseFrame(0, 1, &stB, NULL), SYS_ERR_OK);
    }
    REQUIRE_EQ(stA.u32Seq, stB.u32Seq);
    REQUIRE_EQ(stA.u64PTS, stB.u64PTS);
    REQUIRE(stA.pu8Data != stB.pu8Data);
    REQUIRE(memcmp(stA.pu8Data, stB.pu8Data, stA.u32Len) == 0);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stA, NULL), SYS_ERR_OK);
    REQUIRE_EQ(AI_ReleaseFrame(0, 1, &stB, NULL), SYS_ERR_OK);
    ai_down();
}

/* ======================== Ownership of lent frames ======================== */

static void test_ownership(void) {
    AudioFrameS astHeld[AIO_AI_USR_FRM_DEPTH], stFrame, stBad;
    AiChnStateS stStat;
    U8 au8Copy[PERIOD * 2];
    U32 i;

    ai_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 1), SYS_ERR_OK);

    /* A frame on loan is never overwritten, however much is dropped meanwhile. */
    REQUIRE(fake_capture_advance(1));
    REQUIRE_EQ(AI_GetFrame(0, 0, &astHeld[0], NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    memcpy(au8Copy, astHeld[0].pu8Data, sizeof(au8Copy));
    REQUIRE(fake_capture_advance(100));
    REQUIRE(memcmp(au8Copy, astHeld[0].pu8Data, sizeof(au8Copy)) == 0);
    require_ramp(&astHeld[0], 0);

    /* At most u32UsrFrmDepth frames can be out at once. */
    for (i = 1; i < AIO_AI_USR_FRM_DEPTH; i++)
        REQUIRE_EQ(AI_GetFrame(0, 0, &astHeld[i], NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE(fake_capture_advance(4));
    REQUIRE_EQ(AI_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnBusyNum, AIO_AI_USR_FRM_DEPTH);
    REQUIRE_EQ(stStat.u32QueuedFrames, AIO_AI_QUEUE_FRAMES);
    REQUIRE_EQ(stStat.u32ChnFreeNum, 0);
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), AI_ERR_BUF_EMPTY);
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, 20), SYS_ERR_TIMEOUT);
    /* Pool exhausted: capture carries on and only pending frames are replaced. */
    REQUIRE(fake_capture_advance(3));
    require_ramp(&astHeld[0], 0);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &astHeld[1], NULL), SYS_ERR_OK);
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);

    /* Releasing twice, to the wrong channel, or with an edited descriptor fails. */
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &astHeld[1], NULL), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_ReleaseFrame(0, 1, &astHeld[0], NULL), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, NULL, NULL), SYS_ERR_INVAL);
    stBad = astHeld[0];
    stBad.u32SlotId = 1000;
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stBad, NULL), SYS_ERR_INVAL);
    stBad = astHeld[0];
    stBad.u32SlotId = astHeld[2].u32SlotId;
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stBad, NULL), SYS_ERR_INVAL);
    stBad = astHeld[0];
    stBad.u32Seq++;
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stBad, NULL), SYS_ERR_INVAL);
    stBad = astHeld[0];
    stBad.pu8Data++;
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stBad, NULL), SYS_ERR_INVAL);
    stBad = astHeld[0];
    stBad.u64Epoch++;
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stBad, NULL), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnBusyNum, AIO_AI_USR_FRM_DEPTH - 1); /* nothing above changed the pool */
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &astHeld[2], NULL), SYS_ERR_OK);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &astHeld[3], NULL), SYS_ERR_OK);

    /* DisableChn with a frame still out: the frame stays valid until it comes back. */
    REQUIRE_EQ(AI_DisableChn(0, 0), SYS_ERR_OK);
    require_ramp(&astHeld[0], 0);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE(fake_capture_advance(2));
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE(stFrame.u64Epoch != astHeld[0].u64Epoch);
    REQUIRE_EQ(stFrame.u32Seq, 0); /* a new channel generation counts from zero */
    require_ramp(&astHeld[0], 0);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &astHeld[0], NULL), SYS_ERR_OK); /* the old generation */
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &astHeld[0], NULL), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnBusyNum, 1); /* the new pool only knows its own frame */
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);

    /* Frames still out at DeInit are given up; a late release is refused. */
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    ai_down();
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_NOT_INIT);

    /* Many loans: nothing leaks from the pool. */
    ai_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    for (i = 0; i < 20000; i++) {
        fake_capture_tick(1);
        REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, WAIT_MS), SYS_ERR_OK);
        REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
    }
    REQUIRE(fake_wait_idle(AIO_AL_DIR_CAPTURE));
    REQUIRE_EQ(AI_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnFreeNum, stStat.u32ChnTotalNum);
    REQUIRE_EQ(stStat.u64DroppedFrames, 0);
    ai_down();
}

/* ======================== Resampling ======================== */

#define TONE_HZ 1000.0
#define TONE_AMP 0.5

static float tone_48k(U64 u64Frame, U32 u32Ch) {
    return (float)(TONE_AMP * sin(2.0 * TEST_PI * TONE_HZ * (double)u64Frame / 48000.0));
}

/* Capture u32Frames frames of a resampled channel into pfOut and check their descriptors. */
static void collect_resampled(U32 u32OutRate, U32 u32OutPoints, U32 u32Frames, float *pfOut) {
    AudioFrameS stFrame;
    U32 u32Got = 0;

    while (u32Got < u32Frames) {
        REQUIRE(fake_capture_advance(1));
        while (AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK) == SYS_ERR_OK) {
            long long llPts = (long long)fake_capture_pts_ms(0, 48000) +
                (long long)((U64)u32Got * u32OutPoints * 1000ULL / u32OutRate);

            REQUIRE(u32Got < u32Frames + 2);
            REQUIRE_EQ(stFrame.eSampleRate, u32OutRate);
            REQUIRE_EQ(stFrame.u32Len, u32OutPoints * 2);
            REQUIRE_EQ(stFrame.u32Seq, u32Got);
            /* Output frame k starts at capture time k * points / rate. */
            REQUIRE(llabs((long long)stFrame.u64PTS - llPts) <= 1);
            if (u32Got < u32Frames)
                aio_pcm_to_float(stFrame.pu8Data, pfOut + (size_t)u32Got * u32OutPoints, u32OutPoints,
                    AUDIO_PCM_S16_LE);
            u32Got++;
            REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
        }
    }
}

static void test_resample(void) {
    static float afOut[400 * 341];
    AioDevAttrS stAttr;
    AudioFrameS stFrame;
    double dAmp, dRes;
    U32 i;

    /* 48 kHz device, 16 kHz channel, integer ratio: 960 -> 320 points. */
    ai_up(48000, 1, 960);
    fake_capture_set_source(tone_48k);
    REQUIRE_EQ(AI_EnableReSmp(0, 0, (AudioSampleRate)44000), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_8000), SYS_ERR_INVAL); /* before EnableChn only */
    REQUIRE_EQ(AI_DisableReSmp(0, 0), SYS_ERR_INVAL);
    collect_resampled(16000, 320, 100, afOut);
    test_fit_tone(afOut + 320, 98 * 320, 1, TONE_HZ / 16000.0, &dAmp, &dRes);
    REQUIRE(fabs(test_db(dAmp / TONE_AMP)) < 0.5);
    REQUIRE(test_db(dRes / (TONE_AMP / sqrt(2.0))) < -60.0);
    REQUIRE_EQ(AI_DisableChn(0, 0), SYS_ERR_OK);

    /* A second channel can stay at the device rate. */
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 1), SYS_ERR_OK);
    REQUIRE(fake_capture_advance(2));
    REQUIRE_EQ(AI_GetFrame(0, 1, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(stFrame.eSampleRate, AUDIO_SAMPLE_RATE_48000);
    REQUIRE_EQ(stFrame.u32Len, 960 * 2);
    REQUIRE_EQ(AI_ReleaseFrame(0, 1, &stFrame, NULL), SYS_ERR_OK);
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(stFrame.eSampleRate, AUDIO_SAMPLE_RATE_16000);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
    REQUIRE_EQ(AI_DisableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AI_DisableChn(0, 1), SYS_ERR_OK);

    /* The request stays until DisableReSmp. */
    REQUIRE_EQ(AI_DisableReSmp(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE(fake_capture_advance(1));
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(stFrame.eSampleRate, AUDIO_SAMPLE_RATE_48000);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
    ai_down();

    /* Non-integer ratio: 1024 points at 48 kHz are 341.33 at 16 kHz. Frames stay
     * 341 points long, the remainder rolls over and no sample is lost. */
    ai_up(48000, 1, 1024);
    fake_capture_set_source(tone_48k);
    REQUIRE_EQ(AI_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    collect_resampled(16000, 341, 400, afOut);
    test_fit_tone(afOut + 341, 399 * 341, 1, TONE_HZ / 16000.0, &dAmp, &dRes);
    REQUIRE(fabs(test_db(dAmp / TONE_AMP)) < 0.5);
    REQUIRE(test_db(dRes / (TONE_AMP / sqrt(2.0))) < -60.0);
    ai_down();

    /* The same rate as the device is a plain copy. */
    ai_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AI_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    for (i = 0; i < 3; i++) {
        REQUIRE(fake_capture_advance(1));
        REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
        require_ramp(&stFrame, (U64)i * PERIOD);
        REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
    }
    ai_down();

    /* 8-bit PCM is captured as is but cannot be resampled. */
    stAttr = test_dev_attr(RATE, 1, PERIOD, FRM_NUM);
    stAttr.eBitWidth = AUDIO_BIT_WIDTH_8;
    fake_reset();
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_8000), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_DisableReSmp(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE(fake_capture_advance(1));
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(stFrame.ePcmFormat, AUDIO_PCM_U8);
    REQUIRE_EQ(stFrame.u32Len, PERIOD);
    REQUIRE_EQ(stFrame.pu8Data[0], 128);        /* ramp value 0 */
    REQUIRE_EQ(stFrame.pu8Data[256 + 3], 129);  /* ramp value 259 */
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
    ai_down();

    /* A ratio whose frame would exceed MAX_AIO_POINT_NUM is refused. */
    ai_up(8000, 1, 512);
    REQUIRE_EQ(AI_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_96000), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_INVAL);
    ai_down();
}

/* ======================== Device faults ======================== */

static void test_xrun_recovery(void) {
    AudioFrameS stFrame;
    U32 i;

    ai_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE(fake_capture_advance(2));

    /* The backend reports an overrun: capture recovers, queued frames survive,
     * and the first frame after the break is marked. */
    fake_inject(AIO_AL_DIR_CAPTURE, AIO_AL_ERR_XRUN);
    REQUIRE(fake_wait_idle(AIO_AL_DIR_CAPTURE));
    REQUIRE_EQ(fake_stats()->u32Recovers, 1);
    REQUIRE(fake_capture_advance(2));
    for (i = 0; i < 4; i++) {
        REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
        REQUIRE_EQ(stFrame.u32Seq, i);
        REQUIRE_EQ(stFrame.u32Flags, i == 2 ? AUDIO_FRAME_DISCONTINUITY : 0);
        REQUIRE_EQ(stFrame.u64PTS, fake_capture_pts_ms((U64)i * PERIOD, RATE));
        require_ramp(&stFrame, (U64)i * PERIOD);
        REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
    }

    /* A real overrun: the device buffer (4 periods) fills before it is read.
     * The lost audio shows as a jump of the PTS, not as a shifted timeline. */
    fake_capture_tick(5);
    REQUIRE(fake_wait_idle(AIO_AL_DIR_CAPTURE));
    REQUIRE_EQ(fake_stats()->u32Recovers, 2);
    REQUIRE(fake_capture_advance(1));
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(stFrame.u32Flags, AUDIO_FRAME_DISCONTINUITY);
    REQUIRE_EQ(stFrame.u64PTS, fake_capture_pts_ms(9ULL * PERIOD, RATE));
    require_ramp(&stFrame, 9ULL * PERIOD);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);

    /* A suspended device is resumed the same way. */
    fake_inject(AIO_AL_DIR_CAPTURE, AIO_AL_ERR_SUSPEND);
    REQUIRE(fake_wait_idle(AIO_AL_DIR_CAPTURE));
    REQUIRE_EQ(fake_stats()->u32Recovers, 3);
    REQUIRE(fake_capture_advance(1));
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(stFrame.u32Flags, AUDIO_FRAME_DISCONTINUITY);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);

    /* An overrun reported by the read itself: the period being read goes
     * with it, capture carries on with the next one. */
    fake_inject_io(AIO_AL_DIR_CAPTURE, AIO_AL_ERR_XRUN);
    REQUIRE(fake_capture_advance(1));
    REQUIRE_EQ(fake_stats()->u32Recovers, 4);
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), AI_ERR_BUF_EMPTY);
    REQUIRE(fake_capture_advance(1));
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(stFrame.u32Flags, AUDIO_FRAME_DISCONTINUITY);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
    ai_down();
}

static void test_xrun_with_resample(void) {
    static float afOut[60 * 320];
    AudioFrameS stFrame;
    double dAmp, dRes;
    U32 u32Got = 0, u32Marked = 0;

    ai_up(48000, 1, 960);
    fake_capture_set_source(tone_48k);
    REQUIRE_EQ(AI_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE(fake_capture_advance(3));
    while (AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK) == SYS_ERR_OK)
        REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);

    fake_inject(AIO_AL_DIR_CAPTURE, AIO_AL_ERR_XRUN);
    REQUIRE(fake_wait_idle(AIO_AL_DIR_CAPTURE));
    while (u32Got < 60) {
        REQUIRE(fake_capture_advance(1));
        while (u32Got < 60 && AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK) == SYS_ERR_OK) {
            if (stFrame.u32Flags & AUDIO_FRAME_DISCONTINUITY) {
                REQUIRE_EQ(u32Got, 0); /* only the first frame after the break */
                u32Marked++;
            }
            aio_pcm_to_float(stFrame.pu8Data, afOut + (size_t)u32Got * 320, 320, AUDIO_PCM_S16_LE);
            u32Got++;
            REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
        }
    }
    REQUIRE_EQ(u32Marked, 1);
    /* The converter restarted clean: no leftovers from before the break. */
    test_fit_tone(afOut + 320, 59 * 320, 1, TONE_HZ / 16000.0, &dAmp, &dRes);
    REQUIRE(fabs(test_db(dAmp / TONE_AMP)) < 0.5);
    REQUIRE(test_db(dRes / (TONE_AMP / sqrt(2.0))) < -60.0);
    ai_down();
}

/* ======================== Capture clock ======================== */

static void next_frame(AudioFrameS *pstFrame) {
    REQUIRE(fake_capture_advance(1));
    REQUIRE_EQ(AI_GetFrame(0, 0, pstFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, pstFrame, NULL), SYS_ERR_OK);
}

static void test_capture_clock(void) {
    const U64 u64FrameMs = 1000ULL * PERIOD / RATE;
    AudioFrameS stFrame;
    U64 u64Prev, u64Now;
    U32 i;

    ai_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    for (i = 0; i < 5; i++)
        next_frame(&stFrame);
    REQUIRE_EQ(stFrame.u64PTS, fake_capture_pts_ms(4ULL * PERIOD, RATE));

    /* The device clock turns out to be 16 ms ahead of the sample count. The
     * PTS follows it gradually: no jump, no frame out of step by more than 1 ms. */
    fake_capture_set_clock(MPP_TRUE, 16 * (S64)AIO_NS_PER_MS);
    u64Prev = stFrame.u64PTS;
    for (i = 5; i < 305; i++) {
        next_frame(&stFrame);
        REQUIRE_EQ(stFrame.u32Seq, i);
        REQUIRE_EQ(stFrame.u32Flags, 0);
        REQUIRE(stFrame.u64PTS - u64Prev == u64FrameMs || stFrame.u64PTS - u64Prev == u64FrameMs + 1);
        u64Prev = stFrame.u64PTS;
    }
    REQUIRE(stFrame.u64PTS - fake_capture_pts_ms(304ULL * PERIOD, RATE) >= 15);
    REQUIRE(stFrame.u64PTS - fake_capture_pts_ms(304ULL * PERIOD, RATE) <= 16);

    /* A step of half a second is not followed, the timeline starts over:
     * the frame is marked and carries the new time; the sequence goes on. */
    fake_capture_set_clock(MPP_TRUE, 516 * (S64)AIO_NS_PER_MS);
    next_frame(&stFrame);
    REQUIRE_EQ(stFrame.u32Seq, 305);
    REQUIRE_EQ(stFrame.u32Flags, AUDIO_FRAME_DISCONTINUITY);
    REQUIRE_EQ(stFrame.u64PTS, fake_capture_pts_ms(305ULL * PERIOD, RATE) + 516);
    u64Prev = stFrame.u64PTS;
    next_frame(&stFrame);
    REQUIRE_EQ(stFrame.u32Flags, 0);
    REQUIRE_EQ(stFrame.u64PTS, u64Prev + u64FrameMs);

    /* A driver without timestamps: the frame is dated by its arrival, one
     * frame duration back. (The fake card delivers at once, so "now".) */
    fake_capture_set_clock(MPP_FALSE, 0);
    next_frame(&stFrame);
    u64Now = aio_ns_to_pts(aio_mono_ns());
    REQUIRE_EQ(stFrame.u32Flags, AUDIO_FRAME_DISCONTINUITY); /* the fake clock was nowhere near the real one */
    REQUIRE(stFrame.u64PTS <= u64Now && stFrame.u64PTS + 2000 >= u64Now);
    u64Prev = stFrame.u64PTS;
    next_frame(&stFrame);
    REQUIRE_EQ(stFrame.u32Flags, 0);
    REQUIRE(stFrame.u64PTS >= u64Prev + u64FrameMs - 5 && stFrame.u64PTS <= u64Prev + u64FrameMs + 5);
    ai_down();
}

static void test_device_lost(void) {
    AudioFrameS stFrame;
    Reader stReader;

    ai_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE(fake_capture_advance(1));

    /* The device goes away under a blocked reader. */
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
    reader_start(&stReader, AIO_BLOCK_INFINITE);
    fake_inject(AIO_AL_DIR_CAPTURE, AIO_AL_ERR_NODEV);
    pthread_join(stReader.stThread, NULL);
    REQUIRE_EQ(stReader.s32Ret, AI_ERR_ABORTED);
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), AI_ERR_ABORTED);
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, 1000), AI_ERR_ABORTED);
    REQUIRE_EQ(AI_EnableChn(0, 1), SYS_ERR_NOT_FOUND);

    /* Disable + enable brings it back. */
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE(fake_capture_advance(1));
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);

    /* An overrun that cannot be recovered ends the same way. */
    reader_start(&stReader, AIO_BLOCK_INFINITE);
    fake_set_recover_error(AIO_AL_ERR_NODEV);
    fake_inject(AIO_AL_DIR_CAPTURE, AIO_AL_ERR_XRUN);
    pthread_join(stReader.stThread, NULL);
    REQUIRE_EQ(stReader.s32Ret, AI_ERR_ABORTED);
    ai_down();
}

static void test_hardware_limits(void) {
    AioDevAttrS stAttr = test_dev_attr(RATE, 1, PERIOD, FRM_NUM);
    AudioFrameS stFrame;

    /* The card only does 48 kHz stereo: other settings fail, they are not faked. */
    fake_reset();
    fake_set_support(48000, 2, AUDIO_PCM_S16_LE);
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_INVAL);
    stAttr = test_dev_attr(48000, 2, 960, FRM_NUM);
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
    ai_down();

    /* The hardware picks its own period and a larger buffer: frames keep their size. */
    fake_reset();
    fake_set_geometry(1024, 8192);
    stAttr = test_dev_attr(RATE, 1, PERIOD, FRM_NUM);
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE(fake_capture_advance(2));
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    REQUIRE_EQ(stFrame.u32Len, PERIOD * 2);
    require_ramp(&stFrame, 0);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
    ai_down();

    /* A device buffer that cannot hold two frames is unusable. */
    fake_reset();
    fake_set_geometry(0, PERIOD);
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_INVAL);

    /* Nor is one that cannot take a hardware period of 1024 frames on top of
     * what is unread, see aio_min_device_frames(). */
    fake_set_geometry(1024, 1599);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_INVAL);
    REQUIRE_EQ(fake_stats()->u32Opens, 1);
    REQUIRE_EQ(fake_stats()->u32Closes, 1);
    fake_set_geometry(1024, 1600);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);
}

/* ======================== Not implemented yet ======================== */

static void test_stubs(void) {
    AudioControlCapsS stCaps;
    AudioAnalogGainS stGain;
    AudioSaveFileInfoS stSave;
    AudioFileStatusS stFile;
    AioDeviceInfoS stInfo;
    AudioTrackMode eTrack;
    AiEncChnAttrS stEncAttr;
    AudioStreamS stStream;
    AiVqeConfigS stVqe;
    AiChnParamS stParam;
    AiEncoderS stEncoder;
    AudioEventS stEvent;
    AudioFrameS stFrame;
    AudioFadeS stFade;
    U32 u32Count = 0;
    S32 s32Val = 0;
    BOOL bMute;

    memset(&stParam, 0, sizeof(stParam));
    memset(&stEncAttr, 0, sizeof(stEncAttr));
    memset(&stEncoder, 0, sizeof(stEncoder));
    memset(&stStream, 0, sizeof(stStream));
    memset(&stGain, 0, sizeof(stGain));
    memset(&stSave, 0, sizeof(stSave));
    memset(&stVqe, 0, sizeof(stVqe));
    ai_up(RATE, 1, PERIOD);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);

    REQUIRE_EQ(AI_EnumDevices(&stInfo, 1, &u32Count), SYS_ERR_INVAL);
    REQUIRE(AI_GetEventFd(0) < 0);
    REQUIRE_EQ(AI_GetEvent(0, &stEvent, AIO_NO_BLOCK), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_QueryControlCaps(0, &stCaps, 1, &u32Count), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_SetChnParam(0, 1, &stParam), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_GetChnParam(0, 0, &stParam), SYS_ERR_INVAL);
    REQUIRE(AI_GetFd(0, 0) < 0);
    REQUIRE_EQ(AI_RegisterEncoder(&s32Val, &stEncoder), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_UnRegisterEncoder(0), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_EnableEncChn(0, 0, &stEncAttr), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_DisableEncChn(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_GetStream(0, 0, &stStream, AIO_NO_BLOCK), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_ReleaseStream(0, 0, &stStream), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_SetVolume(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_GetVolume(0, &s32Val), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_SetAnalogGain(0, &stGain), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_GetAnalogGain(0, &stGain), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_SetMute(0, MPP_TRUE, NULL), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_GetMute(0, &bMute, &stFade), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_SetTrackMode(0, AUDIO_TRACK_NORMAL), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_GetTrackMode(0, &eTrack), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_SetVqeAttr(0, 1, AI_INVALID_DEV_ID, AI_INVALID_CHN_ID, &stVqe), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_GetVqeAttr(0, 0, &stVqe), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_EnableVqe(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_DisableVqe(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_EnableAecRef(0, 1, 0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_DisableAecRef(0, 0), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_SaveFile(0, 0, &stSave), SYS_ERR_INVAL);
    REQUIRE_EQ(AI_QueryFileStatus(0, 0, &stFile), SYS_ERR_INVAL);

    /* None of them disturbed the channel. */
    REQUIRE(fake_capture_advance(1));
    REQUIRE_EQ(AI_GetFrame(0, 0, &stFrame, NULL, AIO_NO_BLOCK), SYS_ERR_OK);
    require_ramp(&stFrame, 0);
    REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
    ai_down();
}

int main(void) {
    RUN(test_init_deinit);
    RUN(test_not_initialized);
    RUN(test_set_dev_attr);
    RUN(test_enable_dev);
    RUN(test_mic_attr);
    RUN(test_chn_enable);
    RUN(test_get_frame);
    RUN(test_get_frame_blocking);
    RUN(test_drop_oldest);
    RUN(test_multi_chn);
    RUN(test_ownership);
    RUN(test_resample);
    RUN(test_xrun_recovery);
    RUN(test_xrun_with_resample);
    RUN(test_capture_clock);
    RUN(test_device_lost);
    RUN(test_hardware_limits);
    RUN(test_stubs);

    return 0;
}
