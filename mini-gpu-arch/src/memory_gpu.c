#include "memory_gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

GPUMemory gpu_mem_init(uint32_t size_gb)
{
    GPUMemory m;
    /* 限制最多模拟4GB，以避免超大内存分配 */
    if (size_gb > 4) size_gb = 4;
    m.global_mem_bytes = size_gb * 1024ULL * 1024ULL * 1024ULL;

    m.global_mem = (uint8_t *)malloc(m.global_mem_bytes);
    if (m.global_mem) {
        memset(m.global_mem, 0, m.global_mem_bytes);
    }

    m.shared_mem_bytes = SHARED_BANKS * 4 * 48; /* 48KB */
    m.shared_mem = (uint8_t *)malloc(m.shared_mem_bytes);
    if (m.shared_mem) {
        memset(m.shared_mem, 0, m.shared_mem_bytes);
    }

    m.l1_lines = 128 * 1024 / CACHE_LINE_BYTES;
    m.l1_cache = (uint8_t *)malloc(m.l1_lines * CACHE_LINE_BYTES);
    if (m.l1_cache) {
        memset(m.l1_cache, 0, m.l1_lines * CACHE_LINE_BYTES);
    }

    m.l2_lines = L2_CACHE_KB * 1024 / CACHE_LINE_BYTES;
    m.l2_cache = (uint8_t *)malloc(m.l2_lines * CACHE_LINE_BYTES);
    if (m.l2_cache) {
        memset(m.l2_cache, 0, m.l2_lines * CACHE_LINE_BYTES);
    }

    m.const_cache = (uint32_t *)malloc(64 * 1024);
    if (m.const_cache) {
        memset(m.const_cache, 0, 64 * 1024);
    }

    m.num_channels = 8;
    m.bandwidth_gbps = 900.0;
    m.queue_depth = 16;
    m.total_transactions = 0;
    m.coalesced_hits = 0;
    m.uncoalesced_misses = 0;

    return m;
}

void gpu_mem_free(GPUMemory *m)
{
    free(m->global_mem);
    free(m->shared_mem);
    free(m->l1_cache);
    free(m->l2_cache);
    free(m->const_cache);
    m->global_mem = NULL;
    m->shared_mem = NULL;
    m->l1_cache = NULL;
    m->l2_cache = NULL;
    m->const_cache = NULL;
}

uint32_t gpu_mem_load(GPUMemory *m, uint32_t addr, MemSpace space)
{
    if (addr >= m->global_mem_bytes) return 0;
    /* 模拟~200 cycle DRAM延迟 */
    return *(uint32_t *)(m->global_mem + addr);
}

void gpu_mem_store(GPUMemory *m, uint32_t addr, uint32_t data, MemSpace space)
{
    if (addr >= m->global_mem_bytes) return;
    *(uint32_t *)(m->global_mem + addr) = data;
}

/* 检查一个warp的地址序列是否对齐合并访问 */
bool mem_is_coalesced(const GPUMemory *m, const uint32_t *addresses, int n_addresses)
{
    if (n_addresses <= 1) return true;

    /* 检查所有地址是否在同一个cache line (128字节) 内 */
    uint32_t base_line = addresses[0] / CACHE_LINE_BYTES;

    for (int i = 1; i < n_addresses; i++) {
        uint32_t line = addresses[i] / CACHE_LINE_BYTES;
        if (line != base_line) {
            return false;
        }
    }

    /* 检查是否连续增长 */
    for (int i = 1; i < n_addresses; i++) {
        if (addresses[i] != addresses[0] + (uint32_t)i * 4) {
            return false;
        }
    }

    return true;
}

/* 合并访问：一次DRAM事务 */
int mem_coalesced_access(GPUMemory *m, const uint32_t *addrs, int n)
{
    (void)n;
    /* 合并访问：1次DRAM事务，覆盖整个cache line */
    m->total_transactions += 1;
    m->coalesced_hits += 1;
    /* 返回事务数 */
    return 1;
}

/* 非合并访问：每次访问一个单独的事务 */
int mem_uncoalesced_access(GPUMemory *m, const uint32_t *addrs, int n)
{
    /* 非合并访问：每个地址一次DRAM事务 = n次 */
    m->total_transactions += n;
    m->uncoalesced_misses += n;
    return n;
}

/* 计算共享内存bank conflict数 */
int mem_bank_conflict_count(const uint32_t *addresses, int n)
{
    int bank_access[SHARED_BANKS];
    int max_conflict = 0;
    memset(bank_access, 0, sizeof(bank_access));

    for (int i = 0; i < n; i++) {
        /* 共享内存地址映射到bank: (addr/4) % 32 */
        int bank = (addresses[i] / 4) % SHARED_BANKS;
        bank_access[bank]++;
        if (bank_access[bank] > max_conflict) {
            max_conflict = bank_access[bank];
        }
    }

    return max_conflict;
}

void gpu_mem_print_bandwidth_stats(const GPUMemory *m)
{
    printf("GPU Memory Stats:\n");
    printf("  Global memory:  %u MB\n", m->global_mem_bytes / (1024 * 1024));
    printf("  GDDR channels:  %d\n", m->num_channels);
    printf("  Bandwidth:      %.1f GB/s\n", m->bandwidth_gbps);
    printf("  L1 cache:       %d lines (%d bytes/line)\n", m->l1_lines, CACHE_LINE_BYTES);
    printf("  L2 cache:       %d lines\n", m->l2_lines);
    printf("  Shared memory:  %u KB (%d banks)\n",
           m->shared_mem_bytes / 1024, SHARED_BANKS);
    printf("  Transactions:   %llu total\n", (unsigned long long)m->total_transactions);
    printf("  Coalesced:      %llu (1 trans each)\n", (unsigned long long)m->coalesced_hits);
    printf("  Uncoalesced:    %llu (%llu trans each)\n",
           (unsigned long long)m->uncoalesced_misses,
           (unsigned long long)m->uncoalesced_misses);
}
