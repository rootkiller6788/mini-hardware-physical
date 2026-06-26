#ifndef SYSTOLIC_ARRAY_H
#define SYSTOLIC_ARRAY_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_SYSTOLIC_SIZE 16

typedef struct {
    float accumulator;
    float weight;
    float activation;
    float partial_sum;
} SystolicCell;

typedef struct {
    int rows;
    int cols;
    SystolicCell cells[MAX_SYSTOLIC_SIZE][MAX_SYSTOLIC_SIZE];
    float input_fifo[MAX_SYSTOLIC_SIZE];
    float weight_fifo[MAX_SYSTOLIC_SIZE];
    float output_buffer[MAX_SYSTOLIC_SIZE];
    int input_head;
    int weight_head;
} SystolicArray;

SystolicArray *systolic_array_create(int rows, int cols);
void systolic_array_destroy(SystolicArray *sa);
void systolic_load_weights(SystolicArray *sa, float *weights, int rows, int cols);
void systolic_load_activation(SystolicArray *sa, float *act, int len);
void systolic_cycle(SystolicArray *sa);
void systolic_run(SystolicArray *sa, float *activations, float *weights, int M, int N, int K, float *result);
void systolic_print_state(SystolicArray *sa);

#endif
