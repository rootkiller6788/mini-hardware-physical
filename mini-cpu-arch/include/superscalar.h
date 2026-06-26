#ifndef SUPERSCALAR_H
#define SUPERSCALAR_H

#include <stdbool.h>
#include <stdint.h>
#include "isa.h"

#define MAX_DISPATCH_WIDTH 2
#define MAX_ROB_ENTRIES    32
#define MAX_ISSUE_WIDTH    2

typedef struct {
    bool        valid;
    bool        ready_src1;
    bool        ready_src2;
    Instruction inst;
    uint32_t    pc;
    uint8_t     rd;
    uint8_t     rs1;
    uint8_t     rs2;
    uint32_t    rs1_val;
    uint32_t    rs2_val;
    uint32_t    rob_index;
} DispatchSlot;

typedef struct {
    bool        busy;
    bool        done;
    bool        committed;
    Instruction inst;
    uint8_t     rd;
    uint32_t    value;
    uint32_t    pc;
    uint32_t    rob_index;
} ROBEntry;

typedef struct {
    DispatchSlot issue_queue[MAX_DISPATCH_WIDTH];
    ROBEntry     rob[MAX_ROB_ENTRIES];
    uint32_t     rob_head;
    uint32_t     rob_tail;
    uint32_t     rob_count;
    uint32_t     registers[MAX_REGISTERS];
    uint32_t     reg_rob_mapping[MAX_REGISTERS];
    uint32_t     pc;
    uint8_t      memory[MEMORY_SIZE];
    uint32_t     cycles;
    uint32_t     instructions_issued;
    uint32_t     instructions_committed;
} Superscalar;

void superscalar_init(Superscalar* s);
void superscalar_issue(Superscalar* s, const Instruction* insts, size_t count);
void superscalar_dispatch(Superscalar* s);
void superscalar_commit(Superscalar* s);
void superscalar_step(Superscalar* s);
void superscalar_dump(const Superscalar* s);

#endif
