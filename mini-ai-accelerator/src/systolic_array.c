#include "systolic_array.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

SystolicArray *systolic_array_create(int rows, int cols) {
    if (rows < 1 || rows > MAX_SYSTOLIC_SIZE || cols < 1 || cols > MAX_SYSTOLIC_SIZE) {
        fprintf(stderr, "systolic_array_create: invalid size %dx%d (max %d)\n", rows, cols, MAX_SYSTOLIC_SIZE);
        return NULL;
    }
    SystolicArray *sa = (SystolicArray *)malloc(sizeof(SystolicArray));
    if (!sa) {
        fprintf(stderr, "systolic_array_create: malloc failed\n");
        return NULL;
    }
    sa->rows = rows;
    sa->cols = cols;
    sa->input_head = 0;
    sa->weight_head = 0;
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            sa->cells[i][j].accumulator = 0.0f;
            sa->cells[i][j].weight = 0.0f;
            sa->cells[i][j].activation = 0.0f;
            sa->cells[i][j].partial_sum = 0.0f;
        }
        sa->input_fifo[i] = 0.0f;
        sa->weight_fifo[i] = 0.0f;
        sa->output_buffer[i] = 0.0f;
    }
    return sa;
}

void systolic_array_destroy(SystolicArray *sa) {
    if (sa) free(sa);
}

void systolic_load_weights(SystolicArray *sa, float *weights, int rows, int cols) {
    if (!sa) return;
    for (int i = 0; i < rows && i < sa->rows; i++) {
        for (int j = 0; j < cols && j < sa->cols; j++) {
            sa->cells[i][j].weight = weights[i * cols + j];
        }
    }
}

void systolic_load_activation(SystolicArray *sa, float *act, int len) {
    if (!sa) return;
    for (int i = 0; i < len && i < sa->cols; i++) {
        sa->input_fifo[i] = act[i];
    }
    sa->input_head = 0;
}

void systolic_cycle(SystolicArray *sa) {
    if (!sa) return;

    float prev_activations[MAX_SYSTOLIC_SIZE];
    for (int i = 0; i < sa->rows; i++) {
        prev_activations[i] = sa->cells[i][0].activation;
    }

    for (int i = 0; i < sa->rows; i++) {
        for (int j = 0; j < sa->cols; j++) {
            float a = sa->cells[i][j].activation;
            float w = sa->cells[i][j].weight;
            sa->cells[i][j].partial_sum = a * w;
            sa->cells[i][j].accumulator += a * w;
        }
    }

    for (int i = 0; i < sa->rows; i++) {
        for (int j = sa->cols - 1; j >= 1; j--) {
            sa->cells[i][j].activation = sa->cells[i][j - 1].activation;
        }
    }

    for (int i = sa->rows - 1; i >= 1; i--) {
        for (int j = 0; j < sa->cols; j++) {
            sa->cells[i][j].weight = sa->cells[i - 1][j].weight;
        }
    }

    for (int j = 0; j < sa->cols; j++) {
        sa->cells[0][j].weight = (sa->weight_head < sa->rows) ? sa->weight_fifo[sa->weight_head] : 0.0f;
    }

    for (int i = 0; i < sa->rows; i++) {
        sa->cells[i][0].activation = sa->input_fifo[sa->input_head];
    }

    sa->input_head++;
    sa->weight_head++;
}

void systolic_run(SystolicArray *sa, float *activations, float *weights, int M, int N, int K, float *result) {
    if (!sa) return;

    SystolicArray *worker = systolic_array_create(sa->rows, sa->cols);
    if (!worker) return;
    systolic_load_weights(worker, weights, N, M);

    int total_cycles = M + N + K - 2;
    for (int c = 0; c < total_cycles; c++) {
        if (c < K) {
            for (int i = 0; i < N; i++) {
                worker->cells[0].weight = weights[c * M + i];
            }
        }
        if (c < M) {
            for (int i = 0; i < K; i++) {
                worker->input_fifo[i] = activations[c * K + i];
            }
        }
        systolic_cycle(worker);
    }

    for (int i = 0; i < N; i++) {
        result[i] = worker->cells[i][worker->cols - 1].accumulator;
    }

    systolic_array_destroy(worker);
}

void systolic_print_state(SystolicArray *sa) {
    if (!sa) return;
    printf("Systolic Array %dx%d:\n", sa->rows, sa->cols);
    for (int i = 0; i < sa->rows; i++) {
        printf("  Row %d: ", i);
        for (int j = 0; j < sa->cols; j++) {
            printf("[acc=%.3f w=%.3f a=%.3f] ", sa->cells[i][j].accumulator,
                   sa->cells[i][j].weight, sa->cells[i][j].activation);
        }
        printf("\n");
    }
    printf("  Input FIFO: ");
    for (int j = 0; j < sa->cols; j++) {
        printf("%.3f ", sa->input_fifo[j]);
    }
    printf("\n  Weight FIFO: ");
    for (int j = 0; j < sa->rows; j++) {
        printf("%.3f ", sa->weight_fifo[j]);
    }
    printf("\n");
}
