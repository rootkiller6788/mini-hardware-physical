#include "quantization.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define DATA_LEN 16
#define CHANNEL_ROWS 4
#define CHANNEL_COLS 4

int main(void) {
    printf("=========================================================\n");
    printf("  Quantization Demo (INT8)\n");
    printf("=========================================================\n\n");

    float data[DATA_LEN] = {-3.0f, -2.0f, -1.0f, 0.0f,
                             1.0f, 2.0f, 3.0f, 4.0f,
                             5.0f, 6.0f, 7.0f, 7.5f,
                            -1.5f, 0.5f, 2.5f, 4.5f};

    printf("Original float data:\n  ");
    for (int i = 0; i < DATA_LEN; i++) {
        printf("%7.3f ", data[i]);
        if ((i + 1) % 8 == 0) printf("\n  ");
    }
    printf("\n\n");

    printf("=== Per-Tensor Quantization ===\n");
    QuantParams params = quant_per_tensor_find_params(data, DATA_LEN, QUANT_INT8);
    printf("  Scale: %f\n", params.scale);
    printf("  Zero point: %d\n", params.zero_point);
    printf("  Range: [%.4f, %.4f]\n\n", params.min_val, params.max_val);

    uint8_t *quantized = (uint8_t *)malloc(DATA_LEN * sizeof(uint8_t));
    float *dequantized = (float *)malloc(DATA_LEN * sizeof(float));
    if (!quantized || !dequantized) {
        fprintf(stderr, "Malloc failed\n");
        free(quantized); free(dequantized);
        return 1;
    }

    quant_quantize(data, DATA_LEN, &params, quantized);
    quant_dequantize(quantized, DATA_LEN, &params, dequantized);

    printf("Quantized values (uint8):\n  ");
    for (int i = 0; i < DATA_LEN; i++) {
        printf("%4u ", quantized[i]);
        if ((i + 1) % 8 == 0) printf("\n  ");
    }
    printf("\n\n");

    printf("Dequantized back to float:\n  ");
    for (int i = 0; i < DATA_LEN; i++) {
        printf("%7.3f ", dequantized[i]);
        if ((i + 1) % 8 == 0) printf("\n  ");
    }
    printf("\n\n");

    quant_print_error(data, quantized, DATA_LEN, &params);

    float roundtrip[DATA_LEN];
    printf("\n=== Simulated Quantization (round-trip) ===\n  ");
    for (int i = 0; i < DATA_LEN; i++) {
        roundtrip[i] = quant_simulated_quantize(data[i], &params);
        printf("%7.3f ", roundtrip[i]);
        if ((i + 1) % 8 == 0) printf("\n  ");
    }
    printf("\n");

    printf("\n=== Per-Channel Quantization ===\n");
    QuantParams *ch_params = quant_per_channel_find_params(data, CHANNEL_ROWS, CHANNEL_COLS, QUANT_INT8);
    if (ch_params) {
        for (int c = 0; c < CHANNEL_ROWS; c++) {
            printf("  Channel %d: scale=%f, zp=%d, range=[%.4f, %.4f]\n",
                   c, ch_params[c].scale, ch_params[c].zero_point,
                   ch_params[c].min_val, ch_params[c].max_val);
        }
        free(ch_params);
    }

    printf("\n=== Comparison Summary ===\n");
    printf("  Per-tensor:   scale=%.6f, zp=%d\n", params.scale, params.zero_point);
    printf("  Per-channel provides finer granularity for each output channel.\n");
    printf("  Per-tensor uses single scale for all channels, simpler hardware.\n");

    printf("\n=========================================================\n");
    printf("  Demo Complete\n");
    printf("=========================================================\n");

    free(quantized);
    free(dequantized);
    return 0;
}
