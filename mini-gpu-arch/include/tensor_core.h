#ifndef TENSOR_CORE_H
#define TENSOR_CORE_H

#include <stdbool.h>
#include <stdint.h>

#define TILE_SIZE            4
#define TENSOR_CORES_PER_SM  8

typedef struct {
    float data[TILE_SIZE][TILE_SIZE];
} TileFragment;

typedef struct {
    int   num_tensor_cores;
    int   throughput_fma_per_cycle;
    bool  sparsity_enabled;
} TensorCore;

TensorCore tensor_core_create(int num_cores);
void       tensor_core_mma(const TileFragment *a, const TileFragment *b,
                           const TileFragment *c, TileFragment *d);
void       tensor_core_conv2d_tile(const TileFragment *input,
                                   const TileFragment *kernel,
                                   TileFragment *output);
int        tensor_core_throughput_estimate(int m, int n, int k);
void       tensor_core_matmul_tiled(int m, int n, int k,
                                    const float *a, const float *b,
                                    float *c);
void       tensor_core_print_tile(const TileFragment *t, const char *name);

#endif
