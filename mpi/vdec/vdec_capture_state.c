#include "vdec_capture_state.h"

BOOL vdec_capture_should_requeue(BOOL was_in_decoder,
                                BOOL had_decoder_ref,
                                UL vb_buffer) {
    return was_in_decoder && had_decoder_ref && vb_buffer != 0;
}

void vdec_capture_apply_queue_result(BOOL *in_decoder, S32 queue_result) {
    if (in_decoder)
        *in_decoder = (queue_result == MPP_OK) ? MPP_TRUE : MPP_FALSE;
}

BOOL vdec_capture_claim_recycled_ref(BOOL *has_decoder_ref,
                                    BOOL recycle_run,
                                    BOOL pool_reconfig,
                                    BOOL same_pool) {
    if (!has_decoder_ref || !recycle_run || pool_reconfig || !same_pool)
        return MPP_FALSE;

    *has_decoder_ref = MPP_TRUE;
    return MPP_TRUE;
}
