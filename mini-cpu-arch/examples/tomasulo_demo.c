#include <stdio.h>
#include <stdint.h>
#include "ooo_exec.h"
#include "isa.h"

static uint32_t rtype(uint8_t op, uint8_t rd, uint8_t rs1, uint8_t rs2,
                      uint8_t f3, uint8_t f7) {
    return (uint32_t)((f7 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op);
}

static uint32_t itype(uint8_t op, uint8_t rd, uint8_t rs1, int16_t imm, uint8_t f3) {
    return (uint32_t)(((imm & 0xFFF) << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op);
}

static void print_phase(const char* phase, int num) {
    printf("\n========================================\n");
    printf("  Phase %d: %s\n", num, phase);
    printf("========================================\n");
}

int main(void) {
    printf("=== mini-cpu-arch: Tomasulo Algorithm Detailed Demo ===\n\n");

    OOOProcessor proc;
    ooo_init(&proc);

    uint32_t raw_prog[] = {
        itype(0x13, 1, 0, 5, 0),          /* ADDI x1, x0, 5    */
        itype(0x13, 2, 0, 3, 0),          /* ADDI x2, x0, 3    */
        rtype(0x33, 3, 1, 2, 0, 0x00),    /* ADD  x3, x1, x2   */
        rtype(0x33, 4, 3, 1, 0, 0x00),    /* ADD  x4, x3, x1   */
        rtype(0x33, 5, 1, 2, 0, 0x20),    /* SUB  x5, x1, x2   */
        rtype(0x33, 6, 4, 5, 7, 0x00),    /* AND  x6, x4, x5   */
    };
    size_t n = sizeof(raw_prog) / sizeof(raw_prog[0]);
    Instruction prog[6];
    for (size_t i = 0; i < n; i++) {
        prog[i] = isa_decode(raw_prog[i]);
    }

    printf("Program:\n");
    printf("  I1: ADDI x1, x0, 5\n");
    printf("  I2: ADDI x2, x0, 3\n");
    printf("  I3: ADD  x3, x1, x2   (RAW on x1,x2)\n");
    printf("  I4: ADD  x4, x3, x1   (RAW on x3)\n");
    printf("  I5: SUB  x5, x1, x2   (no RAW with x1,x2 after I1,I2)\n");
    printf("  I6: AND  x6, x4, x5   (RAW on x4,x5)\n\n");

    print_phase("Issue all instructions", 1);
    for (size_t i = 0; i < n; i++) {
        printf("Issuing I%zu: %s rd=x%d rs1=x%d rs2=x%d\n",
               i + 1,
               isa_opcode_name(prog[i].opcode),
               prog[i].rd, prog[i].rs1, prog[i].rs2);
        ooo_issue(&proc, &prog[i]);
    }
    ooo_dump(&proc);

    print_phase("Execute (functional units become free)", 2);
    ooo_execute(&proc);
    ooo_dump(&proc);

    print_phase("Write Result (CDB broadcast)", 3);
    ooo_write_result(&proc);
    ooo_dump(&proc);

    print_phase("Execute remaining ready ops", 4);
    for (int e = 0; e < 5; e++) {
        bool did = ooo_execute(&proc);
        ooo_write_result(&proc);
        if (!did && !proc.cdb.valid) break;
        printf("\n  After execution round %d:\n", e + 1);
        ooo_dump(&proc);
    }

    print_phase("Commit in-order", 5);
    ooo_commit(&proc);
    ooo_dump(&proc);

    print_phase("Final commit", 6);
    for (int c = 0; c < 10; c++) {
        ooo_step(&proc);
        if (proc.inst_committed >= n) {
            printf("All %u instructions committed!\n", proc.inst_committed);
            break;
        }
    }
    ooo_dump(&proc);

    printf("\n--- Final Register File ---\n");
    for (int i = 0; i < 16; i++) {
        printf("  x%d = %u\n", i, proc.registers[i]);
    }
    printf("Expected: x1=5 x2=3 x3=8 x4=13 x5=2 x6=4\n");

    return 0;
}
