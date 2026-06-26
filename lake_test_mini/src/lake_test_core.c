/**
 * lake_test_core.c — Core Implementation for Hardware Testing Framework
 *
 * Implements the fundamental operations for test configuration management,
 * benchmark result handling, test suite orchestration, and platform
 * information modeling. All functions implement independent knowledge
 * points from computer architecture and testing methodology.
 *
 * Knowledge Coverage:
 *   L1: struct initialization and lifecycle management
 *   L2: Test configuration for hardware benchmarking concepts
 *   L3: Test suite orchestration data structure
 *   L6: Complete test suite management pipeline
 */

#include "lake_test_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ============================================================================
 * TestConfig Implementation
 * ============================================================================ */

void test_config_init(TestConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(TestConfig));
    cfg->working_set_bytes = 64 * 1024 * 1024;  /* 64 MB default */
    cfg->num_operations = 1000000;
    cfg->num_repetitions = 5;
    cfg->mem_pattern = MEM_LINEAR;
    cfg->io_pattern = IO_SEQUENTIAL;
    cfg->mode = TEST_MODE_SINGLE;
    cfg->random_seed = 12345;
    cfg->warm_cache = true;
    cfg->use_huge_pages = false;
    cfg->num_threads = 1;
    cfg->block_size_bytes = 4096;
    cfg->time_limit_sec = 60.0;
    snprintf(cfg->description, sizeof(cfg->description),
             "Default test: %lu MB working set, %lu ops",
             (unsigned long)(cfg->working_set_bytes >> 20),
             (unsigned long)cfg->num_operations);
}

void test_config_set_workload(TestConfig *cfg, const LakeWorkloadProfile *profile) {
    if (!cfg || !profile) return;
    test_config_init(cfg);
    cfg->io_pattern = profile->dominant_io;
    cfg->mem_pattern = profile->dominant_mem;
    cfg->working_set_bytes = (uint64_t)(profile->data_size_gb * 1024.0 * 1024.0 * 1024.0);
    /* Scale operations based on expected QPS for a 60-second window */
    if (profile->expected_qps > 0) {
        cfg->num_operations = profile->expected_qps * 60;
    }
    cfg->block_size_bytes = 4096;
    snprintf(cfg->description, sizeof(cfg->description),
             "Lakehouse workload: %s, %.1f GB, %lu QPS target, scan=%.2f",
             profile->profile_name, profile->data_size_gb,
             (unsigned long)profile->expected_qps, profile->scan_fraction);
}

/* ============================================================================
 * BenchResult Implementation
 * ============================================================================ */

void bench_result_init(BenchResult *result) {
    if (!result) return;
    memset(result, 0, sizeof(BenchResult));
    result->is_valid = false;
    result->error_code = 0;
}

/* ============================================================================
 * TestSuite Implementation
 * ============================================================================ */

TestSuite *test_suite_create(const char *name, size_t capacity) {
    TestSuite *suite = (TestSuite *)calloc(1, sizeof(TestSuite));
    if (!suite) return NULL;
    
    strncpy(suite->suite_name, name, sizeof(suite->suite_name) - 1);
    suite->capacity = capacity;
    suite->num_tests = 0;
    suite->executed = false;
    suite->suite_score = 0.0;
    
    suite->configs = (TestConfig *)calloc(capacity, sizeof(TestConfig));
    suite->results = (BenchResult *)calloc(capacity, sizeof(BenchResult));
    
    if (!suite->configs || !suite->results) {
        free(suite->configs);
        free(suite->results);
        free(suite);
        return NULL;
    }
    
    /* Initialize all result slots */
    for (size_t i = 0; i < capacity; i++) {
        bench_result_init(&suite->results[i]);
    }
    
    return suite;
}

bool test_suite_add_config(TestSuite *suite, const TestConfig *cfg) {
    if (!suite || !cfg || suite->num_tests >= suite->capacity) {
        return false;
    }
    memcpy(&suite->configs[suite->num_tests], cfg, sizeof(TestConfig));
    suite->num_tests++;
    return true;
}

const BenchResult *test_suite_get_result(const TestSuite *suite, size_t idx) {
    if (!suite || idx >= suite->num_tests) return NULL;
    return &suite->results[idx];
}

void test_suite_compute_aggregate(TestSuite *suite) {
    if (!suite || suite->num_tests == 0) return;
    
    AggregateStats *agg = &suite->aggregate;
    memset(agg, 0, sizeof(AggregateStats));
    agg->num_runs = (uint32_t)suite->num_tests;
    
    /* First pass: compute means */
    double sum_tp = 0.0, sum_lat = 0.0;
    double valid_count = 0;
    
    for (size_t i = 0; i < suite->num_tests; i++) {
        if (suite->results[i].is_valid) {
            sum_tp += suite->results[i].throughput_ops_per_sec;
            sum_lat += suite->results[i].avg_latency_ns;
            valid_count++;
        }
    }
    
    if (valid_count == 0) return;
    
    agg->mean_throughput = sum_tp / valid_count;
    agg->mean_latency_ns = sum_lat / valid_count;
    agg->min_throughput = agg->mean_throughput;
    agg->max_throughput = agg->mean_throughput;
    agg->min_latency_ns = agg->mean_latency_ns;
    agg->max_latency_ns = agg->mean_latency_ns;
    
    /* Second pass: min/max and stddev */
    double ss_tp = 0.0, ss_lat = 0.0;
    for (size_t i = 0; i < suite->num_tests; i++) {
        if (suite->results[i].is_valid) {
            double tp = suite->results[i].throughput_ops_per_sec;
            double lat = suite->results[i].avg_latency_ns;
            
            if (tp < agg->min_throughput) agg->min_throughput = tp;
            if (tp > agg->max_throughput) agg->max_throughput = tp;
            if (lat < agg->min_latency_ns) agg->min_latency_ns = lat;
            if (lat > agg->max_latency_ns) agg->max_latency_ns = lat;
            
            ss_tp += (tp - agg->mean_throughput) * (tp - agg->mean_throughput);
            ss_lat += (lat - agg->mean_latency_ns) * (lat - agg->mean_latency_ns);
        }
    }
    
    agg->stddev_throughput = sqrt(ss_tp / valid_count);
    agg->stddev_latency_ns = sqrt(ss_lat / valid_count);
    
    /* Coefficient of variation */
    if (agg->mean_throughput > 0.0) {
        agg->cv_throughput = agg->stddev_throughput / agg->mean_throughput;
    }
    if (agg->mean_latency_ns > 0.0) {
        agg->cv_latency = agg->stddev_latency_ns / agg->mean_latency_ns;
    }
    
    /* Outlier detection using 2-sigma rule */
    double tp_upper = agg->mean_throughput + 2.0 * agg->stddev_throughput;
    double tp_lower = agg->mean_throughput - 2.0 * agg->stddev_throughput;
    for (size_t i = 0; i < suite->num_tests; i++) {
        if (suite->results[i].is_valid) {
            double tp = suite->results[i].throughput_ops_per_sec;
            if (tp > tp_upper || tp < tp_lower) {
                agg->num_outliers++;
            }
        }
    }
}

void test_suite_destroy(TestSuite *suite) {
    if (!suite) return;
    free(suite->configs);
    free(suite->results);
    free(suite);
}

/* ============================================================================
 * PlatformInfo Implementation
 * ============================================================================ */

void platform_info_init(PlatformInfo *info) {
    if (!info) return;
    memset(info, 0, sizeof(PlatformInfo));
    
    /* Simulated modern server hardware (matching typical lakehouse node) */
    strncpy(info->cpu_name, "Simulated Intel Xeon Platinum 8380", sizeof(info->cpu_name) - 1);
    info->cpu_cores = 40;
    info->cpu_threads_per_core = 2;
    info->cpu_freq_ghz = 2.3;
    info->l1d_cache_bytes = 32 * 1024 * 40;    /* 32 KB per core * 40 cores */
    info->l1i_cache_bytes = 32 * 1024 * 40;
    info->l2_cache_bytes = 1280 * 1024 * 40;    /* 1.25 MB per core * 40 */
    info->l3_cache_bytes = 60 * 1024 * 1024;     /* 60 MB shared LLC */
    info->ram_bytes = 256ULL * 1024 * 1024 * 1024; /* 256 GB */
    info->ram_bandwidth_mbps = 204800.0;          /* ~200 GB/s DDR4-3200 8ch */
    info->disk_bandwidth_mbps = 7000.0;           /* NVMe Gen4 x4 */
    info->disk_capacity_bytes = 4ULL * 1024 * 1024 * 1024 * 1024; /* 4 TB */
    info->has_nvme = true;
    info->has_gpu = false;
    info->cache_line_bytes = 64;
    info->page_size_bytes = 4096;
}

void platform_info_print(const PlatformInfo *info) {
    if (!info) {
        printf("PlatformInfo: NULL\n");
        return;
    }
    
    printf("========================================\n");
    printf("  Platform Hardware Summary\n");
    printf("========================================\n");
    printf("  CPU:           %s\n", info->cpu_name);
    printf("  Cores/Threads: %u cores, %u threads/core (%u logical)\n",
           info->cpu_cores, info->cpu_threads_per_core,
           info->cpu_cores * info->cpu_threads_per_core);
    printf("  Frequency:     %.1f GHz\n", info->cpu_freq_ghz);
    printf("  L1d Cache:     %.1f KB\n", info->l1d_cache_bytes / 1024.0);
    printf("  L1i Cache:     %.1f KB\n", info->l1i_cache_bytes / 1024.0);
    printf("  L2 Cache:      %.1f MB\n", info->l2_cache_bytes / (1024.0 * 1024.0));
    printf("  L3 Cache:      %.1f MB\n", info->l3_cache_bytes / (1024.0 * 1024.0));
    printf("  RAM:           %.1f GB\n", info->ram_bytes / (1024.0 * 1024.0 * 1024.0));
    printf("  RAM BW:        %.1f GB/s\n", info->ram_bandwidth_mbps / 1024.0);
    printf("  Disk Capacity: %.1f TB\n", info->disk_capacity_bytes / (1024.0 * 1024.0 * 1024.0 * 1024.0));
    printf("  Disk BW:       %.1f GB/s\n", info->disk_bandwidth_mbps / 1024.0);
    printf("  NVMe:          %s\n", info->has_nvme ? "Yes" : "No");
    printf("  GPU:           %s\n", info->has_gpu ? "Yes" : "No");
    printf("  Cache Line:    %u bytes\n", info->cache_line_bytes);
    printf("  Page Size:     %lu KB\n", (unsigned long)(info->page_size_bytes / 1024));
    printf("========================================\n");
}

/* ============================================================================
 * HwCounterSet Implementation
 * ============================================================================ */

void hw_counter_set_init(HwCounterSet *cs) {
    if (!cs) return;
    memset(cs, 0, sizeof(HwCounterSet));
}

void hw_counter_set_compute_derived(HwCounterSet *cs) {
    if (!cs) return;
    
    /* IPC = instructions / cycles */
    if (cs->cpu_cycles > 0) {
        cs->ipc = (double)cs->instructions_retired / (double)cs->cpu_cycles;
    }
    
    /* Branch miss rate */
    if (cs->branch_instructions > 0) {
        cs->branch_miss_rate = (double)cs->branch_mispredictions /
                               (double)cs->branch_instructions;
    }
    
    /* Cache miss rates at each level */
    uint64_t l1_total = cs->cache_l1_hits + cs->cache_l1_misses;
    if (l1_total > 0) {
        cs->l1_miss_rate = (double)cs->cache_l1_misses / (double)l1_total;
    }
    
    uint64_t l2_total = cs->cache_l2_hits + cs->cache_l2_misses;
    if (l2_total > 0) {
        cs->l2_miss_rate = (double)cs->cache_l2_misses / (double)l2_total;
    }
    
    uint64_t l3_total = cs->cache_l3_hits + cs->cache_l3_misses;
    if (l3_total > 0) {
        cs->l3_miss_rate = (double)cs->cache_l3_misses / (double)l3_total;
    }
    
    uint64_t tlb_total = cs->tlb_hits + cs->tlb_misses;
    if (tlb_total > 0) {
        cs->tlb_miss_rate = (double)cs->tlb_misses / (double)tlb_total;
    }
}

/* ============================================================================
 * LakeWorkloadProfile Factory Functions
 * ============================================================================ */

void lake_workload_profile_init_tpch(LakeWorkloadProfile *p) {
    if (!p) return;
    memset(p, 0, sizeof(LakeWorkloadProfile));
    strncpy(p->profile_name, "TPC-H (Decision Support)", sizeof(p->profile_name) - 1);
    /* TPC-H: mostly sequential scans with some joins */
    p->scan_fraction = 0.65;
    p->point_query_fraction = 0.05;
    p->join_fraction = 0.20;
    p->aggregation_fraction = 0.10;
    p->data_size_gb = 100.0;
    p->expected_qps = 10;
    p->slo_latency_ms = 5000.0;
    p->dominant_io = IO_SEQUENTIAL;
    p->dominant_mem = MEM_LINEAR;
}

void lake_workload_profile_init_tpcds(LakeWorkloadProfile *p) {
    if (!p) return;
    memset(p, 0, sizeof(LakeWorkloadProfile));
    strncpy(p->profile_name, "TPC-DS (Complex Analytics)", sizeof(p->profile_name) - 1);
    /* TPC-DS: more complex, more joins and aggregations */
    p->scan_fraction = 0.50;
    p->point_query_fraction = 0.10;
    p->join_fraction = 0.25;
    p->aggregation_fraction = 0.15;
    p->data_size_gb = 500.0;
    p->expected_qps = 5;
    p->slo_latency_ms = 10000.0;
    p->dominant_io = IO_MIXED;
    p->dominant_mem = MEM_STRIDED_8;
}

void lake_workload_profile_init_log_analytics(LakeWorkloadProfile *p) {
    if (!p) return;
    memset(p, 0, sizeof(LakeWorkloadProfile));
    strncpy(p->profile_name, "Log Analytics (Append-heavy)", sizeof(p->profile_name) - 1);
    /* Log analytics: mostly sequential scans of recent data, some point queries */
    p->scan_fraction = 0.75;
    p->point_query_fraction = 0.15;
    p->join_fraction = 0.02;
    p->aggregation_fraction = 0.08;
    p->data_size_gb = 1000.0;
    p->expected_qps = 50;
    p->slo_latency_ms = 2000.0;
    p->dominant_io = IO_SEQUENTIAL;
    p->dominant_mem = MEM_LINEAR;
}