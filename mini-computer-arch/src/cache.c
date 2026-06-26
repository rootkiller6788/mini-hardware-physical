#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t log2_u32(uint32_t x)
{
    uint32_t result = 0;
    while (x >>= 1) result++;
    return result;
}

void cache_init(Cache *cache, uint32_t total_size, uint32_t line_size,
                uint32_t associativity, ReplacementPolicy rp, WritePolicy wp)
{
    cache->total_size = total_size;
    cache->line_size = line_size;
    cache->associativity = associativity;

    uint32_t total_lines = total_size / line_size;
    cache->num_sets = total_lines / associativity;

    if (cache->num_sets < 1) cache->num_sets = 1;
    if (cache->num_sets > CACHE_MAX_SETS) cache->num_sets = CACHE_MAX_SETS;

    cache->sets = (CacheSet *)calloc(cache->num_sets, sizeof(CacheSet));
    if (!cache->sets) {
        fprintf(stderr, "cache_init: failed to allocate sets\n");
        exit(1);
    }

    cache->replacement = rp;
    cache->write_policy = wp;
    cache->global_counter = 0;

    memset(&cache->stats, 0, sizeof(cache->stats));

    srand((unsigned int)time(NULL));
}

void cache_decompose_address(Cache *cache, uint32_t address,
                             uint32_t *tag, uint32_t *index, uint32_t *offset)
{
    uint32_t offset_bits = log2_u32(cache->line_size);
    uint32_t index_bits = log2_u32(cache->num_sets);

    *offset = address & ((1u << offset_bits) - 1);
    *index = (address >> offset_bits) & ((1u << index_bits) - 1);
    *tag = address >> (offset_bits + index_bits);
}

static uint32_t find_replacement_candidate(CacheSet *set, uint32_t associativity,
                                           ReplacementPolicy rp, uint64_t global_counter)
{
    uint32_t candidate = 0;

    for (uint32_t i = 0; i < associativity; i++) {
        if (!set->lines[i].valid) return i;
    }

    switch (rp) {
    case LRU: {
        uint64_t min_lru = set->lines[0].lru_counter;
        for (uint32_t i = 1; i < associativity; i++) {
            if (set->lines[i].lru_counter < min_lru) {
                min_lru = set->lines[i].lru_counter;
                candidate = i;
            }
        }
        break;
    }
    case FIFO:
        candidate = global_counter % associativity;
        break;
    case RANDOM:
        candidate = (uint32_t)rand() % associativity;
        break;
    case LFU: {
        uint64_t min_count = set->lines[0].access_count;
        for (uint32_t i = 1; i < associativity; i++) {
            if (set->lines[i].access_count < min_count) {
                min_count = set->lines[i].access_count;
                candidate = i;
            }
        }
        break;
    }
    }

    return candidate;
}

bool cache_read(Cache *cache, uint32_t address, uint8_t *data_out)
{
    uint32_t tag, index, offset;
    cache_decompose_address(cache, address, &tag, &index, &offset);

    cache->stats.reads++;
    cache->global_counter++;

    if (index >= cache->num_sets) {
        cache->stats.misses++;
        return false;
    }

    CacheSet *set = &cache->sets[index];

    for (uint32_t i = 0; i < cache->associativity; i++) {
        CacheLine *line = &set->lines[i];
        if (line->valid && line->tag == tag) {
            cache->stats.hits++;
            line->lru_counter = cache->global_counter;
            line->access_count++;

            if (data_out) {
                memcpy(data_out, &line->data[offset],
                       cache->line_size - offset < 8
                           ? cache->line_size - offset
                           : 8);
            }
            return true;
        }
    }

    cache->stats.misses++;

    uint32_t victim = find_replacement_candidate(set, cache->associativity,
                                                  cache->replacement,
                                                  cache->global_counter);
    CacheLine *victim_line = &set->lines[victim];

    if (victim_line->valid && victim_line->dirty &&
        cache->write_policy == WRITE_BACK) {
        cache->stats.writebacks++;
        cache->stats.evictions++;
    } else if (victim_line->valid) {
        cache->stats.evictions++;
    }

    victim_line->valid = true;
    victim_line->dirty = false;
    victim_line->tag = tag;
    victim_line->lru_counter = cache->global_counter;
    victim_line->access_count = 1;
    memset(victim_line->data, 0, CACHE_LINE_DATA_SIZE);

    return false;
}

bool cache_write(Cache *cache, uint32_t address, const uint8_t *data_in)
{
    uint32_t tag, index, offset;
    cache_decompose_address(cache, address, &tag, &index, &offset);

    cache->stats.writes++;
    cache->global_counter++;

    if (index >= cache->num_sets) {
        cache->stats.misses++;
        return false;
    }

    CacheSet *set = &cache->sets[index];

    for (uint32_t i = 0; i < cache->associativity; i++) {
        CacheLine *line = &set->lines[i];
        if (line->valid && line->tag == tag) {
            cache->stats.hits++;
            line->lru_counter = cache->global_counter;
            line->access_count++;
            line->dirty = true;

            if (data_in) {
                memcpy(&line->data[offset], data_in,
                       cache->line_size - offset < 8
                           ? cache->line_size - offset
                           : 8);
            }

            if (cache->write_policy == WRITE_THROUGH) {
                cache->stats.writebacks++;
            }
            return true;
        }
    }

    cache->stats.misses++;

    uint32_t victim = find_replacement_candidate(set, cache->associativity,
                                                  cache->replacement,
                                                  cache->global_counter);
    CacheLine *victim_line = &set->lines[victim];

    if (victim_line->valid && victim_line->dirty &&
        cache->write_policy == WRITE_BACK) {
        cache->stats.writebacks++;
        cache->stats.evictions++;
    } else if (victim_line->valid) {
        cache->stats.evictions++;
    }

    victim_line->valid = true;
    victim_line->dirty = (cache->write_policy == WRITE_BACK);
    victim_line->tag = tag;
    victim_line->lru_counter = cache->global_counter;
    victim_line->access_count = 1;
    memset(victim_line->data, 0, CACHE_LINE_DATA_SIZE);

    if (data_in) {
        memcpy(&victim_line->data[offset], data_in,
               cache->line_size - offset < 8 ? cache->line_size - offset : 8);
    }

    return false;
}

void cache_flush(Cache *cache)
{
    for (uint32_t s = 0; s < cache->num_sets; s++) {
        CacheSet *set = &cache->sets[s];
        for (uint32_t i = 0; i < cache->associativity; i++) {
            CacheLine *line = &set->lines[i];
            if (line->valid && line->dirty &&
                cache->write_policy == WRITE_BACK) {
                cache->stats.writebacks++;
            }
            line->valid = false;
            line->dirty = false;
            line->tag = 0;
            line->lru_counter = 0;
            line->access_count = 0;
        }
    }
}

double cache_hit_rate(const Cache *cache)
{
    uint64_t total = cache->stats.hits + cache->stats.misses;
    if (total == 0) return 0.0;
    return (double)cache->stats.hits / (double)total;
}

double cache_miss_rate(const Cache *cache)
{
    uint64_t total = cache->stats.hits + cache->stats.misses;
    if (total == 0) return 1.0;
    return (double)cache->stats.misses / (double)total;
}

double cache_amat(const Cache *cache, double hit_time_ns, double miss_penalty_ns)
{
    double miss_rate = cache_miss_rate(cache);
    return hit_time_ns + miss_rate * miss_penalty_ns;
}

void cache_print_stats(const Cache *cache)
{
    printf("========================================\n");
    printf("  Cache Statistics\n");
    printf("========================================\n");
    printf("Configuration:\n");
    printf("  Total Size:     %u bytes\n", cache->total_size);
    printf("  Line Size:      %u bytes\n", cache->line_size);
    printf("  Sets:           %u\n", cache->num_sets);
    printf("  Associativity:  %u-way\n", cache->associativity);
    printf("  Replacement:    ");
    switch (cache->replacement) {
    case LRU:    printf("LRU\n"); break;
    case FIFO:   printf("FIFO\n"); break;
    case RANDOM: printf("Random\n"); break;
    case LFU:    printf("LFU\n"); break;
    }
    printf("  Write Policy:   ");
    switch (cache->write_policy) {
    case WRITE_THROUGH: printf("Write-Through\n"); break;
    case WRITE_BACK:    printf("Write-Back\n"); break;
    }
    printf("----------------------------------------\n");
    printf("Results:\n");
    printf("  Total Accesses: %llu\n", (unsigned long long)(cache->stats.hits + cache->stats.misses));
    printf("  Hits:           %llu\n", (unsigned long long)cache->stats.hits);
    printf("  Misses:         %llu\n", (unsigned long long)cache->stats.misses);
    printf("  Evictions:      %llu\n", (unsigned long long)cache->stats.evictions);
    printf("  Writebacks:     %llu\n", (unsigned long long)cache->stats.writebacks);
    printf("  Hit Rate:       %.2f%%\n", cache_hit_rate(cache) * 100.0);
    printf("  Miss Rate:      %.2f%%\n", cache_miss_rate(cache) * 100.0);
    printf("========================================\n");
}
