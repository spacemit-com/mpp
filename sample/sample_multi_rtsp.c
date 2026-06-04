/*
 *------------------------------------------------------------------------------
 * Copyright 2025-2026 SPACEMIT. All rights reserved.
 *
 * @File      : sample_multi_rtsp.c
 * @Brief     : Multi-stream RTSP transcoding sample.
 *
 * Usage:
 *   ./sample_multi_rtsp <url1> [url2] [url3] ... [options]
 *
 * Options:
 *   --tcp, -t         Use TCP for all RTSP inputs
 *   --port, -p <N>    Output RTSP port (default: 8554)
 *
 * Example:
 *   ./sample_multi_rtsp \
 *     rtsp://admin:123456@10.5.90.121:8554/cam1.mp4 \
 *     rtsp://admin:123456@10.5.90.121:8554/cam2.mp4 \
 *     rtsp://admin:123456@10.5.90.121:8554/cam3.mp4 \
 *     rtsp://admin:123456@10.5.90.121:8554/cam4.mp4 \
 *     --tcp -p 8554
 *
 * Output URLs:
 *   rtsp://localhost:8554/live/0
 *   rtsp://localhost:8554/live/1
 *   rtsp://localhost:8554/live/2
 *   rtsp://localhost:8554/live/3
 *------------------------------------------------------------------------------
 */

#include <getopt.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "stream/mpp_stream_manager.h"

#define MAX_INPUT_URLS 16

static volatile int g_running = 1;
static volatile int g_signal_count = 0;
static StreamManager *g_manager = NULL;

static void signal_handler(int sig) {
    (void)sig;
    g_signal_count++;
    printf("\nReceived signal, stopping... (press %d more times to force exit)\n", 3 - g_signal_count);
    g_running = 0;

    if (g_signal_count >= 3) {
        printf("Force exit!\n");
        _exit(1);
    }
}

static void event_callback(S32 streamId, StreamEvent event, void *userData) {
    (void)userData;
    const char *eventNames[] = {
        "STARTED", "STOPPED", "ERROR", "RECONNECTING", "CLIENT_CONNECTED", "CLIENT_DISCONNECTED"};
    printf("[EVENT] Stream %d: %s\n", streamId, eventNames[event]);
}

static void print_usage(const char *progname) {
    printf("Multi-Stream RTSP Transcoder\n\n");
    printf("Usage: %s <url1> [url2] [url3] ... [options]\n\n", progname);
    printf("Options:\n");
    printf("  --tcp, -t         Use TCP for all RTSP inputs\n");
    printf("  --port, -p <N>    Output RTSP port (default: 8554)\n");
    printf("  --help, -h        Show this help\n");
    printf("\nExample:\n");
    printf("  %s rtsp://admin:pass@192.168.1.100/cam1 \\\n", progname);
    printf("         rtsp://admin:pass@192.168.1.100/cam2 --tcp\n");
}

static void print_stats(StreamManager *mgr, S32 numStreams) {
    ManagerStats mgrStats;
    StreamManager_GetStats(mgr, &mgrStats);

    printf("\n============ Statistics ============\n");
    printf("Streams: %d running / %d total, %d errors\n", mgrStats.runningStreams, mgrStats.totalStreams,
        mgrStats.errorStreams);
    printf("Total: %" PRIu64 " frames, %" PRIu64 " drops\n\n", (uint64_t)mgrStats.totalFrames,
        (uint64_t)mgrStats.totalDrops);

    for (S32 i = 0; i < numStreams; i++) {
        StreamStats stats;
        if (StreamManager_GetStreamStats(mgr, i, &stats) == 0) {
            const char *stateNames[] = {"IDLE", "STARTING", "RUNNING", "ERROR", "STOPPING"};
            printf("  [%d] %s: demux=%" PRIu64 " vdec=%" PRIu64 " venc=%" PRIu64 " mux=%" PRIu64 " drop=%" PRIu64
                " err=%" PRIu64 "\n",
                i, stateNames[stats.state], (uint64_t)stats.demuxFrames, (uint64_t)stats.vdecFrames,
                (uint64_t)stats.vencFrames, (uint64_t)stats.muxFrames, (uint64_t)stats.dropFrames,
                (uint64_t)stats.errorCount);
        }
    }
    printf("====================================\n");
}

int main(int argc, char *argv[]) {
    char *inputUrls[MAX_INPUT_URLS] = {0};
    int numUrls = 0;
    BOOL preferTcp = MPP_FALSE;
    U16 outputPort = 8554;

    /* Parse command line */
    static struct option long_options[] = {
        {"tcp", no_argument, 0, 't'}, {"port", required_argument, 0, 'p'}, {"help", no_argument, 0, 'h'}, {0, 0, 0, 0}};

    int opt;
    while ((opt = getopt_long(argc, argv, "tp:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 't':
            preferTcp = MPP_TRUE;
            break;
        case 'p':
            outputPort = (U16)atoi(optarg);
            break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Collect input URLs (non-option arguments) */
    for (int i = optind; i < argc && numUrls < MAX_INPUT_URLS; i++) {
        if (strncmp(argv[i], "rtsp://", 7) == 0) {
            inputUrls[numUrls++] = argv[i];
        }
    }

    if (numUrls == 0) {
        fprintf(stderr, "Error: At least one RTSP URL is required\n\n");
        print_usage(argv[0]);
        return 1;
    }

    /* Setup signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    printf("=== Multi-Stream RTSP Transcoder ===\n");
    printf("Input streams: %d\n", numUrls);
    printf("Output port:   %u\n", outputPort);
    printf("Transport:     %s\n\n", preferTcp ? "TCP" : "UDP");

    /* Create stream manager */
    g_manager = StreamManager_Create(outputPort, numUrls);
    if (!g_manager) {
        fprintf(stderr, "Failed to create stream manager\n");
        return 1;
    }

    /* Set event callback */
    StreamManager_SetEventCallback(g_manager, event_callback, NULL);

    /* Add all streams */
    printf("Adding streams:\n");
    for (int i = 0; i < numUrls; i++) {
        StreamConfig config = {0};
        strncpy(config.inputUrl, inputUrls[i], sizeof(config.inputUrl) - 1);
        snprintf(config.outputPath, sizeof(config.outputPath), "/live/%d", i);
        config.preferTcp = preferTcp;
        config.bitrate = 0; /* Auto */

        S32 id = StreamManager_AddStream(g_manager, &config);
        if (id >= 0) {
            char outputUrl[256];
            StreamManager_GetOutputUrl(g_manager, id, outputUrl, sizeof(outputUrl));
            printf("  [%d] %s\n      -> %s\n", id, inputUrls[i], outputUrl);
        } else {
            fprintf(stderr, "  Failed to add stream: %s\n", inputUrls[i]);
        }
    }

    /* Start all streams */
    printf("\nStarting all streams...\n");
    S32 started = StreamManager_StartAll(g_manager);
    printf("Started %d/%d streams\n\n", started, numUrls);

    if (started == 0) {
        fprintf(stderr, "No streams started successfully\n");
        StreamManager_Destroy(g_manager);
        return 1;
    }

    printf("Output URLs:\n");
    for (int i = 0; i < numUrls; i++) {
        char outputUrl[256];
        if (StreamManager_GetOutputUrl(g_manager, i, outputUrl, sizeof(outputUrl)) == 0) {
            printf("  %s\n", outputUrl);
        }
    }
    printf("\nPress Ctrl+C to stop.\n");

    /* Main loop */
    int statCounter = 0;
    while (g_running) {
        sleep(1);
        statCounter++;

        /* Print stats every 10 seconds */
        if (statCounter >= 10) {
            print_stats(g_manager, numUrls);
            statCounter = 0;
        }
    }

    /* Cleanup */
    printf("\nStopping all streams...\n");
    StreamManager_StopAll(g_manager);

    /* Final stats */
    print_stats(g_manager, numUrls);

    StreamManager_Destroy(g_manager);
    g_manager = NULL;

    printf("Done.\n");
    return 0;
}
