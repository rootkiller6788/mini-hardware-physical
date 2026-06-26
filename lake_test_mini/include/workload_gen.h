#ifndef WORKLOAD_GEN_H
#define WORKLOAD_GEN_H

/**
 * workload_gen.h — Synthetic Workload Generation for Data Lake Queries
 *
 * L5: Algorithm — Zipfian key generation for skewed data access simulation.
 *     Implements the fast Zipf generator from Gray & Shenoy (IEEE TKDE 2000).
 *     Data lake queries (especially point lookups) often follow Zipf-like
 *     access patterns due to hot keys in partitioning columns.
 *
 * L7: Application — Generates workloads simulating TPC-H, TPC-DS, and
 *     real-world log analytics query patterns. Feeds performance data
 *     to module 7 (data-engine) for query planning and resource estimation.
 *
 * L3: Engineering — Template-based query plan generation with operator-level
 *     cost estimation. Models the Volcano iterator model (Graefe, 1993).
 *
 * Universities: MIT 6.824 (distributed query execution), CMU 15-721
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lake_test_core.h"
#include "io_profile.h"

/* ============================================================================
 * L1: Workload Generation Types
 * ============================================================================ */

/** Query plan operator types (Volcano model) */
typedef enum {
    OP_TABLE_SCAN = 0,
    OP_INDEX_SCAN = 1,
    OP_FILTER = 2,
    OP_PROJECT = 3,
    OP_HASH_JOIN = 4,
    OP_MERGE_JOIN = 5,
    OP_NESTED_LOOP_JOIN = 6,
    OP_HASH_AGG = 7,
    OP_SORT = 8,
    OP_LIMIT = 9,
    OP_UNION = 10
} QueryOpType;

/** Single query plan operator with estimated cost */
typedef struct {
    QueryOpType type;
    double      estimated_rows_in;
    double      estimated_rows_out;
    double      estimated_bytes_in;
    double      estimated_bytes_out;
    double      cpu_cost_ms;
    double      io_cost_ms;
    double      memory_bytes;
    uint32_t    operator_id;
    uint32_t    parent_id;
    char        description[128];
} QueryOperator;

/** Complete query plan */
typedef struct {
    QueryOperator *operators;
    size_t         num_operators;
    double         total_cpu_cost_ms;
    double         total_io_cost_ms;
    double         total_memory_bytes;
    double         peak_memory_bytes;   /* Peak memory during execution */
    bool           is_parallelizable;
    uint64_t       query_id;
    char           query_name[128];
} QueryPlan;

/** Access pattern descriptor for synthetic key generation */
typedef struct {
    uint64_t key_space_size;
    uint64_t num_accesses;
    double   zipf_alpha;    /* Skew parameter: 0=uniform, >1=highly skewed */
    double   sequential_ratio; /* Fraction of accesses that are sequential */
    double   read_ratio;    /* Fraction of reads vs writes */
    uint64_t min_key;
    uint64_t max_key;
} AccessPattern;

/** Generated access sequence */
typedef struct {
    uint64_t *keys;
    size_t    num_keys;
    double    actual_entropy;     /* Measured entropy of generated keys */
    double    actual_skew;        /* Measured Gini coefficient */
} AccessSequence;

/** Workload generation configuration */
typedef struct {
    char            workload_name[64];
    uint64_t        num_tables;
    uint64_t        total_data_gb;
    uint64_t        num_queries;
    double          duration_sec;
    AccessPattern   scan_pattern;
    AccessPattern   lookup_pattern;
    AccessPattern   join_pattern;
    bool            enable_skew;
    double          skew_factor;
    LakeFileFormat  primary_format;
} WorkloadConfig;

/** Complete generated workload */
typedef struct {
    WorkloadConfig   config;
    QueryPlan       *queries;
    size_t           num_queries;
    AccessSequence  *sequences;
    size_t           num_sequences;
    double           total_estimated_io_gb;
    double           total_estimated_cpu_ms;
    uint64_t         total_bytes_processed;
    uint64_t         total_rows_processed;
} GeneratedWorkload;

/* ============================================================================
 * L3: Cost Model (Volcano-style)
 *
 * Implements cost estimation formulas for each operator type.
 * Reference: Graefe, "Volcano — An Extensible and Parallel Query
 * Evaluation System" (IEEE TKDE 1994).
 * ============================================================================ */

/** Cost model parameters */
typedef struct {
    double tuple_size_bytes;
    double cpu_tuple_cost;
    double cpu_operator_cost;
    double cpu_index_tuple_cost;
    double seq_page_cost;
    double random_page_cost;
    double page_size_bytes;
    double hash_join_cost_per_tuple;
    double sort_cost_per_tuple;
    uint64_t effective_cache_size;
    double memory_granularity_bytes;
} CostModelParams;

/** Cost estimate for a query operator */
typedef struct {
    double startup_cost;    /* One-time cost before first output row */
    double total_cost;      /* Total cost (startup + per-tuple) */
    double plan_rows;       /* Estimated output rows */
    double plan_width;      /* Estimated output width in bytes */
} CostEstimate;

/* ============================================================================
 * L1: API Declarations
 * ============================================================================ */

/** Initialize workload configuration with TPC-H-like defaults */
void workload_config_init_tpch(WorkloadConfig *cfg);

/** Initialize workload configuration with TPC-DS-like defaults */
void workload_config_init_tpcds(WorkloadConfig *cfg);

/** Initialize workload configuration for log analytics */
void workload_config_init_log(WorkloadConfig *cfg);

/**
 * L5: Generate a Zipf-distributed access sequence.
 *     Uses the fast rejection-sampling method for Zipf generation.
 *     Step 1: Generate uniform u in [0,1]
 *     Step 2: key = min_key + floor((max_key-min_key+1) * u^(1/(1-alpha)))
 *     O(N) time, O(1) memory per key.
 */
AccessSequence *workload_gen_zipf_sequence(uint64_t key_space, uint64_t num_keys,
                                            double alpha, uint64_t seed);

/** Generate a sequential access sequence */
AccessSequence *workload_gen_sequential(uint64_t start_key, uint64_t num_keys,
                                         uint64_t stride);

/** Generate a mixed access pattern sequence */
AccessSequence *workload_gen_mixed(uint64_t key_space, uint64_t num_keys,
                                    double seq_ratio, double alpha, uint64_t seed);

/** Compute Gini coefficient (skew measure) of an access sequence */
double workload_gen_gini(const uint64_t *keys, size_t num_keys);

/** Compute Shannon entropy of an access sequence */
double workload_gen_entropy(const uint64_t *keys, size_t num_keys);

/** Free an access sequence */
void workload_gen_seq_destroy(AccessSequence *seq);

/** Generate a complete synthetic workload */
GeneratedWorkload *workload_generate(const WorkloadConfig *cfg);

/** Free a generated workload */
void workload_destroy(GeneratedWorkload *wl);

/** Print workload summary */
void workload_print_summary(const GeneratedWorkload *wl);

/** Initialize cost model parameters with PostgreSQL defaults */
void cost_model_init_default(CostModelParams *params);

/** 
 * L5: Estimate cost for a table scan operator.
 *     cost = seq_page_cost * ceil(rel_pages / effective_cache_size_to_buffer)
 *     Reference: PostgreSQL optimizer cost model.
 */
CostEstimate cost_estimate_table_scan(const CostModelParams *params,
                                       uint64_t num_rows, double row_width);

/** Estimate cost for a hash join operator */
CostEstimate cost_estimate_hash_join(const CostModelParams *params,
                                      uint64_t outer_rows, uint64_t inner_rows,
                                      double outer_width, double inner_width);

/** Estimate cost for a sort operator */
CostEstimate cost_estimate_sort(const CostModelParams *params,
                                 uint64_t num_rows, double row_width);

/** Estimate cost for a hash aggregation */
CostEstimate cost_estimate_hash_agg(const CostModelParams *params,
                                     uint64_t num_rows, uint32_t num_groups,
                                     double row_width);

/** Build a complete query plan with cost estimation */
QueryPlan *query_plan_build(const QueryOperator *operators, size_t num_ops,
                             const CostModelParams *cost_params);

/** Print query plan with costs */
void query_plan_print(const QueryPlan *plan);

/** Free a query plan */
void query_plan_destroy(QueryPlan *plan);

/**
 * L4: Little's Law applied to query execution.
 *     L = lambda * W
 *     where L = average concurrent queries, lambda = arrival rate,
 *     W = average query execution time.
 *     Returns the required concurrency for a given QPS target.
 */
uint32_t workload_littles_law_concurrency(double target_qps, double avg_query_time_ms);

/**
 * L4: Amdahl's Law for query parallelization.
 *     Speedup = 1 / ((1-P) + P/N)
 *     where P = parallelizable fraction, N = number of processors.
 */
double workload_amdahl_speedup(double parallel_fraction, uint32_t num_processors);

#endif /* WORKLOAD_GEN_H */