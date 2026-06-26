#ifndef ACCELERATOR_ROOFLINE_H
#define ACCELERATOR_ROOFLINE_H

#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * accelerator_roofline.h — Roofline Model & Accelerator Efficiency Analysis
 *
 * L4: Standards/Theorems
 *   - Roofline Model (Williams, Patterson, Waterman 2009)
 *   - Amdahl's Law for multi-accelerator systems
 *   - Operational Intensity bounds
 *   - MAC utilization ceiling
 *
 * Reference: "Roofline: An Insightful Visual Performance Model for
 * Multicore Architectures", CACM 2009
 *
 * MIT 6.5930 · Stanford CS217 · Berkeley CS267
 * ========================================================================== */

/* ---- Hardware platform descriptor ---- */
typedef enum {
    PLATFORM_TPU_V1,
    PLATFORM_TPU_V2,
    PLATFORM_TPU_V3,
    PLATFORM_TPU_V4,
    PLATFORM_EYERISS_V2,
    PLATFORM_CUSTOM
} AcceleratorPlatform;

/* Memory hierarchy level for energy modeling */
typedef enum {
    MEM_LEVEL_REGISTER,   /* 0.0 pJ — free in pipeline */
    MEM_LEVEL_LOCAL_SRAM, /* 1.0 pJ — MAC-local scratchpad */
    MEM_LEVEL_GLOBAL_SRAM,/* 5.0 pJ — shared SRAM / unified buffer */
    MEM_LEVEL_DRAM,       /* 640.0 pJ — off-chip DRAM */
    MEM_LEVEL_HBM         /* 20.0 pJ — HBM stack (TPUv4+) */
} MemoryLevel;

/* ---- Operational intensity descriptor ---- */
typedef struct {
    double flops;            /* total floating-point operations */
    double bytes_read;        /* total bytes read from memory hierarchy */
    double bytes_written;     /* total bytes written */
    double operational_intensity; /* FLOPs per byte */
    const char *bound_type;   /* "compute-bound" or "memory-bound" */
} OpIntensityReport;

/* ---- Roofline model parameters per-platform ---- */
typedef struct {
    AcceleratorPlatform platform;
    const char *name;
    double peak_compute_tflops;  /* peak TFLOPs (FP16 or INT8, whatever higher) */
    double peak_bandwidth_gbps;  /* peak DRAM/HBM bandwidth in GB/s */
    double sram_bandwidth_gbps;  /* on-chip SRAM bandwidth */
    int systolic_array_size;     /* N of N×N array */
    double frequency_ghz;        /* clock frequency */
    double on_chip_sram_mb;      /* on-chip SRAM size in MB */
    double ridge_point;          /* peak_compute / peak_bandwidth = ridge FLOP/byte */
} RooflinePlatform;

/* ---- Layer descriptor for roofline analysis ---- */
typedef struct {
    const char *name;
    int M;           /* output rows */
    int N;           /* output cols / output channels */
    int K;           /* reduction dimension */
    int batch_size;
    double flops_per_inference;
    double weights_bytes;    /* total weight tensor bytes */
    double activations_bytes;/* activation bytes per inference */
    double weight_reuse;     /* times each weight is reused */
    double operational_intensity;
} RooflineLayer;

/* ---- Amdahl's Law analysis for multi-accelerator ---- */
typedef struct {
    int num_accelerators;
    double serial_fraction;    /* fraction of work that cannot be parallelized */
    double parallel_fraction;  /* fraction accelerated */
    double speedup;            /* Amdahl speedup: 1 / (serial + parallel/N) */
    double efficiency;         /* speedup / N */
    double scaled_speedup;     /* Gustafson's scaled speedup */
} AmdahlReport;

/* ---- Platform parameter presets ---- */
RooflinePlatform roofline_platform_preset(AcceleratorPlatform platform);
RooflinePlatform roofline_custom_platform(double peak_tflops, double bw_gbps,
                                           double freq_ghz, int array_size, double sram_mb);

/* ---- Operational intensity analysis ---- */
OpIntensityReport roofline_compute_operational_intensity(double flops, double bytes_r,
                                                          double bytes_w);
OpIntensityReport roofline_layer_intensity(RooflineLayer *layer);

/* ---- Roofline boundary determination ---- */
const char *roofline_bound_classify(double op_intensity, double ridge_point);
double roofline_attainable_performance(double op_intensity, RooflinePlatform *plat);
double roofline_utilization(double attained_tflops, double peak_tflops);

/* ---- Multi-layer analysis ---- */
void roofline_analyze_layers(RooflineLayer *layers, int num_layers,
                              RooflinePlatform *plat);
void roofline_compare_platforms(RooflineLayer *layer,
                                 RooflinePlatform *plats, int num_plats);

/* ---- Amdahl's Law ---- */
AmdahlReport amdahl_analyze(int num_accelerators, double serial_fraction);
double amdahl_gustafson_speedup(int num_accelerators, double parallel_fraction);
void amdahl_print_report(AmdahlReport *report);

/* ---- Memory hierarchy energy model ---- */
double memory_energy_cost(MemoryLevel level);
double memory_hierarchy_energy(int num_reg, int num_local_sram,
                               int num_global_sram, int num_dram, int num_hbm);
double memory_access_energy_breakdown(int reads[], int writes[], int num_levels);

/* ---- MAC utilization ---- */
double mac_utilization_theoretical(int M, int N, int K, int array_size, int cycles);
double mac_utilization_with_bubbles(int M, int N, int K, int array_size,
                                     int pipeline_bubbles, double frequency_ghz);

#endif /* ACCELERATOR_ROOFLINE_H */
