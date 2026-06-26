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

/* ==========================================================================
 * L5: INT4 Packing/Unpacking
 *
 * Two INT4 values per byte: lower nibble = first value, upper nibble = second.
 * Range: 0-15 (unsigned) or -8 to 7 (signed). Used in LLM.int8() and
 * modern transformer deployment for extreme compression.
 *
 * This enables 2× storage reduction vs INT8.
 * ========================================================================== */

void quant_int4_pack(uint8_t *int8_data, int len, uint8_t *int4_packed) {
    if (!int8_data || !int4_packed) return;
    for (int i = 0; i < len; i += 2) {
        uint8_t low  = int8_data[i] & 0x0F;
        uint8_t high = (i + 1 < len) ? (int8_data[i + 1] & 0x0F) : 0;
        int4_packed[i / 2] = (uint8_t)((high << 4) | low);
    }
    /* Packed length = packed_len bytes */
}

void quant_int4_unpack(uint8_t *int4_packed, int byte_len, uint8_t *int8_unpacked) {
    if (!int4_packed || !int8_unpacked) return;
    for (int i = 0; i < byte_len; i++) {
        uint8_t byte_val = int4_packed[i];
        int8_unpacked[2 * i]     = byte_val & 0x0F;
        int8_unpacked[2 * i + 1] = (byte_val >> 4) & 0x0F;
    }
}

/* ==========================================================================
 * L5: FP16 (IEEE 754 binary16) Conversion for Quantization-Aware Inference
 *
 * FP16 provides 3-4× throughput vs FP32 on supporting hardware.
 * This is a hardware-friendly path that differs from INT8 quantization:
 * it's a simple cast (no scale/zero-point), but precision loss follows
 * IEEE 754 rounding.
 *
 * FP16 format: 1 sign | 5 exp (bias=15) | 10 mantissa
 * Range: ±65504 (normal), ±5.96e-8 (subnormal min)
 * ========================================================================== */

static uint16_t float_to_fp16_bits(float f) {
    uint32_t f_bits;
    memcpy(&f_bits, &f, sizeof(uint32_t));

    uint16_t sign = (uint16_t)((f_bits >> 16) & 0x8000);
    int32_t exp   = (int32_t)((f_bits >> 23) & 0xFF) - 127;
    uint32_t mant = f_bits & 0x7FFFFF;

    if (f == 0.0f) return sign;
    if (exp > 15)  return sign | 0x7C00; /* overflow → inf */
    if (exp < -14) return sign;          /* underflow → zero */

    int32_t new_exp = exp + 15;
    /* Round mantissa: 23 bits → 10 bits with round-to-nearest-even */
    uint32_t new_mant = (mant + 0x1000) >> 13;
    if (new_mant >= 0x400) { new_mant = 0; new_exp++; }
    if (new_exp >= 31) return sign | 0x7C00; /* overflow → inf */

    return (uint16_t)(sign | ((uint32_t)new_exp << 10) | (new_mant & 0x3FF));
}

static float fp16_bits_to_float(uint16_t bits) {
    uint32_t sign     = ((uint32_t)bits & 0x8000) << 16;
    uint32_t exponent = ((uint32_t)bits >> 10) & 0x1F;
    uint32_t mantissa = ((uint32_t)bits & 0x3FF);

    if (exponent == 0) {
        /* Subnormal or zero */
        if (mantissa == 0) {
            uint32_t f_bits = sign;
            float r;
            memcpy(&r, &f_bits, sizeof(float));
            return r;
        }
        /* Subnormal normalization — simplified */
        float val = (float)mantissa / 1024.0f * ldexpf(1.0f, -14);
        return (sign ? -val : val);
    }
    if (exponent == 31) {
        uint32_t f_bits = sign | 0x7F800000 | (mantissa << 13);
        float r;
        memcpy(&r, &f_bits, sizeof(float));
        return r;
    }

    int32_t fp32_exp = (int32_t)exponent - 15 + 127;
    uint32_t f_bits  = sign | ((uint32_t)fp32_exp << 23) | (mantissa << 13);
    float r;
    memcpy(&r, &f_bits, sizeof(float));
    return r;
}

void quant_fp16_cast(float *data, int len, uint16_t *fp16_out) {
    if (!data || !fp16_out) return;
    for (int i = 0; i < len; i++) {
        fp16_out[i] = float_to_fp16_bits(data[i]);
    }
}

void quant_fp16_uncast(uint16_t *fp16_in, int len, float *float_out) {
    if (!fp16_in || !float_out) return;
    for (int i = 0; i < len; i++) {
        float_out[i] = fp16_bits_to_float(fp16_in[i]);
    }
}

float quant_fp16_single_cast(float val) {
    uint16_t h = float_to_fp16_bits(val);
    return fp16_bits_to_float(h);
}

/* ==========================================================================
 * L8: KL Divergence Calibration (TensorRT / NVIDIA method)
 *
 * Used to determine optimal quantization clipping range by minimizing
 * KL divergence between original FP32 distribution and quantized INT8
 * distribution.
 *
 * D_KL(P || Q) = Σ P(i) log(P(i) / Q(i))
 *
 * Algorithm:
 *   1. Build histogram of FP32 activations (n_bins=2048 typical)
 *   2. For each candidate threshold (top-bin):
 *      a. Clamp values above threshold to threshold
 *      b. Quantize to INT8
 *      c. Compute KL divergence with original distribution
 *   3. Choose threshold with minimum KL divergence
 * ========================================================================== */

float quant_kl_divergence(float *hist_p, float *hist_q, int num_bins) {
    if (!hist_p || !hist_q || num_bins <= 0) return 0.0f;

    double kl = 0.0;
    for (int i = 0; i < num_bins; i++) {
        if (hist_p[i] > 1e-10f && hist_q[i] > 1e-10f) {
            kl += (double)hist_p[i] * log((double)hist_p[i] / (double)hist_q[i]);
        }
    }
    return (float)kl;
}

float quant_calibrate_kl(float *data, int len, int num_bins, QuantParams *params) {
    if (!data || !params || len <= 0 || num_bins <= 0) return 0.0f;

    /* Find data range */
    float min_val = data[0], max_val = data[0];
    for (int i = 1; i < len; i++) {
        if (data[i] < min_val) min_val = data[i];
        if (data[i] > max_val) max_val = data[i];
    }

    if (max_val - min_val < 1e-7f) {
        params->scale = 1.0f;
        params->zero_point = 0;
        params->min_val = min_val;
        params->max_val = max_val;
        return 0.0f;
    }

    /* Build reference histogram (FP32) */
    float *ref_hist = (float *)calloc(num_bins, sizeof(float));
    if (!ref_hist) return 0.0f;

    float bin_width = (max_val - min_val) / (float)num_bins;
    for (int i = 0; i < len; i++) {
        int bin = (int)((data[i] - min_val) / bin_width);
        if (bin < 0) bin = 0;
        if (bin >= num_bins) bin = num_bins - 1;
        ref_hist[bin] += 1.0f;
    }

    /* Normalize reference histogram */
    float total_count = (float)len;
    for (int i = 0; i < num_bins; i++) {
        ref_hist[i] /= total_count;
    }

    /* Search for optimal threshold */
    float best_kl = 1e30f;
    int best_threshold_bin = num_bins - 1;

    for (int t = num_bins / 2; t < num_bins; t++) {
        /* Build quantized histogram: sum overflow bins into last quantized bin */
        float *quant_hist = (float *)calloc(256, sizeof(float));
        if (!quant_hist) continue;

        for (int i = 0; i < num_bins; i++) {
            int qbin;
            if (i > t) {
                qbin = 255; /* clamp overflow to max */
            } else {
                qbin = (int)((float)i / (float)t * 255.0f);
                if (qbin > 255) qbin = 255;
            }
            quant_hist[qbin] += ref_hist[i];
        }

        /* Map quant_hist back to ref_bins for KL computation
         * Each quantized value maps to center of its bin range */
        float *mapped_hist = (float *)calloc(num_bins, sizeof(float));
        if (mapped_hist) {
            float *q_bin_centers = (float *)calloc(256, sizeof(float));
            if (q_bin_centers) {
                /* Dequantized value of each INT8 bin */
                for (int q = 0; q < 256; q++) {
                    q_bin_centers[q] = ((float)q - 128.0f) * (max_val - min_val) / 255.0f
                                       + (max_val + min_val) / 2.0f;
                }

                /* Distribute quant_hist back to reference histogram bins */
                for (int q = 0; q < 256; q++) {
                    if (quant_hist[q] > 0.0f) {
                        float center = q_bin_centers[q];
                        int ref_bin = (int)((center - min_val) / bin_width);
                        if (ref_bin < 0) ref_bin = 0;
                        if (ref_bin >= num_bins) ref_bin = num_bins - 1;
                        mapped_hist[ref_bin] += quant_hist[q];
                    }
                }
                free(q_bin_centers);
            }

            float kl = quant_kl_divergence(ref_hist, mapped_hist, num_bins);
            if (kl < best_kl) {
                best_kl = kl;
                best_threshold_bin = t;
            }
            free(mapped_hist);
        }
        free(quant_hist);
    }

    /* Compute final quantization parameters from best threshold */
    float best_threshold = min_val + bin_width * (float)best_threshold_bin;
    params->min_val = min_val;
    params->max_val = best_threshold;
    params->scale   = best_threshold / 127.0f;
    params->zero_point = 0; /* symmetric calibration */

    free(ref_hist);
    return best_kl;
}

/* ==========================================================================
 * L5: Per-Axis (Per-Output-Channel) Quantization
 *
 * Different output channels often have dramatically different value ranges.
 * Per-axis quantization assigns separate scale/zero-point to each channel,
 * significantly reducing accuracy loss compared to per-tensor.
 *
 * This is the default in TFLite, ONNX Runtime, and most mobile inference.
 * ========================================================================== */

QuantParams *quant_per_axis_params(float *data, int rows, int cols, int axis, QuantType type) {
    if (!data || rows <= 0 || cols <= 0) return NULL;

    int num_params;

    if (axis == 0) {
        num_params = rows;
    } else {
        num_params = cols;
    }

    QuantParams *params = (QuantParams *)malloc(sizeof(QuantParams) * num_params);
    if (!params) {
        fprintf(stderr, "quant_per_axis_params: malloc failed\n");
        return NULL;
    }

    for (int i = 0; i < num_params; i++) {
        params[i] = quant_find_params(
            (axis == 0) ? data + i * cols : data + i,
            (axis == 0) ? cols : rows,
            type);
    }

    return params;
}

void quant_per_axis_quantize(float *data, int rows, int cols, QuantParams *params,
                              int axis, uint8_t *output) {
    if (!data || !params || !output) return;

    if (axis == 0) {
        for (int i = 0; i < rows; i++) {
            quant_quantize(data + i * cols, cols, &params[i], output + i * cols);
        }
    } else {
        /* Per-column: interleaved access */
        QuantParams col_params;
        for (int j = 0; j < cols; j++) {
            col_params = params[j];
            for (int i = 0; i < rows; i++) {
                float val = data[i * cols + j];
                float q = roundf(val / col_params.scale) + col_params.zero_point;
                if (q < 0.0f) q = 0.0f;
                if (q > 255.0f) q = 255.0f;
                output[i * cols + j] = (uint8_t)q;
            }
        }
    }
}

/* ==========================================================================
 * L8: Dynamic Quantization — Calibrate Per-Input
 *
 * Unlike static quantization (pre-calibrated params), dynamic quantization
 * computes scale/zero-point at runtime for each input. This is common in
 * PyTorch dynamic quantization and ONNX Runtime for RNN/Transformer models
 * where activation ranges vary significantly.
 * ========================================================================== */

void quant_dynamic_quantize(float *data, int len, QuantType type,
                             uint8_t *output, QuantParams *out_params) {
    if (!data || !output || !out_params) return;

    *out_params = quant_find_params(data, len, type);
    quant_quantize(data, len, out_params, output);
}

/* ==========================================================================
 * L9: Zero-Point-Free Symmetric Quantization
 *
 * Modern hardware (TPUv2+, NVIDIA Tensor Cores with BF16, Apple ANE) often
 * uses symmetric quantization (zero_point = 0) to simplify hardware.
 * This eliminates the zero-point subtraction in the inner loop.
 *
 * Symmetric: q = clamp(round(x / S), qmin, qmax)
 * Asymmetric: q = clamp(round(x / S) + Z, qmin, qmax)
 *
 * Symmetric doubles representable range on one side (±127 vs 0-255 for INT8).
 * ========================================================================== */

QuantParams quant_symmetric_params(float *data, int len, QuantType type) {
    QuantParams params;
    memset(&params, 0, sizeof(params));

    if (len <= 0 || !data) return params;

    /* Symmetric: scale = max(|min|, |max|) / qmax */
    float max_abs = fabsf(data[0]);
    for (int i = 1; i < len; i++) {
        float aval = fabsf(data[i]);
        if (aval > max_abs) max_abs = aval;
    }

    int qmax;
    switch (type) {
        case QUANT_INT4: qmax = 7; break;   /* INT4 signed: -8 to 7, but symmetric uses ±7 */
        case QUANT_INT8: qmax = 127; break;
        case QUANT_FP16: qmax = 65504; break;
        default:         qmax = 127; break;
    }

    params.scale      = max_abs / (float)qmax;
    params.zero_point = 0;
    params.min_val    = -max_abs;
    params.max_val    = max_abs;

    if (params.scale < 1e-9f) params.scale = 1e-9f;

    return params;
}

void quant_symmetric_quantize(float *data, int len, QuantParams *params, uint8_t *output) {
    if (!data || !params || !output) return;

    float inv_scale = 1.0f / params->scale;
    for (int i = 0; i < len; i++) {
        float q = roundf(data[i] * inv_scale);
        if (q < -128.0f) q = -128.0f;
        if (q > 127.0f)  q = 127.0f;
        /* Store as uint8 with bias 128 to keep unsigned */
        output[i] = (uint8_t)((int8_t)q + 128);
    }
}
