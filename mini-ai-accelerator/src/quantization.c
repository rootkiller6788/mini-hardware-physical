#include "quantization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

QuantParams quant_find_params(float *data, int len, QuantType type) {
    QuantParams params;
    memset(&params, 0, sizeof(QuantParams));

    if (len <= 0 || !data) return params;

    float min_val = data[0];
    float max_val = data[0];
    for (int i = 1; i < len; i++) {
        if (data[i] < min_val) min_val = data[i];
        if (data[i] > max_val) max_val = data[i];
    }

    params.min_val = min_val;
    params.max_val = max_val;

    int qmin = 0, qmax = 255;
    if (type == QUANT_INT4) {
        qmin = 0;
        qmax = 15;
    } else if (type == QUANT_FP16) {
        qmin = 0;
        qmax = 65535;
    }

    float range = max_val - min_val;
    if (range < 1e-7f) range = 1e-7f;
    params.scale = range / (float)(qmax - qmin);
    float mid_real = (max_val + min_val) * 0.5f;
    float mid_quant = (float)(qmax + qmin) * 0.5f;
    params.zero_point = (int)roundf(mid_quant - mid_real / params.scale);

    if (params.zero_point < qmin) params.zero_point = qmin;
    if (params.zero_point > qmax) params.zero_point = qmax;

    return params;
}

void quant_quantize(float *data, int len, QuantParams *params, uint8_t *output) {
    if (!data || !params || !output) return;
    float inv_scale = 1.0f / params->scale;
    for (int i = 0; i < len; i++) {
        float q = roundf(data[i] * inv_scale) + params->zero_point;
        if (q < 0.0f) q = 0.0f;
        if (q > 255.0f) q = 255.0f;
        output[i] = (uint8_t)q;
    }
}

void quant_dequantize(uint8_t *data, int len, QuantParams *params, float *output) {
    if (!data || !params || !output) return;
    for (int i = 0; i < len; i++) {
        output[i] = ((float)data[i] - params->zero_point) * params->scale;
    }
}

float quant_simulated_quantize(float val, QuantParams *params) {
    if (!params) return val;
    float inv_scale = 1.0f / params->scale;
    float q = roundf(val * inv_scale) + params->zero_point;
    if (q < 0.0f) q = 0.0f;
    if (q > 255.0f) q = 255.0f;
    uint8_t qv = (uint8_t)q;
    return ((float)qv - params->zero_point) * params->scale;
}

QuantParams *quant_per_channel_find_params(float *data, int rows, int cols, QuantType type) {
    if (!data) return NULL;
    QuantParams *params = (QuantParams *)malloc(sizeof(QuantParams) * rows);
    if (!params) {
        fprintf(stderr, "quant_per_channel_find_params: malloc failed\n");
        return NULL;
    }
    for (int i = 0; i < rows; i++) {
        params[i] = quant_find_params(data + i * cols, cols, type);
    }
    return params;
}

QuantParams quant_per_tensor_find_params(float *data, int len, QuantType type) {
    return quant_find_params(data, len, type);
}

float quant_compute_mse(float *original, uint8_t *quantized, int len, QuantParams *params) {
    if (!original || !quantized || !params) return 0.0f;
    float sum_sq = 0.0f;
    for (int i = 0; i < len; i++) {
        float deq = ((float)quantized[i] - params->zero_point) * params->scale;
        float diff = original[i] - deq;
        sum_sq += diff * diff;
    }
    return sum_sq / (float)len;
}

float quant_compute_max_error(float *original, uint8_t *quantized, int len, QuantParams *params) {
    if (!original || !quantized || !params) return 0.0f;
    float max_err = 0.0f;
    for (int i = 0; i < len; i++) {
        float deq = ((float)quantized[i] - params->zero_point) * params->scale;
        float diff = fabsf(original[i] - deq);
        if (diff > max_err) max_err = diff;
    }
    return max_err;
}

void quant_print_error(float *original, uint8_t *quantized, int len, QuantParams *params) {
    if (!original || !quantized || !params) return;
    float mse = quant_compute_mse(original, quantized, len, params);
    float max_err = quant_compute_max_error(original, quantized, len, params);

    printf("Quantization Error Analysis:\n");
    printf("  Scale: %f, Zero point: %d\n", params->scale, params->zero_point);
    printf("  Range: [%.4f, %.4f]\n", params->min_val, params->max_val);
    printf("  MSE: %f\n", mse);
    printf("  Max Error: %f\n", max_err);
    printf("  Per-element errors (first 16):\n  ");
    int show = len < 16 ? len : 16;
    for (int i = 0; i < show; i++) {
        float deq = ((float)quantized[i] - params->zero_point) * params->scale;
        printf("[%.4f→%.4f] ", original[i], deq);
        if ((i + 1) % 4 == 0 && i < show - 1) printf("\n  ");
    }
    printf("\n");
}
