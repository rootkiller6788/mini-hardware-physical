#include "superscalar.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void superscalar_init(Superscalar* s) {
    if (!s) return;
    memset(s, 0, sizeof(Superscalar));
    for (uint32_t i = 0; i < MAX_REGISTERS; i++) {
        s->reg_rob_mapping[i] = (uint32_t)-1;
    }
    s->instructions_issued = 0;
    s->instructions_committed = 0;
}

static int find_free_rob(Superscalar* s) {
    for (int i = 0; i < MAX_ROB_ENTRIES; i++) {
        if (!s->rob[i].busy) return i;
    }
    return -1;
}

void superscalar_issue(Superscalar* s, const Instruction* insts, size_t count) {
    if (!s || !insts) return;
    size_t issued = 0;

    for (size_t i = 0; i < count && issued < MAX_ISSUE_WIDTH; i++) {
        if (s->rob_count >= MAX_ROB_ENTRIES) break;

        int rob_slot = find_free_rob(s);
        if (rob_slot < 0) break;

        Instruction inst = insts[i];
        s->rob[rob_slot].busy       = true;
        s->rob[rob_slot].done       = false;
        s->rob[rob_slot].committed  = false;
        s->rob[rob_slot].inst       = inst;
        s->rob[rob_slot].rd         = inst.rd;
        s->rob[rob_slot].pc         = s->pc;
        s->rob[rob_slot].value      = 0;
        s->rob[rob_slot].rob_index  = (uint32_t)rob_slot;

        if (inst.rd != 0) {
            s->reg_rob_mapping[inst.rd] = (uint32_t)rob_slot;
        }

        s->rob_count++;
        s->instructions_issued++;
        s->pc += 4;
        issued++;
    }

    superscalar_dispatch(s);
}

void superscalar_dispatch(Superscalar* s) {
    if (!s) return;

    for (int i = 0; i < MAX_ROB_ENTRIES; i++) {
        ROBEntry* entry = &s->rob[i];
        if (!entry->busy || entry->done) continue;

        Instruction* inst = &entry->inst;
        uint32_t a = inst->rs1 < MAX_REGISTERS ? s->registers[inst->rs1] : 0;
        uint32_t b = inst->rs2 < MAX_REGISTERS ? s->registers[inst->rs2] : 0;

        if (inst->rs1 != 0 && s->reg_rob_mapping[inst->rs1] != (uint32_t)-1) {
            uint32_t rob_idx = s->reg_rob_mapping[inst->rs1];
            if (s->rob[rob_idx].busy && s->rob[rob_idx].done) {
                a = s->rob[rob_idx].value;
            } else {
                continue;
            }
        }
        if (inst->rs2 != 0 && s->reg_rob_mapping[inst->rs2] != (uint32_t)-1) {
            uint32_t rob_idx = s->reg_rob_mapping[inst->rs2];
            if (s->rob[rob_idx].busy && s->rob[rob_idx].done) {
                b = s->rob[rob_idx].value;
            } else {
                continue;
            }
        }

        int32_t sa = (int32_t)a;
        (void)b; /* sb only needed for certain opcodes */
        uint32_t result = 0;

        switch (inst->opcode) {
            case OP_ADD:  result = a + b; break;
            case OP_SUB:  result = a - b; break;
            case OP_AND:  result = a & b; break;
            case OP_OR:   result = a | b; break;
            case OP_XOR:  result = a ^ b; break;
            case OP_ADDI: result = a + (uint32_t)inst->immediate; break;
            case OP_SLTI: result = (sa < (int32_t)inst->immediate) ? 1 : 0; break;
            case OP_LW: {
                uint32_t addr = a + (uint32_t)inst->immediate;
                if (addr + 4 <= MEMORY_SIZE) {
                    result = 0;
                    result |= (uint32_t)s->memory[addr];
                    result |= (uint32_t)s->memory[addr + 1] << 8;
                    result |= (uint32_t)s->memory[addr + 2] << 16;
                    result |= (uint32_t)s->memory[addr + 3] << 24;
                }
                break;
            }
            case OP_SW: {
                uint32_t addr = a + (uint32_t)inst->immediate;
                if (addr + 4 <= MEMORY_SIZE) {
                    s->memory[addr]     = (uint8_t)(b & 0xFF);
                    s->memory[addr + 1] = (uint8_t)((b >> 8)  & 0xFF);
                    s->memory[addr + 2] = (uint8_t)((b >> 16) & 0xFF);
                    s->memory[addr + 3] = (uint8_t)((b >> 24) & 0xFF);
                }
                result = 0;
                break;
            }
            default: result = 0; break;
        }

        entry->value = result;
        entry->done  = true;
    }
}

void superscalar_commit(Superscalar* s) {
    if (!s) return;

    int commit_count = 0;
    for (int i = 0; i < MAX_ROB_ENTRIES && commit_count < MAX_DISPATCH_WIDTH; i++) {
        int idx = (int)((s->rob_head + (uint32_t)i) % MAX_ROB_ENTRIES);
        ROBEntry* entry = &s->rob[idx];
        if (!entry->busy) continue;
        if (!entry->done) break;
        if (entry->committed) break;

        if (entry->rd != 0) {
            s->registers[entry->rd] = entry->value;
        }

        entry->busy      = false;
        entry->committed = true;
        s->rob_count--;
        s->instructions_committed++;
        s->rob_head = (s->rob_head + 1) % MAX_ROB_ENTRIES;
        commit_count++;
    }
}

void superscalar_step(Superscalar* s) {
    if (!s) return;
    superscalar_dispatch(s);
    superscalar_commit(s);
    s->cycles++;
}

void superscalar_dump(const Superscalar* s) {
    if (!s) return;
    printf("----- Superscalar State (Cycle %u) -----\n", s->cycles);
    printf("PC = 0x%08X\n", s->pc);
    printf("ROB: head=%u tail=%u count=%u\n", s->rob_head, s->rob_tail, s->rob_count);
    for (int i = 0; i < MAX_ROB_ENTRIES; i++) {
        if (s->rob[i].busy) {
            printf("  ROB[%d]: busy rd=%u done=%d val=0x%X op=%s\n",
                   i, s->rob[i].rd, s->rob[i].done, s->rob[i].value,
                   isa_opcode_name(s->rob[i].inst.opcode));
        }
    }
    printf("Registers: ");
    for (int i = 0; i < 16; i++) {
        printf("x%d=%-5u ", i, s->registers[i]);
    }
    printf("\n");
    printf("Issued: %u Committed: %u\n", s->instructions_issued, s->instructions_committed);
    printf("------------------------------------------\n");
}
