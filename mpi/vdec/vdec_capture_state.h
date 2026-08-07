#ifndef VDEC_CAPTURE_STATE_H
#define VDEC_CAPTURE_STATE_H

#include "sys/type.h"
#include "para.h"

BOOL vdec_capture_should_requeue(BOOL was_in_decoder,
                                BOOL had_decoder_ref,
                                UL vb_buffer);

void vdec_capture_apply_queue_result(BOOL *in_decoder, S32 queue_result);

BOOL vdec_capture_claim_recycled_ref(BOOL *has_decoder_ref,
                                    BOOL recycle_run,
                                    BOOL pool_reconfig,
                                    BOOL same_pool);

#endif
