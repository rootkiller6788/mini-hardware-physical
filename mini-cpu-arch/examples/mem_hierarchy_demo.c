#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "cache.h"
#include "perf_model.h"
#include "memory_system.h"

/* ================================================================
 * mem_hierarchy_demo.c -- Integrated Memory Hierarchy Demonstration
 *
 * Demonstrates the full memory subsystem:
 *   1. Cache hierarchy (L1I, L1D, L2, L3) with MESI coherence
 *   2. Performance modeling (Amdahl, Gustafson, CPI stacks)
 *   3. Virtual memory (TLB + page table walk)
 *   4. Memory controller scheduling (FR-FCFS)
 *
 * This is the L6 Canonical Problem: end-to-end memory system.
 * ================================================================ */

/* ---- Test patterns ---- */

/* Streaming access pattern: sequential, high spatial locality */
static void test_streaming(CacheHierarchy* h) {
    printf("\n--- Streaming Access (Sequential) ---\n");
    for (uint32_t addr = 0; addr < 4096; addr += 64) {
        cache_hierarchy_read(h, addr, false);
    }
    cache_dump(h->L1D, "L1D after streaming");
    printf("  L1D hit rate: %.2f%%\n", cache_hit_rate(h->L1D) * 100.0);
}

/* Random access pattern: low locality, cache-unfriendly */
static void test_random(CacheHierarchy* h) {
    printf("\n--- Random Access ---\n");
    cache_stats_reset(h->L1D);
    cache_stats_reset(h->L2);

    srand(42); /* deterministic seed */
    for (int i = 0; i < 1000; i++) {
        uint32_t addr = (uint32_t)(rand() % 8192);
        cache_hierarchy_read(h, addr, false);
    }
    cache_dump(h->L1D, "L1D after random");
    cache_dump(h->L2, "L2 after random");
    printf("  L1D hit rate: %.2f%%\n", cache_hit_rate(h->L1D) * 100.0);
    printf("  L2  hit rate: %.2f%%\n", cache_hit_rate(h->L2) * 100.0);
}

/* ---- MESI coherence demo ---- */
static void test_mesi(void) {
    printf("\n--- MESI Coherence Demo ---\n");

    CacheGeometry geo = { 1024, 16, 2, 32 };
    Cache core0, core1;
    cache_init(&core0, geo, REPLACE_LRU, WRITE_BACK_ALLOC);
    cache_init(&core1, geo, REPLACE_LRU, WRITE_BACK_ALLOC);

    Cache* cores[2] = { &core0, &core1 };
    CoherenceBus bus;
    mesi_bus_init(&bus, cores, 2);

    /* Core 0 reads addr 0x100 -> E state */
    cache_read(&core0, 0x100);
    printf("Core0 reads 0x100: mesi=%s\n",
           mesi_state_name((MESIState)core0.lines[0].mesi_state));

    /* Core 1 reads addr 0x100 -> core0 downgrades E->S */
    mesi_bus_read_snoop(&bus, 1, 0x100);
    printf("Core1 reads 0x100: core0 mesi=%s\n",
           mesi_state_name((MESIState)core0.lines[0].mesi_state));

    /* Core 1 writes addr 0x100 -> invalidate core0's S copy */
    int inv = mesi_bus_write_snoop(&bus, 1, 0x100);
    printf("Core1 writes 0x100: %d invalidations\n", inv);
    printf("  core0 mesi=%s\n",
           mesi_state_name((MESIState)core0.lines[0].mesi_state));
    printf("  Bus cycles: %u, Snoop requests: %llu\n",
           bus.bus_cycles, (unsigned long long)bus.snoop_requests);

    cache_destroy(&core0);
    cache_destroy(&core1);
}

/* ---- Amdahl vs Gustafson ---- */
static void test_speedup(void) {
    printf("\n--- Amdahl vs Gustafson Speedup ---\n");
    printf("Serial fraction = 0.10 (90%% parallelizable)\n\n");

    printf("  Processors | Amdahl | Gustafson | Amdahl+Comm\n");
    printf("  -----------+--------+-----------+------------\n");
    for (uint32_t n = 1; n <= 64; n *= 2) {
        double amd  = amdahl_speedup(0.10, n);
        double gus  = gustafson_speedup(0.10, n);
        double amdc = amdahl_with_comm(0.10, n, 0.01);
        printf("  %10u | %6.2f | %9.2f | %10.2f\n", n, amd, gus, amdc);
    }

    printf("\n  Amdahl limit (N->inf): %.2fx\n", amdahl_limit(0.10));
    printf("  Optimal N (comm=0.02): %u\n",
           optimal_proc_count(0.10, 0.02, 64));
}

/* ---- CPI Stack Analysis ---- */
static void test_cpi_stack(void) {
    printf("\n--- CPI Stack Analysis ---\n");

    PerfCounters pc;
    perf_counters_reset(&pc);
    pc.instructions     = 1000000;
    pc.cycles           = 1800000;
    pc.cache_misses     = 15000;
    pc.cache_references = 200000;
    pc.branch_misses    = 5000;
    pc.branches         = 100000;
    pc.stall_cycles     = 100000;

    CPIStack cs = cpi_stack_from_counters(&pc);
    cpi_stack_dump(&cs);
}

/* ---- TLB + Page Table Walk ---- */
static void test_tlb_pagewalk(void) {
    printf("\n--- TLB + Page Table Walk ---\n");

    TLB tlb;
    tlb_init(&tlb, TLB_REPLACE_LRU);

    PageTable pt;
    pagetable_init(&pt, 256, 2);

    /* Map 100 pages: VPN 0-99 -> PFN 1000-1099 */
    for (uint32_t vpn = 0; vpn < 100; vpn++) {
        pagetable_map(&pt, vpn, 1000 + vpn, true, true, false);
    }

    /* Access pattern: temporal locality (reuse same pages) */
    uint32_t paddr;
    for (int i = 0; i < 500; i++) {
        uint32_t vpn = (uint32_t)(i % 20); /* working set of 20 pages */
        uint32_t vaddr = (vpn << 12) | (uint32_t)((i * 64) & 0xFFF);
        mmu_translate(&tlb, &pt, vaddr, &paddr);
    }

    tlb_dump(&tlb);

    pagetable_destroy(&pt);
}

/* ---- Memory Controller FR-FCFS ---- */
static void test_mem_controller(void) {
    printf("\n--- Memory Controller (FR-FCFS) ---\n");

    MemController mc;
    memctrl_init(&mc, 32);

    /* Enqueue mixed read/write requests */
    for (int i = 0; i < 20; i++) {
        uint32_t addr;
        if (i < 10) {
            /* Sequential: same bank, same row -> row buffer hits */
            addr = (uint32_t)(0x2000 + i * 64);
        } else {
            /* Random: different rows -> row buffer misses */
            addr = (uint32_t)(0x10000 + (rand() % 16) * 4096);
        }
        memctrl_enqueue(&mc, REQ_READ, addr, 0);
    }

    /* Schedule all requests */
    while (mc.queue_size > 0) {
        memctrl_schedule(&mc);
    }

    memctrl_dump(&mc);
    printf("  FR-FCFS prioritizes row-buffer hits to maximize DRAM throughput\n");

    memctrl_destroy(&mc);
}

/* ---- Roofline Model ---- */
static void test_roofline(void) {
    printf("\n--- Roofline Model ---\n");

    RooflineModel rm;
    rm.peak_flops     = 500.0;  /* 500 GFLOPS (e.g., GPU) */
    rm.peak_bandwidth = 100.0;  /* 100 GB/s (e.g., HBM2) */
    rm.operational_intensity = 2.5; /* FLOPS/byte */

    printf("  Peak: %.0f GFLOPS, Bandwidth: %.0f GB/s\n",
           rm.peak_flops, rm.peak_bandwidth);
    printf("  Machine balance: %.1f FLOPS/byte\n",
           rm.peak_flops / rm.peak_bandwidth);
    printf("  Kernel OI: %.1f FLOPS/byte\n", rm.operational_intensity);
    printf("  %s-bound\n",
           roofline_is_compute_bound(&rm) ? "Compute" : "Memory");
    printf("  Achievable: %.1f GFLOPS\n", roofline_memory_bound_gflops(&rm));

    /* Generate curve points */
    double intensities[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
    double gflops[7];
    roofline_curve(&rm, intensities, gflops, 7);
    printf("  Roofline curve (OI -> GFLOPS):\n");
    for (int i = 0; i < 7; i++) {
        printf("    OI=%5.2f -> %7.1f GFLOPS\n", intensities[i], gflops[i]);
    }
}

/* ---- Main ---- */

int main(void) {
    printf("========================================\n");
    printf("  mini-cpu-arch: Memory Hierarchy Demo\n");
    printf("========================================\n");

    /* Initialize cache hierarchy */
    CacheGeometry l1i_geo = { 8192,  32, 4, 64  };  /* 8KB L1I */
    CacheGeometry l1d_geo = { 8192,  32, 4, 64  };  /* 8KB L1D */
    CacheGeometry l2_geo  = { 65536, 64, 8, 128 };  /* 64KB L2 */
    CacheGeometry l3_geo  = { 0, 0, 0, 0 };         /* no L3 */

    CacheHierarchy hier;
    if (cache_hierarchy_init(&hier, l1i_geo, l1d_geo, l2_geo, l3_geo,
                              WRITE_BACK_ALLOC, WRITE_BACK_ALLOC) != 0) {
        printf("Failed to init cache hierarchy\n");
        return 1;
    }

    cache_hierarchy_dump(&hier);

    /* Run tests */
    test_streaming(&hier);
    test_random(&hier);
    test_mesi();
    test_speedup();
    test_cpi_stack();
    test_tlb_pagewalk();
    test_mem_controller();
    test_roofline();

    cache_hierarchy_destroy(&hier);

    printf("\n========================================\n");
    printf("  All memory hierarchy tests completed!\n");
    printf("========================================\n");

    return 0;
}

