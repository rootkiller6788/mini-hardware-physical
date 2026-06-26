#ifndef QUANTIZATION_H
#define QUANTIZATION_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    QUANT_INT8,
    QUANT_INT4,
    QUANT_FP16
} QuantType;

typedef struct {
    float scale;
    int zero_point;
    float min_val;
    float max_val;
} QuantParams;

QuantParams quant_find_params(float *data, int len, QuantType type);
void quant_quantize(float *data, int len, QuantParams *params, uint8_t *output);
void quant_dequantize(uint8_t *data, int len, QuantParams *params, float *output);
float quant_simulated_quantize(float val, QuantParams *params);
QuantParams *quant_per_channel_find_params(float *data, int rows, int cols, QuantType type);
QuantParams quant_per_tensor_find_params(float *data, int len, QuantType type);
float quant_compute_mse(float *original, uint8_t *quantized, int len, QuantParams *params);
float quant_compute_max_error(float *original, uint8_t *quantized, int len, QuantParams *params);
void quant_print_error(float *original, uint8_t *quantized, int len, QuantParams *params);

#endif
