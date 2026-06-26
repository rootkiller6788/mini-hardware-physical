#ifndef OOO_EXEC_H
#define OOO_EXEC_H

#include <stdbool.h>
#include <stdint.h>
#include "isa.h"

#define MAX_RS    16
#define MAX_ROB   32
#define NUM_ALUS  2
#define NUM_LSU   1

typedef struct {
    bool     busy;
    Opcode   op;
    uint32_t vj;
    uint32_t vk;
    uint32_t qj;
    uint32_t qk;
    uint32_t dest;
    bool     qj_valid;
    bool     qk_valid;
    uint32_t addr;
    bool     executing;
    uint32_t cycles_left;
} ReservationStation;

typedef struct {
    uint32_t rob_num;
    bool     ready;
} RegisterStatus;

typedef struct {
    uint32_t value;
    uint32_t tag;
    bool     valid;
} CDBEntry;

typedef struct {
    bool     busy;
    bool     ready;
    bool     committed;
    Instruction inst;
    uint32_t value;
    uint32_t pc;
    uint32_t rob_index;
    uint8_t  rd;
    bool     is_store;
    uint32_t store_addr;
    uint32_t store_data;
} ROBEntry;

typedef struct {
    ReservationStation rs[MAX_RS];
    RegisterStatus     reg_status[MAX_REGISTERS];
    ROBEntry           rob[MAX_ROB];
    uint32_t           rob_head;
    uint32_t           rob_tail;
    uint32_t           rob_count;
    CDBEntry           cdb;
    uint32_t           registers[MAX_REGISTERS];
    uint8_t            memory[MEMORY_SIZE];
    uint32_t           pc;
    uint32_t           cycles;
    uint32_t           inst_issued;
    uint32_t           inst_committed;
} OOOProcessor;

void ooo_init(OOOProcessor* p);
void ooo_issue(OOOProcessor* p, const Instruction* inst);
bool ooo_execute(OOOProcessor* p);
void ooo_write_result(OOOProcessor* p);
void ooo_commit(OOOProcessor* p);
void ooo_step(OOOProcessor* p);
void ooo_dump(const OOOProcessor* p);

#endif
