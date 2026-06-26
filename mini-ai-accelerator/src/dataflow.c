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

    int array_size = array_w * array_h;

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
