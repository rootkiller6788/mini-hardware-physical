#ifndef MEMORY_HIERARCHY_H
#define MEMORY_HIERARCHY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define MEMORY_HIERARCHY_MAX_LEVELS 8

typedef struct {
    double hit_time;
    double miss_penalty;
    double hit_rate;
} MemoryStats;

typedef struct {
    char name[32];
    uint64_t size_bytes;
    double access_time_ns;
    double bandwidth_mbps;
} MemoryLevel;

typedef struct {
    MemoryLevel levels[MEMORY_HIERARCHY_MAX_LEVELS];
    size_t count;
    double hit_rates[MEMORY_HIERARCHY_MAX_LEVELS];
} MemoryHierarchy;

void mem_hierarchy_create(MemoryHierarchy *h);
void mem_hierarchy_add_level(MemoryHierarchy *h, const char *name, uint64_t size_bytes,
                             double access_time_ns, double bandwidth_mbps);
double mem_hierarchy_access_time(const MemoryHierarchy *h);
void mem_hierarchy_print(const MemoryHierarchy *h);

#endif /* MEMORY_HIERARCHY_H */
