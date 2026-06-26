#include <stdio.h>
#include "rtl_basic.h"

int main(void) {
    printf("===== 5-Stage Pipeline Demo =====\n\n");
    FiveStagePipeline p = pipeline_create();
    pipeline_write_reg(&p, 1, 100);
    pipeline_write_reg(&p, 2, 200);
    printf("Initial regs: R1=%u, R2=%u\n\n", pipeline_read_reg(&p, 1), pipeline_read_reg(&p, 2));
    printf("Feeding 5 instructions...\n");
    for (int i = 0; i < 5; i++) pipeline_fetch(&p, 0x00000000 + i * 4);
    for (int i = 0; i < 10; i++) {
        int c = pipeline_cycle(&p);
        if (c > 1) printf("  Cycle %d: STALL\n", p.cycles);
    }
    int cycles, instrs, stalls, bubbles; double cpi;
    pipeline_stats(&p, &cycles, &instrs, &stalls, &bubbles, &cpi);
    printf("\nPipeline stats:\n");
    printf("  Cycles: %d\n  Instructions: %d\n  Stalls: %d\n  Bubbles: %d\n  CPI: %.2f\n",
           cycles, instrs, stalls, bubbles, cpi);
    printf("\nPipeline demo complete.\n");
    return 0;
}
