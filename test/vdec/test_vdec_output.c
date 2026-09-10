/* Copyright 2026 SPACEMIT. All rights reserved. BSD-style license: LICENSE.
 * Exercise real MPI control/readout and pool code with a scripted AL and VB.
 * No decoder device or installed plugin is used. */
#include <dlfcn.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *test_dlsym(void *handle, const char *symbol);
#define dlsym test_dlsym
#include "../../mpi/vdec/vdec.c"
#undef dlsym

#define REQUIRE(x)                                                                                                     \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);                                               \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)
#define TEST_ERROR (-12345)
#define TEST_POOLS 32

S32 dma_sync_buf(int fd, U32 flags) {
    (void)fd;
    (void)flags;
    REQUIRE(0 && "output test must not sync bound input");
    return -1;
}

typedef struct {
    BOOL active;
    VbPoolCfg cfg;
    unsigned refs[VDEC_MAX_EXT_BUF][MPP_ID_MAX];
    unsigned char data[VDEC_MAX_EXT_BUF][16];
} TestPool;
static TestPool pools[TEST_POOLS];
static unsigned nextPool;
static unsigned gets, creates, appAdds;
static pthread_mutex_t vbLock = PTHREAD_MUTEX_INITIALIZER;
static UL sinkHandle;
static atomic_uint sent;
static S32 sendResult;

static UL handle_of(unsigned p, unsigned i) { return ((UL)p << 8) | (i + 1); }
static TestPool *pool_of(UL handle, unsigned *index) {
    unsigned p = (unsigned)(handle >> 8);
    *index = (unsigned)(handle & 255) - 1;
    REQUIRE(p && p < TEST_POOLS && pools[p].active && *index < pools[p].cfg.u32BufCnt);
    return &pools[p];
}
static unsigned total_refs(TestPool *pool, unsigned i) {
    unsigned total = 0;
    for (unsigned m = 0; m < MPP_ID_MAX; ++m)
        total += pool->refs[i][m];
    return total;
}
UL VB_CreatePool(VbPoolCfg *cfg) {
    pthread_mutex_lock(&vbLock);
    REQUIRE(++nextPool < TEST_POOLS && cfg->u32BufCnt <= VDEC_MAX_EXT_BUF);
    pools[nextPool].active = MPP_TRUE;
    pools[nextPool].cfg = *cfg;
    ++creates;
    pthread_mutex_unlock(&vbLock);
    return nextPool;
}
S32 VB_DestroyPool(UL id) {
    pthread_mutex_lock(&vbLock);
    REQUIRE(id && id < TEST_POOLS && pools[id].active);
    for (unsigned i = 0; i < pools[id].cfg.u32BufCnt; ++i) {
        if (total_refs(&pools[id], i)) {
            pthread_mutex_unlock(&vbLock);
            return -5;
        }
    }
    pools[id].active = MPP_FALSE;
    pthread_mutex_unlock(&vbLock);
    return 0;
}
UL VB_ModGetBuffer(UL id, ModId mod, U32 wait) {
    pthread_mutex_lock(&vbLock);
    REQUIRE(id && id < TEST_POOLS && pools[id].active);
    ++gets;
    UL handle = 0;
    for (unsigned i = 0; i < pools[id].cfg.u32BufCnt; ++i) {
        if (!total_refs(&pools[id], i)) {
            pools[id].refs[i][mod] = 1;
            handle = handle_of((unsigned)id, i);
            break;
        }
    }
    pthread_mutex_unlock(&vbLock);
    if (!handle && wait)
        usleep(wait * 1000U);
    return handle;
}
S32 VB_ModRefAdd(UL handle, ModId mod) {
    pthread_mutex_lock(&vbLock);
    unsigned i;
    TestPool *pool = pool_of(handle, &i);
    REQUIRE(total_refs(pool, i));
    ++pool->refs[i][mod];
    if (mod == MPP_ID_SYS)
        ++appAdds;
    pthread_mutex_unlock(&vbLock);
    return 0;
}
S32 VB_ModReleaseBuffer(UL handle, ModId mod) {
    pthread_mutex_lock(&vbLock);
    unsigned i;
    TestPool *pool = pool_of(handle, &i);
    REQUIRE(pool->refs[i][mod]);
    --pool->refs[i][mod];
    pthread_mutex_unlock(&vbLock);
    return 0;
}
S32 VB_RefAdd(UL handle) { return VB_ModRefAdd(handle, MPP_ID_SYS); }
S32 VB_ReleaseBuffer(UL handle) { return VB_ModReleaseBuffer(handle, MPP_ID_SYS); }
S32 VB_GetDmaBufFd(UL handle, S32 *fd) {
    *fd = (S32)handle;
    return 0;
}
S32 VB_GetVirAddr(UL handle, VOID **ptr) {
    unsigned i;
    TestPool *pool = pool_of(handle, &i);
    *ptr = pool->data[i];
    return 0;
}
S32 VB_UpdateBufferFrameInfo(UL handle, const VideoFrameInfo *frame) {
    REQUIRE(handle == frame->ulBufferId && frame->stVFrame.u32PlaneNum);
    return 0;
}
S32 SYS_RecvStream(const MppNode *sink, StreamBufferInfo *stream, U32 wait) {
    (void)sink;
    (void)stream;
    (void)wait;
    return SYS_ERR_NOT_FOUND;
}
S32 SYS_SendFrame(const MppNode *src, UL handle) {
    REQUIRE(src->eModId == MPP_ID_VDEC);
    if (sendResult == SYS_ERR_OK) {
        VB_ModRefAdd(handle, MPP_ID_VENC);
        if (sinkHandle)
            VB_ModReleaseBuffer(sinkHandle, MPP_ID_VENC);
        sinkHandle = handle;
    }
    atomic_fetch_add(&sent, 1);
    return sendResult;
}

typedef struct {
    S32 result;
    U32 index;
    U64 pts;
    BOOL eos;
} Step;
typedef struct {
    AlDecOutputRequirement req, changed;
    Step steps[128];
    U32 stepCount, step;
    BOOL queued[VDEC_MAX_EXT_BUF];
    UL handles[VDEC_MAX_EXT_BUF];
    U32 queues, requests, resets;
    S32 failIndex;
    BOOL failAfterChange;
} TestDecoder;
static TestDecoder decoder;

static ALBaseContext *fake_create(void) { return (ALBaseContext *)&decoder; }
static S32 fake_init(ALBaseContext *ctx, const VdecChnAttr *attr, AlDecBufferRequirement *req) {
    (void)ctx;
    (void)attr;
    req->u32OutputBufNum = decoder.req.u32BufCnt;
    return MPP_OK;
}
static S32 fake_req(ALBaseContext *ctx, AlDecOutputRequirement *req) {
    (void)ctx;
    *req = decoder.req;
    return MPP_OK;
}
static S32 fake_status(ALBaseContext *ctx, VdecChnStatus *status) {
    (void)ctx;
    memset(status, 0, sizeof(*status));
    return MPP_OK;
}
static S32 fake_decode(ALBaseContext *ctx, const StreamBufferInfo *stream, U32 timeoutMs) {
    (void)ctx;
    (void)stream;
    return MPP_OK;
}
static S32 fake_queue(ALBaseContext *ctx, const VideoFrameInfo *frame) {
    (void)ctx;
    U32 i = frame->u32Idx;
    REQUIRE(i < decoder.req.u32BufCnt && !decoder.queued[i]);
    ++decoder.queues;
    if ((S32)i == decoder.failIndex)
        return TEST_ERROR;
    unsigned slot;
    TestPool *pool = pool_of(frame->stVFrame.u32Fd[0], &slot);
    REQUIRE(pool->refs[slot][MPP_ID_VDEC] == 1);
    REQUIRE(total_refs(pool, slot) == 1); /* never overwrite a held frame */
    decoder.queued[i] = MPP_TRUE;
    decoder.handles[i] = frame->stVFrame.u32Fd[0];
    return MPP_OK;
}
static S32 fake_read(ALBaseContext *ctx, VideoFrameInfo *frame, U32 wait) {
    (void)ctx;
    (void)wait;
    ++decoder.requests;
    if (decoder.step == decoder.stepCount)
        return MPP_CODER_NO_DATA;
    Step step = decoder.steps[decoder.step++];
    if (step.result == MPP_RESOLUTION_CHANGED) {
        memset(decoder.queued, 0, sizeof(decoder.queued));
        decoder.req = decoder.changed;
        if (decoder.failAfterChange)
            decoder.failIndex = 1;
        return step.result;
    }
    if (step.result == MPP_CODER_EOS || step.result == MPP_CODER_NO_DATA || step.result == TEST_ERROR)
        return step.result;
    REQUIRE(step.index < decoder.req.u32BufCnt && decoder.queued[step.index]);
    decoder.queued[step.index] = MPP_FALSE;
    frame->u32Idx = step.index;
    frame->stVFrame.u64PTS = step.pts;
    frame->stVFrame.u32Fd[0] = decoder.handles[step.index];
    frame->stVFrame.u32PlaneNum = decoder.req.u32PlaneNum;
    frame->stVdecFrameInfo.bEndOfStream = step.eos;
    return step.result;
}
static S32 fake_reset(ALBaseContext *ctx) {
    (void)ctx;
    memset(decoder.queued, 0, sizeof(decoder.queued));
    ++decoder.resets;
    return MPP_OK;
}
static void fake_destroy(ALBaseContext *ctx) {
    (void)ctx;
    for (U32 i = 0; i < decoder.req.u32BufCnt; ++i) {
        if (decoder.queued[i]) {
            unsigned slot;
            TestPool *pool = pool_of(decoder.handles[i], &slot);
            REQUIRE(pool->refs[slot][MPP_ID_VDEC] == 1);
        }
    }
    memset(decoder.queued, 0, sizeof(decoder.queued));
}
MppModule *module_init(MppModuleType type) {
    (void)type;
    return (MppModule *)&decoder;
}
void module_destory(MppModule *module) { (void)module; }
void *module_get_so_path(MppModule *module) { return module; }
static void *test_dlsym(void *handle, const char *symbol) {
    (void)handle;
#define SYMBOL(name, fn)                                                                                               \
    if (!strcmp(symbol, name))                                                                                         \
    return (void *)(fn)
    SYMBOL("al_dec_create", fake_create);
    SYMBOL("al_dec_init", fake_init);
    SYMBOL("al_dec_get_status", fake_status);
    SYMBOL("al_dec_get_output_requirement", fake_req);
    SYMBOL("al_dec_decode", fake_decode);
    SYMBOL("al_dec_request_output_frame", fake_read);
    SYMBOL("al_dec_queue_output_buffer", fake_queue);
    SYMBOL("al_dec_flush", fake_reset);
    SYMBOL("al_dec_reset", fake_reset);
    SYMBOL("al_dec_destory", fake_destroy);
#undef SYMBOL
    return NULL;
}

static void setup(VdecOutputMode mode, U32 count) {
    memset(pools, 0, sizeof(pools));
    memset(&decoder, 0, sizeof(decoder));
    nextPool = gets = creates = appAdds = 0;
    sinkHandle = 0;
    atomic_store(&sent, 0);
    sendResult = SYS_ERR_OK;
    decoder.failIndex = -1;
    decoder.req = (AlDecOutputRequirement){
        .u32BufCnt = count,
        .u32Width = 64,
        .u32Height = 64,
        .ePixelFormat = MPP_PIXEL_FORMAT_NV12,
        .u32PlaneNum = 2,
        .au32Stride = {64, 64},
        .au32Size = {4096, 2048},
    };
    REQUIRE(VDEC_Init() == 0);
    VdecChnAttr attr = {.eCodecType = MPP_STREAM_CODEC_H264, .eOutputMode = mode};
    REQUIRE(VDEC_CreateChn(0, &attr) == 0);
}
static void script(S32 result, U32 index, U64 pts, BOOL eos) {
    REQUIRE(decoder.stepCount < 128);
    decoder.steps[decoder.stepCount++] = (Step){result, index, pts, eos};
}
static void finish(void) {
    REQUIRE(VDEC_DisableChn(0) == 0);
    if (sinkHandle) {
        VB_ModReleaseBuffer(sinkHandle, MPP_ID_VENC);
        sinkHandle = 0;
    }
    REQUIRE(VDEC_DestroyChn(0) == 0);
    REQUIRE(VDEC_Exit() == 0);
    for (unsigned p = 1; p <= nextPool; ++p)
        REQUIRE(!pools[p].active);
}
static void test_pull(void) {
    setup(VDEC_OUTPUT_PULL, 2);
    script(MPP_OK, 0, 10, MPP_FALSE);
    REQUIRE(VDEC_EnableChn(0) == 0);
    usleep(15000);
    REQUIRE(decoder.requests == 0); /* no background output in PULL */
    VideoFrameInfo frame, empty;
    REQUIRE(VDEC_GetFrame(0, &frame, 0) == 0);
    unsigned slot;
    TestPool *pool = pool_of(frame.ulBufferId, &slot);
    REQUIRE(pool->refs[slot][MPP_ID_SYS] == 1 && pool->refs[slot][MPP_ID_VDEC] == 0);
    U32 queues = decoder.queues;
    REQUIRE(VDEC_GetFrame(0, &empty, 0) == ERR_VDEC_NO_FRAME);
    REQUIRE(decoder.queues == queues);
    REQUIRE(VDEC_ReleaseFrame(0, frame.ulBufferId) == 0);
    REQUIRE(VDEC_GetFrame(0, &empty, 0) == ERR_VDEC_NO_FRAME);
    REQUIRE(decoder.queues == queues + 1 && atomic_load(&sent) == 0);
    U64 start = vdec_monotonic_ms();
    REQUIRE(VDEC_GetFrame(0, &empty, 15) == ERR_VDEC_TIMEOUT);
    REQUIRE(vdec_monotonic_ms() - start >= 15);
    REQUIRE(decoder.requests < 12);
    finish();
}
static void test_latest_and_eos(void) {
    setup(VDEC_OUTPUT_PULL, 3);
    script(MPP_OK, 0, 1, MPP_FALSE);
    script(MPP_OK, 1, 2, MPP_FALSE);
    script(MPP_OK, 2, 3, MPP_TRUE);
    REQUIRE(VDEC_EnableChn(0) == 0);
    VideoFrameInfo frame;
    REQUIRE(VDEC_GetLatestFrame(0, &frame, 100) == 0);
    REQUIRE(frame.stVFrame.u64PTS == 3 && frame.stVdecFrameInfo.bEndOfStream);
    REQUIRE(VDEC_ReleaseFrame(0, frame.ulBufferId) == 0);
    REQUIRE(VDEC_GetFrame(0, &frame, 0) == ERR_VDEC_EOS);
    REQUIRE(VDEC_Reset(0) == 0);
    REQUIRE(VDEC_GetFrame(0, &frame, 0) == ERR_VDEC_NO_FRAME);
    finish();
}
static void test_latest_is_bounded(void) {
    setup(VDEC_OUTPUT_PULL, 3);
    for (U32 i = 0; i < 100; ++i)
        script(MPP_OK, i % 3, i + 1, MPP_FALSE);
    REQUIRE(VDEC_EnableChn(0) == 0);
    VideoFrameInfo frame;
    REQUIRE(VDEC_GetLatestFrame(0, &frame, 0) == 0);
    REQUIRE(frame.stVFrame.u64PTS == VDEC_MAX_EXT_BUF);
    REQUIRE(decoder.step == VDEC_MAX_EXT_BUF);
    REQUIRE(VDEC_ReleaseFrame(0, frame.ulBufferId) == 0);
    REQUIRE(VDEC_GetLatestFrame(0, &frame, 0) == 0);
    REQUIRE(frame.stVFrame.u64PTS == 100);
    REQUIRE(VDEC_ReleaseFrame(0, frame.ulBufferId) == 0);
    finish();
}
static void test_resolution_and_shutdown(void) {
    setup(VDEC_OUTPUT_PULL, 2);
    decoder.changed = decoder.req;
    decoder.changed.u32BufCnt = 4;
    decoder.changed.au32Size[0] = 512;
    decoder.changed.au32Size[1] = 256;
    script(MPP_OK, 0, 10, MPP_FALSE);
    script(MPP_RESOLUTION_CHANGED, 0, 0, MPP_FALSE);
    script(MPP_OK, 3, 20, MPP_FALSE);
    REQUIRE(VDEC_EnableChn(0) == 0);
    VideoFrameInfo old, frame;
    REQUIRE(VDEC_GetFrame(0, &old, 0) == 0);
    REQUIRE(VDEC_GetFrame(0, &frame, 100) == 0);
    REQUIRE(old.ulPoolId != frame.ulPoolId && creates == 2);
    REQUIRE(pools[frame.ulPoolId].cfg.u32BufCnt == 4);
    REQUIRE(pools[frame.ulPoolId].cfg.u32BufSize == 768);
    REQUIRE(pools[old.ulPoolId].active);
    REQUIRE(VDEC_ReleaseFrame(0, frame.ulBufferId) == 0);
    REQUIRE(VDEC_DisableChn(0) == 0);
    unsigned before = gets;
    REQUIRE(VDEC_DestroyChn(0) == ERR_VDEC_BUSY);
    REQUIRE(VDEC_EnableChn(0) == ERR_VDEC_BUSY);
    REQUIRE(gets == before); /* retired cleanup never reacquires buffers */
    REQUIRE(VDEC_ReleaseFrame(0, old.ulBufferId) == 0);
    REQUIRE(VDEC_DestroyChn(0) == 0);
    REQUIRE(VDEC_Exit() == 0);
}
static void test_latest_crosses_resolution(void) {
    setup(VDEC_OUTPUT_PULL, 2);
    decoder.changed = decoder.req;
    decoder.changed.u32BufCnt = 3;
    decoder.changed.au32Size[0] = 8192;
    script(MPP_OK, 0, 1, MPP_FALSE);
    script(MPP_RESOLUTION_CHANGED, 0, 0, MPP_FALSE);
    script(MPP_OK, 2, 2, MPP_FALSE);
    REQUIRE(VDEC_EnableChn(0) == 0);
    VideoFrameInfo old, frame;
    REQUIRE(VDEC_GetLatestFrame(0, &old, 0) == 0);
    REQUIRE(old.stVFrame.u64PTS == 1 && creates == 2);
    REQUIRE(VDEC_GetFrame(0, &frame, 0) == 0);
    REQUIRE(frame.ulPoolId != old.ulPoolId);
    REQUIRE(VDEC_ReleaseFrame(0, old.ulBufferId) == 0);
    REQUIRE(VDEC_ReleaseFrame(0, frame.ulBufferId) == 0);
    finish();
}
static void test_error_and_flush(void) {
    setup(VDEC_OUTPUT_PULL, 2);
    script(MPP_ERROR_FRAME, 0, 1, MPP_FALSE);
    script(MPP_OK, 1, 2, MPP_FALSE);
    REQUIRE(VDEC_EnableChn(0) == 0);
    VideoFrameInfo frame;
    REQUIRE(VDEC_GetFrame(0, &frame, 0) == ERR_VDEC_NO_FRAME);
    REQUIRE(VDEC_GetFrame(0, &frame, 0) == 0);
    UL held = frame.ulBufferId;
    REQUIRE(VDEC_Flush(0) == 0);
    REQUIRE(!decoder.queued[1]); /* the consumer still holds this slot */
    REQUIRE(VDEC_ReleaseFrame(0, held) == 0);
    decoder.failIndex = 1;
    REQUIRE(VDEC_GetFrame(0, &frame, 0) == TEST_ERROR);
    U32 queues = decoder.queues;
    REQUIRE(VDEC_GetFrame(0, &frame, 0) == TEST_ERROR);
    REQUIRE(decoder.queues == queues); /* latched failure, no retry spin */
    decoder.failIndex = -1;
    REQUIRE(VDEC_Reset(0) == 0);
    REQUIRE(decoder.queued[1]);
    finish();
}
static void test_partial_failure(void) {
    setup(VDEC_OUTPUT_PULL, 3);
    decoder.failIndex = 1;
    REQUIRE(VDEC_EnableChn(0) == TEST_ERROR);
    REQUIRE(VDEC_DestroyChn(0) == 0);
    REQUIRE(VDEC_Exit() == 0);
    for (unsigned p = 1; p <= nextPool; ++p)
        REQUIRE(!pools[p].active);
    setup(VDEC_OUTPUT_PULL, 2);
    decoder.changed = decoder.req;
    decoder.changed.u32BufCnt = 4;
    decoder.failAfterChange = MPP_TRUE;
    script(MPP_RESOLUTION_CHANGED, 0, 0, MPP_FALSE);
    REQUIRE(VDEC_EnableChn(0) == 0);
    VideoFrameInfo frame;
    REQUIRE(VDEC_GetFrame(0, &frame, 0) == TEST_ERROR);
    finish();
}
static void test_bind(S32 result) {
    setup(VDEC_OUTPUT_BIND, 2);
    sendResult = result;
    script(MPP_OK, 0, 1, MPP_FALSE);
    script(MPP_OK, 1, 2, MPP_TRUE);
    REQUIRE(VDEC_EnableChn(0) == 0);
    VideoFrameInfo frame;
    REQUIRE(VDEC_GetFrame(0, &frame, 0) == ERR_VDEC_NOT_SUPPORT);
    REQUIRE(VDEC_GetLatestFrame(0, &frame, 0) == ERR_VDEC_NOT_SUPPORT);
    U64 start = vdec_monotonic_ms();
    while (atomic_load(&sent) < 2 && vdec_monotonic_ms() - start < 1000)
        usleep(1000);
    REQUIRE(atomic_load(&sent) == 2);
    REQUIRE(VDEC_DisableChn(0) == 0);
    REQUIRE(appAdds == 0); /* includes the NOT_INIT / NOT_FOUND paths */
    if (sinkHandle)
        VB_ModReleaseBuffer(sinkHandle, MPP_ID_VENC);
    REQUIRE(VDEC_DestroyChn(0) == 0);
    REQUIRE(VDEC_Exit() == 0);
}
int main(void) {
    test_pull();
    test_latest_and_eos();
    test_latest_is_bounded();
    test_resolution_and_shutdown();
    test_latest_crosses_resolution();
    test_error_and_flush();
    test_partial_failure();
    test_bind(SYS_ERR_OK);
    test_bind(SYS_ERR_NOT_FOUND);
    test_bind(SYS_ERR_NOT_INIT);
    puts("PASS: VDEC pull/bind ownership, deadlines, EOS, reconfiguration, shutdown and failure recovery");
    return 0;
}
