#ifndef MEM_BANDWIDTH_H
#define MEM_BANDWIDTH_H

/**
 * mem_bandwidth.h — Memory Bandwidth Measurement and Analysis
 *
 * L2: Memory bandwidth — Measures sustained DRAM bandwidth under various
 *     access patterns typical of data lake workloads. Critical metric for
 *     columnar scan throughput in lakehouse query engines (DuckDB, Spark).
 *
 * L5: Algorithm — Pointer chasing for latency measurement, streaming triad
 *     for bandwidth saturation. Implements the STREAM benchmark methodology
 *     (McCalpin, 1995) and the Roofline-intrinsic memory characterization
 *     (Williams, Waterman, Patterson, CACM 2009).
 *
 * L8: NUMA-aware bandwidth testing — Measures cross-socket bandwidth
 *     penalties and models optimal data placement strategies for lakehouse
 *     architectures (Spark shuffle, Presto data locality).
 *
 * Universities: CMU 15-418, Stanford CS149, UC Berkeley CS267
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "lake_test_core.h"

/* ============================================================================
 * L1: Memory Bandwidth Measurement Types
 * ============================================================================ */

/** Memory bandwidth test operation (STREAM kernel types) */
typedef enum {
    BW_COPY = 0,    /* A[i] = B[i]         — 2 ops (1R 1W) */
    BW_SCALE = 1,   /* A[i] = q * B[i]     — 2 ops (1R 1W + mul) */
    BW_ADD = 2,     /* A[i] = B[i] + C[i]  — 3 ops (2R 1W) */
    BW_TRIAD = 3,   /* A[i] = B[i] + q*C[i] — 3 ops (2R 1W + mul) */
    BW_POINTER = 4, /* pointer chasing       — pure latency test */
    BW_GATHER = 5,  /* A[B[i]] = C[i]        — random gather */
    BW_SCATTER = 6  /* A[B[i]] = C[i]        — random scatter */
} BwTestOperation;

/** Single bandwidth measurement data point */
typedef struct {
    BwTestOperation operation;
    uint64_t        array_size_bytes;
    uint64_t        num_iterations;
    double          best_bw_mbps;      /* Best observed bandwidth */
    double          avg_bw_mbps;       /* Average bandwidth */
    double          min_bw_mbps;       /* Minimum bandwidth */
    double          bw_stddev_mbps;    /* Standard deviation */
    double          effective_clock_mhz; /* Effective memory clock */
    double          latency_ns;        /* For pointer chasing */
    double          bytes_per_cycle;   /* Bytes transferred per memory cycle */
} BwDataPoint;

/** Complete bandwidth benchmark result */
typedef struct {
    BwDataPoint     *points;
    size_t           num_points;
    double           peak_bw_mbps;     /* Maximum observed across all tests */
    double           sustained_bw_mbps; /* Harmonic mean (fairness-weighted) */
    double           arithmetic_mean_bw;
    double           read_bw_mbps;
    double           write_bw_mbps;
    double           copy_bw_mbps;
} BwBenchResult;

/* ============================================================================
 * L2: NUMA Topology Description
 * ============================================================================ */

/** NUMA node descriptor */
typedef struct {
    uint32_t node_id;
    uint64_t total_memory_bytes;
    uint64_t free_memory_bytes;
    uint32_t cpu_start;
    uint32_t cpu_count;
    double   local_access_latency_ns;
    double   remote_access_latency_ns;
} NumaNode;

/** NUMA topology (up to 16 nodes) */
typedef struct {
    NumaNode nodes[16];
    uint32_t num_nodes;
    double   latency_matrix[16][16];  /* inter-node access latencies */
    double   bandwidth_matrix[16][16]; /* inter-node bandwidth */
} NumaTopology;

/* ============================================================================
 * L3: Memory Hierarchy Profiler
 *
 * Performs a sweep across working set sizes to identify cache and TLB
 * boundaries (L1/L2/L3 sizes, TLB reach). This is the core technique
 * used to reverse-engineer unknown hardware parameters.
 * ============================================================================ */

/** Memory hierarchy boundary point */
typedef struct {
    uint64_t size_bytes;
    double   access_time_ns;
    bool     is_boundary;   /* True if this point is a cache/TLB boundary */
    char     description[128];
} MemHierarchyBoundary;

/** Memory hierarchy sweep result */
typedef struct {
    MemHierarchyBoundary *boundaries;
    size_t                num_boundaries;
    /* Detected parameters */
    uint64_t              detected_l1_size;
    uint64_t              detected_l2_size;
    uint64_t              detected_l3_size;
    uint64_t              detected_l1_tlb_entries;
    uint64_t              detected_l2_tlb_entries;
    uint64_t              detected_page_size;
    uint32_t              detected_cache_line;
} MemHierarchySweep;

/* ============================================================================
 * L1: API Declarations
 * ============================================================================ */

/** Initialize bandwidth test configuration */
void bw_bench_result_init(BwBenchResult *result);

/** 
 * L5: Run a STREAM-style bandwidth benchmark for a specific operation.
 *     Implements the exact computation pattern from McCalpin's STREAM.
 *     The array_size determines the working set; use sizes that exceed 
 *     last-level cache to measure true DRAM bandwidth.
 *     Returns a heap-allocated data point.
 */
BwDataPoint *bw_run_stream_test(BwTestOperation op, uint64_t array_size_bytes,
                                 uint64_t num_iterations);

/** Run all STREAM operations (COPY, SCALE, ADD, TRIAD) and aggregate */
BwBenchResult *bw_run_complete_test(uint64_t array_size_bytes, uint64_t num_iterations);

/** Run pointer chasing latency test (measures true memory latency) */
BwDataPoint *bw_run_pointer_chase(uint64_t array_size_bytes, uint64_t num_chases);

/** Run gather operation bandwidth test */
BwDataPoint *bw_run_gather_test(uint64_t array_size_bytes, uint64_t num_accesses);

/** Run scatter operation bandwidth test */
BwDataPoint *bw_run_scatter_test(uint64_t array_size_bytes, uint64_t num_accesses);

/** Free bandwidth benchmark result */
void bw_bench_result_destroy(BwBenchResult *result);

/** Print bandwidth results in formatted table */
void bw_bench_result_print(const BwBenchResult *result);

/**
 * L4: Roofline model — Compute arithmetic intensity threshold.
 *     AI = FLOPs / Bytes. Peak performance = min(peak_flops, peak_bw * AI).
 *     Returns the ridge point (AI where compute-bound meets memory-bound).
 *     Reference: Williams, Waterman, Patterson (CACM 2009).
 */
double bw_roofline_ridge_point(double peak_flops, double peak_bandwidth);

/**
 * L8: Estimate optimal data placement for NUMA systems.
 *     Uses the latency matrix to compute a cost-minimizing placement.
 *     Returns optimal node placement for given thread count.
 */
void bw_numa_optimal_placement(const NumaTopology *topo, uint32_t num_threads,
                                uint32_t *node_assignments);

/** Initialize NUMA topology with simulated values */
void bw_numa_topology_init(NumaTopology *topo, uint32_t num_nodes);

/** Print NUMA topology */
void bw_numa_topology_print(const NumaTopology *topo);

/** 
 * L5: Run memory hierarchy sweep — benchmark access time across working
 *     set sizes to detect cache and TLB boundaries.
 */
MemHierarchySweep *bw_run_mem_hierarchy_sweep(uint64_t min_size, uint64_t max_size,
                                               uint64_t step_factor);

/** Free memory hierarchy sweep result */
void bw_mem_sweep_destroy(MemHierarchySweep *sweep);

/** Print detected memory hierarchy */
void bw_mem_sweep_print(const MemHierarchySweep *sweep);

/**
 * L3: Compute effective memory parallelism — the number of concurrent
 *     memory requests the hardware can handle (MSHRs / line-fill buffers).
 *     Observed via bandwidth saturation curve.
 */
double bw_measure_effective_parallelism(uint64_t array_size_bytes);

#endif /* MEM_BANDWIDTH_H */