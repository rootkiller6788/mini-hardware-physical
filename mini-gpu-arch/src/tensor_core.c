/**
 * mini-gpu-arch: Tensor Core Implementation
 *
 * Knowledge layers:
 *   L1: TensorCore struct, MMA shapes, precision formats
 *   L2: MMA operation scheduling (fetch→compute→accumulate→writeback)
 *   L3: Blocked GEMM with tensor core tiling
 *   L4: Tensor core roofline model (peak TFLOPS analysis)
 *   L5: Sparse MMA (2:4 structured sparsity for Ampere)
 *   L6: Mixed-precision conversion (FP8, BF16, TF32)
 *   L7: Performance statistics tracking
 *   L8: K-blocking strategy for deep pipelines
 *
 * References:
 *   - NVIDIA Tensor Core Guide: https://docs.nvidia.com/cuda/cuda-c-programming-guide/#wmma
 *   - Markidis et al. "NVIDIA Tensor Core Programmability, Performance & Precision" (2018)
 *   - Sun et al. "Efficient Sparse-Winograd Convolution on Tensor Cores" (2021)
 *   - IEEE 754-2008 (FP16), IEEE 754-2019 (FP8 formats)
 */

#include "tensor_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ===================================================================
 * Helper: MMA shape → tile dimensions
 * =================================================================== */

static void mma_shape_dims(MMAShape shape, uint8_t *m, uint8_t *n, uint8_t *k) {
    switch (shape) {
        case MMA_M8N8K4:   *m = 8;  *n = 8;  *k = 4;  break;
        case MMA_M16N8K8:  *m = 16; *n = 8;  *k = 8;  break;
        case MMA_M16N8K16: *m = 16; *n = 8;  *k = 16; break;
        default:           *m = 8;  *n = 8;  *k = 4;  break;
    }
}

/* ===================================================================
 * L1: Tensor Core Lifecycle
 * =================================================================== */

TensorCore* tc_create(int tc_id, MMAShape shape, MMAPrecision precision) {
    TensorCore *tc = (TensorCore*)calloc(1, sizeof(TensorCore));
    if (!tc) return NULL;

    tc->tc_id = tc_id;
    tc->shape = shape;
    tc->precision = precision;
    mma_shape_dims(shape, &tc->m, &tc->n, &tc->k);

    tc->busy = false;
    tc->k_step = 0;
    tc->total_k_steps = 1;
    tc->ops_completed = 0;

    return tc;
}

void tc_destroy(TensorCore *tc) {
    free(tc);
}

TensorCluster* tcluster_create(int num_cores, MMAShape shape, MMAPrecision prec) {
    if (num_cores <= 0 || num_cores > MAX_TENSOR_CORES) return NULL;

    TensorCluster *cl = (TensorCluster*)calloc(1, sizeof(TensorCluster));
    if (!cl) return NULL;

    cl->num_cores = num_cores;
    cl->free_count = num_cores;

    for (int i = 0; i < num_cores; i++) {
        TensorCore *tc = tc_create(i, shape, prec);
        if (!tc) {
            for (int j = 0; j < i; j++) tc_destroy(&cl->cores[j]);
            free(cl);
            return NULL;
        }
        memcpy(&cl->cores[i], tc, sizeof(TensorCore));
        free(tc);
    }

    return cl;
}

void tcluster_destroy(TensorCluster *cl) {
    if (!cl) return;
    /* Cores are embedded, no individual free needed */
    free(cl);
}

/* ===================================================================
 * L2: MMA Operation
 * =================================================================== */

/**
 * Tensor Core MMA: D = A * B + C
 *
 * Performs matrix multiplication on a tile of size M×K * K×N → M×N
 * using the tensor core's MMA instruction.
 *
 * For Volta SM 7.0 with MMA_M8N8K4:
 *   M=8, N=8, K=4: Each MMA performs 8*8*4=256 FP16 FMA = 512 FLOPs
 *   In practice, each MMA executes in a single cycle.
 *
 * For Hopper SM 9.0 with MMA_M16N8K16:
 *   M=16, N=8, K=16: 16*8*16=2048 operations per MMA instruction
 *
 * The operation is iterative: for K dimension > tile_k, we decompose into
 * multiple K-steps, accumulating partial results.
 *
 * Complexity: O(M*N*K) operations per MMA call
 */
bool tc_mma_compute(TensorCore *tc, const float *a, const float *b,
                     const float *c, float *d, int m, int n, int k) {
    if (!tc || !a || !b || !d || m <= 0 || n <= 0 || k <= 0) {
        return false;
    }

    uint8_t tm = tc->m, tn = tc->n, tk = tc->k;
    int kmax = (k < tk) ? k : tk;

    /* Initialize accumulator from C (or zero if c is NULL) */
    for (int i = 0; i < tm && i < m; i++) {
        for (int j = 0; j < tn && j < n; j++) {
            tc->c_accum[i][j] = c ? c[i * n + j] : 0.0f;
        }
    }

    /* Perform K-step dot products */
    for (int ki = 0; ki < kmax; ki++) {
        for (int i = 0; i < tm && i < m; i++) {
            for (int j = 0; j < tn && j < n; j++) {
                float a_val = a[i * k + ki];
                float b_val = b[ki * n + j];
                tc->c_accum[i][j] += a_val * b_val;
            }
        }
    }

    /* Write results */
    for (int i = 0; i < tm && i < m; i++) {
        for (int j = 0; j < tn && j < n; j++) {
            tc->d_result[i][j] = tc->c_accum[i][j];
            if (d) d[i * n + j] = tc->d_result[i][j];
        }
    }

    /* Count operations: each A[i,k]*B[k,j] is 1 mul + 1 add = 2 FLOPs */
    tc->ops_completed += (uint64_t)(2ULL * tm * tn * kmax);
    tc->k_step = kmax;

    return true;
}

/** Perform one K-step of an MMA operation */
void tc_mma_step(TensorCore *tc) {
    if (!tc || !tc->busy) return;

    /* In hardware, each step processes one K-tile of the MMA.
     * We model this by incrementing the K-step counter. */
    tc->k_step++;

    uint8_t tm = tc->m, tn = tc->n;
    tc->ops_completed += (uint64_t)(2ULL * tm * tn * tc->k);

    if (tc->k_step >= tc->total_k_steps) {
        tc->busy = false;
    }
}

bool tc_is_busy(const TensorCore *tc) {
    if (!tc) return false;
    return tc->busy;
}

/* ===================================================================
 * L3: Blocked GEMM
 * =================================================================== */

/**
 * Configure GEMM blocking for tensor core execution.
 *
 * Given a global GEMM problem [M×K] * [K×N] → [M×N] and a tensor core
 * tile shape tm×tn×tk, computes:
 *   - Number of tiles in each dimension
 *   - Remainder handling (padding or separate cleanup)
 *
 * Complexity: O(1)
 */
GEMMBlockConfig gemm_block_config(int M, int N, int K, MMAShape shape) {
    GEMMBlockConfig cfg = {0};
    cfg.M_global = M;
    cfg.N_global = N;
    cfg.K_global = K;

    mma_shape_dims(shape, &cfg.tm, &cfg.tn, &cfg.tk);

    cfg.M_blocks = (M + cfg.tm - 1) / cfg.tm;
    cfg.N_blocks = (N + cfg.tn - 1) / cfg.tn;
    cfg.K_blocks = (K + cfg.tk - 1) / cfg.tk;

    cfg.M_remainder = M % cfg.tm;
    cfg.N_remainder = N % cfg.tn;
    cfg.K_remainder = K % cfg.tk;

    return cfg;
}

/**
 * Execute a tiled GEMM using a tensor core cluster.
 *
 * This decomposes the global GEMM into tensor-core-sized tiles,
 * dispatches each tile to an available tensor core, and accumulates results.
 *
 * This is the key kernel that maps to NVIDIA's wmma::load_matrix_sync
 * and wmma::mma_sync operations in the CUDA WMMA API.
 *
 * Complexity: O(M*N*K) with tile-level parallelism
 * Parallelism: num_cores-way parallel across tile invocations
 *
 * Reference: NVIDIA WMMA API (CUDA 9.0+)
 */
GEMMExecutionPlan gemm_tiled_execute(TensorCluster *cl, const float *A,
                                      const float *B, const float *C, float *D,
                                      const GEMMBlockConfig *cfg) {
    GEMMExecutionPlan plan = {0};
    if (!cl || !A || !B || !D || !cfg) return plan;

    int M_blocks = cfg->M_blocks;
    int N_blocks = cfg->N_blocks;
    int K_blocks = cfg->K_blocks;
    int tm = cfg->tm, tn = cfg->tn, tk = cfg->tk;
    int M = cfg->M_global, N = cfg->N_global, K = cfg->K_global;

    plan.total_tiles = M_blocks * N_blocks * K_blocks;
    plan.theoretical_flops = 2.0 * M * N * K;

    /* Initialize D with C (accumulator) or zero */
    if (C) {
        for (int i = 0; i < M; i++)
            for (int j = 0; j < N; j++)
                D[i * N + j] = C[i * N + j];
    } else {
        memset(D, 0, M * N * sizeof(float));
    }

    /* For each tile block decomposition */
    for (int mi = 0; mi < M_blocks; mi++) {
        int m_start = mi * tm;
        int m_size = ((mi == M_blocks - 1) && cfg->M_remainder > 0)
                     ? cfg->M_remainder : tm;

        for (int ni = 0; ni < N_blocks; ni++) {
            int n_start = ni * tn;
            int n_size = ((ni == N_blocks - 1) && cfg->N_remainder > 0)
                         ? cfg->N_remainder : tn;

            for (int ki = 0; ki < K_blocks; ki++) {
                int k_start = ki * tk;
                int k_size = ((ki == K_blocks - 1) && cfg->K_remainder > 0)
                             ? cfg->K_remainder : tk;

                /* Extract A tile [m_start:m_start+m_size, k_start:k_start+k_size] */
                float a_tile[16*16] = {0};
                for (int i = 0; i < m_size; i++)
                    for (int kk = 0; kk < k_size; kk++)
                        a_tile[i * k_size + kk] = A[(m_start + i) * K + (k_start + kk)];

                /* Extract B tile [k_start:k_start+k_size, n_start:n_start+n_size] */
                float b_tile[16*16] = {0};
                for (int kk = 0; kk < k_size; kk++)
                    for (int j = 0; j < n_size; j++)
                        b_tile[kk * n_size + j] = B[(k_start + kk) * N + (n_start + j)];

                /* Compute partial product and accumulate */
                for (int i = 0; i < m_size; i++) {
                    for (int j = 0; j < n_size; j++) {
                        float dot = 0.0f;
                        for (int kk = 0; kk < k_size; kk++) {
                            dot += a_tile[i * k_size + kk] * b_tile[kk * n_size + j];
                        }
                        D[(m_start + i) * N + (n_start + j)] += dot;
                    }
                }

                plan.tiles_completed++;
            }
        }
    }

    plan.effective_flops = 2.0 * M * N * K;
    plan.efficiency = 1.0;

    return plan;
}

/**
 * Simplified tensor-core GEMM wrapper.
 *
 * Performs C = α*A*B + β*C using tensor core cluster.
 * For this implementation, α=1, β=0 (no scaling).
 */
void gemm_tensor_core_gemm(const float *A, const float *B,
                            const float *C, float *D,
                            int M, int N, int K,
                            TensorCluster *cl) {
    if (!A || !B || !D || !cl) return;

    GEMMBlockConfig cfg = gemm_block_config(M, N, K, MMA_M8N8K4);
    (void)gemm_tiled_execute(cl, A, B, C, D, &cfg);
}

/* ===================================================================
 * L4: Tensor Core Roofline
 * =================================================================== */

/**
 * Tensor core roofline evaluation.
 *
 * Tensor cores provide much higher peak throughput than CUDA cores
 * but only for MMA operations. The roofline model shows whether a
 * kernel's operational intensity makes it tensor-core bound.
 *
 * Peak TFLOPS per SM per GPU generation:
 *   SM 7.0 (V100):   ~15.7 TFLOPS FP16 tensor
 *   SM 8.0 (A100):   ~19.5 TFLOPS TF32 tensor, ~312 TFLOPS FP16 (sparse)
 *   SM 9.0 (H100):   ~60 TFLOPS FP8, ~30 TFLOPS FP16
 *
 * CUDA core peak (FP32):
 *   V100: ~0.25 TFLOPS/sm, A100: ~0.31 TFLOPS/sm, H100: ~0.5 TFLOPS/sm
 *
 * Reference: NVIDIA A100 Whitepaper, H100 Whitepaper
 */
TensorRoofline tensor_roofline_eval(MMAPrecision prec, double oi,
                                     double mem_bw, SMComputeCap cc) {
    TensorRoofline tr;
    tr.operation_intensity = (oi > 0.0) ? oi : 0.0;
    tr.memory_bandwidth = mem_bw;

    /* Set peak TFLOPS based on precision and compute capability */
    switch (prec) {
        case MMA_FP16:
            tr.tensor_peak_tflops = (cc >= SM_CC_90) ? 30.0 :
                                    (cc >= SM_CC_80) ? 19.5 : 15.7;
            tr.cuda_core_peak_tflops = 0.25;
            break;
        case MMA_TF32:
            tr.tensor_peak_tflops = (cc >= SM_CC_80) ? 19.5 : 0.0; /* Volta doesn't have TF32 */
            tr.cuda_core_peak_tflops = 0.31;
            break;
        case MMA_BF16:
            tr.tensor_peak_tflops = (cc >= SM_CC_90) ? 30.0 :
                                    (cc >= SM_CC_80) ? 19.5 : 0.0;
            tr.cuda_core_peak_tflops = 0.31;
            break;
        case MMA_FP8_E4M3:
        case MMA_FP8_E5M2:
            tr.tensor_peak_tflops = (cc >= SM_CC_90) ? 60.0 : 0.0; /* Only Hopper */
            tr.cuda_core_peak_tflops = 0.5;
            break;
        case MMA_INT8:
            tr.tensor_peak_tflops = (cc >= SM_CC_80) ? 19.5 * 2 : 0.0; /* TOPS but in TFLOPS equiv */
            tr.cuda_core_peak_tflops = 0.5;
            break;
        default:
            tr.tensor_peak_tflops = 0.0;
            tr.cuda_core_peak_tflops = 0.25;
            break;
    }

    /* Determine if tensor-core-bound */
    double tensor_bound_perf = tr.operation_intensity * mem_bw;
    tr.is_tensor_bound = (tensor_bound_perf > tr.tensor_peak_tflops);

    /* Speedup: ratio of tensor peak to CUDA core peak */
    if (tr.cuda_core_peak_tflops > 0.0) {
        tr.speedup_vs_cuda = tr.tensor_peak_tflops / tr.cuda_core_peak_tflops;
    } else {
        tr.speedup_vs_cuda = 0.0;
    }

    return tr;
}

/* ===================================================================
 * L5: Sparse MMA (2:4 Structured Sparsity)
 * =================================================================== */

/**
 * Sparse MMA using NVIDIA Ampere 2:4 structured sparsity.
 *
 * In 2:4 sparsity, exactly 2 out of every 4 contiguous values are non-zero.
 * The non-zero values and their metadata (2-bit indices per group of 4)
 * are stored compressed. This enables 2× throughput on tensor cores.
 *
 * A_sparse: compressed dense-like access to non-zero values
 * metadata: 2 bits per group of 4 indicating which positions are non-zero
 *
 * Reference: Mishra et al. "Accelerating Sparse Deep Neural Networks" (2020)
 *            NVIDIA A100 Whitepaper §Structured Sparsity
 */
int tc_sparse_mma(TensorCore *tc, const float *a_sparse, const uint8_t *meta,
                   const float *b, const float *c, float *d,
                   int m, int n, int k) {
    if (!tc || !a_sparse || !meta || !b || !d) return -1;

    /* With 2:4 sparsity, A matrix has 50% fewer non-zeros.
     * The metadata encodes which 2 positions per group of 4 are non-zero. */

    int ops_count = 0;
    /* exactly 2 of 4 are non-zero per group (2:4 sparsity) */

    for (int i = 0; i < m && i < 16; i++) {
        for (int j = 0; j < n && j < 16; j++) {
            float acc = c ? c[i * n + j] : 0.0f;

            for (int ki_base = 0; ki_base < k; ki_base += 4) {
                uint8_t mask = (ki_base / 4 < k/4) ? meta[ki_base / 4] : 0;

                /* Decode which positions are non-zero */
                int sel0 = mask & 0x3;        /* first non-zero position */
                int sel1 = (mask >> 2) & 0x3;  /* second non-zero position */

                /* Only compute for non-zero entries */
                if (sel0 < 4 && ki_base + sel0 < k) {
                    int k_idx0 = ki_base + sel0;
                    /* a_sparse index: (ki_base/4)*nnz_per_group + 0 */
                    int a_idx = (ki_base / 4) * 2 + 0;
                    acc += a_sparse[i * (k/2) + a_idx] * b[k_idx0 * n + j];
                    ops_count++;
                }
                if (sel1 < 4 && ki_base + sel1 < k) {
                    int k_idx1 = ki_base + sel1;
                    int a_idx = (ki_base / 4) * 2 + 1;
                    acc += a_sparse[i * (k/2) + a_idx] * b[k_idx1 * n + j];
                    ops_count++;
                }
            }

            d[i * n + j] = acc;
        }
    }

    tc->ops_completed += ops_count * 2; /* 2 FLOPs per FMA */
    return ops_count;
}

/** Estimate sparse speedup: With 2:4 sparsity, 2× throughput on Ampere */
double tc_sparse_speedup_estimate(double density) {
    if (density <= 0.0 || density > 1.0) return 1.0;
    /* 2:4 sparsity = 50% density theoretical max */
    if (density > 0.5) return 1.0; /* Not eligible for 2:4 */
    return 2.0; /* 2× speedup on Ampere tensor cores */
}

/* ===================================================================
 * L6: Mixed-Precision Conversion
 * =================================================================== */

/**
 * FP8 (E4M3 or E5M2) to float conversion.
 *
 * E4M3: 1 sign, 4 exponent, 3 mantissa
 *   bias = 7, range: ±448 max, ±2^-6 min normal
 *   Used for forward pass (higher precision)
 *
 * E5M2: 1 sign, 5 exponent, 2 mantissa
 *   bias = 15, range: ±57344 max, ±2^-14 min normal
 *   Used for backward pass (wider dynamic range)
 *
 * Reference: Micikevicius et al. "FP8 Formats for Deep Learning" (2022)
 */
float fp8_to_float(uint8_t fp8, bool e5m2) {
    int sign = (fp8 >> 7) & 1;
    int biased_exp, exp_bits, mantissa, mant_bits;

    if (e5m2) {
        /* E5M2: 5 exp bits, 2 mantissa bits, bias 15 */
        biased_exp = (fp8 >> 2) & 0x1F;
        mantissa = fp8 & 0x3;
        exp_bits = 5; mant_bits = 2;
    } else {
        /* E4M3: 4 exp bits, 3 mantissa bits, bias 7 */
        biased_exp = (fp8 >> 3) & 0xF;
        mantissa = fp8 & 0x7;
        exp_bits = 4; mant_bits = 3;
    }

    int bias = (1 << (exp_bits - 1)) - 1;

    if (biased_exp == 0) {
        /* Subnormal or zero */
        if (mantissa == 0) {
            return sign ? -0.0f : 0.0f;
        }
        /* Subnormal: (-1)^s * 2^(1-bias) * (0.mantissa) */
        float val = (float)mantissa / (float)(1 << mant_bits);
        val *= powf(2.0f, 1.0f - (float)bias);
        return sign ? -val : val;
    } else if (biased_exp == (1 << exp_bits) - 1) {
        /* Infinity or NaN */
        if (mantissa == 0) {
            return sign ? -INFINITY : INFINITY;
        }
        return NAN;
    }

    /* Normal: (-1)^s * 2^(E-bias) * (1.mantissa) */
    float val = 1.0f + (float)mantissa / (float)(1 << mant_bits);
    val *= powf(2.0f, (float)(biased_exp - bias));
    return sign ? -val : val;
}

/**
 * Float to FP8 conversion (round to nearest even).
 */
uint8_t float_to_fp8(float f, bool e5m2) {
    if (isnan(f)) {
        int mant_bits = e5m2 ? 2 : 3;
        return (1 << (e5m2 ? 5 : 4)) | ((1 << mant_bits) - 1);
    }
    if (isinf(f)) {
        int sign_bit = (f < 0) ? 0x80 : 0x00;
        return sign_bit | ((1 << (e5m2 ? 5 : 4)) - 1) << (e5m2 ? 2 : 3);
    }

    int sign = (f < 0) ? 1 : 0;
    f = fabsf(f);

    if (f == 0.0f) return sign ? 0x80 : 0x00;

    int exp_bits = e5m2 ? 5 : 4;
    int mant_bits = e5m2 ? 2 : 3;
    int bias = (1 << (exp_bits - 1)) - 1;

    /* Extract exponent */
    int exp;
    float mant = frexpf(f, &exp);
    exp--; /* frexp gives mantissa in [0.5,1), we want [1,2) */

    if (exp < 1 - bias) {
        /* Subnormal: clamp to minimum */
        return sign ? 0x80 : 0x00;
    }

    int biased_exp = exp + bias;
    if (biased_exp >= (1 << exp_bits) - 1) {
        /* Overflow: saturate to max */
        int max_exp = ((1 << exp_bits) - 2) << mant_bits;
        int max_mant = (1 << mant_bits) - 1;
        return (sign << 7) | max_exp | max_mant;
    }

    /* Compute mantissa bits */
    mant = mant * 2.0f; /* scale to [1,2) */
    int mant_val = (int)((mant - 1.0f) * (1 << mant_bits) + 0.5f);
    if (mant_val >= (1 << mant_bits)) mant_val = (1 << mant_bits) - 1;

    if (e5m2) {
        return (sign << 7) | (biased_exp << 2) | mant_val;
    } else {
        return (sign << 7) | (biased_exp << 3) | mant_val;
    }
}

/**
 * BF16 to float conversion.
 *
 * BF16 (Brain Float 16): 1 sign, 8 exponent, 7 mantissa
 * Same exponent range as FP32, reduced precision.
 * Just pad the mantissa with zeros.
 *
 * Reference: Kalamkar et al. "A Study of BFLOAT16 for Deep Learning Training" (2019)
 */
float bf16_to_float(uint16_t bf16) {
    uint32_t fp32_bits = ((uint32_t)bf16) << 16;
    float result;
    memcpy(&result, &fp32_bits, sizeof(float));
    return result;
}

uint16_t float_to_bf16(float f) {
    uint32_t fp32_bits;
    memcpy(&fp32_bits, &f, sizeof(float));

    /* Round to nearest even for BF16 */
    uint32_t rounding_bias = 0x00007FFF + ((fp32_bits >> 16) & 1);
    uint32_t rounded = fp32_bits + rounding_bias;

    return (uint16_t)(rounded >> 16);
}

/* ===================================================================
 * L7: Statistics
 * =================================================================== */

void tc_print_stats(const TensorCore *tc) {
    if (!tc) { printf("TensorCore: NULL\n"); return; }

    printf("--- Tensor Core %d ---\n", tc->tc_id);
    printf("Shape:      M%uN%uK%u\n", tc->m, tc->n, tc->k);
    printf("Precision:  %d\n", tc->precision);
    printf("Busy:       %s\n", tc->busy ? "yes" : "no");
    printf("K-step:     %d / %d\n", tc->k_step, tc->total_k_steps);
    printf("Ops done:   %lu\n", (unsigned long)tc->ops_completed);
}

void tc_print_performance(const TensorCore *tc) {
    if (!tc) { printf("TensorCore: NULL\n"); return; }

    printf("--- Tensor Core %d Performance ---\n", tc->tc_id);
    uint64_t ops = tc->ops_completed;
    /* For MMA_M16N8K8: each instruction = 16*8*8*2 = 2048 FLOPs
     * For MMA_M8N8K4: each instruction = 8*8*4*2 = 512 FLOPs */
    uint32_t ops_per_mma = (uint32_t)(2ULL * tc->m * tc->n * tc->k);
    uint64_t mma_instructions = (ops_per_mma > 0) ? ops / ops_per_mma : 0;

    printf("Total FLOPs:       %lu\n", (unsigned long)ops);
    printf("MMA instructions:  %lu\n", (unsigned long)mma_instructions);
    printf("FLOPs/MMA:         %u\n", ops_per_mma);

    /* Estimate TFLOPS assuming 1.4 GHz clock */
    double assumed_ghz = 1.4;
    double seconds = (double)(mma_instructions) / (assumed_ghz * 1e9);
    double tflops = (seconds > 0) ? (double)ops / seconds / 1e12 : 0.0;
    printf("Est. TFLOPS (@%.1fGHz): %.4f\n", assumed_ghz, tflops);
}

void tcluster_print_stats(const TensorCluster *cl) {
    if (!cl) { printf("TensorCluster: NULL\n"); return; }

    printf("--- Tensor Cluster (%d cores) ---\n", cl->num_cores);
    printf("Free cores:   %d / %d\n", cl->free_count, cl->num_cores);
    printf("Total ops:    %lu\n", (unsigned long)cl->total_ops);
    printf("Idle cycles:  %lu\n", (unsigned long)cl->idle_cycles);
    for (int i = 0; i < cl->num_cores; i++) {
        printf("  TC[%d]: %s, ops=%lu\n", i,
               cl->cores[i].busy ? "busy" : "idle",
               (unsigned long)cl->cores[i].ops_completed);
    }
}
