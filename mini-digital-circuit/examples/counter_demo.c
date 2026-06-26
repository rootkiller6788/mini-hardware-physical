#include <stdio.h>
#include "sequential.h"

int main(void) {
    printf("===== 4-bit Ripple Counter Demo =====\n\n");

    /* 创建一个 4 位寄存器作为计数器 */
    Register counter = reg_create(4);

    /* 构建一个简单的全加器逻辑函数 +1 */
    printf("Counting from 0 to 15 (4-bit binary counter):\n\n");

    /* 模拟自增操作：每次递增 1 */
    printf("Step | Binary  | Decimal\n");
    printf("-----|---------|--------\n");

    for (int step = 0; step <= 16; step++) {
        unsigned long long val = reg_read(&counter);
        printf(" %2d  | ", step);

        for (int b = 3; b >= 0; b--) {
            printf("%d", (int)((val >> b) & 1));
        }
        printf("   | %2llu\n", val);

        /* +1 计数器 */
        if (step < 16) {
            reg_write(&counter, val + 1);
        }
    }

    /* D Flip-Flop 波形演示 */
    printf("\n===== D Flip-Flop Waveform Demo =====\n\n");

    DFlipFlop dff = dff_create("demo_dff");

    printf("Clk  D  | Q  Q_bar  (rising-edge)\n");
    printf("--------|----------------------\n");

    int test_vals[] = {0, 1, 1, 0, 1, 0, 0, 1};
    for (int i = 0; i < 8; i++) {
        dff.d = test_vals[i];
        dff_clock(&dff);
        printf(" 1   %d  | %d    %d\n",
               test_vals[i], dff.q, dff.q_bar);
    }

    /* SR Latch 演示 */
    printf("\n===== SR Latch Demo =====\n\n");
    SRLatch sr = sr_latch_create();
    printf("Initial: Q=%d, Q_bar=%d\n", sr.q, sr.q_bar);

    sr_latch_set_s(&sr);
    printf("After SET:   Q=%d, Q_bar=%d\n", sr.q, sr.q_bar);

    sr.s = false; sr.r = false;
    sr_latch_eval(&sr);
    printf("After HOLD:  Q=%d, Q_bar=%d\n", sr.q, sr.q_bar);

    sr_latch_set_r(&sr);
    printf("After RESET: Q=%d, Q_bar=%d\n", sr.q, sr.q_bar);

    printf("\nCounter demo complete.\n");
    return 0;
}
