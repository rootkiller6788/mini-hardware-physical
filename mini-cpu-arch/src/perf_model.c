#include "perf_model.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * perf_model.c -- CPU Performance Modeling (L1-L7)
 *
 * L1: struct defs  L2: Amdahl/Gustafson  L3: CPI stack decompositon
 * L4: theorem limits  L5: Roofline model  L6: bottleneck analysis
 * L7: multi-core scaling prediction
 *
 * Ref: Hennessy & Patterson "CA:AQA" 6th Ed, Chapters 1-2.
 * ================================================================ */

/* ---- L2: Amdahl's Law (1967) ----
 * Speedup(N) = 1 / ((1-f) + f/N)
 * where f = parallelizable fraction.
 *
 * Derivation: T_N = T_1 * ((1-f) + f/N)
 * S = T_1/T_N = 1 / ((1-f) + f/N)
 *
 * Key insight (L4): lim_{N->inf} S = 1/(1-f). The serial fraction
 * bounds maximum speedup regardless of how many cores you add. */

double amdahl_speedup(double serial_fraction, uint32_t num_procs) {
    if (num_procs == 0 || serial_fraction >= 1.0) return 1.0;
    if (serial_fraction <= 0.0) return (double)num_procs;
    double p = 1.0 - serial_fraction;
    return 1.0 / (serial_fraction + p / (double)num_procs);
}

/* ---- L2: Gustafson's Law (1988) ----
 * Speedup(N) = N - alpha*(N-1)
 * where alpha = serial fraction.
 *
 * Unlike Amdahl (fixed problem size), Gustafson scales the problem
 * with processors.  As N grows, parallel work grows linearly.
 *
 * Derivation: With N processors, the serial portion stays constant
 * while the parallel portion scales by N.
 *   S = (serial + N*parallel) / (serial + parallel) = N - alpha*(N-1)
 *
 * Key insight (L4): Speedup is NOT bounded by serial fraction when
 * problem size scales.  This enables "weak scaling" analysis. */

double gustafson_speedup(double serial_fraction, uint32_t num_procs) {
    if (num_procs == 0) return 1.0;
    double alpha = serial_fraction;
    return (double)num_procs - alpha * (double)(num_procs - 1);
}

/* Amdahl with communication overhead:
 *   S(N) = 1 / ((1-f) + f/N + overhead(N))
 * where overhead(N) = comm_overhead * log2(N) models interconnect cost.
 *
 * For large N, overhead dominates and speedup can DECREASE with more cores. */

double amdahl_with_comm(double serial_fraction, uint32_t num_procs,
                         double comm_overhead) {
    if (num_procs == 0 || serial_fraction >= 1.0) return 1.0;
    double p = 1.0 - serial_fraction;
    double overhead = comm_overhead * log2((double)num_procs);
    return 1.0 / (serial_fraction + p / (double)num_procs + overhead);
}

/* ---- L4: Theorem Verification ---- */

/* Amdahl limit: S_max = 1 / (1 - f_parallel)
 * As N -> infinity, the parallel portion becomes zero cost. */

double amdahl_limit(double serial_fraction) {
    if (serial_fraction >= 1.0) return 1.0;
    if (serial_fraction <= 0.0) return INFINITY;
    return 1.0 / (1.0 - (1.0 - serial_fraction)); /* = 1/serial */
}

/* Gustafson limit ratio: S(N) / N = 1 - alpha*(1 - 1/N)
 * As N -> infinity: S/N -> 1 - alpha = parallel_fraction
 * This confirms linear scaling for embarrassingly parallel workloads. */

double gustafson_limit_ratio(double serial_fraction, uint32_t num_procs) {
    if (num_procs <= 1) return 1.0;
    return gustafson_speedup(serial_fraction, num_procs) / (double)num_procs;
}

/* ---- L3: CPI Stack Engineering ----
 *
 * Iron Law of Performance (L4):
 *   CPU_Time = Instruction_Count * CPI * Cycle_Time
 *   where CPI = Cycles_Per_Instruction.
 *
 * We decompose CPI into identifiable components:
 *   CPI_total = CPI_base + CPI_cache + CPI_branch + CPI_stall + CPI_other
 *
 * This is the foundation of processor performance analysis. */

void perf_counters_reset(PerfCounters* pc) {
    if (pc) memset(pc, 0, sizeof(PerfCounters));
}

void perf_counters_accumulate(PerfCounters* total, const PerfCounters* add) {
    if (!total || !add) return;
    total->instructions     += add->instructions;
    total->cycles           += add->cycles;
    total->cache_misses     += add->cache_misses;
    total->cache_references += add->cache_references;
    total->branch_misses    += add->branch_misses;
    total->branches         += add->branches;
    total->stall_cycles     += add->stall_cycles;
}

/* Derive CPI stack from hardware counters.
 *
 * base_cpi: assume ideal 1.0 for single-issue scalar
 * cache_miss_cpi: (cache_misses / instructions) * miss_penalty
 *   where miss_penalty is assumed to be 10 cycles (configurable)
 * branch_misp_cpi: (branch_misses / instructions) * mispredict_penalty
 *   mispredict_penalty = pipeline_depth (assume 5)
 * pipeline_stall: stall_cycles / instructions
 * other_stall: (cycles - (instructions + cache_penalty + branch_penalty + stall))
 *              / instructions (catch-all for remaining stalls) */

CPIStack cpi_stack_from_counters(const PerfCounters* pc) {
    CPIStack cs;
    memset(&cs, 0, sizeof(CPIStack));
    if (!pc || pc->instructions == 0) return cs;

    double instrs = (double)pc->instructions;
    double miss_penalty     = 10.0;  /* cycles per cache miss */
    double mispred_penalty  = 5.0;   /* cycles per branch mispredict */

    cs.base_cpi        = 1.0;
    cs.cache_miss_cpi  = ((double)pc->cache_misses / instrs) * miss_penalty;
    cs.branch_misp_cpi = ((double)pc->branch_misses / instrs) * mispred_penalty;
    cs.pipeline_stall  = (double)pc->stall_cycles / instrs;

    /* Remaining: other unexplained stall cycles */
    double explained = cs.base_cpi + cs.cache_miss_cpi + cs.branch_misp_cpi + cs.pipeline_stall;
    double total_cpi = (double)pc->cycles / instrs;
    cs.other_stall = total_cpi - explained;
    if (cs.other_stall < 0.0) cs.other_stall = 0.0;

    return cs;
}

double cpi_total(const CPIStack* cs) {
    if (!cs) return 0.0;
    return cs->base_cpi + cs->cache_miss_cpi + cs->branch_misp_cpi +
           cs->pipeline_stall + cs->other_stall;
}

double cpi_to_ipc(const CPIStack* cs) {
    double cpi = cpi_total(cs);
    if (cpi <= 0.0) return 0.0;
    return 1.0 / cpi;
}

/* Identify dominant bottleneck.
 * Iron Law connection (L4): Time = Instructions * CPI * Cycle_Time.
 * The largest CPI component determines the optimization target. */

const char* cpi_bottleneck(const CPIStack* cs) {
    if (!cs) return "no data";

    double max_val = cs->base_cpi;
    const char* bottleneck = "base CPI (ideal)";

    if (cs->cache_miss_cpi > max_val)  { max_val = cs->cache_miss_cpi;  bottleneck = "cache misses"; }
    if (cs->branch_misp_cpi > max_val) { max_val = cs->branch_misp_cpi; bottleneck = "branch mispredicts"; }
    if (cs->pipeline_stall > max_val)  { max_val = cs->pipeline_stall;  bottleneck = "pipeline stalls"; }
    if (cs->other_stall > max_val)     { max_val = cs->other_stall;     bottleneck = "other stalls"; }

    return bottleneck;
}

void cpi_stack_dump(const CPIStack* cs) {
    if (!cs) return;
    double total = cpi_total(cs);
    printf("=== CPI Stack Analysis ===\n");
    printf("  Base CPI:          %6.2f  (%5.1f%%)\n", cs->base_cpi, 100.0*cs->base_cpi/total);
    printf("  Cache Miss CPI:    %6.2f  (%5.1f%%)\n", cs->cache_miss_cpi, 100.0*cs->cache_miss_cpi/total);
    printf("  Branch Misp CPI:   %6.2f  (%5.1f%%)\n", cs->branch_misp_cpi, 100.0*cs->branch_misp_cpi/total);
    printf("  Pipeline Stall CPI:%6.2f  (%5.1f%%)\n", cs->pipeline_stall, 100.0*cs->pipeline_stall/total);
    printf("  Other Stall CPI:   %6.2f  (%5.1f%%)\n", cs->other_stall, 100.0*cs->other_stall/total);
    printf("  --------------------------------\n");
    printf("  TOTAL CPI:         %6.2f  (IPC = %.3f)\n", total, cpi_to_ipc(cs));
    printf("  Bottleneck: %s\n", cpi_bottleneck(cs));
    printf("============================\n");
}

/* ---- L5: Roofline Model (Williams et al., 2009) ----
 *
 * The Roofline model bounds kernel performance by two ceilings:
 *   1. Peak compute (GFLOPS) — horizontal line
 *   2. Memory bandwidth — sloped line: GFLOPS = BW * OI
 *      where OI = Operational Intensity (FLOPS/byte)
 *
 * Performance = min(Peak_GFLOPS, Peak_BW * OI)
 *
 * Machine balance = Peak_GFLOPS / Peak_BW (FLOPS/byte)
 * If kernel OI < machine_balance: memory-bound
 * If kernel OI > machine_balance: compute-bound */

bool roofline_is_compute_bound(const RooflineModel* rm) {
    if (!rm || rm->peak_bandwidth <= 0.0) return true;
    double machine_balance = rm->peak_flops / rm->peak_bandwidth;
    return rm->operational_intensity >= machine_balance;
}

double roofline_memory_bound_gflops(const RooflineModel* rm) {
    if (!rm) return 0.0;
    if (roofline_is_compute_bound(rm))
        return rm->peak_flops;
    else
        return rm->peak_bandwidth * rm->operational_intensity;
}

/* Generate roofline curve data points.
 * For each operational intensity value, compute the achievable GFLOPS
 * as min(peak_flops, peak_bandwidth * intensity). */

void roofline_curve(const RooflineModel* rm,
                    const double* intensities, double* gflops_out, size_t count) {
    if (!rm || !intensities || !gflops_out) return;
    for (size_t i = 0; i < count; i++) {
        double mem_bound = rm->peak_bandwidth * intensities[i];
        gflops_out[i] = (mem_bound < rm->peak_flops) ? mem_bound : rm->peak_flops;
    }
}

/* ---- L7: Multi-core Scaling Prediction ----
 *
 * Parallel execution time: T_N = T_1 / Speedup(N)
 * Parallel efficiency: E = Speedup / N (ideal = 1.0)
 *
 * Efficiency < 1.0 indicates load imbalance, communication overhead,
 * or insufficient parallelism (Amdahl's serial bottleneck).
 *
 * Optimal processor count: find N that maximizes speedup given
 * communication overhead that grows with N.  This is a classic
 * trade-off: more cores = more parallelism but also more overhead. */

double parallel_exec_time(double single_core_time, const SpeedupModel* sm) {
    if (!sm || single_core_time <= 0.0) return 0.0;
    double s = amdahl_with_comm(sm->serial_fraction, sm->num_processors,
                                 sm->communication_overhead);
    if (s <= 0.0) return single_core_time;
    return single_core_time / s;
}

double parallel_efficiency(const SpeedupModel* sm) {
    if (!sm || sm->num_processors == 0) return 0.0;
    double s = amdahl_with_comm(sm->serial_fraction, sm->num_processors,
                                 sm->communication_overhead);
    return s / (double)sm->num_processors;
}

/* Find optimal processor count by brute-force search.
 * For each N in [1, max_N], compute speedup with overhead.
 * Return N that maximizes speedup.  This models the real-world
 * observation that adding too many cores degrades performance
 * (negative scaling) due to synchronization overhead.
 *
 * Complexity: O(max_N) — trivial for max_N up to thousands. */

uint32_t optimal_proc_count(double serial_fraction, double comm_overhead,
                             uint32_t max_N) {
    if (max_N == 0) return 1;
    double best_speedup = 1.0;
    uint32_t best_N = 1;

    for (uint32_t n = 1; n <= max_N; n++) {
        double s = amdahl_with_comm(serial_fraction, n, comm_overhead);
        if (s > best_speedup) {
            best_speedup = s;
            best_N = n;
        }
    }
    return best_N;
}

