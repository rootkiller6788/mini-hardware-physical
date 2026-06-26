/* ============================================================================
 * tensor_core.c — Tensor Core Microarchitecture Simulation
 *
 * L5: Algorithms — Warp-level MMA, FP16/BF16/FP8 precision simulation
 * L8: Advanced Topics — Fragment-based programming, structured sparse MMA
 *
 * Key concepts:
 *   - IEEE 754 binary16 (half precision): 1 sign | 5 exp (bias 15) | 10 mantissa
 *   - bfloat16: 1 sign | 8 exp (same range as FP32) | 7 mantissa
 *   - FP8 E4M3: 1 sign | 4 exp (bias 7) | 3 mantissa (Hopper forward)
 *   - MMA: D = A × B + C  (warp collective)
 *
 * References:
 *   - IEEE 754-2008 binary16 format
 *   - NVIDIA CUDA C Programming Guide §7.23
 *   - NVIDIA A100 Tensor Core Architecture Whitepaper
 *   - Micikevicius et al. "Mixed Precision Training", ICLR 2018
 *
 * Berkeley CS267 · CMU 15-418 · Stanford CS217
 * ========================================================================== */

#include "tensor_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ==========================================================================
 * L1: FP16 — IEEE 754 binary16 conversion
 *
 * Format: 1 sign bit, 5 exponent bits (bias=15), 10 mantissa bits
 * Value = (-1)^s × 2^(e-15) × (1 + m/1024)   (for normal numbers)
 * Subnormal: e=0, Value = (-1)^s × 2^(-14) × (m/1024)
 * Special: e=31 (all 1s) → inf or NaN
 * ========================================================================== */

float16_t fp32_to_fp16(float f) {
    float16_t result;
    result.bits = 0;

    if (f == 0.0f) return result; /* +0 */

    uint32_t f_bits;
    memcpy(&f_bits, &f, sizeof(uint32_t));

    uint32_t sign     = (f_bits >> 16) & 0x8000;
    int32_t  exponent = (int32_t)((f_bits >> 23) & 0xFF) - 127; /* remove FP32 bias */
    uint32_t mantissa = f_bits & 0x7FFFFF;

    if ((f_bits & 0x7FFFFFFF) == 0) return result; /* true zero */

    if (exponent > 15) {
        /* Overflow to infinity */
        result.bits = (uint16_t)(sign | 0x7C00);
        return result;
    }

    if (exponent < -14) {
        /* Underflow to zero or subnormal (approximate to zero for simplicity) */
        result.bits = (uint16_t)sign;
        return result;
    }

    /* Normal number: exponent bias 15, mantissa rounded to 10 bits */
    int32_t new_exp  = exponent + 15;
    uint32_t new_mant = (mantissa + 0x1000) >> 13; /* round to nearest, 10 bits */

    /* Handle mantissa overflow */
    if (new_mant >= 0x400) {
        new_mant = 0;
        new_exp++;
    }

    if (new_exp >= 31) {
        /* Overflow to infinity */
        result.bits = (uint16_t)(sign | 0x7C00);
        return result;
    }

    result.bits = (uint16_t)(sign | ((uint32_t)new_exp << 10) | (new_mant & 0x3FF));
    return result;
}

float fp16_to_fp32(float16_t h) {
    uint32_t sign     = (uint32_t)(h.bits & 0x8000) << 16;
    uint32_t exponent = (uint32_t)(h.bits >> 10) & 0x1F;
    uint32_t mantissa = (uint32_t)(h.bits & 0x3FF);

    if (exponent == 0) {
        /* Zero or subnormal: treat subnormal as zero for simplicity */
        uint32_t f_bits = sign;
        float result;
        memcpy(&result, &f_bits, sizeof(float));
        return result;
    }

    if (exponent == 31) {
        /* Infinity or NaN */
        uint32_t f_bits = sign | 0x7F800000 | (mantissa << 13);
        float result;
        memcpy(&result, &f_bits, sizeof(float));
        return result;
    }

    /* Normal: expand exponent and mantissa */
    int32_t fp32_exp = (int32_t)exponent - 15 + 127;
    uint32_t fp32_mant = mantissa << 13;
    uint32_t f_bits = sign | ((uint32_t)fp32_exp << 23) | fp32_mant;

    float result;
    memcpy(&result, &f_bits, sizeof(float));
    return result;
}

/* ==========================================================================
 * L1: BF16 — Brain Float 16 (same exponent range as FP32, 7-bit mantissa)
 *
 * BF16 is a truncation of FP32: keep upper 16 bits
 * Value = (-1)^s × 2^(e-127) × (1 + m/128)  (same exponent bias as FP32!)
 * ========================================================================== */

bfloat16_t fp32_to_bf16(float f) {
    bfloat16_t result;
    uint32_t f_bits;
    memcpy(&f_bits, &f, sizeof(uint32_t));
    /* BF16 = upper 16 bits of FP32, with rounding */
    uint32_t rounding_bias = 0x7FFF + ((f_bits >> 16) & 1);
    uint32_t rounded = f_bits + rounding_bias;
    result.bits = (uint16_t)(rounded >> 16);
    return result;
}

float bf16_to_fp32(bfloat16_t b) {
    uint32_t f_bits = (uint32_t)b.bits << 16;
    float result;
    memcpy(&result, &f_bits, sizeof(float));
    return result;
}

/* ==========================================================================
 * L8: FP8 E4M3 — Hopper forward-pass format
 *
 * Format: 1 sign | 4 exponent (bias=7) | 3 mantissa
 * Max normal: 448.0, Min normal: 2^-6 ≈ 0.015625
 * No infinities (all exponent=15 used for NaN)
 * ========================================================================== */

float8_e4m3_t fp32_to_fp8_e4m3(float f) {
    float8_e4m3_t result;
    result.bits = 0;

    if (f == 0.0f) return result;

    uint32_t f_bits;
    memcpy(&f_bits, &f, sizeof(uint32_t));

    int sign_bit = (f_bits >> 31) & 0x1;
    int exponent = (int)((f_bits >> 23) & 0xFF) - 127;
    uint32_t mantissa = f_bits & 0x7FFFFF;

    if (exponent > 7) {
        /* Overflow: clamp to max (448.0), encoded as 0b0_1110_111 */
        result.bits = (uint8_t)((sign_bit << 7) | 0x77);
        return result;
    }

    if (exponent < -8) {
        /* Underflow to zero */
        result.bits = (uint8_t)(sign_bit << 7);
        return result;
    }

    /* Convert mantissa: FP32 23 bits → FP8 3 bits */
    int new_exp  = exponent + 7; /* bias 7 */
    if (new_exp < 0) {
        /* Subnormal: shift mantissa */
        int shift = -new_exp;
        new_exp = 0;
        uint32_t sub_mant = (0x400000 | mantissa) >> (shift + 20);
        result.bits = (uint8_t)((sign_bit << 7) | (sub_mant & 0x7));
        return result;
    }

    /* Normal: round mantissa to 3 bits */
    uint32_t rounded = mantissa + 0x80000; /* round half up */
    uint32_t new_mant = (rounded >> 20) & 0x07;

    /* Check for mantissa overflow */
    if (new_mant >= 0x8) {
        new_mant = 0;
        new_exp++;
    }

    if (new_exp >= 15) {
        /* Max: 0b0_1110_111 */
        result.bits = (uint8_t)((sign_bit << 7) | 0x77);
        return result;
    }

    result.bits = (uint8_t)((sign_bit << 7) | ((new_exp & 0xF) << 3) | (new_mant & 0x7));
    return result;
}

float fp8_e4m3_to_fp32(float8_e4m3_t v) {
    int sign  = (v.bits >> 7) & 0x1;
    int exp   = (v.bits >> 3) & 0xF;
    int mant  = v.bits & 0x7;

    /* NaN check: exp=15 and mant>0 */
    if (exp == 15) {
        uint32_t f_bits = (uint32_t)(sign << 31) | 0x7FC00000;
        float result;
        memcpy(&result, &f_bits, sizeof(float));
        return result;
    }

    if (exp == 0) {
        /* Subnormal or zero */
        if (mant == 0) {
            uint32_t f_bits = (uint32_t)(sign << 31);
            float result;
            memcpy(&result, &f_bits, sizeof(float));
            return result;
        }
        /* Subnormal: exponent = -6, mantissa = 0.mant */
        float value = (float)mant / 8.0f;
        value *= ldexpf(1.0f, -6);
        return sign ? -value : value;
    }

    /* Normal */
    float value = 1.0f + (float)mant / 8.0f;
    value *= ldexpf(1.0f, exp - 7);
    return sign ? -value : value;
}

/* ==========================================================================
 * L5: Tensor Core Management
 * ========================================================================== */

TensorCore *tensor_core_create(int num_warps, TensorCorePrecision precision) {
    TensorCore *tc = (TensorCore *)malloc(sizeof(TensorCore));
    if (!tc) {
        fprintf(stderr, "tensor_core_create: malloc failed\n");
        return NULL;
    }
    memset(tc, 0, sizeof(TensorCore));
    tc->num_warps      = num_warps;
    tc->precision      = precision;
    tc->sparse_enabled  = (precision == TC_PREC_FP16_SPARSE_FP32);
    tc->mma_cycle_count = tc_cycles_per_mma(precision);
    tc->total_mma_ops   = 0;

    /* Compute peak TFLOPS based on precision and warps */
    double flops_per_mma = 2.0 * TC_M * TC_N * TC_K;
    tc->peak_tflops = flops_per_mma * num_warps / (double)tc->mma_cycle_count * 1e-9;
    /* 1e-9: assume 1GHz reference, actual scales with frequency */

    return tc;
}

void tensor_core_destroy(TensorCore *tc) {
    if (tc) free(tc);
}

void tensor_core_config_precision(TensorCore *tc, TensorCorePrecision precision) {
    if (!tc) return;
    tc->precision       = precision;
    tc->sparse_enabled   = (precision == TC_PREC_FP16_SPARSE_FP32);
    tc->mma_cycle_count  = tc_cycles_per_mma(precision);
}

/* ==========================================================================
 * L5: Cycles per MMA operation per precision
 *
 * Volta/Turing: FP16 MMA = 4 cycles (warp scheduling)
 * Ampere: FP16 = 1 cycle, TF32 = 1 cycle
 * Hopper: FP8 = 1 cycle
 *
 * For simulation purposes, we use simplified values.
 * ========================================================================== */

int tc_cycles_per_mma(TensorCorePrecision precision) {
    switch (precision) {
        case TC_PREC_FP16_FP16_FP32:  return 4;
        case TC_PREC_BF16_BF16_FP32:  return 4;
        case TC_PREC_TF32_TF32_FP32:  return 2;
        case TC_PREC_FP8_FP8_FP32:    return 1;
        case TC_PREC_INT8_INT8_INT32: return 1;
        case TC_PREC_INT4_INT4_INT32: return 1;
        case TC_PREC_FP16_SPARSE_FP32:return 4;
        default:                       return 4;
    }
}

double tc_flops_per_mma(int M, int N, int K) {
    /* 2 operations (multiply+add) per element per reduction step */
    return 2.0 * M * N * K;
}

/* ==========================================================================
 * L5: Fragment Management (NVIDIA PTX-style)
 *
 * Fragments are warp-local register arrays that hold portions of matrices.
 * A [M×K], B [K×N], C/D [M×N] are distributed across 32 threads in a warp.
 *
 * In our simplified model, we store the full fragment logically and
 * track the total FLOPs.
 * ========================================================================== */

TensorCoreFragment *tensor_core_fragment_create(int M, int N, int K,
                                                  TensorCorePrecision precision) {
    TensorCoreFragment *frag = (TensorCoreFragment *)malloc(sizeof(TensorCoreFragment));
    if (!frag) {
        fprintf(stderr, "tensor_core_fragment_create: malloc failed\n");
        return NULL;
    }
    memset(frag, 0, sizeof(TensorCoreFragment));

    frag->M = (M > TC_M) ? TC_M : M;
    frag->N = (N > TC_N) ? TC_N : N;
    frag->K = (K > TC_K) ? TC_K : K;
    frag->precision = precision;

    frag->A = (float *)calloc(TC_M * TC_K, sizeof(float));
    frag->B = (float *)calloc(TC_K * TC_N, sizeof(float));
    frag->C = (float *)calloc(TC_M * TC_N, sizeof(float));
    frag->D = (float *)calloc(TC_M * TC_N, sizeof(float));

    if (!frag->A || !frag->B || !frag->C || !frag->D) {
        fprintf(stderr, "tensor_core_fragment_create: fragment alloc failed\n");
        free(frag->A); free(frag->B); free(frag->C); free(frag->D);
        free(frag);
        return NULL;
    }

    return frag;
}

void tensor_core_fragment_destroy(TensorCoreFragment *frag) {
    if (!frag) return;
    free(frag->A);
    free(frag->B);
    free(frag->C);
    free(frag->D);
    free(frag);
}

/* Load data into fragment with leading dimension ld (column-major or row-major) */
void tensor_core_fragment_load_A(TensorCoreFragment *frag, float *data, int ld) {
    if (!frag || !data) return;
    for (int i = 0; i < frag->M; i++) {
        for (int k = 0; k < frag->K; k++) {
            frag->A[i * TC_K + k] = data[i * ld + k];
        }
    }
}

void tensor_core_fragment_load_B(TensorCoreFragment *frag, float *data, int ld) {
    if (!frag || !data) return;
    for (int k = 0; k < frag->K; k++) {
        for (int j = 0; j < frag->N; j++) {
            frag->B[k * TC_N + j] = data[k * ld + j];
        }
    }
}

void tensor_core_fragment_load_C(TensorCoreFragment *frag, float *data, int ld) {
    if (!frag || !data) return;
    for (int i = 0; i < frag->M; i++) {
        for (int j = 0; j < frag->N; j++) {
            frag->C[i * TC_N + j] = data[i * ld + j];
        }
    }
}

void tensor_core_fragment_store_D(TensorCoreFragment *frag, float *dst, int ld) {
    if (!frag || !dst) return;
    for (int i = 0; i < frag->M; i++) {
        for (int j = 0; j < frag->N; j++) {
            dst[i * ld + j] = frag->D[i * TC_N + j];
        }
    }
}

/* ==========================================================================
 * L5: Tensor Core MMA — D = A × B + C
 *
 * Implements the core MMA operation at the warp level with precision conversion.
 * Precision simulation: FP16 inputs are converted to FP32, accumulated in FP32,
 * then (optionally) rounded back.
 *
 * For FP16/BF16/FP8 simulation: convert to FP32, compute, simulate rounding error
 * by converting inputs to target precision and back.
 * ========================================================================== */

static float precision_round_input(float val, TensorCorePrecision precision) {
    switch (precision) {
        case TC_PREC_FP16_FP16_FP32:
        case TC_PREC_FP16_SPARSE_FP32: {
            float16_t h = fp32_to_fp16(val);
            return fp16_to_fp32(h);
        }
        case TC_PREC_BF16_BF16_FP32: {
            bfloat16_t b = fp32_to_bf16(val);
            return bf16_to_fp32(b);
        }
        case TC_PREC_TF32_TF32_FP32: {
            /* TF32: FP32 with mantissa truncated to 10 bits (like FP16 mantissa) */
            uint32_t bits;
            memcpy(&bits, &val, sizeof(uint32_t));
            bits &= 0xFFFFE000; /* keep sign(1)+exp(8)+mantissa(10)=19 bits, zero lower 13 */
            float result;
            memcpy(&result, &bits, sizeof(float));
            return result;
        }
        case TC_PREC_FP8_FP8_FP32: {
            float8_e4m3_t v8 = fp32_to_fp8_e4m3(val);
            return fp8_e4m3_to_fp32(v8);
        }
        case TC_PREC_INT8_INT8_INT32:
            /* INT8: clamp and round */
            val = roundf(val);
            if (val > 127.0f) val = 127.0f;
            if (val < -128.0f) val = -128.0f;
            return val;
        case TC_PREC_INT4_INT4_INT32:
            /* INT4: clamp and round to [-8, 7] */
            val = roundf(val);
            if (val > 7.0f) val = 7.0f;
            if (val < -8.0f) val = -8.0f;
            return val;
        default:
            return val;
    }
}

void tensor_core_mma(TensorCore *tc, TensorCoreFragment *frag) {
    if (!tc || !frag) return;

    int M = frag->M;
    int N = frag->N;
    int K = frag->K;

    /* D = round_to_precision(A) × round_to_precision(B) + C */
    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            float sum = frag->C[i * TC_N + j];
            for (int k = 0; k < K; k++) {
                float a_val = precision_round_input(frag->A[i * TC_K + k], frag->precision);
                float b_val = precision_round_input(frag->B[k * TC_N + j], frag->precision);
                sum += a_val * b_val;
            }
            frag->D[i * TC_N + j] = sum;
        }
    }

    tc->total_mma_ops++;
}

/* ==========================================================================
 * L8: Structured Sparse MMA (2:4 pattern)
 *
 * NVIDIA Ampere+: 2:4 structured sparse MMA — B matrix has exactly 2 non-zero
 * values per group of 4 elements in the K dimension. The hardware skips
 * the zero multiplies, effectively doubling throughput.
 *
 * B_sparse: compressed B with only non-zero values [K/2][N]
 * B_sparse_indices: metadata indicating which 2 of 4 are non-zero
 *   (2 bits per group of 4)
 * ========================================================================== */

void tensor_core_mma_sparse(TensorCore *tc, float *A, float *B_sparse,
                             int *B_sparse_indices, float *C, float *D,
                             int M, int N, int K) {
    if (!tc || !A || !B_sparse || !B_sparse_indices || !D) return;

    /* K must be multiple of 2 for 2:4 sparsity (4 elements → 2 kept) */
    /* B_sparse has K/2 columns per row, nz_indices tells which of the 2 per group */

    for (int i = 0; i < M; i++) {
        for (int j = 0; j < N; j++) {
            float sum = C ? C[i * N + j] : 0.0f;
            int sparse_idx = 0;
            for (int k = 0; k < K; k += 4) {
                int pattern = B_sparse_indices[k / 4];
                for (int g = 0; g < 4; g++) {
                    /* Check if position k+g is kept (bit g set in pattern) */
                    if ((pattern >> g) & 1) {
                        /* B_sparse has the non-zero value */
                        float b_val = 0.0f;
                        if (sparse_idx < K / 2) {
                            b_val = B_sparse[sparse_idx * N + j];
                        }
                        sum += A[i * K + (k + g)] * b_val;
                        sparse_idx++;
                    }
                }
            }
            D[i * N + j] = sum;
        }
    }
}

/* ==========================================================================
 * L5: Large matrix multiply using repeated Tensor Core MMA invocations
 *
 * Splits large matrices into TC_M×TC_K and TC_K×TC_N tiles, accumulates
 * in FP32. This follows the same tiling strategy as CUDA warp MMA.
 * ========================================================================== */

void tensor_core_large_matmul(TensorCore *tc, float *A, float *B, float *C,
                               int M, int N, int K, float *D) {
    if (!tc || !A || !B || !D) return;

    /* Initialize D with C if provided */
    for (int i = 0; i < M * N; i++) {
        D[i] = C ? C[i] : 0.0f;
    }

    TensorCoreFragment *frag = tensor_core_fragment_create(TC_M, TC_N, TC_K, tc->precision);
    if (!frag) return;

    int M_tiles = (M + TC_M - 1) / TC_M;
    int N_tiles = (N + TC_N - 1) / TC_N;
    int K_tiles = (K + TC_K - 1) / TC_K;

    for (int mt = 0; mt < M_tiles; mt++) {
        for (int nt = 0; nt < N_tiles; nt++) {
            int m_start = mt * TC_M;
            int m_end   = (m_start + TC_M <= M) ? m_start + TC_M : M;
            int m_act   = m_end - m_start;

            int n_start = nt * TC_N;
            int n_end   = (n_start + TC_N <= N) ? n_start + TC_N : N;
            int n_act   = n_end - n_start;

            /* Load C tile with zero-padding */
            for (int i = 0; i < TC_M; i++) {
                for (int j = 0; j < TC_N; j++) {
                    frag->C[i * TC_N + j] = 0.0f;
                }
            }
            for (int i = 0; i < m_act; i++) {
                for (int j = 0; j < n_act; j++) {
                    frag->C[i * TC_N + j] = D[(m_start + i) * N + (n_start + j)];
                }
            }

            for (int kt = 0; kt < K_tiles; kt++) {
                int k_start = kt * TC_K;
                int k_end   = (k_start + TC_K <= K) ? k_start + TC_K : K;
                int k_act   = k_end - k_start;

                /* Load A tile [m_act × k_act] (zero-padded to TC_M×TC_K) */
                for (int i = 0; i < TC_M; i++) {
                    for (int k = 0; k < TC_K; k++) {
                        frag->A[i * TC_K + k] = 0.0f;
                    }
                }
                for (int i = 0; i < m_act; i++) {
                    for (int k = 0; k < k_act; k++) {
                        frag->A[i * TC_K + k] = A[(m_start + i) * K + (k_start + k)];
                    }
                }

                /* Load B tile [k_act × n_act] */
                for (int k = 0; k < TC_K; k++) {
                    for (int j = 0; j < TC_N; j++) {
                        frag->B[k * TC_N + j] = 0.0f;
                    }
                }
                for (int k = 0; k < k_act; k++) {
                    for (int j = 0; j < n_act; j++) {
                        frag->B[k * TC_N + j] = B[(k_start + k) * N + (n_start + j)];
                    }
                }

                frag->M = TC_M;
                frag->N = TC_N;
                frag->K = TC_K;
                tensor_core_mma(tc, frag);

                /* Accumulate: C = D (accumulated) */
                for (int i = 0; i < TC_M; i++) {
                    for (int j = 0; j < TC_N; j++) {
                        frag->C[i * TC_N + j] = frag->D[i * TC_N + j];
                    }
                }
            }

            /* Store result tile */
            for (int i = 0; i < m_act; i++) {
                for (int j = 0; j < n_act; j++) {
                    D[(m_start + i) * N + (n_start + j)] = frag->D[i * TC_N + j];
                }
            }
        }
    }

    tensor_core_fragment_destroy(frag);
}

/* ==========================================================================
 * L5: Performance Reporting
 * ========================================================================== */

double tensor_core_effective_tflops(TensorCore *tc, int M, int N, int K,
                                     int cycles_taken, double frequency_ghz) {
    if (!tc || cycles_taken <= 0 || frequency_ghz <= 0.0) return 0.0;
    double flops = 2.0 * M * N * K;
    double seconds = (double)cycles_taken / (frequency_ghz * 1e9);
    if (seconds <= 0.0) return 0.0;
    return flops / seconds / 1e12;
}

void tensor_core_print_fragment(TensorCoreFragment *frag, const char *label) {
    if (!frag) return;
    printf("=== Fragment: %s [%dx%d] C/D ===\n", label, frag->M, frag->N);
    for (int i = 0; i < frag->M; i++) {
        printf("  ");
        for (int j = 0; j < frag->N; j++) {
            printf("%8.2f ", frag->D[i * TC_N + j]);
        }
        printf("\n");
    }
    printf("========================\n");
}
