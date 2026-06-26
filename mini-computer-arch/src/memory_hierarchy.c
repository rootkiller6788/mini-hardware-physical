#include "memory_hierarchy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void mem_hierarchy_create(MemoryHierarchy *h)
{
    h->count = 0;
    memset(h->levels, 0, sizeof(h->levels));
    memset(h->hit_rates, 0, sizeof(h->hit_rates));
}

void mem_hierarchy_add_level(MemoryHierarchy *h, const char *name,
                             uint64_t size_bytes, double access_time_ns,
                             double bandwidth_mbps)
{
    if (h->count >= MEMORY_HIERARCHY_MAX_LEVELS) return;

    MemoryLevel *level = &h->levels[h->count];
    strncpy(level->name, name, sizeof(level->name) - 1);
    level->name[sizeof(level->name) - 1] = '\0';
    level->size_bytes = size_bytes;
    level->access_time_ns = access_time_ns;
    level->bandwidth_mbps = bandwidth_mbps;
    h->count++;
}

double mem_hierarchy_access_time(const MemoryHierarchy *h)
{
    double total_time = 0.0;
    double miss_prob = 1.0;

    for (size_t i = 0; i < h->count; i++) {
        double hit_rate = h->hit_rates[i];
        total_time += miss_prob * h->levels[i].access_time_ns;
        miss_prob *= (1.0 - hit_rate);
    }

    return total_time;
}

void mem_hierarchy_print(const MemoryHierarchy *h)
{
    printf("========================================\n");
    printf("  Memory Hierarchy Configuration\n");
    printf("========================================\n");
    printf("%-16s %-12s %-12s %-14s %-10s\n",
           "Level", "Size", "Access Time", "Bandwidth", "Hit Rate");
    printf("------------------------------------------------------------\n");

    for (size_t i = 0; i < h->count; i++) {
        const MemoryLevel *level = &h->levels[i];
        char size_str[32];
        if (level->size_bytes >= 1073741824ULL) {
            snprintf(size_str, sizeof(size_str), "%.1f GB",
                     level->size_bytes / 1073741824.0);
        } else if (level->size_bytes >= 1048576ULL) {
            snprintf(size_str, sizeof(size_str), "%.1f MB",
                     level->size_bytes / 1048576.0);
        } else if (level->size_bytes >= 1024ULL) {
            snprintf(size_str, sizeof(size_str), "%.1f KB",
                     level->size_bytes / 1024.0);
        } else {
            snprintf(size_str, sizeof(size_str), "%llu B",
                     (unsigned long long)level->size_bytes);
        }

        printf("%-16s %-12s %-8.1f ns  %-10.1f MB/s %-10.1f%%\n",
               level->name, size_str, level->access_time_ns,
               level->bandwidth_mbps,
               h->hit_rates[i] * 100.0);
    }

    printf("------------------------------------------------------------\n");
    printf("Average Access Time: %.2f ns\n", mem_hierarchy_access_time(h));
    printf("========================================\n");
}
