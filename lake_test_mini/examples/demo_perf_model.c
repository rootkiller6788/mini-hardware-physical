/**
 * demo_perf_model.c — Performance Modeling Demo
 *
 * L4/L8: Demonstrates Roofline Model, Amdahl's Law, Gustafson's Law,
 *        Universal Scalability Law, and ML-based performance prediction.
 *        Shows how analytical models bound hardware performance and how
 *        regression on counter data can predict query latency.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "lake_test_core.h"
#include "perf_model.h"
#include "mem_bandwidth.h"
#include "result_analyze.h"

int main(void) {
    printf("=== Performance Modeling Demo ===\n\n");
    
    /* ============================================================
     * Part 1: Roofline Model
     * ============================================================ */
    printf("--- Roofline Model ---\n");
    
    PlatformInfo platform;
    platform_info_init(&platform);
    
    RooflineParams params;
    roofline_params_from_platform(&params, &platform);
    printf("Machine: %.0f GFLOP/s peak, %.1f GB/s peak BW\n",
           params.peak_gflops, params.peak_bandwidth_gbps);
    
    double ridge = roofline_ridge(&params);
    printf("Ridge point: %.2f FLOP/Byte\n", ridge);
    
    /* Analyze some workload points */
    RooflineAnalysis *analysis = (RooflineAnalysis *)calloc(1, sizeof(RooflineAnalysis));
    analysis->machine = params;
    analysis->ridge_point = ridge;
    
    /* Add various points at different arithmetic intensities */
    double test_points[][2] = {
        {0.5, 40.0},   /* Low AI — memory bound */
        {1.0, 80.0},   /* Near ridge */
        {5.0, 200.0},  /* Above ridge — compute bound */
        {10.0, 800.0}, /* High AI */
        {20.0, 1400.0} /* Very high AI */
    };
    
    for (int i = 0; i < 5; i++) {
        double ai = test_points[i][0];
        double gflops = test_points[i][1];
        double bw_used = gflops / ai;
        roofline_add_point(analysis, ai, gflops, bw_used);
    }
    
    roofline_print(analysis);
    roofline_destroy(analysis);
    
    /* ============================================================
     * Part 2: Scalability Laws
     * ============================================================ */
    printf("\n--- Scalability Laws Comparison ---\n\n");
    
    double parallel_fraction = 0.9;
    printf("With %.0f%% serial code:\n", (1.0 - parallel_fraction) * 100.0);
    printf("%-10s %-18s %-18s %s\n", "Cores", "Amdahl", "Gustafson", "USL(s=0.02,k=0.005)");
    printf("--------------------------------------------------------------\n");
    
    for (uint32_t n = 1; n <= 64; n *= 2) {
        double a = amdahl_speedup(parallel_fraction, n);
        double g = gustafson_speedup(1.0 - parallel_fraction, n);
        double u = usl_throughput(0.02, 0.005, n);
        printf("%-10u %-18.2f %-18.2f %.2f\n", n, a, g, u);
    }
    
    /* Universal Scalability Law analysis */
    printf("\n--- Universal Scalability Law ---\n");
    uint32_t n_opt = usl_optimal_processors(0.02, 0.005);
    printf("Optimal processor count: %u\n", n_opt);
    printf("At %u processors, contention + coherency costs overwhelm gains.\n", n_opt);
    
    double tp_nopt = usl_throughput(0.02, 0.005, n_opt);
    printf("Peak throughput: %.2fx speedup\n", tp_nopt);
    
    /* ============================================================
     * Part 3: Performance Prediction with Hardware Counters
     * ============================================================ */
    printf("\n--- ML-Based Performance Prediction ---\n");
    
    /* Generate synthetic training data */
    PerfSample samples[15];
    for (int i = 0; i < 15; i++) {
        memset(&samples[i], 0, sizeof(PerfSample));
        samples[i].ipc = 0.5 + (double)i * 0.1;
        samples[i].l2_miss_rate = 0.30 - (double)i * 0.02;
        samples[i].l3_miss_rate = 0.15 - (double)i * 0.01;
        samples[i].branch_miss_rate = 0.02;
        samples[i].tlb_miss_rate = 0.001;
        samples[i].bw_utilization = 0.3 + (double)i * 0.03;
        /* Simulated latency: decreases with better IPC, increases with misses */
        samples[i].measured_latency_ms = 50.0 - samples[i].ipc * 10.0 +
                                          samples[i].l2_miss_rate * 100.0 +
                                          samples[i].bw_utilization * 5.0;
    }
    
    PerfPredictor predictor;
    perf_predictor_init(&predictor);
    
    bool trained = perf_predictor_train(&predictor, samples, 15);
    if (trained) {
        perf_predictor_print(&predictor);
        
        /* Predict for a new sample */
        PerfSample new_sample;
        memset(&new_sample, 0, sizeof(new_sample));
        new_sample.ipc = 2.0;
        new_sample.l2_miss_rate = 0.05;
        new_sample.l3_miss_rate = 0.02;
        new_sample.bw_utilization = 0.5;
        
        double predicted = perf_predictor_predict(&predictor, &new_sample);
        printf("\nPredicted latency for IPC=%.1f, L2_miss=%.2f: %.2f ms\n",
               new_sample.ipc, new_sample.l2_miss_rate, predicted);
    } else {
        printf("Training failed (need more data)\n");
    }
    
    /* ============================================================
     * Part 4: Saturation Curve Fitting
     * ============================================================ */
    printf("\n--- Saturation Curve Fitting ---\n");
    
    double qd[] = {1, 2, 4, 8, 16, 32};
    double iops[] = {120000, 220000, 380000, 520000, 580000, 595000};
    FittedCurve curve = saturation_curve_fit(qd, iops, 6, SAT_EXPONENTIAL);
    
    saturation_curve_print(&curve);
    
    /* Show predictions */
    printf("\nCurve predictions:\n");
    for (int i = 0; i < 6; i++) {
        printf("  QD=%2.0f: actual=%8.0f, predicted=%8.0f IOPS\n",
               qd[i], iops[i], saturation_curve_eval(&curve, qd[i]));
    }
    
    /* ============================================================
     * Part 5: Queueing Theory Analysis
     * ============================================================ */
    printf("\n--- Queueing Theory ---\n");
    
    /* M/D/1: I/O subsystem modeling */
    double io_latency = queueing_md1_latency(50000.0, 10e-6); /* 50000 IOPS, 10us service */
    printf("M/D/1 I/O latency (50000 IOPS, 10us service): %.2f us\n",
           io_latency * 1e6);
    
    /* M/M/1: CPU queue modeling */
    double cpu_rt = queueing_mm1_response_time(500.0, 1000.0); /* lambda=500, mu=1000 */
    printf("M/M/1 CPU response time (lambda=500, mu=1000): %.4f sec\n", cpu_rt);
    
    printf("\n=== Demo complete ===\n");
    return 0;
}