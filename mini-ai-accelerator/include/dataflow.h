#ifndef DATAFLOW_H
#define DATAFLOW_H

#include <stdbool.h>
#include <stdint.h>

#define MAC_ENERGY_PJ 1.0f
#define SRAM_READ_ENERGY_PJ 5.0f
#define DRAM_READ_ENERGY_PJ 640.0f

typedef enum {
    WEIGHT_STATIONARY,
    OUTPUT_STATIONARY,
    INPUT_STATIONARY,
    ROW_STATIONARY
} DataflowType;

const char *dataflow_type_name(DataflowType type);
double dataflow_energy_estimate(DataflowType type, int M, int N, int K, int array_w, int array_h);
double dataflow_timing_estimate(DataflowType type, int M, int N, int K, int array_w, int array_h);
void dataflow_compare_all(int M, int N, int K, int array_w, int array_h);

/* ---- L4: MAC utilization estimation ---- */
double dataflow_mac_utilization(DataflowType type, int M, int N, int K,
                                 int array_w, int array_h);

/* ---- L3: Energy breakdown by memory level ---- */
void dataflow_energy_breakdown(DataflowType type, int M, int N, int K,
                                int array_w, int array_h,
                                double *mac_energy_pct,
                                double *sram_energy_pct,
                                double *dram_energy_pct);

/* ---- L3: Heuristic dataflow recommendation for a given layer ---- */
DataflowType dataflow_recommend(int M, int N, int K, int array_w, int array_h);

/* ---- L3: Eyeriss Row Stationary detailed DRAM access model ---- */
double eyeriss_row_stationary_dram_accesses(int H, int W, int C, int K, int R, int S);

/* ---- L4: Temporal vs Spatial utilization breakdown ---- */
void dataflow_utilization_temporal_spatial(int M, int N, int K,
                                            int array_w, int array_h,
                                            double *temporal_util,
                                            double *spatial_util);

#endif
