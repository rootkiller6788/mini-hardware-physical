#include <stdio.h>
#include "combinational.h"
#include "alu.h"

int main(void) {
    printf("===== 8-bit ALU Demo =====\n\n");
    unsigned char tests[][2] = {{0x0A,0x05},{0x55,0xAA},{0x7F,0x01},{0xFF,0x01},{0x3C,0x0F},{0x12,0x34}};
    int n = 6;
    ALU alu = alu_create();
    for (int t = 0; t < n; t++) {
        for (int op = ALU_ADD; op <= ALU_XOR; op++) {
            alu_set_inputs(&alu, tests[t][0], tests[t][1], (AluOp)op);
            uint32_t res = alu_compute(&alu);
            printf("%-4s 0x%02X 0x%02X = 0x%02X", alu_op_name((AluOp)op), tests[t][0], tests[t][1], res);
            printf(" (Z=%d N=%d C=%d V=%d)\n", alu_flag_zero(&alu), alu_flag_negative(&alu),
                   alu_flag_carry(&alu), alu_flag_overflow(&alu));
        }
        printf("---\n");
    }
    printf("\n===== Ripple Adder Demo =====\n");
    RippleAdder ra = ripple_adder_create(8);
    ripple_adder_set_inputs(&ra, 0x55, 0xAA);
    uint64_t sum = ripple_adder_compute(&ra);
    printf("0x55 + 0xAA = 0x%02llX (C=%d)\n", (unsigned long long)sum, ra.carry_out->value);
    printf("\n===== CLA Demo =====\n");
    CarryLookaheadAdder cla = cla_create(8);
    cla_set_inputs(&cla, 0x0F, 0x01, 0);
    uint64_t cla_sum = cla_compute(&cla);
    printf("0x0F + 0x01 = 0x%02llX (Cout=%d)\n", (unsigned long long)cla_sum, cla_get_carry_out(&cla));
    printf("\n===== Barrel Shifter Demo =====\n");
    BarrelShifter bs = barrel_create(8);
    barrel_set_input(&bs, 0xA5);
    ShiftType types[] = {SHIFT_LL, SHIFT_LR, SHIFT_AR, SHIFT_ROL, SHIFT_ROR};
    const char* tnames[] = {"SHL","SHR","SAR","ROL","ROR"};
    for (int i = 0; i < 5; i++) {
        barrel_set_shift(&bs, types[i], 2);
        uint64_t out = barrel_compute(&bs);
        printf("  0x%02X %s 2 = 0x%02llX\n", 0xA5, tnames[i], (unsigned long long)out);
    }
    printf("\nALU demo complete.\n");
    return 0;
}
