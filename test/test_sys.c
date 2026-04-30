/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      :    test_sys.c
 * @Date      :    2026-3-26
 * @Author    :    rmwei(rongmin.wei@spacemit.com)
 * @Brief     :    Minimal test for SYS module: PTS, Bind/UnBind, MmzAlloc/Free.
 *
 * Build:
 *   gcc -std=c11 -D_GNU_SOURCE -pthread -Wall -Wextra -Werror \
 *       -Wno-unused-variable -Wno-unused-function -I../../include/sys \
 *       ../../mpi/sys/sys.c ../../mpi/sys/mpp_shm.c ../../mpi/sys/dma_alloc.c \
 *       test_sys.c -o test_sys -lrt
 *
 * Run:
 *   ./test_sys
 *------------------------------------------------------------------------------
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "sys_api.h"

#define TEST_PASS(name) printf("[PASS] %s\n", (name))
#define TEST_FAIL(name, msg) do { printf("[FAIL] %s: %s\n", (name), (msg)); exit(1); } while(0)

/* ======================== Test 1: Init / Exit ======================== */
static void test_init_exit(void)
{
    const char *name = "init_exit";
    S32 ret;

    ret = SYS_Init();
    assert(ret == 0);

    /* double init succeeds (multi-process attach semantics) */
    ret = SYS_Init();
    assert(ret == 0);

    /* need two exits to match two inits */
    ret = SYS_Exit();
    assert(ret == 0);
    ret = SYS_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Test 2: PTS Monotonicity ======================== */
static void test_pts(void)
{
    const char *name = "pts";
    S32 ret;
    U64 pts1 = 0, pts2 = 0, pts3 = 0;

    ret = SYS_Init();
    assert(ret == 0);

    /* get PTS before init base — should still work (base=0) */
    ret = SYS_GetCurPTS(&pts1);
    assert(ret == 0);

    /* set base */
    ret = SYS_InitPTSBase(1000000);  /* 1 second base */
    assert(ret == 0);

    ret = SYS_GetCurPTS(&pts2);
    assert(ret == 0);
    if (pts2 < 1000000)
        TEST_FAIL(name, "PTS should be >= base after InitPTSBase");

    /* small sleep to verify monotonicity */
    usleep(10000);  /* 10ms */

    ret = SYS_GetCurPTS(&pts3);
    assert(ret == 0);
    if (pts3 <= pts2)
        TEST_FAIL(name, "PTS should be monotonically increasing");

    /* verify delta is reasonable (~10ms = ~10000us, allow 5-50ms range) */
    U64 delta = pts3 - pts2;
    if (delta < 5000 || delta > 50000)
        TEST_FAIL(name, "PTS delta unreasonable for 10ms sleep");

    /* SyncPTS — reset to a new anchor */
    ret = SYS_SyncPTS(5000000);
    assert(ret == 0);

    U64 pts4 = 0;
    ret = SYS_GetCurPTS(&pts4);
    assert(ret == 0);
    if (pts4 < 5000000)
        TEST_FAIL(name, "PTS should be >= new sync base");

    /* null ptr should fail */
    ret = SYS_GetCurPTS(NULL);
    if (ret == 0)
        TEST_FAIL(name, "null ptr should fail");

    ret = SYS_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Test 3: Bind / UnBind ======================== */
static void test_bind(void)
{
    const char *name = "bind";
    S32 ret;

    ret = SYS_Init();
    assert(ret == 0);

    MppNode src  = { .eModId = MPP_ID_VI,   .s32DevId = 0, .s32ChnId = 0 };
    MppNode sink = { .eModId = MPP_ID_VENC,  .s32DevId = 0, .s32ChnId = 0 };

    /* bind */
    ret = SYS_Bind(&src, &sink);
    assert(ret == 0);

    /* duplicate bind should fail */
    ret = SYS_Bind(&src, &sink);
    if (ret == 0)
        TEST_FAIL(name, "duplicate bind should fail");

    /* bind another pair */
    MppNode src2  = { .eModId = MPP_ID_VDEC, .s32DevId = 0, .s32ChnId = 0 };
    MppNode sink2 = { .eModId = MPP_ID_VO,   .s32DevId = 0, .s32ChnId = 0 };
    ret = SYS_Bind(&src2, &sink2);
    assert(ret == 0);

    /* one source, multiple sinks */
    MppNode sink3 = { .eModId = MPP_ID_VENC, .s32DevId = 0, .s32ChnId = 1 };
    ret = SYS_Bind(&src, &sink3);
    assert(ret == 0);

    /* unbind */
    ret = SYS_UnBind(&src, &sink);
    assert(ret == 0);

    /* double unbind should fail */
    ret = SYS_UnBind(&src, &sink);
    if (ret == 0)
        TEST_FAIL(name, "double unbind should fail");

    /* unbind remaining */
    ret = SYS_UnBind(&src2, &sink2);
    assert(ret == 0);
    ret = SYS_UnBind(&src, &sink3);
    assert(ret == 0);

    /* null ptr should fail */
    ret = SYS_Bind(NULL, &sink);
    if (ret == 0)
        TEST_FAIL(name, "null src should fail");
    ret = SYS_Bind(&src, NULL);
    if (ret == 0)
        TEST_FAIL(name, "null sink should fail");

    ret = SYS_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Test 4: MmzAlloc / MmzFree ======================== */
static void test_mmz(void)
{
    const char *name = "mmz";
    S32 ret;
    U64 phy1 = 0, phy2 = 0;
    VOID *vir1 = NULL, *vir2 = NULL;

    ret = SYS_Init();
    assert(ret == 0);

    /* allocate non-cached */
    ret = SYS_MmzAlloc(&phy1, &vir1, "test_block", NULL, 4096);
    assert(ret == 0);
    if (phy1 == 0 || vir1 == NULL)
        TEST_FAIL(name, "MmzAlloc returned null");

    /* verify physical address is real (not same as virtual) */
    if (phy1 == (U64)(uintptr_t)vir1)
        TEST_FAIL(name, "phy should differ from vir (real CMA)");

    /* allocate cached */
    ret = SYS_MmzAlloc_Cached(&phy2, &vir2, "test_cached", "zone0", 8192);
    assert(ret == 0);
    if (phy2 == 0 || vir2 == NULL)
        TEST_FAIL(name, "MmzAlloc_Cached returned null");

    /* two allocations should have different physical addresses */
    if (phy1 == phy2)
        TEST_FAIL(name, "two allocations have same phy addr");

    /* write and read back to verify memory is usable */
    memset(vir1, 0xAA, 4096);
    memset(vir2, 0xBB, 8192);

    unsigned char *p1 = (unsigned char *)vir1;
    unsigned char *p2 = (unsigned char *)vir2;
    if (p1[0] != 0xAA || p1[4095] != 0xAA)
        TEST_FAIL(name, "non-cached memory readback failed");
    if (p2[0] != 0xBB || p2[8191] != 0xBB)
        TEST_FAIL(name, "cached memory readback failed");

    /* flush cache (no-op in user-space, but should return OK) */
    ret = SYS_MmzFlushCache(phy2, vir2, 8192);
    assert(ret == 0);

    /* dump status for visual inspection */
    SYS_DumpStatus();

    /* free both */
    ret = SYS_MmzFree(phy1, vir1);
    assert(ret == 0);
    ret = SYS_MmzFree(phy2, vir2);
    assert(ret == 0);

    /* double free should fail */
    ret = SYS_MmzFree(phy1, vir1);
    if (ret == 0)
        TEST_FAIL(name, "double MmzFree should fail");

    /* zero-length alloc should fail */
    ret = SYS_MmzAlloc(&phy1, &vir1, NULL, NULL, 0);
    if (ret == 0)
        TEST_FAIL(name, "zero-length alloc should fail");

    ret = SYS_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Test 5: Exit with Active Binds ======================== */
static void test_exit_with_binds(void)
{
    const char *name = "exit_with_binds";
    S32 ret;

    ret = SYS_Init();
    assert(ret == 0);

    MppNode src  = { .eModId = MPP_ID_VI,   .s32DevId = 0, .s32ChnId = 0 };
    MppNode sink = { .eModId = MPP_ID_VENC,  .s32DevId = 0, .s32ChnId = 0 };

    ret = SYS_Bind(&src, &sink);
    assert(ret == 0);

    /* Exit should still work but warn about active binds */
    ret = SYS_Exit();
    assert(ret == 0);

    TEST_PASS(name);
}

/* ======================== Main ======================== */
int main(void)
{
    printf("=== SYS Module Tests ===\n\n");

    test_init_exit();
    test_pts();
    test_bind();
    test_mmz();
    test_exit_with_binds();

    printf("\n=== All SYS tests passed ===\n");
    return 0;
}
