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

/* ---- L5: Block-CSR (BCSR) — fixed-size block sparse format ---- */
#define BCSR_BLOCK_SIZE 4

typedef struct {
    int rows;
    int cols;
    int block_rows;
    int block_cols;
    int nnz_blocks;
    int *row_ptr;
    int *col_idx;
    float *values;  /* [nnz_blocks * BCSR_BLOCK_SIZE * BCSR_BLOCK_SIZE] */
} BCSRMatrix;

BCSRMatrix *bcsr_create(int rows, int cols);
void bcsr_destroy(BCSRMatrix *bcsr);
void bcsr_from_dense(float *dense, int rows, int cols, BCSRMatrix *bcsr);
void bcsr_spmm(BCSRMatrix *bcsr, float *dense_x, float *result);

/* ---- L5: ELLPACK format — fixed-width sparse storage for GPUs ---- */
#define ELL_MAX_ENTRIES 8192

typedef struct {
    int rows;
    int cols;
    int max_nnz_per_row;
    int *col_idx;   /* [rows * max_nnz_per_row] */
    float *values;  /* [rows * max_nnz_per_row] */
} ELLMatrix;

ELLMatrix *ell_create(int rows, int cols, int max_nnz_per_row);
void ell_destroy(ELLMatrix *ell);
void ell_from_dense(float *dense, int rows, int cols, ELLMatrix *ell);
void ell_spmv(ELLMatrix *ell, float *x, float *y);

/* ---- L8: Block-sparse attention pattern ---- */
typedef struct {
    int block_size;
    int num_blocks;
    int *block_mask;  /* [num_blocks × num_blocks], 1 = active block */
    float *values;    /* [num_blocks × num_blocks × block_size × block_size] */
} BlockSparsePattern;

BlockSparsePattern *block_sparse_create(int num_blocks, int block_size);
void block_sparse_destroy(BlockSparsePattern *bsp);
void block_sparse_random_mask(BlockSparsePattern *bsp, float density);
void block_sparse_matmul(BlockSparsePattern *bsp, float *A, float *B, float *C,
                          int M, int N, int K);
float block_sparse_compression_ratio(BlockSparsePattern *bsp, int total_blocks);

/* ---- L5: Magnitude-based Iterative Pruning ---- */
void sparse_iterative_prune(float *weights, int rows, int cols,
                             float target_sparsity, int iterations);
float sparse_compute_sparsity(float *weights, int rows, int cols);

#endif
