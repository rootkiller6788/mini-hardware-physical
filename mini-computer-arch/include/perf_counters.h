#ifndef PERF_COUNTERS_H
#define PERF_COUNTERS_H

#include <stdbool.h>
#include <stdint.h>

/* L1: Core Definitions — Hardware Performance Counters (PMU) */

#define PERF_MAX_COUNTERS    32
#define PERF_MAX_EVENTS      64

/* L1: Standard architectural performance events (Intel PMU / ARM PMU) */
typedef enum {
    EVENT_CYCLES,
    EVENT_INSTRUCTIONS,
    EVENT_BRANCHES,
    EVENT_BRANCH_MISSES,
    EVENT_CACHE_REFS,
    EVENT_CACHE_MISSES,
    EVENT_L1D_REFS,
    EVENT_L1D_MISSES,
    EVENT_L1I_REFS,
    EVENT_L1I_MISSES,
    EVENT_L2_REFS,
    EVENT_L2_MISSES,
    EVENT_L3_REFS,
    EVENT_L3_MISSES,
    EVENT_DTLB_REFS,
    EVENT_DTLB_MISSES,
    EVENT_ITLB_REFS,
    EVENT_ITLB_MISSES,
    EVENT_LOAD_OPS,
    EVENT_STORE_OPS,
    EVENT_FP_OPS,
    EVENT_INT_OPS,
    EVENT_SIMD_OPS,
    EVENT_STALLS_FRONTEND,
    EVENT_STALLS_BACKEND,
    EVENT_STALLS_MEMORY,
    EVENT_BUS_CYCLES,
    EVENT_CPU_CLOCK,
    EVENT_REF_CLOCK,
    EVENT_MISPREDICT_RECOVERY,
    EVENT_MACHINE_CLEARS,
    EVENT_UOPS_ISSUED,
    EVENT_UOPS_RETIRED,
    EVENT_CONTEXT_SWITCHES,
    EVENT_PAGE_FAULTS,
    EVENT_LLC_MISSES,
    EVENT_LLC_REFS,
    EVENT_OFFCORE_REQUESTS,
    EVENT_OFFCORE_RESPONSES,
    EVENT_COUNT
} PerfEvent;

/* L1: Counter configuration */
typedef struct {
    PerfEvent   event;
    bool        enabled;
    bool        kernel_mode;
    bool        user_mode;
    uint64_t    reset_value;
    uint64_t    overflow_threshold;
} CounterConfig;

typedef struct {
    CounterConfig config;
    uint64_t       count;
    uint64_t       prev_count;
    double         ratio;      /* Events per cycle / per instruction */
    bool           overflowed;
} PerformanceCounter;

/* L2: Core metrics — Top-Down Microarchitecture Analysis (Yasin 2014) */
typedef struct {
    double retiring;        /* Fraction of slots with useful work */
    double bad_speculation; /* Wasted due to mispredictions */
    double frontend_bound;  /* Stalled waiting for frontend */
    double backend_bound;   /* Stalled waiting for backend resources */
    double backend_memory;  /* Memory-bound fraction */
    double backend_core;    /* Core-bound fraction */
} TopDownMetrics;

/* L3: CPI stack breakdown (Eyerman et al. 2006) */
typedef struct {
    double base_cpi;         /* Ideal CPI (no misses) */
    double l1i_miss_cpi;
    double l1d_miss_cpi;
    double l2_miss_cpi;
    double l3_miss_cpi;
    double tlb_miss_cpi;
    double branch_mispred_cpi;
    double other_cpi;
    double total_cpi;
} CPIStack;

/* L3: Roofline model (Williams, Waterman & Patterson 2009) */
typedef struct {
    double peak_flops;        /* GFLOP/s theoretical peak */
    double peak_bandwidth;    /* GB/s theoretical peak */
    double operational_intensity; /* FLOP/byte */
    double achieved_flops;
    double achieved_bandwidth;
    bool   compute_bound;
    bool   memory_bound;
    double attic;             /* Achievable performance ceiling */
} RooflineModel;

/* L7: SPEC-style benchmark measurement */
typedef struct {
    uint64_t instructions;
    uint64_t cycles;
    double   execution_time_sec;
    double   ipc;
    double   cpi;
    double   freq_ghz;
} BenchmarkMetrics;

/* L3: Performance Monitoring Unit (PMU) */
typedef struct {
    PerformanceCounter counters[PERF_MAX_COUNTERS];
    uint32_t           num_counters;
    uint64_t           fixed_counter_cycles;
    uint64_t           fixed_counter_instrs;
    double             cpu_frequency_ghz;  /* Detected or configured */
} PMU;

/* L1 API */
void pmu_init(PMU *pmu, double freq_ghz);
void pmu_add_counter(PMU *pmu, PerfEvent event);
void pmu_start(PMU *pmu);
void pmu_stop(PMU *pmu);
void pmu_reset(PMU *pmu);
uint64_t pmu_read_counter(const PMU *pmu, PerfEvent event);
void pmu_accumulate_event(PMU *pmu, PerfEvent event, uint64_t delta);

/* L2: Top-down analysis */
TopDownMetrics pmu_topdown_analyze(const PMU *pmu);
void pmu_print_topdown(const TopDownMetrics *tdm);

/* L3: CPI stack */
CPIStack pmu_cpi_stack(const PMU *pmu);
void pmu_print_cpi_stack(const CPIStack *cs);

/* L3: Roofline model */
RooflineModel pmu_roofline_model(double peak_gflops, double peak_gbps,
                                  const PMU *pmu);
void pmu_print_roofline(const RooflineModel *rm);

/* L7: Benchmark */
BenchmarkMetrics pmu_benchmark(const PMU *pmu, double elapsed_sec);
void pmu_print_benchmark(const BenchmarkMetrics *bm);

/* L4: Amdahl's Law in practice */
double pmu_amdal_practical(double fraction_serial, double measured_speedup, int cores);

/* L4: Pollack's Rule (Borkar 2007) */
double pmu_pollack_perf(int num_cores_simple, int num_cores_complex);

/* Stats and print */
void pmu_print_all(const PMU *pmu);
const char *pmu_event_name(PerfEvent event);

#endif
