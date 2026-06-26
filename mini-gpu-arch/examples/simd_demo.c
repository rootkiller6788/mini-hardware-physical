/**
 * simd_demo.c — SIMD Vector Execution Demo
 *
 * Demonstrates:
 *   - SIMD vector load/store/broadcast
 *   - Vector FMA for matrix-vector multiply (DL inference micro-kernel)
 *   - Warp reduction (tree-based sum)
 *   - Predication with mask push/pop
 *   - Amdahl's Law speedup prediction
 *
 * L6: Canonical problem — vector dot product using SIMD lanes
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "simd.h"

int main(void) {
    printf("=== SIMD Execution Engine Demo ===\n\n");

    /* Create 32-lane SIMD unit (warp-sized) */
    SIMDUnit *u = simd_create(32);
    if (!u) { fprintf(stderr, "Failed to create SIMD unit\n"); return 1; }

    /* --- Demo 1: Vector FMA (Multiply-Add) --- */
    printf("--- Fused Multiply-Add (FMA) ---\n");
    float a[32], b[32], c[32], r[32];
    for (int i = 0; i < 32; i++) {
        a[i] = (float)(i + 1);
        b[i] = 2.0f;
        c[i] = 1.0f;
    }
    simd_vfma(u, a, b, c, r, 32);
    printf("  FMA(dot-product) r[0]=%.2f r[15]=%.2f r[31]=%.2f\n",
           r[0], r[15], r[31]);

    /* --- Demo 2: Warp-Sized Reduction --- */
    printf("\n--- Warp Tree Reduction ---\n");
    float data[32];
    for (int i = 0; i < 32; i++) data[i] = 1.0f;
    float sum;
    simd_vreduce(u, REDUCE_SUM, data, 32, &sum);
    printf("  Sum of 32 ones = %.2f (expected 32.0)\n", sum);

    /* --- Demo 3: Predication & Mask --- */
    printf("\n--- Predication Mask Demo ---\n");
    simd_mask_push(u, 0x0000FFFF);  /* only lower 16 lanes active */
    printf("  Mask pushed: only %d lanes active\n", 16);

    float pred_data[32], pred_out[32];
    for (int i = 0; i < 32; i++) pred_data[i] = (float)i;
    simd_vop(u, VOP_MUL, pred_data, pred_data, pred_out, 32);
    printf("  Predicated multiply: out[0]=%.2f out[16]=%.2f (inactive→0)\n",
           pred_out[0], pred_out[16]);

    simd_mask_pop(u);
    printf("  Mask popped: all %d lanes active again\n", u->num_lanes);

    /* --- Demo 4: Amdahl's Law --- */
    printf("\n--- Amdahl's Law ---\n");
    for (int n = 1; n <= 1024; n *= 4) {
        AmdahlModel m = amdahl_compute(0.05, n);
        printf("  N=%4d, serial=5%% → speedup=%.2f×\n", n, m.speedup);
    }
    printf("  As N→∞, max speedup → 1/0.05 = 20×\n");

    /* --- Demo 5: Coalescing Analysis --- */
    printf("\n--- Memory Coalescing Analysis ---\n");
    MemAccessPattern pat = {0};
    pat.stride = 1;
    pat.is_scatter = false;
    pat.num_indices = 32;
    for (int i = 0; i < 32; i++) pat.indices[i] = i * 4;

    CoalesceResult cr = simd_coalesce_analyze(&pat, 128);
    printf("  Contiguous 32×float: %d transactions, eff=%.1f%%\n",
           cr.num_transactions, cr.efficiency * 100.0);

    /* Strided (every 1024 bytes) — very poor coalescing */
    for (int i = 0; i < 32; i++) pat.indices[i] = i * 1024;
    cr = simd_coalesce_analyze(&pat, 128);
    printf("  Strided 1KB:         %d transactions, eff=%.1f%%\n",
           cr.num_transactions, cr.efficiency * 100.0);

    simd_print_stats(u);
    simd_destroy(u);

    printf("\n=== SIMD Demo Complete ===\n");
    return 0;
}
