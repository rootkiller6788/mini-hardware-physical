#ifndef PERF_MODEL_H
#define PERF_MODEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* CPI stack decomposition (L1: Definitions) */
typedef struct {
    double base_cpi;
    double cache_miss_cpi;
    double branch_misp_cpi;
    double pipeline_stall;
    double other_stall;
} CPIStack;

typedef struct {
    uint64_t instructions;
    uint64_t cycles;
    uint64_t cache_misses;
    uint64_t cache_references;
    uint64_t branch_misses;
    uint64_t branches;
    uint64_t stall_cycles;
} PerfCounters;

typedef struct {
    double serial_fraction;
    double parallel_fraction;
    uint32_t num_processors;
    double communication_overhead;
} SpeedupModel;

typedef struct {
    double peak_flops;
    double peak_bandwidth;
    double operational_intensity;
} RooflineModel;

/* L2-L4: Speedup laws */
double amdahl_speedup(double sf, uint32_t np);
double gustafson_speedup(double sf, uint32_t np);
double amdahl_with_comm(double sf, uint32_t np, double co);
double amdahl_limit(double sf);
double gustafson_limit_ratio(double sf, uint32_t np);

/* L3: CPI engineering */
CPIStack cpi_stack_from_counters(const PerfCounters* pc);
double cpi_total(const CPIStack* cs);
void cpi_stack_dump(const CPIStack* cs);
const char* cpi_bottleneck(const CPIStack* cs);
double cpi_to_ipc(const CPIStack* cs);

/* L5: Roofline model */
double roofline_memory_bound_gflops(const RooflineModel* rm);
bool roofline_is_compute_bound(const RooflineModel* rm);
void roofline_curve(const RooflineModel* rm, const double* intensities, double* out, size_t n);

/* L7: Multi-core prediction */
double parallel_exec_time(double t1, const SpeedupModel* sm);
double parallel_efficiency(const SpeedupModel* sm);
uint32_t optimal_proc_count(double sf, double co, uint32_t max_n);

void perf_counters_reset(PerfCounters* pc);
void perf_counters_accumulate(PerfCounters* t, const PerfCounters* a);

#endif