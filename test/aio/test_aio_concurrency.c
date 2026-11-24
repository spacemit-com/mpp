/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * AI/AO under concurrent callers: several readers / senders on one channel,
 * blocked calls against Disable, and enable/disable and init/deinit churn
 * while data flows.
 * Runs against the scripted pcm backend, no audio hardware involved.
 */

#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#include "ai_api.h"
#include "ao_api.h"
#include "aio_fake_backend.h"
#include "aio_internal.h"
#include "aio_test.h"

#define RATE 16000U
#define PERIOD 320U
#define FRM_NUM 4U
#define PREFILL (FRM_NUM * PERIOD)
#define THREADS 4
#define MAX_SEQ 100000U

static atomic_bool g_bRun;

static void both_up(void) {
    AioDevAttrS stAttr = test_dev_attr(RATE, 1, PERIOD, FRM_NUM);

    fake_reset();
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
}

static void both_down(void) {
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);
    REQUIRE_EQ(AO_DeInit(), SYS_ERR_OK);
    REQUIRE_EQ(fake_stats()->u32Opens, fake_stats()->u32Closes);
}

static S32 send_const(int16_t s16Value, S32 s32MilliSec) {
    int16_t as16[PERIOD];
    AudioFrameS stFrame;
    U32 i;

    for (i = 0; i < PERIOD; i++)
        as16[i] = s16Value;
    memset(&stFrame, 0, sizeof(stFrame));
    stFrame.eBitWidth = AUDIO_BIT_WIDTH_16;
    stFrame.eSoundMode = AUDIO_SOUND_MODE_MONO;
    stFrame.eSampleRate = (AudioSampleRate)RATE;
    stFrame.s32Channels = 1;
    stFrame.pu8Data = (U8 *)as16;
    stFrame.u32Len = sizeof(as16);

    return AO_SendFrame(0, 0, &stFrame, s32MilliSec);
}

/* The device clocks run on their own, as fast as the framework keeps up. */
static void *free_clock_thread(void *pArg) {
    while (atomic_load(&g_bRun)) {
        fake_capture_tick(1);
        fake_playback_tick(1);
        usleep(200);
    }

    return NULL;
}

/* ======================== Several readers on one channel ======================== */

static atomic_uchar g_au8Seen[MAX_SEQ];
static atomic_uint g_u32Frames;

static void *multi_reader_thread(void *pArg) {
    AudioFrameS stFrame;
    U32 i;

    while (atomic_load(&g_bRun)) {
        S32 ret = AI_GetFrame(0, 0, &stFrame, NULL, 20);

        if (ret == SYS_ERR_TIMEOUT)
            continue;
        REQUIRE_EQ(ret, SYS_ERR_OK);
        /* Each frame goes to exactly one reader... */
        REQUIRE(stFrame.u32Seq < MAX_SEQ);
        REQUIRE_EQ(atomic_exchange(&g_au8Seen[stFrame.u32Seq], 1), 0);
        /* ...and is whole: the ramp inside has no seam. */
        for (i = 1; i < PERIOD; i++) {
            int16_t s16Prev, s16Cur;

            memcpy(&s16Prev, stFrame.pu8Data + (size_t)(i - 1) * 2, 2);
            memcpy(&s16Cur, stFrame.pu8Data + (size_t)i * 2, 2);
            REQUIRE_EQ((int16_t)(s16Cur - s16Prev), 1);
        }
        atomic_fetch_add(&g_u32Frames, 1);
        REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
    }

    return NULL;
}

static void test_multi_reader(void) {
    pthread_t astThread[THREADS];
    AiChnStateS stStat;
    U32 i;

    both_up();
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    memset(g_au8Seen, 0, sizeof(g_au8Seen));
    atomic_store(&g_u32Frames, 0);
    atomic_store(&g_bRun, MPP_TRUE);
    for (i = 0; i < THREADS; i++)
        REQUIRE(pthread_create(&astThread[i], NULL, multi_reader_thread, NULL) == 0);
    for (i = 0; i < 3000; i++)
        REQUIRE(fake_capture_advance(1));
    usleep(50000);
    atomic_store(&g_bRun, MPP_FALSE);
    for (i = 0; i < THREADS; i++)
        pthread_join(astThread[i], NULL);

    REQUIRE_EQ(AI_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    REQUIRE_EQ(stStat.u32ChnBusyNum, 0);
    /* Every captured frame was either delivered, dropped or is still queued. */
    REQUIRE_EQ(atomic_load(&g_u32Frames) + stStat.u64DroppedFrames + stStat.u32QueuedFrames, 3000);
    REQUIRE(atomic_load(&g_u32Frames) > 0);
    both_down();
}

/* ======================== Several senders on one channel ======================== */

#define FRAMES_PER_SENDER 200

static void *multi_sender_thread(void *pArg) {
    int16_t s16Value = (int16_t)(1000 * (1 + (int)(intptr_t)pArg));
    U32 i;

    for (i = 0; i < FRAMES_PER_SENDER; i++)
        REQUIRE_EQ(send_const(s16Value, AIO_BLOCK_INFINITE), SYS_ERR_OK);

    return NULL;
}

static void *playback_clock_thread(void *pArg) {
    while (atomic_load(&g_bRun))
        REQUIRE(fake_playback_advance(1));

    return NULL;
}

static void test_multi_sender(void) {
    U32 au32Count[THREADS + 1] = {0};
    pthread_t astThread[THREADS], stClock;
    AoChnStateS stStat;
    U64 u64Periods, p;
    U32 i;

    both_up();
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    atomic_store(&g_bRun, MPP_TRUE);
    REQUIRE(pthread_create(&stClock, NULL, playback_clock_thread, NULL) == 0);
    for (i = 0; i < THREADS; i++)
        REQUIRE(pthread_create(&astThread[i], NULL, multi_sender_thread, (void *)(intptr_t)i) == 0);
    for (i = 0; i < THREADS; i++)
        pthread_join(astThread[i], NULL);
    do {
        usleep(1000);
        REQUIRE_EQ(AO_QueryChnStat(0, 0, &stStat), SYS_ERR_OK);
    } while (stStat.u32ChnBusyNum > 0);
    atomic_store(&g_bRun, MPP_FALSE);
    pthread_join(stClock, NULL);
    REQUIRE(fake_wait_idle(AIO_AL_DIR_PLAYBACK));

    /* Every frame was played exactly once and never mixed with another. */
    u64Periods = fake_playback_frames() / PERIOD;
    for (p = FRM_NUM; p < u64Periods; p++) {
        int16_t s16First;

        memcpy(&s16First, fake_playback_data() + p * PERIOD * 2, 2);
        for (i = 1; i < PERIOD; i++) {
            int16_t s16;

            memcpy(&s16, fake_playback_data() + (p * PERIOD + i) * 2, 2);
            REQUIRE_EQ(s16, s16First);
        }
        REQUIRE(s16First % 1000 == 0 && s16First >= 0 && s16First <= 1000 * THREADS);
        au32Count[s16First / 1000]++;
    }
    for (i = 1; i <= THREADS; i++)
        REQUIRE_EQ(au32Count[i], FRAMES_PER_SENDER);
    REQUIRE_EQ(fake_stats()->u32Recovers, 0);
    both_down();
}

/* ======================== Blocked calls against Disable ======================== */

static void *blocked_reader_thread(void *pArg) {
    AudioFrameS stFrame;

    *(S32 *)pArg = AI_GetFrame(0, 0, &stFrame, NULL, AIO_BLOCK_INFINITE);
    return NULL;
}

static void *blocked_sender_thread(void *pArg) {
    *(S32 *)pArg = send_const(1, AIO_BLOCK_INFINITE);
    return NULL;
}

static void test_disable_wakes_everyone(void) {
    pthread_t astReader[THREADS], astSender[THREADS];
    S32 as32Reader[THREADS], as32Sender[THREADS];
    U32 i;

    both_up();
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
    while (send_const(1, AIO_NO_BLOCK) == SYS_ERR_OK) {
    }
    for (i = 0; i < THREADS; i++) {
        REQUIRE(pthread_create(&astReader[i], NULL, blocked_reader_thread, &as32Reader[i]) == 0);
        REQUIRE(pthread_create(&astSender[i], NULL, blocked_sender_thread, &as32Sender[i]) == 0);
    }
    usleep(50000);
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_DisableDev(0), SYS_ERR_OK);
    for (i = 0; i < THREADS; i++) {
        pthread_join(astReader[i], NULL);
        pthread_join(astSender[i], NULL);
        REQUIRE_EQ(as32Reader[i], AI_ERR_ABORTED);
        REQUIRE_EQ(as32Sender[i], AO_ERR_ABORTED);
    }
    both_down();
}

/* ======================== Enable / disable churn ======================== */

static void *churn_reader_thread(void *pArg) {
    AudioFrameS stFrame;

    while (atomic_load(&g_bRun)) {
        S32 ret = AI_GetFrame(0, 0, &stFrame, NULL, 5);

        if (ret == SYS_ERR_OK)
            /* Still valid even when the channel was disabled in between. */
            REQUIRE_EQ(AI_ReleaseFrame(0, 0, &stFrame, NULL), SYS_ERR_OK);
        else
            REQUIRE(ret == SYS_ERR_TIMEOUT || ret == AI_ERR_ABORTED || ret == SYS_ERR_NOT_INIT);
    }

    return NULL;
}

static void *churn_sender_thread(void *pArg) {
    while (atomic_load(&g_bRun)) {
        S32 ret = send_const(7, 5);

        REQUIRE(ret == SYS_ERR_OK || ret == SYS_ERR_TIMEOUT || ret == AO_ERR_ABORTED || ret == SYS_ERR_NOT_INIT);
    }

    return NULL;
}

static void test_churn(void) {
    pthread_t astReader[2], astSender[2], stClock;
    U32 i;

    both_up();
    atomic_store(&g_bRun, MPP_TRUE);
    REQUIRE(pthread_create(&stClock, NULL, free_clock_thread, NULL) == 0);
    for (i = 0; i < 2; i++) {
        REQUIRE(pthread_create(&astReader[i], NULL, churn_reader_thread, NULL) == 0);
        REQUIRE(pthread_create(&astSender[i], NULL, churn_sender_thread, NULL) == 0);
    }

    /* Channels come and go under running traffic. */
    for (i = 0; i < 300; i++) {
        REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
        REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
        usleep(300);
        REQUIRE_EQ(AI_DisableChn(0, 0), SYS_ERR_OK);
        REQUIRE_EQ(AO_DisableChn(0, 0), SYS_ERR_OK);
    }
    /* So do whole devices. */
    for (i = 0; i < 100; i++) {
        REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
        REQUIRE_EQ(AO_DisableDev(0), SYS_ERR_OK);
        REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
        REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
        REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
        REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
        usleep(300);
    }

    atomic_store(&g_bRun, MPP_FALSE);
    pthread_join(stClock, NULL);
    for (i = 0; i < 2; i++) {
        pthread_join(astReader[i], NULL);
        pthread_join(astSender[i], NULL);
    }
    both_down();
}

/* ======================== Init / DeInit churn ======================== */

static void *init_churn_reader_thread(void *pArg) {
    AudioFrameS stFrame;

    while (atomic_load(&g_bRun)) {
        S32 ret = AI_GetFrame(0, 0, &stFrame, NULL, 5);

        if (ret == SYS_ERR_OK) {
            /* The module may have been torn down, or set up again, in between. */
            ret = AI_ReleaseFrame(0, 0, &stFrame, NULL);
            REQUIRE(ret == SYS_ERR_OK || ret == SYS_ERR_NOT_INIT || ret == SYS_ERR_INVAL);
        } else {
            REQUIRE(ret == SYS_ERR_TIMEOUT || ret == AI_ERR_ABORTED || ret == SYS_ERR_NOT_INIT);
        }
    }

    return NULL;
}

static void test_init_churn(void) {
    AioDevAttrS stAttr = test_dev_attr(RATE, 1, PERIOD, FRM_NUM);
    pthread_t astReader[2], astSender[2], stClock;
    U32 i;

    fake_reset();
    atomic_store(&g_bRun, MPP_TRUE);
    REQUIRE(pthread_create(&stClock, NULL, free_clock_thread, NULL) == 0);
    for (i = 0; i < 2; i++) {
        REQUIRE(pthread_create(&astReader[i], NULL, init_churn_reader_thread, NULL) == 0);
        REQUIRE(pthread_create(&astSender[i], NULL, churn_sender_thread, NULL) == 0);
    }

    /* The whole module comes and goes under callers that never stop. */
    for (i = 0; i < 300; i++) {
        REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
        REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
        REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);
        REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_OK);
        REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
        REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
        REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_OK);
        REQUIRE_EQ(AO_EnableChn(0, 0), SYS_ERR_OK);
        usleep(300);
        REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
        REQUIRE_EQ(AO_DisableDev(0), SYS_ERR_OK);
        REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);
        REQUIRE_EQ(AO_DeInit(), SYS_ERR_OK);
    }

    atomic_store(&g_bRun, MPP_FALSE);
    pthread_join(stClock, NULL);
    for (i = 0; i < 2; i++) {
        pthread_join(astReader[i], NULL);
        pthread_join(astSender[i], NULL);
    }
    REQUIRE_EQ(fake_stats()->u32Opens, fake_stats()->u32Closes);
    REQUIRE_EQ(fake_stats()->u32Loads, fake_stats()->u32Unloads);
}

int main(void) {
    RUN(test_multi_reader);
    RUN(test_multi_sender);
    RUN(test_disable_wakes_everyone);
    RUN(test_churn);
    RUN(test_init_churn);

    return 0;
}
