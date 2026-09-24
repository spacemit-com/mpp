#include "vdec_output_recovery.h"

VOID vdec_output_recovery_record_error(VdecOutputRecoveryState *pstState, U64 u64NowMs) {
    if (!pstState || pstState->bArmed)
        return;

    pstState->bArmed = MPP_TRUE;
    pstState->u64DeadlineMs = u64NowMs + VDEC_OUTPUT_RECOVERY_DELAY_MS;
}

VOID vdec_output_recovery_record_frame(VdecOutputRecoveryState *pstState) {
    if (!pstState)
        return;

    pstState->bArmed = MPP_FALSE;
    pstState->u64DeadlineMs = 0;
}

BOOL vdec_output_recovery_due(VdecOutputRecoveryState *pstState, U64 u64NowMs) {
    if (!pstState || !pstState->bArmed || u64NowMs < pstState->u64DeadlineMs)
        return MPP_FALSE;

    pstState->bArmed = MPP_FALSE;
    pstState->u64DeadlineMs = 0;
    return MPP_TRUE;
}
