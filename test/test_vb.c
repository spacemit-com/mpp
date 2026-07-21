/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_vb.c
 * @Date      :    2026-3-26
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Minimal test for VB CreatePool/DestroyPool/GetBuffer/ReleaseBuffer.
 *                 Covers: common pool, private pool, refcount, timeout, multi-thread.
 *
 * Build:
 *   gcc -std=c11 -D_GNU_SOURCE -pthread -Wall -Wextra -Werror \
 *       -Wno-unused-variable -Wno-unused-function -I../../include/sys \
 *       ../../mpi/sys/sys.c ../../mpi/sys/vb.c \
 *       ../../mpi/sys/mpp_shm.c ../../mpi/sys/dma_alloc.c \
 *       test_vb.c -o test_vb -lrt
 *
 * Run:
 *   ./test_vb
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>

#include "sys_api.h"
#include "vb_api.h"

#define TEST_PASS(name) printf("[PASS] %s\n", (name))
#define TEST_FAIL(name, msg)                      \
    do {                                          \
        printf("[FAIL] %s: %s\n", (name), (msg)); \
        exit(1);                                  \
    } while (0)

/* ======================== Test 1: Basic Lifecycle ======================== */
static void test_basic_lifecycle(void) {
    const char *name = "basic_lifecycle";
    S32 ret __attribute__((unused));
    UL pool_id, buf1, buf2;

    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    /* create a common pool (eModId = MPP_ID_SYS) */
    VbPoolCfg cfg = {
        .u32BufSize = 4096,
        .u32BufCnt = 4,
        .eModId = MPP_ID_SYS,
        .eRemapMode = VB_REMAP_MODE_NONE,
    };
    pool_id = VB_CreatePool(&cfg);
    if (pool_id == 0)
        TEST_FAIL(name, "CreatePool returned 0");

    /* get 2 buffers */
    buf1 = VB_GetBuffer(pool_id, 0);
    buf2 = VB_GetBuffer(pool_id, 0);
    if (buf1 == 0 || buf2 == 0)
        TEST_FAIL(name, "GetBuffer returned 0");
    if (buf1 == buf2)
        TEST_FAIL(name, "got same buffer twice");

    /* release both */
    ret = VB_ReleaseBuffer(buf1);
    assert(ret == 0);
    ret = VB_ReleaseBuffer(buf2);
    assert(ret == 0);

    /* destroy pool */
    ret = VB_DestroyPool(pool_id);
    assert(ret == 0);

    ret = VB_Exit();
    assert(ret == 0);
    SYS_Exit();

    TEST_PASS(name);
}

/* ======================== Test 2: Pool Exhaustion & Timeout ======================== */
static void test_exhaustion_timeout(void) {
    const char *name = "exhaustion_timeout";
    S32 ret __attribute__((unused));
    UL pool_id;
    UL bufs[3];

    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    VbPoolCfg cfg = {
        .u32BufSize = 1024,
        .u32BufCnt = 3,
        .eModId = MPP_ID_VI, /* private pool for VI */
        .eRemapMode = VBUF_REMAP_MODE_NOCACHE,
    };
    pool_id = VB_CreatePool(&cfg);
    assert(pool_id != 0);

    /* exhaust all buffers */
    for (int i = 0; i < 3; i++) {
        bufs[i] = VB_GetBuffer(pool_id, 0);
        assert(bufs[i] != 0);
    }

    /* next get should fail (non-blocking) */
    UL fail_buf = VB_GetBuffer(pool_id, 0);
    if (fail_buf != 0)
        TEST_FAIL(name, "should fail when pool exhausted");

    /* timed wait should timeout */
    fail_buf = VB_GetBuffer(pool_id, 50);
    if (fail_buf != 0)
        TEST_FAIL(name, "should timeout");

    /* release one, then get should succeed */
    ret = VB_ReleaseBuffer(bufs[0]);
    assert(ret == 0);

    UL new_buf = VB_GetBuffer(pool_id, 0);
    if (new_buf == 0)
        TEST_FAIL(name, "should get buffer after release");

    /* cleanup */
    VB_ReleaseBuffer(new_buf);
    VB_ReleaseBuffer(bufs[1]);
    VB_ReleaseBuffer(bufs[2]);
    VB_DestroyPool(pool_id);
    VB_Exit();
    SYS_Exit();

    TEST_PASS(name);
}

/* ======================== Test 3: RefCount ======================== */
static void test_refcount(void) {
    const char *name = "refcount";
    S32 ret;

    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    VbPoolCfg cfg = {.u32BufSize = 2048, .u32BufCnt = 2, .eModId = MPP_ID_SYS, .eRemapMode = VB_REMAP_MODE_NONE};
    UL pool_id = VB_CreatePool(&cfg);
    assert(pool_id != 0);

    UL buf = VB_GetBuffer(pool_id, 0);
    assert(buf != 0);

    /* add 2 refs -> total refcount = 3 */
    ret = VB_RefAdd(buf);
    assert(ret == 0);
    ret = VB_RefAdd(buf);
    assert(ret == 0);

    /* release 2 times -> refcount = 1, buffer still held */
    ret = VB_ReleaseBuffer(buf);
    assert(ret == 0);
    ret = VB_RefSub(buf);
    assert(ret == 0);

    /* cannot destroy pool while buffer is held */
    ret = VB_DestroyPool(pool_id);
    if (ret == 0)
        TEST_FAIL(name, "destroy should fail with outstanding buffer");

    /* final release -> refcount = 0, block returned */
    ret = VB_ReleaseBuffer(buf);
    assert(ret == 0);

    /* double release should fail */
    ret = VB_ReleaseBuffer(buf);
    if (ret == 0)
        TEST_FAIL(name, "double release should fail");

    /* now destroy should succeed */
    ret = VB_DestroyPool(pool_id);
    assert(ret == 0);

    VB_Exit();
    SYS_Exit();
    TEST_PASS(name);
}

/* ======================== Test 4: Multi-Thread ======================== */

typedef struct {
    UL pool_id;
    int iterations;
    int thread_id;
    int errors;
} ThreadArg;

static void *thread_worker(void *arg) {
    ThreadArg *ta = (ThreadArg *)arg;
    ta->errors = 0;

    for (int i = 0; i < ta->iterations; i++) {
        UL buf = VB_GetBuffer(ta->pool_id, 100);
        if (buf == 0) {
            /* timeout is acceptable under contention */
            continue;
        }

        /* simulate some work */
        usleep(100);

        S32 ret = VB_ReleaseBuffer(buf);
        if (ret != 0) {
            ta->errors++;
        }
    }

    return NULL;
}

static void test_multithread(void) {
    const char *name = "multithread";
    S32 ret __attribute__((unused));
    int num_threads = 8;
    int iterations = 200;

    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    VbPoolCfg cfg = {.u32BufSize = 512, .u32BufCnt = 4, .eModId = MPP_ID_SYS, .eRemapMode = VB_REMAP_MODE_NONE};
    UL pool_id = VB_CreatePool(&cfg);
    assert(pool_id != 0);

    pthread_t threads[8];
    ThreadArg args[8];

    for (int i = 0; i < num_threads; i++) {
        args[i].pool_id = pool_id;
        args[i].iterations = iterations;
        args[i].thread_id = i;
        args[i].errors = 0;
        pthread_create(&threads[i], NULL, thread_worker, &args[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    int total_errors = 0;
    for (int i = 0; i < num_threads; i++) {
        total_errors += args[i].errors;
    }

    if (total_errors > 0)
        TEST_FAIL(name, "release errors in multi-thread");

    ret = VB_DestroyPool(pool_id);
    assert(ret == 0);

    VB_Exit();
    SYS_Exit();
    TEST_PASS(name);
}

/* ======================== Test 5: Destroy with Outstanding ======================== */
static void test_destroy_outstanding(void) {
    const char *name = "destroy_outstanding";
    S32 ret;

    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    VbPoolCfg cfg = {.u32BufSize = 1024, .u32BufCnt = 2, .eModId = MPP_ID_VDEC, .eRemapMode = VB_REMAP_MODE_NONE};
    UL pool_id = VB_CreatePool(&cfg);
    assert(pool_id != 0);

    UL buf = VB_GetBuffer(pool_id, 0);
    assert(buf != 0);

    /* destroy should fail because a buffer is outstanding */
    ret = VB_DestroyPool(pool_id);
    if (ret == 0)
        TEST_FAIL(name, "destroy should fail with outstanding buffer");

    /* release then destroy should work */
    VB_ReleaseBuffer(buf);
    ret = VB_DestroyPool(pool_id);
    assert(ret == 0);

    VB_Exit();
    SYS_Exit();
    TEST_PASS(name);
}

/* ======================== Test 6: Packed YUV422 Layout ======================== */
static void test_packed_yuv422_layout(void) {
    const char *name = "packed_yuv422_layout";
    const MppPixelFormat formats[] = {MPP_PIXEL_FORMAT_YUYV, MPP_PIXEL_FORMAT_UYVY};
    const U32 width = 641;
    const U32 height = 479;
    const U32 align = 16;
    const U32 expected_stride = ((width * 2 + align - 1) / align) * align;
    const U32 expected_height = ((height + align - 1) / align) * align;

    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        VideoFrameInfo frame;
        memset(&frame, 0, sizeof(frame));
        frame.stCommFrameInfo.ePixelFormat = formats[i];
        frame.stCommFrameInfo.u32Width = width;
        frame.stCommFrameInfo.u32Height = height;
        frame.stCommFrameInfo.u32Align = align;

        S32 size = VB_GetPicBufferSize(&frame);
        if (size <= 0 || frame.stVFrame.u32PlaneStride[0] != expected_stride ||
            frame.stVFrame.u32PlaneSizeValid[0] != expected_stride * expected_height ||
            frame.stVFrame.u32TotalSize != (U32)size) {
            TEST_FAIL(name, "invalid packed YUV422 buffer layout");
        }
    }

    TEST_PASS(name);
}

/* ======================== Test 7: Reject Stale Export Token ======================== */
static void test_stale_export_token(void) {
    const char *name = "stale_export_token";
    S32 ret;
    U64 token_a = 0;
    U64 token_b = 0;
    UL imported = 0;

    ret = SYS_Init();
    if (ret != 0)
        TEST_FAIL(name, "SYS_Init failed");
    ret = VB_Init();
    if (ret != 0)
        TEST_FAIL(name, "VB_Init failed");

    VbPoolCfg cfg = {
        .u32BufSize = 1024,
        .u32BufCnt = 1,
        .eModId = MPP_ID_SYS,
        .eRemapMode = VB_REMAP_MODE_NONE,
    };
    UL pool_id = VB_CreatePool(&cfg);
    if (pool_id == 0)
        TEST_FAIL(name, "VB_CreatePool failed");

    UL frame_a = VB_GetBuffer(pool_id, 0);
    if (frame_a == 0)
        TEST_FAIL(name, "VB_GetBuffer for frame A failed");
    if (VB_Export(frame_a, &token_a) != 0)
        TEST_FAIL(name, "VB_Export for frame A failed");
    /* Match an asynchronous producer: publish the token, then release its
     * acquisition reference while the export reference keeps A alive. */
    if (VB_ReleaseBuffer(frame_a) != 0)
        TEST_FAIL(name, "VB_ReleaseBuffer for frame A failed");
    if (VB_Unexport(frame_a) != 0)
        TEST_FAIL(name, "VB_Unexport for frame A failed");

    if (VB_Import(token_a, &imported) != VB_ERR_STALE_TOKEN)
        TEST_FAIL(name, "revoked token did not report stale before slot reuse");

    UL frame_b = VB_GetBuffer(pool_id, 0);
    if (frame_b != frame_a)
        TEST_FAIL(name, "single-block pool did not reuse frame A handle");
    if (VB_Export(frame_b, &token_b) != 0)
        TEST_FAIL(name, "VB_Export for frame B failed");
    /* refcount is now acquisition + export; retain only the export while the
     * descriptor is in flight. */
    if (VB_ReleaseBuffer(frame_b) != 0)
        TEST_FAIL(name, "VB_ReleaseBuffer for frame B failed");

    if (token_b == token_a)
        TEST_FAIL(name, "reused slot returned the same token");
    if (VB_Import(token_a, &imported) != VB_ERR_STALE_TOKEN)
        TEST_FAIL(name, "stale token did not report stale after slot reuse");
    if (VB_Import(token_b, &imported) != 0)
        TEST_FAIL(name, "current token failed to import");

    /* Drop the import reference first, then the final export reference. */
    if (VB_ReleaseBuffer(imported) != 0)
        TEST_FAIL(name, "VB_ReleaseBuffer for imported frame B failed");
    if (VB_Unexport(frame_b) != 0)
        TEST_FAIL(name, "VB_Unexport for frame B failed");
    if (VB_DestroyPool(pool_id) != 0)
        TEST_FAIL(name, "VB_DestroyPool failed");
    if (VB_Exit() != 0)
        TEST_FAIL(name, "VB_Exit failed");
    if (SYS_Exit() != 0)
        TEST_FAIL(name, "SYS_Exit failed");
    TEST_PASS(name);
}

/* ======================== Main ======================== */
int main(void) {
    printf("=== VB Module Tests ===\n\n");

    test_basic_lifecycle();
    test_exhaustion_timeout();
    test_refcount();
    test_multithread();
    test_destroy_outstanding();
    test_packed_yuv422_layout();
    test_stale_export_token();

    printf("\n=== All tests passed ===\n");
    return 0;
}
