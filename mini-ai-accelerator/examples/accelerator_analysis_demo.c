#include "accelerator_roofline.h"
#include "dataflow.h"
#include "systolic_array.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void) {
    printf("=========================================================\n");
    printf("  Accelerator Roofline & Efficiency Analysis Demo\n");
    printf("=========================================================\n\n");

    /* ---- Roofline Model: Platform Comparison ---- */
    printf("--- Roofline Model: Platform Presets ---\n");
    AcceleratorPlatform plats[] = {PLATFORM_TPU_V1, PLATFORM_TPU_V2,
                                    PLATFORM_TPU_V3, PLATFORM_TPU_V4,
                                    PLATFORM_EYERISS_V2};
    int num_plats = 5;

    for (int i = 0; i < num_plats; i++) {
        RooflinePlatform p = roofline_platform_preset(plats[i]);
        printf("  %-12s: Peak=%.1f TFLOPS, BW=%.1f GB/s, Ridge=%.3f FLOP/byte, Array=%dx%d\n",
               p.name, p.peak_compute_tflops, p.peak_bandwidth_gbps, p.ridge_point,
               p.systolic_array_size, p.systolic_array_size);
    }

    /* ---- Layer Roofline Analysis ---- */
    printf("\n--- Layer Roofline Analysis ---\n");
    RooflineLayer layers[] = {
        {"Conv1_7x7",      112, 112, 64,  1},
        {"Conv2_3x3",      56,  56,  128, 1},
        {"Conv3_3x3",      28,  28,  256, 1},
        {"FC1",            1,   1,   4096, 1},
        {"Attention_S=128", 128, 128, 64,  1},
        {"Attention_S=512", 512, 512, 64,  1}
    };
    int num_layers = 6;

    /* Set layer bytes */
    for (int i = 0; i < num_layers; i++) {
        layers[i].weights_bytes = (double)(layers[i].N * layers[i].K) * 4.0;
        layers[i].activations_bytes = (double)(layers[i].M * layers[i].K) * 4.0;
    }

    RooflinePlatform tpu_v3 = roofline_platform_preset(PLATFORM_TPU_V3);
    roofline_analyze_layers(layers, num_layers, &tpu_v3);

    /* ---- Platform Comparison ---- */
    printf("\n--- Platform Comparison (for Attention_S=512) ---\n");
    RooflinePlatform plat_array[5];
    for (int i = 0; i < 5; i++) {
        plat_array[i] = roofline_platform_preset(plats[i]);
    }
    roofline_compare_platforms(&layers[5], plat_array, 5);

    /* ---- Amdahl's Law ---- */
    printf("\n--- Amdahl's Law: Multi-Accelerator Scaling ---\n");
    double serial_fracs[] = {0.01, 0.05, 0.10, 0.20};
    int core_counts[] = {1, 2, 4, 8, 16};

    for (int si = 0; si < 4; si++) {
        printf("\nSerial fraction = %.1f%%:\n", serial_fracs[si] * 100.0);
        printf("  Cores: ");
        for (int ci = 0; ci < 5; ci++) printf("%6d ", core_counts[ci]);
        printf("\n  Speedup:");
        for (int ci = 0; ci < 5; ci++) {
            AmdahlReport r = amdahl_analyze(core_counts[ci], serial_fracs[si]);
            printf("%6.2f ", r.speedup);
        }
        printf("\n  Gustafson:");
        for (int ci = 0; ci < 5; ci++) {
            double gs = amdahl_gustafson_speedup(core_counts[ci], 1.0 - serial_fracs[si]);
            printf("%6.2f ", gs);
        }
        printf("\n");
    }

    /* ---- MAC Utilization ---- */
    printf("\n--- MAC Utilization Analysis ---\n");
    int array_sizes[] = {16, 64, 128, 256};
    int test_dims[][3] = {{64, 64, 64}, {128, 128, 128}, {256, 256, 256}, {512, 512, 512}};

    printf("  %-20s %10s %10s %10s %10s\n", "Layer", "M=N=K", "ArraySize", "Cycles", "Util%%");
    printf("  %-20s %10s %10s %10s %10s\n", "--------------------", "----------", "----------", "----------", "----------");
    for (int di = 0; di < 4; di++) {
        for (int ai = 0; ai < 4; ai++) {
            int M = test_dims[di][0];
            int N = test_dims[di][1];
            int K = test_dims[di][2];
            int sz = array_sizes[ai];
            int cycles = systolic_count_cycles(M, N, K, sz, sz);
            double util = systolic_utilization_efficiency(M, N, K, sz, sz) * 100.0;
            char label[32];
            snprintf(label, sizeof(label), "MatMul_%dx%dx%d", M, N, K);
            printf("  %-20s %10d %10d %10d %9.1f%%\n", label, M, sz, cycles, util);
        }
    }

    /* ---- Dataflow Recommendation ---- */
    printf("\n--- Dataflow Strategy Recommendations ---\n");
    int layers_to_test[][3] = {{14, 14, 32}, {56, 56, 64}, {56, 56, 128}, {112, 112, 256}};
    for (int i = 0; i < 4; i++) {
        DataflowType best = dataflow_recommend(layers_to_test[i][0],
                                                layers_to_test[i][1],
                                                layers_to_test[i][2], 16, 16);
        printf("  Layer M=%d N=%d K=%d: recommended %s\n",
               layers_to_test[i][0], layers_to_test[i][1], layers_to_test[i][2],
               dataflow_type_name(best));
    }

    /* ---- Memory Hierarchy Energy ---- */
    printf("\n--- Memory Hierarchy Energy Model ---\n");
    printf("  Level        Energy (pJ/access)\n");
    printf("  ---------    -----------------\n");
    const char *level_names[] = {"Register", "Local SRAM", "Global SRAM", "DRAM", "HBM"};
    MemoryLevel levels[] = {MEM_LEVEL_REGISTER, MEM_LEVEL_LOCAL_SRAM,
                            MEM_LEVEL_GLOBAL_SRAM, MEM_LEVEL_DRAM, MEM_LEVEL_HBM};
    for (int i = 0; i < 5; i++) {
        printf("  %-12s %17.1f\n", level_names[i], memory_energy_cost(levels[i]));
    }
    printf("  DRAM/HBM ratio: %.0fx\n", memory_energy_cost(MEM_LEVEL_DRAM) / memory_energy_cost(MEM_LEVEL_HBM));

    printf("\n=========================================================\n");
    printf("  Demo Complete\n");
    printf("=========================================================\n");
    return 0;
}
