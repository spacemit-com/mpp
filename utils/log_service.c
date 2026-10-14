/*
 * Copyright (C) 2026 SpacemiT (Beijing) Technology Co. Ltd.
 * Use of this source code is governed by the MIT license in LICENSE.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "log.h"
#include "log_internal.h"
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
    int level, module;
    char text[MPP_LOG_TEXT_SIZE];
} LogRecord;
typedef struct {
    int fd, active, ready;
    uint64_t cursor, end, drops;
    int64_t deadline;
    MppLogRequest request;
} LogClient;

/* Lock order: lifetime -> clients -> ring. Producers only try-lock ring. */
static pthread_mutex_t lifetime_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t ring_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t setup_once = PTHREAD_ONCE_INIT;
static LogRecord records[MPP_LOG_RING_SIZE];
static LogClient clients[MPP_LOG_MAX_CLIENTS];
static uint64_t next_sequence;
static atomic_uint_fast64_t producer_drops;
static atomic_int subscription_levels[MPP_LOG_MOD_COUNT];
static atomic_int running;
static int listener = -1, thread_started, setup_error;
static pthread_t worker;

int mpp_log_subscription_level(int module) {
    return atomic_load_explicit(&subscription_levels[module], memory_order_relaxed);
}

void mpp_log_publish(int level, int module, const char *text) {
    if (pthread_mutex_trylock(&ring_lock) != 0) {
        atomic_fetch_add_explicit(&producer_drops, 1, memory_order_relaxed);
        return;
    }
    LogRecord *record = &records[next_sequence % MPP_LOG_RING_SIZE];
    record->level = level;
    record->module = module;
    snprintf(record->text, sizeof(record->text), "%s", text);
    ++next_sequence;
    pthread_mutex_unlock(&ring_lock);
}

static int64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void update_levels(void) {
    for (int module = 0; module < MPP_LOG_MOD_COUNT; ++module) {
        int level = 0;
        for (int i = 0; i < MPP_LOG_MAX_CLIENTS; ++i) {
            LogClient *c = &clients[i];
            /* Snapshot readers do not enable new DEBUG production. */
            if (c->fd >= 0 && c->active && c->request.follow &&
                (c->request.modules & (1U << module)) && (int)c->request.level > level)
                level = (int)c->request.level;
        }
        atomic_store_explicit(&subscription_levels[module], level, memory_order_relaxed);
    }
}

static void close_client(LogClient *c) {
    if (c->fd >= 0) close(c->fd);
    memset(c, 0, sizeof(*c));
    c->fd = -1;
}

/* Returns 1 sent, 0 retry later, -1 disconnected. Never blocks or raises SIGPIPE. */
static int send_text(int fd, const char *text) {
    size_t len = strlen(text);
    ssize_t n = send(fd, text, len, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (n == (ssize_t)len) return 1;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return 0;
    return -1;
}

static int matches(const LogRecord *r, const MppLogRequest *q) {
    return r->level <= (int)q->level && (q->modules & (1U << r->module));
}

static void accept_clients(void) {
    for (int attempt = 0; attempt < MPP_LOG_MAX_CLIENTS; ++attempt) {
        int fd = accept4(listener, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) return;
        struct ucred cred;
        socklen_t len = sizeof(cred);
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) ||
            (cred.uid != geteuid() && cred.uid != 0)) {
            close(fd);
            continue;
        }
        int slot;
        for (slot = 0; slot < MPP_LOG_MAX_CLIENTS; ++slot)
            if (clients[slot].fd < 0) break;
        if (slot == MPP_LOG_MAX_CLIENTS) {
            send_text(fd, "[mpp-log] too many subscribers\n");
            close(fd);
            continue;
        }
        int size = 32768; /* Bound the kernel buffer for a stalled terminal. */
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
        clients[slot].fd = fd;
        clients[slot].deadline = monotonic_ms() + 3000;
    }
}

static void receive_request(LogClient *c) {
    MppLogRequest q;
    ssize_t n = recv(c->fd, &q, sizeof(q), MSG_DONTWAIT | MSG_TRUNC);
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
    if (n != sizeof(q) || q.version != MPP_LOG_PROTOCOL_VERSION ||
        q.level < MPP_LOG_FATAL || q.level > MPP_LOG_VERBOSE ||
        !q.modules || (q.modules & ~MPP_LOG_ALL_MODULES) ||
        q.tail > MPP_LOG_RING_SIZE || q.follow > 1) {
        send_text(c->fd, "[mpp-log] invalid subscription request\n");
        close_client(c);
        return;
    }
    c->request = q;
    c->active = 1;
    c->ready = 0;
    c->drops = atomic_load_explicit(&producer_drops, memory_order_relaxed);
    pthread_mutex_lock(&ring_lock);
    c->end = next_sequence;
    c->cursor = next_sequence;
    uint64_t oldest = next_sequence > MPP_LOG_RING_SIZE ? next_sequence - MPP_LOG_RING_SIZE : 0;
    unsigned found = 0;
    while (c->cursor > oldest && found < q.tail) {
        --c->cursor;
        if (matches(&records[c->cursor % MPP_LOG_RING_SIZE], &q)) ++found;
    }
    pthread_mutex_unlock(&ring_lock);
}

static void drain_client(LogClient *c) {
    if (!c->ready) {
        int rc = send_text(c->fd, MPP_LOG_READY);
        if (rc < 0) close_client(c);
        if (rc != 1) return;
        c->ready = 1;
    }
    uint64_t drops = atomic_load_explicit(&producer_drops, memory_order_relaxed);
    if (drops != c->drops) {
        char notice[128];
        snprintf(notice, sizeof(notice), "[mpp-log] dropped %" PRIu64 " records (producer contention)\n",
            drops - c->drops);
        int rc = send_text(c->fd, notice);
        if (rc < 0) close_client(c);
        if (rc != 1) return;
        c->drops = drops;
    }
    /* Per-client work budget prevents a noisy reader starving other readers. */
    for (int budget = 0; budget < 64; ++budget) {
        LogRecord record;
        pthread_mutex_lock(&ring_lock);
        uint64_t end = c->request.follow ? next_sequence : c->end;
        uint64_t oldest = next_sequence > MPP_LOG_RING_SIZE ? next_sequence - MPP_LOG_RING_SIZE : 0;
        if (oldest > end) oldest = end;
        if (c->cursor < oldest) {
            uint64_t lost = oldest - c->cursor;
            pthread_mutex_unlock(&ring_lock);
            char notice[128];
            snprintf(notice, sizeof(notice), "[mpp-log] dropped %" PRIu64 " records (history overwritten)\n", lost);
            int rc = send_text(c->fd, notice);
            if (rc < 0) close_client(c);
            if (rc != 1) return;
            c->cursor = oldest;
            continue;
        }
        if (c->cursor >= end) {
            pthread_mutex_unlock(&ring_lock);
            if (!c->request.follow) {
                int rc = send_text(c->fd, MPP_LOG_END);
                if (rc != 0) close_client(c);
            }
            return;
        }
        record = records[c->cursor % MPP_LOG_RING_SIZE];
        pthread_mutex_unlock(&ring_lock);
        if (matches(&record, &c->request)) {
            int rc = send_text(c->fd, record.text);
            if (rc < 0) close_client(c);
            if (rc != 1) return;
        }
        ++c->cursor;
    }
}

static void *serve(void *unused) {
    (void)unused;
    while (atomic_load_explicit(&running, memory_order_relaxed)) {
        struct pollfd fds[MPP_LOG_MAX_CLIENTS + 1];
        pthread_mutex_lock(&clients_lock);
        fds[0] = (struct pollfd){.fd = listener, .events = POLLIN};
        for (int i = 0; i < MPP_LOG_MAX_CLIENTS; ++i)
            fds[i + 1] = (struct pollfd){.fd = clients[i].fd, .events = POLLIN};
        pthread_mutex_unlock(&clients_lock);
        int rc = poll(fds, MPP_LOG_MAX_CLIENTS + 1, 10);
        if (rc < 0 && errno != EINTR) break;
        pthread_mutex_lock(&clients_lock);
        for (int i = 0; i < MPP_LOG_MAX_CLIENTS; ++i) {
            LogClient *c = &clients[i];
            if (c->fd < 0) continue;
            short revents = fds[i + 1].revents;
            if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
                close_client(c);
            } else if (revents & POLLIN) {
                if (!c->active) receive_request(c);
                else close_client(c); /* Only one request per connection. */
            } else if (!c->active && monotonic_ms() >= c->deadline) {
                close_client(c);
            }
        }
        if (fds[0].revents & POLLIN) accept_clients();
        update_levels(); /* Enable requested levels before acknowledging READY. */
        for (int i = 0; i < MPP_LOG_MAX_CLIENTS; ++i)
            if (clients[i].fd >= 0 && clients[i].active) drain_client(&clients[i]);
        update_levels();
        pthread_mutex_unlock(&clients_lock);
    }
    return NULL;
}

/* Fork must not leave inherited locks, dead threads, sockets or DEBUG leases.
 * Child may explicitly start its own service (also done by SYS_Init). */
static void fork_prepare(void) {
    pthread_mutex_lock(&lifetime_lock);
    pthread_mutex_lock(&clients_lock);
    pthread_mutex_lock(&ring_lock);
}
static void fork_parent(void) {
    pthread_mutex_unlock(&ring_lock);
    pthread_mutex_unlock(&clients_lock);
    pthread_mutex_unlock(&lifetime_lock);
}
static void fork_child(void) {
    if (listener >= 0) close(listener);
    listener = -1;
    for (int i = 0; i < MPP_LOG_MAX_CLIENTS; ++i) {
        if (clients[i].fd >= 0) close(clients[i].fd);
        clients[i].fd = -1;
        clients[i].active = 0;
    }
    for (int i = 0; i < MPP_LOG_MOD_COUNT; ++i)
        atomic_store_explicit(&subscription_levels[i], 0, memory_order_relaxed);
    atomic_store_explicit(&running, 0, memory_order_relaxed);
    atomic_store_explicit(&producer_drops, 0, memory_order_relaxed);
    next_sequence = 0;
    thread_started = 0;
    fork_parent();
}
static void setup(void) {
    for (int i = 0; i < MPP_LOG_MAX_CLIENTS; ++i) clients[i].fd = -1;
    setup_error = pthread_atfork(fork_prepare, fork_parent, fork_child);
}

int mpp_log_start(void) {
    pthread_once(&setup_once, setup);
    if (setup_error) { errno = setup_error; return -1; }
    pthread_mutex_lock(&lifetime_lock);
    if (thread_started) { pthread_mutex_unlock(&lifetime_lock); return 0; }
    listener = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listener < 0) goto fail;
    struct sockaddr_un addr;
    socklen_t len = mpp_log_address(&addr, (unsigned)geteuid(), (unsigned)getpid());
    if (bind(listener, (struct sockaddr *)&addr, len) || listen(listener, MPP_LOG_MAX_CLIENTS))
        goto fail;
    atomic_store_explicit(&running, 1, memory_order_relaxed);
    int rc = pthread_create(&worker, NULL, serve, NULL);
    if (rc) { errno = rc; goto fail; }
    thread_started = 1;
    pthread_mutex_unlock(&lifetime_lock);
    return 0;
fail: {
    int saved = errno;
    if (listener >= 0) close(listener);
    listener = -1;
    atomic_store_explicit(&running, 0, memory_order_relaxed);
    pthread_mutex_unlock(&lifetime_lock);
    errno = saved;
    return -1;
}
}

void mpp_log_stop(void) {
    pthread_mutex_lock(&lifetime_lock);
    if (thread_started) {
        atomic_store_explicit(&running, 0, memory_order_relaxed);
        pthread_join(worker, NULL);
        pthread_mutex_lock(&clients_lock);
        for (int i = 0; i < MPP_LOG_MAX_CLIENTS; ++i) close_client(&clients[i]);
        update_levels();
        close(listener);
        listener = -1;
        thread_started = 0;
        pthread_mutex_unlock(&clients_lock);
    }
    pthread_mutex_unlock(&lifetime_lock);
}

__attribute__((destructor)) static void log_shutdown(void) {
    mpp_log_stop();
}
