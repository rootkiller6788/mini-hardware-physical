/* ============================================================================
 * accelerator_roofline.c — Roofline Model & Accelerator Efficiency Analysis
 *
 * L4: Standards/Theorems
 *   § Roofline Model (Williams, Patterson, Waterman, CACM 2009)
 *     Performance is bounded by min(peak_compute, op_intensity × peak_bandwidth)
 *   § Amdahl's Law: S(N) = 1 / (s + (1-s)/N)
 *   § Gustafson's Law: S(N) = N - s·(N-1)
 *   § MAC utilization: U = (2·M·N·K) / (2·array_size·cycles)
 *
 * L8: Advanced Topics — multi-platform comparative analysis
 * ========================================================================== */

#include "accelerator_roofline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ==========================================================================
 * L4: Roofline Model — Platform Presets
 * Data from: TPUv1-4 ISCA papers, Eyeriss JSSC 2017
 * ========================================================================== */

RooflinePlatform roofline_platform_preset(AcceleratorPlatform platform) {
    RooflinePlatform p;
    memset(&p, 0, sizeof(p));
    p.platform = platform;

    switch (platform) {
        case PLATFORM_TPU_V1:
            p.name                  = "TPUv1";
            p.peak_compute_tflops   = 92.0;    /* INT8 TOPS */
            p.peak_bandwidth_gbps   = 34.0;    /* DDR3-2133 × 2ch */
            p.sram_bandwidth_gbps   = 3584.0;  /* 28 MB × 128B/cycle × 700MHz */
            p.systolic_array_size   = 256;
            p.frequency_ghz         = 0.700;
            p.on_chip_sram_mb       = 24.0;
            p.ridge_point           = 92.0 / 34.0;
            break;
        case PLATFORM_TPU_V2:
            p.name                  = "TPUv2";
            p.peak_compute_tflops   = 45.0;    /* BF16 TFLOPS per chip */
            p.peak_bandwidth_gbps   = 600.0;   /* HBM2 */
            p.sram_bandwidth_gbps   = 8192.0;
            p.systolic_array_size   = 128;
            p.frequency_ghz         = 0.700;
            p.on_chip_sram_mb       = 16.0;
            p.ridge_point           = 45.0 / 600.0;
            break;
        case PLATFORM_TPU_V3:
            p.name                  = "TPUv3";
            p.peak_compute_tflops   = 123.0;
            p.peak_bandwidth_gbps   = 900.0;
            p.sram_bandwidth_gbps   = 12288.0;
            p.systolic_array_size   = 128;
            p.frequency_ghz         = 0.940;
            p.on_chip_sram_mb       = 32.0;
            p.ridge_point           = 123.0 / 900.0;
            break;
        case PLATFORM_TPU_V4:
            p.name                  = "TPUv4";
            p.peak_compute_tflops   = 275.0;
            p.peak_bandwidth_gbps   = 1200.0;
            p.sram_bandwidth_gbps   = 16384.0;
            p.systolic_array_size   = 128;
            p.frequency_ghz         = 1.050;
            p.on_chip_sram_mb       = 128.0;
            p.ridge_point           = 275.0 / 1200.0;
            break;
        case PLATFORM_EYERISS_V2:
            p.name                  = "Eyeriss v2";
            p.peak_compute_tflops   = 0.102;   /* 102 GFLOPS (FP16) */
            p.peak_bandwidth_gbps   = 34.133;  /* 256b DDR4-2133 */
            p.sram_bandwidth_gbps   = 256.0;
            p.systolic_array_size   = 16;
            p.frequency_ghz         = 0.250;
            p.on_chip_sram_mb       = 0.384;   /* 384 KB */
            p.ridge_point           = 0.102 / 34.133;
            break;
        case PLATFORM_CUSTOM:
        default:
            p.name                  = "Custom";
            p.peak_compute_tflops   = 1.0;
            p.peak_bandwidth_gbps   = 100.0;
            p.sram_bandwidth_gbps   = 1000.0;
            p.systolic_array_size   = 64;
            p.frequency_ghz         = 1.0;
            p.on_chip_sram_mb       = 8.0;
            p.ridge_point           = 1.0 / 100.0;
            break;
    }
    return p;
}

RooflinePlatform roofline_custom_platform(double peak_tflops, double bw_gbps,
                                           double freq_ghz, int array_size,
                                           double sram_mb) {
    RooflinePlatform p;
    memset(&p, 0, sizeof(p));
    p.platform           = PLATFORM_CUSTOM;
    p.name               = "Custom";
    p.peak_compute_tflops = peak_tflops;
    p.peak_bandwidth_gbps = bw_gbps;
    p.sram_bandwidth_gbps = bw_gbps * 10.0; /* estimate 10× on-chip bandwidth */
    p.systolic_array_size = array_size;
    p.frequency_ghz       = freq_ghz;
    p.on_chip_sram_mb     = sram_mb;
    p.ridge_point         = peak_tflops / (bw_gbps > 0 ? bw_gbps : 1.0);
    return p;
}

/* ==========================================================================
 * L4: Roofline Model — Operational Intensity
 *
 * Core formula: OpIntensity = FLOPs / Bytes_Accessed
 * If OpIntensity < RidgePoint → memory-bound
 * If OpIntensity ≥ RidgePoint → compute-bound
 * ========================================================================== */

OpIntensityReport roofline_compute_operational_intensity(double flops,
                                                          double bytes_r,
                                                          double bytes_w) {
    OpIntensityReport report;
    memset(&report, 0, sizeof(report));
    report.flops   = flops;
    report.bytes_read    = bytes_r;
    report.bytes_written = bytes_w;
    double total_bytes = bytes_r + bytes_w;
    if (total_bytes < 1.0) total_bytes = 1.0;
    report.operational_intensity = flops / total_bytes;
    report.bound_type = "unclassified";
    return report;
}

OpIntensityReport roofline_layer_intensity(RooflineLayer *layer) {
    OpIntensityReport report;
    memset(&report, 0, sizeof(report));
    if (!layer) return report;

    /* FLOPs for matmul: 2 * M * N * K (per sample) */
    double matmul_flops = 2.0 * layer->M * layer->N * layer->K;
    double total_flops  = matmul_flops * layer->batch_size;

    /* Data movement: weights + activations
     * Weight stationary: each weight read once, reused batch_size × output_spatial times
     */
    double data_moved  = layer->weights_bytes
                       + layer->activations_bytes * layer->batch_size;
    if (data_moved < 1.0) data_moved = 1.0;

    report.flops      = total_flops;
    report.bytes_read = data_moved;
    report.bytes_written = layer->M * layer->N * sizeof(float) * layer->batch_size;
    report.operational_intensity = total_flops / data_moved;
    layer->operational_intensity = report.operational_intensity;

    return report;
}

const char *roofline_bound_classify(double op_intensity, double ridge_point) {
    if (ridge_point <= 0.0) return "unknown";
    if (op_intensity >= ridge_point) return "compute-bound";
    return "memory-bound";
}

double roofline_attainable_performance(double op_intensity, RooflinePlatform *plat) {
    if (!plat) return 0.0;
    /* Roofline formula: P_attainable = min(peak_compute, op_intensity × peak_bandwidth) */
    double mem_bound_perf = op_intensity * plat->peak_bandwidth_gbps;
    /* Convert GB/s × FLOP/byte → TFLOPs (1e3) */
    double mem_bound_tflops = mem_bound_perf / 1000.0;
    if (mem_bound_tflops < plat->peak_compute_tflops) {
        return mem_bound_tflops;
    }
    return plat->peak_compute_tflops;
}

double roofline_utilization(double attained_tflops, double peak_tflops) {
    if (peak_tflops <= 0.0) return 0.0;
    double util = attained_tflops / peak_tflops;
    if (util > 1.0) util = 1.0;
    return util;
}

/* ==========================================================================
 * L4: Multi-layer roofline analysis
 * ========================================================================== */

void roofline_analyze_layers(RooflineLayer *layers, int num_layers,
                              RooflinePlatform *plat) {
    if (!layers || !plat || num_layers <= 0) return;

    printf("\n");
    printf("==========================================================================================\n");
    printf("  Roofline Analysis for Platform: %s (Ridge: %.3f FLOP/byte)\n",
           plat->name, plat->ridge_point);
    printf("  Peak Compute: %.1f TFLOPS | Peak BW: %.1f GB/s\n",
           plat->peak_compute_tflops, plat->peak_bandwidth_gbps);
    printf("==========================================================================================\n");
    printf("  %-20s %8s %8s %8s %14s %12s %16s %10s\n",
           "Layer", "M", "N", "K", "OpIntensity", "Attain.TFLOPS", "Bound", "Util%%");
    printf("  %-20s %8s %8s %8s %14s %12s %16s %10s\n",
           "--------------------", "--------", "--------", "--------",
           "--------------", "------------", "----------------", "----------");

    for (int i = 0; i < num_layers; i++) {
        OpIntensityReport rpt = roofline_layer_intensity(&layers[i]);
        double attainable = roofline_attainable_performance(rpt.operational_intensity, plat);
        const char *bound = roofline_bound_classify(rpt.operational_intensity, plat->ridge_point);
        double util = roofline_utilization(attainable, plat->peak_compute_tflops) * 100.0;
        printf("  %-20s %8d %8d %8d %14.3f %12.3f %16s %9.1f%%\n",
               layers[i].name, layers[i].M, layers[i].N, layers[i].K,
               rpt.operational_intensity, attainable, bound, util);
    }
    printf("==========================================================================================\n");
}

void roofline_compare_platforms(RooflineLayer *layer,
                                 RooflinePlatform *plats, int num_plats) {
    if (!layer || !plats || num_plats <= 0) return;
    OpIntensityReport rpt = roofline_layer_intensity(layer);

    printf("\n");
    printf("================================================================================\n");
    printf("  Platform Comparison for Layer: %s (OpIntensity: %.3f FLOP/byte)\n",
           layer->name, rpt.operational_intensity);
    printf("================================================================================\n");
    printf("  %-20s %10s %10s %14s %12s\n",
           "Platform", "Ridge", "Peak TFLOPs", "Attain. TFLOPs", "Bound");
    printf("  %-20s %10s %10s %14s %12s\n",
           "--------------------", "----------", "----------", "--------------", "------------");

    for (int i = 0; i < num_plats; i++) {
        double attainable = roofline_attainable_performance(rpt.operational_intensity, &plats[i]);
        const char *bound = roofline_bound_classify(rpt.operational_intensity, plats[i].ridge_point);
        printf("  %-20s %10.3f %10.1f %14.3f %12s\n",
               plats[i].name, plats[i].ridge_point, plats[i].peak_compute_tflops,
               attainable, bound);
    }
    printf("================================================================================\n");
}

/* ==========================================================================
 * L4: Amdahl's Law — S(N) = 1 / (s + p/N)
 *
 * s = serial fraction (cannot be parallelized)
 * p = parallel fraction = 1 - s
 * N = number of accelerators
 * Speedup S(N) = 1 / (s + (1-s)/N)
 * Efficiency = S(N) / N
 *
 * Gustafson: S(N) = N - s·(N-1)  (scaled problem size)
 * ========================================================================== */

AmdahlReport amdahl_analyze(int num_accelerators, double serial_fraction) {
    AmdahlReport report;
    memset(&report, 0, sizeof(report));

    report.num_accelerators  = num_accelerators;
    report.serial_fraction   = serial_fraction;
    report.parallel_fraction = 1.0 - serial_fraction;

    /* Amdahl's Law */
    if (num_accelerators > 0) {
        double denom = serial_fraction + report.parallel_fraction / (double)num_accelerators;
        if (denom < 1e-12) denom = 1e-12;
        report.speedup   = 1.0 / denom;
        report.efficiency = report.speedup / (double)num_accelerators;

        /* Gustafson's Law: S = N - s·(N-1) */
        report.scaled_speedup = num_accelerators
                                - serial_fraction * (num_accelerators - 1);
    } else {
        report.speedup         = 1.0;
        report.efficiency      = 1.0;
        report.scaled_speedup  = 1.0;
    }

    return report;
}

double amdahl_gustafson_speedup(int num_accelerators, double parallel_fraction) {
    if (num_accelerators <= 0) return 1.0;
    /* Gustafson: assume problem scales with N */
    return num_accelerators - (1.0 - parallel_fraction) * (num_accelerators - 1.0);
}

void amdahl_print_report(AmdahlReport *report) {
    if (!report) return;
    printf("\n===============================================================\n");
    printf("  Amdahl's Law Analysis\n");
    printf("===============================================================\n");
    printf("  Number of accelerators:  %d\n", report->num_accelerators);
    printf("  Serial fraction:         %.4f (%.1f%%)\n",
           report->serial_fraction, report->serial_fraction * 100.0);
    printf("  Parallel fraction:       %.4f (%.1f%%)\n",
           report->parallel_fraction, report->parallel_fraction * 100.0);
    printf("  -----------------------------------------------------------\n");
    printf("  Amdahl speedup:          %.4f×\n", report->speedup);
    printf("  Efficiency:              %.2f%%\n", report->efficiency * 100.0);
    printf("  Gustafson speedup:       %.4f×\n", report->scaled_speedup);
    printf("  -----------------------------------------------------------\n");
    printf("  Max possible speedup:    1/s = %.4f× (as N→∞)\n",
           1.0 / (report->serial_fraction > 0 ? report->serial_fraction : 0.01));
    printf("===============================================================\n");
}

/* ==========================================================================
 * L4: Memory Hierarchy Energy Model
 *
 * Energy costs based on Horowitz (ISSCC 2014) and TPU ISCA 2017:
 *   Register:     ~0.0 pJ/access (free in pipeline)
 *   Local SRAM:   ~1.0 pJ/access (MAC-local scratchpad)
 *   Global SRAM:  ~5.0 pJ/access (unified buffer)
 *   HBM:          ~20.0 pJ/access (stacked DRAM)
 *   DRAM:         ~640.0 pJ/access (off-chip DDR/GDDR)
 * ========================================================================== */

double memory_energy_cost(MemoryLevel level) {
    switch (level) {
        case MEM_LEVEL_REGISTER:    return 0.0;
        case MEM_LEVEL_LOCAL_SRAM:  return 1.0;
        case MEM_LEVEL_GLOBAL_SRAM: return 5.0;
        case MEM_LEVEL_DRAM:        return 640.0;
        case MEM_LEVEL_HBM:         return 20.0;
        default:                    return 0.0;
    }
}

double memory_hierarchy_energy(int num_reg, int num_local_sram,
                                int num_global_sram, int num_dram, int num_hbm) {
    return (double)num_reg        * memory_energy_cost(MEM_LEVEL_REGISTER)
         + (double)num_local_sram * memory_energy_cost(MEM_LEVEL_LOCAL_SRAM)
         + (double)num_global_sram * memory_energy_cost(MEM_LEVEL_GLOBAL_SRAM)
         + (double)num_dram       * memory_energy_cost(MEM_LEVEL_DRAM)
         + (double)num_hbm        * memory_energy_cost(MEM_LEVEL_HBM);
}

double memory_access_energy_breakdown(int reads[], int writes[], int num_levels) {
    double total = 0.0;
    static const double cost_table[] = {0.0, 1.0, 5.0, 640.0, 20.0};
    int max_levels = 5;
    if (num_levels > max_levels) num_levels = max_levels;
    for (int i = 0; i < num_levels; i++) {
        total += (double)(reads[i] + writes[i]) * cost_table[i];
    }
    return total;
}

/* ==========================================================================
 * L4: MAC Utilization Analysis
 *
 * Theoretical peak MAC utilization:
 *   U = (2 × M × N × K) / (2 × array_size × cycles_used)
 *
 * Pipeline bubbles come from:
 *   - Data skew in systolic array (M+N+K-2 cycles minimum latency)
 *   - Memory stalls
 *   - Control overhead
 * ========================================================================== */

double mac_utilization_theoretical(int M, int N, int K, int array_size, int cycles) {
    if (array_size <= 0 || cycles <= 0) return 0.0;
    double total_mac_ops = 2.0 * M * N * K;  /* multiply+add per output element */
    double peak_mac_capacity = 2.0 * array_size * cycles; /* 2 ops per PE per cycle */
    if (peak_mac_capacity <= 0.0) return 0.0;
    double utilization = total_mac_ops / peak_mac_capacity;
    if (utilization > 1.0) utilization = 1.0;
    return utilization;
}

double mac_utilization_with_bubbles(int M, int N, int K, int array_size,
                                     int pipeline_bubbles, double frequency_ghz) {
    if (array_size <= 0 || frequency_ghz <= 0.0) return 0.0;
    int minimum_cycles = M + N + K - 2;
    int total_cycles   = minimum_cycles + pipeline_bubbles;
    return mac_utilization_theoretical(M, N, K, array_size, total_cycles);
}
