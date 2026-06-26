#ifndef MEMORY_GPU_H
#define MEMORY_GPU_H

#include <stdbool.h>
#include <stdint.h>

#define CACHE_LINE_BYTES    128
#define MAX_CHANNELS        16
#define SHARED_BANKS        32
#define L2_CACHE_KB         4096

typedef enum {
    MEMSPACE_GLOBAL,
    MEMSPACE_SHARED,
    MEMSPACE_CONSTANT,
    MEMSPACE_LOCAL,
    MEMSPACE_TEXTURE
} MemSpace;

typedef struct {
    uint32_t address;
    bool     is_read;
    int      size;
    uint32_t data;
    int      latency_cycles;
    MemSpace space;
} MemoryAccess;

typedef struct {
    uint8_t *global_mem;
    uint32_t global_mem_bytes;
    uint8_t *shared_mem;
    uint32_t shared_mem_bytes;
    uint8_t *l1_cache;
    int      l1_lines;
    uint8_t *l2_cache;
    int      l2_lines;
    uint32_t *const_cache;
    int      num_channels;
    double   bandwidth_gbps;
    int      queue_depth;
    uint64_t total_transactions;
    uint64_t coalesced_hits;
    uint64_t uncoalesced_misses;
} GPUMemory;

GPUMemory gpu_mem_init(uint32_t size_gb);
void      gpu_mem_free(GPUMemory *m);
uint32_t  gpu_mem_load(GPUMemory *m, uint32_t addr, MemSpace space);
void      gpu_mem_store(GPUMemory *m, uint32_t addr, uint32_t data, MemSpace space);
bool      mem_is_coalesced(const GPUMemory *m, const uint32_t *addresses, int n_addresses);
int       mem_coalesced_access(GPUMemory *m, const uint32_t *addrs, int n);
int       mem_uncoalesced_access(GPUMemory *m, const uint32_t *addrs, int n);
int       mem_bank_conflict_count(const uint32_t *addresses, int n);
void      gpu_mem_print_bandwidth_stats(const GPUMemory *m);

#endif
