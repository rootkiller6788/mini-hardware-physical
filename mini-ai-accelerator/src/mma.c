#include "mma.h"
#include "systolic_array.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

MMAEngine *mma_engine_create(int tile_size) {
    MMAEngine *eng = (MMAEngine *)malloc(sizeof(MMAEngine));
    if (!eng) {
        fprintf(stderr, "mma_engine_create: malloc failed\n");
        return NULL;
    }
    if (tile_size > TILE_SIZE) tile_size = TILE_SIZE;
    if (tile_size < 1) tile_size = 1;
    eng->tile_size = tile_size;
    eng->systolic_array = systolic_array_create(tile_size, tile_size);
    if (!eng->systolic_array) {
        fprintf(stderr, "mma_engine_create: systolic array creation failed\n");
        free(eng);
        return NULL;
    }
    eng->tiling_strategy = 0;
    return eng;
}

void mma_engine_destroy(MMAEngine *eng) {
    if (!eng) return;
    if (eng->systolic_array) systolic_array_destroy(eng->systolic_array);
    free(eng);
}

void mma_tile_matmul(MMAEngine *eng, MMATile *A, MMATile *B, MMATile *C, MMATile *D) {
    if (!eng || !A || !B || !C || !D) return;
    int t = eng->tile_size;

    for (int i = 0; i < t; i++) {
        for (int j = 0; j < t; j++) {
            D->data[i][j] = C->data[i][j];
            for (int k = 0; k < t; k++) {
                D->data[i][j] += A->data[i][k] * B->data[k][j];
            }
        }
    }
}

void mma_large_matmul(MMAEngine *eng, float *A, float *B, float *C, int M, int N, int K, float *D) {
    if (!eng || !A || !B || !D) return;
    int ts = eng->tile_size;

    for (int i = 0; i < M * N; i++) {
        D[i] = C ? C[i] : 0.0f;
    }

    int M_tiles = (M + ts - 1) / ts;
    int N_tiles = (N + ts - 1) / ts;
    int K_tiles = (K + ts - 1) / ts;

    MMATile tileA, tileB, tileC, tileD;

    for (int mi = 0; mi < M_tiles; mi++) {
        for (int ni = 0; ni < N_tiles; ni++) {
            for (int ki = 0; ki < K_tiles; ki++) {
                for (int i = 0; i < ts; i++) {
                    for (int j = 0; j < ts; j++) {
                        int ai = mi * ts + i;
                        int ak = ki * ts + j;
                        tileA.data[i][j] = (ai < M && ak < K) ? A[ai * K + ak] : 0.0f;

                        int bk = ki * ts + i;
                        int bj = ni * ts + j;
                        tileB.data[i][j] = (bk < K && bj < N) ? B[bk * N + bj] : 0.0f;

                        int ci = mi * ts + i;
                        int cj = ni * ts + j;
                        tileC.data[i][j] = (ci < M && cj < N) ? D[ci * N + cj] : 0.0f;
                    }
                }

                mma_tile_matmul(eng, &tileA, &tileB, &tileC, &tileD);

                for (int i = 0; i < ts; i++) {
                    for (int j = 0; j < ts; j++) {
                        int di = mi * ts + i;
                        int dj = ni * ts + j;
                        if (di < M && dj < N) {
                            D[di * N + dj] = tileD.data[i][j];
                        }
                    }
                }
            }
        }
    }
}

void mma_conv2d_to_matmul(float *input, float *kernel, int H, int W, int C, int K, int R, int S, float *output) {
    if (!input || !kernel || !output) return;

    int out_h = H - R + 1;
    int out_w = W - S + 1;
    int im2col_rows = out_h * out_w;
    int im2col_cols = C * R * S;

    float *im2col = (float *)calloc(im2col_rows * im2col_cols, sizeof(float));
    if (!im2col) {
        fprintf(stderr, "mma_conv2d_to_matmul: im2col malloc failed\n");
        return;
    }

    for (int oh = 0; oh < out_h; oh++) {
        for (int ow = 0; ow < out_w; ow++) {
            int patch_idx = oh * out_w + ow;
            for (int c = 0; c < C; c++) {
                for (int r = 0; r < R; r++) {
                    for (int s = 0; s < S; s++) {
                        int in_idx = ((oh + r) * W + (ow + s)) * C + c;
                        int im2col_idx = patch_idx * im2col_cols + c * R * S + r * S + s;
                        im2col[im2col_idx] = input[in_idx];
                    }
                }
            }
        }
    }

    for (int k = 0; k < K; k++) {
        for (int oh = 0; oh < out_h; oh++) {
            for (int ow = 0; ow < out_w; ow++) {
                float sum = 0.0f;
                int patch_idx = oh * out_w + ow;
                for (int c = 0; c < C; c++) {
                    for (int r = 0; r < R; r++) {
                        for (int s = 0; s < S; s++) {
                            int kidx = k * C * R * S + c * R * S + r * S + s;
                            int im2col_idx = patch_idx * im2col_cols + c * R * S + r * S + s;
                            sum += im2col[im2col_idx] * kernel[kidx];
                        }
                    }
                }
                output[k * out_h * out_w + oh * out_w + ow] = sum;
            }
        }
    }

    free(im2col);
}

void mma_print_tile(MMATile *t, const char *label) {
    if (!t) return;
    printf("=== %s ===\n", label);
    for (int i = 0; i < TILE_SIZE; i++) {
        for (int j = 0; j < TILE_SIZE; j++) {
            printf("%7.2f ", t->data[i][j]);
        }
        printf("\n");
    }
    printf("========================\n");
}
