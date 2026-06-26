#ifndef GPU_SIMD_H
#define GPU_SIMD_H

/**
 * mini-gpu-arch: SIMD Execution Engine
 *
 * @L1_Definitions: SIMD lane, vector width, predication mask, vector ALU ops
 * @L2_CoreConcepts: SIMD vs SIMT, warp-level vectorization, gather/scatter
 * @L3_EngStructures: Vector register file, execution mask stack, lane-wise ALU
 * @L4_Standards: Amdahl's Law applied to SIMD fraction, Flynn's Taxonomy
 * @L5_Algorithms: Vector prefix sum, warp reduction, scatter-gather coalescing
 *
 * Course mapping:
 *   CMU 15-418: Parallel Computer Architecture - SIMD/SIMT execution
 *   Stanford CS149: Parallel Computing - GPU vector lanes
 *   Georgia Tech CS 6290: HPCA - Vector processors
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ================================================================
 * L1: Core Definitions
 * ================================================================ */

#define SIMD_MAX_LANES 64
#define SIMD_WARP_SIZE 32

/** Flynn's Taxonomy classification for this unit */
typedef enum {
    FLYNN_SISD = 0,   /* Single Instruction Single Data */
    FLYNN_SIMD = 1,   /* Single Instruction Multiple Data */
    FLYNN_MISD = 2,   /* Multiple Instruction Single Data */
    FLYNN_MIMD = 3    /* Multiple Instruction Multiple Data */
} FlynnClass;

/** Vector lane predication modes */
typedef enum {
    PRED_NONE   = 0,   /* All lanes active */
    PRED_IF     = 1,   /* if-predicate masking */
    PRED_ELSE   = 2,   /* else-predicate masking */
    PRED_WHILE  = 3    /* while-loop masking */
} PredMode;

/** Single SIMD lane state */
typedef struct {
    int      lane_id;
    float    reg;          /* scalar register per lane */
    bool     active;       /* lane enabled under current mask */
    uint32_t pred_mask;   /* per-lane predicate bit */
} SIMDLane;

/** SIMD execution unit with vector width W */
typedef struct {
    uint8_t   num_lanes;          /* W: vector width */
    SIMDLane  lanes[SIMD_MAX_LANES];
    float     vreg[SIMD_MAX_LANES];  /* vector register: vreg[0..W-1] */

    /* Predication / mask stack for divergent control flow */
    uint32_t  exec_mask;           /* current active lane mask */
    uint32_t  mask_stack[16];      /* nested divergence stack */
    int       mask_top;

    /* Statistics */
    uint64_t  cycle_count;
    uint64_t  active_lane_cycles;  /* sum(lanes_active) across cycles */
    uint64_t  total_ops;
} SIMDUnit;

/* ================================================================
 * L2: Data movement operations
 * ================================================================ */

/** Memory access pattern descriptor for gather/scatter analysis */
typedef struct {
    int      stride;          /* uniform stride (1=contiguous, N=strided) */
    bool     is_scatter;      /* true=scatter, false=gather */
    int      indices[SIMD_MAX_LANES];  /* per-lane indices for irregular access */
    int      num_indices;
    bool     is_coalesced;    /* true if all lanes access same cacheline */
} MemAccessPattern;

/** Coalescing analyzer result */
typedef struct {
    int      num_transactions; /* cacheline transactions needed */
    double   efficiency;       /* coalescing efficiency [0,1] */
    int      cacheline_size;   /* bytes per cacheline */
} CoalesceResult;

/* ================================================================
 * L3: Engineering Structures - Vector ALU operations
 * ================================================================ */

typedef enum {
    VOP_ADD, VOP_SUB, VOP_MUL, VOP_DIV,
    VOP_FMA,                     /* fused multiply-add */
    VOP_MAX, VOP_MIN, VOP_ABS,
    VOP_RECIP, VOP_SQRT,
    VOP_AND, VOP_OR, VOP_XOR,
    VOP_SHL, VOP_SHR
} VectorOp;

/* Reduction operation type */
typedef enum {
    REDUCE_SUM, REDUCE_PROD,
    REDUCE_MIN, REDUCE_MAX,
    REDUCE_AND, REDUCE_OR
} ReduceOp;

/* ================================================================
 * L4: Performance Modeling (Amdahl's Law)
 * ================================================================ */

/** Amdahl's Law acceleration model */
typedef struct {
    double   f_s;         /* serial fraction */
    double   f_p;         /* parallelizable fraction (1 - f_s) */
    int      num_processors;
    double   speedup;     /* 1 / (f_s + f_p / N) */
} AmdahlModel;

/* ================================================================
 * API Declarations
 * ================================================================ */

/* --- L1: Creation & Lifecycle --- */
SIMDUnit* simd_create(int num_lanes);
void      simd_destroy(SIMDUnit *u);
void      simd_reset(SIMDUnit *u);

/* --- L2: Vector operations --- */
void  simd_vload(SIMDUnit *u, const float *data, int n);
void  simd_vstore(const SIMDUnit *u, float *out, int n);
void  simd_vbroadcast(SIMDUnit *u, float val);
void  simd_vgather(const SIMDUnit *u, const float *mem_base, const int *indices, float *out, int n);
void  simd_vscatter(const SIMDUnit *u, float *mem_base, const int *indices, const float *vals, int n);

/* --- L3: Vector ALU --- */
void  simd_vop(SIMDUnit *u, VectorOp op, const float *a, const float *b, float *result, int n);
void  simd_vfma(SIMDUnit *u, const float *a, const float *b, float *c, float *result, int n);
void  simd_vreduce(SIMDUnit *u, ReduceOp op, const float *data, int n, float *result);
void  simd_vprefixsum(SIMDUnit *u, const float *data, float *out, int n);

/* --- L4: Predication & Control Flow --- */
void  simd_mask_push(SIMDUnit *u, uint32_t mask);
void  simd_mask_pop(SIMDUnit *u);
void  simd_mask_set(SIMDUnit *u, uint32_t mask);
void  simd_mask_all(SIMDUnit *u);
void  simd_mask_none(SIMDUnit *u);
bool  simd_mask_any(SIMDUnit *u);

/* --- L5: Coalescing & Memory Analysis --- */
CoalesceResult simd_coalesce_analyze(const MemAccessPattern *pat, int cacheline_sz);

/* --- L6: Performance Modeling --- */
AmdahlModel amdahl_compute(double serial_fraction, int num_procs);

/* --- L7: Statistics --- */
void     simd_print_stats(const SIMDUnit *u);
double   simd_lane_utilization(const SIMDUnit *u);
uint64_t simd_total_flops(const SIMDUnit *u);

#endif /* GPU_SIMD_H */
