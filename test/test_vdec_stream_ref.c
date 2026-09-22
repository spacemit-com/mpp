/* Exercise the real bound input thread on main, without a codec or global SHM. */
#include "../mpi/vdec/vdec.c"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); abort(); } } while (0)
#define CHECK_EQ(a, b) do { if ((a) != (b)) { fprintf(stderr, "FAIL %d: %s == %s\n", __LINE__, #a, #b); abort(); } } while (0)

static const U8 payload[] = {1, 2, 3};
static struct {
    S32 result;
    U32 calls, releases, starts, ends;
    BOOL held, reading, empty_eos, stop_on_receive, stop_on_retry, fail_fd, fail_start, fail_end;
} fake;

S32 SYS_RecvStream(const MppNode *sink, StreamBufferInfo *stream, U32 timeoutMs) {
    CHECK(!fake.held && sink->eModId == MPP_ID_VDEC && !stream->ulVbHandle && !stream->pu8Addr && !stream->u32Size);
    *stream = (StreamBufferInfo){.pu8Addr = payload, .u32Size = sizeof(payload),
        .u64PTS = 123, .bEndOfStream = MPP_TRUE};
    if (fake.empty_eos) { stream->pu8Addr = NULL; stream->u32Size = 0; }
    if (!fake.empty_eos) {
        stream->ulVbHandle = 42;
        fake.held = MPP_TRUE;
    }
    if (fake.stop_on_receive) {
        pthread_mutex_lock(&g_stChn[0].lock);
        g_stChn[0].bStreamInputRun = MPP_FALSE;
        pthread_mutex_unlock(&g_stChn[0].lock);
    }
    return 0;
}

S32 VB_GetDmaBufFd(UL handle, S32 *fd) {
    CHECK(fake.held && handle == 42);
    if (fake.fail_fd) return -1;
    *fd = 7;
    return 0;
}

S32 VB_ReleaseBuffer(UL handle) {
    CHECK(fake.held && !fake.reading && handle == 42);
    fake.held = MPP_FALSE;
    ++fake.releases;
    return 0;
}

S32 dma_sync_buf(int fd, U32 flags) {
    CHECK(fake.held && fd == 7);
    if (flags == (DMA_SYNC_READ | DMA_SYNC_START)) {
        CHECK(!fake.reading);
        ++fake.starts;
        if (fake.fail_start) return -1;
        fake.reading = MPP_TRUE;
    } else {
        CHECK(flags == (DMA_SYNC_READ | DMA_SYNC_END) && fake.reading);
        ++fake.ends;
        fake.reading = MPP_FALSE;
        if (fake.fail_end) return -1;
    }
    return 0;
}

static S32 decode(ALBaseContext *ctx, const StreamBufferInfo *stream) {
    CHECK_EQ(stream->u64PTS, 123);
    CHECK(fake.empty_eos ? (!stream->pu8Addr && !stream->u32Size) :
        (stream->pu8Addr == payload && stream->u32Size == sizeof(payload)));
    CHECK(fake.empty_eos || (fake.held && fake.reading && !fake.releases));
    ++fake.calls;
    if (fake.stop_on_retry) g_stChn[0].bStreamInputRun = MPP_FALSE;
    if (fake.calls <= 2) return MPP_DATAQUEUE_FULL;
    return fake.result;
}

static void run(S32 result, BOOL stop_receive, BOOL stop_retry, BOOL fail_fd, BOOL fail_start, BOOL fail_end, BOOL empty_eos) {
    VdecChnCtx *chn = &g_stChn[0];
    memset(chn, 0, sizeof(*chn));
    memset(&fake, 0, sizeof(fake));
    fake.result = result;
    fake.stop_on_receive = stop_receive;
    fake.stop_on_retry = stop_retry;
    fake.fail_fd = fail_fd;
    fake.fail_start = fail_start;
    fake.fail_end = fail_end;
    fake.empty_eos = empty_eos;
    CHECK_EQ(pthread_mutex_init(&chn->lock, NULL), 0);
    CHECK_EQ(pthread_mutex_init(&chn->inputLock, NULL), 0);
    chn->bUsed = chn->bStreamInputRun = MPP_TRUE;
    chn->eState = VDEC_CHN_STATE_STARTED;
    chn->stOps.decode = decode;
    vdec_stream_input_task(chn);
    CHECK(fake.calls == (fail_fd || fail_start || stop_receive ? 0 : stop_retry ? 1 : 3));
    CHECK(!fake.held && !fake.reading);
    CHECK(fake.releases == !empty_eos);
    CHECK(fake.starts == (!empty_eos && !fail_fd));
    CHECK(fake.ends == (!empty_eos && !fail_fd && !fail_start));
    CHECK_EQ(pthread_mutex_destroy(&chn->inputLock), 0);
    CHECK_EQ(pthread_mutex_destroy(&chn->lock), 0);
}

int main(void) {
    run(MPP_OK, 0, 0, 0, 0, 0, 0);
    run(MPP_POLL_FAILED, 0, 0, 0, 0, 0, 0);
    run(MPP_CODER_EOS, 0, 0, 0, 0, 0, 0);
    run(MPP_OK, 1, 0, 0, 0, 0, 0);
    run(MPP_OK, 0, 1, 0, 0, 0, 0);
    run(MPP_OK, 0, 0, 1, 0, 0, 0);
    run(MPP_OK, 0, 0, 0, 1, 0, 0);
    run(MPP_OK, 0, 0, 0, 0, 1, 0);
    run(MPP_OK, 0, 0, 0, 0, 0, 1);
    puts("[PASS] retries retain VB; success/error/EOS/stop/fd/sync failures release it; empty_eos preserved");
    return 0;
}
