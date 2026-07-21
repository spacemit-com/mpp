#include <stdio.h>
#include <string.h>

#include "para.h"
#include "vdec/vdec_type.h"
#include "vdec_input_retry.h"

typedef struct _FakeSubmit {
    S32 results[8];
    U32 resultCount;
    U32 calls;
    U64 nowMs;
    U32 sleeps;
} FakeSubmit;

static S32 fake_try_submit(VOID *opaque) {
    FakeSubmit *fake = (FakeSubmit *)opaque;
    U32 index = fake->calls < fake->resultCount ? fake->calls : fake->resultCount - 1;
    fake->calls++;
    return fake->results[index];
}

static U64 fake_now_ms(VOID *opaque) {
    return ((FakeSubmit *)opaque)->nowMs;
}

static VOID fake_sleep_us(U32 delayUs, VOID *opaque) {
    FakeSubmit *fake = (FakeSubmit *)opaque;
    fake->nowMs += (delayUs + 999U) / 1000U;
    fake->sleeps++;
}

static int expect(const char *name, BOOL condition) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", name);
        return 1;
    }
    printf("[PASS] %s\n", name);
    return 0;
}

static VdecInputRetryOps fake_ops(FakeSubmit *fake) {
    VdecInputRetryOps ops = {
        .trySubmit = fake_try_submit,
        .nowMs = fake_now_ms,
        .sleepUs = fake_sleep_us,
        .opaque = fake,
    };
    return ops;
}

int main(void) {
    int failures = 0;
    FakeSubmit fake;
    VdecInputRetryOps ops;
    S32 ret;

    memset(&fake, 0, sizeof(fake));
    fake.results[0] = MPP_DATAQUEUE_FULL;
    fake.results[1] = MPP_DATAQUEUE_FULL;
    fake.results[2] = MPP_OK;
    fake.resultCount = 3;
    ops = fake_ops(&fake);
    ret = vdec_input_submit_with_timeout(&ops, 20);
    failures += expect("retry preserves packet until accepted", ret == MPP_OK && fake.calls == 3 && fake.sleeps == 2);

    memset(&fake, 0, sizeof(fake));
    fake.results[0] = MPP_POLL_FAILED;
    fake.resultCount = 1;
    ops = fake_ops(&fake);
    ret = vdec_input_submit_with_timeout(&ops, (U32)-1);
    failures += expect(
        "fatal poll error is not retried", ret == MPP_POLL_FAILED && fake.calls == 1 && fake.sleeps == 0);

    memset(&fake, 0, sizeof(fake));
    fake.results[0] = MPP_DATAQUEUE_FULL;
    fake.resultCount = 1;
    ops = fake_ops(&fake);
    ret = vdec_input_submit_with_timeout(&ops, 0);
    failures += expect("nonblocking backpressure returns busy", ret == ERR_VDEC_BUSY && fake.calls == 1);

    memset(&fake, 0, sizeof(fake));
    fake.results[0] = MPP_DATAQUEUE_FULL;
    fake.resultCount = 1;
    ops = fake_ops(&fake);
    ret = vdec_input_submit_with_timeout(&ops, 5);
    failures += expect(
        "finite backpressure wait times out", ret == ERR_VDEC_TIMEOUT && fake.nowMs >= 5 && fake.calls == 3);

    memset(&fake, 0, sizeof(fake));
    fake.results[0] = MPP_DATAQUEUE_FULL;
    fake.results[1] = ERR_VDEC_NOT_STARTED;
    fake.resultCount = 2;
    ops = fake_ops(&fake);
    ret = vdec_input_submit_with_timeout(&ops, (U32)-1);
    failures += expect("infinite wait remains interruptible", ret == ERR_VDEC_NOT_STARTED && fake.calls == 2);

    return failures ? 1 : 0;
}
