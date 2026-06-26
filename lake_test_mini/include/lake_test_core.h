#ifndef LAKE_TEST_CORE_H
#define LAKE_TEST_CORE_H

/**
 * lake_test_core.h — Hardware Testing Core Definitions for Data Lake Workloads
 *
 * L1: Core type definitions (struct/typedef/enum) for hardware testing framework.
 *     Defines the fundamental data structures used throughout the lake_test system:
 *     test configurations, benchmark results, hardware performance counters,
 *     test suite orchestration, and data lake workload profiles.
 *
 * L2: Core concept — Hardware benchmarking for data-intensive workloads.
 *     Data lake/lakehouse systems stress hardware in unique ways (large sequential
 *     scans, random point queries, heavy memory pressure, I/O bandwidth saturation).
 *     This module provides the type system to model these workloads at the hardware level.
 *
 * Universities: MIT 6.004, CMU 18-447, Stanford EE282
 *
 * Cross-module integration: 
 *   Feeds performance metrics to data-engine(7) for query planning optimization.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * L1: Hardware Counter Types — Model CPU performance counters
 * Reference: Intel SDM Vol.3 Ch.18-20, ARM PMU
 * ============================================================================ */

/** Individual hardware performance counter */
typedef struct {
    const char *name;
    uint64_t    value;
    uint64_t    raw;          /* raw counter value before scaling */
    double      scaled;       /* scaled value (e.g. per-second rate) */
    bool        overflowed;   /* whether the counter overflowed during measurement */
} HwCounter;

/** Collection of hardware counters sampled during a test run */
typedef struct {
    uint64_t instructions_retired;
    uint64_t cpu_cycles;
    uint64_t cache_l1_hits;
    uint64_t cache_l1_misses;
    uint64_t cache_l2_hits;
    uint64_t cache_l2_misses;
    uint64_t cache_l3_hits;
    uint64_t cache_l3_misses;
    uint64_t branch_instructions;
    uint64_t branch_mispredictions;
    uint64_t tlb_hits;
    uint64_t tlb_misses;
    uint64_t page_faults;
    double   ipc;              /* instructions per cycle */
    double   branch_miss_rate;
    double   l1_miss_rate;
    double   l2_miss_rate;
    double   l3_miss_rate;
    double   tlb_miss_rate;
} HwCounterSet;

/* ============================================================================
 * L1: Test Configuration — Defines how a benchmark test is configured
 * ============================================================================ */

/** Test repeat mode: single-run or statistical multi-run */
typedef enum {
    TEST_MODE_SINGLE = 0,
    TEST_MODE_STATISTICAL = 1,
    TEST_MODE_STRESS = 2,
    TEST_MODE_SWEEP = 3
} TestMode;

/** I/O access pattern characterization for data lake workloads */
typedef enum {
    IO_SEQUENTIAL = 0,    /* Full table scans (typical in lakehouse analytics) */
    IO_RANDOM = 1,        /* Point queries via index lookups */
    IO_MIXED = 2,         /* Hybrid: scan with occasional seeks */
    IO_STRIDED = 3,       /* Columnar access patterns (Parquet/ORC column chunks) */
    IO_ZIPFIAN = 4        /* Zipf-distributed popularity (real-world access skew) */
} IoAccessPattern;

/** Memory access pattern for workload characterization */
typedef enum {
    MEM_LINEAR = 0,       /* Unit-stride sequential */
    MEM_RANDOM = 1,       /* Uniform random across working set */
    MEM_POINTER_CHASE = 2,/* Linked-list traversal (measures true latency) */
    MEM_STRIDED_2 = 3,    /* Stride-2 (tests cache bank conflicts) */
    MEM_STRIDED_8 = 4,    /* Stride-8 (tests cache line utilization) */
    MEM_STRIDED_64 = 5    /* Stride-64 (tests prefetcher across cache lines) */
} MemAccessPattern;

/** Hardware test configuration descriptor */
typedef struct {
    uint64_t          working_set_bytes;  /* Working set size in bytes */
    uint64_t          num_operations;     /* Number of operations to perform */
    uint64_t          num_repetitions;    /* Repetitions for statistical mode */
    MemAccessPattern  mem_pattern;        /* Memory access pattern */
    IoAccessPattern   io_pattern;         /* I/O access pattern */
    TestMode          mode;               /* Test execution mode */
    uint32_t          random_seed;        /* Seed for reproducible runs */
    bool              warm_cache;         /* Whether to pre-warm the cache */
    bool              use_huge_pages;     /* Simulate huge-page enabled systems */
    uint32_t          num_threads;        /* Simulated thread count */
    uint64_t          block_size_bytes;   /* I/O block size */
    double            time_limit_sec;     /* Maximum test duration */
    char              description[256];   /* Human-readable test description */
} TestConfig;

/* ============================================================================
 * L1: Benchmark Result — Complete measurement from a single test run  
 * ============================================================================ */

/** Timing breakdown for fine-grained analysis */
typedef struct {
    double total_sec;
    double setup_sec;       /* Test initialization time */
    double warmup_sec;      /* Cache warmup phase */
    double measurement_sec; /* Actual measurement window */
    double teardown_sec;    /* Cleanup time */
} TimingBreakdown;

/** Single benchmark measurement result */
typedef struct {
    uint64_t        operations_performed;
    double          elapsed_sec;
    double          throughput_ops_per_sec;
    double          throughput_mbps;
    double          avg_latency_ns;
    double          p50_latency_ns;
    double          p95_latency_ns;
    double          p99_latency_ns;
    double          p999_latency_ns;
    double          latency_stddev_ns;
    double          latency_variance;
    HwCounterSet    counters;
    TimingBreakdown timing;
    uint64_t        bytes_read;
    uint64_t        bytes_written;
    uint64_t        cache_misses_total;
    bool            is_valid;
    int             error_code;
} BenchResult;

/* ============================================================================
 * L1: Test Suite — Orchestrates multiple benchmark tests
 * ============================================================================ */

/** Aggregated statistics across multiple runs */
typedef struct {
    double mean_throughput;
    double stddev_throughput;
    double mean_latency_ns;
    double stddev_latency_ns;
    double min_throughput;
    double max_throughput;
    double min_latency_ns;
    double max_latency_ns;
    double cv_throughput;     /* coefficient of variation */
    double cv_latency;
    uint32_t num_runs;
    uint32_t num_outliers;    /* runs excluded as outliers (>2 sigma) */
} AggregateStats;

/** Data lake workload profile (bridges to module 7: data-engine) */
typedef struct {
    char            profile_name[64];
    double          scan_fraction;     /* Fraction of operations that are full scans */
    double          point_query_fraction;
    double          join_fraction;
    double          aggregation_fraction;
    double          data_size_gb;
    uint64_t        expected_qps;      /* Queries per second target */
    double          slo_latency_ms;    /* Service level objective for latency */
    IoAccessPattern dominant_io;
    MemAccessPattern dominant_mem;
} LakeWorkloadProfile;

/** Complete test suite with multiple test configurations */
typedef struct {
    char            suite_name[128];
    TestConfig      *configs;
    BenchResult     *results;
    size_t          num_tests;
    size_t          capacity;
    LakeWorkloadProfile workload;
    AggregateStats  aggregate;
    bool            executed;
    double          suite_score;       /* Composite performance score */
} TestSuite;

/* ============================================================================
 * L3: Engineering — Data structures for stateful operations
 * ============================================================================ */

/** Platform hardware description (auto-detected or user-specified) */
typedef struct {
    char        cpu_name[64];
    uint32_t    cpu_cores;
    uint32_t    cpu_threads_per_core;
    double      cpu_freq_ghz;
    uint64_t    l1d_cache_bytes;
    uint64_t    l1i_cache_bytes;
    uint64_t    l2_cache_bytes;
    uint64_t    l3_cache_bytes;
    uint64_t    ram_bytes;
    double      ram_bandwidth_mbps;
    double      disk_bandwidth_mbps;
    uint64_t    disk_capacity_bytes;
    bool        has_nvme;
    bool        has_gpu;
    uint32_t    cache_line_bytes;
    uint64_t    page_size_bytes;
} PlatformInfo;

/* ============================================================================
 * L1: API Declarations — Core operations
 * ============================================================================ */

/** Initialize a test configuration with defaults */
void test_config_init(TestConfig *cfg);

/** Set a test configuration for a specific data lake workload pattern */
void test_config_set_workload(TestConfig *cfg, const LakeWorkloadProfile *profile);

/** Initialize a benchmark result to zero/empty state */
void bench_result_init(BenchResult *result);

/** Create a new test suite with given capacity */
TestSuite *test_suite_create(const char *name, size_t capacity);

/** Add a test configuration to the suite */
bool test_suite_add_config(TestSuite *suite, const TestConfig *cfg);

/** Get the result at a specific index */
const BenchResult *test_suite_get_result(const TestSuite *suite, size_t idx);

/** Compute aggregate statistics from all results in the suite */
void test_suite_compute_aggregate(TestSuite *suite);

/** Free a test suite and all associated resources */
void test_suite_destroy(TestSuite *suite);

/** Initialize platform info with sensible defaults (simulated hardware) */
void platform_info_init(PlatformInfo *info);

/** Print platform information in human-readable format */
void platform_info_print(const PlatformInfo *info);

/** Initialize a hardware counter set to zero */
void hw_counter_set_init(HwCounterSet *cs);

/** Compute derived metrics from raw counter values */
void hw_counter_set_compute_derived(HwCounterSet *cs);

/** Initialize a lake workload profile for a specific workload class */
void lake_workload_profile_init_tpch(LakeWorkloadProfile *p);
void lake_workload_profile_init_tpcds(LakeWorkloadProfile *p);
void lake_workload_profile_init_log_analytics(LakeWorkloadProfile *p);

#endif /* LAKE_TEST_CORE_H */