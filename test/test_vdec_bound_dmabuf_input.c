/*
 * Hardware-only end-to-end test for SYS fixed DMA-BUF input -> Linlon VDEC.
 *
 * A JPEG is copied once into a preallocated CMA slot, VDEC queues that slot
 * with V4L2_MEMORY_DMABUF, and the test waits for one decoded VB-backed frame.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sys/sys_api.h"
#include "sys/vb_api.h"
#include "vdec/vdec_api.h"

#define JPEG_WIDTH 1920U
#define JPEG_HEIGHT 1080U
/*
 * The decoder advertises a 2,076,672-byte VIDEO_OUTPUT buffer for this
 * 1920x1080 JPEG profile, although the encoded sample itself is much smaller.
 * A DMA-BUF queued to V4L2 must cover that advertised buffer length.
 */
#define INPUT_SLOT_SIZE (3U * 1024U * 1024U)
#define INPUT_SLOT_COUNT 12U

static int read_file(const char *path, U8 **data, U32 *size) {
    FILE *file = fopen(path, "rb");
    long length;

    if (!file) {
        fprintf(stderr, "open %s failed\n", path);
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 || length > (long)INPUT_SLOT_SIZE ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        fprintf(stderr, "invalid JPEG size\n");
        return -1;
    }
    *data = malloc((size_t)length);
    if (!*data || fread(*data, 1, (size_t)length, file) != (size_t)length) {
        fclose(file);
        free(*data);
        *data = NULL;
        return -1;
    }
    fclose(file);
    *size = (U32)length;
    return 0;
}

int main(int argc, char **argv) {
    const char *jpeg_path = argc > 1 ? argv[1] : "test/assets/1920x1080.jpg";
    /* Exercise stop while the event thread waits with no input, or while
     * compressed packets are still queued. No sleep is used to hide races. */
    const char *mode = argc > 2 ? argv[2] : "decode";
    const MppNode source = {.eModId = MPP_ID_DEMUX, .s32DevId = 17, .s32ChnId = 0};
    const MppNode sink = {.eModId = MPP_ID_VDEC, .s32DevId = 0, .s32ChnId = 17};
    U8 *jpeg = NULL;
    U32 jpeg_size = 0;
    StreamBufferInfo packet;
    VdecChnAttr attr;
    VideoFrameInfo frame;
    S32 ret;
    int result = 1;
    BOOL sys_ready = MPP_FALSE;
    BOOL vb_ready = MPP_FALSE;
    BOOL vdec_ready = MPP_FALSE;
    BOOL channel_created = MPP_FALSE;
    BOOL channel_enabled = MPP_FALSE;
    BOOL bound = MPP_FALSE;

    if (argc > 3 || (strcmp(mode, "decode") && strcmp(mode, "empty") && strcmp(mode, "queued") &&
        strcmp(mode, "eos")))
        return 2;
    if (read_file(jpeg_path, &jpeg, &jpeg_size) != 0) {
        return 2;
    }
    if ((ret = SYS_Init()) != SYS_ERR_OK) {
        fprintf(stderr, "SYS_Init: %d\n", ret);
        goto done;
    }
    sys_ready = MPP_TRUE;
    if ((ret = VB_Init()) != 0) {
        fprintf(stderr, "VB_Init: %d\n", ret);
        goto done;
    }
    vb_ready = MPP_TRUE;
    if ((ret = VDEC_Init()) != 0) {
        fprintf(stderr, "VB/VDEC init: %d\n", ret);
        goto done;
    }
    vdec_ready = MPP_TRUE;

    memset(&attr, 0, sizeof(attr));
    attr.eCodecType = MPP_STREAM_CODEC_MJPEG;
    attr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    attr.u32Align = 16;
    attr.u32Width = JPEG_WIDTH;
    attr.u32Height = JPEG_HEIGHT;
    attr.bEnableInputDmaBuf = MPP_TRUE;
    if ((ret = VDEC_CreateChn(17, &attr)) != 0) {
        fprintf(stderr, "VDEC_CreateChn: %d\n", ret);
        goto done;
    }
    channel_created = MPP_TRUE;
    if ((ret = VDEC_EnableChn(17)) != 0) {
        fprintf(stderr, "VDEC_EnableChn: %d\n", ret);
        goto done;
    }
    channel_enabled = MPP_TRUE;
    if ((ret = SYS_Bind(&source, &sink)) != SYS_ERR_OK) {
        fprintf(stderr, "SYS_Bind: %d\n", ret);
        goto done;
    }
    bound = MPP_TRUE;
    if ((ret = SYS_ConfigStreamDmaBufPool(&source, &sink, INPUT_SLOT_SIZE, INPUT_SLOT_COUNT)) != SYS_ERR_OK) {
        fprintf(stderr, "bind/config: %d\n", ret);
        goto done;
    }
    if (!strcmp(mode, "empty")) {
        result = 0;
        goto done;
    }

    memset(&packet, 0, sizeof(packet));
    packet.pu8Addr = jpeg;
    packet.u32Size = jpeg_size;
    packet.bKeyFrame = MPP_TRUE;
    packet.eCodecType = MPP_STREAM_CODEC_MJPEG;
    packet.u64PTS = 1;
    packet.s32DmaBufFd = -1;
    if ((ret = SYS_SendStream(&source, &packet)) != SYS_ERR_OK) {
        fprintf(stderr, "SYS_SendStream: %d\n", ret);
        goto done;
    }
    if (!strcmp(mode, "queued")) {
        for (U32 i = 1; i < 4; ++i) {
            packet.u64PTS = i + 1;
            if ((ret = SYS_SendStream(&source, &packet)) != SYS_ERR_OK)
                goto done;
        }
        result = 0;
        goto done;
    }

    memset(&frame, 0, sizeof(frame));
    if ((ret = VDEC_GetFrame(17, &frame, 5000)) != 0) {
        fprintf(stderr, "VDEC_GetFrame: %d\n", ret);
        goto done;
    }
    if (frame.ulBufferId == 0 || frame.stVFrame.u32PlaneSizeValid[0] == 0) {
        fprintf(stderr, "decoded frame is empty\n");
        (void)VDEC_ReleaseFrame(17, frame.ulBufferId);
        goto done;
    }
    if (VDEC_ReleaseFrame(17, frame.ulBufferId) != 0)
        goto done;
    if (!strcmp(mode, "eos")) {
        packet.pu8Addr = NULL;
        packet.u32Size = 0;
        packet.u64PTS = 2;
        packet.bEndOfStream = MPP_TRUE;
        if ((ret = SYS_SendStream(&source, &packet)) != SYS_ERR_OK ||
            (ret = VDEC_GetFrame(17, &frame, 5000)) != ERR_VDEC_EOS) {
            fprintf(stderr, "EOS submission/drain failed: %d\n", ret);
            goto done;
        }
    }
    result = 0;

done:
    /* VDEC stream-off returns every leased input slot before SYS_UnBind. */
    if (channel_enabled && VDEC_DisableChn(17) != 0)
        result = 1;
    if (channel_created && VDEC_DestroyChn(17) != 0)
        result = 1;
    if (bound && SYS_UnBind(&source, &sink) != 0)
        result = 1;
    if (vdec_ready)
        (void)VDEC_Exit();
    if (vb_ready)
        (void)VB_Exit();
    if (sys_ready)
        (void)SYS_Exit();
    free(jpeg);
    if (result == 0)
        printf("[PASS] SYS DMA-BUF -> VDEC %s and cleanup %ux%u\n", mode, JPEG_WIDTH, JPEG_HEIGHT);
    return result;
}
