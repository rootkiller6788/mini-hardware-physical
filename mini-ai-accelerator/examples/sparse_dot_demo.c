#include "sparse_accel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define SIZE 8

static void dense_matmul(float *A, float *B, int rows, int cols_unused, int inner, float *C) {
    (void)cols_unused;
    for (int i = 0; i < rows; i++) {
        C[i] = 0.0f;
        for (int k = 0; k < inner; k++) {
            C[i] += A[i * inner + k] * B[k];
        }
    }
}

static void print_matrix(const char *name, float *mat, int rows, int cols) {
    printf("%s (%dx%d):\n", name, rows, cols);
    for (int i = 0; i < rows; i++) {
        printf("  ");
        for (int j = 0; j < cols; j++) {
            printf("%7.2f ", mat[i * cols + j]);
        }
        printf("\n");
    }
}

int main(void) {
    printf("=========================================================\n");
    printf("  Sparse Acceleration Demo (2:4 Structured Sparsity)\n");
    printf("=========================================================\n\n");

    float weights[SIZE * SIZE] = {
         0.8f, -0.3f,  0.1f,  0.02f,  0.5f, -0.6f,  0.0f,  0.01f,
        -0.2f,  0.9f, -0.1f,  0.03f, -0.4f,  0.7f,  0.0f,  0.05f,
         0.3f, -0.5f,  1.2f, -0.01f,  0.2f, -0.8f,  0.0f,  0.04f,
        -0.1f,  0.4f, -0.7f,  0.08f, -0.3f,  0.6f,  0.0f, -0.02f,
         0.6f, -0.2f,  0.3f, -0.05f,  0.9f, -0.1f,  0.0f,  0.03f,
        -0.4f,  0.8f, -0.5f,  0.06f, -0.7f,  1.0f,  0.0f, -0.01f,
         0.2f, -0.6f,  0.4f, -0.03f,  0.1f, -0.9f,  0.0f,  0.02f,
         0.7f, -0.1f,  0.8f, -0.04f, -0.5f,  0.3f,  0.0f, -0.06f
    };

    float input_vec[SIZE] = {1.0f, 0.5f, -0.5f, 0.3f, -1.0f, 0.8f, -0.2f, 0.6f};

    printf("Original Dense Weight Matrix:\n");
    print_matrix("W", weights, SIZE, SIZE);

    printf("\nInput vector:\n  ");
    for (int i = 0; i < SIZE; i++) printf("%7.2f ", input_vec[i]);
    printf("\n\n");

    float dense_output[SIZE];
    dense_matmul(weights, input_vec, SIZE, SIZE, SIZE, dense_output);
    printf("Dense output:\n  ");
    for (int i = 0; i < SIZE; i++) printf("%7.2f ", dense_output[i]);
    printf("\n\n");

    printf("=== Applying 2:4 Structured Sparsity ===\n");
    sparse_2of4_prune(weights, SIZE, SIZE);
    print_matrix("W (2:4 pruned)", weights, SIZE, SIZE);

    printf("\n=== Converting to CSR ===\n");
    SparseMatrix *csr = sparse_csr_create(SIZE, SIZE, 0);
    if (!csr) {
        fprintf(stderr, "Failed to create CSR matrix\n");
        return 1;
    }
    sparse_csr_from_dense(weights, SIZE, SIZE, csr);

    float original_copy[SIZE * SIZE];
    memcpy(original_copy, weights, sizeof(float) * SIZE * SIZE);
    sparse_print_compression_ratio(csr, original_copy, SIZE, SIZE);

    printf("\n=== Sparse-Dense MatMul ===\n");
    float sparse_output[SIZE];
    sparse_spmm(csr, input_vec, SIZE, sparse_output);
    printf("Sparse output:\n  ");
    for (int i = 0; i < SIZE; i++) printf("%7.2f ", sparse_output[i]);
    printf("\n\n");

    printf("=== Output Comparison ===\n  %-10s %-10s %-10s\n", "Dense", "Sparse", "Error");
    float max_diff = 0.0f;
    for (int i = 0; i < SIZE; i++) {
        float diff = fabsf(dense_output[i] - sparse_output[i]);
        if (diff > max_diff) max_diff = diff;
        printf("  %10.4f %10.4f %10.6f\n", dense_output[i], sparse_output[i], diff);
    }
    printf("  Max error: %.6f\n", max_diff);

    printf("\n=== Operations Comparison ===\n");
    (void)sparse_compute_reduction(weights, input_vec, SIZE * SIZE);
    int dense_ops = SIZE * SIZE;
    int dense_nnz = 0;
    for (int i = 0; i < SIZE * SIZE; i++) {
        if (fabsf(weights[i]) > 1e-7f) dense_nnz++;
    }
    printf("  Dense MAC operations: %d\n", dense_ops);
    printf("  Sparse MAC operations: %d\n", dense_nnz);
    printf("  Theoretical speedup: %.2fx\n", (float)dense_ops / (float)dense_nnz);
    printf("  Structured sparsity (2:4): %.1f%% of weights retained\n",
           100.0f * (float)dense_nnz / (float)dense_ops);

    printf("\n=========================================================\n");
    printf("  Demo Complete\n");
    printf("=========================================================\n");

    sparse_csr_destroy(csr);
    return 0;
}
