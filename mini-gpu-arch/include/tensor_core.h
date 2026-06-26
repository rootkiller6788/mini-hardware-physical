#ifndef GPU_TENSOR_CORE_H
#define GPU_TENSOR_CORE_H

/**
 * mini-gpu-arch: Tensor Core (Matrix Multiply-Accumulate)
 *
 * @L1_Definitions: Tensor core, MMA instruction, warp-level matrix tile
 * @L2_CoreConcepts: Matrix blocking for tensor cores, mixed-precision
 * @L3_EngStructures: MMA pipeline, accumulator staging, tile decomposition
 * @L4_Standards: Roofline-bound compute intensity, Amdahl for tensor offload
 * @L5_Algorithms: Blocked GEMM, K-blocking, tile decomposition for MMA shapes
 *
 * Course mapping:
 *   CMU 15-418: GPU tensor core programming, warp matrix functions
 *   UT Austin CS395T: Deep learning hardware, systolic vs tensor
 *   Stanford CS217: GPU deep learning kernels
 */

#include <stdint.h>
#include <stdbool.h>
#include "shader_core.h"

/* ================================================================
 * L1: Core Definitions
 * ================================================================ */

/** Tensor Core MMA shapes per generation
 *
 *  Volta (SM 7.0):    M8N8K4  FP16→FP32
 *  Turing (SM 7.5):   M8N8K4  FP16, INT8, INT4
 *  Ampere (SM 8.0):   M16N8K8 FP16, BF16, TF32, INT8, INT4, sparse
 *  Hopper (SM 9.0):   M16N8K16 FP16, BF16, TF32, FP8, INT8
 */
typedef enum {
    MMA_M8N8K4  = 0,
    MMA_M16N8K8 = 1,
    MMA_M16N8K16 = 2
} MMAShape;

/** MMA precision formats */
typedef enum {
    MMA_FP16,
    MMA_BF16,
    MMA_TF32,
    MMA_FP8_E4M3,
    MMA_FP8_E5M2,
    MMA_INT8,
    MMA_INT4
} MMAPrecision;

/** Single tensor core descriptor */
typedef struct {
    int    tc_id;
    MMAShape shape;
    MMAPrecision precision;
    uint8_t m, n, k;          /* tile dimensions */

    /* Matrix operands */
    float  a_tile[16][16];    /* input A tile (M×K) */
    float  b_tile[16][16];    /* input B tile (K×N) */
    float  c_accum[16][16];   /* accumulator (M×N) */
    float  d_result[16][16];  /* result = A*B + C */

    /* Pipeline state */
    bool   busy;
    int    k_step;            /* current K dimension step */
    int    total_k_steps;     /* total K iterations for full multiply */
    uint64_t ops_completed;   /* total ops performed */
} TensorCore;

/** Tensor core cluster (multiple TCs per SM) */
#define MAX_TENSOR_CORES 4

typedef struct {
    int         num_cores;
    TensorCore  cores[MAX_TENSOR_CORES];
    int         free_count;
    uint64_t    total_ops;
    uint64_t    idle_cycles;
} TensorCluster;

/* ================================================================
 * L2: MMA Operation
 * ================================================================ */

/** MMA descriptor for a single warp-level matrix multiply */
typedef struct {
    int    m, n, k;
    MMAShape  shape;
    MMAPrecision precision;
    /* Pointers to data in registers/memory */
    const float *a_data;
    const float *b_data;
    const float *c_data;      /* accumulator input */
    float       *d_data;      /* output */
} MMAOperation;

/** MMA scheduling state */
typedef enum {
    MMA_IDLE,
    MMA_FETCH_A,
    MMA_FETCH_B,
    MMA_COMPUTE,
    MMA_ACCUMULATE,
    MMA_WRITEBACK,
    MMA_DONE
} MMAState;

/* ================================================================
 * L3: Matrix Blocking
 * ================================================================ */

/** GEMM blocking configuration
 *
 *  Blocking strategy for tiling [M×K] * [K×N] → [M×N]
 *  with tensor core tile size tm×tn×tk
 */
typedef struct {
    int    M_global, N_global, K_global;  /* full matrix dimensions */
    uint8_t tm, tn, tk;                 /* tensor core tile size */
    int     M_blocks, N_blocks, K_blocks;  /* number of tiles per dimension */
    int     M_remainder, N_remainder, K_remainder;
} GEMMBlockConfig;

/** Tiled GEMM execution plan */
typedef struct {
    int    total_tiles;
    int    tiles_completed;
    double theoretical_flops;
    double effective_flops;
    double efficiency;
} GEMMExecutionPlan;

/* ================================================================
 * L4: Roofline for Tensor Cores
 * ================================================================ */

/** Tensor core roofline model */
typedef struct {
    double   tensor_peak_tflops;    /* peak TFLOPS of tensor cores */
    double   cuda_core_peak_tflops; /* peak TFLOPS of CUDA cores */
    double   memory_bandwidth;      /* GB/s */
    double   operation_intensity;   /* FLOP/byte of GEMM */
    bool     is_tensor_bound;       /* faster on tensor cores */
    double   speedup_vs_cuda;       /* tensor speedup over CUDA cores */
} TensorRoofline;

/* ================================================================
 * L5: Mixed-Precision & Quantization
 * ================================================================ */

/** FP8 scaling descriptor (microscaling blocks) */
typedef struct {
    float    scale;              /* shared scale for block */
    int      block_size;         /* elements per scaling block */
    uint8_t  *fp8_data;          /* raw FP8 data */
    int      num_elements;
} FP8Block;

/** Sparsity pattern for Ampere 2:4 structured sparsity */
typedef struct {
    bool     sparse;             /* 2 of 4 non-zero */
    int      num_nonzeros;
    int      nnz_per_block;
    uint8_t  *metadata;          /* compression metadata */
} SparseDescriptor;

/* ================================================================
 * API Declarations
 * ================================================================ */

/* --- L1: Tensor Core lifecycle --- */
TensorCore*    tc_create(int tc_id, MMAShape shape, MMAPrecision precision);
void           tc_destroy(TensorCore *tc);
TensorCluster* tcluster_create(int num_cores, MMAShape shape, MMAPrecision prec);
void           tcluster_destroy(TensorCluster *cl);

/* --- L2: MMA operations --- */
bool  tc_mma_compute(TensorCore *tc, const float *a, const float *b,
                     const float *c, float *d, int m, int n, int k);
void  tc_mma_step(TensorCore *tc);                   /* one K-step */
bool  tc_is_busy(const TensorCore *tc);

/* --- L3: Blocked GEMM --- */
GEMMBlockConfig  gemm_block_config(int M, int N, int K, MMAShape shape);
GEMMExecutionPlan gemm_tiled_execute(TensorCluster *cl, const float *A,
                                     const float *B, const float *C, float *D,
                                     const GEMMBlockConfig *cfg);
void             gemm_tensor_core_gemm(const float *A, const float *B,
                                       const float *C, float *D,
                                       int M, int N, int K,
                                       TensorCluster *cl);

/* --- L4: Performance modeling --- */
TensorRoofline tensor_roofline_eval(MMAPrecision prec, double oi,
                                     double mem_bw, SMComputeCap cc);

/* --- L5: Sparse tensor acceleration --- */
int    tc_sparse_mma(TensorCore *tc, const float *a_sparse, const uint8_t *meta,
                     const float *b, const float *c, float *d,
                     int m, int n, int k);
double tc_sparse_speedup_estimate(double density);

/* --- L6: Mixed-precision utilities --- */
float  fp8_to_float(uint8_t fp8, bool e5m2);
uint8_t float_to_fp8(float f, bool e5m2);
float  bf16_to_float(uint16_t bf16);
uint16_t float_to_bf16(float f);

/* --- L7: Statistics --- */
void   tc_print_stats(const TensorCore *tc);
void   tc_print_performance(const TensorCore *tc);
void   tcluster_print_stats(const TensorCluster *cl);

#endif /* GPU_TENSOR_CORE_H */
