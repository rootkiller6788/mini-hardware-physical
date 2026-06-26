#include "perf_counters.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ===== L1: PMU Initialization ===== */

const char *pmu_event_name(PerfEvent event)
{
    switch (event) {
    case EVENT_CYCLES:        return "CYCLES";
    case EVENT_INSTRUCTIONS:  return "INSTRUCTIONS";
    case EVENT_BRANCHES:      return "BRANCHES";
    case EVENT_BRANCH_MISSES: return "BRANCH_MISS";
    case EVENT_CACHE_REFS:    return "CACHE_REFS";
    case EVENT_CACHE_MISSES:  return "CACHE_MISS";
    case EVENT_L1D_REFS:      return "L1D_REFS";
    case EVENT_L1D_MISSES:    return "L1D_MISS";
    case EVENT_L1I_REFS:      return "L1I_REFS";
    case EVENT_L1I_MISSES:    return "L1I_MISS";
    case EVENT_L2_REFS:       return "L2_REFS";
    case EVENT_L2_MISSES:     return "L2_MISS";
    case EVENT_L3_REFS:       return "L3_REFS";
    case EVENT_L3_MISSES:     return "L3_MISS";
    case EVENT_DTLB_REFS:     return "DTLB_REFS";
    case EVENT_DTLB_MISSES:   return "DTLB_MISS";
    case EVENT_ITLB_REFS:     return "ITLB_REFS";
    case EVENT_ITLB_MISSES:   return "ITLB_MISS";
    case EVENT_LOAD_OPS:      return "LOAD_OPS";
    case EVENT_STORE_OPS:     return "STORE_OPS";
    case EVENT_STALLS_FRONTEND: return "STALLS_FRONT";
    case EVENT_STALLS_BACKEND:  return "STALLS_BACK";
    case EVENT_STALLS_MEMORY:   return "STALLS_MEM";
    case EVENT_CPU_CLOCK:     return "CPU_CLK";
    case EVENT_REF_CLOCK:     return "REF_CLK";
    case EVENT_LLC_MISSES:    return "LLC_MISS";
    case EVENT_LLC_REFS:      return "LLC_REFS";
    case EVENT_CONTEXT_SWITCHES: return "CTX_SW";
    case EVENT_PAGE_FAULTS:   return "PAGE_FAULT";
    default: return "UNKNOWN";
    }
}

void pmu_init(PMU *pmu, double freq_ghz)
{
    memset(pmu, 0, sizeof(*pmu));
    pmu->num_counters = 0;
    pmu->cpu_frequency_ghz = freq_ghz > 0.0 ? freq_ghz : 3.0;
}

void pmu_add_counter(PMU *pmu, PerfEvent event)
{
    if (pmu->num_counters >= PERF_MAX_COUNTERS) return;

    PerformanceCounter *c = &pmu->counters[pmu->num_counters];
    memset(c, 0, sizeof(*c));
    c->config.event = event;
    c->config.enabled = true;
    c->config.user_mode = true;
    c->config.kernel_mode = false;
    pmu->num_counters++;
}

void pmu_start(PMU *pmu)
{
    for (uint32_t i = 0; i < pmu->num_counters; i++) {
        pmu->counters[i].prev_count = pmu->counters[i].count;
        pmu->counters[i].config.enabled = true;
    }
}

void pmu_stop(PMU *pmu)
{
    for (uint32_t i = 0; i < pmu->num_counters; i++) {
        pmu->counters[i].config.enabled = false;
    }
}

void pmu_reset(PMU *pmu)
{
    for (uint32_t i = 0; i < pmu->num_counters; i++) {
        pmu->counters[i].count = 0;
        pmu->counters[i].prev_count = 0;
        pmu->counters[i].overflowed = false;
    }
    pmu->fixed_counter_cycles = 0;
    pmu->fixed_counter_instrs = 0;
}

uint64_t pmu_read_counter(const PMU *pmu, PerfEvent event)
{
    for (uint32_t i = 0; i < pmu->num_counters; i++) {
        if (pmu->counters[i].config.event == event)
            return pmu->counters[i].count;
    }
    return 0;
}

void pmu_accumulate_event(PMU *pmu, PerfEvent event, uint64_t delta)
{
    for (uint32_t i = 0; i < pmu->num_counters; i++) {
        if (pmu->counters[i].config.event == event &&
            pmu->counters[i].config.enabled) {
            pmu->counters[i].count += delta;
            return;
        }
    }
}

/* ===== L2: Top-Down Microarchitecture Analysis (Yasin 2014) =====
 *
 * Yasin, "A Top-Down method for performance analysis and
 * counters architecture", ISPASS 2014.
 *
 * Level 1 breakdown:
 *   Retiring        = uops_retired / uops_issued
 *   Bad Speculation = (uops_issued - uops_retired - frontend_stalls) / total
 *   Frontend Bound  = frontend_stalls / total
 *   Backend Bound   = 1 - Retiring - BadSpec - Frontend
 */

TopDownMetrics pmu_topdown_analyze(const PMU *pmu)
{
    TopDownMetrics tdm;
    memset(&tdm, 0, sizeof(tdm));

    uint64_t cycles     = pmu->fixed_counter_cycles;
    uint64_t instrs     = pmu->fixed_counter_instrs;
    uint64_t stalls_fe  = pmu_read_counter(pmu, EVENT_STALLS_FRONTEND);
    uint64_t br_miss    = pmu_read_counter(pmu, EVENT_BRANCH_MISSES);

    if (cycles == 0) return tdm;

    /* Simplified top-down using available counters */
    double total_slots = (double)cycles * 4.0; /* Assume 4-wide issue */
    if (total_slots <= 0.0) return tdm;

    double retiring = (instrs > 0) ?
        (double)instrs / total_slots : 0.0;

    /* Bad speculation: 2-cycle penalty per branch misprediction */
    double bad_spec = (br_miss > 0) ?
        (double)(br_miss * 2) / total_slots : 0.0;

    double frontend = (stalls_fe > 0) ?
        (double)stalls_fe / (double)cycles : 0.0;

    double backend = 1.0 - retiring - bad_spec - frontend;
    if (backend < 0.0) backend = 0.0;

    tdm.retiring        = retiring;
    tdm.bad_speculation = bad_spec;
    tdm.frontend_bound  = frontend;
    tdm.backend_bound   = backend;

    /* Memory vs core split (backend decomposition) */
    uint64_t l3_misses = pmu_read_counter(pmu, EVENT_LLC_MISSES);
    if (l3_misses > 0 && cycles > 0) {
        tdm.backend_memory = (double)l3_misses * 200.0 / (double)cycles;
        if (tdm.backend_memory > tdm.backend_bound)
            tdm.backend_memory = tdm.backend_bound;
    }
    tdm.backend_core = tdm.backend_bound - tdm.backend_memory;
    if (tdm.backend_core < 0.0) tdm.backend_core = 0.0;

    return tdm;
}

void pmu_print_topdown(const TopDownMetrics *tdm)
{
    printf("=== Top-Down Microarchitecture Analysis ===\n");
    printf("  Retiring:        %.1f%%\n", tdm->retiring * 100.0);
    printf("  Bad Speculation: %.1f%%\n", tdm->bad_speculation * 100.0);
    printf("  Frontend Bound:  %.1f%%\n", tdm->frontend_bound * 100.0);
    printf("  Backend Bound:   %.1f%%\n", tdm->backend_bound * 100.0);
    printf("    - Memory:      %.1f%%\n", tdm->backend_memory * 100.0);
    printf("    - Core:        %.1f%%\n", tdm->backend_core * 100.0);
    printf("========================================\n");
}

/* ===== L3: CPI Stack (Eyerman et al. 2006) ===== */

CPIStack pmu_cpi_stack(const PMU *pmu)
{
    CPIStack cs;
    memset(&cs, 0, sizeof(cs));

    uint64_t cycles  = pmu->fixed_counter_cycles;
    uint64_t instrs  = pmu->fixed_counter_instrs;

    if (instrs == 0 || cycles == 0) return cs;

    cs.total_cpi = (double)cycles / (double)instrs;
    cs.base_cpi  = 0.25; /* Ideal: 4-wide issue */

    if (pmu_read_counter(pmu, EVENT_L1I_MISSES) > 0)
        cs.l1i_miss_cpi = (double)pmu_read_counter(pmu, EVENT_L1I_MISSES) * 10.0 / (double)instrs;
    if (pmu_read_counter(pmu, EVENT_L1D_MISSES) > 0)
        cs.l1d_miss_cpi = (double)pmu_read_counter(pmu, EVENT_L1D_MISSES) * 10.0 / (double)instrs;
    if (pmu_read_counter(pmu, EVENT_L2_MISSES) > 0)
        cs.l2_miss_cpi = (double)pmu_read_counter(pmu, EVENT_L2_MISSES) * 30.0 / (double)instrs;
    if (pmu_read_counter(pmu, EVENT_LLC_MISSES) > 0)
        cs.l3_miss_cpi = (double)pmu_read_counter(pmu, EVENT_LLC_MISSES) * 200.0 / (double)instrs;
    if (pmu_read_counter(pmu, EVENT_BRANCH_MISSES) > 0)
        cs.branch_mispred_cpi = (double)pmu_read_counter(pmu, EVENT_BRANCH_MISSES) * 15.0 / (double)instrs;
    if (pmu_read_counter(pmu, EVENT_DTLB_MISSES) > 0)
        cs.tlb_miss_cpi = (double)pmu_read_counter(pmu, EVENT_DTLB_MISSES) * 50.0 / (double)instrs;

    return cs;
}

void pmu_print_cpi_stack(const CPIStack *cs)
{
    printf("=== CPI Stack Breakdown ===\n");
    printf("  Total CPI:       %.3f\n", cs->total_cpi);
    printf("  Base CPI:        %.3f\n", cs->base_cpi);
    printf("  L1I Miss:        %.3f\n", cs->l1i_miss_cpi);
    printf("  L1D Miss:        %.3f\n", cs->l1d_miss_cpi);
    printf("  L2 Miss:         %.3f\n", cs->l2_miss_cpi);
    printf("  L3/LLC Miss:     %.3f\n", cs->l3_miss_cpi);
    printf("  Branch Mispred:  %.3f\n", cs->branch_mispred_cpi);
    printf("  TLB Miss:        %.3f\n", cs->tlb_miss_cpi);
    printf("========================================\n");
}

/* ===== L3: Roofline Model (Williams, Waterman & Patterson 2009) ===== */

RooflineModel pmu_roofline_model(double peak_gflops, double peak_gbps,
                                  const PMU *pmu)
{
    RooflineModel rm;
    memset(&rm, 0, sizeof(rm));

    rm.peak_flops    = peak_gflops;
    rm.peak_bandwidth = peak_gbps;

    uint64_t instrs = pmu->fixed_counter_instrs;
    uint64_t cycles = pmu->fixed_counter_cycles;
    uint64_t l1d_refs = pmu_read_counter(pmu, EVENT_L1D_REFS);
    double freq_ghz   = pmu->cpu_frequency_ghz;

    if (instrs > 0 && cycles > 0 && freq_ghz > 0.0) {
        double exec_time = (double)cycles / (freq_ghz * 1e9);
        if (exec_time > 0.0) {
            rm.achieved_flops = (double)instrs * 2.0 / (exec_time * 1e9);
            rm.achieved_bandwidth = (double)(l1d_refs * 64) / (exec_time * 1e9);
        }
    }

    /* Operational intensity = FLOPs / byte */
    if (rm.achieved_bandwidth > 1e-10)
        rm.operational_intensity = rm.achieved_flops / rm.achieved_bandwidth;

    /* Determine bound */
    double ridge_point = peak_gflops / peak_gbps;
    rm.compute_bound = (rm.operational_intensity >= ridge_point);
    rm.memory_bound  = !rm.compute_bound;

    /* Attic: minimum of peak flops and bandwidth*OI */
    rm.attic = (peak_gflops < peak_gbps * rm.operational_intensity) ?
               peak_gflops : peak_gbps * rm.operational_intensity;

    return rm;
}

void pmu_print_roofline(const RooflineModel *rm)
{
    printf("=== Roofline Model ===\n");
    printf("  Peak GFLOP/s:      %.1f\n", rm->peak_flops);
    printf("  Peak GB/s:         %.1f\n", rm->peak_bandwidth);
    printf("  Operational Int:   %.2f FLOP/byte\n", rm->operational_intensity);
    printf("  Achieved GFLOP/s:  %.2f\n", rm->achieved_flops);
    printf("  Achieved GB/s:     %.2f\n", rm->achieved_bandwidth);
    printf("  Bound:             %s\n", rm->compute_bound ? "COMPUTE" : "MEMORY");
    printf("  Attic (ceiling):   %.2f GFLOP/s\n", rm->attic);
    printf("========================================\n");
}

/* ===== L7: Benchmark Measurement ===== */

BenchmarkMetrics pmu_benchmark(const PMU *pmu, double elapsed_sec)
{
    BenchmarkMetrics bm;
    memset(&bm, 0, sizeof(bm));

    bm.instructions = pmu->fixed_counter_instrs;
    bm.cycles       = pmu->fixed_counter_cycles;
    bm.execution_time_sec = elapsed_sec;
    bm.freq_ghz     = pmu->cpu_frequency_ghz;

    if (bm.cycles > 0) {
        bm.ipc = (double)bm.instructions / (double)bm.cycles;
        bm.cpi = 1.0 / (bm.ipc > 0.0 ? bm.ipc : 1.0);
    }

    return bm;
}

void pmu_print_benchmark(const BenchmarkMetrics *bm)
{
    printf("=== Benchmark Results ===\n");
    printf("  Instructions:  %llu\n", (unsigned long long)bm->instructions);
    printf("  Cycles:        %llu\n", (unsigned long long)bm->cycles);
    printf("  Time:          %.3f s\n", bm->execution_time_sec);
    printf("  IPC:           %.3f\n", bm->ipc);
    printf("  CPI:           %.3f\n", bm->cpi);
    printf("  Frequency:     %.1f GHz\n", bm->freq_ghz);
    printf("========================================\n");
}

/* ===== L4: Pollack's Rule (Borkar 2007) =====
 * Performance ~ sqrt(area). Doubling cores for single-thread
 * yields ~40% performance improvement.
 */

double pmu_pollack_perf(int num_cores_simple, int num_cores_complex)
{
    if (num_cores_complex <= 0) return 0.0;
    double ratio = (double)num_cores_simple / (double)num_cores_complex;
    return sqrt(ratio);
}

double pmu_amdal_practical(double fraction_serial, double measured_speedup,
                            int cores)
{
    if (cores <= 0) return 1.0;
    double predicted = 1.0 / ((1.0 - (1.0 - fraction_serial)) +
                               (1.0 - fraction_serial) / (double)cores);
    /* Return the gap between Amdahl prediction and measured */
    return predicted - measured_speedup;
}

void pmu_print_all(const PMU *pmu)
{
    printf("=== PMU State (%u counters) ===\n", pmu->num_counters);
    printf("  Fixed: cycles=%llu, instrs=%llu\n",
           (unsigned long long)pmu->fixed_counter_cycles,
           (unsigned long long)pmu->fixed_counter_instrs);
    for (uint32_t i = 0; i < pmu->num_counters; i++) {
        const PerformanceCounter *c = &pmu->counters[i];
        printf("  [%2u] %-16s = %llu\n",
               i, pmu_event_name(c->config.event),
               (unsigned long long)c->count);
    }
    printf("========================================\n");
}
