#include "ooo_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ooo_init(OOOProcessor* p) {
    if (!p) return;
    memset(p, 0, sizeof(OOOProcessor));
    for (int i = 0; i < MAX_REGISTERS; i++) {
        p->reg_status[i].rob_num = (uint32_t)-1;
        p->reg_status[i].ready = true;
        p->registers[i] = 0;
    }
    for (int i = 0; i < MAX_RS; i++) {
        p->rs[i].busy = false;
        p->rs[i].qj_valid = false;
        p->rs[i].qk_valid = false;
    }
    p->cdb.valid = false;
    p->pc = 0;
    p->cycles = 0;
    p->inst_issued = 0;
    p->inst_committed = 0;
}

static int find_free_rs(OOOProcessor* p) {
    for (int i = 0; i < MAX_RS; i++) {
        if (!p->rs[i].busy) return i;
    }
    return -1;
}

static int find_free_rob(OOOProcessor* p) {
    if (p->rob_count >= MAX_ROB) return -1;
    return (int)p->rob_tail;
}

void ooo_issue(OOOProcessor* p, const Instruction* inst) {
    if (!p || !inst) return;
    if (inst->opcode == OP_NOP) {
        p->pc += 4;
        return;
    }

    int rs_idx = find_free_rs(p);
    int rob_idx = find_free_rob(p);
    if (rs_idx < 0 || rob_idx < 0) return;

    ReservationStation* rs = &p->rs[rs_idx];
    rs->busy       = true;
    rs->op         = inst->opcode;
    rs->executing  = false;
    rs->cycles_left = 1;
    rs->dest       = (uint32_t)rob_idx;

    if (inst->rs1 != 0 && !p->reg_status[inst->rs1].ready) {
        rs->qj = p->reg_status[inst->rs1].rob_num;
        rs->qj_valid = true;
        rs->vj = 0;
    } else {
        rs->vj = p->registers[inst->rs1];
        rs->qj_valid = false;
    }

    if (inst->rs2 != 0 && !p->reg_status[inst->rs2].ready) {
        rs->qk = p->reg_status[inst->rs2].rob_num;
        rs->qk_valid = true;
        rs->vk = 0;
    } else {
        rs->vk = p->registers[inst->rs2];
        rs->qk_valid = false;
    }

    if (inst->opcode == OP_SW) {
        if (inst->rs2 != 0 && !p->reg_status[inst->rs2].ready) {
            rs->qk = p->reg_status[inst->rs2].rob_num;
            rs->qk_valid = true;
        } else {
            rs->vk = p->registers[inst->rs2];
            rs->qk_valid = false;
        }
    }

    ROBEntry* rob = &p->rob[rob_idx];
    rob->busy      = true;
    rob->ready     = false;
    rob->committed = false;
    rob->inst      = *inst;
    rob->pc        = p->pc;
    rob->rob_index = (uint32_t)rob_idx;
    rob->rd        = inst->rd;
    rob->value     = 0;
    rob->is_store  = (inst->opcode == OP_SW);

    if (inst->rd != 0) {
        p->reg_status[inst->rd].rob_num = (uint32_t)rob_idx;
        p->reg_status[inst->rd].ready = false;
    }

    p->rob_tail = (p->rob_tail + 1) % MAX_ROB;
    p->rob_count++;
    p->pc += 4;
    p->inst_issued++;
}

bool ooo_execute(OOOProcessor* p) {
    if (!p) return false;
    bool any_executed = false;

    for (int i = 0; i < MAX_RS; i++) {
        ReservationStation* rs = &p->rs[i];
        if (!rs->busy || rs->executing) continue;
        if (rs->qj_valid || rs->qk_valid) continue;

        rs->executing = true;
        rs->cycles_left = 1;
        any_executed = true;
    }

    for (int i = 0; i < MAX_RS; i++) {
        ReservationStation* rs = &p->rs[i];
        if (!rs->busy || !rs->executing) continue;

        rs->cycles_left--;
        if (rs->cycles_left == 0) {
            uint32_t a = rs->vj;
            uint32_t b = rs->vk;
            int32_t sa = (int32_t)a;
            uint32_t result = 0;

            switch (rs->op) {
                case OP_ADD: result = a + b; break;
                case OP_SUB: result = a - b; break;
                case OP_AND: result = a & b; break;
                case OP_OR:  result = a | b; break;
                case OP_XOR: result = a ^ b; break;
                case OP_ADDI: result = a + (uint32_t)b; break;
                case OP_SLTI: result = (sa < (int32_t)b) ? 1 : 0; break;
                case OP_LW: {
                    uint32_t addr = a + (uint32_t)b;
                    if (addr + 4 <= MEMORY_SIZE) {
                        result = 0;
                        result |= (uint32_t)p->memory[addr];
                        result |= (uint32_t)p->memory[addr + 1] << 8;
                        result |= (uint32_t)p->memory[addr + 2] << 16;
                        result |= (uint32_t)p->memory[addr + 3] << 24;
                    }
                    break;
                }
                case OP_SW: {
                    uint32_t addr = a + (uint32_t)b;
                    if (addr + 4 <= MEMORY_SIZE) {
                        uint32_t store_val = rs->vk;
                        p->memory[addr]     = (uint8_t)(store_val & 0xFF);
                        p->memory[addr + 1] = (uint8_t)((store_val >> 8)  & 0xFF);
                        p->memory[addr + 2] = (uint8_t)((store_val >> 16) & 0xFF);
                        p->memory[addr + 3] = (uint8_t)((store_val >> 24) & 0xFF);
                    }
                    result = 0;
                    break;
                }
                default: result = 0; break;
            }

            p->cdb.value = result;
            p->cdb.tag   = rs->dest;
            p->cdb.valid = true;

            rs->busy = false;
            rs->executing = false;
        }
    }

    return any_executed;
}

void ooo_write_result(OOOProcessor* p) {
    if (!p || !p->cdb.valid) return;

    uint32_t rob_idx = p->cdb.tag;
    if (rob_idx < MAX_ROB) {
        p->rob[rob_idx].value = p->cdb.value;
        p->rob[rob_idx].ready = true;
    }

    for (int i = 0; i < MAX_REGISTERS; i++) {
        if (p->reg_status[i].rob_num == rob_idx) {
            p->reg_status[i].ready = true;
            p->registers[i] = p->cdb.value;
        }
    }

    for (int i = 0; i < MAX_RS; i++) {
        ReservationStation* rs = &p->rs[i];
        if (rs->busy && rs->qj_valid && rs->qj == rob_idx) {
            rs->vj = p->cdb.value;
            rs->qj_valid = false;
        }
        if (rs->busy && rs->qk_valid && rs->qk == rob_idx) {
            rs->vk = p->cdb.value;
            rs->qk_valid = false;
        }
    }

    p->cdb.valid = false;
}

void ooo_commit(OOOProcessor* p) {
    if (!p) return;

    while (p->rob_count > 0) {
        ROBEntry* rob = &p->rob[p->rob_head];
        if (!rob->busy || !rob->ready) break;

        if (rob->rd != 0 && !rob->is_store) {
            p->registers[rob->rd] = rob->value;
        }

        rob->busy      = false;
        rob->committed = true;
        p->rob_head    = (p->rob_head + 1) % MAX_ROB;
        p->rob_count--;
        p->inst_committed++;
    }
}

void ooo_step(OOOProcessor* p) {
    if (!p) return;
    ooo_write_result(p);
    ooo_execute(p);
    ooo_commit(p);
    p->cycles++;
}

void ooo_dump(const OOOProcessor* p) {
    if (!p) return;
    printf("===== OOO Processor State (Cycle %u) =====\n", p->cycles);
    printf("PC = 0x%08X\n", p->pc);
    printf("\n-- Reservation Stations --\n");
    for (int i = 0; i < MAX_RS; i++) {
        if (p->rs[i].busy) {
            printf("  RS[%02d]: op=%-5s vj=%-6u vk=%-6u qj=%s%-3u qk=%s%-3u dest=%-2u exec=%d\n",
                   i,
                   isa_opcode_name(p->rs[i].op),
                   p->rs[i].vj, p->rs[i].vk,
                   p->rs[i].qj_valid ? "ROB" : "   ",
                   p->rs[i].qj_valid ? p->rs[i].qj : 0,
                   p->rs[i].qk_valid ? "ROB" : "   ",
                   p->rs[i].qk_valid ? p->rs[i].qk : 0,
                   p->rs[i].dest,
                   p->rs[i].executing);
        }
    }
    printf("\n-- Reorder Buffer --\n");
    for (int i = 0; i < MAX_ROB; i++) {
        ROBEntry* rob = &p->rob[i];
        if (rob->busy) {
            printf("  ROB[%02d]: rd=%u ready=%d val=0x%X op=%s pc=0x%X\n",
                   i, rob->rd, rob->ready, rob->value,
                   isa_opcode_name(rob->inst.opcode), rob->pc);
        }
    }
    printf("\n-- Register Status --\n");
    for (int i = 0; i < MAX_REGISTERS; i++) {
        if (!p->reg_status[i].ready) {
            printf("  x%d -> ROB[%u]\n", i, p->reg_status[i].rob_num);
        }
    }
    printf("\n-- CDB --\n");
    printf("  valid=%d tag=%u value=0x%X\n", p->cdb.valid, p->cdb.tag, p->cdb.value);
    printf("  ROB: head=%u tail=%u count=%u\n", p->rob_head, p->rob_tail, p->rob_count);
    printf("  Issued: %u  Committed: %u\n", p->inst_issued, p->inst_committed);
    printf("===========================================\n");
}
