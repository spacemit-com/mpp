/* Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd. MIT license: LICENSE.
 * Exercise a single sender and concurrent channel control without a codec device. */
#include "../../mpi/vdec/vdec.c"
#include <poll.h>

#define REQUIRE(x)                                                                                                     \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                                       \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

static struct {
    S32 result;
    U32 fullCount, calls, stopAt, waited;
    const StreamBufferInfo *packet;
} fake;
static U8 payload[] = {1, 2, 3};
static StreamBufferInfo packet = {.pu8Addr = payload, .u32Size = sizeof(payload), .u64PTS = 17};
static atomic_bool entered;

static S32 decode(ALBaseContext *ctx, const StreamBufferInfo *stream, U32 waitMs) {
    REQUIRE(stream == fake.packet && stream->pu8Addr == payload && stream->u64PTS == 17);
    REQUIRE(waitMs <= VDEC_INPUT_WAIT_SLICE_MS);
    ++fake.calls;
    atomic_store(&entered, MPP_TRUE);
    if (fake.calls == fake.stopAt)
        atomic_store(&g_stChn[0].bStreamInputRun, MPP_FALSE);
    if (fake.calls <= fake.fullCount) {
        fake.waited += waitMs;
        REQUIRE(poll(NULL, 0, (int)waitMs) == 0);
        return MPP_DATAQUEUE_FULL;
    }
    return fake.result;
}

static void setup(void) {
    VdecChnCtx *chn = &g_stChn[0];
    memset(chn, 0, sizeof(*chn));
    REQUIRE(pthread_mutex_init(&chn->lock, NULL) == 0);
    atomic_init(&chn->bUsed, MPP_TRUE);
    atomic_init(&chn->eState, VDEC_CHN_STATE_STARTED);
    atomic_init(&chn->bStreamInputRun, MPP_TRUE);
    chn->stOps.decode = decode;
    memset(&fake, 0, sizeof(fake));
    fake.packet = &packet;
    atomic_store(&entered, MPP_FALSE);
}

static void finish(void) {
    REQUIRE(pthread_mutex_destroy(&g_stChn[0].lock) == 0);
}

static void test_submission(void) {
    setup();
    REQUIRE(VDEC_SendStream(0, &packet, 0) == ERR_VDEC_OK && fake.calls == 1);
    fake.calls = 0;
    fake.fullCount = 2;
    REQUIRE(VDEC_SendStream(0, &packet, 500) == ERR_VDEC_OK);
    REQUIRE(fake.calls == 3 && fake.waited == 20);
    fake.calls = fake.waited = 0;
    fake.fullCount = (U32)-1;
    REQUIRE(VDEC_SendStream(0, &packet, 0) == ERR_VDEC_BUSY);
    REQUIRE(fake.calls == 1 && fake.waited == 0);
    U64 start = vdec_monotonic_ms();
    REQUIRE(VDEC_SendStream(0, &packet, 25) == ERR_VDEC_TIMEOUT);
    REQUIRE(vdec_monotonic_ms() - start >= 25 && fake.waited <= 25);
    fake.calls = fake.fullCount = 0;
    fake.result = MPP_POLL_FAILED;
    REQUIRE(VDEC_SendStream(0, &packet, (U32)-1) == MPP_POLL_FAILED && fake.calls == 1);
    fake.result = MPP_CODER_EOS;
    REQUIRE(VDEC_SendStream(0, &packet, 0) == ERR_VDEC_EOS);
    finish();
    puts("[PASS] packet preservation, bounded waits and result translation");
}

static void test_stop_and_binding(void) {
    setup();
    fake.fullCount = (U32)-1;
    fake.stopAt = 1;
    REQUIRE(VDEC_SendStream(0, &packet, (U32)-1) == ERR_VDEC_NOT_STARTED);
    REQUIRE(fake.calls == 1 && fake.waited == 10);
    atomic_store(&g_stChn[0].bStreamInputRun, MPP_TRUE);
    fake.calls = fake.fullCount = fake.stopAt = 0;
    g_stChn[0].bInputBound = MPP_TRUE;
    REQUIRE(VDEC_SendStream(0, &packet, 0) == ERR_VDEC_BUSY && fake.calls == 0);
    REQUIRE(vdec_submit_stream(&g_stChn[0], &packet, (U32)-1, MPP_FALSE) == MPP_OK);
    fake.calls = 0;
    g_stChn[0].bInputBound = MPP_FALSE;
    fake.stopAt = 1;
    REQUIRE(VDEC_SendStream(0, &packet, (U32)-1) == ERR_VDEC_OK);
    finish();
    puts("[PASS] stop between slices, accepted packet and bound input");
}

typedef struct {
    U32 timeout;
    S32 result;
} Sender;
static void *send_packet(void *opaque) {
    Sender *sender = opaque;
    sender->result = VDEC_SendStream(0, &packet, sender->timeout);
    return NULL;
}

static void test_channel_lock_budget(void) {
    setup();
    pthread_mutex_t *mutex = &g_stChn[0].lock;
    REQUIRE(pthread_mutex_lock(mutex) == 0);
    const U32 timeouts[] = {0, 25};
    const S32 results[] = {ERR_VDEC_BUSY, ERR_VDEC_TIMEOUT};
    for (U32 i = 0; i < 2; ++i) {
        Sender sender = {.timeout = timeouts[i]};
        pthread_t thread;
        REQUIRE(pthread_create(&thread, NULL, send_packet, &sender) == 0);
        struct timespec deadline;
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec += 2;
        /* Must finish while the other thread still owns the lock. */
        REQUIRE(pthread_timedjoin_np(thread, NULL, &deadline) == 0);
        REQUIRE(sender.result == results[i] && fake.calls == 0);
    }
    REQUIRE(pthread_mutex_unlock(mutex) == 0);
    REQUIRE(VDEC_SendStream(0, &packet, 0) == ERR_VDEC_OK);
    finish();
    puts("[PASS] lock contention respects nonblocking/finite budgets and "
        "releases locks");
}

static void test_control_handoff(void) {
    setup();
    fake.fullCount = (U32)-1;
    Sender sender = {.timeout = (U32)-1};
    pthread_t thread;
    REQUIRE(pthread_create(&thread, NULL, send_packet, &sender) == 0);
    U64 deadline = vdec_monotonic_ms() + 2000;
    while (!atomic_load(&entered) && vdec_monotonic_ms() < deadline)
        poll(NULL, 0, 1);
    REQUIRE(atomic_load(&entered));
    S32 ret = vdec_lock_channel(&g_stChn[0], deadline);
    atomic_store(&g_stChn[0].bStreamInputRun, MPP_FALSE);
    if (ret == MPP_OK)
        pthread_mutex_unlock(&g_stChn[0].lock);
    REQUIRE(pthread_join(thread, NULL) == 0);
    REQUIRE(ret == MPP_OK && sender.result == ERR_VDEC_NOT_STARTED);
    finish();
    puts("[PASS] control acquires channel lock during continuous backpressure");
}

int main(void) {
    test_submission();
    test_stop_and_binding();
    test_channel_lock_budget();
    test_control_handoff();
    return 0;
}
