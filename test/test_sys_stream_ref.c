/* Exercise real SYS queues with a counted VB owner and a memfd DMA allocator.
 * No hardware or global /mpp_ctrl shared-memory segment is used. */
#include "../mpi/sys/sys.c"
#include <fcntl.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); abort(); } } while (0)
#define CHECK_EQ(a, b) do { if ((a) != (b)) { fprintf(stderr, "FAIL %d: %s == %s\n", __LINE__, #a, #b); abort(); } } while (0)
#define CHECK_GE(a, b) do { if (!((a) >= (b))) { fprintf(stderr, "FAIL %d: %s >= %s\n", __LINE__, #a, #b); abort(); } } while (0)

static MppSharedMem shared;
static U8 source_data[64] = "original-camera-packet";
static const UL source_handle = 42;
static int source_fd, refs, allocations, read_starts, read_ends;
static BOOL fail_ref, fail_start, fail_end, fail_map;
static const MppNode source = {MPP_ID_UVC, 0, 0};
static const MppNode sink = {MPP_ID_VDEC, 0, 0};
static const MppNode sink2 = {MPP_ID_VDEC, 0, 1};

MppSharedMem *mpp_shm_get(void) { return &shared; }
void dma_alloc_deinit(void) {}
S32 mpp_shm_detach(void (*on_last)(MppSharedMem *)) { if (on_last) on_last(&shared); return 0; }
S32 VB_GetFrameInfo(UL handle, VideoFrameInfo *frame) {
    if (handle != source_handle || refs <= 0) return -1;
    memset(frame, 0, sizeof(*frame));
    frame->stVFrame.u32PlaneSize[0] = sizeof(source_data);
    return 0;
}
S32 VB_RefAdd(UL handle) {
    if (handle != source_handle || refs <= 0 || fail_ref) return -1;
    ++refs;
    return 0;
}
S32 VB_ReleaseBuffer(UL handle) {
    CHECK(handle == source_handle && refs > 0);
    --refs;
    return 0;
}
S32 VB_GetDmaBufFd(UL handle, S32 *fd) {
    CHECK(handle == source_handle && refs > 0);
    *fd = source_fd;
    return 0;
}
S32 VB_GetVirAddr(UL handle, VOID **ptr) {
    CHECK(handle == source_handle && refs > 0);
    if (fail_map) return -1;
    *ptr = source_data;
    return 0;
}
S32 dma_sync_buf(int fd, U32 flags) {
    CHECK_GE(fcntl(fd, F_GETFD), 0);
    if (fd == source_fd) {
        if (flags == (DMA_SYNC_READ | DMA_SYNC_START)) {
            if (fail_start) return -1;
            ++read_starts;
        } else {
            CHECK(flags == (DMA_SYNC_READ | DMA_SYNC_END));
            ++read_ends;
            if (fail_end) return -1;
        }
    }
    return 0;
}
S32 dma_alloc_buf(U32 size, int *fd, U64 *phy, VOID **ptr) {
    ++allocations;
    *fd = memfd_create("sys-stream-test", MFD_CLOEXEC);
    CHECK(*fd >= 0 && ftruncate(*fd, size) == 0);
    *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, *fd, 0);
    CHECK(*ptr != MAP_FAILED);
    *phy = 0;
    return 0;
}
void dma_free_buf(int fd, VOID *ptr, U32 size) {
    CHECK(fd != source_fd);
    if (ptr) CHECK_EQ(munmap(ptr, size), 0);
    CHECK_EQ(close(fd), 0);
}

static StreamBufferInfo packet(void) {
    return (StreamBufferInfo){.pu8Addr = source_data, .u32Size = sizeof(source_data),
        .u64PTS = 123, .ulPrivate = 99, .eCodecType = MPP_STREAM_CODEC_MJPEG, .ulVbHandle = source_handle};
}

static S32 receive_vb(const MppNode *node, StreamBufferInfo *stream, UL *held, U32 timeout) {
    S32 ret = SYS_RecvStream(node, stream, timeout);
    *held = stream->ulVbHandle;
    return ret;
}

static void test_borrow_and_unbind(void) {
    StreamBufferInfo sent = packet(), received = {0};
    UL held = 0;
    CHECK_EQ(SYS_Bind(&source, &sink), 0);
    sent.pu8Addr = NULL; /* VB send must not dereference the supplied pointer. */
    refs = 1;
    CHECK(SYS_SendStream(&source, &sent) == 0 && refs == 2);
    CHECK(VB_ReleaseBuffer(source_handle) == 0 && refs == 1); /* producer drops ownership */
    CHECK_EQ(receive_vb(&sink, &received, &held, 0), 0);
    CHECK(held == source_handle && received.pu8Addr == source_data);
    S32 fd = -1;
    CHECK(VB_GetDmaBufFd(held, &fd) == 0 && fd == source_fd);
    CHECK(received.u64PTS == 123 && received.ulPrivate == 99 && received.u32Size == sizeof(source_data));
    CHECK(allocations == 0 && refs == 1 && read_starts == 0 && read_ends == 0);
    CHECK(SYS_UnBind(&source, &sink) == 0 && refs == 1);
    CHECK_EQ(dma_sync_buf(fd, DMA_SYNC_READ | DMA_SYNC_START), 0);
    CHECK_EQ(memcmp(received.pu8Addr, source_data, sizeof(source_data)), 0);
    CHECK_EQ(dma_sync_buf(fd, DMA_SYNC_READ | DMA_SYNC_END), 0);
    CHECK(VB_ReleaseBuffer(held) == 0 && refs == 0 && read_ends == 1);
    CHECK_GE(fcntl(source_fd, F_GETFD), 0); /* SYS must never close the source fd */
    puts("[PASS] original storage/fd retained without allocation, received ref survives unbind");
}

static void test_fanout_and_full(void) {
    StreamBufferInfo sent = packet(), received = {0};
    UL held = 0;
    refs = 1;
    CHECK(SYS_Bind(&source, &sink) == 0 && SYS_Bind(&source, &sink2) == 0);
    for (U32 i = 0; i < MPP_STREAM_CHAN_DEPTH; ++i)
        CHECK_EQ(SYS_SendStream(&source, &sent), 0);
    int expected = 1 + 2 * MPP_STREAM_CHAN_DEPTH;
    CHECK(refs == expected);
    CHECK(SYS_SendStream(&source, &sent) == SYS_ERR_FULL && refs == expected);
    CHECK_EQ(receive_vb(&sink, &received, &held, 0), 0);
    CHECK(VB_ReleaseBuffer(held) == 0 && refs == expected - 1);
    CHECK(SYS_UnBind(&source, &sink) == 0 && refs == 1 + MPP_STREAM_CHAN_DEPTH);
    CHECK(SYS_UnBind(&source, &sink2) == 0 && refs == 1);
    CHECK(VB_ReleaseBuffer(source_handle) == 0 && refs == 0 && allocations == 0);
    puts("[PASS] per-sink ownership, queue-full rollback, queued refs drained on unbind");
}

static void test_copy_and_errors(void) {
    StreamBufferInfo sent = packet(), received = {0};
    CHECK_EQ(SYS_Bind(&source, &sink), 0);
    refs = 1;
    sent.ulVbHandle = 0;
    CHECK(SYS_SendStream(&source, &sent) == SYS_ERR_INVAL);
    CHECK(allocations == 0 && refs == 1);
    sent = packet();
    sent.u32Size = sizeof(source_data) + 1;
    CHECK(SYS_SendStream(&source, &sent) == SYS_ERR_INVAL && refs == 1);
    sent = packet();
    fail_ref = MPP_TRUE;
    CHECK(SYS_SendStream(&source, &sent) == SYS_ERR_INVAL && refs == 1);
    fail_ref = MPP_FALSE;
    CHECK(SYS_SendStream(&source, &sent) == 0 && refs == 2);
    fail_map = MPP_TRUE;
    received.ulVbHandle = 999;
    CHECK(SYS_RecvStream(&sink, &received, 0) == SYS_ERR_BUSY);
    CHECK(received.ulVbHandle == 0 && refs == 2);
    fail_map = MPP_FALSE;
    CHECK(SYS_RecvStream(&sink, &received, 0) == 0 && refs == 2);
    CHECK(received.pu8Addr == source_data && received.ulVbHandle == source_handle);
    CHECK_EQ(VB_ReleaseBuffer(received.ulVbHandle), 0);
    CHECK(SYS_UnBind(&source, &sink) == 0 && VB_ReleaseBuffer(source_handle) == 0);
    CHECK(refs == 0 && allocations == 0);
    puts("[PASS] pointer-only payloads rejected; invalid refs/maps preserve ownership");
}

static void test_eos_and_cleanup(void) {
    StreamBufferInfo sent = {.bEndOfStream = MPP_TRUE, .u64PTS = 124}, received = {0};
    CHECK_EQ(SYS_Bind(&source, &sink), 0);
    CHECK_EQ(SYS_SendStream(&source, &sent), 0);
    CHECK_EQ(SYS_RecvStream(&sink, &received, 0), 0);
    CHECK(received.bEndOfStream && !received.ulVbHandle && !received.pu8Addr && !received.u32Size);
    CHECK(received.u64PTS == 124 && allocations == 0);
    CHECK_EQ(SYS_UnBind(&source, &sink), 0);
    refs = 1;
    sent = packet();
    CHECK_EQ(SYS_Bind(&source, &sink), 0);
    CHECK_EQ(SYS_SendStream(&source, &sent), 0);
    CHECK(VB_ReleaseBuffer(source_handle) == 0 && refs == 1);
    g_sys_init_ref = 1;
    CHECK_EQ(SYS_Exit(), 0);
    CHECK(refs == 0 && shared.bind_cnt == 0);
    puts("[PASS] empty EOS and final queue cleanup");
}

int main(void) {
    shared.sys_inited = 1;
    CHECK_EQ(pthread_mutex_init(&shared.map_lock, NULL), 0);
    CHECK_EQ(pthread_rwlock_init(&shared.bind_lock, NULL), 0);
    for (U32 i = 0; i < MPP_MAX_BIND; ++i) {
        CHECK_EQ(pthread_mutex_init(&shared.queues[i].lock, NULL), 0);
        CHECK_EQ(pthread_mutex_init(&shared.stream_queues[i].lock, NULL), 0);
        CHECK_EQ(pthread_cond_init(&shared.stream_queues[i].not_empty, NULL), 0);
        CHECK_EQ(pthread_cond_init(&shared.stream_queues[i].not_full, NULL), 0);
    }
    source_fd = memfd_create("original-vb-dmabuf", MFD_CLOEXEC);
    CHECK_GE(source_fd, 0);
    test_borrow_and_unbind();
    test_fanout_and_full();
    test_copy_and_errors();
    test_eos_and_cleanup();
    CHECK(read_starts == read_ends);
    CHECK_EQ(close(source_fd), 0);
    return 0;
}
