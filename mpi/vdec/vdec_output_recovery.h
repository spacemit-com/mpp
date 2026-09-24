#ifndef VDEC_OUTPUT_RECOVERY_H
#define VDEC_OUTPUT_RECOVERY_H

#include "sys/type.h"

#define VDEC_OUTPUT_RECOVERY_DELAY_MS 1000U

typedef struct _VdecOutputRecoveryState {
    BOOL bArmed;
    U64 u64DeadlineMs;
} VdecOutputRecoveryState;

__attribute__((visibility("hidden"))) VOID vdec_output_recovery_record_error(
    VdecOutputRecoveryState *pstState, U64 u64NowMs);
__attribute__((visibility("hidden"))) VOID vdec_output_recovery_record_frame(
    VdecOutputRecoveryState *pstState);
__attribute__((visibility("hidden"))) BOOL vdec_output_recovery_due(
    VdecOutputRecoveryState *pstState, U64 u64NowMs);

#endif /* VDEC_OUTPUT_RECOVERY_H */
