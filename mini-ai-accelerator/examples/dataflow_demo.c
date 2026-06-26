#include "dataflow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void) {
    printf("=========================================================\n");
    printf("  Dataflow Strategy Comparison Demo\n");
    printf("=========================================================\n\n");

    printf("Energy Model Parameters:\n");
    printf("  MAC operation:     %.0f pJ\n", MAC_ENERGY_PJ);
    printf("  SRAM read:         %.0f pJ\n", SRAM_READ_ENERGY_PJ);
    printf("  DRAM read:         %.0f pJ\n\n", DRAM_READ_ENERGY_PJ);

    dataflow_compare_all(56, 56, 64, 16, 16);

    printf("\n--- Layer-by-Layer Analysis ---\n\n");

    printf("Lightweight Layer (M=14, N=14, K=32):\n");
    dataflow_compare_all(14, 14, 32, 16, 16);

    printf("\nMedium Layer (M=56, N=56, K=128):\n");
    dataflow_compare_all(56, 56, 128, 16, 16);

    printf("\nLarge Layer (M=112, N=112, K=256):\n");
    dataflow_compare_all(112, 112, 256, 16, 16);

    printf("\n--- Energy Breakdown for M=56 N=56 K=64 ---\n\n");

    double total_macs = 56.0 * 56.0 * 64.0;
    printf("  Total MACs: %.0f\n", total_macs);
    printf("  MAC Energy: %.2f pJ\n", total_macs * MAC_ENERGY_PJ);
    printf("  DRAM Energy (per access): %.0f pJ\n", DRAM_READ_ENERGY_PJ);
    printf("  DRAM/SRAM ratio: %.0fx more expensive\n\n", DRAM_READ_ENERGY_PJ / SRAM_READ_ENERGY_PJ);

    printf("Recommendation:\n");
    printf("  - Weight stationary: best for inference (weights reused across batch)\n");
    printf("  - Output stationary: best for training (partial sums kept local)\n");
    printf("  - Row stationary (Eyeriss): best overall energy efficiency via row-level reuse\n");
    printf("  - Input stationary: simple control, good for streaming data\n");

    printf("\n=========================================================\n");
    printf("  Demo Complete\n");
    printf("=========================================================\n");

    return 0;
}
