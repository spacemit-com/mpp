/* Copyright 2026 SPACEMIT. All rights reserved. BSD-style license: LICENSE.
 * Script poll and monotonic time; execute the actual AL input wait. */
#include <poll.h>
#include <time.h>
static int test_poll(struct pollfd *fds, nfds_t count, int timeout);
static int test_clock(clockid_t clock, struct timespec *ts);
#define poll test_poll
#define clock_gettime test_clock
#include "../../al/vcodec/linlonv5v7/linlonv5v7_dec.c"
#undef clock_gettime
#undef poll

#define REQUIRE(x)                                                                                                     \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x);                                                       \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

typedef struct {
    int wait, result, error;
    short events;
    U32 elapsed;
} Step;
static const Step *steps;
static U32 stepCount, stepIndex;
static U64 nowMs;
static int test_clock(clockid_t clock, struct timespec *ts) {
    REQUIRE(clock == CLOCK_MONOTONIC);
    ts->tv_sec = (time_t)(nowMs / 1000U);
    ts->tv_nsec = (long)(nowMs % 1000U) * 1000000L;
    return 0;
}
static int test_poll(struct pollfd *fds, nfds_t count, int timeout) {
    REQUIRE(count == 1 && fds[0].events == POLLOUT && stepIndex < stepCount);
    const Step *step = &steps[stepIndex++];
    REQUIRE(timeout == step->wait);
    nowMs += step->elapsed;
    fds[0].revents = step->events;
    errno = step->error;
    return step->result;
}
static void check(U32 timeout, const Step *script, U32 count, S32 expected) {
    ALLinlonv5v7DecContext ctx = {.nVideoFd = 42};
    steps = script;
    stepCount = count;
    stepIndex = 0;
    nowMs = 1000;
    REQUIRE(waitInputReady(&ctx, timeout) == expected && stepIndex == count);
}

int main(void) {
    const Step empty[] = {{.wait = 0}};
    const Step ready[] = {{.wait = 0, .result = 1, .events = POLLOUT}};
    const Step finite[] = {{.wait = 10, .elapsed = 10}};
    const Step interrupted[] = {
        {.wait = 10, .result = -1, .error = EINTR, .elapsed = 4},
        {.wait = 6, .elapsed = 6},
    };
    const Step expired[] = {{.wait = 10, .result = -1, .error = EINTR, .elapsed = 12}};
    const Step interruptedReady[] = {
        {.wait = 10, .result = -1, .error = EINTR, .elapsed = 3},
        {.wait = 7, .result = 1, .events = POLLOUT},
    };
    const Step large[] = {{.wait = INT_MAX, .elapsed = INT_MAX}};
    const Step failed[] = {{.wait = 0, .result = -1, .error = EBADF}};
    check(0, empty, 1, MPP_DATAQUEUE_FULL);
    check(0, ready, 1, MPP_OK);
    check(10, finite, 1, MPP_DATAQUEUE_FULL);
    check(10, interrupted, 2, MPP_DATAQUEUE_FULL);
    check(10, expired, 1, MPP_DATAQUEUE_FULL);
    check(10, interruptedReady, 2, MPP_OK);
    check(INT_MAX, large, 1, MPP_DATAQUEUE_FULL);
    check(0, failed, 1, MPP_POLL_FAILED);
    const short errors[] = {POLLERR, POLLHUP, POLLNVAL};
    for (U32 i = 0; i < 3; ++i) {
        Step step = {.wait = 0, .result = 1, .events = errors[i] | POLLOUT};
        check(0, &step, 1, MPP_POLL_FAILED);
    }
    puts("[PASS] AL nonblocking/finite waits, EINTR deadlines and fd errors");
    return 0;
}
