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

/* ---- L5: INT4 packing/unpacking (two INT4 values per byte) ---- */
void quant_int4_pack(uint8_t *int8_data, int len, uint8_t *int4_packed);
void quant_int4_unpack(uint8_t *int4_packed, int byte_len, uint8_t *int8_unpacked);

/* ---- L5: FP16 IEEE 754 binary16 conversion (hardware-accelerated path) ---- */
void quant_fp16_cast(float *data, int len, uint16_t *fp16_out);
void quant_fp16_uncast(uint16_t *fp16_in, int len, float *float_out);
float quant_fp16_single_cast(float val);

/* ---- L8: KL divergence calibration for quantization (TensorRT method) ---- */
float quant_kl_divergence(float *hist_p, float *hist_q, int num_bins);
float quant_calibrate_kl(float *data, int len, int num_bins, QuantParams *params);

/* ---- L5: Per-axis (per-output-channel) symmetric quantization ---- */
QuantParams *quant_per_axis_params(float *data, int rows, int cols, int axis, QuantType type);
void quant_per_axis_quantize(float *data, int rows, int cols, QuantParams *params,
                              int axis, uint8_t *output);

/* ---- L8: Dynamic quantization (calibrate per-input, no pre-computed params) ---- */
void quant_dynamic_quantize(float *data, int len, QuantType type,
                             uint8_t *output, QuantParams *out_params);

/* ---- L9: Zero-point free symmetric quantization (newer hardware trend) ---- */
QuantParams quant_symmetric_params(float *data, int len, QuantType type);
void quant_symmetric_quantize(float *data, int len, QuantParams *params, uint8_t *output);

#endif
