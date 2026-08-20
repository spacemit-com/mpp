/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      : mpp_stream_manager.h
 * @Brief     : Multi-stream manager for concurrent RTSP transcoding.
 *
 * Supports up to 16 concurrent video streams, each with independent
 * DEMUX → VDEC → VENC → MUX pipeline.
 *------------------------------------------------------------------------------
 */

#ifndef MPP_STREAM_MANAGER_H
#define MPP_STREAM_MANAGER_H

#include "sys/type.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STREAM_MAX_COUNT 16
#define STREAM_URL_MAX_LEN 256
#define STREAM_PATH_MAX_LEN 128
#define STREAM_MANAGER_MAX_NAME_LEN 64

/*
 * Stream state
 */
typedef enum StreamState {
    STREAM_STATE_IDLE = 0, /* Not started */
    STREAM_STATE_STARTING, /* Starting up */
    STREAM_STATE_RUNNING,  /* Running normally */
    STREAM_STATE_ERROR,    /* Error occurred */
    STREAM_STATE_STOPPING, /* Shutting down */
} StreamState;

/*
 * Single stream configuration
 */
typedef struct StreamConfig {
    char inputUrl[STREAM_URL_MAX_LEN];    /* Input RTSP URL */
    char outputPath[STREAM_PATH_MAX_LEN]; /* Output path (e.g., "/live/cam1") */
    BOOL preferTcp;                       /* Use TCP for RTSP input */
    U32 bitrate;                          /* Output bitrate (0 = auto) */
    U32 width;                            /* Output width (0 = same as input) */
    U32 height;                           /* Output height (0 = same as input) */
} StreamConfig;

/*
 * Single stream statistics
 */
typedef struct StreamStats {
    S32 id;            /* Stream ID */
    StreamState state; /* Current state */
    U64 demuxFrames;   /* Packets received */
    U64 vdecFrames;    /* Frames decoded */
    U64 vencFrames;    /* Frames encoded */
    U64 muxFrames;     /* Packets sent */
    U64 dropFrames;    /* Frames dropped */
    U64 errorCount;    /* Error count */
    U32 inputWidth;    /* Detected input width */
    U32 inputHeight;   /* Detected input height */
    U32 inputFps;      /* Detected input FPS */
    U32 activeClients; /* Connected RTSP clients */
} StreamStats;

/*
 * Manager statistics
 */
typedef struct ManagerStats {
    S32 totalStreams;   /* Total configured streams */
    S32 runningStreams; /* Currently running streams */
    S32 errorStreams;   /* Streams in error state */
    U64 totalFrames;    /* Total frames processed */
    U64 totalDrops;     /* Total frames dropped */
    U32 totalClients;   /* Total connected clients */
} ManagerStats;

/*
 * Stream event callback
 */
typedef enum StreamEvent {
    STREAM_EVENT_STARTED = 0,
    STREAM_EVENT_STOPPED,
    STREAM_EVENT_ERROR,
    STREAM_EVENT_RECONNECTING,
    STREAM_EVENT_CLIENT_CONNECTED,
    STREAM_EVENT_CLIENT_DISCONNECTED,
} StreamEvent;

typedef void (*StreamEventCallback)(S32 streamId, StreamEvent event, void *userData);

/*
 * Frame info for processing callback (YOLO, etc.)
 */
typedef struct StreamFrameInfo {
    S32 streamId;   /* Stream ID */
    U64 frameIndex; /* Frame index */
    U32 width;      /* Frame width */
    U32 height;     /* Frame height */
    U64 pts;        /* Presentation timestamp */
    U32 dmaFd;      /* DMA-buf fd (for V2D/GPU) */
    void *virAddr;  /* Virtual address (for CPU) */
    U32 stride;     /* Line stride */
    UL bufferId;    /* VB buffer ID (required for V2D) */

    /* Output frame override (set by callback for OSD blending) */
    BOOL useOutputFrame; /* If TRUE, use output* fields for VENC */
    U32 outDmaFd;        /* Output frame DMA fd */
    void *outVirAddr;    /* Output frame virtual address */
    UL outBufferId;      /* Output frame VB buffer ID */
} StreamFrameInfo;

/**
 * @brief Frame processing callback (called between VDEC and VENC).
 *
 *        This callback is invoked for each decoded frame before encoding.
 *        It can be used for:
 *        - AI inference (YOLO detection)
 *        - OSD drawing (set useOutputFrame=TRUE and fill out* fields)
 *        - Frame analysis
 *
 *        For OSD blending: Set frame->useOutputFrame=TRUE and provide
 *        frame->outDmaFd, frame->outVirAddr, frame->outBufferId to use
 *        a different frame for encoding (e.g., after V2D blending).
 *
 *        IMPORTANT: Do NOT release the frame buffer in this callback.
 *        The StreamManager handles buffer lifecycle.
 *
 * @param frame     Frame info (can be modified for OSD output)
 * @param userData  User-provided context
 * @return 0 to continue processing, non-zero to skip encoding this frame
 */
typedef S32 (*StreamFrameCallback)(StreamFrameInfo *frame, void *userData);

/*
 * Opaque manager handle
 */
typedef struct StreamManager StreamManager;

/**
 * @brief Create a multi-stream manager.
 *
 * @param rtspPort      RTSP server port (shared by all streams)
 * @param maxStreams    Maximum number of streams (1-16)
 * @return Manager handle, or NULL on failure
 */
StreamManager *StreamManager_Create(U16 rtspPort, S32 maxStreams);

/**
 * @brief Destroy the manager and stop all streams.
 */
void StreamManager_Destroy(StreamManager *mgr);

/**
 * @brief Add a stream configuration.
 *
 * @param mgr       Manager handle
 * @param config    Stream configuration
 * @return Stream ID (>=0) on success, -1 on failure
 */
S32 StreamManager_AddStream(StreamManager *mgr, const StreamConfig *config);

/**
 * @brief Remove a stream (must be stopped first).
 *
 * @param mgr       Manager handle
 * @param streamId  Stream ID
 * @return 0 on success, -1 on failure
 */
S32 StreamManager_RemoveStream(StreamManager *mgr, S32 streamId);

/**
 * @brief Start a specific stream.
 */
S32 StreamManager_StartStream(StreamManager *mgr, S32 streamId);

/**
 * @brief Stop a specific stream.
 */
S32 StreamManager_StopStream(StreamManager *mgr, S32 streamId);

/**
 * @brief Start all configured streams.
 */
S32 StreamManager_StartAll(StreamManager *mgr);

/**
 * @brief Stop all running streams.
 */
S32 StreamManager_StopAll(StreamManager *mgr);

/**
 * @brief Get statistics for a specific stream.
 */
S32 StreamManager_GetStreamStats(StreamManager *mgr, S32 streamId, StreamStats *stats);

/**
 * @brief Get overall manager statistics.
 */
S32 StreamManager_GetStats(StreamManager *mgr, ManagerStats *stats);

/**
 * @brief Set event callback.
 */
void StreamManager_SetEventCallback(StreamManager *mgr, StreamEventCallback cb, void *userData);

/**
 * @brief Set frame processing callback (for AI inference, OSD, etc.).
 *        Called for each decoded frame before encoding.
 *
 * @param mgr       Manager handle
 * @param cb        Frame callback function
 * @param userData  User context passed to callback
 */
void StreamManager_SetFrameCallback(StreamManager *mgr, StreamFrameCallback cb, void *userData);

/**
 * @brief Get output URL for a stream.
 *
 * @param mgr       Manager handle
 * @param streamId  Stream ID
 * @param buf       Output buffer
 * @param len       Buffer length
 * @return 0 on success
 */
S32 StreamManager_GetOutputUrl(StreamManager *mgr, S32 streamId, char *buf, U32 len);

#ifdef __cplusplus
}
#endif

#endif /* MPP_STREAM_MANAGER_H */
