/**
 * mem_demo.c — GPU Memory Hierarchy Demo
 *
 * Demonstrates:
 *   - Multi-space memory access (global, shared, constant)
 *   - L1/L2 cache hierarchy with LRU replacement
 *   - TLB address translation
 *   - Memory coalescing analysis for warp access patterns
 *   - Shared memory bank conflict detection
 *   - Memory consistency & fence operations
 *
 * L6: Canonical problem — GPU memory coalescing and cache behavior
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory_gpu.h"

int main(void) {
    printf("=== GPU Memory Hierarchy Demo ===\n\n");

    /* --- Demo 1: Create memory subsystem --- */
    printf("--- Creating GPU Memory Subsystem ---\n");
    GPUMemorySubsystem *m = gpu_mem_create(64ULL*1024*1024, 32768, 65536);
    if (!m) { fprintf(stderr, "Failed to create memory subsystem\n"); return 1; }

    printf("  Global memory: %lu MB\n",
           (unsigned long)(m->global_mem.total_size / (1024*1024)));
    printf("  L1 cache:      %d bytes\n", m->l1_cache.total_size);
    printf("  L2 cache:      %d bytes\n", m->l2_cache.total_size);
    printf("  L1 lines:      %d\n", m->l1_cache.num_sets * m->l1_cache.assoc);
    printf("  Shared mem:    %d banks × %d bytes\n",
           m->shared_mem.num_banks, m->shared_mem.bank_width);

    /* --- Demo 2: Global Memory Read/Write --- */
    printf("\n--- Global Memory Access ---\n");
    float write_data[8] = {10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f};
    gpu_mem_write(m, 0, write_data, 32, MEM_GLOBAL, 0, 0);
    printf("  Wrote 8 floats to global mem[0..31]\n");

    float read_data[8] = {0};
    gpu_mem_read(m, 0, read_data, 32, MEM_GLOBAL, 0, 0);
    printf("  Read back: [%.1f, %.1f, %.1f, ..., %.1f]\n",
           read_data[0], read_data[1], read_data[2], read_data[7]);

    /* --- Demo 3: Cache Behavior --- */
    printf("\n--- Cache Access Pattern ---\n");
    /* Access same address 100 times → high L1 hit rate */
    for (int i = 0; i < 100; i++) {
        float val;
        gpu_mem_read(m, 64, &val, 4, MEM_GLOBAL, 0, 0);
    }
    printf("  After 100 repeated reads to same addr:\n");
    printf("    L1 hits=%lu, misses=%lu, rate=%.1f%%\n",
           (unsigned long)m->l1_cache.hits, (unsigned long)m->l1_cache.misses,
           cache_hit_rate(&m->l1_cache) * 100.0);

    /* Flush and test sequential access (streaming) */
    cache_flush(&m->l1_cache);
    cache_invalidate(&m->l1_cache);
    m->l1_cache.hits = m->l1_cache.misses = 0;

    /* Stream through many different addresses */
    for (int i = 0; i < 256; i++) {
        float val;
        gpu_mem_read(m, (uint64_t)(i * 128), &val, 4, MEM_GLOBAL, 0, 0);
    }
    printf("  After streaming 256 different cachelines:\n");
    printf("    L1 hits=%lu, misses=%lu, rate=%.1f%%\n",
           (unsigned long)m->l1_cache.hits, (unsigned long)m->l1_cache.misses,
           cache_hit_rate(&m->l1_cache) * 100.0);

    /* --- Demo 4: Coalescing Analysis --- */
    printf("\n--- Warp Coalescing Analysis ---\n");

    /* God case: all 32 threads access contiguous 4-byte aligned addresses */
    uint32_t coalesced_addrs[32];
    for (int i = 0; i < 32; i++) coalesced_addrs[i] = (uint32_t)(i * 4);
    CoalescingAnalysis ca1 = coalesce_analyze(coalesced_addrs, 32, 128);
    printf("  Contiguous float[32]: %d txns, eff=%.1f%%\n",
           ca1.num_transactions, ca1.coalescing_efficiency * 100.0);

    /* Bad case: strided with stride=512 (larger than cacheline) */
    uint32_t strided_addrs[32];
    for (int i = 0; i < 32; i++) strided_addrs[i] = (uint32_t)(i * 512);
    CoalescingAnalysis ca2 = coalesce_analyze(strided_addrs, 32, 128);
    printf("  Strided (512B):        %d txns, eff=%.1f%%\n",
           ca2.num_transactions, ca2.coalescing_efficiency * 100.0);

    /* MCS case: misaligned access starting at offset 4 */
    uint32_t misaligned_addrs[32];
    for (int i = 0; i < 32; i++) misaligned_addrs[i] = (uint32_t)(i * 4 + 4);
    CoalescingAnalysis ca3 = coalesce_analyze(misaligned_addrs, 32, 128);
    printf("  Misaligned float[32]:  %d txns (misaligned=%d)\n",
           ca3.num_transactions, ca3.misaligned_count);

    /* --- Demo 5: Shared Memory Bank Conflicts --- */
    printf("\n--- Shared Memory Bank Conflicts ---\n");

    uint32_t smem_addrs_ok[32];
    for (int i = 0; i < 32; i++) smem_addrs_ok[i] = (uint32_t)(i * 4);
    BankConflictAnalysis bca1 = shared_bank_analyze(smem_addrs_ok, 32, 32, 4);
    printf("  Stride-1 float:        max_conflict=%d, eff=%.1f%%\n",
           bca1.max_conflict, bca1.efficiency * 100.0);

    /* All hitting same bank */
    uint32_t smem_addrs_bad[32];
    for (int i = 0; i < 32; i++) smem_addrs_bad[i] = 0; /* all bank 0 */
    BankConflictAnalysis bca2 = shared_bank_analyze(smem_addrs_bad, 32, 32, 4);
    printf("  Same bank (bank 0):    max_conflict=%d, eff=%.1f%%\n",
           bca2.max_conflict, bca2.efficiency * 100.0);

    /* --- Demo 6: TLB Translation --- */
    printf("\n--- TLB Translation ---\n");
    for (int i = 0; i < 10; i++) {
        tlb_insert(&m->tlb, (uint64_t)(i * 0x1000), (uint64_t)(i * 0x2000), MEM_GLOBAL);
    }

    uint64_t ppn;
    GPUMemSpace sp;
    bool hit = tlb_lookup(&m->tlb, 0x3000, &ppn, &sp);
    printf("  Lookup VPN=0x3000: %s → PPN=0x%llX\n", hit ? "hit" : "miss", (unsigned long long)ppn);

    hit = tlb_lookup(&m->tlb, 0x5000, &ppn, &sp);
    printf("  Lookup VPN=0x5000: %s → PPN=0x%llX\n", hit ? "hit" : "miss", (unsigned long long)ppn);

    hit = tlb_lookup(&m->tlb, 0x9999, &ppn, &sp);
    printf("  Lookup VPN=0x9999: %s (not in TLB)\n", hit ? "hit" : "miss");

    /* --- Demo 7: Consistency & Fence --- */
    printf("\n--- Memory Fence ---\n");
    mem_fence_issue(m, FENCE_GPU);
    printf("  Issued __threadfence(): gpu-scope\n");

    bool fence_done = mem_fence_complete(m, 100);
    printf("  Fence completed at cycle 100: %s\n", fence_done ? "yes" : "no");

    /* --- Final Stats --- */
    printf("\n--- Memory Subsystem Final Stats ---\n");
    gpu_mem_print_stats(m);

    gpu_mem_destroy(m);
    printf("\n=== Memory Hierarchy Demo Complete ===\n");
    return 0;
}
