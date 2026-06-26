#ifndef ISA_H
#define ISA_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_REGISTERS   16
#define MEMORY_SIZE     4096
#define INSTR_LEN       4

typedef enum {
    OP_ADD,   OP_SUB,   OP_AND,   OP_OR,    OP_XOR,
    OP_SLL,   OP_SRL,   OP_SRA,   OP_SLT,   OP_SLTU,
    OP_ADDI,  OP_ANDI,  OP_ORI,   OP_XORI,  OP_SLLI,
    OP_SRLI,  OP_SRAI,  OP_SLTI,  OP_SLTIU,
    OP_LW,    OP_SW,    OP_LB,    OP_SB,
    OP_BEQ,   OP_BNE,   OP_BLT,   OP_BGE,   OP_BLTU,  OP_BGEU,
    OP_JAL,   OP_JALR,
    OP_LUI,   OP_AUIPC,
    OP_NOP,
    OP_COUNT
} Opcode;

typedef struct {
    uint32_t raw;
    Opcode   opcode;
    uint8_t  rd;
    uint8_t  rs1;
    uint8_t  rs2;
    uint8_t  funct3;
    uint8_t  funct7;
    int32_t  immediate;
} Instruction;

typedef struct {
    uint32_t registers[MAX_REGISTERS];
    uint32_t pc;
    uint8_t  memory[MEMORY_SIZE];
    bool     halted;
    uint64_t cycles;
} ISAContext;

void        isa_init(ISAContext* ctx);
Instruction isa_fetch(const ISAContext* ctx);
Instruction isa_decode(uint32_t raw);
void        isa_execute(ISAContext* ctx, const Instruction* inst);
void        isa_step(ISAContext* ctx);
void        isa_load_program(ISAContext* ctx, const uint32_t* program, size_t len);
void        isa_dump_registers(const ISAContext* ctx);
const char* isa_opcode_name(Opcode op);

#endif
