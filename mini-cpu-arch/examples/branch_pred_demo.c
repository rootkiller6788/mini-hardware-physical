#include <stdio.h>
#include <stdint.h>
#include "branch_pred.h"

static bool branch_pattern[] = {
    true,  true,  true,  false, true, false, true, false,
    true,  true,  true,  false, true, false, true, false,
    true,  false, true,  false, true, false, true, false,
    false, true,  false, true,  true,  true,  false, true,
    true,  true,  true,  false, true, false, true, false,
    true,  true,  true,  false, true, false, true, false,
    true,  false, true,  false, true, false, true, false,
    false, true,  false, true,  true,  true,  false, true,
};

#define PATTERN_LEN (sizeof(branch_pattern) / sizeof(branch_pattern[0]))

int main(void) {
    printf("=== mini-cpu-arch: Branch Predictor Demo ===\n");
    printf("Pattern: TTTNTNTN (repeating 8x)\n");
    printf("Total branches: %zu\n\n", PATTERN_LEN);

    BranchPredictor predictors[PRED_TYPE_COUNT];
    for (int t = 0; t < PRED_TYPE_COUNT; t++) {
        bp_create(&predictors[t], (PredictorType)t);
    }

    for (size_t i = 0; i < PATTERN_LEN; i++) {
        uint32_t pc = 0x1000 + (uint32_t)(i * 4);
        bool actual = branch_pattern[i];

        for (int t = 0; t < PRED_TYPE_COUNT; t++) {
            bool pred = bp_predict(&predictors[t], pc);
            bool correct = (pred == actual);
            predictors[t].total_predictions++;
            if (correct) predictors[t].correct_predictions++;
            bp_update(&predictors[t], pc, actual);
        }
    }

    printf("--- Prediction Results ---\n\n");
    for (int t = 0; t < PRED_TYPE_COUNT; t++) {
        bp_print_stats(&predictors[t]);
        printf("\n");
    }

    printf("--- Analysis ---\n");
    printf("Always Taken:    Works well for T-heavy patterns (75%% T here)\n");
    printf("Always Not Taken: Only correct for NT branches (25%% NT here)\n");
    printf("Bimodal:         Learns per-branch bias with 2-bit counters\n");
    printf("Two-Level:       Correlates with recent branch history\n");
    printf("Gshare:          XORs PC with GHR for better pattern matching\n");

    return 0;
}
