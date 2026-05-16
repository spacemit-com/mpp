/*
*------------------------------------------------------------------------------
* Copyright 2025-2026 SPACEMIT. All rights reserved.
*
* @File      :    test_multiproc.c
* @Date      :    2026-3-26
* @Author    :    rmwei(rongmin.wei@spacemit.com)
* @Brief     :    Multi-process tests for SYS + VB modules.
*                 Uses fork() to verify cross-process shared memory,
*                 DMA buffer sharing via pidfd_getfd, zero-copy SendFrame/RecvFrame,
*                 and concurrent multi-process stress.
*
* Build:
*   gcc -std=c11 -D_GNU_SOURCE -pthread -Wall -Wextra -Werror \
*       -Wno-unused-variable -Wno-unused-function -I../../include/sys \
*       ../../mpi/sys/sys.c ../../mpi/sys/vb.c \
*       ../../mpi/sys/mpp_shm.c ../../mpi/sys/dma_alloc.c \
*       test_multiproc.c -o test_multiproc -lrt
*
* Run:
*   ./test_multiproc
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
#include <sys/wait.h>
#include <sys/mman.h>

#include "sys_api.h"
#include "vb_api.h"

#define TEST_PASS(name) printf("[PASS] %s\n", (name))
#define TEST_FAIL(name, msg) do { printf("[FAIL] %s: %s\n", (name), (msg)); exit(1); } while (0)

/* Shared between parent and child via mmap'd anonymous page */
typedef struct {
    UL pool_id;
    UL buf_handle;
    U64 export_token;
    U64 phy_addr;
    int child_ready;
    int parent_done;
} SharedTestData;

static SharedTestData *alloc_shared_data(void)
{
    SharedTestData *sd = mmap(NULL, sizeof(SharedTestData),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(sd != MAP_FAILED);
    memset(sd, 0, sizeof(*sd));
    return sd;
}

static void free_shared_data(SharedTestData *sd)
{
    munmap(sd, sizeof(SharedTestData));
}

/* ======================== Test 1: Cross-Process Pool Access ======================== */
/*
* Parent: SYS_Init, VB_Init, CreatePool, GetBuffer, Export -> fork child.
* Child:  SYS_Init (attach), VB_Init (attach), Import, verify phy addr matches,
*         write to buffer, ReleaseBuffer.
* Parent: wait for child, verify buffer content, ReleaseBuffer, DestroyPool.
*/
static void test_cross_process_pool(void)
{
    const char *name = "cross_process_pool";
    SharedTestData *sd = alloc_shared_data();
    S32 ret;

    /* Parent: setup */
    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    VbPoolCfg cfg = {
        .u32BufSize  = 4096,
        .u32BufCnt   = 4,
        .eModId      = MPP_ID_SYS,
        .eRemapMode  = VB_REMAP_MODE_NONE,
    };
    sd->pool_id = VB_CreatePool(&cfg);
    assert(sd->pool_id != 0);

    sd->buf_handle = VB_GetBuffer(sd->pool_id, 0);
    assert(sd->buf_handle != 0);

    /* Export for child to import */
    ret = VB_Export(sd->buf_handle, &sd->export_token);
    assert(ret == 0);

    /* Get physical address for verification */
    ret = VB_GetPhyAddr(sd->buf_handle, &sd->phy_addr);
    assert(ret == 0 && sd->phy_addr != 0);

    /* Write a known pattern to the buffer */
    void *vir = NULL;
    ret = VB_GetVirAddr(sd->buf_handle, &vir);
    assert(ret == 0 && vir != NULL);
    memset(vir, 0xDE, 4096);

    pid_t child = fork();
    if (child == 0) {
        /* Child process */
        ret = SYS_Init();   /* attach to existing SHM */
        assert(ret == 0);
        ret = VB_Init();    /* attach to existing VB */
        assert(ret == 0);

        /* Import the buffer */
        UL imported = 0;
        ret = VB_Import(sd->export_token, &imported);
        if (ret != 0) {
            fprintf(stderr, "child: VB_Import failed ret=%d\n", ret);
            _exit(1);
        }

        /* Verify physical address matches */
        U64 child_phy = 0;
        ret = VB_GetPhyAddr(imported, &child_phy);
        if (ret != 0 || child_phy != sd->phy_addr) {
            fprintf(stderr, "child: phy mismatch parent=0x%lx child=0x%lx\n",
                (unsigned long)sd->phy_addr, (unsigned long)child_phy);
            _exit(2);
        }

        /* Get virtual address in child's address space */
        void *child_vir = NULL;
        ret = VB_GetVirAddr(imported, &child_vir);
        if (ret != 0 || child_vir == NULL) {
            fprintf(stderr, "child: VB_GetVirAddr failed ret=%d\n", ret);
            _exit(3);
        }

        /* Verify parent's written pattern */
        unsigned char *p = (unsigned char *)child_vir;
        if (p[0] != 0xDE || p[4095] != 0xDE) {
            fprintf(stderr, "child: pattern mismatch: [0]=0x%02x [4095]=0x%02x\n",
                p[0], p[4095]);
            _exit(4);
        }

        /* Write child pattern */
        memset(child_vir, 0xAB, 4096);

        /* Release imported buffer */
        ret = VB_ReleaseBuffer(imported);
        assert(ret == 0);

        VB_Exit();
        SYS_Exit();
        _exit(0);
    }

    /* Parent: wait for child */
    int status = 0;
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "child exited with status %d", WEXITSTATUS(status));
        TEST_FAIL(name, msg);
    }

    /* Verify child's written pattern (same physical memory) */
    unsigned char *pp = (unsigned char *)vir;
    if (pp[0] != 0xAB || pp[4095] != 0xAB){
        TEST_FAIL(name, "child's write not visible in parent");
    }

    /* Cleanup */
    ret = VB_Unexport(sd->buf_handle);
    assert(ret == 0);
    ret = VB_ReleaseBuffer(sd->buf_handle);
    assert(ret == 0);
    ret = VB_DestroyPool(sd->pool_id);
    assert(ret == 0);

    VB_Exit();
    SYS_Exit();
    free_shared_data(sd);

    TEST_PASS(name);
}

/* ======================== Test 2: Zero-Copy SendFrame/RecvFrame ======================== */
/*
* Parent: bind VI->VENC, create pool, get buffer, SendFrame.
* Child:  SYS_Init (attach), RecvFrame, verify handle and phy addr, ReleaseBuffer.
*/
static void test_sendrecv_frame(void)
{
    const char *name = "sendrecv_frame";
    SharedTestData *sd = alloc_shared_data();
    S32 ret;

    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    /* Create pool */
    VbPoolCfg cfg = {
        .u32BufSize  = 2048,
        .u32BufCnt   = 4,
        .eModId      = MPP_ID_VI,
        .eRemapMode  = VB_REMAP_MODE_NONE,
    };
    sd->pool_id = VB_CreatePool(&cfg);
    assert(sd->pool_id != 0);

    /* Bind VI ch0 -> VENC ch0 */
    MppNode src  = { .eModId = MPP_ID_VI,   .s32DevId = 0, .s32ChnId = 0 };
    MppNode sink = { .eModId = MPP_ID_VENC, .s32DevId = 0, .s32ChnId = 0 };
    ret = SYS_Bind(&src, &sink);
    assert(ret == 0);

    /* Get buffer, write data, set PTS */
    sd->buf_handle = VB_GetBuffer(sd->pool_id, 0);
    assert(sd->buf_handle != 0);

    void *vir = NULL;
    ret = VB_GetVirAddr(sd->buf_handle, &vir);
    assert(ret == 0 && vir != NULL);
    memset(vir, 0xCC, 2048);

    ret = VB_SetBufferPTS(sd->buf_handle, 1234567);
    assert(ret == 0);

    ret = VB_GetPhyAddr(sd->buf_handle, &sd->phy_addr);

    pid_t child = fork();
    if (child == 0) {
        /* Child: attach and receive frame */
        ret = SYS_Init();
        assert(ret == 0);
        ret = VB_Init();
        assert(ret == 0);

        /* RecvFrame should block until parent sends */
        UL recv_buf = 0;
        ret = SYS_RecvFrame(&sink, &recv_buf, 3000);  /* 3s timeout */
        if (ret != 0) {
            fprintf(stderr, "child: RecvFrame failed ret=%d\n", ret);
            _exit(1);
        }

        VideoFrameInfo recv_frame;
        memset(&recv_frame, 0, sizeof(recv_frame));
        ret = VB_GetFrameInfo(recv_buf, &recv_frame);
        if (ret != 0) {
            fprintf(stderr, "child: VB_GetFrameInfo failed ret=%d\n", ret);
            _exit(2);
        }
        if (recv_frame.stVFrame.u64PTS != 1234567 ||
            recv_frame.stVFrame.u32TotalSize != 2048 ||
            recv_frame.stVFrame.u32PlaneNum != 1 ||
            recv_frame.stVFrame.u32PlaneSizeValid[0] != 2048 ||
            recv_frame.ulBufferId != recv_buf) {
            fprintf(stderr, "child: frame info mismatch pts=%llu total=%u planes=%u valid0=%u buf=%lu/%lu\n",
                (unsigned long long)recv_frame.stVFrame.u64PTS,
                recv_frame.stVFrame.u32TotalSize,
                recv_frame.stVFrame.u32PlaneNum,
                recv_frame.stVFrame.u32PlaneSizeValid[0],
                recv_frame.ulBufferId,
                recv_buf);
            _exit(3);
        }
        if (recv_frame.stVFrame.ulPlaneVirAddr[0] == 0 || recv_frame.stVFrame.u32Fd[0] == 0) {
            fprintf(stderr, "child: frame local addr/fd not auto-filled vir=%lu fd=%lu\n",
                recv_frame.stVFrame.ulPlaneVirAddr[0], recv_frame.stVFrame.u32Fd[0]);
            _exit(4);
        }

        /* Verify it's the same buffer (same physical memory) */
        U64 recv_phy = 0;
        ret = VB_GetPhyAddr(recv_buf, &recv_phy);
        if (ret != 0 || recv_phy != sd->phy_addr) {
            fprintf(stderr, "child: phy mismatch send=0x%lx recv=0x%lx\n",
                (unsigned long)sd->phy_addr, (unsigned long)recv_phy);
            _exit(5);
        }

        /* Verify data */
        void *recv_vir = NULL;
        ret = VB_GetVirAddr(recv_buf, &recv_vir);
        if (ret != 0 || recv_vir == NULL) {
            fprintf(stderr, "child: VB_GetVirAddr failed ret=%d\n", ret);
            _exit(6);
        }
        unsigned char *p = (unsigned char *)recv_vir;
        if (p[0] != 0xCC || p[2047] != 0xCC) {
            fprintf(stderr, "child: data mismatch [0]=0x%02x [2047]=0x%02x\n",
                p[0], p[2047]);
            _exit(7);
        }

        /* Release received buffer */
        ret = VB_ReleaseBuffer(recv_buf);
        assert(ret == 0);

        VB_Exit();
        SYS_Exit();
        _exit(0);
    }

    /* Parent: give child a moment to call RecvFrame, then SendFrame */
    usleep(100000);  /* 100ms */
    ret = SYS_SendFrame(&src, sd->buf_handle);
    assert(ret == 0);

    /* Wait for child */
    int status = 0;
    waitpid(child, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        char msg[64];
        snprintf(msg, sizeof(msg), "child exited with status %d", WEXITSTATUS(status));
        TEST_FAIL(name, msg);
    }

    /* Parent release its reference (SendFrame added a ref for the sink) */
    ret = VB_ReleaseBuffer(sd->buf_handle);
    assert(ret == 0);

    /* Cleanup */
    ret = SYS_UnBind(&src, &sink);
    assert(ret == 0);
    ret = VB_DestroyPool(sd->pool_id);
    assert(ret == 0);

    VB_Exit();
    SYS_Exit();
    free_shared_data(sd);

    TEST_PASS(name);
}

/* ======================== Test 3: Multi-Process Stress ======================== */
/*
* Parent creates pool. Fork N children, each does get/release in a loop.
* Wait for all children. Verify pool is clean, destroy.
*/
static void test_multiproc_stress(void)
{
    const char *name = "multiproc_stress";
    S32 ret;
    int num_children = 4;
    int iterations = 100;

    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    VbPoolCfg cfg = {
        .u32BufSize  = 512,
        .u32BufCnt   = 4,
        .eModId      = MPP_ID_SYS,
        .eRemapMode  = VB_REMAP_MODE_NONE,
    };
    UL pool_id = VB_CreatePool(&cfg);
    assert(pool_id != 0);

    pid_t children[4];

    for (int c = 0; c < num_children; c++) {
        pid_t pid = fork();
        if (pid == 0) {
            /* Child process */
            ret = SYS_Init();
            assert(ret == 0);
            ret = VB_Init();
            assert(ret == 0);

            int errors = 0;
            for (int i = 0; i < iterations; i++) {
                UL buf = VB_GetBuffer(pool_id, 200);  /* 200ms timeout */
                if (buf == 0) {
                    /* timeout under contention is acceptable */
                    continue;
                }

                /* simulate some work */
                usleep(100 + (c * 17) % 200);

                ret = VB_ReleaseBuffer(buf);
                if (ret != 0){
                    errors++;
                }
            }

            VB_Exit();
            SYS_Exit();
            _exit(errors > 0 ? 1 : 0);
        }
        children[c] = pid;
    }

    /* Wait for all children */
    int any_failed = 0;
    for (int c = 0; c < num_children; c++) {
        int status = 0;
        waitpid(children[c], &status, 0);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("  child %d failed (status=%d)\n", c, WEXITSTATUS(status));
            any_failed = 1;
        }
    }

    if (any_failed){
        TEST_FAIL(name, "one or more children had errors");
    }

    /* Pool should be fully free now */
    ret = VB_DestroyPool(pool_id);
    if (ret != 0){
        TEST_FAIL(name, "destroy failed after stress — possible leak");
    }

    VB_Exit();
    SYS_Exit();

    TEST_PASS(name);
}

/* ======================== Test 4: Physical Address is Real ======================== */
/*
* Verify that VB buffers have real CMA physical addresses (phy != vir).
* Also verify MmzAlloc returns real physical addresses.
*/
static void test_real_phy_addr(void)
{
    const char *name = "real_phy_addr";
    S32 ret;

    ret = SYS_Init();
    assert(ret == 0);
    ret = VB_Init();
    assert(ret == 0);

    /* VB buffer */
    VbPoolCfg cfg = {
        .u32BufSize  = 4096,
        .u32BufCnt   = 2,
        .eModId      = MPP_ID_SYS,
        .eRemapMode  = VB_REMAP_MODE_NONE,
    };
    UL pool_id = VB_CreatePool(&cfg);
    assert(pool_id != 0);

    UL buf = VB_GetBuffer(pool_id, 0);
    assert(buf != 0);

    U64 phy = 0;
    void *vir = NULL;
    ret = VB_GetPhyAddr(buf, &phy);
    assert(ret == 0);
    ret = VB_GetVirAddr(buf, &vir);
    assert(ret == 0);
    printf("  VB: phy=0x%lx vir=%p\n", (unsigned long)phy, vir);

    if (phy == 0){
        TEST_FAIL(name, "VB phy addr is 0");
    }
    if (vir == NULL){
        TEST_FAIL(name, "VB vir addr is NULL");
    }
    if (phy == (U64)(uintptr_t)vir){
        TEST_FAIL(name, "VB phy == vir (not real CMA)");
    }

    VB_ReleaseBuffer(buf);
    VB_DestroyPool(pool_id);

    /* MmzAlloc */
    U64 mmz_phy = 0;
    void *mmz_vir = NULL;
    ret = SYS_MmzAlloc(&mmz_phy, &mmz_vir, "test_phy", NULL, 8192);
    assert(ret == 0);

    printf("  MMZ: phy=0x%lx vir=%p\n", (unsigned long)mmz_phy, mmz_vir);

    if (mmz_phy == 0){
        TEST_FAIL(name, "MMZ phy addr is 0");
    }
    if (mmz_vir == NULL){
        TEST_FAIL(name, "MMZ vir addr is NULL");
    }
    if (mmz_phy == (U64)(uintptr_t)mmz_vir){
        TEST_FAIL(name, "MMZ phy == vir (not real CMA)");
    }

    ret = SYS_MmzFree(mmz_phy, mmz_vir);
    assert(ret == 0);

    VB_Exit();
    SYS_Exit();

    TEST_PASS(name);
}

/* ======================== Main ======================== */
int main(void)
{
    printf("=== Multi-Process Tests (SYS + VB) ===\n\n");

    test_real_phy_addr();
    test_cross_process_pool();
    test_sendrecv_frame();
    test_multiproc_stress();

    printf("\n=== All multi-process tests passed ===\n");
    return 0;
}
