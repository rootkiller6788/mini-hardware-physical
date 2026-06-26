#include "sparse_accel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

SparseMatrix *sparse_csr_create(int rows, int cols, int nnz) {
    SparseMatrix *sm = (SparseMatrix *)malloc(sizeof(SparseMatrix));
    if (!sm) {
        fprintf(stderr, "sparse_csr_create: malloc failed\n");
        return NULL;
    }
    sm->rows = rows;
    sm->cols = cols;
    sm->nnz = nnz;
    sm->row_ptr = (int *)calloc(rows + 1, sizeof(int));
    sm->col_idx = (int *)malloc(nnz > 0 ? nnz * sizeof(int) : sizeof(int));
    sm->values = (float *)malloc(nnz > 0 ? nnz * sizeof(float) : sizeof(float));
    if (!sm->row_ptr || !sm->col_idx || !sm->values) {
        fprintf(stderr, "sparse_csr_create: malloc failed for arrays\n");
        free(sm->row_ptr); free(sm->col_idx); free(sm->values); free(sm);
        return NULL;
    }
    return sm;
}

void sparse_csr_destroy(SparseMatrix *sm) {
    if (!sm) return;
    if (sm->row_ptr) free(sm->row_ptr);
    if (sm->col_idx) free(sm->col_idx);
    if (sm->values) free(sm->values);
    free(sm);
}

void sparse_csr_from_dense(float *dense, int rows, int cols, SparseMatrix *sparse) {
    if (!dense || !sparse) return;

    int nnz = 0;
    for (int i = 0; i < rows * cols; i++) {
        if (fabsf(dense[i]) > 1e-7f) nnz++;
    }

    sparse->rows = rows;
    sparse->cols = cols;
    sparse->nnz = nnz;

    if (nnz > MAX_SPARSE_ENTRIES) {
        fprintf(stderr, "sparse_csr_from_dense: nnz %d exceeds max %d\n", nnz, MAX_SPARSE_ENTRIES);
        sparse->nnz = 0;
        return;
    }

    sparse->row_ptr = (int *)realloc(sparse->row_ptr, (rows + 1) * sizeof(int));
    sparse->col_idx = (int *)realloc(sparse->col_idx, nnz * sizeof(int));
    sparse->values = (float *)realloc(sparse->values, nnz * sizeof(float));

    int idx = 0;
    sparse->row_ptr[0] = 0;
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            float val = dense[i * cols + j];
            if (fabsf(val) > 1e-7f) {
                sparse->col_idx[idx] = j;
                sparse->values[idx] = val;
                idx++;
            }
        }
        sparse->row_ptr[i + 1] = idx;
    }
}

void sparse_spmm(SparseMatrix *A, float *dense_B, int K, float *result) {
    if (!A || !dense_B || !result) return;
    (void)K;

    for (int i = 0; i < A->rows; i++) {
        result[i] = 0.0f;
        for (int j = A->row_ptr[i]; j < A->row_ptr[i + 1]; j++) {
            int col = A->col_idx[j];
            float val = A->values[j];
            result[i] += val * dense_B[col];
        }
    }
}

float sparse_compute_reduction(float *A, float *B, int len) {
    if (!A || !B) return 0.0f;
    int nnz = 0;
    for (int i = 0; i < len; i++) {
        if (fabsf(A[i]) > 1e-7f) nnz++;
    }
    if (len == 0) return 1.0f;
    return (float)len / (float)(nnz + 1);
}

void sparse_2of4_prune(float *weights, int rows, int cols) {
    if (!weights) return;
    int total = rows * cols;
    for (int i = 0; i < total; i += 4) {
        float group[4];
        int indices[4] = {0, 1, 2, 3};
        for (int k = 0; k < 4 && i + k < total; k++) {
            group[k] = fabsf(weights[i + k]);
        }

        for (int a = 0; a < 3; a++) {
            for (int b = a + 1; b < 4; b++) {
                if (group[a] < group[b]) {
                    float tmp_g = group[a]; group[a] = group[b]; group[b] = tmp_g;
                    int tmp_i = indices[a]; indices[a] = indices[b]; indices[b] = tmp_i;
                }
            }
        }

        int keep_mask[4] = {0, 0, 0, 0};
        keep_mask[indices[0]] = 1;
        keep_mask[indices[1]] = 1;

        for (int k = 0; k < 4 && i + k < total; k++) {
            if (!keep_mask[k]) {
                weights[i + k] = 0.0f;
            }
        }
    }
}

void sparse_print_compression_ratio(SparseMatrix *sm, float *dense, int rows, int cols) {
    if (!sm || !dense) return;
    int dense_elements = rows * cols;
    int sparse_storage = sm->nnz * (int)(sizeof(float) + sizeof(int)) + (sm->rows + 1) * (int)sizeof(int);
    int dense_storage = dense_elements * (int)sizeof(float);
    float ratio = (float)dense_storage / (float)(sparse_storage + 1);

    printf("Compression Analysis:\n");
    printf("  Original elements: %d\n", dense_elements);
    printf("  Non-zero elements: %d\n", sm->nnz);
    printf("  Sparsity: %.2f%%\n", 100.0f * (1.0f - (float)sm->nnz / (float)dense_elements));
    printf("  Dense storage: %d bytes\n", dense_storage);
    printf("  CSR storage: %d bytes\n", sparse_storage);
    printf("  Compression ratio: %.2fx\n", ratio);
}

SparseAccelerator *sparse_accel_create(int input_size, int output_size) {
    SparseAccelerator *sa = (SparseAccelerator *)malloc(sizeof(SparseAccelerator));
    if (!sa) {
        fprintf(stderr, "sparse_accel_create: malloc failed\n");
        return NULL;
    }
    sa->input_size = input_size;
    sa->output_size = output_size;
    sa->input_buffer = (float *)calloc(input_size, sizeof(float));
    sa->output_dense = (float *)calloc(output_size, sizeof(float));
    sa->weight_sparse_matrix = NULL;
    sa->sparsity = 0.0f;
    if (!sa->input_buffer || !sa->output_dense) {
        fprintf(stderr, "sparse_accel_create: malloc failed for buffers\n");
        free(sa->input_buffer); free(sa->output_dense); free(sa);
        return NULL;
    }
    return sa;
}

void sparse_accel_destroy(SparseAccelerator *sa) {
    if (!sa) return;
    if (sa->input_buffer) free(sa->input_buffer);
    if (sa->output_dense) free(sa->output_dense);
    if (sa->weight_sparse_matrix) sparse_csr_destroy(sa->weight_sparse_matrix);
    free(sa);
}

void sparse_accel_load_weights(SparseAccelerator *sa, float *weights, int rows, int cols) {
    if (!sa || !weights) return;
    if (sa->weight_sparse_matrix) sparse_csr_destroy(sa->weight_sparse_matrix);
    sa->weight_sparse_matrix = sparse_csr_create(rows, cols, 0);
    if (sa->weight_sparse_matrix) {
        sparse_csr_from_dense(weights, rows, cols, sa->weight_sparse_matrix);
    }
}

void sparse_accel_compute(SparseAccelerator *sa, float *input, float *output) {
    if (!sa || !input || !output || !sa->weight_sparse_matrix) return;
    sparse_spmm(sa->weight_sparse_matrix, input, sa->weight_sparse_matrix->cols, output);
}

/* ==========================================================================
 * L5: Block-CSR (BCSR) — Fixed-Size Block Sparse Format
 *
 * Groups non-zero values into fixed-size blocks (e.g., 4×4). This improves
 * memory coalescing and register reuse on GPUs. Popularized by DeepMind's
 * Block-Sparse GPU Kernels (Gray et al., 2017).
 *
 * Storage:
 *   - row_ptr: [num_block_rows + 1] — row pointer for block rows
 *   - col_idx: [nnz_blocks] — column index of each block
 *   - values: [nnz_blocks × block_size × block_size] — dense blocks
 *
 * Complexity: O(nnz_blocks × block_size²) vs O(nnz) for CSR
 * When blocks are dense internally, BCSR has lower metadata overhead.
 * ========================================================================== */

BCSRMatrix *bcsr_create(int rows, int cols) {
    BCSRMatrix *bcsr = (BCSRMatrix *)malloc(sizeof(BCSRMatrix));
    if (!bcsr) {
        fprintf(stderr, "bcsr_create: malloc failed\n");
        return NULL;
    }
    memset(bcsr, 0, sizeof(BCSRMatrix));
    bcsr->rows       = rows;
    bcsr->cols       = cols;
    bcsr->block_rows = (rows + BCSR_BLOCK_SIZE - 1) / BCSR_BLOCK_SIZE;
    bcsr->block_cols = (cols + BCSR_BLOCK_SIZE - 1) / BCSR_BLOCK_SIZE;

    bcsr->row_ptr = (int *)calloc(bcsr->block_rows + 1, sizeof(int));
    bcsr->col_idx = NULL;
    bcsr->values  = NULL;
    bcsr->nnz_blocks = 0;

    if (!bcsr->row_ptr) {
        fprintf(stderr, "bcsr_create: malloc failed for row_ptr\n");
        free(bcsr);
        return NULL;
    }
    return bcsr;
}

void bcsr_destroy(BCSRMatrix *bcsr) {
    if (!bcsr) return;
    free(bcsr->row_ptr);
    free(bcsr->col_idx);
    free(bcsr->values);
    free(bcsr);
}

void bcsr_from_dense(float *dense, int rows, int cols, BCSRMatrix *bcsr) {
    if (!dense || !bcsr) return;

    int br = bcsr->block_rows;
    int bc = bcsr->block_cols;
    int bs = BCSR_BLOCK_SIZE;

    /* First pass: count non-zero blocks */
    int nnz_blocks = 0;
    for (int bi = 0; bi < br; bi++) {
        int block_nnz_count = 0;
        for (int bj = 0; bj < bc; bj++) {
            int has_nonzero = 0;
            for (int r = 0; r < bs && !has_nonzero; r++) {
                int ri = bi * bs + r;
                if (ri >= rows) break;
                for (int c = 0; c < bs; c++) {
                    int ci = bj * bs + c;
                    if (ci >= cols) break;
                    if (fabsf(dense[ri * cols + ci]) > 1e-7f) {
                        has_nonzero = 1;
                        break;
                    }
                }
            }
            if (has_nonzero) block_nnz_count++;
        }
        nnz_blocks += block_nnz_count;
    }

    /* Allocate BCSR storage */
    bcsr->nnz_blocks = nnz_blocks;
    free(bcsr->col_idx);
    free(bcsr->values);
    bcsr->col_idx = (int *)malloc(nnz_blocks * sizeof(int));
    bcsr->values  = (float *)calloc(nnz_blocks * bs * bs, sizeof(float));
    if (!bcsr->col_idx || !bcsr->values) {
        fprintf(stderr, "bcsr_from_dense: malloc failed\n");
        bcsr->nnz_blocks = 0;
        return;
    }

    /* Second pass: fill BCSR */
    int block_idx = 0;
    bcsr->row_ptr[0] = 0;
    for (int bi = 0; bi < br; bi++) {
        for (int bj = 0; bj < bc; bj++) {
            int has_nonzero = 0;
            for (int r = 0; r < bs; r++) {
                int ri = bi * bs + r;
                if (ri >= rows) break;
                for (int c = 0; c < bs; c++) {
                    int ci = bj * bs + c;
                    if (ci >= cols) break;
                    float val = dense[ri * cols + ci];
                    bcsr->values[block_idx * bs * bs + r * bs + c] = val;
                    if (fabsf(val) > 1e-7f) has_nonzero = 1;
                }
            }
            if (has_nonzero) {
                bcsr->col_idx[block_idx] = bj;
                block_idx++;
            }
        }
        bcsr->row_ptr[bi + 1] = block_idx;
    }
}

void bcsr_spmm(BCSRMatrix *bcsr, float *dense_x, float *result) {
    if (!bcsr || !dense_x || !result) return;

    int bs = BCSR_BLOCK_SIZE;
    memset(result, 0, bcsr->rows * sizeof(float));

    for (int bi = 0; bi < bcsr->block_rows; bi++) {
        for (int idx = bcsr->row_ptr[bi]; idx < bcsr->row_ptr[bi + 1]; idx++) {
            int bj = bcsr->col_idx[idx];
            float *block = bcsr->values + idx * bs * bs;

            for (int r = 0; r < bs; r++) {
                int ri = bi * bs + r;
                if (ri >= bcsr->rows) break;
                float sum = 0.0f;
                for (int c = 0; c < bs; c++) {
                    int ci = bj * bs + c;
                    if (ci >= bcsr->cols) break;
                    sum += block[r * bs + c] * dense_x[ci];
                }
                result[ri] += sum;
            }
        }
    }
}

/* ==========================================================================
 * L5: ELLPACK Format — Fixed-Width GPU Sparse Matrix
 *
 * Each row has exactly `max_nnz_per_row` entries. Zero-padding fills
 * short rows. Suitable for GPU SIMD where uniform row lengths enable
 * coalesced memory access and regular control flow.
 *
 * This is used in CUSPARSE's ELL format for SpMV on GPUs (NVIDIA).
 * Optimal when per-row non-zero variance is small.
 * ========================================================================== */

ELLMatrix *ell_create(int rows, int cols, int max_nnz_per_row) {
    ELLMatrix *ell = (ELLMatrix *)malloc(sizeof(ELLMatrix));
    if (!ell) {
        fprintf(stderr, "ell_create: malloc failed\n");
        return NULL;
    }
    memset(ell, 0, sizeof(ELLMatrix));
    ell->rows           = rows;
    ell->cols           = cols;
    ell->max_nnz_per_row = max_nnz_per_row;

    int entries = rows * max_nnz_per_row;
    ell->col_idx = (int *)calloc(entries, sizeof(int));
    ell->values  = (float *)calloc(entries, sizeof(float));

    if (!ell->col_idx || !ell->values) {
        fprintf(stderr, "ell_create: malloc failed for arrays\n");
        free(ell->col_idx);
        free(ell->values);
        free(ell);
        return NULL;
    }

    /* Initialize col_idx to -1 (sentinel for padding) */
    for (int i = 0; i < entries; i++) {
        ell->col_idx[i] = -1;
    }
    return ell;
}

void ell_destroy(ELLMatrix *ell) {
    if (!ell) return;
    free(ell->col_idx);
    free(ell->values);
    free(ell);
}

void ell_from_dense(float *dense, int rows, int cols, ELLMatrix *ell) {
    if (!dense || !ell) return;

    int max_nnz = ell->max_nnz_per_row;
    for (int i = 0; i < rows; i++) {
        int nnz_count = 0;
        for (int j = 0; j < cols && nnz_count < max_nnz; j++) {
            float val = dense[i * cols + j];
            if (fabsf(val) > 1e-7f) {
                int idx = i * max_nnz + nnz_count;
                ell->col_idx[idx] = j;
                ell->values[idx]  = val;
                nnz_count++;
            }
        }
        /* Remaining entries are already zero from calloc and col_idx=-1 */
    }
}

void ell_spmv(ELLMatrix *ell, float *x, float *y) {
    if (!ell || !x || !y) return;

    int max_nnz = ell->max_nnz_per_row;
    for (int i = 0; i < ell->rows; i++) {
        float sum = 0.0f;
        for (int k = 0; k < max_nnz; k++) {
            int idx = i * max_nnz + k;
            int col = ell->col_idx[idx];
            if (col < 0) break; /* padding sentinel */
            sum += ell->values[idx] * x[col];
        }
        y[i] = sum;
    }
}

/* ==========================================================================
 * L8: Block-Sparse Attention Pattern
 *
 * Many transformer models exhibit block-sparse attention patterns:
 * local windows, global tokens, and random blocks. This reduces
 * O(N²) attention to O(N × num_blocks) by attending only over
 * selected block pairs.
 *
 * Patterns: sliding window (local), dilated sliding window,
 *           random (BigBird), global+local (Longformer).
 * ========================================================================== */

BlockSparsePattern *block_sparse_create(int num_blocks, int block_size) {
    BlockSparsePattern *bsp = (BlockSparsePattern *)malloc(sizeof(BlockSparsePattern));
    if (!bsp) {
        fprintf(stderr, "block_sparse_create: malloc failed\n");
        return NULL;
    }
    memset(bsp, 0, sizeof(BlockSparsePattern));
    bsp->num_blocks = num_blocks;
    bsp->block_size = block_size;
    int total = num_blocks * num_blocks;

    bsp->block_mask = (int *)calloc(total, sizeof(int));
    bsp->values     = (float *)calloc(total * block_size * block_size, sizeof(float));

    if (!bsp->block_mask || !bsp->values) {
        fprintf(stderr, "block_sparse_create: malloc failed for arrays\n");
        free(bsp->block_mask);
        free(bsp->values);
        free(bsp);
        return NULL;
    }
    return bsp;
}

void block_sparse_destroy(BlockSparsePattern *bsp) {
    if (!bsp) return;
    free(bsp->block_mask);
    free(bsp->values);
    free(bsp);
}

void block_sparse_random_mask(BlockSparsePattern *bsp, float density) {
    if (!bsp) return;
    int nb = bsp->num_blocks;
    /* Always keep diagonal blocks (local attention) */
    for (int i = 0; i < nb; i++) {
        for (int j = 0; j < nb; j++) {
            if (i == j) {
                bsp->block_mask[i * nb + j] = 1;
            } else {
                float r = (float)rand() / (float)RAND_MAX;
                bsp->block_mask[i * nb + j] = (r < density) ? 1 : 0;
            }
        }
    }
}

void block_sparse_matmul(BlockSparsePattern *bsp, float *A, float *B, float *C,
                          int M, int N, int K) {
    if (!bsp || !A || !B || !C) return;

    int nb = bsp->num_blocks;
    int bs = bsp->block_size;

    /* Initialize C to zero */
    memset(C, 0, M * N * sizeof(float));

    int m_blocks = (M + bs - 1) / bs;
    int n_blocks = (N + bs - 1) / bs;
    int k_blocks = (K + bs - 1) / bs;

    for (int mi = 0; mi < m_blocks && mi < nb; mi++) {
        for (int ni = 0; ni < n_blocks && ni < nb; ni++) {
            if (!bsp->block_mask[mi * nb + ni]) continue;

            for (int ki = 0; ki < k_blocks; ki++) {
                for (int r = 0; r < bs; r++) {
                    int mr = mi * bs + r;
                    if (mr >= M) break;
                    for (int c = 0; c < bs; c++) {
                        int nc = ni * bs + c;
                        if (nc >= N) break;
                        float sum = 0.0f;
                        for (int kk = 0; kk < bs; kk++) {
                            int k_idx = ki * bs + kk;
                            if (k_idx >= K) break;
                            sum += A[mr * K + k_idx] * B[k_idx * N + nc];
                        }
                        C[mr * N + nc] += sum;
                    }
                }
            }
        }
    }
}

float block_sparse_compression_ratio(BlockSparsePattern *bsp, int total_blocks) {
    if (!bsp || total_blocks <= 0) return 0.0f;

    int active_blocks = 0;
    int nb = bsp->num_blocks;
    for (int i = 0; i < nb * nb; i++) {
        if (bsp->block_mask[i]) active_blocks++;
    }

    return (float)active_blocks / (float)total_blocks;
}

/* ==========================================================================
 * L5: Iterative Magnitude Pruning
 *
 * Gradual pruning: remove smallest-magnitude weights over multiple iterations.
 * This produces better accuracy than one-shot pruning because the network
 * can recover between pruning steps (Frankle & Carbin, ICLR 2019 -
 * "The Lottery Ticket Hypothesis").
 *
 * Algorithm: Gradual Magnitude Pruning (GMP)
 *   1. Sort weights by magnitude
 *   2. Zero out smallest p% per iteration
 *   3. Repeat until target_sparsity reached
 * ========================================================================== */

static int compare_abs_float(const void *a, const void *b) {
    float fa = fabsf(*(const float *)a);
    float fb = fabsf(*(const float *)b);
    if (fa < fb) return -1;
    if (fa > fb) return 1;
    return 0;
}

void sparse_iterative_prune(float *weights, int rows, int cols,
                             float target_sparsity, int iterations) {
    if (!weights || iterations <= 0) return;

    int total = rows * cols;
    float *sorted = (float *)malloc(total * sizeof(float));
    if (!sorted) return;

    float current_sparsity = sparse_compute_sparsity(weights, rows, cols);
    float sparsity_per_iter = (target_sparsity - current_sparsity) / (float)iterations;

    for (int iter = 0; iter < iterations; iter++) {
        /* Copy current weights, find threshold magnitude */
        memcpy(sorted, weights, total * sizeof(float));
        qsort(sorted, total, sizeof(float), compare_abs_float);

        int keep_count = total - (int)(total * (current_sparsity + sparsity_per_iter));
        if (keep_count >= total) keep_count = total - 1;
        if (keep_count < 1) keep_count = 1;

        float threshold = fabsf(sorted[total - keep_count]);

        /* Zero out weights below threshold */
        for (int i = 0; i < total; i++) {
            if (fabsf(weights[i]) < threshold) {
                weights[i] = 0.0f;
            }
        }

        current_sparsity += sparsity_per_iter;
        if (current_sparsity >= target_sparsity) break;
    }

    free(sorted);
}

float sparse_compute_sparsity(float *weights, int rows, int cols) {
    if (!weights) return 0.0f;
    int total = rows * cols;
    if (total <= 0) return 0.0f;

    int zeros = 0;
    for (int i = 0; i < total; i++) {
        if (fabsf(weights[i]) < 1e-7f) zeros++;
    }
    return (float)zeros / (float)total;
}
