#ifndef VDEC_CAPTURE_STATE_H
#define VDEC_CAPTURE_STATE_H

#include "sys/type.h"
#include "para.h"

void vdec_capture_snapshot_for_reconfigure(BOOL *in_decoder,
                                            BOOL *was_in_decoder,
                                            U32 count);

BOOL vdec_capture_should_requeue(BOOL was_in_decoder,
                                BOOL had_decoder_ref,
                                UL vb_buffer);

void vdec_capture_apply_queue_result(BOOL *in_decoder, S32 queue_result);

BOOL vdec_capture_claim_recycled_ref(BOOL *has_decoder_ref,
                                    BOOL eos_reserved,
                                    BOOL recycle_run,
                                    BOOL pool_reconfig,
                                    BOOL same_pool);

BOOL vdec_capture_reserve_eos_slot(BOOL *in_decoder,
                                    BOOL *has_decoder_ref,
                                    BOOL *eos_reserved);

void vdec_capture_reset_eos_reservation(BOOL *eos_reserved);

BOOL vdec_capture_eos_index_valid(U32 index, U32 slot_count);

U64 vdec_capture_generation_advance(U64 generation);

BOOL vdec_capture_generation_is_current(U64 generation,
                                        U64 frame_generation);

#endif
