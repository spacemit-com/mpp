#include "vdec_input_retry.h"

#include "para.h"
#include "vdec/vdec_type.h"

#define VDEC_INPUT_RETRY_US 2000U

S32 vdec_input_submit_with_timeout(const VdecInputRetryOps *ops, U32 timeoutMs) {
    const BOOL waitForever = timeoutMs == (U32)-1;
    U64 deadlineMs;

    if (!ops || !ops->trySubmit || !ops->nowMs || !ops->sleepUs)
        return ERR_VDEC_NULL_PTR;

    deadlineMs = waitForever ? 0 : ops->nowMs(ops->opaque) + timeoutMs;
    for (;;) {
        S32 ret = ops->trySubmit(ops->opaque);
        if (ret != MPP_DATAQUEUE_FULL)
            return ret;
        if (timeoutMs == 0)
            return ERR_VDEC_BUSY;
        if (!waitForever && ops->nowMs(ops->opaque) >= deadlineMs)
            return ERR_VDEC_TIMEOUT;
        ops->sleepUs(VDEC_INPUT_RETRY_US, ops->opaque);
        if (!waitForever && ops->nowMs(ops->opaque) >= deadlineMs)
            return ERR_VDEC_TIMEOUT;
    }
}
