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

#endif
