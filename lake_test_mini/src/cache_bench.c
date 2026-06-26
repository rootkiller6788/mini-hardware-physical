/**
 * cache_bench.c — Cache Hierarchy Benchmarking Implementation
 *
 * L2: Simulates LRU cache with configurable size/associativity/line size.
 * L3: Stack distance profiling (Mattson's algorithm) for miss curve prediction.
 * L4: AMAT computation and power-law miss rate modeling.
 * L5: Full cache benchmark producing miss curves across working set sizes.
 *
 * Reference: Hennessy & Patterson, "Computer Architecture: A Quantitative
 * Approach" (6th Ed.), Appendix B: Cache Principles.
 */

#include "cache_bench.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ============================================================================
 * SimCache — LRU Cache Simulator
 * ============================================================================ */

void sim_cache_init(SimCache *cache, uint32_t total_size, uint32_t line_size,
                    uint32_t associativity, bool write_back) {
    if (!cache) return;
    memset(cache, 0, sizeof(SimCache));
    
    cache->line_size = line_size;
    cache->assoc = associativity;
    cache->total_size = total_size;
    cache->num_sets = total_size / (line_size * associativity);
    cache->write_back = write_back;
    
    if (cache->num_sets == 0) cache->num_sets = 1;
    
    cache->sets = (CacheSetLRU *)calloc(cache->num_sets, sizeof(CacheSetLRU));
    for (uint32_t s = 0; s < cache->num_sets; s++) {
        cache->sets[s].assoc = associativity;
        cache->sets[s].line_size = line_size;
        cache->sets[s].tags = (uint64_t *)calloc(associativity, sizeof(uint64_t));
        cache->sets[s].access_timestamps = (uint64_t *)calloc(associativity, sizeof(uint64_t));
        cache->sets[s].valid = (bool *)calloc(associativity, sizeof(bool));
        cache->sets[s].dirty = (bool *)calloc(associativity, sizeof(bool));
    }
}

bool sim_cache_access(SimCache *cache, uint64_t address, CacheAccessType type) {
    if (!cache || !cache->sets) return false;
    
    uint64_t tag = address / (cache->line_size * cache->num_sets);
    uint32_t set_idx = (address / cache->line_size) % cache->num_sets;
    
    CacheSetLRU *set = &cache->sets[set_idx];
    cache->total_accesses++;
    
    /* Check for hit */
    for (uint32_t i = 0; i < set->assoc; i++) {
        if (set->valid[i] && set->tags[i] == tag) {
            /* Cache HIT */
            set->access_timestamps[i] = ++cache->global_time;
            cache->total_hits++;
            if (type == CACHE_ACCESS_WRITE) {
                set->dirty[i] = true;
            }
            return true;
        }
    }
    
    /* Cache MISS — need to evict */
    cache->total_misses++;
    
    /* Find LRU way (or first invalid) */
    uint32_t victim = 0;
    uint64_t oldest = UINT64_MAX;
    bool found_invalid = false;
    
    for (uint32_t i = 0; i < set->assoc; i++) {
        if (!set->valid[i]) {
            victim = i;
            found_invalid = true;
            break;
        }
        if (set->access_timestamps[i] < oldest) {
            oldest = set->access_timestamps[i];
            victim = i;
        }
    }
    
    /* Eviction of dirty line triggers writeback */
    if (!found_invalid && set->valid[victim] && set->dirty[victim]) {
        cache->total_writebacks++;
    }
    if (!found_invalid) {
        cache->total_evictions++;
    }
    
    /* Install new line */
    set->tags[victim] = tag;
    set->valid[victim] = true;
    set->dirty[victim] = (type == CACHE_ACCESS_WRITE);
    set->access_timestamps[victim] = ++cache->global_time;
    
    return false;
}

void sim_cache_flush(SimCache *cache) {
    if (!cache) return;
    
    for (uint32_t s = 0; s < cache->num_sets; s++) {
        CacheSetLRU *set = &cache->sets[s];
        for (uint32_t i = 0; i < set->assoc; i++) {
            if (set->valid[i] && set->dirty[i]) {
                cache->total_writebacks++;
            }
            set->valid[i] = false;
            set->dirty[i] = false;
        }
    }
}

void sim_cache_stats(const SimCache *cache, uint64_t *hits, uint64_t *misses,
                     double *hit_rate, double *amat, double hit_ns, double miss_ns) {
    if (!cache) return;
    if (hits) *hits = cache->total_hits;
    if (misses) *misses = cache->total_misses;
    if (hit_rate && cache->total_accesses > 0) {
        *hit_rate = (double)cache->total_hits / (double)cache->total_accesses;
    }
    if (amat && cache->total_accesses > 0) {
        double mr = (double)cache->total_misses / (double)cache->total_accesses;
        *amat = hit_ns + mr * miss_ns;
    }
}

void sim_cache_destroy(SimCache *cache) {
    if (!cache) return;
    if (cache->sets) {
        for (uint32_t s = 0; s < cache->num_sets; s++) {
            free(cache->sets[s].tags);
            free(cache->sets[s].access_timestamps);
            free(cache->sets[s].valid);
            free(cache->sets[s].dirty);
        }
        free(cache->sets);
    }
    memset(cache, 0, sizeof(SimCache));
}

/* ============================================================================
 * Cache Bench Configuration and Run
 * ============================================================================ */

void cache_bench_config_init(CacheBenchConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(CacheBenchConfig));
    
    /* Default: simulate typical Xeon L1/L2/L3 */
    cfg->l1.level = 1;
    cfg->l1.size_bytes = 32 * 1024;      /* 32 KB */
    cfg->l1.line_size = 64;
    cfg->l1.associativity = 8;
    cfg->l1.num_sets = cfg->l1.size_bytes / (cfg->l1.line_size * cfg->l1.associativity);
    cfg->l1.hit_latency_ns = 1.0;        /* ~4 cycles at 2.3 GHz */
    cfg->l1.miss_penalty_ns = 8.0;       /* L2 hit */
    
    cfg->l2.level = 2;
    cfg->l2.size_bytes = 256 * 1024;     /* 256 KB */
    cfg->l2.line_size = 64;
    cfg->l2.associativity = 8;
    cfg->l2.num_sets = cfg->l2.size_bytes / (cfg->l2.line_size * cfg->l2.associativity);
    cfg->l2.hit_latency_ns = 4.0;        /* ~12 cycles */
    cfg->l2.miss_penalty_ns = 30.0;      /* L3 hit */
    
    cfg->l3.level = 3;
    cfg->l3.size_bytes = 60 * 1024 * 1024; /* 60 MB (shared) */
    cfg->l3.line_size = 64;
    cfg->l3.associativity = 20;
    cfg->l3.num_sets = cfg->l3.size_bytes / (cfg->l3.line_size * cfg->l3.associativity);
    cfg->l3.hit_latency_ns = 12.0;       /* ~40 cycles */
    cfg->l3.miss_penalty_ns = 100.0;     /* DRAM access */
    
    cfg->working_set_min = 8 * 1024;      /* 8 KB */
    cfg->working_set_max = 128 * 1024 * 1024; /* 128 MB */
    cfg->working_set_step = 2;           /* multiply by 2 each step */
    cfg->stride_count = 1;
    cfg->measure_inclusive = true;
}

CacheBenchResult *cache_bench_run(const CacheBenchConfig *cfg) {
    if (!cfg) return NULL;
    
    /* Count how many working set sizes we'll test */
    size_t num_points = 0;
    for (uint64_t ws = cfg->working_set_min; ws <= cfg->working_set_max; ws *= cfg->working_set_step) {
        num_points++;
    }
    
    CacheBenchResult *result = (CacheBenchResult *)calloc(1, sizeof(CacheBenchResult));
    if (!result) return NULL;
    
    result->config = *cfg;
    result->num_points = num_points;
    result->points = (CacheBenchPoint *)calloc(num_points, sizeof(CacheBenchPoint));
    if (!result->points) {
        free(result);
        return NULL;
    }
    
    size_t point_idx = 0;
    for (uint64_t ws = cfg->working_set_min; ws <= cfg->working_set_max; ws *= cfg->working_set_step) {
        /* For each working set size, simulate sequential access */
        SimCache cache;
        sim_cache_init(&cache, (uint32_t)ws, cfg->l1.line_size, cfg->l1.associativity, false);
        
        uint64_t num_accesses = ws / cfg->l1.line_size;
        if (num_accesses < 1000) num_accesses = 1000;
        if (num_accesses > 10000000) num_accesses = 10000000;
        
        /* Simulate sequential access pattern (common in lakehouse scans) */
        for (uint64_t i = 0; i < num_accesses; i++) {
            uint64_t addr = i * cfg->l1.line_size;
            sim_cache_access(&cache, addr, CACHE_ACCESS_READ);
        }
        
        /* Record results */
        CacheBenchPoint *point = &result->points[point_idx];
        point->working_set_bytes = ws;
        point->stride_bytes = cfg->l1.line_size;
        point->accesses = num_accesses;
        point->hits = cache.total_hits;
        point->misses = cache.total_misses;
        
        if (num_accesses > 0) {
            point->hit_rate = (double)cache.total_hits / (double)num_accesses;
            point->miss_rate = (double)cache.total_misses / (double)num_accesses;
        }
        
        point->avg_access_time_ns = cache_compute_amat(
            cfg->l1.hit_latency_ns, point->miss_rate, cfg->l1.miss_penalty_ns);
        
        point->bandwidth_mbps = (double)(point->hits * cfg->l1.line_size) /
                                (1.0 / cfg->l1.hit_latency_ns * 1e9 * 1e-6);
        /* Simplified: bandwidth = data_moved / access_time_estimate */
        if (num_accesses > 0) {
            double total_ns = (double)point->hits * cfg->l1.hit_latency_ns +
                              (double)point->misses * cfg->l1.miss_penalty_ns;
            point->bandwidth_mbps = (double)(num_accesses * cfg->l1.line_size) / total_ns * 1000.0;
        }
        
        sim_cache_destroy(&cache);
        point_idx++;
    }
    
    return result;
}

void cache_bench_result_destroy(CacheBenchResult *result) {
    if (!result) return;
    free(result->points);
    free(result);
}

void cache_bench_result_print(const CacheBenchResult *result) {
    if (!result) {
        printf("CacheBenchResult: NULL\n");
        return;
    }
    
    printf("\n========== Cache Miss Curve ==========\n");
    printf("L1: %lu KB, %u-way, %u B line\n",
           (unsigned long)(result->config.l1.size_bytes / 1024),
           result->config.l1.associativity, result->config.l1.line_size);
    printf("%-16s %-12s %-12s %-12s %-14s %-14s\n",
           "WorkingSet", "Accesses", "Hit Rate", "Miss Rate", "AMAT(ns)", "BW(MB/s)");
    printf("--------------------------------------------------------------\n");
    
    for (size_t i = 0; i < result->num_points; i++) {
        const CacheBenchPoint *p = &result->points[i];
        printf("%-10lu KB  %-10lu  %-10.2f%% %-10.2f%% %-12.2f %-12.2f\n",
               (unsigned long)(p->working_set_bytes / 1024),
               (unsigned long)p->accesses,
               p->hit_rate * 100.0, p->miss_rate * 100.0,
               p->avg_access_time_ns, p->bandwidth_mbps);
    }
    printf("======================================\n");
}

/* ============================================================================
 * Stack Distance Profiling (Mattson's Algorithm, 1970)
 * ============================================================================ */

StackDistProfile *stack_dist_build(const uint64_t *addresses, size_t num_accesses) {
    if (!addresses || num_accesses == 0) return NULL;
    
    /* Simple implementation using LRU stack simulation.
     * We track the last-access position of each unique address.
     * Maximum stack distance = number of unique addresses seen so far.
     */
    
    /* First, count unique addresses */
    uint64_t *unique_addrs = (uint64_t *)calloc(num_accesses, sizeof(uint64_t));
    size_t unique_count = 0;
    
    /* Use a simple visited tracking scheme: for each new address, check all previous */
    for (size_t i = 0; i < num_accesses; i++) {
        bool found = false;
        for (size_t j = 0; j < unique_count; j++) {
            if (unique_addrs[j] == addresses[i]) {
                found = true;
                break;
            }
        }
        if (!found && unique_count < num_accesses) {
            unique_addrs[unique_count++] = addresses[i];
        }
    }
    free(unique_addrs);
    
    /* Allocate histogram bins: one per unique address + infinite */
    uint64_t *histogram = (uint64_t *)calloc(unique_count + 2, sizeof(uint64_t));
    uint64_t *last_pos = (uint64_t *)malloc(unique_count * sizeof(uint64_t));
    uint64_t *stack = (uint64_t *)malloc(unique_count * sizeof(uint64_t));
    size_t stack_top = 0;
    
    for (size_t i = 0; i < num_accesses; i++) {
        uint64_t addr = addresses[i];
        
        /* Find address in stack */
        ssize_t stack_pos = -1;
        for (size_t j = 0; j < stack_top; j++) {
            if (stack[j] == addr) {
                stack_pos = (ssize_t)j;
                break;
            }
        }
        
        if (stack_pos >= 0) {
            /* HIT: stack distance = position in stack */
            histogram[(size_t)stack_pos]++;
            /* Move to top of stack (LRU promotion) */
            for (ssize_t j = stack_pos; j > 0; j--) {
                stack[j] = stack[j - 1];
            }
            stack[0] = addr;
        } else {
            /* MISS: distance = infinity (unique_count) */
            histogram[unique_count]++;
            /* Push to top of stack */
            if (stack_top < unique_count) {
                for (ssize_t j = (ssize_t)stack_top; j > 0; j--) {
                    stack[j] = stack[j - 1];
                }
                stack[0] = addr;
                stack_top++;
            } else {
                /* Evict bottom and shift */
                for (ssize_t j = (ssize_t)unique_count - 1; j > 0; j--) {
                    stack[j] = stack[j - 1];
                }
                stack[0] = addr;
            }
        }
    }
    
    /* Build histogram entries */
    size_t num_entries = 0;
    for (size_t i = 0; i <= unique_count; i++) {
        if (histogram[i] > 0) num_entries++;
    }
    
    StackDistProfile *profile = (StackDistProfile *)calloc(1, sizeof(StackDistProfile));
    profile->entries = (StackDistEntry *)calloc(num_entries, sizeof(StackDistEntry));
    profile->num_entries = num_entries;
    profile->total_references = num_accesses;
    profile->unique_addresses = unique_count;
    
    size_t entry_idx = 0;
    uint64_t cumulative = 0;
    for (size_t i = 0; i <= unique_count; i++) {
        if (histogram[i] > 0) {
            cumulative += histogram[i];
            profile->entries[entry_idx].distance = (uint64_t)i;
            profile->entries[entry_idx].count = histogram[i];
            profile->entries[entry_idx].cumulative_prob = (double)cumulative / (double)num_accesses;
            entry_idx++;
        }
    }
    
    free(histogram);
    free(last_pos);
    free(stack);
    
    return profile;
}

double stack_dist_predict_miss_rate(const StackDistProfile *profile,
                                     uint64_t cache_size, uint32_t line_size) {
    if (!profile || line_size == 0) return 0.0;
    
    uint64_t cache_lines = cache_size / line_size;
    uint64_t misses_beyond = 0;
    
    /* For LRU, a cache of C lines will miss on all accesses with
     * stack distance >= C (each maps to a unique line) */
    for (size_t i = 0; i < profile->num_entries; i++) {
        if (profile->entries[i].distance >= cache_lines) {
            misses_beyond += profile->entries[i].count;
        }
    }
    
    return (double)misses_beyond / (double)profile->total_references;
}

void stack_dist_profile_destroy(StackDistProfile *profile) {
    if (!profile) return;
    free(profile->entries);
    free(profile);
}

/* ============================================================================
 * L4: Cache Performance Formulas
 * ============================================================================ */

double cache_compute_amat(double hit_time_ns, double miss_rate, double miss_penalty_ns) {
    return hit_time_ns + miss_rate * miss_penalty_ns;
}

double cache_fit_power_law_exponent(const CacheBenchResult *result, double *r_squared) {
    if (!result || result->num_points < 2) {
        if (r_squared) *r_squared = 0.0;
        return 0.0;
    }
    
    /* Linear regression on log-log scale: log(MR) = log(MR0) - alpha * log(C/C0)
     * y = log(miss_rate), x = log(cache_size)
     * alpha = -slope
     */
    
    double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_xx = 0.0, sum_yy = 0.0;
    size_t n = 0;
    
    for (size_t i = 0; i < result->num_points; i++) {
        if (result->points[i].miss_rate > 0.0) {
            double x = log((double)result->points[i].working_set_bytes);
            double y = log(result->points[i].miss_rate);
            sum_x += x;
            sum_y += y;
            sum_xy += x * y;
            sum_xx += x * x;
            sum_yy += y * y;
            n++;
        }
    }
    
    if (n < 2) {
        if (r_squared) *r_squared = 0.0;
        return 0.0;
    }
    
    double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
    double intercept = (sum_y - slope * sum_x) / n;
    
    /* R-squared */
    double ss_res = 0.0, ss_tot = 0.0;
    double y_mean = sum_y / n;
    for (size_t i = 0; i < result->num_points; i++) {
        if (result->points[i].miss_rate > 0.0) {
            double x = log((double)result->points[i].working_set_bytes);
            double y = log(result->points[i].miss_rate);
            double y_pred = slope * x + intercept;
            ss_res += (y - y_pred) * (y - y_pred);
            ss_tot += (y - y_mean) * (y - y_mean);
        }
    }
    
    if (r_squared) {
        *r_squared = (ss_tot > 0.0) ? (1.0 - ss_res / ss_tot) : 0.0;
    }
    
    /* alpha = -slope (since MR decreases with cache size in log-log) */
    return -slope;
}