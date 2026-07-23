#include "vdec_capture_state.h"

void vdec_capture_snapshot_for_reconfigure(BOOL *in_decoder,
                                            BOOL *was_in_decoder,
                                            U32 count) {
    if (!in_decoder || !was_in_decoder)
        return;

    for (U32 i = 0; i < count; i++) {
        was_in_decoder[i] = in_decoder[i];
        in_decoder[i] = MPP_FALSE;
    }
}

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
                                    BOOL eos_reserved,
                                    BOOL recycle_run,
                                    BOOL pool_reconfig,
                                    BOOL same_pool) {
    if (!has_decoder_ref || eos_reserved || !recycle_run || pool_reconfig ||
        !same_pool)
        return MPP_FALSE;

    *has_decoder_ref = MPP_TRUE;
    return MPP_TRUE;
}

BOOL vdec_capture_reserve_eos_slot(BOOL *in_decoder,
                                    BOOL *has_decoder_ref,
                                    BOOL *eos_reserved) {
    if (!in_decoder || !has_decoder_ref || !eos_reserved ||
        !*in_decoder || !*has_decoder_ref || *eos_reserved)
        return MPP_FALSE;

    *in_decoder = MPP_FALSE;
    *has_decoder_ref = MPP_FALSE;
    *eos_reserved = MPP_TRUE;
    return MPP_TRUE;
}

void vdec_capture_reset_eos_reservation(BOOL *eos_reserved) {
    if (eos_reserved)
        *eos_reserved = MPP_FALSE;
}

BOOL vdec_capture_eos_index_valid(U32 index, U32 slot_count) {
    return slot_count > 0 && index < slot_count;
}

U64 vdec_capture_generation_advance(U64 generation) {
    return generation + 1;
}

BOOL vdec_capture_generation_is_current(U64 generation,
                                        U64 frame_generation) {
    return generation == frame_generation;
}
