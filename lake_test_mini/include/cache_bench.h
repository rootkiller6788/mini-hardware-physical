#ifndef CACHE_BENCH_H
#define CACHE_BENCH_H

/**
 * cache_bench.h — Cache Hierarchy Benchmarking for Data Lake Workloads
 *
 * L2: Cache hierarchy — Models L1/L2/L3 cache behavior under data-intensive
 *     workloads. Data lake operations (scans, lookups, joins) exhibit distinct
 *     cache access patterns that determine end-to-end query performance.
 *
 * L4: Theorem — Cache inclusion properties and the power law of cache misses.
 *     Miss rate ∝ (cache_size)^(-α) where α is the workload's locality exponent.
 *     (Hartstein et al., "On the Nature of Cache Miss Behavior", ISCA 2006)
 *
 * L5: Algorithm — Cache miss curve construction using stack distance profiling.
 *     Implements Mattson's stack algorithm (1970) for analytical cache modeling.
 *
 * Universities: MIT 6.823, CMU 18-447, Stanford EE282
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "lake_test_core.h"

/* ============================================================================
 * L1: Cache Benchmarking Types
 * ============================================================================ */

/** Cache hierarchy level descriptor */
typedef struct {
    uint32_t level;           /* 1, 2, or 3 */
    uint64_t size_bytes;
    uint32_t line_size;
    uint32_t associativity;
    uint32_t num_sets;
    double   hit_latency_ns;
    double   miss_penalty_ns; /* latency to next level */
} CacheLevelDesc;

/** Cache benchmark configuration */
typedef struct {
    CacheLevelDesc l1;
    CacheLevelDesc l2;
    CacheLevelDesc l3;
    uint64_t       working_set_min;
    uint64_t       working_set_max;
    uint64_t       working_set_step;
    uint32_t       stride_count;      /* Number of different stride sizes to test */
    bool           measure_inclusive; /* Test inclusive vs exclusive caching */
} CacheBenchConfig;

/** Single cache benchmark data point */
typedef struct {
    uint64_t working_set_bytes;
    uint32_t stride_bytes;
    uint64_t accesses;
    uint64_t hits;
    uint64_t misses;
    double   hit_rate;
    double   miss_rate;
    double   avg_access_time_ns;  /* AMAT = hit_time + miss_rate * miss_penalty */
    double   bandwidth_mbps;
} CacheBenchPoint;

/** Complete cache benchmark result (miss curve) */
typedef struct {
    CacheBenchPoint *points;
    size_t           num_points;
    CacheBenchConfig config;
    double           mpki_overall;  /* Misses Per Kilo-Instruction */
    double           cpi_cache;    /* CPI component from cache misses */
} CacheBenchResult;

/* ============================================================================
 * L2: Cache Access Simulator
 *
 * Simulates cache access for data lake patterns: sequential scan (common in
 * Parquet/ORC column reads), random point lookup (index probe), and strided
 * access (column chunk hopping).
 * ============================================================================ */

/** Cache access type */
typedef enum {
    CACHE_ACCESS_READ = 0,
    CACHE_ACCESS_WRITE = 1
} CacheAccessType;

/** Cache set with LRU eviction tracking */
typedef struct {
    uint64_t *tags;
    uint64_t *access_timestamps;
    bool     *valid;
    bool     *dirty;
    uint32_t  assoc;
    uint32_t  line_size;
} CacheSetLRU;

/** Simulated cache instance */
typedef struct {
    CacheSetLRU *sets;
    uint32_t     num_sets;
    uint32_t     assoc;
    uint32_t     line_size;
    uint64_t     total_size;
    uint64_t     global_time;
    /* Statistics */
    uint64_t     total_accesses;
    uint64_t     total_hits;
    uint64_t     total_misses;
    uint64_t     total_evictions;
    uint64_t     total_writebacks;
    bool         write_back;
} SimCache;

/* ============================================================================
 * L5: Stack Distance Profiling (Mattson's Algorithm, 1970)
 *
 * Stack distance = number of unique addresses accessed since last access
 * to the same address. The stack distance histogram uniquely determines
 * the miss ratio for any cache size with LRU replacement.
 * ============================================================================ */

/** Stack distance histogram entry */
typedef struct {
    uint64_t distance;
    uint64_t count;
    double   cumulative_prob;  /* P(stack_distance > d) */
} StackDistEntry;

/** Stack distance profile */
typedef struct {
    StackDistEntry *entries;
    size_t          num_entries;
    uint64_t        total_references;
    uint64_t        unique_addresses;
} StackDistProfile;

/* ============================================================================
 * L1: API Declarations
 * ============================================================================ */

/** Initialize cache benchmark config with realistic defaults */
void cache_bench_config_init(CacheBenchConfig *cfg);

/** Initialize a simulated cache */
void sim_cache_init(SimCache *cache, uint32_t total_size, uint32_t line_size,
                    uint32_t associativity, bool write_back);

/** Simulate a cache access; returns true on hit */
bool sim_cache_access(SimCache *cache, uint64_t address, CacheAccessType type);

/** Flush the cache and writeback dirty lines */
void sim_cache_flush(SimCache *cache);

/** Get current cache statistics */
void sim_cache_stats(const SimCache *cache, uint64_t *hits, uint64_t *misses,
                     double *hit_rate, double *amat, double hit_ns, double miss_ns);

/** Free simulated cache resources */
void sim_cache_destroy(SimCache *cache);

/** Run a full cache benchmark producing a miss curve */
CacheBenchResult *cache_bench_run(const CacheBenchConfig *cfg);

/** Free a cache benchmark result */
void cache_bench_result_destroy(CacheBenchResult *result);

/** Print cache benchmark result in table format */
void cache_bench_result_print(const CacheBenchResult *result);

/** Build stack distance profile from an access trace */
StackDistProfile *stack_dist_build(const uint64_t *addresses, size_t num_accesses);

/** Predict miss rate for a given cache size using stack distance profile */
double stack_dist_predict_miss_rate(const StackDistProfile *profile,
                                     uint64_t cache_size, uint32_t line_size);

/** Free a stack distance profile */
void stack_dist_profile_destroy(StackDistProfile *profile);

/**
 * L4: Compute Cache AMAT (Average Memory Access Time)
 *     AMAT = hit_time + miss_rate * miss_penalty
 *     This is the fundamental equation from Hennessy & Patterson.
 */
double cache_compute_amat(double hit_time_ns, double miss_rate, double miss_penalty_ns);

/**
 * L4: Power-law miss rate model
 *     MR(C) = MR0 * (C / C0)^(-alpha)
 *     where alpha characterizes the workload's temporal locality.
 *     Returns the fitted alpha exponent.
 */
double cache_fit_power_law_exponent(const CacheBenchResult *result, double *r_squared);

#endif /* CACHE_BENCH_H */