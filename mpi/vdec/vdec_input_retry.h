#ifndef VDEC_INPUT_RETRY_H
#define VDEC_INPUT_RETRY_H

#include "sys/type.h"

typedef S32 (*VdecInputTryFn)(VOID *opaque);
typedef U64 (*VdecInputNowMsFn)(VOID *opaque);
typedef VOID (*VdecInputSleepUsFn)(U32 delayUs, VOID *opaque);

typedef struct _VdecInputRetryOps {
    VdecInputTryFn trySubmit;
    VdecInputNowMsFn nowMs;
    VdecInputSleepUsFn sleepUs;
    VOID *opaque;
} VdecInputRetryOps;

__attribute__((visibility("hidden"))) S32 vdec_input_submit_with_timeout(
    const VdecInputRetryOps *ops, U32 timeoutMs);

#endif /* VDEC_INPUT_RETRY_H */
