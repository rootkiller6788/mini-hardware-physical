/**
 * warp_demo.c — SIMT Warp Execution Demo
 *
 * Demonstrates:
 *   - Warp lifecycle and thread states
 *   - SIMT divergence/reconvergence via mask stack
 *   - Warp vote (ballot) and shuffle operations
 *   - Branch pattern classification
 *   - SIMT efficiency analysis
 *
 * L6: Canonical problem — warp-level programming (divergent branches)
 */
#include <stdio.h>
#include <stdlib.h>
#include "warp.h"

int main(void) {
    printf("=== SIMT Warp Execution Demo ===\n\n");

    /* Create a warp */
    Warp *w = warp_create(0);
    if (!w) { fprintf(stderr, "Failed to create warp\n"); return 1; }

    /* --- Demo 1: Warp State --- */
    printf("--- Warp State After Creation ---\n");
    warp_print_state(w);

    /* --- Demo 2: Instruction Issue --- */
    printf("\n--- Instruction Issue ---\n");
    for (int i = 0; i < 5; i++) {
        int issued = warp_issue_instr(w);
        printf("  Cycle %d: issued %d threads (PC=%u)\n", i+1, issued, w->unified_pc);
        warp_complete_instr(w);
    }

    /* --- Demo 3: Branch Divergence --- */
    printf("\n--- Branch Divergence Demo ---\n");
    printf("  Before branch: active=%d, diverged=%s\n",
           warp_active_count(w), warp_is_diverged(w) ? "yes" : "no");

    /* Simulate if-else: threads 0-15 take branch, 16-31 don't */
    uint32_t taken_mask = 0x0000FFFF;  /* lower 16 threads go one way */
    BranchPattern pat = warp_classify_branch(w, taken_mask);
    printf("  Branch pattern: %s\n",
           pat == BRANCH_UNIFORM ? "uniform" :
           pat == BRANCH_TWO_WAY ? "two-way" : "other");

    /* Push divergence for taken path */
    warp_push_divergence(w, taken_mask, 10);
    printf("  After push: active=%d, diverged=%s, depth=%d\n",
           warp_active_count(w), warp_is_diverged(w) ? "yes" : "no",
           w->div_depth);

    /* Simulate taken path execution */
    warp_issue_instr(w);
    warp_complete_instr(w);
    printf("  Executed taken path (1 instruction with 16 threads)\n");

    /* Pop divergence: reconverge */
    warp_pop_divergence(w);
    printf("  After pop: active=%d, diverged=%s\n",
           warp_active_count(w), warp_is_diverged(w) ? "yes" : "no");

    /* --- Demo 4: Warp Vote Operations --- */
    printf("\n--- Warp Vote (Ballot) ---\n");
    uint32_t ballot = warp_vote(w, VOTE_ALL, true);
    printf("  __all(true)  = %u (1=all active)\n", ballot);

    ballot = warp_vote(w, VOTE_BALLOT, true);
    printf("  __ballot(true) = 0x%08X\n", ballot);

    /* --- Demo 5: Warp Stall --- */
    printf("\n--- Memory Stall Demo ---\n");
    warp_stall_memory(w);
    printf("  After stall: is_stalled=%s\n", warp_is_stalled(w) ? "yes" : "no");

    /* Issue should fail while stalled */
    int issued = warp_issue_instr(w);
    printf("  Issue while stalled: %d threads\n", issued);

    warp_unstall_memory(w);
    printf("  After unstall: is_stalled=%s\n", warp_is_stalled(w) ? "yes" : "no");

    /* --- Demo 6: SIMT Efficiency --- */
    printf("\n--- SIMT Efficiency Analysis ---\n");
    warp_print_divergence_analysis(w);

    /* --- Demo 7: Nested Divergence --- */
    printf("\n--- Nested Divergence ---\n");
    warp_push_divergence(w, 0x000000FF, 20);
    warp_push_divergence(w, 0x0000000F, 25);
    printf("  Double-nested: depth=%d, active=%d\n",
           w->div_depth, warp_active_count(w));

    warp_pop_divergence(w);
    warp_pop_divergence(w);
    printf("  Fully un-nested: depth=%d\n", w->div_depth);

    warp_destroy(w);
    printf("\n=== Warp Demo Complete ===\n");
    return 0;
}
