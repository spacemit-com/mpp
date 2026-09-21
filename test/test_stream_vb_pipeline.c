/* Hardware regression: producer VB lifetime, bound MUX, and DEMUX -> VDEC.
 * Usage: test_stream_vb_pipeline encode output-prefix
 *        test_stream_vb_pipeline demux input-h264.mp4
 * Uses the existing VENC test's synthetic NV12 source and manual API checks. */
#define main venc_test_cli_main
#include "venc/test_venc.c"
#undef main
#include <dlfcn.h>
#include <stdatomic.h>
#include "demux/demux_api.h"
#include "mux/mux_api.h"
#include "vdec/vdec_api.h"
#include "sys/mpp_shm.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
#define CHECK_EQ(a, b) do { if ((a) != (b)) { fprintf(stderr, "FAIL %d: %s == %s\n", __LINE__, #a, #b); exit(1); } } while (0)
#define FRAME_COUNT 40
static atomic_uint mux_packets, demux_packets, venc_packets;

/* Test-only plugin selection, leaving installed plugins untouched. */
void *dlopen(const char *path, int flags) {
    static void *(*real_open)(const char *, int);
    if (!real_open) real_open = dlsym(RTLD_NEXT, "dlopen");
    const char *override = getenv("MPP_TEST_CODEC_PLUGIN");
    if (path && override && strstr(path, "libv4l2_linlonv5v7_codec2.so")) path = override;
    return real_open(path, flags);
}

S32 SYS_SendStream(const MppNode *node, const StreamBufferInfo *stream) {
    static S32 (*send)(const MppNode *, const StreamBufferInfo *);
    if (!send) send = dlsym(RTLD_NEXT, "SYS_SendStream");
    CHECK(send && (!stream->u32Size || stream->ulVbHandle));
    S32 ret = send(node, stream);
    if (!ret && stream->u32Size) {
        if (node->eModId == MPP_ID_VENC) atomic_fetch_add(&venc_packets, 1);
        if (node->eModId == MPP_ID_DEMUX) atomic_fetch_add(&demux_packets, 1);
    }
    return ret;
}

S32 MUX_SendPacket(S32 chn, const MuxPacket *packet) {
    static S32 (*send)(S32, const MuxPacket *);
    if (!send) send = dlsym(RTLD_NEXT, "MUX_SendPacket");
    CHECK(send && packet->pu8Data && packet->u32Size);
    S32 ret = send(chn, packet);
    if (!ret) atomic_fetch_add(&mux_packets, 1);
    return ret;
}

static U64 now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

static void send_raw(SynthSource *src, U32 index) {
    VideoFrameInfo frame;
    UL handle = 0;
    CHECK_EQ(synth_fill_frame(src, index, &frame, &handle), 0);
    frame.stVFrame.u64PTS = index * 33333ULL;
    S32 ret;
    U64 deadline = now_ms() + 3000;
    do {
        ret = VENC_SendFrame(0, &frame, 100);
        if (ret) usleep(2000);
    } while (ret && now_ms() < deadline);
    CHECK(VB_ReleaseBuffer(handle) == 0 && ret == 0);
}

static void encode(const char *prefix) {
    VencCaseCfg cfg;
    VencChnAttr attr;
    venc_case_defaults(&cfg, MPP_STREAM_CODEC_H264, 640, 480);
    fill_chn_attr(&attr, &cfg);
    char path[512];
    snprintf(path, sizeof(path), "%s-manual.h264", prefix);
    FILE *file = fopen(path, "wb");
    U32 count = 0, keys = 0;
    CHECK(file);
    CHECK_EQ(encode_synth(0, &cfg, FRAME_COUNT, file, NULL, &count, &keys), 0);
    fclose(file);
    CHECK(count == FRAME_COUNT && keys && mpp_shm_get()->pool_cnt == 0);
    puts("PASS VENC manual GetStream/ReleaseStream frames=40 pools=0");

    SynthSource src;
    CHECK_EQ(synth_source_create(&src, 640, 480, 16), 0);
    CHECK(VENC_CreateChn(0, &attr) == 0 && VENC_EnableChn(0) == 0);
    for (U32 i = 0; i < 4; ++i) send_raw(&src, i);
    StreamBufferInfo held = {0};
    CHECK(VENC_GetStream(0, &held, 3000) == 0 && held.ulVbHandle && held.pu8Addr);
    CHECK_EQ(VENC_DisableChn(0), 0);
    CHECK_EQ(VENC_DestroyChn(0), ERR_VENC_BUSY);
    CHECK(VENC_ReleaseStream(0, &held) == 0 && VENC_DestroyChn(0) == 0);
    CHECK(VB_DestroyPool(src.ulPool) == 0 && mpp_shm_get()->pool_cnt == 0);
    puts("PASS VENC retained packet survives stop; destroy waits for release");

    MuxChnAttr mux = {.eOutputType = MUX_OUTPUT_FILE};
    mux.stStreamAttr = (MuxStreamAttr){.eCodecType = MUX_CODEC_H264, .u32Width = 640,
        .u32Height = 480, .u32Fps = 30};
    mux.stSegment.eFileFormat = MUX_FILE_FMP4;
    snprintf(mux.stSegment.szPattern, sizeof(mux.stSegment.szPattern), "%s-bound.mp4", prefix);
    CHECK(MUX_CreateChn(0, &mux) == 0 && MUX_StartChn(0) == 0);
    MppNode source = {MPP_ID_VENC, 0, 0}, sink = {MPP_ID_MUX, 0, 0};
    CHECK_EQ(SYS_Bind(&source, &sink), 0);
    CHECK_EQ(synth_source_create(&src, 640, 480, 16), 0);
    CHECK(VENC_CreateChn(0, &attr) == 0 && VENC_EnableChn(0) == 0);
    for (U32 i = 0; i < FRAME_COUNT; ++i) {
        send_raw(&src, i);
        usleep(10000);
    }
    U64 deadline = now_ms() + 5000;
    while (atomic_load(&mux_packets) < FRAME_COUNT && now_ms() < deadline) usleep(10000);
    CHECK(atomic_load(&mux_packets) == FRAME_COUNT && atomic_load(&venc_packets) == FRAME_COUNT);
    CHECK(VENC_DisableChn(0) == 0 && MUX_StopChn(0) == 0);
    CHECK_EQ(SYS_UnBind(&source, &sink), 0);
    CHECK(VENC_DestroyChn(0) == 0 && MUX_DestroyChn(0) == 0);
    CHECK(VB_DestroyPool(src.ulPool) == 0 && mpp_shm_get()->pool_cnt == 0);
    puts("PASS VENC->SYS->MUX frames=40 pools=0");
}

static void demux(const char *path) {
    DemuxChnAttr attr = {.eInputType = DEMUX_INPUT_FILE, .bInjectPS = MPP_TRUE};
    snprintf(attr.szUrl, sizeof(attr.szUrl), "%s", path);
    VdecChnAttr dec = {.eCodecType = MPP_STREAM_CODEC_H264, .eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12,
        .u32Width = 640, .u32Height = 480, .u32BufCnt = 8, .u32Align = 16};
    MppNode source = {MPP_ID_DEMUX, 0, 0}, sink = {MPP_ID_VDEC, 0, 0};
    CHECK_EQ(DEMUX_CreateChn(0, &attr), 0);
    CHECK(VDEC_CreateChn(0, &dec) == 0 && VDEC_EnableChn(0) == 0);
    CHECK(SYS_Bind(&source, &sink) == 0 && DEMUX_StartChn(0) == 0);
    U32 count = 0;
    U64 last_pts = 0, deadline = now_ms() + 10000;
    S32 ret = 0;
    while (now_ms() < deadline) {
        VideoFrameInfo frame = {0};
        ret = VDEC_GetFrame(0, &frame, 100);
        if (ret == ERR_VDEC_EOS) break;
        if (ret == ERR_VDEC_TIMEOUT || ret == ERR_VDEC_NO_FRAME) continue;
        CHECK(ret == 0 && (!count || frame.stVFrame.u64PTS > last_pts));
        last_pts = frame.stVFrame.u64PTS;
        count++;
        CHECK_EQ(VDEC_ReleaseFrame(0, frame.ulBufferId), 0);
    }
    CHECK(ret == ERR_VDEC_EOS && count == FRAME_COUNT);
    CHECK(DEMUX_StopChn(0) == 0 && VDEC_DisableChn(0) == 0);
    CHECK_EQ(SYS_UnBind(&source, &sink), 0);
    CHECK(DEMUX_DestroyChn(0) == 0 && VDEC_DestroyChn(0) == 0 && mpp_shm_get()->pool_cnt == 0);
    puts("PASS DEMUX->SYS->VDEC h264 frames=40 EOS=PASS pools=0");

    /* A stopped sink leaves queue references outstanding until unbind. */
    sink.eModId = MPP_ID_MUX;
    U32 before = atomic_load(&demux_packets);
    CHECK(DEMUX_CreateChn(0, &attr) == 0 && SYS_Bind(&source, &sink) == 0);
    CHECK_EQ(DEMUX_StartChn(0), 0);
    deadline = now_ms() + 3000;
    while (atomic_load(&demux_packets) == before && now_ms() < deadline) usleep(1000);
    CHECK(atomic_load(&demux_packets) > before && DEMUX_StopChn(0) == 0);
    CHECK_EQ(DEMUX_DestroyChn(0), ERR_DEMUX_BUSY);
    CHECK(SYS_UnBind(&source, &sink) == 0 && DEMUX_DestroyChn(0) == 0);
    CHECK_EQ(mpp_shm_get()->pool_cnt, 0);
    puts("PASS DEMUX queued packets survive stop; unbind releases producer pool");
}

int main(int argc, char **argv) {
    CHECK_EQ(argc, 3);
    CHECK(SYS_Init() == 0 && VB_Init() == 0 && VENC_Init() == 0);
    CHECK(VDEC_Init() == 0 && DEMUX_Init() == 0 && MUX_Init() == 0);
    if (strcmp(argv[1], "encode") == 0) encode(argv[2]);
    else { CHECK_EQ(strcmp(argv[1], "demux"), 0); demux(argv[2]); }
    CHECK(MUX_Exit() == 0 && DEMUX_Exit() == 0 && VDEC_Exit() == 0 && VENC_Exit() == 0);
    CHECK(VB_Exit() == 0 && SYS_Exit() == 0);
    return 0;
}
