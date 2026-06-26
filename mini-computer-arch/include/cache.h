#ifndef CACHE_H
#define CACHE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define CACHE_LINE_DATA_SIZE 64
#define CACHE_MAX_ASSOCIATIVITY 32
#define CACHE_MAX_SETS 4096

typedef enum {
    LRU,
    FIFO,
    RANDOM,
    LFU
} ReplacementPolicy;

typedef enum {
    WRITE_THROUGH,
    WRITE_BACK
} WritePolicy;

typedef struct {
    bool valid;
    bool dirty;
    uint32_t tag;
    uint8_t data[CACHE_LINE_DATA_SIZE];
    uint64_t lru_counter;
    uint64_t access_count;
} CacheLine;

typedef struct {
    CacheLine lines[CACHE_MAX_ASSOCIATIVITY];
} CacheSet;

typedef struct {
    uint64_t hits;
    uint64_t misses;
    uint64_t reads;
    uint64_t writes;
    uint64_t evictions;
    uint64_t writebacks;
} CacheStatistics;

typedef struct {
    uint32_t num_sets;
    uint32_t associativity;
    uint32_t line_size;
    uint32_t total_size;
    CacheSet *sets;
    ReplacementPolicy replacement;
    WritePolicy write_policy;
    CacheStatistics stats;
    uint64_t global_counter;
} Cache;

void cache_init(Cache *cache, uint32_t total_size, uint32_t line_size,
                uint32_t associativity, ReplacementPolicy rp, WritePolicy wp);
bool cache_read(Cache *cache, uint32_t address, uint8_t *data_out);
bool cache_write(Cache *cache, uint32_t address, const uint8_t *data_in);
void cache_flush(Cache *cache);
void cache_print_stats(const Cache *cache);
double cache_hit_rate(const Cache *cache);
double cache_miss_rate(const Cache *cache);
double cache_amat(const Cache *cache, double hit_time_ns, double miss_penalty_ns);
void cache_decompose_address(Cache *cache, uint32_t address,
                             uint32_t *tag, uint32_t *index, uint32_t *offset);

#endif /* CACHE_H */
