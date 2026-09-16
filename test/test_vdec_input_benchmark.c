/* Same-process SYS -> VDEC benchmark. No USB, demux, file I/O or display
 * is included in the measured interval. Use concatenated original JPEGs. */
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#include "sys/dma_alloc.h"
#include "sys/sys_api.h"
#include "sys/vb_api.h"
#include "vdec/vdec_api.h"

static atomic_ulong alloc_count;
static atomic_ullong alloc_ns;
static atomic_ullong sync_ns;
static S32 (*real_alloc)(U32, int *, U64 *, void **);
static S32 (*real_sync)(int, U32);
static U64 now_ns(void);

/* Interpose only the MPP allocator, identically for both test modes. */
S32 dma_alloc_buf(U32 size, int *fd, U64 *phy, void **ptr) {
    U64 begin = now_ns();
    S32 ret = real_alloc(size, fd, phy, ptr);
    atomic_fetch_add(&alloc_ns, now_ns() - begin);
    if (ret == 0)
        atomic_fetch_add(&alloc_count, 1);
    return ret;
}

S32 dma_sync_buf(int fd, U32 flags) {
    U64 begin = now_ns();
    S32 ret = real_sync(fd, flags);
    atomic_fetch_add(&sync_ns, now_ns() - begin);
    return ret;
}

typedef struct {
    const U8 *data;
    U32 size;
} Packet;
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    BOOL stop;
    U32 received;
    U32 pts_errors;
    U32 total;
    S32 error;
    U64 *sent_ns;
    double *latency_ms;
    U64 checksum;
    BOOL verify;
} Consumer;

static U64 now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (U64)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static double cpu_seconds(void) {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
           (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
}

static void wait_changed(Consumer *c) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_nsec += 100000000;
    if (ts.tv_nsec >= 1000000000) {
        ++ts.tv_sec;
        ts.tv_nsec -= 1000000000;
    }
    pthread_cond_timedwait(&c->changed, &c->lock, &ts);
}

static U64 hash_frame(const VideoFrameInfo *frame, U64 hash) {
    dma_sync_buf(frame->stVFrame.u32Fd[0], DMA_SYNC_READ | DMA_SYNC_START);
    /* Check visible NV12 bytes, excluding driver padding. */
    for (U32 plane = 0; plane < 2; ++plane) {
        const U8 *base = (const U8 *)frame->stVFrame.ulPlaneVirAddr[plane];
        U32 rows = frame->stVdecFrameInfo.stCommFrameInfo.u32Height / (plane ? 2 : 1);
        U32 cols = frame->stVdecFrameInfo.stCommFrameInfo.u32Width;
        for (U32 y = 0; y < rows; ++y)
            for (U32 x = 0; x < cols; ++x)
                hash = (hash ^ base[y * frame->stVFrame.u32PlaneStride[plane] + x]) * 1099511628211ULL;
    }
    dma_sync_buf(frame->stVFrame.u32Fd[0], DMA_SYNC_READ | DMA_SYNC_END);
    return hash;
}

static void *consume(void *arg) {
    Consumer *c = arg;
    for (;;) {
        pthread_mutex_lock(&c->lock);
        BOOL stop = c->stop;
        pthread_mutex_unlock(&c->lock);
        if (stop)
            break;
        VideoFrameInfo frame = {0};
        S32 ret = VDEC_GetFrame(17, &frame, 100);
        if (ret == ERR_VDEC_TIMEOUT || ret == ERR_VDEC_NO_FRAME)
            continue;
        if (ret != 0) {
            pthread_mutex_lock(&c->lock);
            c->error = ret;
            pthread_cond_signal(&c->changed);
            pthread_mutex_unlock(&c->lock);
            break;
        }
        U64 end = now_ns(), pts = frame.stVFrame.u64PTS;
        if (c->verify)
            c->checksum = hash_frame(&frame, c->checksum);
        ret = VDEC_ReleaseFrame(17, frame.ulBufferId);
        pthread_mutex_lock(&c->lock);
        if (pts != (U64)c->received + 1 || pts > c->total)
            ++c->pts_errors;
        if (pts > 0 && pts <= c->total)
            c->latency_ms[pts - 1] = (end - c->sent_ns[pts - 1]) / 1e6;
        ++c->received;
        if (ret)
            c->error = ret;
        pthread_cond_signal(&c->changed);
        pthread_mutex_unlock(&c->lock);
    }
    return NULL;
}

static U8 *load_packets(const char *path, Packet **packets, U32 *count) {
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long size = ftell(fp);
    if (size <= 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);
    U8 *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        free(data);
        return NULL;
    }
    fclose(fp);
    *packets = calloc(16384, sizeof(**packets));
    if (!*packets) {
        free(data);
        return NULL;
    }
    *count = 0;
    /* This fixture format is a concatenation of complete MJPEG frames. */
    for (size_t pos = 0; pos + 1 < (size_t)size;) {
        while (pos + 1 < (size_t)size && !(data[pos] == 0xff && data[pos + 1] == 0xd8))
            ++pos;
        size_t start = pos;
        pos += 2;
        while (pos + 1 < (size_t)size && !(data[pos] == 0xff && data[pos + 1] == 0xd9))
            ++pos;
        if (pos + 1 >= (size_t)size || *count == 16384)
            break;
        pos += 2;
        (*packets)[*count] = (Packet){data + start, (U32)(pos - start)};
        ++*count;
    }
    return data;
}

static int compare_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

int main(int argc, char **argv) {
    if (argc < 8 || argc > 10) {
        fprintf(stderr,
                "usage: %s input.mjpg width height legacy|dmabuf frames slots fps [verify] [window]\n",
                argv[0]);
        return 2;
    }
    U32 width = strtoul(argv[2], NULL, 10), height = strtoul(argv[3], NULL, 10);
    BOOL direct = strcmp(argv[4], "dmabuf") == 0;
    U32 measured = strtoul(argv[5], NULL, 10), slots = strtoul(argv[6], NULL, 10);
    double rate = strtod(argv[7], NULL);
    U32 window = argc > 9 ? strtoul(argv[9], NULL, 10) : 4;
    const U32 warmup = 32;
    if (!width || !height || !measured || !window || window > 6 || slots > 16 || !slots || rate < 0 ||
        (!direct && strcmp(argv[4], "legacy") != 0))
        return 2;
    real_alloc = dlsym(RTLD_NEXT, "dma_alloc_buf");
    real_sync = dlsym(RTLD_NEXT, "dma_sync_buf");
    if (!real_alloc || !real_sync)
        return 2;
    Packet *packets = NULL;
    U32 count = 0;
    U8 *data = load_packets(argv[1], &packets, &count);
    if (!data || !count) {
        fprintf(stderr, "no MJPEG packets\n");
        free(packets);
        free(data);
        return 2;
    }
    U32 max_size = 0;
    U64 bytes = 0;
    for (U32 i = 0; i < count; ++i) {
        if (packets[i].size > max_size)
            max_size = packets[i].size;
        bytes += packets[i].size;
    }
    printf("FIXTURE packets=%u mean_bytes=%.0f max_bytes=%u dimensions=%ux%u\n", count, (double)bytes / count,
           max_size, width, height);
    Consumer c = {.lock = PTHREAD_MUTEX_INITIALIZER,
                  .total = measured + warmup,
                  .verify = argc > 8 && atoi(argv[8]),
                  .checksum = 1469598103934665603ULL};
    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);
    pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
    pthread_cond_init(&c.changed, &cond_attr);
    pthread_condattr_destroy(&cond_attr);
    c.sent_ns = calloc(c.total, sizeof(*c.sent_ns));
    c.latency_ms = calloc(c.total, sizeof(*c.latency_ms));
    if (!c.sent_ns || !c.latency_ms)
        return 2;
    const MppNode source = {.eModId = MPP_ID_DEMUX, .s32DevId = 17};
    const MppNode sink = {.eModId = MPP_ID_VDEC, .s32ChnId = 17};
    BOOL sys = 0, vb = 0, dec = 0, created = 0, enabled = 0, bound = 0, started = 0;
    pthread_t thread;
    S32 ret = 1;
    if (SYS_Init() != 0)
        goto done;
    sys = 1;
    if (VB_Init() != 0)
        goto done;
    vb = 1;
    if (VDEC_Init() != 0)
        goto done;
    dec = 1;
    VdecChnAttr attr = {.eCodecType = MPP_STREAM_CODEC_MJPEG,
                        .eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12,
                        .u32Width = width,
                        .u32Height = height,
                        .u32Align = 16,
                        .u32BufCnt = 8,
                        .bEnableInputDmaBuf = direct};
    if (VDEC_CreateChn(17, &attr) != 0)
        goto done;
    created = 1;
    if (VDEC_EnableChn(17) != 0)
        goto done;
    enabled = 1;
    if (SYS_Bind(&source, &sink) != 0)
        goto done;
    bound = 1;
    /* Conservative fixed capacity for the driver; no resizing in the hot path. */
    U32 slot_size = ((width * height + 1048575U) / 1048576U + 1U) * 1048576U;
    if (slot_size < max_size)
        slot_size = (max_size + 4095U) & ~4095U;
    if (direct && SYS_ConfigStreamDmaBufPool(&source, &sink, slot_size, slots) != 0)
        goto done;
    if (pthread_create(&thread, NULL, consume, &c) != 0)
        goto done;
    started = 1;
    U64 begin = 0, phase_begin = now_ns(), deadline = phase_begin + 10000000000ULL;
    double cpu_begin = 0, submit_ms = 0;
    unsigned long alloc_begin = 0;
    U64 alloc_time_begin = 0, sync_time_begin = 0;
    U32 submitted = 0, busy = 0;
    for (U32 i = 0; i < c.total; ++i) {
        pthread_mutex_lock(&c.lock);
        while ((i - c.received >= window || (i == warmup && c.received != warmup)) && !c.error &&
               now_ns() < deadline)
            wait_changed(&c);
        if (c.error || now_ns() >= deadline) {
            pthread_mutex_unlock(&c.lock);
            break;
        }
        pthread_mutex_unlock(&c.lock);
        if (i == warmup) {
            printf("READY mode=%s warmup=%u slots=%u\n", argv[4], warmup, slots);
            fflush(stdout);
            begin = phase_begin = now_ns();
            cpu_begin = cpu_seconds();
            alloc_begin = atomic_load(&alloc_count);
            alloc_time_begin = atomic_load(&alloc_ns);
            sync_time_begin = atomic_load(&sync_ns);
        }
        if (rate > 0) {
            U64 target = phase_begin + (U64)((i < warmup ? i : i - warmup) * 1e9 / rate);
            struct timespec ts = {.tv_sec = target / 1000000000ULL, .tv_nsec = target % 1000000000ULL};
            while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) {
            }
        }
        Packet p = packets[i % count];
        StreamBufferInfo stream = {.pu8Addr = p.data,
                                   .u32Size = p.size,
                                   .eCodecType = MPP_STREAM_CODEC_MJPEG,
                                   .u64PTS = i + 1,
                                   .u32Width = width,
                                   .u32Height = height,
                                   .s32DmaBufFd = -1};
        U64 send_begin = now_ns();
        pthread_mutex_lock(&c.lock);
        c.sent_ns[i] = send_begin;
        pthread_mutex_unlock(&c.lock);
        do {
            ret = SYS_SendStream(&source, &stream);
            if (ret == SYS_ERR_FULL) {
                ++busy;
                pthread_mutex_lock(&c.lock);
                wait_changed(&c);
                pthread_mutex_unlock(&c.lock);
            }
        } while (ret == SYS_ERR_FULL && now_ns() < deadline);
        if (ret != 0)
            break;
        if (i >= warmup)
            submit_ms += (now_ns() - send_begin) / 1e6;
        ++submitted;
        deadline = now_ns() + 5000000000ULL;
    }
    pthread_mutex_lock(&c.lock);
    while (c.received < submitted && !c.error && now_ns() < deadline)
        wait_changed(&c);
    U64 end = now_ns();
    double cpu_used = cpu_seconds() - cpu_begin;
    c.stop = 1;
    pthread_mutex_unlock(&c.lock);
    pthread_join(thread, NULL);
    started = 0;
    ret = (submitted == c.total && c.received == submitted && !c.pts_errors && !c.error) ? 0 : 1;
    if (ret == 0 && begin) {
        qsort(c.latency_ms + warmup, measured, sizeof(double), compare_double);
        double seconds = (end - begin) / 1e9;
        printf("RESULT mode=%s measured=%u fps=%.3f cpu_pct=%.3f cpu_ms_frame=%.4f submit_ms_frame=%.4f "
               "latency_p50_ms=%.3f latency_p95_ms=%.3f hot_allocs=%lu total_allocs=%lu "
               "pts_errors=%u checksum=%016llx verify=%d rate=%.1f slots=%u alloc_ms_frame=%.4f "
               "sync_ms_frame=%.4f\n",
               argv[4], measured, measured / seconds, cpu_used / seconds * 100, cpu_used * 1000 / measured,
               submit_ms / measured, c.latency_ms[warmup + measured / 2],
               c.latency_ms[warmup + measured * 95 / 100], atomic_load(&alloc_count) - alloc_begin,
               atomic_load(&alloc_count), c.pts_errors, (unsigned long long)c.checksum, c.verify, rate, slots,
               (atomic_load(&alloc_ns) - alloc_time_begin) / 1e6 / measured,
               (atomic_load(&sync_ns) - sync_time_begin) / 1e6 / measured);
    } else {
        fprintf(stderr, "FAILED mode=%s submitted=%u decoded=%u pts_errors=%u error=%d slots=%u\n", argv[4],
                submitted, c.received, c.pts_errors, c.error, slots);
    }
done:
    if (started) {
        pthread_mutex_lock(&c.lock);
        c.stop = 1;
        pthread_mutex_unlock(&c.lock);
        pthread_join(thread, NULL);
    }
    if (enabled)
        VDEC_DisableChn(17);
    if (created)
        VDEC_DestroyChn(17);
    if (bound && SYS_UnBind(&source, &sink) != 0)
        ret = 1;
    if (dec)
        VDEC_Exit();
    if (vb)
        VB_Exit();
    if (sys)
        SYS_Exit();
    pthread_cond_destroy(&c.changed);
    pthread_mutex_destroy(&c.lock);
    free(c.sent_ns);
    free(c.latency_ms);
    free(packets);
    free(data);
    return ret;
}
