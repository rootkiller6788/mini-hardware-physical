#include "tensor_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void print_mat(const char *label, float *m, int rows, int cols) {
    printf("%s (%dx%d):\n", label, rows, cols);
    for (int i = 0; i < rows; i++) {
        printf("  ");
        for (int j = 0; j < cols; j++) {
            printf("%8.3f ", m[i * cols + j]);
        }
        printf("\n");
    }
}

int main(void) {
    (void)print_mat;
    printf("=========================================================\n");
    printf("  Tensor Core Microarchitecture Demo\n");
    printf("=========================================================\n\n");

    /* ---- Precision Conversion Demo ---- */
    printf("--- FP16 / BF16 / FP8 Precision Conversion ---\n");

    float test_vals[] = {0.5f, 1.0f, -3.14f, 65504.0f, 0.001f, -0.001f, 42.0f, -100.0f};
    int n_test = 8;

    printf("  %-15s %-15s %-15s %-15s %-15s\n", "FP32", "FP16→FP32", "Error", "BF16→FP32", "FP8→FP32");
    printf("  %-15s %-15s %-15s %-15s %-15s\n",
           "---------------", "---------------", "---------------", "---------------", "---------------");

    for (int i = 0; i < n_test; i++) {
        float orig = test_vals[i];

        float16_t h = fp32_to_fp16(orig);
        float fp16_back = fp16_to_fp32(h);

        bfloat16_t b = fp32_to_bf16(orig);
        float bf16_back = bf16_to_fp32(b);

        float8_e4m3_t e4 = fp32_to_fp8_e4m3(orig);
        float fp8_back = fp8_e4m3_to_fp32(e4);

        float err_fp16 = fabsf(orig - fp16_back) / (fabsf(orig) + 1e-10f);
        printf("  %15.6f %15.6f %14.2e %15.6f %15.6f\n",
               orig, fp16_back, err_fp16, bf16_back, fp8_back);
    }

    /* ---- Tensor Core MMA Demo ---- */
    printf("\n--- Tensor Core Warp-Level MMA ---\n");

    TensorCore *tc = tensor_core_create(4, TC_PREC_FP16_FP16_FP32);
    if (!tc) {
        fprintf(stderr, "Failed to create tensor core\n");
        return 1;
    }

    TensorCoreFragment *frag = tensor_core_fragment_create(TC_M, TC_N, TC_K,
                                                            TC_PREC_FP16_FP16_FP32);
    if (!frag) {
        fprintf(stderr, "Failed to create fragment\n");
        tensor_core_destroy(tc);
        return 1;
    }

    /* Fill A and B with simple values */
    for (int i = 0; i < TC_M; i++) {
        for (int k = 0; k < TC_K; k++) {
            frag->A[i * TC_K + k] = (float)((i + k + 1) % 5) * 0.1f;
        }
    }
    for (int k = 0; k < TC_K; k++) {
        for (int j = 0; j < TC_N; j++) {
            frag->B[k * TC_N + j] = (float)((k + j + 1) % 4) * 0.1f;
        }
    }

    /* Execute MMA */
    tensor_core_mma(tc, frag);
    printf("  MMA completed: %llu total operations\n", (unsigned long long)tc->total_mma_ops);

    tensor_core_print_fragment(frag, "D = A×B + C");

    /* ---- Float compare with naive matmul ---- */
    printf("\n--- Verification: Tensor Core vs Naive MatMul ---\n");
    float *naive_D = (float *)calloc(TC_M * TC_N, sizeof(float));
    for (int i = 0; i < TC_M; i++) {
        for (int j = 0; j < TC_N; j++) {
            float sum = 0.0f;
            for (int k = 0; k < TC_K; k++) {
                sum += frag->A[i * TC_K + k] * frag->B[k * TC_N + j];
            }
            naive_D[i * TC_N + j] = sum;
        }
    }

    float max_diff = 0.0f;
    for (int i = 0; i < TC_M * TC_N; i++) {
        float diff = fabsf(naive_D[i] - frag->D[i]);
        if (diff > max_diff) max_diff = diff;
    }
    printf("  Max difference: %.6f (due to FP16 precision rounding)\n", max_diff);
    if (max_diff < 0.5f) {
        printf("  Tensor Core output matches naive FP32 matmul (within FP16 tolerance)\n");
    }

    /* ---- Large Matrix Multiply via Tensor Core ---- */
    printf("\n--- Large MatMul via Repeated Tensor Core MMA ---\n");

    int M = 32, N = 32, K = 32;
    float *A_large = (float *)malloc(M * K * sizeof(float));
    float *B_large = (float *)malloc(K * N * sizeof(float));
    float *D_large = (float *)calloc(M * N, sizeof(float));

    for (int i = 0; i < M * K; i++) A_large[i] = (float)(i % 10) * 0.1f;
    for (int i = 0; i < K * N; i++) B_large[i] = (float)(i % 8 + 1) * 0.1f;

    tensor_core_large_matmul(tc, A_large, B_large, NULL, M, N, K, D_large);

    printf("  First few elements of D[%d×%d]:\n", M, N);
    for (int i = 0; i < 4; i++) {
        printf("  Row %d: ", i);
        for (int j = 0; j < 4; j++) {
            printf("%8.3f ", D_large[i * N + j]);
        }
        printf("\n");
    }

    /* ---- Performance Analysis ---- */
    printf("\n--- Tensor Core Performance Analysis ---\n");
    printf("  Precision modes & cycles per MMA:\n");
    TensorCorePrecision precs[] = {
        TC_PREC_FP16_FP16_FP32, TC_PREC_BF16_BF16_FP32,
        TC_PREC_TF32_TF32_FP32, TC_PREC_FP8_FP8_FP32,
        TC_PREC_INT8_INT8_INT32, TC_PREC_INT4_INT4_INT32
    };
    const char *prec_names[] = {
        "FP16-FP32", "BF16-FP32", "TF32-FP32",
        "FP8-FP32", "INT8-INT32", "INT4-INT32"
    };

    for (int i = 0; i < 6; i++) {
        int cycles = tc_cycles_per_mma(precs[i]);
        double flops = tc_flops_per_mma(TC_M, TC_N, TC_K);
        printf("  %-12s: %d cycles, %.0f FLOPs/MMA, %.1f GFLOPS/MHz\n",
               prec_names[i], cycles, flops, flops / cycles / 1e3);
    }

    /* ---- Structured Sparse MMA ---- */
    printf("\n--- Structured Sparse MMA (2:4 pattern) ---\n");

    int SM = 8, SN = 4, SK = 8;
    float *A_sp = (float *)malloc(SM * SK * sizeof(float));
    float *B_sp = (float *)malloc((SK / 2) * SN * sizeof(float));
    int   *B_idx = (int *)malloc((SK / 4) * sizeof(int));
    float *C_sp = (float *)calloc(SM * SN, sizeof(float));
    float *D_sp = (float *)calloc(SM * SN, sizeof(float));

    for (int i = 0; i < SM * SK; i++) A_sp[i] = (float)((i + 1) % 5) * 0.2f;
    for (int i = 0; i < (SK / 2) * SN; i++) B_sp[i] = (float)((i + 1) % 4) * 0.15f;

    /* 2:4 pattern: keep positions 0 and 2 (bits 0b0101 = 5) */
    for (int i = 0; i < SK / 4; i++) {
        B_idx[i] = 5; /* keep elements 0 and 2 of each group of 4 */
    }

    tensor_core_mma_sparse(tc, A_sp, B_sp, B_idx, C_sp, D_sp, SM, SN, SK);
    printf("  Sparse MMA result (first row): ");
    for (int j = 0; j < SN; j++) printf("%8.3f ", D_sp[j]);
    printf("\n");

    printf("\n=========================================================\n");
    printf("  Demo Complete\n");
    printf("=========================================================\n");

    tensor_core_fragment_destroy(frag);
    tensor_core_destroy(tc);
    free(naive_D);
    free(A_large); free(B_large); free(D_large);
    free(A_sp); free(B_sp); free(B_idx); free(C_sp); free(D_sp);
    return 0;
}
