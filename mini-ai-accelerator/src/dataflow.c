#include "dataflow.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

const char *dataflow_type_name(DataflowType type) {
    switch (type) {
        case WEIGHT_STATIONARY: return "Weight Stationary";
        case OUTPUT_STATIONARY: return "Output Stationary";
        case INPUT_STATIONARY:  return "Input Stationary";
        case ROW_STATIONARY:    return "Row Stationary (Eyeriss)";
        default: return "Unknown";
    }
}

double dataflow_energy_estimate(DataflowType type, int M, int N, int K, int array_w, int array_h) {
    int total_macs = M * N * K;
    double mac_energy = total_macs * MAC_ENERGY_PJ;

    double sram_reads = 0.0;
    double dram_reads = 0.0;

    (void)array_w;
    (void)array_h;

    switch (type) {
        case WEIGHT_STATIONARY:
            dram_reads = (double)(M * K + K * N);
            sram_reads = (double)(total_macs * 2);
            break;
        case OUTPUT_STATIONARY:
            dram_reads = (double)(M * K + K * N + M * N);
            sram_reads = (double)(total_macs * 2);
            break;
        case INPUT_STATIONARY:
            dram_reads = (double)(M * K);
            sram_reads = (double)(total_macs * 2 + K * N);
            break;
        case ROW_STATIONARY: {
            int folds = (M + array_h - 1) / array_h;
            dram_reads = (double)(M * K + folds * K * N);
            sram_reads = (double)(total_macs * 3);
            break;
        }
    }

    double total_energy = mac_energy + sram_reads * SRAM_READ_ENERGY_PJ + dram_reads * DRAM_READ_ENERGY_PJ;
    return total_energy;
}

double dataflow_timing_estimate(DataflowType type, int M, int N, int K, int array_w, int array_h) {
    int array_size = array_w * array_h;
    double cycles = 0.0;

    switch (type) {
        case WEIGHT_STATIONARY:
            cycles = (double)(M * N * K) / (double)array_size + (double)(M + N);
            break;
        case OUTPUT_STATIONARY:
            cycles = (double)(M * N * K) / (double)array_size + (double)(M + N + K);
            break;
        case INPUT_STATIONARY:
            cycles = (double)(M * N * K) / (double)array_size + (double)(N);
            break;
        case ROW_STATIONARY: {
            int folds = (M + array_h - 1) / array_h;
            cycles = (double)(M * N * K) / (double)array_size + (double)(folds * N);
            break;
        }
    }
    return cycles;
}

void dataflow_compare_all(int M, int N, int K, int array_w, int array_h) {
    printf("\n");
    printf("================================================================================\n");
    printf("  Dataflow Comparison for Layer: M=%d N=%d K=%d  (Array: %dx%d)\n", M, N, K, array_w, array_h);
    printf("================================================================================\n");
    printf("  %-30s %20s %20s\n", "Dataflow Type", "Energy (pJ)", "Cycles (est.)");
    printf("  %-30s %20s %20s\n", "------------------------------", "--------------------", "--------------------");

    DataflowType types[] = {WEIGHT_STATIONARY, OUTPUT_STATIONARY, INPUT_STATIONARY, ROW_STATIONARY};
    int best_energy_idx = 0;
    int best_cycles_idx = 0;
    double best_energy = 1e18;
    double best_cycles = 1e18;
    double energies[4];
    double cycles_vals[4];

    for (int i = 0; i < 4; i++) {
        energies[i] = dataflow_energy_estimate(types[i], M, N, K, array_w, array_h);
        cycles_vals[i] = dataflow_timing_estimate(types[i], M, N, K, array_w, array_h);
        if (energies[i] < best_energy) { best_energy = energies[i]; best_energy_idx = i; }
        if (cycles_vals[i] < best_cycles) { best_cycles = cycles_vals[i]; best_cycles_idx = i; }
        printf("  %-30s %18.2f pJ %18.0f\n", dataflow_type_name(types[i]), energies[i], cycles_vals[i]);
    }

    printf("  %-30s %20s %20s\n", "------------------------------", "--------------------", "--------------------");
    printf("  Best energy: %s (%.2f pJ)\n", dataflow_type_name(types[best_energy_idx]), energies[best_energy_idx]);
    printf("  Best latency: %s (%.0f cycles)\n", dataflow_type_name(types[best_cycles_idx]), cycles_vals[best_cycles_idx]);
    printf("================================================================================\n");
}

/* ==========================================================================
 * L4: MAC Utilization & Throughput Estimation
 *
 * Utilization = total_useful_MACs / total_available_MAC_cycles
 *
 * Different dataflows have different utilization efficiency:
 * - Weight stationary: high utilization (weights preloaded, reused)
 * - Output stationary: medium (partial sums accumulate, need to be stored)
 * - Input stationary: low-medium (input stays, but weights streamed)
 * - Row stationary: highest (Eyeriss maximizes all reuse levels)
 * ========================================================================== */

double dataflow_mac_utilization(DataflowType type, int M, int N, int K,
                                 int array_w, int array_h) {
    int array_size = array_w * array_h;
    if (array_size <= 0) return 0.0;

    double total_macs = (double)(M * N * K);
    double cycles     = dataflow_timing_estimate(type, M, N, K, array_w, array_h);
    double peak_macs  = (double)array_size * cycles;

    if (peak_macs <= 0.0) return 0.0;

    /* Each PE does 2 ops per cycle (multiply+add) when fully utilized */
    double peak_capacity = 2.0 * peak_macs;
    double utilization = (2.0 * total_macs) / peak_capacity;

    if (utilization > 1.0) utilization = 1.0;
    return utilization;
}

/* ==========================================================================
 * L3: Memory Hierarchy Energy Breakdown (per dataflow)
 *
 * Breaks down energy by memory level for each dataflow strategy.
 * This is the core insight of the Eyeriss paper (Chen et al., ISCA 2016):
 * different dataflows shift energy consumption across memory levels,
 * and the optimal dataflow minimizes total energy for a given layer shape.
 *
 * Energy formula per dataflow (simplified from Eyeriss JSSC 2017):
 *   Weight Stationary:
 *     E_total = N_MAC × E_MAC + (activations read from SRAM) × E_SRAM
 *             + (weights read from DRAM once) × E_DRAM
 *   Row Stationary:
 *     E_total = N_MAC × E_MAC + (1D row reuse in RF) × E_RF
 *             + (partial sum in SRAM) × E_SRAM
 * ========================================================================== */

void dataflow_energy_breakdown(DataflowType type, int M, int N, int K,
                                int array_w, int array_h,
                                double *mac_energy_pct,
                                double *sram_energy_pct,
                                double *dram_energy_pct) {
    double total_energy = dataflow_energy_estimate(type, M, N, K, array_w, array_h);
    if (total_energy <= 0.0) {
        *mac_energy_pct = 0.0;
        *sram_energy_pct = 0.0;
        *dram_energy_pct = 0.0;
        return;
    }

    int total_macs = M * N * K;
    double mac_e = total_macs * MAC_ENERGY_PJ;

    double sram_reads, dram_reads;
    (void)array_w;
    (void)array_h;

    switch (type) {
        case WEIGHT_STATIONARY:
            dram_reads = (double)(M * K + K * N);
            sram_reads = (double)(total_macs * 2);
            break;
        case OUTPUT_STATIONARY:
            dram_reads = (double)(M * K + K * N + M * N);
            sram_reads = (double)(total_macs * 2);
            break;
        case INPUT_STATIONARY:
            dram_reads = (double)(M * K);
            sram_reads = (double)(total_macs * 2 + K * N);
            break;
        case ROW_STATIONARY: {
            int folds = (M + array_h - 1) / array_h;
            dram_reads = (double)(M * K + folds * K * N);
            sram_reads = (double)(total_macs * 3);
            break;
        }
        default:
            dram_reads = 0.0;
            sram_reads = 0.0;
            break;
    }

    double sram_e = sram_reads * SRAM_READ_ENERGY_PJ;
    double dram_e = dram_reads * DRAM_READ_ENERGY_PJ;

    *mac_energy_pct  = mac_e / total_energy * 100.0;
    *sram_energy_pct = sram_e / total_energy * 100.0;
    *dram_energy_pct = dram_e / total_energy * 100.0;
}

/* ==========================================================================
 * L3: Layer-by-layer dataflow selection heuristic
 *
 * Given layer dimensions, recommend the best dataflow strategy.
 * This implements a simplified version of the Eyeriss v2 hierarchical
 * tiling and dataflow selection algorithm.
 *
 * Heuristic:
 *   - If K is very large (> N×2): Weight Stationary (preload weights, reuse)
 *   - If M×N is very large (output-dominated): Output Stationary
 *   - If M is small and N moderate: Input Stationary (stream inputs)
 *   - Default: Row Stationary (best overall for most CNN layers)
 * ========================================================================== */

DataflowType dataflow_recommend(int M, int N, int K, int array_w, int array_h) {
    /* Compare energy across all strategies */
    DataflowType types[] = {WEIGHT_STATIONARY, OUTPUT_STATIONARY,
                            INPUT_STATIONARY, ROW_STATIONARY};
    double best_energy = 1e30;
    DataflowType best_type = ROW_STATIONARY;

    for (int i = 0; i < 4; i++) {
        double energy = dataflow_energy_estimate(types[i], M, N, K, array_w, array_h);
        if (energy < best_energy) {
            best_energy = energy;
            best_type = types[i];
        }
    }

    return best_type;
}

/* ==========================================================================
 * L3: Eyeriss Row Stationary detailed model
 *
 * Eyeriss v1 (Chen et al., ISCA 2016) introduced Row Stationary dataflow:
 * - Each PE processes one row of filter and one row of ifmap
 * - Partial sums are accumulated locally in each PE (row stationary)
 * - 1D row reuse in RF, 2D convolution reuse in PE array
 * - Multi-level memory hierarchy: RF → PE scratchpad → GLB SRAM → DRAM
 *
 * This function models the number of DRAM accesses required.
 * ========================================================================== */

double eyeriss_row_stationary_dram_accesses(int H, int W, int C, int K, int R, int S) {
    /* Eyeriss v1 models:
     * - Input activations: H×W×C per layer (streamed once from DRAM if possible)
     * - Weights: K×C×R×S (loaded once per inference batch)
     * - Partial sums: K×H_out×W_out (accumulated in PE array, minimal DRAM)
     *
     * DRAM accesses when on-chip buffer insufficient:
     *   ifmaps: H×W×C words per layer (or tiled if too large)
     *   weights: K×C×R×S words (loaded once, then reused across batch)
     */

    int H_out = H - R + 1;
    int W_out = W - S + 1;

    /* Input DRAM reads (ifmaps) */
    double input_dram = (double)(H * W * C);

    /* Weight DRAM reads (loaded once, then kept in SRAM/GLOB) */
    double weight_dram = (double)(K * C * R * S);

    /* Output DRAM writes (partial sums → final output) */
    double output_dram = (double)(K * H_out * W_out);

    return input_dram + weight_dram + output_dram;
}

/* ==========================================================================
 * L4: Temporal vs Spatial Utilization in Systolic Arrays
 *
 * Temporal utilization: fraction of cycles that each PE is active
 * Spatial utilization: fraction of PEs that are active per cycle
 *
 * For a systolic array processing M×N×K matmul:
 *   Temporal: M×N×K MACs / (total_cycles × array_size)
 *   Spatial: depends on dataflow skew — at steady state, nearly 100%
 *            but at start/end, only a partial wavefront is active
 * ========================================================================== */

void dataflow_utilization_temporal_spatial(int M, int N, int K,
                                            int array_w, int array_h,
                                            double *temporal_util,
                                            double *spatial_util) {
    int array_size = array_w * array_h;
    double total_cycles = (double)(M + N + K - 2);
    double total_macs = (double)(M * N * K);

    /* Temporal: actual MACs vs peak capacity */
    double peak_macs = (double)array_size * total_cycles;
    *temporal_util = (peak_macs > 0.0) ? total_macs / peak_macs : 0.0;

    /* Spatial: steady-state wavefront coverage
     * At steady state, the number of active PEs = min(array_w, M) × min(array_h, K)
     * Averaged over all cycles */
    int ssa_pe_w = (array_w < M) ? array_w : M;
    int ssa_pe_h = (array_h < K) ? array_h : K;
    int steady_pe_count = ssa_pe_w * ssa_pe_h;

    /* Fill and drain phases have fewer active PEs */
    double ramp_up_cycles = (double)(array_w + array_h - 2);
    double steady_cycles = total_cycles - ramp_up_cycles;
    if (steady_cycles < 0.0) steady_cycles = 0.0;

    double avg_active_pes = (ramp_up_cycles * (double)steady_pe_count * 0.5
                             + steady_cycles * (double)steady_pe_count) / total_cycles;
    *spatial_util = avg_active_pes / (double)array_size;
    if (*spatial_util > 1.0) *spatial_util = 1.0;
}
