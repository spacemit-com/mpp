/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
*
* @File      :    test_integration.c
* @Date      :    2026-3-26
* @Author    :    rmwei(rongmin.wei@spacemit.com)
* @Brief     :    Integration test for SYS + VB modules.
*                 Covers: export/import, SYS+VB combined flow,
*                 concurrent stress, and resource leak checking.
*
* Build:
*   gcc -std=c11 -D_GNU_SOURCE -pthread -Wall -Wextra -Werror \
*       -Wno-unused-variable -Wno-unused-function -I../../include/sys \
*       ../../mpi/sys/sys.c ../../mpi/sys/vb.c \
*       ../../mpi/sys/mpp_shm.c ../../mpi/sys/dma_alloc.c \
*       test_integration.c -o test_integration -lrt
*
* Run:
*   ./test_integration
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

/* debug dumps — VB_DumpPools not in public headers */
extern VOID VB_DumpPools(VOID);

#define TEST_PASS(name) printf("[PASS] %s\n", (name))
#define TEST_FAIL(name, msg) do { printf("[FAIL] %s: %s\n", (name), (msg)); exit(1); } while (0)

/* ======================== Test 1: VB Export / Import / Unexport ======================== */
static void test_export_import(void)
{
    const char *name = "export_import";
    S32 ret;
    U64 token = 0;
    UL imported_buf = 0;

    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    VbPoolCfg cfg = { .u32BufSize = 2048, .u32BufCnt = 4,
                        .eModId = MPP_ID_SYS, .eRemapMode = VB_REMAP_MODE_NONE };
    UL pool_id = VB_CreatePool(&cfg);
    assert(pool_id != 0);

    UL buf = VB_GetBuffer(pool_id, 0);
    assert(buf != 0);

    /* export */
    ret = VB_Export(buf, &token);
    assert(ret == 0);
    if (token == 0){
        TEST_FAIL(name, "export returned zero token");
    }

    /* double export should fail */
    U64 token2 = 0;
    ret = VB_Export(buf, &token2);
    if (ret == 0){
        TEST_FAIL(name, "double export should fail");
    }

    /* import — simulates another "process" getting the buffer */
    ret = VB_Import(token, &imported_buf);
    assert(ret == 0);
    if (imported_buf == 0){
        TEST_FAIL(name, "import returned zero handle");
    }

    /* original release — buffer still alive (export ref + import ref) */
    ret = VB_ReleaseBuffer(buf);
    assert(ret == 0);

    /* unexport — removes export reference */
    ret = VB_Unexport(imported_buf);
    assert(ret == 0);

    /* double unexport should fail */
    ret = VB_Unexport(imported_buf);
    if (ret == 0){
        TEST_FAIL(name, "double unexport should fail");
    }

    /* destroy should fail — imported ref still held */
    ret = VB_DestroyPool(pool_id);
    if (ret == 0){
        TEST_FAIL(name, "destroy should fail with import ref held");
    }

    /* release imported ref — buffer now free */
    ret = VB_ReleaseBuffer(imported_buf);
    assert(ret == 0);

    /* now destroy should succeed */
    ret = VB_DestroyPool(pool_id);
    assert(ret == 0);

    VB_Exit();
    SYS_Exit();
    TEST_PASS(name);
}

/* ======================== Test 2: Export Free Block ======================== */
static void test_export_invalid(void)
{
    const char *name = "export_invalid";
    S32 ret;
    U64 token = 0;

    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    VbPoolCfg cfg = { .u32BufSize = 1024, .u32BufCnt = 2,
                        .eModId = MPP_ID_SYS, .eRemapMode = VB_REMAP_MODE_NONE };
    UL pool_id = VB_CreatePool(&cfg);
    assert(pool_id != 0);

    /* export invalid handle */
    ret = VB_Export(0, &token);
    if (ret == 0){
        TEST_FAIL(name, "export invalid handle should fail");
    }

    /* export null token */
    UL buf = VB_GetBuffer(pool_id, 0);
    assert(buf != 0);
    ret = VB_Export(buf, NULL);
    if (ret == 0){
        TEST_FAIL(name, "export null token should fail");
    }

    /* import invalid token */
    UL imp = 0;
    ret = VB_Import(0, &imp);
    if (ret == 0){
        TEST_FAIL(name, "import invalid token should fail");
    }

    /* import non-exported buffer */
    UL buf2 = VB_GetBuffer(pool_id, 0);
    assert(buf2 != 0);
    ret = VB_Import((U64)buf2, &imp);
    if (ret == 0){
        TEST_FAIL(name, "import non-exported should fail");
    }

    VB_ReleaseBuffer(buf);
    VB_ReleaseBuffer(buf2);
    VB_DestroyPool(pool_id);
    VB_Exit();
    SYS_Exit();
    TEST_PASS(name);
}

/* ======================== Test 3: SYS + VB Combined Flow ======================== */
static void test_sys_vb_combined(void)
{
    const char *name = "sys_vb_combined";
    S32 ret;

    /* init both systems */
    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    /* setup PTS */
    ret = SYS_InitPTSBase(0);
    assert(ret == 0);

    /* create a pool and allocate a buffer */
    VbPoolCfg cfg = { .u32BufSize = 4096, .u32BufCnt = 4,
                        .eModId = MPP_ID_VI, .eRemapMode = VB_REMAP_MODE_NONE };
    UL pool_id = VB_CreatePool(&cfg);
    assert(pool_id != 0);

    /* set frame info on pool */
    VideoFrameInfo fi;
    memset(&fi, 0, sizeof(fi));
    fi.eFrameType = FRAME_TYPE_VI;
    fi.stCommFrameInfo.u32Width = 1920;
    fi.stCommFrameInfo.u32Height = 1080;
    fi.stCommFrameInfo.ePixelFormat = MPP_PIXEL_FORMAT_NV12;
    ret = VB_SetFrameInfo(pool_id, &fi);
    assert(ret == 0);

    /* simulate VI capture: get buffer, stamp PTS */
    UL buf = VB_GetBuffer(pool_id, 0);
    assert(buf != 0);

    U64 pts = 0;
    ret = SYS_GetCurPTS(&pts);
    assert(ret == 0);
    ret = VB_SetBufferPTS(buf, pts);
    assert(ret == 0);

    /* simulate bind: VI -> VENC */
    MppNode src  = { .eModId = MPP_ID_VI,   .s32DevId = 0, .s32ChnId = 0 };
    MppNode sink = { .eModId = MPP_ID_VENC,  .s32DevId = 0, .s32ChnId = 0 };
    ret = SYS_Bind(&src, &sink);
    assert(ret == 0);

    /* simulate zero-copy: ref add for VENC */
    ret = VB_RefAdd(buf);
    assert(ret == 0);

    /* VENC reads frame info */
    VideoFrameInfo read_fi;
    ret = VB_GetFrameInfo(buf, &read_fi);
    assert(ret == 0);
    if (read_fi.stCommFrameInfo.u32Width != 1920 ||
        read_fi.stCommFrameInfo.u32Height != 1080){
        TEST_FAIL(name, "frame info mismatch");
    }
    if (read_fi.stVFrame.u64PTS != pts){
        TEST_FAIL(name, "PTS mismatch");
    }

    /* VI releases its ref */
    ret = VB_ReleaseBuffer(buf);
    assert(ret == 0);

    /* VENC releases its ref -> buffer returns to pool */
    ret = VB_RefSub(buf);
    assert(ret == 0);

    /* unbind */
    ret = SYS_UnBind(&src, &sink);
    assert(ret == 0);

    /* debug dump */
    SYS_DumpStatus();
    VB_DumpPools();

    /* cleanup */
    ret = VB_DestroyPool(pool_id);
    assert(ret == 0);
    VB_Exit();
    SYS_Exit();

    TEST_PASS(name);
}

/* ======================== Test 4: Concurrent Stress with Leak Check ======================== */

typedef struct {
    UL pool_id;
    int iterations;
    int thread_id;
    int get_count;
    int release_count;
    int timeout_count;
    int errors;
} StressArg;

static void *stress_worker(void *arg)
{
    StressArg *sa = (StressArg *)arg;
    sa->errors = 0;
    sa->get_count = 0;
    sa->release_count = 0;
    sa->timeout_count = 0;

    for (int i = 0; i < sa->iterations; i++) {
        UL buf = VB_GetBuffer(sa->pool_id, 50);
        if (buf == 0) {
            sa->timeout_count++;
            continue;
        }
        sa->get_count++;

        /* set PTS on buffer */
        VB_SetBufferPTS(buf, (U64)(sa->thread_id * 100000 + i));

        /* sometimes add and sub a ref */
        if (i % 3 == 0) {
            S32 r = VB_RefAdd(buf);
            if (r != 0) {
                sa->errors++;
                VB_ReleaseBuffer(buf);
                continue;
            }
            usleep(50);
            r = VB_RefSub(buf);
            if (r != 0){sa->errors++;}
        }

        usleep(50 + (sa->thread_id * 7) % 100);

        S32 ret = VB_ReleaseBuffer(buf);
        if (ret != 0) {
            sa->errors++;
        } else {
            sa->release_count++;
        }
    }

    return NULL;
}

static void test_concurrent_stress(void)
{
    const char *name = "concurrent_stress";
    S32 ret;
    int num_threads = 16;
    int iterations = 500;

    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    /* small pool to maximize contention */
    VbPoolCfg cfg = { .u32BufSize = 256, .u32BufCnt = 4,
                        .eModId = MPP_ID_SYS, .eRemapMode = VB_REMAP_MODE_NONE };
    UL pool_id = VB_CreatePool(&cfg);
    assert(pool_id != 0);

    pthread_t threads[16];
    StressArg args[16];

    for (int i = 0; i < num_threads; i++) {
        args[i].pool_id    = pool_id;
        args[i].iterations = iterations;
        args[i].thread_id  = i;
        pthread_create(&threads[i], NULL, stress_worker, &args[i]);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    int total_errors = 0;
    int total_gets = 0;
    int total_releases = 0;
    int total_timeouts = 0;
    for (int i = 0; i < num_threads; i++) {
        total_errors   += args[i].errors;
        total_gets     += args[i].get_count;
        total_releases += args[i].release_count;
        total_timeouts += args[i].timeout_count;
    }

    printf("  stress stats: threads=%d iters=%d gets=%d releases=%d timeouts=%d errors=%d\n",
        num_threads, iterations, total_gets, total_releases, total_timeouts, total_errors);

    if (total_errors > 0){
        TEST_FAIL(name, "errors in concurrent stress");
    }

    /* leak check: all gets should equal releases */
    if (total_gets != total_releases) {
        char msg[128];
        snprintf(msg, sizeof(msg), "leak detected: gets=%d releases=%d",
            total_gets, total_releases);
        TEST_FAIL(name, msg);
    }

    /* pool should be fully free now */
    VB_DumpPools();

    ret = VB_DestroyPool(pool_id);
    if (ret != 0){
        TEST_FAIL(name, "destroy failed after stress — possible leak");
    }

    VB_Exit();
    SYS_Exit();
    TEST_PASS(name);
}

/* ======================== Test 5: Export Under Contention ======================== */

typedef struct {
    UL buf;
    U64 token;
    int import_count;
    int errors;
} ImportArg;

static void *import_worker(void *arg)
{
    ImportArg *ia = (ImportArg *)arg;
    ia->errors = 0;
    ia->import_count = 0;

    for (int i = 0; i < 100; i++) {
        UL imp = 0;
        S32 ret = VB_Import(ia->token, &imp);
        if (ret != 0) {
            /* may fail if buffer was unexported — acceptable */
            continue;
        }
        ia->import_count++;

        usleep(100);

        ret = VB_ReleaseBuffer(imp);
        if (ret != 0) {
            ia->errors++;
        }
    }
    return NULL;
}

static void test_export_concurrent(void)
{
    const char *name = "export_concurrent";
    S32 ret;

    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    VbPoolCfg cfg = { .u32BufSize = 1024, .u32BufCnt = 2,
                        .eModId = MPP_ID_SYS, .eRemapMode = VB_REMAP_MODE_NONE };
    UL pool_id = VB_CreatePool(&cfg);
    assert(pool_id != 0);

    UL buf = VB_GetBuffer(pool_id, 0);
    assert(buf != 0);

    U64 token = 0;
    ret = VB_Export(buf, &token);
    assert(ret == 0);

    /* spawn multiple importers */
    int num_importers = 4;
    pthread_t threads[4];
    ImportArg iargs[4];

    for (int i = 0; i < num_importers; i++) {
        iargs[i].buf = buf;
        iargs[i].token = token;
        pthread_create(&threads[i], NULL, import_worker, &iargs[i]);
    }

    for (int i = 0; i < num_importers; i++) {
        pthread_join(threads[i], NULL);
    }

    int total_imports = 0;
    int total_errors = 0;
    for (int i = 0; i < num_importers; i++) {
        total_imports += iargs[i].import_count;
        total_errors  += iargs[i].errors;
    }

    printf("  export_concurrent: imports=%d errors=%d\n", total_imports, total_errors);

    if (total_errors > 0){
        TEST_FAIL(name, "import/release errors under contention");
    }

    /* unexport and release original */
    ret = VB_Unexport(buf);
    assert(ret == 0);
    ret = VB_ReleaseBuffer(buf);
    assert(ret == 0);

    /* pool should be fully free now */
    ret = VB_DestroyPool(pool_id);
    if (ret != 0){
        TEST_FAIL(name, "destroy failed after export_concurrent — possible leak");
    }

    VB_Exit();
    SYS_Exit();
    TEST_PASS(name);
}

/* ======================== Main ======================== */
int main(void)
{
    printf("=== Integration Tests (SYS + VB) ===\n\n");

    test_export_import();
    test_export_invalid();
    test_sys_vb_combined();
    test_concurrent_stress();
    test_export_concurrent();

    printf("\n=== All integration tests passed ===\n");
    return 0;
}
