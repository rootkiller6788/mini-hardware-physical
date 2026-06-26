#ifndef PIPELINE_H
#define PIPELINE_H

#include <stdbool.h>
#include <stdint.h>
#include "isa.h"

typedef enum {
    STAGE_IF  = 0,
    STAGE_ID  = 1,
    STAGE_EX  = 2,
    STAGE_MEM = 3,
    STAGE_WB  = 4,
    STAGE_COUNT
} PipelineStage;

typedef struct {
    bool        valid;
    bool        stalled;
    Instruction inst;
    uint32_t    pc;
    uint32_t    next_pc;
    uint8_t     reg_write_en;
    uint8_t     rd;
    uint8_t     rs1;
    uint8_t     rs2;
    uint32_t    rs1_val;
    uint32_t    rs2_val;
    int32_t     imm;
    uint32_t    alu_result;
    uint32_t    mem_data;
    bool        mem_read;
    bool        mem_write;
    bool        branch_taken;
} PipelineReg;

typedef struct {
    PipelineReg if_id;
    PipelineReg id_ex;
    PipelineReg ex_mem;
    PipelineReg mem_wb;
    ISAContext   isa;
    bool         forward_a;
    bool         forward_b;
    uint32_t     forward_a_val;
    uint32_t     forward_b_val;
    uint32_t     cycles;
    uint32_t     stall_count;
    uint32_t     bubble_count;
} Pipeline;

void pipeline_init(Pipeline* p);
void pipeline_fetch(Pipeline* p);
void pipeline_decode(Pipeline* p);
void pipeline_execute(Pipeline* p);
void pipeline_memory(Pipeline* p);
void pipeline_writeback(Pipeline* p);
void pipeline_cycle(Pipeline* p);
void pipeline_load_program(Pipeline* p, const uint32_t* prog, size_t len);
void forwarding_unit(Pipeline* p);
void pipeline_dump(const Pipeline* p);

#endif
