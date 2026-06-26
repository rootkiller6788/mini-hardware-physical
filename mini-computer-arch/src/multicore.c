#include "multicore.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mc_init(MulticoreSystem *mc, uint32_t num_cores,
             uint32_t l1_size, uint32_t l2_size)
{
    if (num_cores > MULTICORE_MAX_CORES) num_cores = MULTICORE_MAX_CORES;

    mc->num_cores = num_cores;
    mc->bus_transaction_count = 0;
    mc->shared_l2_cache.size = l2_size;

    for (uint32_t i = 0; i < num_cores; i++) {
        mc->cores[i].id = i;
        mc->cores[i].pc = 0;
        memset(mc->cores[i].registers, 0, sizeof(mc->cores[i].registers));
        cache_init(&mc->cores[i].l1_cache, l1_size, 64, 4, LRU, WRITE_BACK);
    }

    mc->shared_l2_cache.state = (uint8_t *)calloc(l2_size, 1);
    if (!mc->shared_l2_cache.state) {
        fprintf(stderr, "mc_init: failed to allocate L2 cache\n");
        exit(1);
    }
}

void mc_run_core(MulticoreSystem *mc, uint32_t core_id, uint32_t instruction)
{
    if (core_id >= mc->num_cores) return;

    Core *core = &mc->cores[core_id];
    core->pc++;

    uint8_t dummy[8];
    uint32_t addr = instruction & 0x0000FFFF;

    if (instruction & 0x80000000) {
        cache_read(&core->l1_cache, addr, dummy);
    } else {
        cache_write(&core->l1_cache, addr, dummy);
    }

    mc_bus_request(mc, core_id, addr, instruction & 0x80000000);
}

void mc_bus_request(MulticoreSystem *mc, uint32_t core_id,
                    uint32_t address, bool is_write)
{
    mc->bus_transaction_count++;

    (void)core_id;
    (void)address;
    (void)is_write;
}

void mc_print_state(const MulticoreSystem *mc)
{
    printf("========================================\n");
    printf("  Multicore System State\n");
    printf("========================================\n");
    printf("  Cores: %u\n", mc->num_cores);
    printf("  Bus Transactions: %u\n", mc->bus_transaction_count);
    printf("  L2 Shared Cache: %u bytes\n", mc->shared_l2_cache.size);
    printf("----------------------------------------\n");

    for (uint32_t i = 0; i < mc->num_cores; i++) {
        const Core *core = &mc->cores[i];
        printf("  Core %u: PC=0x%08X\n", core->id, core->pc);
        printf("    L1 Cache: %u hits, %u misses, %.1f%% hit rate\n",
               (unsigned int)core->l1_cache.stats.hits,
               (unsigned int)core->l1_cache.stats.misses,
               cache_hit_rate(&core->l1_cache) * 100.0);
    }
    printf("========================================\n");
}
