#include "tensor_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

TensorCore tensor_core_create(int num_cores)
{
    TensorCore tc;
    tc.num_tensor_cores = num_cores > 0 ? num_cores : TENSOR_CORES_PER_SM;
    tc.throughput_fma_per_cycle = tc.num_tensor_cores * 64;
    tc.sparsity_enabled = false;
    return tc;
}

/* D = A * B + C, 4x4矩阵乘法累加 */
void tensor_core_mma(const TileFragment *a, const TileFragment *b,
                     const TileFragment *c, TileFragment *d)
{
    for (int row = 0; row < TILE_SIZE; row++) {
        for (int col = 0; col < TILE_SIZE; col++) {
            float sum = c->data[row][col];
            for (int k = 0; k < TILE_SIZE; k++) {
                sum += a->data[row][k] * b->data[k][col];
            }
            d->data[row][col] = sum;
        }
    }
}

/* 卷积tile: output = input (*) kernel (简化为4x4) */
void tensor_core_conv2d_tile(const TileFragment *input,
                             const TileFragment *kernel,
                             TileFragment *output)
{
    for (int i = 0; i < TILE_SIZE; i++) {
        for (int j = 0; j < TILE_SIZE; j++) {
            float sum = 0.0f;
            for (int ki = 0; ki < TILE_SIZE; ki++) {
                for (int kj = 0; kj < TILE_SIZE; kj++) {
                    float in_val = input->data[(i + ki) % TILE_SIZE][(j + kj) % TILE_SIZE];
                    sum += in_val * kernel->data[ki][kj];
                }
            }
            output->data[i][j] = sum;
        }
    }
}

/* 估计MxNxK矩阵乘法所需周期数 */
int tensor_core_throughput_estimate(int m, int n, int k)
{
    /* 每个tensor core每周期做64次FMA (4x4x4 MMA) */
    int total_fmas = m * n * k;
    int fmas_per_cycle = TENSOR_CORES_PER_SM * 64;
    return (total_fmas + fmas_per_cycle - 1) / fmas_per_cycle;
}

/* 通用矩阵乘法（分块4x4） */
void tensor_core_matmul_tiled(int m, int n, int k,
                              const float *a, const float *b,
                              float *c)
{
    memset(c, 0, m * n * sizeof(float));

    for (int i = 0; i < m; i += TILE_SIZE) {
        for (int j = 0; j < n; j += TILE_SIZE) {
            for (int kk = 0; kk < k; kk += TILE_SIZE) {
                /* 对于每个4x4 tile块 */
                int m_end = (i + TILE_SIZE < m) ? i + TILE_SIZE : m;
                int n_end = (j + TILE_SIZE < n) ? j + TILE_SIZE : n;
                int k_end = (kk + TILE_SIZE < k) ? kk + TILE_SIZE : k;

                for (int ii = i; ii < m_end; ii++) {
                    for (int jj = j; jj < n_end; jj++) {
                        float sum = 0.0f;
                        for (int k1 = kk; k1 < k_end; k1++) {
                            sum += a[ii * k + k1] * b[k1 * n + jj];
                        }
                        c[ii * n + jj] += sum;
                    }
                }
            }
        }
    }
}

void tensor_core_print_tile(const TileFragment *t, const char *name)
{
    printf("%s:\n", name);
    for (int i = 0; i < TILE_SIZE; i++) {
        printf("  ");
        for (int j = 0; j < TILE_SIZE; j++) {
            printf("%8.2f ", t->data[i][j]);
        }
        printf("\n");
    }
}
