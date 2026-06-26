/**
 * gemm_demo.c — Tensor Core GEMM Demo
 *
 * Demonstrates:
 *   - MMA instruction on tensor core (matrix multiply-accumulate)
 *   - Blocked GEMM using tensor core tiling
 *   - Mixed-precision conversion (FP8, BF16)
 *   - Tensor core roofline analysis
 *   - Performance statistics
 *
 * L6: Canonical problem — Matrix multiply using tensor cores
 * L7: Application — deep learning inference kernel
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "tensor_core.h"

/** Print a small matrix */
static void print_matrix(const char *name, const float *mat, int rows, int cols) {
    printf("%s [%d×%d]:\n", name, rows, cols);
    for (int i = 0; i < rows; i++) {
        printf("  ");
        for (int j = 0; j < cols; j++) {
            printf("%7.2f ", mat[i * cols + j]);
        }
        printf("\n");
    }
}

int main(void) {
    printf("=== Tensor Core GEMM Demo ===\n\n");

    /* --- Demo 1: Tensor Core Creation --- */
    printf("--- Tensor Core Setup ---\n");
    TensorCore *tc = tc_create(0, MMA_M8N8K4, MMA_FP16);
    if (!tc) { fprintf(stderr, "Failed to create tensor core\n"); return 1; }
    printf("  Tensor Core %d: M%u×N%u×K%u, precision=%d\n",
           tc->tc_id, tc->m, tc->n, tc->k, tc->precision);

    /* --- Demo 2: Small MMA (4×4 GEMM unit test) --- */
    printf("\n--- Small MMA: 4×4 GEMM ---\n");
    float A[16] = {
        1, 2, 3, 4,
        5, 6, 7, 8,
        9, 10, 11, 12,
        13, 14, 15, 16
    };
    float B_eye[16] = {  /* identity */
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    float C_zero[16] = {0};
    float D[16] = {0};

    tc_mma_compute(tc, A, B_eye, C_zero, D, 4, 4, 4);
    print_matrix("A", A, 4, 4);
    print_matrix("B (I)", B_eye, 4, 4);
    print_matrix("D = A*B", D, 4, 4);
    printf("  Ops completed: %lu\n", (unsigned long)tc->ops_completed);

    /* --- Demo 3: Blocked GEMM for larger matrix --- */
    printf("\n--- Blocked GEMM: 8×8 × 8×8 ---\n");
    float A8[64], B8[64], C8[64], D8[64];
    memset(C8, 0, sizeof(C8));
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            A8[i * 8 + j] = (float)(i + j);
            B8[i * 8 + j] = (float)((i == j) ? 1 : 0);
        }
    }

    GEMMBlockConfig cfg = gemm_block_config(8, 8, 8, MMA_M8N8K4);
    printf("  Block config: M_blocks=%d N_blocks=%d K_blocks=%d\n",
           cfg.M_blocks, cfg.N_blocks, cfg.K_blocks);

    TensorCluster *cl = tcluster_create(1, MMA_M8N8K4, MMA_FP16);
    if (!cl) { fprintf(stderr, "Failed to create tensor cluster\n"); tc_destroy(tc); return 1; }

    gemm_tensor_core_gemm(A8, B8, C8, D8, 8, 8, 8, cl);
    print_matrix("A8", A8, 8, 8);
    print_matrix("D8 = A8*I", D8, 8, 8);

    /* --- Demo 4: Tensor Core Roofline --- */
    printf("\n--- Tensor Core Roofline ---\n");
    printf("  GPU: A100 (SM 8.0), HBM2e 1555 GB/s\n");
    TensorRoofline tr = tensor_roofline_eval(MMA_FP16, 50.0, 1555.0, SM_CC_80);
    printf("  FP16 peak TFLOPS:  %.1f\n", tr.tensor_peak_tflops);
    printf("  CUDA peak TFLOPS:  %.2f\n", tr.cuda_core_peak_tflops);
    printf("  Speedup (tensor):  %.1f×\n", tr.speedup_vs_cuda);
    printf("  Is tensor-bound:   %s\n", tr.is_tensor_bound ? "yes" : "no");

    /* --- Demo 5: Mixed Precision (FP8) --- */
    printf("\n--- FP8 Mixed Precision ---\n");
    float test_values[] = {0.0f, 1.0f, -1.0f, 2.0f, 0.5f, -0.5f, 3.0f, -4.0f};
    printf("  FP8 E4M3 conversion roundtrip:\n");
    for (int i = 0; i < 8; i++) {
        float orig = test_values[i];
        uint8_t fp8 = float_to_fp8(orig, false);
        float back = fp8_to_float(fp8, false);
        printf("    float %.1f → FP8 0x%02X → float %.4f (err=%.4f)\n",
               orig, fp8, back, fabsf(orig - back));
    }

    /* --- Demo 6: BF16 --- */
    printf("\n  BF16 conversion:\n");
    for (int i = 0; i < 4; i++) {
        float orig = test_values[i];
        uint16_t bf16 = float_to_bf16(orig);
        float back = bf16_to_float(bf16);
        printf("    float %.1f → BF16 0x%04X → float %.4f\n",
               orig, bf16, back);
    }

    /* --- Demo 7: Performance Stats --- */
    printf("\n--- Performance Statistics ---\n");
    tc_print_stats(tc);
    tc_print_performance(tc);
    tcluster_print_stats(cl);

    /* Cleanup */
    tc_destroy(tc);
    tcluster_destroy(cl);

    printf("\n=== GEMM Demo Complete ===\n");
    return 0;
}
