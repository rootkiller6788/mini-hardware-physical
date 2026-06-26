#include "isa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* isa_opcode_name(Opcode op) {
    static const char* names[OP_COUNT] = {
        "ADD",  "SUB",  "AND",  "OR",   "XOR",
        "SLL",  "SRL",  "SRA",  "SLT",  "SLTU",
        "ADDI", "ANDI", "ORI",  "XORI", "SLLI",
        "SRLI", "SRAI", "SLTI", "SLTIU",
        "LW",   "SW",   "LB",   "SB",
        "BEQ",  "BNE",  "BLT",  "BGE",  "BLTU", "BGEU",
        "JAL",  "JALR",
        "LUI",  "AUIPC",
        "NOP"
    };
    if (op < OP_COUNT) return names[op];
    return "???";
}

void isa_init(ISAContext* ctx) {
    if (!ctx) return;
    memset(ctx->registers, 0, sizeof(ctx->registers));
    memset(ctx->memory, 0, sizeof(ctx->memory));
    ctx->pc = 0;
    ctx->halted = false;
    ctx->cycles = 0;
}

Instruction isa_decode(uint32_t raw) {
    Instruction inst;
    inst.raw = raw;
    inst.opcode = OP_NOP;
    inst.rd     = 0;
    inst.rs1    = 0;
    inst.rs2    = 0;
    inst.funct3 = 0;
    inst.funct7 = 0;
    inst.immediate = 0;

    if (raw == 0) {
        inst.opcode = OP_NOP;
        return inst;
    }

    uint8_t opcode_field = raw & 0x7F;
    uint8_t rd_field     = (raw >> 7)  & 0x1F;
    uint8_t funct3_field = (raw >> 12) & 0x7;
    uint8_t rs1_field    = (raw >> 15) & 0x1F;
    uint8_t rs2_field    = (raw >> 20) & 0x1F;
    uint8_t funct7_field = (raw >> 25) & 0x7F;

    int32_t imm_i = (int32_t)((raw >> 20) & 0xFFF);
    if (imm_i & 0x800) imm_i |= 0xFFFFF000;

    int32_t imm_s = (int32_t)(((raw >> 7) & 0x1F) | ((raw >> 25) << 5));
    if (imm_s & 0x800) imm_s |= 0xFFFFF000;

    int32_t imm_b = (int32_t)(
        ((raw >> 7)  & 0x1E)  |
        ((raw >> 25) & 0x7E)  |
        ((raw >> 7)  & 0x1)   |
        ((raw >> 31) & 0x1)
    );
    if (imm_b & 0x1000) imm_b |= 0xFFFFE000;

    int32_t imm_u = (int32_t)(raw & 0xFFFFF000);

    int32_t imm_j = (int32_t)(
        ((raw >> 21) & 0x3FF) |
        ((raw >> 20) & 0x1)   |
        ((raw >> 12) & 0xFF)  |
        ((raw >> 31) & 0x1)
    );
    if (imm_j & 0x100000) imm_j |= 0xFFE00000;

    inst.rd     = (uint8_t)(rd_field & 0xF);
    inst.rs1    = (uint8_t)(rs1_field & 0xF);
    inst.rs2    = (uint8_t)(rs2_field & 0xF);
    inst.funct3 = funct3_field;
    inst.funct7 = funct7_field;

    switch (opcode_field) {
        case 0x33:
            switch (funct3_field) {
                case 0x0: inst.opcode = (funct7_field == 0x00) ? OP_ADD : OP_SUB; break;
                case 0x1: inst.opcode = OP_SLL;  break;
                case 0x2: inst.opcode = OP_SLT;  break;
                case 0x3: inst.opcode = OP_SLTU; break;
                case 0x4: inst.opcode = OP_XOR;  break;
                case 0x5: inst.opcode = (funct7_field == 0x00) ? OP_SRL : OP_SRA; break;
                case 0x6: inst.opcode = OP_OR;   break;
                case 0x7: inst.opcode = OP_AND;  break;
            }
            inst.immediate = 0;
            break;
        case 0x13:
            inst.immediate = imm_i;
            switch (funct3_field) {
                case 0x0: inst.opcode = OP_ADDI;  break;
                case 0x1: inst.opcode = OP_SLLI;  break;
                case 0x2: inst.opcode = OP_SLTI;  break;
                case 0x3: inst.opcode = OP_SLTIU; break;
                case 0x4: inst.opcode = OP_XORI;  break;
                case 0x5: inst.opcode = (funct7_field == 0x00) ? OP_SRLI : OP_SRAI; break;
                case 0x6: inst.opcode = OP_ORI;   break;
                case 0x7: inst.opcode = OP_ANDI;  break;
            }
            break;
        case 0x03:
            inst.opcode = OP_LW;
            inst.immediate = imm_i;
            break;
        case 0x23:
            inst.opcode = OP_SW;
            inst.immediate = imm_s;
            break;
        case 0x63:
            inst.immediate = imm_b;
            switch (funct3_field) {
                case 0x0: inst.opcode = OP_BEQ;  break;
                case 0x1: inst.opcode = OP_BNE;  break;
                case 0x4: inst.opcode = OP_BLT;  break;
                case 0x5: inst.opcode = OP_BGE;  break;
                case 0x6: inst.opcode = OP_BLTU; break;
                case 0x7: inst.opcode = OP_BGEU; break;
            }
            break;
        case 0x6F:
            inst.opcode = OP_JAL;
            inst.immediate = imm_j;
            break;
        case 0x67:
            inst.opcode = OP_JALR;
            inst.immediate = imm_i;
            break;
        case 0x37:
            inst.opcode = OP_LUI;
            inst.immediate = imm_u;
            break;
        case 0x17:
            inst.opcode = OP_AUIPC;
            inst.immediate = imm_u;
            break;
    }
    return inst;
}

Instruction isa_fetch(const ISAContext* ctx) {
    if (ctx->halted || ctx->pc + 4 > MEMORY_SIZE) {
        Instruction nop;
        nop.raw = 0;
        nop.opcode = OP_NOP;
        nop.rd = 0; nop.rs1 = 0; nop.rs2 = 0;
        nop.funct3 = 0; nop.funct7 = 0;
        nop.immediate = 0;
        return nop;
    }
    uint32_t raw = 0;
    raw |= (uint32_t)ctx->memory[ctx->pc];
    raw |= (uint32_t)ctx->memory[ctx->pc + 1] << 8;
    raw |= (uint32_t)ctx->memory[ctx->pc + 2] << 16;
    raw |= (uint32_t)ctx->memory[ctx->pc + 3] << 24;
    return isa_decode(raw);
}

void isa_execute(ISAContext* ctx, const Instruction* inst) {
    if (!ctx || !inst || ctx->halted) return;

    uint32_t* regs = ctx->registers;
    uint32_t  next_pc = ctx->pc + 4;
    uint32_t  a = (inst->rs1 < MAX_REGISTERS) ? regs[inst->rs1] : 0;
    uint32_t  b = (inst->rs2 < MAX_REGISTERS) ? regs[inst->rs2] : 0;
    int32_t   sa = (int32_t)a;
    int32_t   sb = (int32_t)b;
    uint32_t  rd_val = 0;

    switch (inst->opcode) {
        case OP_ADD:  rd_val = a + b; break;
        case OP_SUB:  rd_val = a - b; break;
        case OP_AND:  rd_val = a & b; break;
        case OP_OR:   rd_val = a | b; break;
        case OP_XOR:  rd_val = a ^ b; break;
        case OP_SLL:  rd_val = a << (b & 0x1F); break;
        case OP_SRL:  rd_val = a >> (b & 0x1F); break;
        case OP_SRA:  rd_val = (uint32_t)(sa >> (b & 0x1F)); break;
        case OP_SLT:  rd_val = (sa < sb) ? 1 : 0; break;
        case OP_SLTU: rd_val = (a < b) ? 1 : 0; break;
        case OP_ADDI: rd_val = a + (uint32_t)inst->immediate; break;
        case OP_ANDI: rd_val = a & (uint32_t)inst->immediate; break;
        case OP_ORI:  rd_val = a | (uint32_t)inst->immediate; break;
        case OP_XORI: rd_val = a ^ (uint32_t)inst->immediate; break;
        case OP_SLLI: rd_val = a << (inst->immediate & 0x1F); break;
        case OP_SRLI: rd_val = a >> (inst->immediate & 0x1F); break;
        case OP_SRAI: rd_val = (uint32_t)(sa >> (inst->immediate & 0x1F)); break;
        case OP_SLTI: rd_val = (sa < inst->immediate) ? 1 : 0; break;
        case OP_SLTIU: rd_val = (a < (uint32_t)inst->immediate) ? 1 : 0; break;
        case OP_LUI:  rd_val = (uint32_t)inst->immediate; break;
        case OP_AUIPC: rd_val = ctx->pc + (uint32_t)inst->immediate; break;
        case OP_JAL:
            rd_val = ctx->pc + 4;
            next_pc = ctx->pc + (uint32_t)inst->immediate;
            break;
        case OP_JALR:
            rd_val = ctx->pc + 4;
            next_pc = (a + (uint32_t)inst->immediate) & ~1u;
            break;
        case OP_BEQ:  if (a == b)           next_pc = ctx->pc + (uint32_t)inst->immediate; break;
        case OP_BNE:  if (a != b)           next_pc = ctx->pc + (uint32_t)inst->immediate; break;
        case OP_BLT:  if (sa < sb)          next_pc = ctx->pc + (uint32_t)inst->immediate; break;
        case OP_BGE:  if (sa >= sb)         next_pc = ctx->pc + (uint32_t)inst->immediate; break;
        case OP_BLTU: if (a < b)            next_pc = ctx->pc + (uint32_t)inst->immediate; break;
        case OP_BGEU: if (a >= b)           next_pc = ctx->pc + (uint32_t)inst->immediate; break;
        case OP_LW: {
            uint32_t addr = a + (uint32_t)inst->immediate;
            if (addr + 4 <= MEMORY_SIZE) {
                rd_val = 0;
                rd_val |= (uint32_t)ctx->memory[addr];
                rd_val |= (uint32_t)ctx->memory[addr + 1] << 8;
                rd_val |= (uint32_t)ctx->memory[addr + 2] << 16;
                rd_val |= (uint32_t)ctx->memory[addr + 3] << 24;
            }
            break;
        }
        case OP_SW: {
            uint32_t addr = a + (uint32_t)inst->immediate;
            if (addr + 4 <= MEMORY_SIZE) {
                ctx->memory[addr]     = (uint8_t)(b & 0xFF);
                ctx->memory[addr + 1] = (uint8_t)((b >> 8)  & 0xFF);
                ctx->memory[addr + 2] = (uint8_t)((b >> 16) & 0xFF);
                ctx->memory[addr + 3] = (uint8_t)((b >> 24) & 0xFF);
            }
            break;
        }
        case OP_NOP:
            break;
        default:
            break;
    }

    if (inst->rd > 0 && inst->opcode != OP_SW &&
        inst->opcode != OP_BEQ && inst->opcode != OP_BNE &&
        inst->opcode != OP_BLT && inst->opcode != OP_BGE &&
        inst->opcode != OP_BLTU && inst->opcode != OP_BGEU &&
        inst->opcode != OP_NOP) {
        regs[inst->rd] = rd_val;
    }

    ctx->pc = next_pc;
    ctx->cycles++;
}

void isa_step(ISAContext* ctx) {
    if (!ctx || ctx->halted) return;
    Instruction inst = isa_fetch(ctx);
    isa_execute(ctx, &inst);
}

void isa_load_program(ISAContext* ctx, const uint32_t* program, size_t len) {
    if (!ctx || !program || len == 0) return;
    size_t bytes = len * sizeof(uint32_t);
    if (bytes > MEMORY_SIZE) bytes = MEMORY_SIZE;
    memcpy(ctx->memory, program, bytes);
}

void isa_dump_registers(const ISAContext* ctx) {
    if (!ctx) return;
    printf("--- Register Dump ---\n");
    for (int i = 0; i < MAX_REGISTERS; i++) {
        printf("  x%-2d = 0x%08X  (%u)", i, ctx->registers[i], ctx->registers[i]);
        if ((i + 1) % 4 == 0) printf("\n");
    }
    printf("  PC  = 0x%08X\n", ctx->pc);
    printf("  Cycles: %llu\n", (unsigned long long)ctx->cycles);
    printf("----------------------\n");
}
