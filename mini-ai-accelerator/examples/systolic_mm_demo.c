#include "systolic_array.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAT_SIZE 4

static void naive_matmul(float *A, float *B, int M, int Ncols, int K, float *C) {
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < Ncols; j++) {
            float sum = 0.0f;
            for (int k = 0; k < K; k++) {
                sum += A[i * K + k] * B[k * Ncols + j];
            }
            C[i * Ncols + j] = sum;
        }
    }
}

static void print_matrix(float *mat, int rows, int cols, const char *name) {
    printf("%s (%dx%d):\n", name, rows, cols);
    for (int i = 0; i < rows; i++) {
        printf("  ");
        for (int j = 0; j < cols; j++) {
            printf("%8.2f ", mat[i * cols + j]);
        }
        printf("\n");
    }
}

int main(void) {
    printf("=========================================================\n");
    printf("  Systolic Array Matrix Multiplication Demo\n");
    printf("=========================================================\n\n");

    float A[MAT_SIZE * MAT_SIZE] = {
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    };

    float B[MAT_SIZE * MAT_SIZE] = {
        0.5f, 1.0f, 1.5f, 2.0f,
        2.5f, 3.0f, 3.5f, 4.0f,
        4.5f, 5.0f, 5.5f, 6.0f,
        6.5f, 7.0f, 7.5f, 8.0f
    };

    print_matrix(A, MAT_SIZE, MAT_SIZE, "A");
    print_matrix(B, MAT_SIZE, MAT_SIZE, "B");

    SystolicArray *sa = systolic_array_create(MAT_SIZE, MAT_SIZE);
    if (!sa) {
        fprintf(stderr, "Failed to create systolic array\n");
        return 1;
    }

    float *C_naive = (float *)calloc(MAT_SIZE * MAT_SIZE, sizeof(float));
    float *C_systolic = (float *)calloc(MAT_SIZE * MAT_SIZE, sizeof(float));
    if (!C_naive || !C_systolic) {
        fprintf(stderr, "Memory allocation failed\n");
        systolic_array_destroy(sa);
        free(C_naive); free(C_systolic);
        return 1;
    }

    naive_matmul(A, B, MAT_SIZE, MAT_SIZE, MAT_SIZE, C_naive);
    print_matrix(C_naive, MAT_SIZE, MAT_SIZE, "Expected C = A x B (naive)");

    printf("\n--- Systolic Array Execution ---\n");
    systolic_load_weights(sa, B, MAT_SIZE, MAT_SIZE);

    int total_cycles = 3 * MAT_SIZE - 2;
    for (int cycle = 0; cycle < total_cycles; cycle++) {
        printf("\n=== Cycle %d ===\n", cycle);

        for (int j = 0; j < MAT_SIZE; j++) {
            if (cycle < MAT_SIZE) {
                sa->input_fifo[j] = A[cycle * MAT_SIZE + j];
            } else {
                sa->input_fifo[j] = 0.0f;
            }
        }

        systolic_cycle(sa);
        systolic_print_state(sa);
    }

    printf("\n--- Systolic Array Result ---\n");
    for (int i = 0; i < MAT_SIZE; i++) {
        C_systolic[i] = sa->cells[i][MAT_SIZE - 1].accumulator;
    }
    print_matrix(C_systolic, 1, MAT_SIZE, "Systolic output (last column)");

    printf("\n--- Verification ---\n");
    int mismatches = 0;
    for (int i = 0; i < MAT_SIZE; i++) {
        float diff = C_naive[i * MAT_SIZE] - C_systolic[i];
        if (diff < 0.0f) diff = -diff;
        if (diff > 0.01f) {
            printf("  MISMATCH at row %d: expected %.2f, got %.2f\n", i, C_naive[i * MAT_SIZE], C_systolic[i]);
            mismatches++;
        }
    }
    if (mismatches == 0) {
        printf("  All results match! (within tolerance)\n");
    } else {
        printf("  %d mismatches found\n", mismatches);
    }

    printf("\n=========================================================\n");
    printf("  Demo Complete\n");
    printf("=========================================================\n");

    systolic_array_destroy(sa);
    free(C_naive);
    free(C_systolic);
    return 0;
}
