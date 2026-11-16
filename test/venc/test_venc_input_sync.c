/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license
 * that can be found in the LICENSE file.
 *
 * Exercise synchronous input borrowing with a controlled codec completion.
 */
#include <pthread.h>
#include <stdlib.h>

static void *test_malloc(size_t size);
static int test_cond_wait(pthread_cond_t *cond, pthread_mutex_t *lock);
#define malloc test_malloc
#define pthread_cond_wait test_cond_wait
#include "../../mpi/venc/venc.c"
#undef pthread_cond_wait
#undef malloc

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
            exit(1); \
        } \
    } while (0)

static VencChnCtx *channel;
static VideoFrameInfo frame;
static UL pending;
static U32 allocations, adds, subs, references;
static BOOL fail_alloc, immediate, closed, received;
static S32 submit_result;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static BOOL waiting, returned;
static char fake_context, fake_module;

static void *test_malloc(size_t size) {
    allocations++;
    return fail_alloc ? NULL : malloc(size);
}

static int test_cond_wait(pthread_cond_t *cond, pthread_mutex_t *lock) {
    pthread_mutex_lock(&gate);
    waiting = MPP_TRUE;
    pthread_cond_signal(&changed);
    pthread_mutex_unlock(&gate);
    return pthread_cond_wait(cond, lock);
}

S32 VB_ModRefAdd(UL handle, ModId module) {
    REQUIRE(handle == 55 && module == MPP_ID_VENC);
    adds++;
    references++;
    return MPP_OK;
}

S32 VB_ModRefSub(UL handle, ModId module) {
    REQUIRE(handle == 55 && module == MPP_ID_VENC && references > 0);
    subs++;
    references--;
    return MPP_OK;
}

S32 VB_DestroyPool(UL pool) {
    REQUIRE(0);  /* This fixture has no output VB pool. */
    return MPP_OK;
}

void module_destory(MppModule *module) {
    REQUIRE(module == (MppModule *)&fake_module);
    REQUIRE(closed && !pending && references == 0);
}

static void complete_input(void) {
    UL token = pending;
    REQUIRE(token != 0);
    pending = 0;
    venc_input_buffer_done(channel, token);
}

static S32 send_input(ALBaseContext *ctx, const VideoFrameInfo *input) {
    REQUIRE(ctx == (ALBaseContext *)&fake_context && !pending);
    if ((input->stVFrame.u32FrameFlag & MPP_FRAME_FLAG_EOS) && input->stVFrame.u32PlaneNum == 0)
        return MPP_OK;
    REQUIRE(input->ulBufferId != 0 && input->ulBufferId != frame.ulBufferId);
    REQUIRE(input->stVFrame.ulPlaneVirAddr[0] == frame.stVFrame.ulPlaneVirAddr[0]);
    if (submit_result != MPP_OK)
        return submit_result;
    pending = input->ulBufferId;
    if (immediate)
        complete_input();
    return MPP_OK;
}

static S32 flush_input(ALBaseContext *ctx) {
    REQUIRE(ctx == (ALBaseContext *)&fake_context);
    if (pending)
        complete_input();
    return MPP_OK;
}

static void destroy_input(ALBaseContext *ctx) {
    REQUIRE(ctx == (ALBaseContext *)&fake_context);
    closed = MPP_TRUE;
    if (pending)
        complete_input();
}

S32 SYS_RecvFrame(const MppNode *sink, UL *handle, U32 timeout) {
    REQUIRE(sink->eModId == MPP_ID_VENC && sink->s32ChnId == 3 && timeout == 100);
    if (received) {
        channel->bFrameInputRun = MPP_FALSE;
        return SYS_ERR_TIMEOUT;
    }
    received = MPP_TRUE;
    references++;  /* SYS already owns the reference handed to VENC. */
    *handle = 55;
    return MPP_OK;
}

S32 VB_GetFrameInfo(UL handle, VideoFrameInfo *info) {
    REQUIRE(handle == 55);
    *info = frame;
    return MPP_OK;
}

static void *idle_worker(void *arg) {
    return NULL;
}

static void open_channel(void) {
    VencChnAttr attr = {.eFrameBufMode = VENC_FRAME_BUF_USERPTR_EXTERNAL};
    REQUIRE(VENC_CreateChn(3, &attr) == MPP_OK);
    channel = &g_stChn[3];
    channel->s32ChnId = 3;
    channel->eState = VENC_CHN_STATE_STARTED;
    channel->pAlCtx = (ALBaseContext *)&fake_context;
    channel->pModule = (MppModule *)&fake_module;
    channel->stOps.send_input_frame = send_input;
    channel->stOps.flush = flush_input;
    channel->stOps.destory = destroy_input;
    channel->bTaskRun = channel->bFrameInputRun = MPP_TRUE;
    REQUIRE(pthread_mutex_init(&channel->queueLock, NULL) == 0);
    REQUIRE(pthread_cond_init(&channel->queueNotEmpty, NULL) == 0);
    REQUIRE(pthread_cond_init(&channel->queueNotFull, NULL) == 0);
    REQUIRE(pthread_create(&channel->taskTid, NULL, idle_worker, NULL) == 0);
    REQUIRE(pthread_create(&channel->frameInputTid, NULL, idle_worker, NULL) == 0);
    closed = MPP_FALSE;
}

static void *sync_worker(void *arg) {
    VideoFrameInfo original = frame;
    REQUIRE(VENC_SendFrameSync(3, &frame) == MPP_OK);
    REQUIRE(memcmp(&frame, &original, sizeof(frame)) == 0);
    REQUIRE(references == 0);
    pthread_testcancel();
    pthread_mutex_lock(&gate);
    returned = MPP_TRUE;
    pthread_cond_signal(&changed);
    pthread_mutex_unlock(&gate);
    return NULL;
}

static pthread_t start_waiting(void) {
    pthread_t tid;
    waiting = returned = MPP_FALSE;
    REQUIRE(pthread_create(&tid, NULL, sync_worker, NULL) == 0);
    pthread_mutex_lock(&gate);
    while (!waiting)
        pthread_cond_wait(&changed, &gate);
    REQUIRE(!returned && pending);
    pthread_mutex_unlock(&gate);
    /* GetStream and lifecycle operations must remain able to take this lock. */
    REQUIRE(pthread_mutex_trylock(&channel->lock) == 0);
    pthread_mutex_unlock(&channel->lock);
    return tid;
}

static void check_direct_submission(void) {
    immediate = fail_alloc = MPP_TRUE;
    U32 before = allocations;
    REQUIRE(VENC_SendFrameSync(3, &frame) == MPP_OK);
    REQUIRE(allocations == before && !pending && references == 0);
    frame.ulBufferId = 55;
    REQUIRE(VENC_SendFrameSync(3, &frame) == MPP_OK);
    REQUIRE(adds == 1 && subs == 1 && references == 0 && allocations == before);
    submit_result = MPP_IOCTL_FAILED;
    REQUIRE(VENC_SendFrameSync(3, &frame) == MPP_IOCTL_FAILED);
    REQUIRE(references == 0 && !pending && adds == subs);
    submit_result = MPP_OK;
    REQUIRE(VENC_SendFrame(3, &frame, 0) != MPP_OK);
    REQUIRE(references == 0 && !pending && adds == subs);
    fail_alloc = MPP_FALSE;
    REQUIRE(VENC_SendFrame(3, &frame, 0) == MPP_OK);
    REQUIRE(references == 0 && adds == subs);
    submit_result = MPP_IOCTL_FAILED;
    REQUIRE(VENC_SendFrame(3, &frame, 0) == MPP_IOCTL_FAILED);
    REQUIRE(references == 0 && !pending && adds == subs);
    submit_result = MPP_OK;
    before = allocations;
    U32 refs_before = adds;
    frame.stVFrame.u32FrameFlag = MPP_FRAME_FLAG_EOS;
    frame.stVFrame.u32PlaneNum = 0;
    REQUIRE(VENC_SendFrameSync(3, &frame) == MPP_OK);
    REQUIRE(allocations == before && adds == refs_before && !pending);
    frame.stVFrame.u32FrameFlag = 0;
    /* A non-EOS DMABUF descriptor can validly omit planeNum. */
    frame.ulBufferId = 0;
    REQUIRE(VENC_SendFrameSync(3, &frame) == MPP_OK);
    frame.stVFrame.u32PlaneNum = 1;
    for (U32 i = 0; i < 40; ++i)
        REQUIRE(VENC_SendFrame(3, &frame, 0) == MPP_OK);
    REQUIRE(!pending);  /* Async external input has no completion-ID queue. */
}

static void check_bound_input(void) {
    frame.ulBufferId = 55;
    immediate = MPP_FALSE;
    for (U32 scenario = 0; scenario < 4; ++scenario) {
        U32 refs_before = adds;
        received = MPP_FALSE;
        channel->bFrameInputRun = MPP_TRUE;
        frame.stVFrame.u32FrameFlag = scenario == 1 ? MPP_FRAME_FLAG_EOS : 0;
        frame.stVFrame.u32PlaneNum = scenario == 1 ? 0 : 1;
        submit_result = scenario == 2 ? MPP_IOCTL_FAILED : MPP_OK;
        fail_alloc = scenario == 3;
        venc_frame_input_task(channel);
        if (scenario == 0) {
            REQUIRE(references == 1 && pending);
            complete_input();
        }
        REQUIRE(adds == refs_before && references == 0 && !pending);
    }
    channel->bBound = MPP_FALSE;
    fail_alloc = MPP_FALSE;
    submit_result = MPP_OK;
}

static void check_delayed_completion(void) {
    pthread_t tid = start_waiting();
    complete_input();
    REQUIRE(pthread_join(tid, NULL) == 0 && returned);
    tid = start_waiting();
    REQUIRE(pthread_cancel(tid) == 0);
    REQUIRE(pending && references == 1);
    complete_input();
    void *result = NULL;
    REQUIRE(pthread_join(tid, &result) == 0);
    REQUIRE(result == PTHREAD_CANCELED && !pending && references == 0);
    tid = start_waiting();
    REQUIRE(VENC_DisableChn(3) == MPP_OK);
    REQUIRE(pthread_join(tid, NULL) == 0 && returned && !pending);
    REQUIRE(VENC_DestroyChn(3) == MPP_OK);
}

int main(void) {
    REQUIRE(VENC_Init() == MPP_OK);
    open_channel();
    frame.stVFrame.u32PlaneNum = 1;
    frame.stVFrame.ulPlaneVirAddr[0] = (UL)(uintptr_t)&fake_context;
    check_direct_submission();
    check_bound_input();
    check_delayed_completion();
    REQUIRE(VENC_Exit() == MPP_OK);
    REQUIRE(pthread_cond_destroy(&changed) == 0);
    REQUIRE(pthread_mutex_destroy(&gate) == 0);
    puts("PASS: synchronous borrowing, cancellation, rollback, VB/SYS and safe shutdown");
    return 0;
}
