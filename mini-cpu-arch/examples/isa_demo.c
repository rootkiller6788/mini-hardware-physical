#include <stdio.h>
#include <stdint.h>
#include "isa.h"

static uint32_t encode_rtype(uint8_t opcode, uint8_t rd, uint8_t rs1, uint8_t rs2,
                             uint8_t funct3, uint8_t funct7) {
    return (uint32_t)((funct7 << 25) | (rs2 << 20) | (rs1 << 15) |
                      (funct3 << 12) | (rd << 7) | opcode);
}

static uint32_t encode_itype(uint8_t opcode, uint8_t rd, uint8_t rs1, int16_t imm,
                             uint8_t funct3) {
    return (uint32_t)(((imm & 0xFFF) << 20) | (rs1 << 15) | (funct3 << 12) |
                      (rd << 7) | opcode);
}

int main(void) {
    printf("=== mini-cpu-arch: ISA Demo ===\n\n");

    ISAContext ctx;
    isa_init(&ctx);

    uint32_t program[] = {
        encode_itype(0x13, 1, 0, 42, 0),         /* ADDI x1, x0, 42   */
        encode_itype(0x13, 2, 0, 100, 0),        /* ADDI x2, x0, 100  */
        encode_rtype(0x33, 3, 1, 2, 0, 0x00),    /* ADD  x3, x1, x2   */
        encode_rtype(0x33, 4, 3, 1, 0, 0x20),    /* SUB  x4, x3, x1   */
        encode_itype(0x13, 5, 0, 8, 3),          /* SLTI x5, x0, 8    */
        encode_rtype(0x33, 6, 3, 2, 6, 0x00),    /* OR   x6, x3, x2   */
        encode_rtype(0x33, 7, 3, 2, 7, 0x00),    /* AND  x7, x3, x2   */
    };
    size_t prog_len = sizeof(program) / sizeof(program[0]);

    printf("Loading %zu instructions into memory...\n", prog_len);
    isa_load_program(&ctx, program, prog_len);

    printf("\n--- Initial Register State ---\n");
    isa_dump_registers(&ctx);

    printf("\n--- Stepping through program ---\n");
    ctx.pc = 0;
    for (int step = 0; step < (int)prog_len; step++) {
        printf("\n[Step %d] PC=0x%04X\n", step + 1, ctx.pc);

        Instruction raw_inst = isa_fetch(&ctx);
        printf("  Fetch: raw=0x%08X\n", raw_inst.raw);

        Instruction decoded = isa_decode(raw_inst.raw);
        printf("  Decode: op=%-6s rd=x%-2d rs1=x%-2d rs2=x%-2d imm=%d\n",
               isa_opcode_name(decoded.opcode), decoded.rd,
               decoded.rs1, decoded.rs2, decoded.immediate);

        isa_execute(&ctx, &decoded);
        printf("  Execute: result stored\n");
    }

    printf("\n--- Final Register State ---\n");
    isa_dump_registers(&ctx);

    printf("\nExpected values:\n");
    printf("  x1 = 42, x2 = 100, x3 = 142, x4 = 100\n");
    printf("  x5 = 0 (8 is not < 0), x6 = 238, x7 = 132\n");

    return 0;
}
