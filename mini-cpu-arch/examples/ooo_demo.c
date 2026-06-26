#include <stdio.h>
#include <stdint.h>
#include "ooo_exec.h"

static uint32_t rtype(uint8_t op, uint8_t rd, uint8_t rs1, uint8_t rs2,
                      uint8_t f3, uint8_t f7) {
    return (uint32_t)((f7 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op);
}

static uint32_t itype(uint8_t op, uint8_t rd, uint8_t rs1, int16_t imm, uint8_t f3) {
    return (uint32_t)(((imm & 0xFFF) << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op);
}

static uint32_t raw_to_inst_tomasulo(uint32_t raw) {
    return raw;
}

int main(void) {
    printf("=== mini-cpu-arch: OOO Execution Demo ===\n\n");

    OOOProcessor proc;
    ooo_init(&proc);

    uint32_t raw_prog[] = {
        itype(0x13, 1, 0, 10, 0),        /* ADDI x1, x0, 10   */
        itype(0x13, 2, 0, 20, 0),        /* ADDI x2, x0, 20   */
        rtype(0x33, 3, 1, 2, 0, 0x00),   /* ADD  x3, x1, x2   */
        rtype(0x33, 4, 3, 1, 0, 0x20),   /* SUB  x4, x3, x1   */
        rtype(0x33, 5, 3, 2, 6, 0x00),   /* OR   x5, x3, x2   */
        rtype(0x33, 6, 4, 5, 7, 0x00),   /* AND  x6, x4, x5   */
    };
    size_t n = sizeof(raw_prog) / sizeof(raw_prog[0]);
    Instruction prog[6];
    for (size_t i = 0; i < n; i++) {
        prog[i] = isa_decode(raw_prog[i]);
    }

    printf("Program: 6 instructions\n");
    printf("  ADDI x1,x0,10  ADDI x2,x0,20  ADD x3,x1,x2\n");
    printf("  SUB x4,x3,x1   OR x5,x3,x2    AND x6,x4,x5\n\n");

    for (size_t i = 0; i < n; i++) {
        printf("--- Issuing instruction %zu: %s ---\n",
               i + 1, isa_opcode_name(prog[i].opcode));
        ooo_issue(&proc, &prog[i]);
        ooo_dump(&proc);
    }

    printf("\n--- Executing (running cycles until all committed) ---\n");
    int max_cycles = 20;
    for (int c = 0; c < max_cycles; c++) {
        ooo_step(&proc);
        ooo_dump(&proc);
        if (proc.inst_committed >= n) {
            printf("All instructions committed at cycle %d!\n", c + 1);
            break;
        }
    }

    printf("\n--- Final Register Values ---\n");
    for (int i = 0; i < 16; i++) {
        printf("  x%d = %u\n", i, proc.registers[i]);
    }
    printf("Expected: x1=10 x2=20 x3=30 x4=20 x5=30 x6=10\n");

    return 0;
}
