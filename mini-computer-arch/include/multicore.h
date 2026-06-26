#ifndef MULTICORE_H
#define MULTICORE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "cache.h"

#define MULTICORE_MAX_CORES 4
#define MULTICORE_NUM_REGS 16

typedef struct {
    uint32_t id;
    uint32_t registers[MULTICORE_NUM_REGS];
    uint32_t pc;
    Cache l1_cache;
} Core;

typedef struct {
    uint32_t size;
    uint8_t *state;
} SharedCache;

typedef struct {
    Core cores[MULTICORE_MAX_CORES];
    uint32_t num_cores;
    SharedCache shared_l2_cache;
    uint32_t bus_transaction_count;
} MulticoreSystem;

void mc_init(MulticoreSystem *mc, uint32_t num_cores,
             uint32_t l1_size, uint32_t l2_size);
void mc_run_core(MulticoreSystem *mc, uint32_t core_id, uint32_t instruction);
void mc_bus_request(MulticoreSystem *mc, uint32_t core_id,
                    uint32_t address, bool is_write);
void mc_print_state(const MulticoreSystem *mc);

#endif /* MULTICORE_H */
