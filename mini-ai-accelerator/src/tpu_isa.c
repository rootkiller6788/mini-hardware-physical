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

/* ==========================================================================
 * L7: Multi-Core TPU Orchestration
 *
 * Real TPU deployments (TPUv2+) use multiple cores per chip (2-8 cores).
 * This implements basic multi-core orchestration with data-parallel
 * workload distribution across cores.
 *
 * Model: Data-parallel across batch dimension
 *   Core i processes batch[i * batch_per_core : (i+1) * batch_per_core]
 * ========================================================================== */

struct TPUMultiCore {
    TPUCore **cores;
    int num_cores;
    int *core_allocation;
};

TPUMultiCore *tpu_multicore_init(int num_cores, uint32_t ub_size_mb_per_core) {
    TPUMultiCore *mc = (TPUMultiCore *)malloc(sizeof(TPUMultiCore));
    if (!mc) {
        fprintf(stderr, "tpu_multicore_init: malloc failed\n");
        return NULL;
    }
    mc->num_cores = num_cores;
    mc->cores = (TPUCore **)malloc(num_cores * sizeof(TPUCore *));
    mc->core_allocation = (int *)calloc(num_cores, sizeof(int));

    if (!mc->cores || !mc->core_allocation) {
        fprintf(stderr, "tpu_multicore_init: array malloc failed\n");
        free(mc->cores);
        free(mc->core_allocation);
        free(mc);
        return NULL;
    }

    for (int i = 0; i < num_cores; i++) {
        mc->cores[i] = tpu_init(ub_size_mb_per_core);
        if (!mc->cores[i]) {
            fprintf(stderr, "tpu_multicore_init: core %d init failed\n", i);
            for (int j = 0; j < i; j++) tpu_destroy(mc->cores[j]);
            free(mc->cores);
            free(mc->core_allocation);
            free(mc);
            return NULL;
        }
        mc->core_allocation[i] = 0;
    }
    return mc;
}

void tpu_multicore_destroy(TPUMultiCore *mc) {
    if (!mc) return;
    for (int i = 0; i < mc->num_cores; i++) {
        tpu_destroy(mc->cores[i]);
    }
    free(mc->cores);
    free(mc->core_allocation);
    free(mc);
}

void tpu_multicore_distribute_batch(TPUMultiCore *mc, int total_batch_size) {
    if (!mc || total_batch_size <= 0) return;

    int base = total_batch_size / mc->num_cores;
    int remainder = total_batch_size % mc->num_cores;

    for (int i = 0; i < mc->num_cores; i++) {
        mc->core_allocation[i] = base + (i < remainder ? 1 : 0);
    }
}

void tpu_multicore_execute_parallel(TPUMultiCore *mc, TPUInstruction *program, int prog_len) {
    if (!mc || !program) return;

    /* In a real system, cores execute in parallel.
     * Here we simulate sequential execution for determinism. */
    for (int i = 0; i < mc->num_cores; i++) {
        TPUCore *core = mc->cores[i];
        tpu_load_program(core, program, prog_len);
        core->running = true;
        core->pc = 0;

        while (core->running) {
            tpu_step(core);
        }
    }
}

/* ==========================================================================
 * L3: Pipelined TPU Instruction Execution
 *
 * Real TPUs use instruction pipelining to overlap memory loads with
 * computation. This models a 4-stage pipeline:
 *   1. IF — Instruction Fetch
 *   2. ID — Instruction Decode / Address Calculation
 *   3. EX — Execute (MAC array operation)
 *   4. WB — Write Back (store results)
 *
 * This enables hiding memory latency behind computation.
 * ========================================================================== */

typedef enum {
    PIPE_IF,
    PIPE_ID,
    PIPE_EX,
    PIPE_WB
} PipelineStage;

typedef struct {
    TPUInstruction instruction;
    PipelineStage stage;
    int stall_cycles;
} PipelineSlot;

struct TPUPipeline {
    PipelineSlot slots[4];
    int cycle;
    bool has_data_hazard;
    bool has_structural_hazard;
};

TPUPipeline *tpu_pipeline_init(void) {
    TPUPipeline *pipe = (TPUPipeline *)malloc(sizeof(TPUPipeline));
    if (!pipe) return NULL;
    memset(pipe, 0, sizeof(TPUPipeline));
    return pipe;
}

void tpu_pipeline_destroy(TPUPipeline *pipe) {
    if (pipe) free(pipe);
}

/* Check for data hazards between instructions */
static bool check_data_hazard(TPUInstruction *prev, TPUInstruction *curr) {
    if (!prev || !curr) return false;

    /* If previous writes to accumulator and current reads from it → RAW hazard */
    if ((prev->opcode == OP_MATMUL || prev->opcode == OP_VECTOR_ADD ||
         prev->opcode == OP_VECTOR_MUL || prev->opcode == OP_ACTIVATION) &&
        (curr->opcode == OP_STORE || curr->opcode == OP_ACTIVATION ||
         curr->opcode == OP_VECTOR_ADD || curr->opcode == OP_VECTOR_MUL)) {
        return true;
    }

    /* LOAD_WEIGHT before MATMUL — not a hazard if UB addresses differ */
    if (prev->opcode == OP_LOAD_WEIGHT && curr->opcode == OP_MATMUL) {
        if (prev->dst_addr == curr->dst_addr) return true;
    }

    return false;
}

void tpu_pipeline_step(TPUPipeline *pipe, TPUInstruction *fetch_inst,
                        TPUCore *tpu) {
    if (!pipe || !tpu) return;

    /* Advance pipeline stages in reverse order (like hardware) */
    /* WB → EX → ID → IF */

    /* WB stage: complete previous EX */
    if (pipe->slots[PIPE_EX].stage == PIPE_EX) {
        pipe->slots[PIPE_WB] = pipe->slots[PIPE_EX];
        pipe->slots[PIPE_WB].stage = PIPE_WB;
    }

    /* EX stage: execute instruction if no hazard */
    if (pipe->slots[PIPE_ID].stage == PIPE_ID) {
        bool hazard = check_data_hazard(
            (pipe->slots[PIPE_EX].stage == PIPE_EX) ? &pipe->slots[PIPE_EX].instruction : NULL,
            &pipe->slots[PIPE_ID].instruction);
        if (!hazard) {
            pipe->slots[PIPE_EX] = pipe->slots[PIPE_ID];
            pipe->slots[PIPE_EX].stage = PIPE_EX;
            tpu_execute(tpu, &pipe->slots[PIPE_EX].instruction);
        } else {
            /* Stall — keep instruction in ID */
            pipe->has_data_hazard = true;
        }
    }

    /* ID stage: fetch → decode */
    if (pipe->slots[PIPE_IF].stage == PIPE_IF) {
        pipe->slots[PIPE_ID] = pipe->slots[PIPE_IF];
        pipe->slots[PIPE_ID].stage = PIPE_ID;
    }

    /* IF stage: new instruction */
    if (fetch_inst) {
        pipe->slots[PIPE_IF].instruction = *fetch_inst;
        pipe->slots[PIPE_IF].stage = PIPE_IF;
    } else {
        pipe->slots[PIPE_IF].stage = (PipelineStage)-1; /* bubble */
    }

    pipe->cycle++;
}

/* ==========================================================================
 * L7: DMA Descriptor for Asynchronous Data Transfer
 *
 * TPU uses DMA engines to overlap data transfer with computation.
 * This models a simple DMA descriptor for weight/activation loading.
 *
 * Reference: TPU ISCA 2017 §3.2 "Weight Stationary via DMA"
 * ========================================================================== */

typedef struct {
    uint32_t src_off_chip_addr;  /* host DRAM address */
    uint32_t dst_ub_addr;        /* unified buffer address */
    uint32_t size_bytes;
    bool is_complete;
} DMADescriptor;

static DMADescriptor pending_dma[16];
static int pending_dma_count = 0;

void tpu_dma_submit(uint32_t src, uint32_t dst, uint32_t size) {
    if (pending_dma_count >= 16) return;
    pending_dma[pending_dma_count].src_off_chip_addr = src;
    pending_dma[pending_dma_count].dst_ub_addr       = dst;
    pending_dma[pending_dma_count].size_bytes        = size;
    pending_dma[pending_dma_count].is_complete       = false;
    pending_dma_count++;
}

bool tpu_dma_poll_complete(int dma_id) {
    if (dma_id < 0 || dma_id >= pending_dma_count) return true;
    /* Simulate DMA completion after 1 tick */
    pending_dma[dma_id].is_complete = true;
    return true;
}

void tpu_dma_flush(void) {
    pending_dma_count = 0;
}
