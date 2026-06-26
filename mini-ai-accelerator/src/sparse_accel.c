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
