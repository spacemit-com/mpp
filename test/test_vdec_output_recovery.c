#include <stdio.h>
#include <string.h>

#include "vdec_output_recovery.h"

static int expect(const char *name, BOOL condition) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", name);
        return 1;
    }
    printf("[PASS] %s\n", name);
    return 0;
}

int main(void) {
    int failures = 0;
    VdecOutputRecoveryState state;

    memset(&state, 0, sizeof(state));
    failures += expect("idle state is not due", !vdec_output_recovery_due(&state, 5000));

    vdec_output_recovery_record_error(&state, 100);
    failures += expect(
        "error arms delayed recovery",
        state.bArmed && state.u64DeadlineMs == 100 + VDEC_OUTPUT_RECOVERY_DELAY_MS);
    failures += expect(
        "recovery waits for deadline",
        !vdec_output_recovery_due(&state, state.u64DeadlineMs - 1));

    vdec_output_recovery_record_error(&state, 500);
    failures += expect(
        "error burst keeps first deadline",
        state.u64DeadlineMs == 100 + VDEC_OUTPUT_RECOVERY_DELAY_MS);

    failures += expect(
        "recovery fires once at deadline",
        vdec_output_recovery_due(&state, 100 + VDEC_OUTPUT_RECOVERY_DELAY_MS));
    failures += expect("fired recovery disarms", !vdec_output_recovery_due(&state, 5000));

    vdec_output_recovery_record_error(&state, 2000);
    vdec_output_recovery_record_frame(&state);
    failures += expect("valid frame cancels recovery", !vdec_output_recovery_due(&state, 5000));

    vdec_output_recovery_record_error(NULL, 0);
    vdec_output_recovery_record_frame(NULL);
    failures += expect("null state is safe", !vdec_output_recovery_due(NULL, 0));

    return failures ? 1 : 0;
}
