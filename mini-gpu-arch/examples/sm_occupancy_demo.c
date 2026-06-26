/**
 * sm_occupancy_demo.c — Streaming Multiprocessor Occupancy Calculator
 *
 * Demonstrates:
 *   - CUDA Occupancy Calculator (determines active warps given resource limits)
 *   - Thread block allocation on SM
 *   - Pipeline cycle simulation
 *   - Shared memory bank conflict analysis
 *   - Little's Law pipeline model
 *   - Roofline performance model
 *
 * L6: Canonical problem — SM occupancy tuning
 */
#include <stdio.h>
#include <stdlib.h>
#include "shader_core.h"

int main(void) {
    printf("=== SM / Occupancy Demo ===\n\n");

    /* --- Demo 1: Create SM (Ampere SM 8.0) --- */
    ShaderCore *sm = sm_create(0, SM_CC_80);
    if (!sm) { fprintf(stderr, "Failed to create SM\n"); return 1; }

    printf("--- SM %d (Compute Capability %d.%d) ---\n",
           sm->sm_id, sm->compute_cap / 10, sm->compute_cap % 10);
    printf("  Registers:  %d total\n", sm->total_registers);
    printf("  Shared mem: %d bytes\n", sm->total_shared_mem);
    printf("  Warp slots: %d\n", sm->num_warps);
    printf("  L1 cache:   %d bytes\n", sm->l1_cache_size);

    /* --- Demo 2: Occupancy Calculator --- */
    printf("\n--- CUDA Occupancy Calculator ---\n");
    OccupancyConfig cfg = sm_default_config(SM_CC_80);

    /* Configuration A: 256 threads, 32 regs/thread, 8KB smem */
    cfg.threads_per_block = 256;
    cfg.registers_per_thread = 32;
    cfg.shared_mem_per_block_bytes = 8192;
    OccupancyResult rA = sm_calc_occupancy(&cfg);
    printf("  [A] %d thr/block, %d regs/thr, %d smem → occupancy %.1f%%\n",
           cfg.threads_per_block, cfg.registers_per_thread,
           cfg.shared_mem_per_block_bytes, rA.occupancy * 100.0);
    sm_print_occupancy(&rA);

    /* Configuration B: 1024 threads, 64 regs/thread, 16KB smem */
    cfg.threads_per_block = 1024;
    cfg.registers_per_thread = 64;
    cfg.shared_mem_per_block_bytes = 16384;
    OccupancyResult rB = sm_calc_occupancy(&cfg);
    printf("\n  [B] %d thr/block, %d regs/thr, %d smem → occupancy %.1f%%\n",
           cfg.threads_per_block, cfg.registers_per_thread,
           cfg.shared_mem_per_block_bytes, rB.occupancy * 100.0);
    sm_print_occupancy(&rB);

    /* Configuration C: 128 threads, 16 regs/thread, 1KB smem (lightweight) */
    cfg.threads_per_block = 128;
    cfg.registers_per_thread = 16;
    cfg.shared_mem_per_block_bytes = 1024;
    OccupancyResult rC = sm_calc_occupancy(&cfg);
    printf("\n  [C] %d thr/block, %d regs/thr, %d smem → occupancy %.1f%%\n",
           cfg.threads_per_block, cfg.registers_per_thread,
           cfg.shared_mem_per_block_bytes, rC.occupancy * 100.0);
    sm_print_occupancy(&rC);

    /* --- Demo 3: Block Allocation --- */
    printf("\n--- Block Allocation on SM ---\n");
    int bid = sm_allocate_block(sm, 64, 1, 1, 4096, 24);
    if (bid >= 0) {
        printf("  Allocated block %d (64 threads, 2 warps, 4KB smem)\n", bid);
        printf("  SM state: %d warps active, %d regs used, %d smem used\n",
               sm->num_active_warps, sm->used_registers, sm->used_shared_mem);
    }

    /* Try allocating another block */
    int bid2 = sm_allocate_block(sm, 128, 1, 1, 8192, 32);
    if (bid2 >= 0) {
        printf("  Allocated block %d (128 threads, 4 warps, 8KB smem)\n", bid2);
    } else {
        printf("  Could not allocate second block\n");
    }

    /* --- Demo 4: Pipeline Simulation --- */
    printf("\n--- Pipeline Cycle Simulation ---\n");
    /* Run 20 cycles */
    for (int i = 0; i < 20; i++) {
        sm_cycle(sm);
    }
    sm_print_stats(sm);

    /* --- Demo 5: Bank Conflict Analysis --- */
    printf("\n--- Shared Memory Bank Conflicts ---\n");
    int addrs_no_conflict[32];
    for (int i = 0; i < 32; i++) addrs_no_conflict[i] = i * 4;
    BankAnalyzer ba1 = bank_analyze(addrs_no_conflict, 32, 32, 4);
    printf("  Stride-1 access:    %s\n", bank_conflict_str(ba1.conflict));

    int addrs_conflict[32];
    for (int i = 0; i < 32; i++) addrs_conflict[i] = i * 128; /* all same bank */
    BankAnalyzer ba2 = bank_analyze(addrs_conflict, 32, 32, 4);
    printf("  Same-bank access:   %s (%d-way)\n",
           bank_conflict_str(ba2.conflict), ba2.max_way_conflict);

    /* --- Demo 6: Little's Law --- */
    printf("\n--- Little's Law Pipeline Model ---\n");
    for (double lambda = 0.1; lambda <= 1.0; lambda += 0.3) {
        LittlesLawModel ll = littles_law_sm_model(lambda, 6);
        printf("  λ=%.1f warps/cycle, W=%d stages → L=%.1f warps inflight, throughput=%.3f\n",
               ll.arrival_rate, 6, ll.occupancy_law, ll.throughput);
    }

    /* --- Demo 7: Roofline Model --- */
    printf("\n--- Roofline Model ---\n");
    /* H100: ~60 TFLOPS FP8, ~3 TB/s HBM3 */
    RooflinePoint rp1 = roofline_evaluate(1.0, 60.0, 3000.0);
    printf("  OI=1 FLOP/byte: memory-bound, achievable=%.1f GFLOPS\n", rp1.achievable_perf);

    RooflinePoint rp2 = roofline_evaluate(100.0, 60.0, 3000.0);
    printf("  OI=100 FLOP/byte: compute-bound, achievable=%.1f GFLOPS\n", rp2.achievable_perf);

    double ridge = 60.0 * 1000 / 3000.0; /* TFLOPS→GFLOPS / GB/s → ridge */
    printf("  Ridge point: %.1f FLOP/byte\n", ridge);

    /* Cleanup */
    sm_deallocate_block(sm, bid);
    if (bid2 >= 0) sm_deallocate_block(sm, bid2);
    sm_destroy(sm);

    printf("\n=== SM Occupancy Demo Complete ===\n");
    return 0;
}
