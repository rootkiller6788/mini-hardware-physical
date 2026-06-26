/**
 * demo_cache_bench.c — Cache Benchmark Demo
 *
 * Demonstrates the cache bench module: runs a cache miss curve across
 * multiple working set sizes and shows how hit rate degrades.
 * L6: Canonical problem — Cache performance characterization.
 */

#include <stdio.h>
#include <stdlib.h>
#include "cache_bench.h"
#include "lake_test_core.h"

int main(void) {
    printf("=== Cache Benchmark Demo ===\n\n");
    
    /* Show platform info first */
    PlatformInfo platform;
    platform_info_init(&platform);
    platform_info_print(&platform);
    
    /* Configure and run cache benchmark */
    CacheBenchConfig cfg;
    cache_bench_config_init(&cfg);
    
    /* Customize for a small, focused test */
    cfg.working_set_min = 16 * 1024;      /* 16 KB */
    cfg.working_set_max = 8 * 1024 * 1024; /* 8 MB */
    cfg.working_set_step = 4;             /* 4x steps */
    cfg.measure_inclusive = true;
    
    printf("\nRunning cache miss curve benchmark...\n");
    printf("Working set range: %lu KB to %lu MB\n",
           (unsigned long)(cfg.working_set_min / 1024),
           (unsigned long)(cfg.working_set_max / (1024 * 1024)));
    
    CacheBenchResult *result = cache_bench_run(&cfg);
    if (!result) {
        printf("ERROR: Benchmark failed!\n");
        return 1;
    }
    
    cache_bench_result_print(result);
    
    /* Fit power law to miss rate curve */
    double r_squared = 0.0;
    double alpha = cache_fit_power_law_exponent(result, &r_squared);
    printf("\nCache Miss Rate Power Law: MR ~ C^(-%.3f), R^2=%.4f\n",
           alpha, r_squared);
    printf("(Higher alpha = better locality, easier to cache)\n");
    
    /* Demonstrate AMAT calculation */
    double amat = cache_compute_amat(1.0, 0.05, 100.0);
    printf("\nExample AMAT: 1.0ns hit + 0.05 miss_rate * 100ns penalty = %.1f ns\n", amat);
    
    cache_bench_result_destroy(result);
    
    printf("\n=== Demo complete ===\n");
    return 0;
}