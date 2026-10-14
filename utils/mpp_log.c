/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license in LICENSE.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "log.h"
#include "log_protocol.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

static volatile sig_atomic_t stopped;
static void on_signal(int signo) { (void)signo; stopped = 1; }

static void usage(FILE *out) {
    fprintf(out,
        "Usage: mpp-log list\n"
        "       mpp-log [-p PID] [-m MODULE,...] [-l LEVEL] [-n COUNT] [-f]\n"
        "  -p, --pid PID       Only this process; default: all accessible MPP processes\n"
        "  -m, --module LIST   all (default), sys,vb,vi,vo,uvc,vdec,venc,\n"
        "                      demux,mux,v2d,al,utils,cpp,other\n"
        "  -l, --level LEVEL   fatal,error,warn,info (default),debug,verbose\n"
        "  -n, --tail COUNT    Last matching records per process (0..512)\n"
        "  -f, --follow        Subscribe until Ctrl+C; temporarily enable LEVEL\n"
        "Without -f, print last 100 records per process and exit.\nWith -f, default tail is 0; without -p, also discover new processes.\n");
}

static int number(const char *text, unsigned max, unsigned *result) {
    if (!text || !*text || *text == '-') return -1;
    char *end;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno || *end || value > max) return -1;
    *result = (unsigned)value;
    return 0;
}

static int parse_modules(const char *text, uint32_t *mask) {
    char *copy = strdup(text);
    if (!copy) return -1;
    *mask = 0;
    char *save, *name;
    for (name = strtok_r(copy, ",", &save); name; name = strtok_r(NULL, ",", &save)) {
        if (!strcasecmp(name, "all")) { *mask = MPP_LOG_ALL_MODULES; continue; }
        int i;
        for (i = 0; i < MPP_LOG_MOD_COUNT; ++i)
            if (!strcasecmp(name, mpp_log_module_names[i])) { *mask |= 1U << i; break; }
        if (i == MPP_LOG_MOD_COUNT) { free(copy); return -1; }
    }
    free(copy);
    return *mask ? 0 : -1;
}

enum { DISCOVERY_MS = 1000, SUBSCRIBE_MS = 3000, RETRY_MS = 3000 };

typedef struct {
    unsigned pid, uid;
    int fd, ready, seen, attempted;
    int64_t deadline, retry_at;
} LogTarget;

typedef struct {
    LogTarget *items;
    size_t count, capacity;
} TargetList;

static int64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static LogTarget *add_target(TargetList *list, unsigned pid, unsigned uid) {
    if (list->count == list->capacity) {
        if (list->capacity > SIZE_MAX / 2 / sizeof(*list->items)) {
            errno = ENOMEM;
            return NULL;
        }
        size_t capacity = list->capacity ? list->capacity * 2 : 8;
        LogTarget *items = realloc(list->items, capacity * sizeof(*items));
        if (!items) return NULL;
        list->items = items;
        list->capacity = capacity;
    }
    LogTarget *target = &list->items[list->count++];
    *target = (LogTarget){.pid = pid, .uid = uid, .fd = -1, .seen = 1};
    return target;
}

/* Retain active connections across scans; discard vanished, disconnected PIDs. */
static int discover_targets(TargetList *list) {
    FILE *fp = fopen("/proc/net/unix", "r");
    if (!fp) return -1;
    for (size_t i = 0; i < list->count; ++i) list->items[i].seen = 0;
    char line[512];
    int saved = 0;
    while (fgets(line, sizeof(line), fp)) {
        char *address = strstr(line, "@" MPP_LOG_SOCKET_PREFIX);
        unsigned uid, pid;
        char extra;
        if (!address || sscanf(address, "@" MPP_LOG_SOCKET_PREFIX "%u.%u %c", &uid, &pid, &extra) != 2)
            continue;
        if (!pid || pid > INT_MAX || (geteuid() != 0 && uid != geteuid())) continue;
        char path[64];
        struct stat st;
        snprintf(path, sizeof(path), "/proc/%u", pid);
        if (stat(path, &st) || st.st_uid != uid) continue;
        size_t i;
        for (i = 0; i < list->count; ++i) {
            LogTarget *target = &list->items[i];
            if (target->pid == pid && target->uid == uid) {
                target->seen = 1;
                break;
            }
        }
        if (i == list->count && !add_target(list, pid, uid)) {
            saved = errno;
            break;
        }
    }
    if (ferror(fp) && !saved) saved = errno ? errno : EIO;
    fclose(fp);
    if (saved) { errno = saved; return -1; }
    for (size_t i = 0; i < list->count;) {
        if (!list->items[i].seen && list->items[i].fd < 0)
            list->items[i] = list->items[--list->count];
        else
            ++i;
    }
    return 0;
}

static int list_processes(void) {
    TargetList list = {0};
    if (discover_targets(&list)) {
        perror("mpp-log: discover");
        free(list.items);
        return 1;
    }
    printf("PID\tUID\tCOMMAND\n");
    for (size_t i = 0; i < list.count; ++i) {
        LogTarget *target = &list.items[i];
        char path[64], command[128] = "?";
        snprintf(path, sizeof(path), "/proc/%u/comm", target->pid);
        FILE *comm = fopen(path, "r");
        if (comm) {
            if (fgets(command, sizeof(command), comm)) command[strcspn(command, "\r\n")] = '\0';
            fclose(comm);
        }
        printf("%u\t%u\t%s\n", target->pid, target->uid, command);
    }
    free(list.items);
    return 0;
}

static int connect_process(unsigned pid) {
    char path[64];
    struct stat st;
    snprintf(path, sizeof(path), "/proc/%u", pid);
    if (stat(path, &st)) return -1;
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr;
    socklen_t len = mpp_log_address(&addr, (unsigned)st.st_uid, pid);
    if (connect(fd, (struct sockaddr *)&addr, len)) {
        /* Local abstract sockets return EAGAIN when the accept queue is full. */
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    struct ucred peer;
    len = sizeof(peer);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &len) ||
        peer.pid != (pid_t)pid || peer.uid != st.st_uid) {
        close(fd);
        errno = EACCES;
        return -1;
    }
    return fd;
}

static void close_target(LogTarget *target) {
    if (target->fd >= 0) close(target->fd);
    target->fd = -1;
    target->ready = 0;
    target->retry_at = monotonic_ms() + RETRY_MS;
}

static int attach_target(LogTarget *target, const MppLogRequest *request) {
    target->attempted = 1;
    target->fd = connect_process(target->pid);
    if (target->fd < 0) {
        fprintf(stderr, "mpp-log: PID %u: connect: %s\n", target->pid, strerror(errno));
        close_target(target);
        return -1;
    }
    ssize_t n;
    do {
        n = send(target->fd, request, sizeof(*request), MSG_NOSIGNAL);
    } while (n < 0 && errno == EINTR && !stopped);
    if (n != sizeof(*request)) {
        fprintf(stderr, "mpp-log: PID %u: subscribe: %s\n", target->pid,
                n < 0 ? strerror(errno) : "incomplete request");
        close_target(target);
        return -1;
    }
    target->deadline = monotonic_ms() + SUBSCRIBE_MS;
    return 0;
}

enum { READ_KEEP, READ_DONE, READ_FAILED, OUTPUT_CLOSED, OUTPUT_FAILED };

/* Read one packet per target per poll round so busy sources cannot starve peers. */
static int read_target(LogTarget *target, const MppLogRequest *request, int aggregate) {
    char text[MPP_LOG_TEXT_SIZE + 1];
    ssize_t n = recv(target->fd, text, sizeof(text) - 1, MSG_TRUNC);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) return READ_KEEP;
    if (n == 0 && target->ready && request->follow) {
        fprintf(stderr, "mpp-log: PID %u: target disconnected\n", target->pid);
        return READ_DONE;
    }
    if (n <= 0 || (size_t)n >= sizeof(text)) {
        fprintf(stderr, "mpp-log: PID %u: subscription ended unexpectedly or invalid packet\n", target->pid);
        return READ_FAILED;
    }
    text[n] = '\0';
    if (!strcmp(text, MPP_LOG_READY)) {
        target->ready = 1;
        if (request->follow)
            fprintf(stderr, "mpp-log: attached to %u; Ctrl+C to stop viewing\n", target->pid);
        return READ_KEEP;
    }
    if (!target->ready) {
        fprintf(stderr, "mpp-log: PID %u: %s", target->pid, text);
        return READ_FAILED;
    }
    if (!strcmp(text, MPP_LOG_END)) return READ_DONE;
    /* Normal records already contain PID/TID; identify transport notices too. */
    int rc = aggregate && !strncmp(text, "[mpp-log]", 9)
        ? fprintf(stdout, "[pid %u] %s", target->pid, text) : fputs(text, stdout);
    if (rc < 0 || fflush(stdout) == EOF)
        return errno == EPIPE || stopped ? OUTPUT_CLOSED : OUTPUT_FAILED;
    return READ_KEEP;
}

static int view_logs(unsigned pid, const MppLogRequest *request) {
    TargetList list = {0};
    struct pollfd *fds = NULL;
    size_t poll_capacity = 0;
    int watch = !pid && request->follow;
    int status = 0;
    if (pid ? !add_target(&list, pid, 0) : discover_targets(&list) != 0) {
        perror("mpp-log: discover");
        status = 1;
        goto done;
    }
    if (!list.count) {
        fprintf(stderr, "mpp-log: no accessible MPP processes%s\n",
                watch ? "; waiting for processes, Ctrl+C to stop viewing" : "");
        if (!watch) { status = 1; goto done; }
    }
    int64_t next_scan = monotonic_ms() + DISCOVERY_MS;
    while (!stopped) {
        int64_t now = monotonic_ms();
        if (watch && now >= next_scan) {
            if (discover_targets(&list)) {
                perror("mpp-log: discover");
                status = 1;
                break;
            }
            next_scan = monotonic_ms() + DISCOVERY_MS;
        }
        if (list.count > poll_capacity) {
            struct pollfd *new_fds = realloc(fds, list.capacity * sizeof(*fds));
            if (!new_fds) { perror("mpp-log: poll allocation"); status = 1; break; }
            fds = new_fds;
            poll_capacity = list.capacity;
        }
        size_t active = 0;
        for (size_t i = 0; i < list.count && !stopped; ++i) {
            LogTarget *target = &list.items[i];
            if (target->fd < 0 && (!target->attempted ||
                (watch && target->seen && now >= target->retry_at))) {
                if (attach_target(target, request) && !watch) status = 1;
            }
            fds[i] = (struct pollfd){.fd = target->fd, .events = POLLIN};
            if (target->fd >= 0) ++active;
        }
        if (stopped || (!watch && !active)) break;
        now = monotonic_ms();
        int timeout = watch ? (int)(next_scan > now ? next_scan - now : 0) : -1;
        for (size_t i = 0; i < list.count; ++i) {
            LogTarget *target = &list.items[i];
            if (target->fd >= 0 && !target->ready) {
                int remaining = target->deadline > now ? (int)(target->deadline - now) : 0;
                if (timeout < 0 || remaining < timeout) timeout = remaining;
            }
        }
        int rc = poll(fds, list.count, timeout);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) { perror("mpp-log: poll"); status = 1; break; }
        now = monotonic_ms();
        for (size_t i = 0; i < list.count && !stopped; ++i) {
            LogTarget *target = &list.items[i];
            if (target->fd < 0) continue;
            if (fds[i].revents) {
                int result = read_target(target, request, !pid);
                if (result == OUTPUT_CLOSED || result == OUTPUT_FAILED) {
                    status = result == OUTPUT_FAILED;
                    goto done;
                }
                if (result == READ_DONE || result == READ_FAILED) {
                    if (result == READ_FAILED && !watch) status = 1;
                    close_target(target);
                    continue;
                }
            }
            if (!target->ready && now >= target->deadline) {
                fprintf(stderr, "mpp-log: PID %u: subscription timed out\n", target->pid);
                if (!watch) status = 1;
                close_target(target);
            }
        }
    }
done:
    for (size_t i = 0; i < list.count; ++i)
        if (list.items[i].fd >= 0) close(list.items[i].fd);
    free(fds);
    free(list.items);
    return status;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "list")) return list_processes();
    MppLogRequest q = {.version = MPP_LOG_PROTOCOL_VERSION, .level = MPP_LOG_INFO,
        .modules = MPP_LOG_ALL_MODULES};
    unsigned pid = 0;
    int tail_set = 0, option;
    static const struct option options[] = {
        {"pid", required_argument, NULL, 'p'}, {"module", required_argument, NULL, 'm'},
        {"level", required_argument, NULL, 'l'}, {"tail", required_argument, NULL, 'n'},
        {"follow", no_argument, NULL, 'f'}, {"help", no_argument, NULL, 'h'}, {0}
    };
    while ((option = getopt_long(argc, argv, "p:m:l:n:fh", options, NULL)) != -1) {
        switch (option) {
        case 'p':
            if (number(optarg, INT_MAX, &pid) || !pid) goto bad;
            break;
        case 'm':
            if (parse_modules(optarg, &q.modules)) goto bad;
            break;
        case 'l': {
            static const char *const levels[] = {"fatal", "error", "warn", "info", "debug", "verbose"};
            unsigned i;
            for (i = 0; i < 6; ++i) if (!strcasecmp(optarg, levels[i])) break;
            if (i == 6) goto bad;
            q.level = i + 1;
            break;
        }
        case 'n': {
            unsigned count;
            if (number(optarg, MPP_LOG_RING_SIZE, &count)) goto bad;
            q.tail = count;
            tail_set = 1;
            break;
        }
        case 'f': q.follow = 1; break;
        case 'h': usage(stdout); return 0;
        default: goto bad;
        }
    }
    if (optind != argc) goto bad;
    if (!tail_set) q.tail = q.follow ? 0 : 100;
    struct sigaction action = {.sa_handler = on_signal};
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IOLBF, 0);
    return view_logs(pid, &q);
bad:
    usage(stderr);
    return 2;
}
