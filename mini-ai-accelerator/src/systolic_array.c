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
    (void)prev_activations;

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
                worker->cells[0][0].weight = weights[c * M + i];
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

/* ==========================================================================
 * L3: Output Stationary Dataflow for Systolic Array
 *
 * In Output Stationary mode, each PE accumulates a single output element
 * and partial sums stay local. This reduces data movement for partial sums
 * at the cost of streaming inputs and weights.
 *
 * Weight Stationary (original) vs Output Stationary:
 *   WS: weights pre-loaded, activations and partial sums streamed
 *   OS: partial sums accumulate locally, activations and weights streamed
 *
 * Throughput formula: same as WS → Ops = 2 × N² × f
 * Latency: OS reduces partial sum movement → lower energy for training
 * ========================================================================== */

void systolic_run_output_stationary(SystolicArray *sa, float *activations,
                                     float *weights, int M, int N, int K,
                                     float *result) {
    if (!sa) return;

    /* In OS mode, each PE accumulates one output element.
     * Rows correspond to output M, columns to input K dimension.
     * Weights flow horizontally, activations flow vertically. */

    SystolicArray *worker = systolic_array_create(sa->rows, sa->cols);
    if (!worker) return;

    int total_cycles = M + N + K - 2;

    for (int c = 0; c < total_cycles; c++) {
        /* Feed activations from top */
        for (int j = 0; j < worker->cols && j < K; j++) {
            int a_idx = c - j;
            if (a_idx >= 0 && a_idx < M) {
                worker->input_fifo[j] = activations[a_idx * K + j];
            } else {
                worker->input_fifo[j] = 0.0f;
            }
        }

        /* Feed weights from left */
        for (int i = 0; i < worker->rows && i < N; i++) {
            int w_idx = c - i;
            if (w_idx >= 0 && w_idx < K) {
                worker->cells[i][0].weight = weights[w_idx * N + i];
            }
        }

        systolic_cycle(worker);
    }

    /* Read results from PEs (accumulated partial sums are outputs) */
    for (int i = 0; i < N && i < worker->rows; i++) {
        for (int j = 0; j < M && j < worker->cols; j++) {
            result[i * M + j] = worker->cells[i][j].accumulator;
        }
    }

    systolic_array_destroy(worker);
}

/* ==========================================================================
 * L3: Double-Buffered Weight Loading for Systolic Arrays
 *
 * Double-buffering allows loading the next layer's weights while the
 * current layer is being computed, hiding weight-loading latency.
 *
 * Two weight banks: active (currently used by PEs) and shadow (being loaded).
 * On swap, shadow becomes active and active becomes the new shadow.
 *
 * This is standard practice in TPUv1 (ISCA 2017 §4.1 "Weight Loading").
 * ========================================================================== */

struct DoubleBufferSA {
    SystolicArray *sa;
    float weight_bank_0[MAX_SYSTOLIC_SIZE][MAX_SYSTOLIC_SIZE];
    float weight_bank_1[MAX_SYSTOLIC_SIZE][MAX_SYSTOLIC_SIZE];
    int active_bank;
    bool bank_1_ready;
};

DoubleBufferSA *sa_double_buffer_create(int rows, int cols) {
    DoubleBufferSA *db = (DoubleBufferSA *)malloc(sizeof(DoubleBufferSA));
    if (!db) {
        fprintf(stderr, "sa_double_buffer_create: malloc failed\n");
        return NULL;
    }
    db->sa = systolic_array_create(rows, cols);
    if (!db->sa) {
        free(db);
        return NULL;
    }
    memset(db->weight_bank_0, 0, sizeof(db->weight_bank_0));
    memset(db->weight_bank_1, 0, sizeof(db->weight_bank_1));
    db->active_bank  = 0;
    db->bank_1_ready = false;
    return db;
}

void sa_double_buffer_destroy(DoubleBufferSA *db) {
    if (!db) return;
    if (db->sa) systolic_array_destroy(db->sa);
    free(db);
}

/* Load weights into shadow bank */
void sa_double_buffer_load_shadow(DoubleBufferSA *db, float *weights,
                                   int rows, int cols) {
    if (!db || !weights) return;

    int shadow_bank = 1 - db->active_bank;
    float (*bank)[MAX_SYSTOLIC_SIZE] = (shadow_bank == 0)
        ? db->weight_bank_0 : db->weight_bank_1;

    for (int i = 0; i < rows && i < MAX_SYSTOLIC_SIZE; i++) {
        for (int j = 0; j < cols && j < MAX_SYSTOLIC_SIZE; j++) {
            bank[i][j] = weights[i * cols + j];
        }
    }

    if (shadow_bank == 1) db->bank_1_ready = true;
}

/* Swap banks and load active weights into systolic array */
void sa_double_buffer_swap(DoubleBufferSA *db) {
    if (!db) return;

    db->active_bank = 1 - db->active_bank;
    float (*active)[MAX_SYSTOLIC_SIZE] = (db->active_bank == 0)
        ? db->weight_bank_0 : db->weight_bank_1;

    /* Load from active bank into systolic array */
    SystolicArray *sa = db->sa;
    for (int i = 0; i < sa->rows; i++) {
        for (int j = 0; j < sa->cols; j++) {
            sa->cells[i][j].weight = active[i][j];
        }
    }
    db->bank_1_ready = false;
}

/* ==========================================================================
 * L4: Systolic Array Utilization & Cycle Counting
 *
 * Theoretical peak utilization depends on how well the problem dimensions
 * map to the array size. The efficiency formula:
 *   U = (M×N×K) / (array_height × array_width × total_cycles)
 * where total_cycles = M + N + K - 2 for weight-stationary.
 *
 * With M >> array_height or K >> array_width, utilization approaches 100%.
 * With M < array_height or K < array_width, utilization drops proportionally.
 * ========================================================================== */

double systolic_utilization_efficiency(int M, int N, int K, int array_h, int array_w) {
    if (array_h <= 0 || array_w <= 0) return 0.0;

    double total_macs = (double)M * N * K;
    double total_cycles = (double)(M + N + K - 2);
    double array_size = (double)(array_h * array_w);
    double peak_macs = array_size * total_cycles;

    if (peak_macs <= 0.0) return 0.0;

    double utilization = total_macs / peak_macs;
    if (utilization > 1.0) utilization = 1.0;
    return utilization;
}

/* Count total cycles for a given problem on a given array */
int systolic_count_cycles(int M, int N, int K, int array_h, int array_w) {
    /* Tile iterations */
    int m_tiles = (M + array_h - 1) / array_h;
    int n_tiles = (N + array_w - 1) / array_w;

    /* Per-tile cycles = M_tile + N_tile + K - 2 */
    int total_cycles = 0;
    for (int mt = 0; mt < m_tiles; mt++) {
        int mt_act = (mt * array_h + array_h <= M) ? array_h : M - mt * array_h;
        for (int nt = 0; nt < n_tiles; nt++) {
            int nt_act = (nt * array_w + array_w <= N) ? array_w : N - nt * array_w;
            total_cycles += mt_act + nt_act + K - 2;
        }
    }
    return total_cycles;
}
