#include "tpu_isa.h"
#include "systolic_array.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static float sigmoid(float x) { return 1.0f / (1.0f + expf(-x)); }
static float tanh_approx(float x) { return tanhf(x); }

TPUCore *tpu_init(uint32_t ub_size_mb) {
    TPUCore *tpu = (TPUCore *)malloc(sizeof(TPUCore));
    if (!tpu) {
        fprintf(stderr, "tpu_init: malloc failed for TPUCore\n");
        return NULL;
    }

    ub_size_mb = ub_size_mb > TPU_UB_MAX_MB ? TPU_UB_MAX_MB : ub_size_mb;
    tpu->ub_size = ub_size_mb * 1024 * 1024;
    tpu->unified_buffer = (uint8_t *)calloc(tpu->ub_size, 1);
    if (!tpu->unified_buffer) {
        fprintf(stderr, "tpu_init: malloc failed for unified buffer\n");
        free(tpu);
        return NULL;
    }

    tpu->systolic_array = systolic_array_create(MAX_SYSTOLIC_SIZE, MAX_SYSTOLIC_SIZE);
    if (!tpu->systolic_array) {
        fprintf(stderr, "tpu_init: systolic array creation failed\n");
        free(tpu->unified_buffer);
        free(tpu);
        return NULL;
    }

    tpu->ub_alloc_ptr = 0;
    tpu->scalar_unit = 0.0f;
    tpu->pc = 0;
    tpu->running = true;

    for (int i = 0; i < TPU_ACCUMULATOR_SIZE; i++) {
        tpu->accumulator_buffer[i] = 0.0f;
    }
    for (int i = 0; i < MAX_SYSTOLIC_SIZE; i++) {
        tpu->vector_unit[i] = 0.0f;
    }

    return tpu;
}

void tpu_destroy(TPUCore *tpu) {
    if (!tpu) return;
    if (tpu->systolic_array) systolic_array_destroy(tpu->systolic_array);
    if (tpu->unified_buffer) free(tpu->unified_buffer);
    free(tpu);
}

void tpu_load_program(TPUCore *tpu, TPUInstruction *prog, int len) {
    if (!tpu || !prog) return;
    tpu->pc = 0;
    tpu->running = true;
    int prog_bytes = len * (int)sizeof(TPUInstruction);
    if (prog_bytes > (int)tpu->ub_size) {
        fprintf(stderr, "tpu_load_program: program too large for UB\n");
        tpu->running = false;
        return;
    }
    memcpy(tpu->unified_buffer, prog, prog_bytes);
}

uint32_t tpu_ub_alloc(TPUCore *tpu, uint32_t size_bytes) {
    if (!tpu) return 0;
    uint32_t aligned = (size_bytes + 63) & ~63u;
    if (tpu->ub_alloc_ptr + aligned > tpu->ub_size) {
        fprintf(stderr, "tpu_ub_alloc: out of UB memory\n");
        return (uint32_t)-1;
    }
    uint32_t addr = tpu->ub_alloc_ptr;
    tpu->ub_alloc_ptr += aligned;
    return addr;
}

void tpu_ub_free(TPUCore *tpu) {
    if (!tpu) return;
    tpu->ub_alloc_ptr = 0;
}

int tpu_execute(TPUCore *tpu, TPUInstruction *inst) {
    if (!tpu || !inst) return -1;

    float *ub_float = (float *)tpu->unified_buffer;

    switch (inst->opcode) {
        case OP_MATMUL: {
            int M = inst->arg0;
            int N = inst->arg1;
            int K = inst->arg2;
            float *act = ub_float + inst->src_addr / sizeof(float);
            float *wts = ub_float + inst->dst_addr / sizeof(float);
            systolic_run(tpu->systolic_array, act, wts, M, N, K, tpu->accumulator_buffer);
            break;
        }
        case OP_VECTOR_ADD: {
            float *a = ub_float + inst->src_addr / sizeof(float);
            float *b = ub_float + inst->dst_addr / sizeof(float);
            int len = inst->size_bytes / (int)sizeof(float);
            for (int i = 0; i < len && i < TPU_ACCUMULATOR_SIZE; i++) {
                tpu->accumulator_buffer[i] = a[i] + b[i];
            }
            break;
        }
        case OP_VECTOR_MUL: {
            float *a = ub_float + inst->src_addr / sizeof(float);
            float *b = ub_float + inst->dst_addr / sizeof(float);
            int len = inst->size_bytes / (int)sizeof(float);
            for (int i = 0; i < len && i < TPU_ACCUMULATOR_SIZE; i++) {
                tpu->accumulator_buffer[i] = a[i] * b[i];
            }
            break;
        }
        case OP_ACTIVATION: {
            int len = inst->size_bytes / (int)sizeof(float);
            for (int i = 0; i < len && i < TPU_ACCUMULATOR_SIZE; i++) {
                float v = tpu->accumulator_buffer[i];
                switch (inst->activation) {
                    case ACT_RELU:
                        tpu->accumulator_buffer[i] = v > 0.0f ? v : 0.0f;
                        break;
                    case ACT_SIGMOID:
                        tpu->accumulator_buffer[i] = sigmoid(v);
                        break;
                    case ACT_TANH:
                        tpu->accumulator_buffer[i] = tanh_approx(v);
                        break;
                }
            }
            break;
        }
        case OP_LOAD_WEIGHT: {
            uint32_t addr = inst->src_addr;
            uint32_t size = inst->size_bytes;
            if (addr + size > tpu->ub_size) {
                fprintf(stderr, "OP_LOAD_WEIGHT: address out of bounds\n");
                return -1;
            }
            break;
        }
        case OP_LOAD_ACT: {
            uint32_t addr = inst->src_addr;
            uint32_t size = inst->size_bytes;
            if (addr + size > tpu->ub_size) {
                fprintf(stderr, "OP_LOAD_ACT: address out of bounds\n");
                return -1;
            }
            break;
        }
        case OP_STORE: {
            float *src = tpu->accumulator_buffer;
            float *dst = ub_float + inst->dst_addr / sizeof(float);
            int len = inst->size_bytes / (int)sizeof(float);
            for (int i = 0; i < len && i < TPU_ACCUMULATOR_SIZE; i++) {
                dst[i] = src[i];
            }
            break;
        }
        case OP_SYNC:
            break;
        default:
            fprintf(stderr, "tpu_execute: unknown opcode %d\n", inst->opcode);
            return -1;
    }
    return 0;
}

void tpu_step(TPUCore *tpu) {
    if (!tpu || !tpu->running) return;
    TPUInstruction *program = (TPUInstruction *)tpu->unified_buffer;
    TPUInstruction inst = program[tpu->pc];
    printf("  TPU step %u: op=%d\n", tpu->pc, inst.opcode);
    tpu_execute(tpu, &inst);
    tpu->pc++;
    if (inst.opcode == OP_SYNC) {
        tpu->running = false;
    }
}

void tpu_print_state(TPUCore *tpu) {
    if (!tpu) return;
    printf("TPU Core State:\n");
    printf("  PC: %u, Running: %s\n", tpu->pc, tpu->running ? "yes" : "no");
    printf("  UB: %u MB, alloc_ptr: %u\n", tpu->ub_size / (1024 * 1024), tpu->ub_alloc_ptr);
    printf("  Scalar unit: %.4f\n", tpu->scalar_unit);
    printf("  Accumulator buffer (first 8): ");
    for (int i = 0; i < 8; i++) {
        printf("%.4f ", tpu->accumulator_buffer[i]);
    }
    printf("\n");
    systolic_print_state(tpu->systolic_array);
}
