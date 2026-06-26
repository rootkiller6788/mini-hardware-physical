#ifndef TPU_ISA_H
#define TPU_ISA_H

#include <stdbool.h>
#include <stdint.h>
#include "systolic_array.h"

#define TPU_UB_MAX_MB 24
#define TPU_UB_SIZE (TPU_UB_MAX_MB * 1024 * 1024)
#define TPU_ACCUMULATOR_SIZE 4096

typedef enum {
    OP_MATMUL,
    OP_VECTOR_ADD,
    OP_VECTOR_MUL,
    OP_ACTIVATION,
    OP_LOAD_WEIGHT,
    OP_LOAD_ACT,
    OP_STORE,
    OP_SYNC
} TPUOpcode;

typedef enum {
    ACT_RELU,
    ACT_SIGMOID,
    ACT_TANH
} TPUActivation;

typedef struct {
    TPUOpcode opcode;
    TPUActivation activation;
    uint32_t src_addr;
    uint32_t dst_addr;
    uint32_t size_bytes;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
} TPUInstruction;

typedef struct {
    SystolicArray *systolic_array;
    uint8_t *unified_buffer;
    uint32_t ub_size;
    uint32_t ub_alloc_ptr;
    float accumulator_buffer[TPU_ACCUMULATOR_SIZE];
    float scalar_unit;
    float vector_unit[MAX_SYSTOLIC_SIZE];
    uint32_t pc;
    bool running;
} TPUCore;

TPUCore *tpu_init(uint32_t ub_size_mb);
void tpu_destroy(TPUCore *tpu);
void tpu_load_program(TPUCore *tpu, TPUInstruction *prog, int len);
int tpu_execute(TPUCore *tpu, TPUInstruction *inst);
void tpu_step(TPUCore *tpu);
void tpu_print_state(TPUCore *tpu);
uint32_t tpu_ub_alloc(TPUCore *tpu, uint32_t size_bytes);
void tpu_ub_free(TPUCore *tpu);

/* ---- L7: Multi-core TPU orchestration ---- */
typedef struct TPUMultiCore TPUMultiCore;
TPUMultiCore *tpu_multicore_init(int num_cores, uint32_t ub_size_mb_per_core);
void tpu_multicore_destroy(TPUMultiCore *mc);
void tpu_multicore_distribute_batch(TPUMultiCore *mc, int total_batch_size);
void tpu_multicore_execute_parallel(TPUMultiCore *mc, TPUInstruction *program, int prog_len);

/* ---- L3: Pipelined instruction execution ---- */
typedef struct TPUPipeline TPUPipeline;
TPUPipeline *tpu_pipeline_init(void);
void tpu_pipeline_destroy(TPUPipeline *pipe);
void tpu_pipeline_step(TPUPipeline *pipe, TPUInstruction *fetch_inst, TPUCore *tpu);

/* ---- L7: DMA descriptor management ---- */
void tpu_dma_submit(uint32_t src, uint32_t dst, uint32_t size);
bool tpu_dma_poll_complete(int dma_id);
void tpu_dma_flush(void);

#endif
