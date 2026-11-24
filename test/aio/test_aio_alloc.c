/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * Failure rollback of AI/AO: every allocation on the enable paths is made to
 * fail in turn, and so is thread creation. Each time the call has to report
 * the failure, give back everything it had taken and work on a retry.
 *
 * The implementation is compiled into this file so that its calloc / free /
 * pthread_create can be intercepted; the scripted pcm backend is linked as
 * usual. No audio hardware involved.
 */

#include <pthread.h>
#include <stdlib.h>

static void *test_calloc(size_t nmemb, size_t size);
static void test_free(void *ptr);
static int test_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg);
#define calloc test_calloc
#define free test_free
#define pthread_create test_pthread_create
#include "../../mpi/aio/aio_common.c"
#include "../../mpi/aio/aio_resample.c"
#include "../../mpi/aio/ai.c"
#include "../../mpi/aio/ao.c"
#undef pthread_create
#undef free
#undef calloc

#include "aio_fake_backend.h"
#include "aio_test.h"

static long g_lLive;         /* allocations not freed yet */
static long g_lAllocs;       /* allocations since the last arm */
static long g_lFailAt = -1;  /* index of the allocation to fail, -1 for none */
static BOOL g_bFailThread;

static void *test_calloc(size_t nmemb, size_t size) {
    void *p;

    if (g_lAllocs++ == g_lFailAt)
        return NULL;
    p = calloc(nmemb, size);
    if (p != NULL)
        g_lLive++;

    return p;
}

static void test_free(void *ptr) {
    if (ptr != NULL)
        g_lLive--;
    free(ptr);
}

static int test_pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start)(void *), void *arg) {
    if (g_bFailThread)
        return EAGAIN;
    return pthread_create(thread, attr, start, arg);
}

static void arm(long lFailAt) {
    g_lAllocs = 0;
    g_lFailAt = lFailAt;
}

/* Bring a resampling AI channel up; SYS_ERR_OK or the first error. */
static S32 ai_bring_up(void) {
    AioDevAttrS stAttr = test_dev_attr(48000, 2, 960, 4);
    S32 ret;

    if ((ret = AI_SetDevAttr(0, &stAttr)) != SYS_ERR_OK)
        return ret;
    if ((ret = AI_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000)) != SYS_ERR_OK)
        return ret;
    if ((ret = AI_EnableDev(0)) != SYS_ERR_OK)
        return ret;
    return AI_EnableChn(0, 0);
}

static S32 ao_bring_up(void) {
    AioDevAttrS stAttr = test_dev_attr(48000, 2, 960, 4);
    S32 ret;

    if ((ret = AO_SetDevAttr(0, &stAttr)) != SYS_ERR_OK)
        return ret;
    if ((ret = AO_EnableReSmp(0, 0, AUDIO_SAMPLE_RATE_16000)) != SYS_ERR_OK)
        return ret;
    if ((ret = AO_EnableDev(0)) != SYS_ERR_OK)
        return ret;
    return AO_EnableChn(0, 0);
}

static void test_ai_alloc_failures(void) {
    long lFailAt = 0;
    S32 ret;

    fake_reset();
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    for (;;) {
        arm(lFailAt);
        ret = ai_bring_up();
        arm(-1);
        if (ret == SYS_ERR_OK)
            break;
        REQUIRE_EQ(ret, SYS_ERR_NOMEM);
        /* The failed step took nothing with it; the rest comes down cleanly. */
        REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
        REQUIRE_EQ(g_lLive, 0);
        REQUIRE_EQ(fake_stats()->u32Opens, fake_stats()->u32Closes);
        lFailAt++;
    }
    /* Device buffers, pool, converter and its two buffers at least. */
    REQUIRE(lFailAt >= 6);
    REQUIRE(g_lLive >= 6);
    REQUIRE(fake_capture_advance(3));
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(g_lLive, 0);
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);
    printf("       %ld allocation points covered\n", lFailAt);
}

static void test_ao_alloc_failures(void) {
    long lFailAt = 0;
    S32 ret;

    fake_reset();
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    for (;;) {
        arm(lFailAt);
        ret = ao_bring_up();
        arm(-1);
        if (ret == SYS_ERR_OK)
            break;
        REQUIRE_EQ(ret, SYS_ERR_NOMEM);
        REQUIRE_EQ(AO_DisableDev(0), SYS_ERR_OK);
        REQUIRE_EQ(g_lLive, 0);
        REQUIRE_EQ(fake_stats()->u32Opens, fake_stats()->u32Closes);
        lFailAt++;
    }
    REQUIRE(lFailAt >= 6);
    REQUIRE(fake_playback_advance(3));
    REQUIRE_EQ(AO_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(g_lLive, 0);
    REQUIRE_EQ(AO_DeInit(), SYS_ERR_OK);
    printf("       %ld allocation points covered\n", lFailAt);
}

static void test_thread_create_failure(void) {
    AioDevAttrS stAttr = test_dev_attr(16000, 1, 320, 4);

    fake_reset();
    REQUIRE_EQ(AI_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AO_Init(), SYS_ERR_OK);
    REQUIRE_EQ(AI_SetDevAttr(0, &stAttr), SYS_ERR_OK);
    REQUIRE_EQ(AO_SetDevAttr(0, &stAttr), SYS_ERR_OK);

    g_bFailThread = MPP_TRUE;
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_NOMEM);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_NOMEM);
    REQUIRE_EQ(g_lLive, 0);
    REQUIRE_EQ(fake_stats()->u32Opens, 2);
    REQUIRE_EQ(fake_stats()->u32Closes, 2);
    REQUIRE_EQ(AI_EnableChn(0, 0), SYS_ERR_NOT_INIT); /* the device stayed disabled */

    g_bFailThread = MPP_FALSE;
    REQUIRE_EQ(AI_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_EnableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AI_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(AO_DisableDev(0), SYS_ERR_OK);
    REQUIRE_EQ(g_lLive, 0);
    REQUIRE_EQ(AI_DeInit(), SYS_ERR_OK);
    REQUIRE_EQ(AO_DeInit(), SYS_ERR_OK);
}

int main(void) {
    RUN(test_ai_alloc_failures);
    RUN(test_ao_alloc_failures);
    RUN(test_thread_create_failure);

    return 0;
}
