/**
 * demo_lake_workload.c — Data Lake Workload Analysis Demo
 *
 * L7: Application — Demonstrates how hardware testing informs data lake
 *     query engine configuration. Generates TPC-H workload, analyzes
 *     access patterns, shows scalability predictions, and outputs
 *     hardware recommendations for the lakehouse.
 *
 * Cross-module integration: Feeds data to module 7 (data-engine).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lake_test_core.h"
#include "workload_gen.h"
#include "perf_model.h"
#include "result_analyze.h"
#include "mem_bandwidth.h"
#include "io_profile.h"

int main(void) {
    printf("=== Data Lake Workload Analysis Demo ===\n\n");
    
    /* Step 1: Define the workload */
    WorkloadConfig cfg;
    workload_config_init_tpch(&cfg);
    printf("Workload: %s\n", cfg.workload_name);
    printf("Tables: %lu, Data: %lu GB, Queries: %lu\n\n",
           (unsigned long)cfg.num_tables,
           (unsigned long)cfg.total_data_gb,
           (unsigned long)cfg.num_queries);
    
    /* Step 2: Generate synthetic workload */
    GeneratedWorkload *wl = workload_generate(&cfg);
    if (!wl) {
        printf("ERROR: Failed to generate workload!\n");
        return 1;
    }
    workload_print_summary(wl);
    
    /* Step 3: Analyze scalability using Amdahl's Law */
    printf("\n--- Scalability Analysis ---\n");
    printf("Amdahl's Law — assuming 85%% parallelizable:\n");
    for (uint32_t n = 1; n <= 64; n *= 2) {
        double speedup = amdahl_speedup(0.85, n);
        printf("  %2u cores: %.2fx speedup (efficiency: %.1f%%)\n",
               n, speedup, amdahl_efficiency(0.85, n) * 100.0);
    }
    
    /* Step 4: Apply Little's Law */
    double target_qps = 20.0;
    double avg_query_ms = 500.0;
    uint32_t concurrency = workload_littles_law_concurrency(target_qps, avg_query_ms);
    printf("\nLittle's Law: For %.0f QPS with %.0f ms avg query time\n",
           target_qps, avg_query_ms);
    printf("  Required concurrency: %u\n", concurrency);
    
    /* Step 5: Memory bandwidth analysis for scan-heavy workload */
    printf("\n--- Memory Bandwidth Requirement ---\n");
    BwBenchResult *bw = bw_run_complete_test(64 * 1024 * 1024, 5);
    if (bw) {
        printf("Measured sustained BW: %.1f MB/s\n", bw->sustained_bw_mbps);
        printf("Peak BW: %.1f MB/s\n", bw->peak_bw_mbps);
        
        /* For TPC-H scan-heavy workload, estimate required BW */
        double io_per_query_gb = 1.0; /* ~1GB per query */
        double required_bw_mbps = target_qps * io_per_query_gb * 1024.0;
        printf("Required BW for %.0f QPS: %.1f MB/s\n",
               target_qps, required_bw_mbps);
        printf("Adequacy: %s\n",
               bw->peak_bw_mbps >= required_bw_mbps ? "SUFFICIENT" : "INSUFFICIENT");
        bw_bench_result_destroy(bw);
    }
    
    /* Step 6: I/O queue depth analysis */
    printf("\n--- I/O Configuration ---\n");
    IoBenchResult *io = io_run_queue_depth_test(32, 4096, IO_SEQUENTIAL, 100ULL * 1024 * 1024 * 1024);
    if (io) {
        printf("Optimal I/O queue depth: %u\n", io->optimal_queue_depth);
        printf("Peak IOPS: %lu at QD=%u\n",
               (unsigned long)io->peak_iops, io->optimal_queue_depth);
        printf("Saturation point: QD=%.0f\n", io->saturation_point);
        io_bench_result_destroy(io);
    }
    
    /* Step 7: Generate hardware recommendations */
    printf("\n--- Hardware Recommendations ---\n");
    BenchResult results[5];
    for (int i = 0; i < 5; i++) {
        bench_result_init(&results[i]);
        results[i].is_valid = true;
        results[i].throughput_ops_per_sec = 22.0 + i * 3.0;
        results[i].avg_latency_ns = 45000.0 - i * 2000.0;
    }
    
    char rec[512];
    analyze_recommend_lake_config(results, 5, target_qps, rec, sizeof(rec));
    printf("%s\n", rec);
    
    /* Step 8: Show access pattern statistics */
    printf("\n--- Access Pattern Analysis ---\n");
    AccessSequence *seq = workload_gen_zipf_sequence(100000, 500000, 1.2, 42);
    if (seq) {
        printf("Zipf(alpha=1.2) sequence: %lu keys\n", (unsigned long)seq->num_keys);
        printf("  Entropy: %.2f bits (uniform would be ~%.2f bits)\n",
               seq->actual_entropy, log2(100000.0));
        printf("  Gini coefficient: %.4f (0=uniform, 1=max skew)\n", seq->actual_skew);
        workload_gen_seq_destroy(seq);
    }
    
    workload_destroy(wl);
    
    printf("\n=== Demo complete ===\n");
    return 0;
}