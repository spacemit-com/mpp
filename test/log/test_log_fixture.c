/* Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license in LICENSE.
 * Hardware-independent subprocess used by test_live_log.py. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "log.h"
#include "demux/demux_api.h"
#include "mux/mux_api.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static void *burst(void *arg) {
    long count = (long)arg;
    for (long i = 0; i < count; ++i)
        MPP_LOG(MPP_LOG_INFO, "vdec", "burst %ld", i);
    return NULL;
}
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    mpp_set_log_level(MPP_LOG_WARN);
    if (mpp_get_log_level() != MPP_LOG_WARN || mpp_get_log_level() != MPP_LOG_WARN) return 10;
    int evaluated = 0;
    MPP_LOG(MPP_LOG_DEBUG, "vdec", "disabled argument %d", ++evaluated);
    if (evaluated) return 11;
    mpp_set_log_level(MPP_LOG_INFO);
    errno = EBUSY;
    MPP_LOG(MPP_LOG_INFO, "sys", "fixture starting");
    if (errno != EBUSY) return 12;
    if (argc == 2 && !strcmp(argv[1], "demux")) {
        if (DEMUX_Init()) return 18;
    } else if (argc == 2 && !strcmp(argv[1], "mux")) {
        if (MUX_Init()) return 19;
    } else if (mpp_log_start() || mpp_log_start()) return 13;
    if (argc == 2) MPP_LOG(MPP_LOG_INFO, argv[1], "standalone service ready");
    printf("PID %ld\n", (long)getpid());
    char command[256], tag[64];
    int level, count;
    while (fgets(command, sizeof(command), stdin)) {
        if (sscanf(command, "emit %63s %d %d", tag, &level, &count) == 3) {
            for (int i = 0; i < count; ++i) {
                MPP_LOG(level, tag, "event-%s-%d-%d", tag, level, i);
                usleep(2000);
            }
            puts("OK");
        } else if (sscanf(command, "enabled %63s %d", tag, &level) == 2) {
            printf("%d\n", mpp_log_enabled(level, tag));
        } else if (sscanf(command, "base %d", &level) == 1) {
            mpp_set_log_level(level);
            printf("%d\n", mpp_get_log_level());
        } else if (sscanf(command, "burst %d", &count) == 1 && count >= 0) {
            pthread_t threads[4];
            for (int i = 0; i < 4; ++i) if (pthread_create(&threads[i], NULL, burst, (void *)(long)count)) return 14;
            for (int i = 0; i < 4; ++i) pthread_join(threads[i], NULL);
            puts("OK");
        } else if (!strncmp(command, "long", 4)) {
            char text[4096];
            memset(text, 'x', sizeof(text) - 1);
            text[sizeof(text) - 1] = 0;
            MPP_LOG(MPP_LOG_INFO, "vdec", "LONG%s", text);
            puts("OK");
        } else if (!strncmp(command, "fork", 4)) {
            pid_t child = fork();
            if (child < 0) return 15;
            if (!child) {
                if (mpp_log_enabled(MPP_LOG_DEBUG, "vdec") || mpp_log_start()) _exit(16);
                MPP_LOG(MPP_LOG_INFO, "sys", "child alive");
                mpp_log_stop();
                _exit(0);
            }
            int status;
            waitpid(child, &status, 0);
            printf("CHILD %d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        } else if (!strncmp(command, "stop", 4)) {
            mpp_log_stop();
            puts("OK");
        } else if (!strncmp(command, "start", 5)) {
            printf("%d\n", mpp_log_start());
        } else if (!strncmp(command, "quit", 4)) {
            break;
        } else return 17;
    }
    if (argc == 2 && !strcmp(argv[1], "demux")) DEMUX_Exit();
    if (argc == 2 && !strcmp(argv[1], "mux")) MUX_Exit();
    mpp_log_stop();
    return 0;
}
