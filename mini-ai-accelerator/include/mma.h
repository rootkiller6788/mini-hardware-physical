#ifndef MMA_H
#define MMA_H

#include <stdbool.h>
#include <stdint.h>
#include "systolic_array.h"

#define TILE_SIZE 8
#define MAX_TILING 64

typedef struct {
    float data[TILE_SIZE][TILE_SIZE];
} MMATile;

typedef struct {
    int tile_size;
    SystolicArray *systolic_array;
    int tiling_strategy;
} MMAEngine;

MMAEngine *mma_engine_create(int tile_size);
void mma_engine_destroy(MMAEngine *eng);
void mma_tile_matmul(MMAEngine *eng, MMATile *A, MMATile *B, MMATile *C, MMATile *D);
void mma_large_matmul(MMAEngine *eng, float *A, float *B, float *C, int M, int N, int K, float *D);
void mma_conv2d_to_matmul(float *input, float *kernel, int H, int W, int C, int K, int R, int S, float *output);
void mma_print_tile(MMATile *t, const char *label);

#endif
