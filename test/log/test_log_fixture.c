/* Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license in LICENSE.
 * Hardware-independent subprocess used by test_log_syslog.py. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "log.h"
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
    if (argc == 2) mpp_set_log_level(atoi(argv[1]));
    int initial = mpp_get_log_level();
    if (mpp_get_log_level() != initial) return 10;
    printf("PID %ld LEVEL %d\n", (long)getpid(), initial);
    char command[256], tag[64];
    int level, count;
    while (fgets(command, sizeof(command), stdin)) {
        if (sscanf(command, "emit %63s %d %d", tag, &level, &count) == 3 && count >= 0) {
            errno = EBUSY;
            for (int i = 0; i < count; ++i) {
                MPP_LOG(level, tag, "event-%s-%d-%d", tag, level, i);
            }
            if (errno != EBUSY) return 12;
            puts("OK");
        } else if (sscanf(command, "base %d", &level) == 1) {
            mpp_set_log_level(level);
            printf("%d\n", mpp_get_log_level());
        } else if (!strncmp(command, "level", 5)) {
            errno = EBUSY;
            level = mpp_get_log_level();
            if (errno != EBUSY) return 12;
            printf("%d\n", level);
        } else if (!strncmp(command, "probe", 5)) {
            int evaluated = 0;
            errno = EBUSY;
            MPP_LOG(MPP_LOG_DEBUG, "vdec", "argument %d", ++evaluated);
            if (errno != EBUSY) return 12;
            printf("%d\n", evaluated);
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
        } else if (!strncmp(command, "multiline", 9)) {
            MPP_LOG(MPP_LOG_INFO, "vdec", "one\nsecond\rthird\n");
            puts("OK");
        } else if (!strncmp(command, "fork", 4)) {
            pid_t child = fork();
            if (child < 0) return 15;
            if (!child) {
                if (mpp_get_log_level() != MPP_LOG_INFO) _exit(16);
                MPP_LOG(MPP_LOG_INFO, "sys", "child alive");
                _exit(0);
            }
            int status;
            waitpid(child, &status, 0);
            printf("CHILD %d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
        } else if (!strncmp(command, "quit", 4)) {
            break;
        } else return 17;
    }
    return 0;
}
