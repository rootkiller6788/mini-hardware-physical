#include <stdio.h>
#include "sequential.h"

int main(void) {
    printf("===== 4-bit Binary Counter =====\n\n");
    Register counter = reg_create(4);
    printf("Step | Binary  | Decimal\n");
    printf("-----|---------|--------\n");
    for (int step = 0; step <= 16; step++) {
        unsigned long long val = reg_read(&counter);
        printf(" %2d  | ", step);
        for (int b = 3; b >= 0; b--) printf("%d", (int)((val >> b) & 1));
        printf("   | %2llu\n", val);
        if (step < 16) reg_set_value(&counter, val + 1);
    }
    printf("\n===== D Flip-Flop Demo =====\n\n");
    DFlipFlop dff = dff_create("demo");
    printf("Clk D | Q Q_bar\n------|--------\n");
    int test_vals[] = {0, 1, 1, 0, 1, 0, 0, 1};
    for (int i = 0; i < 8; i++) {
        dff_set_d(&dff, test_vals[i]);
        dff_clock(&dff);
        printf(" 1  %d | %d  %d\n", test_vals[i], dff.q, dff.q_bar);
    }
    printf("\n===== SR Latch Demo =====\n");
    SRLatch sr = sr_latch_create();
    printf("Initial: Q=%d, Q_bar=%d\n", sr.q, sr.q_bar);
    sr_latch_set_inputs(&sr, true, false); sr_latch_eval(&sr);
    printf("SET:     Q=%d, Q_bar=%d\n", sr.q, sr.q_bar);
    sr_latch_set_inputs(&sr, false, true); sr_latch_eval(&sr);
    printf("RESET:   Q=%d, Q_bar=%d\n", sr.q, sr.q_bar);
    printf("\n===== Shift Register Demo =====\n");
    ShiftRegister sr2 = shift_reg_create(8, SHIFT_RIGHT);
    bool init[8] = {1,0,1,0,1,0,1,0};
    shift_reg_load(&sr2, init);
    printf("Initial: "); bool out[8]; shift_reg_read(&sr2, out);
    for (int i = 0; i < 8; i++) printf("%d", out[i]);
    printf("\n");
    shift_reg_serial_in(&sr2, false, true); shift_reg_shift(&sr2);
    shift_reg_read(&sr2, out);
    printf("Shifted:"); for (int i = 0; i < 8; i++) printf("%d", out[i]);
    printf("\n");
    printf("\nCounter demo complete.\n");
    return 0;
}
