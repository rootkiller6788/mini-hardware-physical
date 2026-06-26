#include <stdio.h>
#include <stdint.h>
#include "pipeline.h"

static uint32_t rtype(uint8_t op, uint8_t rd, uint8_t rs1, uint8_t rs2,
                      uint8_t f3, uint8_t f7) {
    return (uint32_t)((f7 << 25) | (rs2 << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op);
}

static uint32_t itype(uint8_t op, uint8_t rd, uint8_t rs1, int16_t imm, uint8_t f3) {
    return (uint32_t)(((imm & 0xFFF) << 20) | (rs1 << 15) | (f3 << 12) | (rd << 7) | op);
}

static const char* stage_name(int s) {
    static const char* names[] = {"IF", "ID", "EX", "MEM", "WB"};
    return (s >= 0 && s < 5) ? names[s] : "??";
}

static void print_pipe_diagram(const Pipeline* p) {
    printf("\n  +--------+--------+--------+--------+--------+\n");
    printf("  |  IF    |  ID    |  EX    |  MEM   |  WB    |\n");
    printf("  +--------+--------+--------+--------+--------+\n");

    printf("  |");
    printf(" %-6s |", p->if_id.valid ? isa_opcode_name(p->if_id.inst.opcode) : " .");
    printf(" %-6s |", p->id_ex.valid ? isa_opcode_name(p->id_ex.inst.opcode) : " .");
    printf(" %-6s |", p->ex_mem.valid ? isa_opcode_name(p->ex_mem.inst.opcode) : " .");
    printf(" %-6s |", p->mem_wb.valid ? "..." : " .");
    printf(" %-6s |", p->mem_wb.valid ? "..." : " .");
    printf("\n");
    printf("  +--------+--------+--------+--------+--------+\n");
}

int main(void) {
    printf("=== mini-cpu-arch: Pipeline Simulator Demo ===\n\n");

    Pipeline pipe;
    pipeline_init(&pipe);

    uint32_t prog[] = {
        itype(0x13, 1, 0, 10, 0),        /* ADDI x1, x0, 10   */
        itype(0x13, 2, 0, 20, 0),        /* ADDI x2, x0, 20   */
        rtype(0x33, 3, 1, 2, 0, 0x00),   /* ADD  x3, x1, x2   */
        rtype(0x33, 4, 3, 1, 0, 0x20),   /* SUB  x4, x3, x1   */
        rtype(0x33, 5, 3, 2, 6, 0x00),   /* OR   x5, x3, x2   */
        rtype(0x33, 6, 3, 4, 7, 0x00),   /* AND  x6, x3, x4   */
        rtype(0x33, 7, 5, 6, 0, 0x00),   /* ADD  x7, x5, x6   */
    };
    size_t prog_len = sizeof(prog) / sizeof(prog[0]);

    pipeline_load_program(&pipe, prog, prog_len);
    pipe.isa.pc = 0;

    printf("Program loaded: %zu instructions\n", prog_len);
    printf("Observing data hazard: x3 used by SUB immediately after ADD\n\n");

    for (int cyc = 0; cyc < 12; cyc++) {
        printf("--- Cycle %d ---\n", cyc + 1);
        pipeline_cycle(&pipe);
        print_pipe_diagram(&pipe);

        if (pipe.forward_a || pipe.forward_b) {
            printf("  ** FORWARDING ACTIVE");
            if (pipe.forward_a) printf(" A=%u", pipe.forward_a_val);
            if (pipe.forward_b) printf(" B=%u", pipe.forward_b_val);
            printf(" **\n");
        }
        if (pipe.stall_count > 0) {
            printf("  ** STALL inserted **\n");
        }
    }

    printf("\n--- Final Register Values ---\n");
    isa_dump_registers(&pipe.isa);
    printf("x1=10 x2=20 x3=30 x4=20 x5=30 x6=10 x7=40\n");
    printf("\nTotal stalls: %u  Total bubbles: %u\n",
           pipe.stall_count, pipe.bubble_count);

    return 0;
}
