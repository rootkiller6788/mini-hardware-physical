#ifndef SPARSE_ACCEL_H
#define SPARSE_ACCEL_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_SPARSE_ENTRIES 65536

typedef struct {
    int row;
    int col;
    float value;
} SparseEntry;

typedef struct {
    int rows;
    int cols;
    int nnz;
    int *row_ptr;
    int *col_idx;
    float *values;
} SparseMatrix;

typedef struct {
    float *input_buffer;
    SparseMatrix *weight_sparse_matrix;
    float *output_dense;
    float sparsity;
    int input_size;
    int output_size;
} SparseAccelerator;

SparseMatrix *sparse_csr_create(int rows, int cols, int nnz);
void sparse_csr_destroy(SparseMatrix *sm);
void sparse_csr_from_dense(float *dense, int rows, int cols, SparseMatrix *sparse);
void sparse_spmm(SparseMatrix *A, float *dense_B, int K, float *result);
float sparse_compute_reduction(float *A, float *B, int len);
void sparse_2of4_prune(float *weights, int rows, int cols);
void sparse_print_compression_ratio(SparseMatrix *sm, float *dense, int rows, int cols);
SparseAccelerator *sparse_accel_create(int input_size, int output_size);
void sparse_accel_destroy(SparseAccelerator *sa);
void sparse_accel_load_weights(SparseAccelerator *sa, float *weights, int rows, int cols);
void sparse_accel_compute(SparseAccelerator *sa, float *input, float *output);

#endif
