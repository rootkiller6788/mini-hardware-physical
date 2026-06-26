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

/* ==========================================================================
 * L5: Winograd Minimal Filtering Algorithm F(2,3)
 *
 * Winograd convolution reduces multiplication count for small convolutions.
 * F(m,r) = minimal filtering: m outputs, r filter size.
 * F(2,3) computes 2 outputs from 3×3 filter with 4×4=16 multiplications
 * instead of 2×3×3=18. Savings = 1 - 16/18 = 11.1%.
 *
 * Transform matrices for F(2,3):
 *   B^T = [[1, 0, -1, 0], [0, 1, 1, 0], [0, -1, 1, 0], [0, 1, 0, -1]]  (input)
 *   G   = [[1, 0, 0], [1/2, 1/2, 1/2], [1/2, -1/2, 1/2], [0, 0, 1]]     (filter)
 *   A^T = [[1, 1, 1, 0], [0, 1, -1, -1]]                                  (output)
 *
 * Algorithm:
 *   U = G · g · G^T       (filter transform, offline)
 *   V = B^T · d · B       (input transform, online / per tile)
 *   M = U ⊙ V              (element-wise multiply in Winograd domain)
 *   Y = A^T · M · A       (output inverse transform)
 *
 * Reference: Lavin & Gray, "Fast Algorithms for Convolutional Neural Networks",
 *            CVPR 2016
 * ========================================================================== */

/* G matrix for F(2,3) — filter transform (4×3) */
static const float G_F23[4][3] = {
    { 1.0f,  0.0f,  0.0f},
    { 0.5f,  0.5f,  0.5f},
    { 0.5f, -0.5f,  0.5f},
    { 0.0f,  0.0f,  1.0f}
};

/* B^T matrix for F(2,3) — input transform (4×4) */
static const float BT_F23[4][4] = {
    { 1.0f,  0.0f, -1.0f,  0.0f},
    { 0.0f,  1.0f,  1.0f,  0.0f},
    { 0.0f, -1.0f,  1.0f,  0.0f},
    { 0.0f,  1.0f,  0.0f, -1.0f}
};

/* A^T matrix for F(2,3) — output inverse transform (2×4) */
static const float AT_F23[2][4] = {
    { 1.0f,  1.0f,  1.0f,  0.0f},
    { 0.0f,  1.0f, -1.0f, -1.0f}
};

/* Winograd precompute: transform a 3×3 filter to 4×4 Winograd domain
 * U = G · g · G^T  where g is [3][3] input, U is [4][4] output */
static void winograd_filter_transform_f23(float g[3][3], float U[4][4]) {
    float tmp[4][3];
    /* tmp = G · g */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 3; j++) {
            tmp[i][j] = 0.0f;
            for (int k = 0; k < 3; k++) {
                tmp[i][j] += G_F23[i][k] * g[k][j];
            }
        }
    }
    /* U = tmp · G^T */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            U[i][j] = 0.0f;
            for (int k = 0; k < 3; k++) {
                U[i][j] += tmp[i][k] * G_F23[j][k];
            }
        }
    }
}

/* Winograd input transform: V = B^T · d · B  where d is [4][4] input tile */
static void winograd_input_transform_f23(float d[4][4], float V[4][4]) {
    float tmp[4][4];
    /* tmp = B^T · d */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = 0.0f;
            for (int k = 0; k < 4; k++) {
                tmp[i][j] += BT_F23[i][k] * d[k][j];
            }
        }
    }
    /* V = tmp · B */
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            V[i][j] = 0.0f;
            for (int k = 0; k < 4; k++) {
                V[i][j] += tmp[i][k] * BT_F23[j][k];
            }
        }
    }
}

/* Winograd output inverse transform: Y = A^T · M · A where M is [4][4], Y is [2][2] */
static void winograd_output_transform_f23(float M[4][4], float Y[2][2]) {
    float tmp[2][4];
    /* tmp = A^T · M */
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 4; j++) {
            tmp[i][j] = 0.0f;
            for (int k = 0; k < 4; k++) {
                tmp[i][j] += AT_F23[i][k] * M[k][j];
            }
        }
    }
    /* Y = tmp · A */
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            Y[i][j] = 0.0f;
            for (int k = 0; k < 4; k++) {
                Y[i][j] += tmp[i][k] * AT_F23[j][k];
            }
        }
    }
}

/* ==========================================================================
 * L5: Winograd Convolution F(2,3) — full 2D convolution
 *
 * Applies Winograd F(2,3) to a 2D convolution with 3×3 filter.
 * Input: H×W, output: (H-2)×(W-2). Processed in 2×2 output tiles.
 *
 * This is the key algorithm used in cuDNN for 3×3 convolutions
 * on NVIDIA GPUs (starting from Volta).
 * ========================================================================== */

void mma_winograd_conv2d_f23(float *input, float *kernel,
                              int H, int W, int C,
                              float *output) {
    if (!input || !kernel || !output) return;
    if (H < 4 || W < 4 || C <= 0) return;

    int out_h = H - 2; /* F(2,3): 2 outputs from input tile of 4 */
    int out_w = W - 2;
    int tiles_h = out_h / 2;
    int tiles_w = out_w / 2;

    /* Step 1: Transform filter (offline, per-channel)
     * For each channel: transform 3×3 kernel to 4×4 Winograd domain */
    float (*U)[4][4] = (float (*)[4][4])calloc(C, 4 * 4 * sizeof(float));
    if (!U) return;

    for (int c = 0; c < C; c++) {
        float g[3][3];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                g[i][j] = kernel[c * 9 + i * 3 + j];
            }
        }
        winograd_filter_transform_f23(g, U[c]);
    }

    /* Step 2: Process input tiles */
    for (int th = 0; th < tiles_h; th++) {
        for (int tw = 0; tw < tiles_w; tw++) {
            int h_start = th * 2;
            int w_start = tw * 2;

            /* For single-channel output: accumulate over C */
            float M[4][4];
            memset(M, 0, sizeof(M));

            for (int c = 0; c < C; c++) {
                /* Extract 4×4 input tile */
                float d[4][4];
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++) {
                        d[i][j] = input[(h_start + i) * W + (w_start + j)];
                    }
                }

                /* Input transform: V = B^T · d · B */
                float V[4][4];
                winograd_input_transform_f23(d, V);

                /* Element-wise multiply in Winograd domain: M += U ⊙ V */
                for (int i = 0; i < 4; i++) {
                    for (int j = 0; j < 4; j++) {
                        M[i][j] += U[c][i][j] * V[i][j];
                    }
                }
            }

            /* Output inverse transform: Y = A^T · M · A */
            float Y[2][2];
            winograd_output_transform_f23(M, Y);

            /* Store output */
            for (int i = 0; i < 2; i++) {
                int out_i = h_start + i;
                if (out_i >= out_h) break;
                for (int j = 0; j < 2; j++) {
                    int out_j = w_start + j;
                    if (out_j >= out_w) break;
                    output[out_i * out_w + out_j] = Y[i][j];
                }
            }
        }
    }

    free(U);
}

/* ==========================================================================
 * L8: Depthwise Convolution with MMA
 *
 * Depthwise separable convolution (MobileNet, Howard et al., 2017):
 * Each input channel has its own filter kernel. This is highly efficient
 * on hardware because it avoids cross-channel reduction.
 *
 * Complexity: H×W×C×K×K for depthwise vs H×W×C×C_out×K×K for standard
 * Speedup factor: C_out (typically 32-1024×)
 * ========================================================================== */

void mma_depthwise_conv2d(float *input, float *kernel,
                           int H, int W, int C, int K_size,
                           float *output) {
    if (!input || !kernel || !output) return;

    int out_h = H - K_size + 1;
    int out_w = W - K_size + 1;
    int khalf = K_size / 2;

    for (int c = 0; c < C; c++) {
        for (int oh = 0; oh < out_h; oh++) {
            for (int ow = 0; ow < out_w; ow++) {
                float sum = 0.0f;
                for (int kh = 0; kh < K_size; kh++) {
                    for (int kw = 0; kw < K_size; kw++) {
                        int in_h = oh + kh;
                        int in_w = ow + kw;
                        float in_val = input[in_h * W * C + in_w * C + c];
                        sum += in_val * kernel[c * K_size * K_size + kh * K_size + kw];
                    }
                }
                output[c * out_h * out_w + oh * out_w + ow] = sum;
            }
        }
    }
}

/* ==========================================================================
 * L8: Im2col + GEMM Convolution Performance Model
 *
 * Models the overhead of im2col transformation:
 * - Memory expansion: im2col matrix is K× (H_out×W_out × C×R×S) vs
 *   original (H×W×C)
 * - Example: 224×224×3 input, 3×3 filter, 64 output channels:
 *   Original activations: 224×224×3 = 150K elements
 *   Im2col matrix: 222×222 × 3×3×3 = 49K × 27 = 1.33M elements
 *   Expansion factor: 1.33M / 150K = 8.9×
 *
 * This is why direct convolution (Winograd, FFT) is preferred
 * over im2col for large images.
 * ========================================================================== */

double mma_im2col_expansion_ratio(int H, int W, int C, int R, int S) {
    int H_out = H - R + 1;
    int W_out = W - S + 1;
    if (H_out <= 0 || W_out <= 0) return 0.0;

    double original   = (double)(H * W * C);
    double im2col_mat = (double)(H_out * W_out) * (double)(C * R * S);

    return im2col_mat / original;
}

/* ==========================================================================
 * L8: Tiling Strategy Optimization
 *
 * For large matrices that don't fit in on-chip SRAM, we must tile them.
 * The optimal tile size balances:
 *   - Large tiles: better reuse, fewer DRAM accesses
 *   - Small tiles: fit in SRAM
 *
 * This function computes the optimal tile size given SRAM capacity.
 *
 * SRAM requirement per tile (MMA):
 *   A_tile: wt × kt × sizeof(float)
 *   B_tile: kt × nt × sizeof(float)
 *   C_tile: wt × nt × sizeof(float)
 *   Total: (wt×kt + kt×nt + wt×nt) × 4 bytes
 *
 * For square tiles (wt=nt=kt=T): 3 × T² × 4 = 12T² bytes
 * For T=128: 12 × 16384 = 196 KB (fits in most modern SRAMs)
 * ========================================================================== */

int mma_optimal_tile_size(int M, int N, int K, int sram_bytes) {
    /* Search for largest tile size that fits in SRAM */
    /* Assume square tiles for simplicity */
    int best_t = 8;
    double best_efficiency = 0.0;

    for (int t = 8; t <= 256; t += 8) {
        /* Triple-buffered: A, B, C tiles */
        int sram_needed = 3 * t * t * (int)sizeof(float);

        if (sram_needed <= sram_bytes) {
            /* Efficiency: ratio of compute to tile overhead */
            int num_tiles_m = (M + t - 1) / t;
            int num_tiles_n = (N + t - 1) / t;
            int num_tiles_k = (K + t - 1) / t;
            int total_tiles = num_tiles_m * num_tiles_n * num_tiles_k;

            /* Prefer fewer tiles (less control overhead) */
            double efficiency = 1.0 / (double)total_tiles;
            if (efficiency > best_efficiency) {
                best_efficiency = efficiency;
                best_t = t;
            }
        }
    }

    return best_t;
}
