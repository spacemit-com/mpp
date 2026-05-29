/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      : stream_manager.c
 * @Brief     : Multi-stream manager implementation.
 *
 * Architecture:
 *   Each stream has independent: DEMUX(callback) → VDEC(thread) → VENC(thread) → MUX(thread)
 *   All streams share a single RTSP server port with different paths.
 *   Hardware codecs ensure low CPU usage even with multiple streams.
 *------------------------------------------------------------------------------
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "stream/mpp_stream_manager.h"
#include "stream/mpp_queue.h"
#include "demux/demux_api.h"
#include "mux/mux_api.h"
#include "sys/sys_api.h"
#include "sys/vb_api.h"
#include "vdec/vdec_api.h"
#include "venc/venc_api.h"

/* Queue capacities - balanced for latency vs stability.
 * IMPORTANT: queue2 must NOT use drop callback because encoder holds
 * dma-buf reference. Dropping a frame while encoder uses it causes
 * double-release. Use small capacity + blocking push instead. */
#define QUEUE1_CAPACITY 16 /* Packet queue: ~1.3s @ 12fps */
#define QUEUE2_CAPACITY 4  /* Frame queue: small but > 1 for smooth flow */
#define QUEUE3_CAPACITY 8  /* Stream queue: ~0.67s @ 12fps */

/* Convert DemuxCodecType to MppStreamCodecType (different enum values!) */
static MppStreamCodecType demux_codec_to_mpp(DemuxCodecType eType) {
    switch (eType) {
    case DEMUX_CODEC_H264:
        return MPP_STREAM_CODEC_H264;
    case DEMUX_CODEC_H265:
        return MPP_STREAM_CODEC_H265;
    case DEMUX_CODEC_MJPEG:
        return MPP_STREAM_CODEC_MJPEG;
    default:
        return MPP_STREAM_CODEC_H264;
    }
}

/* ======================== Internal Types ======================== */

/* Queue item types */
typedef struct PacketItem {
    U8 *pu8Data;
    U32 u32Size;
    U64 u64PTS;
    BOOL bKeyFrame;
    BOOL bEos;
} PacketItem;

typedef struct FrameItem {
    UL ulBufferId;
    U32 u32Width;
    U32 u32Height;
    U64 u64PTS;
    U32 u32Fd[3];
    UL ulPlaneVirAddr[3];
    U32 u32PlaneStride[3];
    U32 u32PlaneNum;
    S32 s32VdecChn;
    BOOL bEos;
} FrameItem;

typedef struct StreamItem {
    U8 *pu8Data;
    U32 u32Size;
    U64 u64PTS;
    BOOL bKeyFrame;
    BOOL bEos;
} StreamItem;

/* Single stream pipeline */
typedef struct StreamPipeline {
    /* Configuration */
    S32 id;
    StreamConfig config;
    StreamState state;
    BOOL configured;

    /* Channels */
    S32 demuxChn;
    S32 vdecChn;
    S32 vencChn;
    S32 muxChn;

    /* Queues */
    MppQueue queue1;
    MppQueue queue2;
    MppQueue queue3;

    /* Threads */
    pthread_t vdecThread;
    pthread_t vencThread;
    pthread_t muxThread;
    BOOL vdecThreadCreated;
    BOOL vencThreadCreated;
    BOOL muxThreadCreated;

    /* Control */
    volatile BOOL bRunning;
    volatile BOOL bStopRequested;

    /* Detected stream info */
    U32 inputWidth;
    U32 inputHeight;
    U32 inputFps;
    MppStreamCodecType inputCodec;

    /* Statistics */
    U64 demuxCount;
    U64 vdecCount;
    U64 vencCount;
    U64 muxCount;
    U64 dropCount;
    U64 errorCount;

    /* Per-stream FrameItem pool (isolated from other streams) */
    #define PER_STREAM_POOL_SIZE 16
    FrameItem       framePool[16];  /* Cannot use macro in array size here */
    BOOL            framePoolUsed[16];
    pthread_mutex_t framePoolLock;

    /* Back-reference */
    struct StreamManager *manager;
} StreamPipeline;

/* Stream manager */
struct StreamManager {
    StreamPipeline *pipelines;
    S32 maxStreams;
    S32 streamCount;
    U16 rtspPort;

    pthread_mutex_t lock;
    volatile BOOL bRunning;

    /* Monitor thread */
    pthread_t monitorThread;
    BOOL monitorThreadCreated;

    /* Event callback */
    StreamEventCallback eventCallback;
    void *eventUserData;

    /* Frame processing callback (YOLO, OSD, etc.) */
    StreamFrameCallback frameCallback;
    void *frameUserData;
    U64 frameIndex; /* Global frame counter */

    /* Global init flag */
    BOOL sysInitialized;
};

/* ======================== Utility ======================== */

#define LOG_STREAM(id, fmt, ...)                                                                                       \
    do {                                                                                                               \
        printf("[STREAM-%d] " fmt "\n", id, ##__VA_ARGS__);                                                            \
        fflush(stdout);                                                                                                \
    } while (0)

__attribute__((unused)) static U64 get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (U64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void fire_event(StreamManager *mgr, S32 id, StreamEvent event) {
    if (mgr && mgr->eventCallback) {
        mgr->eventCallback(id, event, mgr->eventUserData);
    }
}

/* ======================== Drop Callbacks ======================== */

static void drop_packet_cb(void *item, void *userData) {
    PacketItem *pkt = (PacketItem *)item;
    (void)userData;
    if (pkt) {
        if (pkt->pu8Data)
            free(pkt->pu8Data);
        free(pkt);
    }
}

__attribute__((unused)) static void drop_frame_cb(void *item, void *userData) {
    FrameItem *frame = (FrameItem *)item;
    if (frame) {
        if (frame->ulBufferId) {
            VDEC_ReleaseFrame(frame->s32VdecChn, frame->ulBufferId);
        }
        free(frame);
    }
    (void)userData;
}

static void drop_stream_cb(void *item, void *userData) {
    StreamItem *stream = (StreamItem *)item;
    (void)userData;
    if (stream) {
        if (stream->pu8Data)
            free(stream->pu8Data);
        free(stream);
    }
}

/* ======================== DEMUX Callback ======================== */

static S32 demux_callback(S32 s32ChnId, const DemuxPacket *pstPkt, VOID *pPriv) {
    StreamPipeline *p = (StreamPipeline *)pPriv;

    if (!p || p->bStopRequested || !pstPkt || !pstPkt->pu8Data || pstPkt->u32Size == 0) {
        return 0;
    }

    /* DIAG: Verify channel ID matches pipeline ID */
    if (s32ChnId != p->id) {
        static U64 mismatch_cnt = 0;
        if (mismatch_cnt++ % 100 == 0) {
            LOG_STREAM(p->id, "[DEMUX-MISMATCH] chnId=%d but p->id=%d", s32ChnId, p->id);
        }
    }

    /* Allocate and copy packet */
    PacketItem *item = (PacketItem *)malloc(sizeof(PacketItem));
    if (!item)
        return 0;

    item->pu8Data = (U8 *)malloc(pstPkt->u32Size);
    if (!item->pu8Data) {
        free(item);
        return 0;
    }

    memcpy(item->pu8Data, pstPkt->pu8Data, pstPkt->u32Size);
    item->u32Size = pstPkt->u32Size;
    item->u64PTS = pstPkt->u64PTS;
    item->bKeyFrame = pstPkt->bKeyFrame;
    item->bEos = MPP_FALSE;

    S32 ret = MppQueue_Push(&p->queue1, item, 0);
    if (ret < 0 && ret != MPP_QUEUE_DROPPED) {
        free(item->pu8Data);
        free(item);
        return 0;
    }
    if (ret == MPP_QUEUE_DROPPED) {
        p->dropCount++;
    }

    p->demuxCount++;
    (void)s32ChnId;
    return 0;
}

/* ======================== VDEC Thread ======================== */

/* Per-stream FrameItem pool functions (isolated pools prevent cross-stream corruption) */
static FrameItem *frame_pool_alloc(StreamPipeline *p) {
    pthread_mutex_lock(&p->framePoolLock);
    for (int i = 0; i < 16; i++) {
        if (!p->framePoolUsed[i]) {
            p->framePoolUsed[i] = MPP_TRUE;
            pthread_mutex_unlock(&p->framePoolLock);
            return &p->framePool[i];
        }
    }
    pthread_mutex_unlock(&p->framePoolLock);
    return NULL;
}

static void frame_pool_free(StreamPipeline *p, FrameItem *item) {
    if (!item || !p) return;
    pthread_mutex_lock(&p->framePoolLock);
    int idx = item - p->framePool;
    if (idx >= 0 && idx < 16) {
        p->framePoolUsed[idx] = MPP_FALSE;
    }
    pthread_mutex_unlock(&p->framePoolLock);
}

static void *vdec_thread_func(void *arg)
{
    StreamPipeline *p = (StreamPipeline *)arg;
    S32 ret;

    LOG_STREAM(p->id, "VDEC thread started");

    while (!p->bStopRequested) {
        PacketItem *pktItem = NULL;
        ret = MppQueue_Pop(&p->queue1, (void **)&pktItem, 100);
        if (ret != MPP_QUEUE_OK) continue;

        if (pktItem->bEos) {
            free(pktItem);
            FrameItem *eosFrame = (FrameItem *)calloc(1, sizeof(FrameItem));
            if (eosFrame) {
                eosFrame->bEos = MPP_TRUE;
                MppQueue_Push(&p->queue2, eosFrame, 100);
            }
            break;
        }

        /* Send to decoder */
        StreamBufferInfo stream = {0};
        stream.pu8Addr = pktItem->pu8Data;
        stream.u32Size = pktItem->u32Size;
        stream.u64PTS = pktItem->u64PTS;
        stream.bKeyFrame = pktItem->bKeyFrame;

        ret = VDEC_SendStream(p->vdecChn, &stream, 50);
        free(pktItem->pu8Data);
        free(pktItem);

        if (ret != 0 && ret != ERR_VDEC_BUSY) {
            p->errorCount++;
            continue;
        }

        /* Get decoded frames */
        while (1) {
            VideoFrameInfo decFrame;
            memset(&decFrame, 0, sizeof(decFrame));

            ret = VDEC_GetFrame(p->vdecChn, &decFrame, 0);
            if (ret != ERR_VDEC_OK)
                break;

            /* Use static pool instead of malloc to avoid heap corruption issues */
            FrameItem *frameItem = frame_pool_alloc(p);
            if (!frameItem) {
                VDEC_ReleaseFrame(p->vdecChn, decFrame.ulBufferId);
                continue;
            }

            frameItem->ulBufferId = decFrame.ulBufferId;
            frameItem->u32Width = decFrame.stVdecFrameInfo.stCommFrameInfo.u32Width;
            frameItem->u32Height = decFrame.stVdecFrameInfo.stCommFrameInfo.u32Height;
            frameItem->u64PTS = decFrame.stVFrame.u64PTS;
            frameItem->u32PlaneNum = decFrame.stVFrame.u32PlaneNum;
            frameItem->s32VdecChn = p->vdecChn;
            frameItem->bEos = MPP_FALSE;

            for (U32 i = 0; i < decFrame.stVFrame.u32PlaneNum && i < 3; i++) {
                frameItem->u32Fd[i] = decFrame.stVFrame.u32Fd[i];
                frameItem->ulPlaneVirAddr[i] = decFrame.stVFrame.ulPlaneVirAddr[i];
                frameItem->u32PlaneStride[i] = decFrame.stVFrame.u32PlaneStride[i];
            }

            /* Use blocking push (100ms) to wait for VENC to consume.
             * DO NOT use drop callback on queue2 - encoder may still hold
             * dma-buf reference. Only release buffer on actual timeout. */
            ret = MppQueue_Push(&p->queue2, frameItem, 100);
            if (ret != MPP_QUEUE_OK) {
                /* Queue full after 100ms - release buffer to prevent leak */
                VDEC_ReleaseFrame(p->vdecChn, decFrame.ulBufferId);
                frame_pool_free(p, frameItem);
                p->dropCount++;
                continue;
            }

            p->vdecCount++;
        }
    }

    LOG_STREAM(p->id, "VDEC thread exiting (count=%llu)", (uint64_t)p->vdecCount);
    return NULL;
}

/* ======================== VENC Thread ======================== */

/* Synchronous encode mode: send frame, wait for output, release input.
 * This ensures VDEC buffer is only released after encoder truly finished.
 * No pending queue needed - avoids FIFO mismatch with encoder internal order. */

static void *venc_thread_func(void *arg) {
    StreamPipeline *p = (StreamPipeline *)arg;
    S32 ret;

    /* Track the last sent frame's VDEC buffer for release after encode */
    S32 lastVdecChn = -1;
    UL lastBufferId = 0;
    BOOL lastFramePending = MPP_FALSE;

    LOG_STREAM(p->id, "VENC thread started");

    while (!p->bStopRequested) {
        FrameItem *frameItem = NULL;
        ret = MppQueue_Pop(&p->queue2, (void **)&frameItem, 100);

        if (ret != MPP_QUEUE_OK) {
            /* No new frame - must release pending buffer to avoid deadlock.
             * If we don't release, VDEC buffers are exhausted and pipeline stalls. */
            if (lastFramePending) {
                StreamBufferInfo encStream;
                memset(&encStream, 0, sizeof(encStream));

                /* Block wait for encoder output (200ms) - critical to avoid deadlock */
                ret = VENC_GetStream(p->vencChn, &encStream, 200);
                if (ret == ERR_VENC_OK) {
                    /* Release the pending VDEC buffer */
                    VDEC_ReleaseFrame(lastVdecChn, lastBufferId);
                    lastFramePending = MPP_FALSE;

                    /* Process encoded stream */
                    StreamItem *streamItem = (StreamItem *)malloc(sizeof(StreamItem));
                    if (streamItem) {
                        streamItem->pu8Data = (U8 *)malloc(encStream.u32Size);
                        if (streamItem->pu8Data) {
                            memcpy(streamItem->pu8Data, encStream.pu8Addr, encStream.u32Size);
                            streamItem->u32Size = encStream.u32Size;
                            streamItem->u64PTS = encStream.u64PTS;
                            streamItem->bKeyFrame = encStream.bKeyFrame;
                            streamItem->bEos = MPP_FALSE;
                            if (MppQueue_Push(&p->queue3, streamItem, 0) == MPP_QUEUE_OK) {
                                p->vencCount++;
                            } else {
                                free(streamItem->pu8Data);
                                free(streamItem);
                            }
                        } else {
                            free(streamItem);
                        }
                    }
                    VENC_ReleaseStream(p->vencChn, &encStream);
                } else {
                    /* Timeout - release buffer anyway to prevent deadlock */
                    VDEC_ReleaseFrame(lastVdecChn, lastBufferId);
                    lastFramePending = MPP_FALSE;
                    p->errorCount++;
                }
            }
            continue;
        }

        if (frameItem->bEos) {
            frame_pool_free(p, frameItem);
            /* Release last pending buffer before EOS */
            if (lastFramePending) {
                VDEC_ReleaseFrame(lastVdecChn, lastBufferId);
                lastFramePending = MPP_FALSE;
            }
            StreamItem *eosStream = (StreamItem *)calloc(1, sizeof(StreamItem));
            if (eosStream) {
                eosStream->bEos = MPP_TRUE;
                MppQueue_Push(&p->queue3, eosStream, 100);
            }
            break;
        }

        /* STEP 1: If there's a pending frame, wait for its output first */
        if (lastFramePending) {
            StreamBufferInfo encStream;
            memset(&encStream, 0, sizeof(encStream));

            /* Wait for encoder output (up to 200ms) */
            ret = VENC_GetStream(p->vencChn, &encStream, 200);
            if (ret == ERR_VENC_OK) {
                /* Release the pending VDEC buffer */
                VDEC_ReleaseFrame(lastVdecChn, lastBufferId);
                lastFramePending = MPP_FALSE;

                /* Process encoded stream */
                StreamItem *streamItem = (StreamItem *)malloc(sizeof(StreamItem));
                if (streamItem) {
                    streamItem->pu8Data = (U8 *)malloc(encStream.u32Size);
                    if (streamItem->pu8Data) {
                        memcpy(streamItem->pu8Data, encStream.pu8Addr, encStream.u32Size);
                        streamItem->u32Size = encStream.u32Size;
                        streamItem->u64PTS = encStream.u64PTS;
                        streamItem->bKeyFrame = encStream.bKeyFrame;
                        streamItem->bEos = MPP_FALSE;
                        if (MppQueue_Push(&p->queue3, streamItem, 0) == MPP_QUEUE_OK) {
                            p->vencCount++;
                        } else {
                            free(streamItem->pu8Data);
                            free(streamItem);
                            p->dropCount++;
                        }
                    } else {
                        free(streamItem);
                    }
                }
                VENC_ReleaseStream(p->vencChn, &encStream);
            } else {
                /* Timeout - release pending buffer anyway to avoid leak */
                VDEC_ReleaseFrame(lastVdecChn, lastBufferId);
                lastFramePending = MPP_FALSE;
                p->errorCount++;
            }
        }

        /* STEP 2: Validate frame and call callback */
        StreamManager *mgr = p->manager;

        /* Validate frameItem before using */
        if (frameItem->u32Width == 0 || frameItem->u32Width > 8192 || frameItem->u32Height == 0 ||
            frameItem->u32Height > 8192 || frameItem->u32Fd[0] == 0) {
            /* Corrupted frame - skip */
            VDEC_ReleaseFrame(frameItem->s32VdecChn, frameItem->ulBufferId);
            frame_pool_free(p, frameItem);
            p->errorCount++;
            continue;
        }

        /* Prepare frame info */
        StreamFrameInfo frameInfo = {0};
        frameInfo.streamId = p->id;
        frameInfo.frameIndex = mgr ? mgr->frameIndex++ : 0;
        frameInfo.width = frameItem->u32Width;
        frameInfo.height = frameItem->u32Height;
        frameInfo.pts = frameItem->u64PTS;
        frameInfo.dmaFd = frameItem->u32Fd[0];
        frameInfo.virAddr = (void *)frameItem->ulPlaneVirAddr[0];
        frameInfo.stride = frameItem->u32PlaneStride[0];
        frameInfo.bufferId = frameItem->ulBufferId;
        frameInfo.useOutputFrame = MPP_FALSE; /* Default: use original frame */

        /* Call frame callback if set (for YOLO inference, OSD, etc.) */
        if (mgr && mgr->frameCallback) {
            S32 cbRet = mgr->frameCallback(&frameInfo, mgr->frameUserData);
            if (cbRet != 0) {
                /* Callback requested to skip this frame */
                VDEC_ReleaseFrame(frameItem->s32VdecChn, frameItem->ulBufferId);
                frame_pool_free(p, frameItem);
                p->dropCount++;
                continue;
            }
        }

        /* STEP 3: Build and send frame to encoder */
        /* Use output frame if callback provided one (OSD blending) */
        VideoFrameInfo vencFrame;
        memset(&vencFrame, 0, sizeof(vencFrame));
        vencFrame.eFrameType = FRAME_TYPE_VENC;
        vencFrame.stVencFrameInfo.stCommFrameInfo.u32Width = frameItem->u32Width;
        vencFrame.stVencFrameInfo.stCommFrameInfo.u32Height = frameItem->u32Height;
        vencFrame.stVFrame.u64PTS = frameItem->u64PTS;
        vencFrame.stVFrame.u32PlaneNum = frameItem->u32PlaneNum;

        if (frameInfo.useOutputFrame && frameInfo.outDmaFd > 0) {
            /* Use OSD-blended output frame */
            vencFrame.ulBufferId = frameInfo.outBufferId;
            U32 stride = frameItem->u32PlaneStride[0];
            U32 y_size = stride * frameItem->u32Height;
            vencFrame.stVFrame.u32Fd[0] = frameInfo.outDmaFd;
            vencFrame.stVFrame.u32Fd[1] = frameInfo.outDmaFd;
            vencFrame.stVFrame.ulPlaneVirAddr[0] = (UL)frameInfo.outVirAddr;
            vencFrame.stVFrame.ulPlaneVirAddr[1] = (UL)frameInfo.outVirAddr + y_size;
            vencFrame.stVFrame.u32PlaneStride[0] = stride;
            vencFrame.stVFrame.u32PlaneStride[1] = stride;
        } else {
            /* Use original VDEC frame */
            vencFrame.ulBufferId = frameItem->ulBufferId;
            for (U32 i = 0; i < frameItem->u32PlaneNum && i < 3; i++) {
                vencFrame.stVFrame.u32Fd[i] = frameItem->u32Fd[i];
                vencFrame.stVFrame.ulPlaneVirAddr[i] = frameItem->ulPlaneVirAddr[i];
                vencFrame.stVFrame.u32PlaneStride[i] = frameItem->u32PlaneStride[i];
            }
        }

        ret = VENC_SendFrame(p->vencChn, &vencFrame, 0);
        if (ret != 0) {
            /* Encoder rejected frame - release immediately */
            VDEC_ReleaseFrame(frameItem->s32VdecChn, frameItem->ulBufferId);
            frame_pool_free(p, frameItem);
            p->errorCount++;
            continue;
        }

        /* STEP 4: Frame accepted - save for later release */
        lastVdecChn = frameItem->s32VdecChn;
        lastBufferId = frameItem->ulBufferId;
        lastFramePending = MPP_TRUE;
        frame_pool_free(p, frameItem);
    }

    /* Release any remaining pending buffer */
    if (lastFramePending) {
        VDEC_ReleaseFrame(lastVdecChn, lastBufferId);
    }

    LOG_STREAM(p->id, "VENC thread exiting (count=%llu)", (uint64_t)p->vencCount);
    return NULL;
}

/* ======================== MUX Thread ======================== */

static void *mux_thread_func(void *arg) {
    StreamPipeline *p = (StreamPipeline *)arg;
    S32 ret;

    LOG_STREAM(p->id, "MUX thread started");

    while (!p->bStopRequested) {
        StreamItem *streamItem = NULL;
        ret = MppQueue_Pop(&p->queue3, (void **)&streamItem, 100);
        if (ret != MPP_QUEUE_OK)
            continue;

        if (streamItem->bEos) {
            free(streamItem);
            break;
        }

        /* Send via standard MUX API (internally uses shared RTSP server) */
        MuxPacket muxPkt;
        memset(&muxPkt, 0, sizeof(muxPkt));
        muxPkt.pu8Data = streamItem->pu8Data;
        muxPkt.u32Size = streamItem->u32Size;
        muxPkt.u64PTS = streamItem->u64PTS;
        muxPkt.bKeyFrame = streamItem->bKeyFrame;
        muxPkt.eCodecType = MUX_CODEC_H264;

        ret = MUX_SendPacket(p->muxChn, &muxPkt);

        free(streamItem->pu8Data);
        free(streamItem);

        if (ret != 0) {
            p->errorCount++;
            continue;
        }

        p->muxCount++;
    }

    LOG_STREAM(p->id, "MUX thread exiting (count=%llu)", (uint64_t)p->muxCount);
    return NULL;
}

/* ======================== Pipeline Control ======================== */

static S32 pipeline_start(StreamPipeline *p) {
    StreamManager *mgr = p->manager;

    if (p->state != STREAM_STATE_IDLE) {
        return -1;
    }

    p->state = STREAM_STATE_STARTING;
    p->bStopRequested = MPP_FALSE;

    /* Initialize queues */
    if (MppQueue_Init(&p->queue1, QUEUE1_CAPACITY, MPP_TRUE) != 0 ||
        MppQueue_Init(&p->queue2, QUEUE2_CAPACITY, MPP_TRUE) != 0 ||
        MppQueue_Init(&p->queue3, QUEUE3_CAPACITY, MPP_TRUE) != 0) {
        LOG_STREAM(p->id, "Queue init failed");
        goto fail;
    }

    MppQueue_SetDropCallback(&p->queue1, drop_packet_cb, NULL);
    /* DO NOT set drop callback for queue2! Encoder holds dma-buf reference
     * to VDEC buffers. Dropping a frame while encoder uses it causes
     * double-release and corruption. Instead, VDEC thread will block or
     * manually release on push failure. */
    MppQueue_SetDropCallback(&p->queue3, drop_stream_cb, NULL);

    /* Create DEMUX channel */
    DemuxChnAttr demuxAttr = {0};
    strncpy(demuxAttr.szUrl, p->config.inputUrl, sizeof(demuxAttr.szUrl) - 1);
    demuxAttr.bPreferTcp = p->config.preferTcp;
    demuxAttr.u32ReconnectMs = 3000;

    p->demuxChn = p->id;
    LOG_STREAM(p->id, "Creating DEMUX channel for: %s", p->config.inputUrl);
    if (DEMUX_CreateChn(p->demuxChn, &demuxAttr) != 0) {
        LOG_STREAM(p->id, "DEMUX_CreateChn failed");
        goto fail;
    }
    LOG_STREAM(p->id, "DEMUX channel created, starting...");

    /* Start DEMUX to connect and get stream info */
    if (DEMUX_StartChn(p->demuxChn) != 0) {
        LOG_STREAM(p->id, "DEMUX_StartChn failed");
        goto fail;
    }
    LOG_STREAM(p->id, "DEMUX started, waiting for stream info...");

    /* Wait for stream info (poll until valid or timeout) */
    DemuxStreamInfo streamInfo = {0};
    S32 waitMs = 0;
    const S32 maxWaitMs = 10000; /* 10 seconds max */
    while (waitMs < maxWaitMs && !p->bStopRequested) {
        usleep(100000); /* 100ms */
        waitMs += 100;
        if (DEMUX_GetStreamInfo(p->demuxChn, &streamInfo) == 0 && streamInfo.u32Width > 0 && streamInfo.u32Height > 0) {
            break;
        }
    }

    /* Check if stopped during wait */
    if (p->bStopRequested) {
        LOG_STREAM(p->id, "Start interrupted by stop request");
        goto fail;
    }

    if (streamInfo.u32Width == 0 || streamInfo.u32Height == 0) {
        LOG_STREAM(p->id, "DEMUX_GetStreamInfo timeout (using defaults)");
        p->inputWidth = 1920;
        p->inputHeight = 1080;
        p->inputFps = 30;
        p->inputCodec = MPP_STREAM_CODEC_H264;
    } else {
        p->inputWidth = streamInfo.u32Width;
        p->inputHeight = streamInfo.u32Height;
        p->inputFps = streamInfo.u32Fps > 0 ? streamInfo.u32Fps : 30;
        p->inputCodec = demux_codec_to_mpp(streamInfo.eCodecType);
    }

    LOG_STREAM(p->id, "Input: %ux%u @ %ufps", p->inputWidth, p->inputHeight, p->inputFps);

    U32 width = p->config.width ? p->config.width : p->inputWidth;
    U32 height = p->config.height ? p->config.height : p->inputHeight;
    U32 fps = p->inputFps;

    /* Create VDEC channel */
    VdecChnAttr vdecAttr = {0};
    vdecAttr.eCodecType = p->inputCodec;
    vdecAttr.eOutputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    vdecAttr.u32Width = width;
    vdecAttr.u32Height = height;

    p->vdecChn = p->id;
    if (VDEC_CreateChn(p->vdecChn, &vdecAttr) != 0) {
        LOG_STREAM(p->id, "VDEC_CreateChn failed");
        goto fail;
    }

    if (VDEC_EnableChn(p->vdecChn) != 0) {
        LOG_STREAM(p->id, "VDEC_EnableChn failed");
        goto fail;
    }

    /* Create VENC channel */
    VencChnAttr vencAttr = {0};
    vencAttr.eCodecType = MPP_STREAM_CODEC_H264;
    vencAttr.eInputPixelFormat = MPP_PIXEL_FORMAT_NV12;
    vencAttr.u32Width = width;
    vencAttr.u32Height = height;
    vencAttr.u32FrameRate = fps;
    vencAttr.u32Gop = fps * 2;
    vencAttr.u32Bitrate = p->config.bitrate ? p->config.bitrate : (width * height * fps / 10);
    vencAttr.eRcMode = VENC_RC_MODE_CBR;

    p->vencChn = p->id;
    if (VENC_CreateChn(p->vencChn, &vencAttr) != 0) {
        LOG_STREAM(p->id, "VENC_CreateChn failed");
        goto fail;
    }

    if (VENC_EnableChn(p->vencChn) != 0) {
        LOG_STREAM(p->id, "VENC_EnableChn failed");
        goto fail;
    }

    /* Create MUX channel (internally uses shared RTSP server) */
    MuxChnAttr muxAttr = {0};
    muxAttr.eOutputType = MUX_OUTPUT_RTSP;
    snprintf(muxAttr.szUrl, sizeof(muxAttr.szUrl), "rtsp://0.0.0.0:%u%s", mgr->rtspPort, p->config.outputPath);
    muxAttr.stStreamAttr.eCodecType = MUX_CODEC_H264;
    muxAttr.stStreamAttr.u32Width = width;
    muxAttr.stStreamAttr.u32Height = height;
    muxAttr.stStreamAttr.u32Fps = fps;

    p->muxChn = p->id;
    if (MUX_CreateChn(p->muxChn, &muxAttr) != 0) {
        LOG_STREAM(p->id, "MUX_CreateChn failed");
        goto fail;
    }

    if (MUX_StartChn(p->muxChn) != 0) {
        LOG_STREAM(p->id, "MUX_StartChn failed");
        goto fail;
    }

    /* Set DEMUX callback - now all channels are ready to process data */
    if (DEMUX_SetPacketCallback(p->demuxChn, demux_callback, p) != 0) {
        LOG_STREAM(p->id, "DEMUX_SetPacketCallback failed");
        goto fail;
    }

    /* Reset statistics */
    p->demuxCount = 0;
    p->vdecCount = 0;
    p->vencCount = 0;
    p->muxCount = 0;
    p->dropCount = 0;
    p->errorCount = 0;

    /* Start threads */
    if (pthread_create(&p->muxThread, NULL, mux_thread_func, p) != 0) {
        goto fail;
    }
    p->muxThreadCreated = MPP_TRUE;

    if (pthread_create(&p->vencThread, NULL, venc_thread_func, p) != 0) {
        goto fail;
    }
    p->vencThreadCreated = MPP_TRUE;

    if (pthread_create(&p->vdecThread, NULL, vdec_thread_func, p) != 0) {
        goto fail;
    }
    p->vdecThreadCreated = MPP_TRUE;

    p->state = STREAM_STATE_RUNNING;
    p->bRunning = MPP_TRUE;

    LOG_STREAM(p->id, "Pipeline started: %s", p->config.outputPath);
    fire_event(mgr, p->id, STREAM_EVENT_STARTED);

    return 0;

fail:
    p->bStopRequested = MPP_TRUE;
    MppQueue_WakeAll(&p->queue1);
    MppQueue_WakeAll(&p->queue2);
    MppQueue_WakeAll(&p->queue3);

    if (p->vdecThreadCreated) {
        pthread_join(p->vdecThread, NULL);
        p->vdecThreadCreated = MPP_FALSE;
    }
    if (p->vencThreadCreated) {
        pthread_join(p->vencThread, NULL);
        p->vencThreadCreated = MPP_FALSE;
    }
    if (p->muxThreadCreated) {
        pthread_join(p->muxThread, NULL);
        p->muxThreadCreated = MPP_FALSE;
    }

    if (p->muxChn >= 0) {
        MUX_StopChn(p->muxChn);
        MUX_DestroyChn(p->muxChn);
        p->muxChn = -1;
    }
    if (p->vencChn >= 0) {
        VENC_DisableChn(p->vencChn);
        VENC_DestroyChn(p->vencChn);
        p->vencChn = -1;
    }
    if (p->vdecChn >= 0) {
        VDEC_DisableChn(p->vdecChn);
        VDEC_DestroyChn(p->vdecChn);
        p->vdecChn = -1;
    }
    if (p->demuxChn >= 0) {
        DEMUX_StopChn(p->demuxChn);
        DEMUX_DestroyChn(p->demuxChn);
        p->demuxChn = -1;
    }

    MppQueue_Clear(&p->queue1);
    MppQueue_Clear(&p->queue2);
    MppQueue_Clear(&p->queue3);
    MppQueue_Destroy(&p->queue1);
    MppQueue_Destroy(&p->queue2);
    MppQueue_Destroy(&p->queue3);

    p->state = STREAM_STATE_ERROR;
    fire_event(mgr, p->id, STREAM_EVENT_ERROR);

    return -1;
}

static S32 pipeline_stop(StreamPipeline *p) {
    if (p->state != STREAM_STATE_RUNNING && p->state != STREAM_STATE_ERROR) {
        return 0;
    }

    p->state = STREAM_STATE_STOPPING;
    p->bStopRequested = MPP_TRUE;
    p->bRunning = MPP_FALSE;

    /* Stop DEMUX first */
    if (p->demuxChn >= 0) {
        DEMUX_SetPacketCallback(p->demuxChn, NULL, NULL);
        DEMUX_StopChn(p->demuxChn);
    }

    /* Wake up queues */
    MppQueue_WakeAll(&p->queue1);
    MppQueue_WakeAll(&p->queue2);
    MppQueue_WakeAll(&p->queue3);

    /* Wait for threads */
    if (p->vdecThreadCreated) {
        pthread_join(p->vdecThread, NULL);
        p->vdecThreadCreated = MPP_FALSE;
    }
    if (p->vencThreadCreated) {
        pthread_join(p->vencThread, NULL);
        p->vencThreadCreated = MPP_FALSE;
    }
    if (p->muxThreadCreated) {
        pthread_join(p->muxThread, NULL);
        p->muxThreadCreated = MPP_FALSE;
    }

    /* Cleanup channels */
    if (p->muxChn >= 0) {
        MUX_StopChn(p->muxChn);
        MUX_DestroyChn(p->muxChn);
        p->muxChn = -1;
    }
    if (p->vencChn >= 0) {
        VENC_DisableChn(p->vencChn);
        VENC_DestroyChn(p->vencChn);
        p->vencChn = -1;
    }
    if (p->vdecChn >= 0) {
        VDEC_DisableChn(p->vdecChn);
        VDEC_DestroyChn(p->vdecChn);
        p->vdecChn = -1;
    }
    if (p->demuxChn >= 0) {
        DEMUX_DestroyChn(p->demuxChn);
        p->demuxChn = -1;
    }

    /* Clear queues */
    MppQueue_Clear(&p->queue1);
    MppQueue_Clear(&p->queue2);
    MppQueue_Clear(&p->queue3);
    MppQueue_Destroy(&p->queue1);
    MppQueue_Destroy(&p->queue2);
    MppQueue_Destroy(&p->queue3);

    p->state = STREAM_STATE_IDLE;

    LOG_STREAM(p->id, "Pipeline stopped");
    fire_event(p->manager, p->id, STREAM_EVENT_STOPPED);

    return 0;
}

/* ======================== Monitor Thread ======================== */

static void *monitor_thread_func(void *arg) {
    StreamManager *mgr = (StreamManager *)arg;

    printf("[MONITOR] Thread started\n");

    while (mgr->bRunning) {
        sleep(5);

        if (!mgr->bRunning)
            break;

        pthread_mutex_lock(&mgr->lock);

        S32 running = 0, errors = 0;
        U64 totalFrames = 0;

        for (S32 i = 0; i < mgr->maxStreams; i++) {
            StreamPipeline *p = &mgr->pipelines[i];
            if (!p->configured)
                continue;

            if (p->state == STREAM_STATE_RUNNING) {
                running++;
                totalFrames += p->vencCount;
            } else if (p->state == STREAM_STATE_ERROR) {
                errors++;
            }
        }

        printf("[MONITOR] streams: %d running, %d errors, %llu total frames\n", running, errors, (uint64_t)totalFrames);

        pthread_mutex_unlock(&mgr->lock);
    }

    printf("[MONITOR] Thread exiting\n");
    return NULL;
}

/* ======================== Manager API ======================== */

StreamManager *StreamManager_Create(U16 rtspPort, S32 maxStreams) {
    if (maxStreams <= 0 || maxStreams > STREAM_MAX_COUNT) {
        maxStreams = STREAM_MAX_COUNT;
    }

    StreamManager *mgr = (StreamManager *)calloc(1, sizeof(StreamManager));
    if (!mgr)
        return NULL;

    mgr->pipelines = (StreamPipeline *)calloc(maxStreams, sizeof(StreamPipeline));
    if (!mgr->pipelines) {
        free(mgr);
        return NULL;
    }

    mgr->maxStreams = maxStreams;
    mgr->streamCount = 0;
    mgr->rtspPort = rtspPort;
    mgr->bRunning = MPP_TRUE;

    pthread_mutex_init(&mgr->lock, NULL);

    /* Initialize pipeline slots */
    for (S32 i = 0; i < maxStreams; i++) {
        mgr->pipelines[i].id = i;
        mgr->pipelines[i].manager = mgr;
        mgr->pipelines[i].demuxChn = -1;
        mgr->pipelines[i].vdecChn = -1;
        mgr->pipelines[i].vencChn = -1;
        mgr->pipelines[i].muxChn = -1;
        mgr->pipelines[i].state = STREAM_STATE_IDLE;
    }

    /* Initialize MPP system */
    if (SYS_Init() == 0) {
        mgr->sysInitialized = MPP_TRUE;
    }

    /* Initialize VB (Video Buffer) */
    VB_Init();

    /* Initialize MPP modules */
    DEMUX_Init();
    VDEC_Init();
    VENC_Init();
    MUX_Init();

    /* Start monitor thread */
    if (pthread_create(&mgr->monitorThread, NULL, monitor_thread_func, mgr) == 0) {
        mgr->monitorThreadCreated = MPP_TRUE;
    }

    printf("[MANAGER] Created: port=%u, maxStreams=%d\n", rtspPort, maxStreams);

    return mgr;
}

void StreamManager_Destroy(StreamManager *mgr) {
    if (!mgr)
        return;

    mgr->bRunning = MPP_FALSE;

    /* Stop all streams */
    StreamManager_StopAll(mgr);

    /* Stop monitor */
    if (mgr->monitorThreadCreated) {
        pthread_join(mgr->monitorThread, NULL);
    }

    pthread_mutex_destroy(&mgr->lock);

    if (mgr->pipelines) {
        free(mgr->pipelines);
    }

    /* Deinitialize MPP modules */
    MUX_Exit();
    VENC_Exit();
    VDEC_Exit();
    DEMUX_Exit();
    VB_Exit();

    printf("[MANAGER] Destroyed\n");
    free(mgr);
}

S32 StreamManager_AddStream(StreamManager *mgr, const StreamConfig *config) {
    if (!mgr || !config)
        return -1;

    pthread_mutex_lock(&mgr->lock);

    /* Find free slot */
    S32 id = -1;
    for (S32 i = 0; i < mgr->maxStreams; i++) {
        if (!mgr->pipelines[i].configured) {
            id = i;
            break;
        }
    }

    if (id < 0) {
        pthread_mutex_unlock(&mgr->lock);
        printf("[MANAGER] No free slot available\n");
        return -1;
    }

    StreamPipeline *p = &mgr->pipelines[id];
    memcpy(&p->config, config, sizeof(StreamConfig));
    p->configured = MPP_TRUE;
    mgr->streamCount++;

    pthread_mutex_unlock(&mgr->lock);

    printf("[MANAGER] Added stream %d: %s -> %s\n", id, config->inputUrl, config->outputPath);
    return id;
}

S32 StreamManager_RemoveStream(StreamManager *mgr, S32 streamId) {
    if (!mgr || streamId < 0 || streamId >= mgr->maxStreams)
        return -1;

    pthread_mutex_lock(&mgr->lock);

    StreamPipeline *p = &mgr->pipelines[streamId];
    if (!p->configured) {
        pthread_mutex_unlock(&mgr->lock);
        return -1;
    }

    if (p->state != STREAM_STATE_IDLE) {
        pthread_mutex_unlock(&mgr->lock);
        printf("[MANAGER] Stream %d must be stopped first\n", streamId);
        return -1;
    }

    p->configured = MPP_FALSE;
    mgr->streamCount--;

    pthread_mutex_unlock(&mgr->lock);

    printf("[MANAGER] Removed stream %d\n", streamId);
    return 0;
}

S32 StreamManager_StartStream(StreamManager *mgr, S32 streamId) {
    if (!mgr || streamId < 0 || streamId >= mgr->maxStreams)
        return -1;

    pthread_mutex_lock(&mgr->lock);

    StreamPipeline *p = &mgr->pipelines[streamId];
    if (!p->configured) {
        pthread_mutex_unlock(&mgr->lock);
        return -1;
    }

    pthread_mutex_unlock(&mgr->lock);

    return pipeline_start(p);
}

S32 StreamManager_StopStream(StreamManager *mgr, S32 streamId) {
    if (!mgr || streamId < 0 || streamId >= mgr->maxStreams)
        return -1;

    pthread_mutex_lock(&mgr->lock);

    StreamPipeline *p = &mgr->pipelines[streamId];
    if (!p->configured) {
        pthread_mutex_unlock(&mgr->lock);
        return -1;
    }

    pthread_mutex_unlock(&mgr->lock);

    return pipeline_stop(p);
}

S32 StreamManager_StartAll(StreamManager *mgr) {
    if (!mgr)
        return -1;

    S32 started = 0;

    pthread_mutex_lock(&mgr->lock);
    for (S32 i = 0; i < mgr->maxStreams; i++) {
        if (mgr->pipelines[i].configured && mgr->pipelines[i].state == STREAM_STATE_IDLE) {
            pthread_mutex_unlock(&mgr->lock);
            if (pipeline_start(&mgr->pipelines[i]) == 0) {
                started++;
            }
            pthread_mutex_lock(&mgr->lock);
        }
    }
    pthread_mutex_unlock(&mgr->lock);

    printf("[MANAGER] Started %d streams\n", started);
    return started;
}

S32 StreamManager_StopAll(StreamManager *mgr) {
    if (!mgr)
        return -1;

    S32 stopped = 0;

    /* First pass: set stop flag for STARTING pipelines to interrupt blocking operations */
    pthread_mutex_lock(&mgr->lock);
    for (S32 i = 0; i < mgr->maxStreams; i++) {
        if (mgr->pipelines[i].configured && mgr->pipelines[i].state == STREAM_STATE_STARTING) {
            mgr->pipelines[i].bStopRequested = MPP_TRUE;
        }
    }
    pthread_mutex_unlock(&mgr->lock);

    /* Second pass: stop RUNNING/ERROR pipelines */
    pthread_mutex_lock(&mgr->lock);
    for (S32 i = 0; i < mgr->maxStreams; i++) {
        if (mgr->pipelines[i].configured &&
            (mgr->pipelines[i].state == STREAM_STATE_RUNNING || mgr->pipelines[i].state == STREAM_STATE_ERROR)) {
            pthread_mutex_unlock(&mgr->lock);
            if (pipeline_stop(&mgr->pipelines[i]) == 0) {
                stopped++;
            }
            pthread_mutex_lock(&mgr->lock);
        }
    }
    pthread_mutex_unlock(&mgr->lock);

    printf("[MANAGER] Stopped %d streams\n", stopped);
    return stopped;
}

S32 StreamManager_GetStreamStats(StreamManager *mgr, S32 streamId, StreamStats *stats) {
    if (!mgr || !stats || streamId < 0 || streamId >= mgr->maxStreams)
        return -1;

    pthread_mutex_lock(&mgr->lock);

    StreamPipeline *p = &mgr->pipelines[streamId];
    if (!p->configured) {
        pthread_mutex_unlock(&mgr->lock);
        return -1;
    }

    memset(stats, 0, sizeof(*stats));
    stats->id = streamId;
    stats->state = p->state;
    stats->demuxFrames = p->demuxCount;
    stats->vdecFrames = p->vdecCount;
    stats->vencFrames = p->vencCount;
    stats->muxFrames = p->muxCount;
    stats->dropFrames = p->dropCount;
    stats->errorCount = p->errorCount;
    stats->inputWidth = p->inputWidth;
    stats->inputHeight = p->inputHeight;
    stats->inputFps = p->inputFps;

    pthread_mutex_unlock(&mgr->lock);
    return 0;
}

S32 StreamManager_GetStats(StreamManager *mgr, ManagerStats *stats) {
    if (!mgr || !stats)
        return -1;

    memset(stats, 0, sizeof(*stats));

    pthread_mutex_lock(&mgr->lock);

    for (S32 i = 0; i < mgr->maxStreams; i++) {
        StreamPipeline *p = &mgr->pipelines[i];
        if (!p->configured)
            continue;

        stats->totalStreams++;
        if (p->state == STREAM_STATE_RUNNING) {
            stats->runningStreams++;
            stats->totalFrames += p->vencCount;
            stats->totalDrops += p->dropCount;
        } else if (p->state == STREAM_STATE_ERROR) {
            stats->errorStreams++;
        }
    }

    pthread_mutex_unlock(&mgr->lock);
    return 0;
}

void StreamManager_SetEventCallback(StreamManager *mgr, StreamEventCallback cb, void *userData) {
    if (!mgr)
        return;
    mgr->eventCallback = cb;
    mgr->eventUserData = userData;
}

void StreamManager_SetFrameCallback(StreamManager *mgr, StreamFrameCallback cb, void *userData) {
    if (!mgr)
        return;
    mgr->frameCallback = cb;
    mgr->frameUserData = userData;
}

S32 StreamManager_GetOutputUrl(StreamManager *mgr, S32 streamId, char *buf, U32 len) {
    if (!mgr || !buf || len == 0 || streamId < 0 || streamId >= mgr->maxStreams)
        return -1;

    pthread_mutex_lock(&mgr->lock);

    StreamPipeline *p = &mgr->pipelines[streamId];
    if (!p->configured) {
        pthread_mutex_unlock(&mgr->lock);
        return -1;
    }

    snprintf(buf, len, "rtsp://localhost:%u%s", mgr->rtspPort, p->config.outputPath);

    pthread_mutex_unlock(&mgr->lock);
    return 0;
}
