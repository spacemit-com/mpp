/*
 * K3 hardware regression for VDEC input backpressure. This test intentionally
 * submits MJPEG packets faster than the decoder can consume them. It is built
 * with the test suite but is not registered with CTest because it requires the
 * Linlon V4L2 decoder device.
 */

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sys/sys_api.h"
#include "sys/vb_api.h"
#include "vdec/vdec_api.h"

typedef struct _Consumer {
    S32 chn;
    atomic_bool stop;
    U32 frames;
    U32 ptsErrors;
    U64 nextPts;
    S32 error;
} Consumer;

static U8 *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    long length;
    U8 *data;

    if (!file)
        return NULL;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    data = (U8 *)malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static BOOL next_jpeg(const U8 *data, size_t size, size_t *cursor, const U8 **frame, U32 *frameSize) {
    size_t start = *cursor;
    size_t end;

    while (start + 1 < size && !(data[start] == 0xff && data[start + 1] == 0xd8))
        start++;
    if (start + 1 >= size)
        return MPP_FALSE;
    end = start + 2;
    while (end + 1 < size && !(data[end] == 0xff && data[end + 1] == 0xd9))
        end++;
    if (end + 1 >= size || end + 2 - start > UINT32_MAX)
        return MPP_FALSE;

    *frame = data + start;
    *frameSize = (U32)(end + 2 - start);
    *cursor = end + 2;
    return MPP_TRUE;
}

static void *consume_frames(void *opaque) {
    Consumer *consumer = (Consumer *)opaque;

    while (!atomic_load(&consumer->stop)) {
        VideoFrameInfo frame;
        S32 ret;
        memset(&frame, 0, sizeof(frame));
        ret = VDEC_GetFrame(consumer->chn, &frame, 100);
        if (ret == ERR_VDEC_NO_FRAME || ret == ERR_VDEC_TIMEOUT)
            continue;
        if (ret == ERR_VDEC_EOS)
            break;
        if (ret != ERR_VDEC_OK) {
            consumer->error = ret;
            break;
        }

        if (frame.stVFrame.u64PTS != consumer->nextPts)
            consumer->ptsErrors++;
        consumer->nextPts = frame.stVFrame.u64PTS + 1;
        consumer->frames++;
        ret = VDEC_ReleaseFrame(consumer->chn, frame.ulBufferId);
        if (ret != ERR_VDEC_OK) {
            consumer->error = ret;
            break;
        }
    }
    return NULL;
}

static S32 send_eos(S32 chn, const MppNode *source, U64 pts) {
    U8 dummy = 0;
    StreamBufferInfo stream;
    memset(&stream, 0, sizeof(stream));
    stream.pu8Addr = &dummy;
    stream.eCodecType = MPP_STREAM_CODEC_MJPEG;
    stream.bEndOfStream = MPP_TRUE;
    stream.u64PTS = pts;
    if (source) {
        S32 ret;
        do {
            ret = SYS_SendStream(source, &stream);
            if (ret == SYS_ERR_FULL)
                usleep(2000);
        } while (ret == SYS_ERR_FULL);
        return ret;
    }
    return VDEC_SendStream(chn, &stream, 3000);
}

int main(int argc, char **argv) {
    const char *path;
    U32 width;
    U32 height;
    U32 repeats = 20;
    BOOL bindMode = MPP_FALSE;
    U8 *data = NULL;
    size_t size = 0;
    VdecChnAttr attr;
    Consumer consumer;
    pthread_t consumerThread;
    BOOL consumerStarted = MPP_FALSE;
    U64 pts = 0;
    U32 busyCount = 0;
    U32 timeoutCount = 0;
    MppNode source = {.eModId = MPP_ID_DEMUX, .s32DevId = 0, .s32ChnId = 0};
    MppNode sink = {.eModId = MPP_ID_VDEC, .s32DevId = 0, .s32ChnId = 0};
    BOOL bound = MPP_FALSE;
    S32 ret = 1;

    if (argc < 4 || argc > 6) {
        fprintf(stderr, "usage: %s input.mjpeg width height [repeats] [manual|bind]\n", argv[0]);
        return 2;
    }
    path = argv[1];
    width = (U32)strtoul(argv[2], NULL, 10);
    height = (U32)strtoul(argv[3], NULL, 10);
    if (argc == 5)
        repeats = (U32)strtoul(argv[4], NULL, 10);
    if (argc == 6) {
        repeats = (U32)strtoul(argv[4], NULL, 10);
        if (strcmp(argv[5], "bind") == 0)
            bindMode = MPP_TRUE;
        else if (strcmp(argv[5], "manual") != 0)
            return 2;
    }
    if (width == 0 || height == 0 || repeats == 0)
        return 2;

    data = read_file(path, &size);
    if (!data) {
        fprintf(stderr, "failed to read %s: %s\n", path, strerror(errno));
        return 1;
    }

    if (SYS_Init() != 0 || VB_Init() != 0 || VDEC_Init() != 0) {
        fprintf(stderr, "MPP initialization failed\n");
        goto done;
    }
    memset(&attr, 0, sizeof(attr));
    attr.eCodecType = MPP_STREAM_CODEC_MJPEG;
    attr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    attr.u32Width = width;
    attr.u32Height = height;
    if (VDEC_CreateChn(0, &attr) != 0 || VDEC_EnableChn(0) != 0) {
        fprintf(stderr, "VDEC channel initialization failed\n");
        goto runtime_down;
    }
    if (bindMode) {
        if (SYS_Bind(&source, &sink) != SYS_ERR_OK) {
            fprintf(stderr, "SYS_Bind failed\n");
            goto channel_down;
        }
        bound = MPP_TRUE;
    }

    memset(&consumer, 0, sizeof(consumer));
    consumer.chn = 0;
    atomic_init(&consumer.stop, MPP_FALSE);
    if (pthread_create(&consumerThread, NULL, consume_frames, &consumer) != 0) {
        fprintf(stderr, "failed to create consumer thread\n");
        goto channel_down;
    }
    consumerStarted = MPP_TRUE;

    for (U32 pass = 0; pass < repeats; ++pass) {
        size_t cursor = 0;
        const U8 *frame;
        U32 frameSize;
        while (next_jpeg(data, size, &cursor, &frame, &frameSize)) {
            StreamBufferInfo stream;
            memset(&stream, 0, sizeof(stream));
            stream.pu8Addr = (U8 *)frame;
            stream.u32Size = frameSize;
            stream.eCodecType = MPP_STREAM_CODEC_MJPEG;
            stream.u64PTS = pts;
            stream.u32Width = width;
            stream.u32Height = height;

            if (bindMode) {
                do {
                    ret = SYS_SendStream(&source, &stream);
                    if (ret == SYS_ERR_FULL) {
                        busyCount++;
                        usleep(2000);
                    }
                } while (ret == SYS_ERR_FULL);
            } else {
                ret = VDEC_SendStream(0, &stream, 0);
                if (ret == ERR_VDEC_BUSY) {
                    busyCount++;
                    ret = VDEC_SendStream(0, &stream, 1);
                    if (ret == ERR_VDEC_TIMEOUT) {
                        timeoutCount++;
                        ret = VDEC_SendStream(0, &stream, 3000);
                    }
                }
            }
            if (ret != 0) {
                fprintf(stderr, "send failed pts=%lu ret=%d\n", (unsigned long)pts, ret);
                goto stop_consumer;
            }
            pts++;
        }
    }

    ret = send_eos(0, bindMode ? &source : NULL, pts);
    if (ret != ERR_VDEC_OK && ret != ERR_VDEC_EOS) {
        fprintf(stderr, "EOS send failed: %d\n", ret);
        goto stop_consumer;
    }
    pthread_join(consumerThread, NULL);
    consumerStarted = MPP_FALSE;

    printf("mode=%s submitted=%lu decoded=%u busy=%u timeouts=%u pts_errors=%u consumer_error=%d\n",
        bindMode ? "bind" : "manual", (unsigned long)pts, consumer.frames, busyCount,
        timeoutCount, consumer.ptsErrors, consumer.error);
    if (pts == 0 || busyCount == 0 || (!bindMode && timeoutCount == 0) || consumer.frames != pts ||
        consumer.ptsErrors != 0 || consumer.error != 0) {
        fprintf(stderr, "[FAIL] backpressure contract\n");
        ret = 1;
    } else {
        printf("[PASS] backpressure contract\n");
        ret = 0;
    }
    goto channel_down;

stop_consumer:
    atomic_store(&consumer.stop, MPP_TRUE);
    if (consumerStarted) {
        pthread_join(consumerThread, NULL);
        consumerStarted = MPP_FALSE;
    }
channel_down:
    if (bound)
        (void)SYS_UnBind(&source, &sink);
    (void)VDEC_DisableChn(0);
    (void)VDEC_DestroyChn(0);
runtime_down:
    (void)VDEC_Exit();
    (void)VB_Exit();
    (void)SYS_Exit();
done:
    free(data);
    return ret;
}
