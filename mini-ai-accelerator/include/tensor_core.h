#ifndef TENSOR_CORE_H
#define TENSOR_CORE_H

#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * tensor_core.h — Tensor Core Microarchitecture Simulation
 *
 * L5: Algorithms — Warp-level MMA, mixed-precision accumulation
 * L8: Advanced Topics — Fragment-based programming model, precision conversion
 *
 * Simulates the NVIDIA Tensor Core programming model at the warp level.
 * Covers FP16×FP16→FP32 MMA, FP8 (E4M3/E5M2), and structured sparse MMA.
 *
 * Reference: NVIDIA CUDA Programming Guide §7.23 "Warp Matrix Functions"
 *            NVIDIA A100 Tensor Core Architecture Whitepaper
 *
 * Berkeley CS267 · CMU 15-418 · Stanford CS217
 * ========================================================================== */

/* Tile dimensions (NVIDIA-style) */
#define TC_M 16   /* rows of A / rows of C,D */
#define TC_N 16   /* cols of B / cols of C,D */
#define TC_K 16   /* reduction dimension */

/* Fragment storage types */
#define TC_FRAG_SIZE_A (TC_M * TC_K)  /* 256 half-precision elements */
#define TC_FRAG_SIZE_B (TC_K * TC_N)
#define TC_FRAG_SIZE_C (TC_M * TC_N)  /* 256 single-precision elements */
#define TC_FRAG_SIZE_D (TC_M * TC_N)

/* Maximum sparse MMA size (2:4 structured) */
#define TC_SPARSE_M 16
#define TC_SPARSE_N 16
#define TC_SPARSE_K 32  /* doubled K due to 2:4 sparsity */

/* ---- Precision modes ---- */
typedef enum {
    TC_PREC_FP16_FP16_FP32,   /* A=FP16, B=FP16, accum=FP32 (default Volta/Turing) */
    TC_PREC_BF16_BF16_FP32,   /* A=BF16, B=BF16, accum=FP32 (Ampere+) */
    TC_PREC_TF32_TF32_FP32,   /* A=TF32, B=TF32, accum=FP32 (Ampere) */
    TC_PREC_FP8_FP8_FP32,     /* A=FP8 E4M3, B=FP8 E4M3, accum=FP32 (Hopper) */
    TC_PREC_INT8_INT8_INT32,  /* A=INT8, B=INT8, accum=INT32 */
    TC_PREC_INT4_INT4_INT32,  /* A=INT4, B=INT4, accum=INT32 */
    TC_PREC_FP16_SPARSE_FP32  /* 2:4 structured sparse FP16 MMA */
} TensorCorePrecision;

/* ---- Half-precision (FP16) — IEEE 754 binary16 ---- */
typedef struct {
    uint16_t bits;            /* 1 sign | 5 exponent | 10 mantissa */
} float16_t;

/* ---- Brain Float 16 (BF16) — truncated IEEE binary32 ---- */
typedef struct {
    uint16_t bits;            /* 1 sign | 8 exponent | 7 mantissa */
} bfloat16_t;

/* ---- FP8 E4M3 (Hopper) ---- */
typedef struct {
    uint8_t bits;             /* 1 sign | 4 exponent | 3 mantissa */
} float8_e4m3_t;

/* ---- Warp-level fragment descriptor ---- */
typedef struct {
    float *A;                /* [TC_M * TC_K] half or bf16 elements */
    float *B;                /* [TC_K * TC_N] */
    float *C;                /* [TC_M * TC_N] FP32 accumulator input */
    float *D;                /* [TC_M * TC_N] FP32 result */
    int M, N, K;            /* actual dimensions (may be < TC_*) */
    TensorCorePrecision precision;
} TensorCoreFragment;

/* ---- Tensor Core descriptor (one tensor core = one MMA unit) ---- */
typedef struct {
    int num_warps;           /* warps sharing this TC (typically 4) */
    TensorCorePrecision precision;
    int mma_cycle_count;     /* cycles per MMA operation (1 for most precisions) */
    bool sparse_enabled;     /* whether 2:4 structured sparse is active */
    double peak_tflops;      /* peak throughput in TFLOPS */
    uint64_t total_mma_ops;  /* counter: total MMA operations executed */
} TensorCore;

/* ---- FP16 / BF16 / FP8 conversion functions ---- */
float16_t  fp32_to_fp16(float f);
float      fp16_to_fp32(float16_t h);
bfloat16_t fp32_to_bf16(float f);
float      bf16_to_fp32(bfloat16_t b);
float8_e4m3_t fp32_to_fp8_e4m3(float f);
float      fp8_e4m3_to_fp32(float8_e4m3_t v);

/* ---- Tensor Core operations ---- */
TensorCore *tensor_core_create(int num_warps, TensorCorePrecision precision);
void tensor_core_destroy(TensorCore *tc);
void tensor_core_config_precision(TensorCore *tc, TensorCorePrecision precision);

/* MMA: D = A × B + C  (warp-level matrix multiply-accumulate) */
void tensor_core_mma(TensorCore *tc, TensorCoreFragment *frag);

/* Structured sparse MMA: B is 2:4 sparse compressed */
void tensor_core_mma_sparse(TensorCore *tc, float *A, float *B_sparse,
                             int *B_sparse_indices, float *C, float *D,
                             int M, int N, int K);

/* ---- Fragment management ---- */
TensorCoreFragment *tensor_core_fragment_create(int M, int N, int K,
                                                  TensorCorePrecision precision);
void tensor_core_fragment_destroy(TensorCoreFragment *frag);
void tensor_core_fragment_load_A(TensorCoreFragment *frag, float *data, int ld);
void tensor_core_fragment_load_B(TensorCoreFragment *frag, float *data, int ld);
void tensor_core_fragment_load_C(TensorCoreFragment *frag, float *data, int ld);
void tensor_core_fragment_store_D(TensorCoreFragment *frag, float *dst, int ld);

/* ---- Large matrix multiply using repeated tensor core MMA ---- */
void tensor_core_large_matmul(TensorCore *tc, float *A, float *B, float *C,
                               int M, int N, int K, float *D);

/* ---- Performance reporting ---- */
double tensor_core_effective_tflops(TensorCore *tc, int M, int N, int K,
                                     int cycles_taken, double frequency_ghz);
void tensor_core_print_fragment(TensorCoreFragment *frag, const char *label);

/* ---- FLOPS calculation helpers ---- */
double tc_flops_per_mma(int M, int N, int K);
int    tc_cycles_per_mma(TensorCorePrecision precision);

#endif /* TENSOR_CORE_H */
