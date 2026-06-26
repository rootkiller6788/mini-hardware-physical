#include "pipeline.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pipeline_init(Pipeline* p) {
    if (!p) return;
    memset(p, 0, sizeof(Pipeline));
    isa_init(&p->isa);
    p->if_id.valid = false;
    p->id_ex.valid = false;
    p->ex_mem.valid = false;
    p->mem_wb.valid = false;
}

void forwarding_unit(Pipeline* p) {
    if (!p) return;
    p->forward_a = false;
    p->forward_b = false;

    if (p->ex_mem.valid && p->ex_mem.reg_write_en &&
        p->ex_mem.rd != 0 && p->ex_mem.rd == p->id_ex.rs1) {
        p->forward_a = true;
        p->forward_a_val = p->ex_mem.alu_result;
    }
    if (p->mem_wb.valid && p->mem_wb.reg_write_en &&
        p->mem_wb.rd != 0 && p->mem_wb.rd == p->id_ex.rs1 &&
        !(p->ex_mem.valid && p->ex_mem.reg_write_en &&
          p->ex_mem.rd != 0 && p->ex_mem.rd == p->id_ex.rs1)) {
        p->forward_a = true;
        p->forward_a_val = p->mem_wb.alu_result;
    }

    if (p->ex_mem.valid && p->ex_mem.reg_write_en &&
        p->ex_mem.rd != 0 && p->ex_mem.rd == p->id_ex.rs2) {
        p->forward_b = true;
        p->forward_b_val = p->ex_mem.alu_result;
    }
    if (p->mem_wb.valid && p->mem_wb.reg_write_en &&
        p->mem_wb.rd != 0 && p->mem_wb.rd == p->id_ex.rs2 &&
        !(p->ex_mem.valid && p->ex_mem.reg_write_en &&
          p->ex_mem.rd != 0 && p->ex_mem.rd == p->id_ex.rs2)) {
        p->forward_b = true;
        p->forward_b_val = p->mem_wb.alu_result;
    }
}

static bool detect_load_use_hazard(Pipeline* p) {
    if (p->id_ex.mem_read &&
        (p->id_ex.rd == p->if_id.rs1 || p->id_ex.rd == p->if_id.rs2)) {
        return true;
    }
    return false;
}

void pipeline_fetch(Pipeline* p) {
    if (p->isa.halted || p->isa.pc + 4 > MEMORY_SIZE) {
        p->if_id.valid = false;
        return;
    }
    Instruction inst = isa_fetch(&p->isa);
    p->if_id.inst  = inst;
    p->if_id.pc    = p->isa.pc;
    p->if_id.next_pc = p->isa.pc + 4;
    p->if_id.valid = true;
    p->if_id.stalled = false;
    p->isa.pc += 4;
}

void pipeline_decode(Pipeline* p) {
    if (!p->if_id.valid) {
        p->id_ex.valid = false;
        return;
    }

    Instruction inst = p->if_id.inst;

    p->id_ex.inst  = inst;
    p->id_ex.pc    = p->if_id.pc;
    p->id_ex.rd    = inst.rd;
    p->id_ex.rs1   = inst.rs1;
    p->id_ex.rs2   = inst.rs2;
    p->id_ex.imm   = inst.immediate;

    p->id_ex.rs1_val = p->isa.registers[inst.rs1 & 0xF];
    p->id_ex.rs2_val = p->isa.registers[inst.rs2 & 0xF];

    p->id_ex.reg_write_en = 0;
    p->id_ex.mem_read  = false;
    p->id_ex.mem_write = false;
    p->id_ex.branch_taken = false;

    switch (inst.opcode) {
        case OP_ADD: case OP_SUB: case OP_AND: case OP_OR: case OP_XOR:
        case OP_SLL: case OP_SRL: case OP_SRA: case OP_SLT: case OP_SLTU:
        case OP_ADDI: case OP_ANDI: case OP_ORI: case OP_XORI:
        case OP_SLLI: case OP_SRLI: case OP_SRAI: case OP_SLTI: case OP_SLTIU:
        case OP_LUI: case OP_AUIPC:
            p->id_ex.reg_write_en = 1;
            break;
        case OP_LW: case OP_LB:
            p->id_ex.reg_write_en = 1;
            p->id_ex.mem_read = true;
            break;
        case OP_SW: case OP_SB:
            p->id_ex.reg_write_en = 0;
            p->id_ex.mem_write = true;
            break;
        case OP_JAL: case OP_JALR:
            p->id_ex.reg_write_en = 1;
            p->id_ex.branch_taken = true;
            break;
        default:
            break;
    }
    p->id_ex.valid = true;
    p->if_id.valid = false;
}

void pipeline_execute(Pipeline* p) {
    if (!p->id_ex.valid) {
        p->ex_mem.valid = false;
        return;
    }

    forwarding_unit(p);

    uint32_t a = p->forward_a ? p->forward_a_val : p->id_ex.rs1_val;
    uint32_t b = p->forward_b ? p->forward_b_val : p->id_ex.rs2_val;
    int32_t  sa = (int32_t)a;
    int32_t  sb = (int32_t)b;
    uint32_t result = 0;

    Instruction inst = p->id_ex.inst;
    switch (inst.opcode) {
        case OP_ADD: case OP_ADDI: result = a + b; break;
        case OP_SUB: result = a - b; break;
        case OP_AND: case OP_ANDI: result = a & b; break;
        case OP_OR:  case OP_ORI:  result = a | b; break;
        case OP_XOR: case OP_XORI: result = a ^ b; break;
        case OP_SLL: case OP_SLLI: result = a << (b & 0x1F); break;
        case OP_SRL: case OP_SRLI: result = a >> (b & 0x1F); break;
        case OP_SRA: case OP_SRAI: result = (uint32_t)(sa >> (b & 0x1F)); break;
        case OP_SLT: case OP_SLTI: result = (sa < sb) ? 1 : 0; break;
        case OP_SLTU: case OP_SLTIU: result = (a < (uint32_t)inst.immediate) ? 1 : 0; break;
        case OP_LUI: case OP_AUIPC:
            result = (inst.opcode == OP_LUI) ? (uint32_t)inst.immediate : p->id_ex.pc + (uint32_t)inst.immediate;
            break;
        case OP_LW: case OP_LB:
            result = a + (uint32_t)inst.immediate;
            break;
        case OP_SW: case OP_SB:
            result = a + (uint32_t)inst.immediate;
            break;
        case OP_BEQ:
            if (a == b) { p->id_ex.branch_taken = true; result = p->id_ex.pc + (uint32_t)inst.immediate; }
            else { p->id_ex.branch_taken = false; result = p->id_ex.pc + 4; }
            break;
        case OP_BNE:
            if (a != b) { p->id_ex.branch_taken = true; result = p->id_ex.pc + (uint32_t)inst.immediate; }
            else { p->id_ex.branch_taken = false; result = p->id_ex.pc + 4; }
            break;
        case OP_JAL: case OP_JALR:
            result = (inst.opcode == OP_JAL) ? p->id_ex.pc + (uint32_t)inst.immediate : (a + (uint32_t)inst.immediate) & ~1u;
            break;
        default: break;
    }

    p->ex_mem.alu_result  = result;
    p->ex_mem.reg_write_en = p->id_ex.reg_write_en;
    p->ex_mem.rd           = p->id_ex.rd;
    p->ex_mem.rs2_val      = b;
    p->ex_mem.mem_read     = p->id_ex.mem_read;
    p->ex_mem.mem_write    = p->id_ex.mem_write;
    p->ex_mem.branch_taken = p->id_ex.branch_taken;
    p->ex_mem.pc           = p->id_ex.pc;
    p->ex_mem.valid        = true;

    if (p->id_ex.branch_taken) {
        p->isa.pc = result;
        p->if_id.valid = false;
        p->bubble_count++;
    }

    p->id_ex.valid = false;
}

void pipeline_memory(Pipeline* p) {
    if (!p->ex_mem.valid) {
        p->mem_wb.valid = false;
        return;
    }

    p->mem_wb.alu_result   = p->ex_mem.alu_result;
    p->mem_wb.reg_write_en = p->ex_mem.reg_write_en;
    p->mem_wb.rd           = p->ex_mem.rd;
    p->mem_wb.pc           = p->ex_mem.pc;
    p->mem_wb.valid        = true;

    if (p->ex_mem.mem_read) {
        uint32_t addr = p->ex_mem.alu_result;
        if (addr + 4 <= MEMORY_SIZE) {
            uint32_t val = 0;
            val |= (uint32_t)p->isa.memory[addr];
            val |= (uint32_t)p->isa.memory[addr + 1] << 8;
            val |= (uint32_t)p->isa.memory[addr + 2] << 16;
            val |= (uint32_t)p->isa.memory[addr + 3] << 24;
            p->mem_wb.alu_result = val;
        }
    }

    if (p->ex_mem.mem_write) {
        uint32_t addr = p->ex_mem.alu_result;
        uint32_t val  = p->ex_mem.rs2_val;
        if (addr + 4 <= MEMORY_SIZE) {
            p->isa.memory[addr]     = (uint8_t)(val & 0xFF);
            p->isa.memory[addr + 1] = (uint8_t)((val >> 8)  & 0xFF);
            p->isa.memory[addr + 2] = (uint8_t)((val >> 16) & 0xFF);
            p->isa.memory[addr + 3] = (uint8_t)((val >> 24) & 0xFF);
        }
    }

    p->ex_mem.valid = false;
}

void pipeline_writeback(Pipeline* p) {
    if (!p->mem_wb.valid) return;

    if (p->mem_wb.reg_write_en && p->mem_wb.rd != 0) {
        p->isa.registers[p->mem_wb.rd] = p->mem_wb.alu_result;
    }

    p->mem_wb.valid = false;
}

void pipeline_cycle(Pipeline* p) {
    if (!p || p->isa.halted) return;

    if (detect_load_use_hazard(p)) {
        p->stall_count++;
        pipeline_writeback(p);
        pipeline_memory(p);
        return;
    }

    pipeline_writeback(p);
    pipeline_memory(p);
    pipeline_execute(p);
    pipeline_decode(p);
    pipeline_fetch(p);

    p->cycles++;
}

void pipeline_load_program(Pipeline* p, const uint32_t* prog, size_t len) {
    isa_load_program(&p->isa, prog, len);
}

void pipeline_dump(const Pipeline* p) {
    if (!p) return;
    printf("------- Pipeline State (Cycle %u) -------\n", p->cycles);
    printf("IF/ID : valid=%d stalled=%d PC=0x%X op=%s\n",
           p->if_id.valid, p->if_id.stalled, p->if_id.pc,
           isa_opcode_name(p->if_id.inst.opcode));
    printf("ID/EX : valid=%d PC=0x%X op=%s rs1=%u rs2=%u\n",
           p->id_ex.valid, p->id_ex.pc,
           isa_opcode_name(p->id_ex.inst.opcode),
           p->id_ex.rs1_val, p->id_ex.rs2_val);
    printf("EX/MEM: valid=%d ALU=0x%X rd=%u mem_r=%d mem_w=%d\n",
           p->ex_mem.valid, p->ex_mem.alu_result, p->ex_mem.rd,
           p->ex_mem.mem_read, p->ex_mem.mem_write);
    printf("MEM/WB: valid=%d ALU=0x%X rd=%u reg_wr=%d\n",
           p->mem_wb.valid, p->mem_wb.alu_result, p->mem_wb.rd,
           p->mem_wb.reg_write_en);
    printf("Forwarding: A=%d(%d) B=%d(%d)\n",
           p->forward_a, p->forward_a_val, p->forward_b, p->forward_b_val);
    printf("Stalls: %u  Bubbles: %u\n", p->stall_count, p->bubble_count);
    printf("-------------------------------------------\n");
}
