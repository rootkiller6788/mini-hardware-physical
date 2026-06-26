#include <stdio.h>
#include <stdlib.h>
#include "combinational.h"

/* 演示 8 位 ALU 操作 */
typedef enum {
    ALU_ADD, ALU_SUB, ALU_AND, ALU_OR, ALU_XOR
} AluOp;

static const char* op_names[] = {"ADD", "SUB", "AND", "OR", "XOR"};

static unsigned char alu_compute(AluOp op, unsigned char a, unsigned char b) {
    switch (op) {
    case ALU_ADD: return a + b;
    case ALU_SUB: return a - b;
    case ALU_AND: return a & b;
    case ALU_OR:  return a | b;
    case ALU_XOR: return a ^ b;
    default: return 0;
    }
}

int main(void) {
    printf("===== 8-bit ALU Demo =====\n\n");

    unsigned char test_cases[][2] = {
        {0x0A, 0x05},
        {0x55, 0xAA},
        {0x7F, 0x01},
        {0xFF, 0x01},
        {0x3C, 0x0F},
        {0x00, 0x00},
        {0x80, 0x80},
        {0x12, 0x34}
    };
    int num_tests = sizeof(test_cases) / sizeof(test_cases[0]);

    printf("Op   |   A      B   | Result | Binary Result\n");
    printf("-----|--------------|--------|---------------\n");

    for (int t = 0; t < num_tests; t++) {
        unsigned char a = test_cases[t][0];
        unsigned char b = test_cases[t][1];

        for (int op = 0; op < 5; op++) {
            unsigned char result = alu_compute((AluOp)op, a, b);

            printf("%-4s | 0x%02X  0x%02X |  0x%02X  | ",
                   op_names[op], a, b, result);
            for (int bit = 7; bit >= 0; bit--) {
                printf("%d", (result >> bit) & 1);
            }
            printf("\n");
        }
        printf("-----|--------------|--------|---------------\n");
    }

    /* 验证组合逻辑方法 */
    printf("\n===== ALU via Combinational Simulation =====\n\n");
    printf("Building 8-bit ripple-carry adder from gates...\n");

    Combinational c = comb_create();

    /* 创建 8 对输入 wires 和 8 个 sum wires + carry */
    int w_a[8], w_b[8], w_s[8];
    for (int i = 0; i < 8; i++) {
        char name[8];
        snprintf(name, sizeof(name), "A%d", i);   w_a[i] = comb_add_wire(&c, name);
        snprintf(name, sizeof(name), "B%d", i);   w_b[i] = comb_add_wire(&c, name);
        snprintf(name, sizeof(name), "S%d", i);   w_s[i] = comb_add_wire(&c, name);
    }
    int w_cin  = comb_add_wire(&c, "CIN");
    int w_cout = comb_add_wire(&c, "COUT");

    /* 中间进位 wire */
    int w_carry[9];
    w_carry[0] = w_cin;
    for (int i = 1; i < 9; i++) {
        char name[16];
        snprintf(name, sizeof(name), "C%d", i);
        w_carry[i] = comb_add_wire(&c, name);
    }
    w_carry[8] = w_cout;

    /* 为每个 bit 创建全加器门电路 */
    for (int i = 0; i < 8; i++) {
        int w_xor1 = comb_add_wire(&c, "xor1");
        int w_and1 = comb_add_wire(&c, "and1");
        int w_xor2 = comb_add_wire(&c, "xor2");
        int w_and2 = comb_add_wire(&c, "and2");
        int w_or   = comb_add_wire(&c, "or_out");

        int xor1_in[]  = {w_a[i], w_b[i]};
        int and1_in[]  = {w_a[i], w_b[i]};
        int xor2_in[]  = {w_xor1, w_carry[i]};
        int and2_in[]  = {w_xor1, w_carry[i]};
        int or_in[]    = {w_and2, w_and1};

        comb_add_gate(&c, GATE_XOR, xor1_in, 2, w_xor1);
        comb_add_gate(&c, GATE_AND, and1_in, 2, w_and1);
        comb_add_gate(&c, GATE_XOR, xor2_in, 2, w_s[i]);
        comb_add_gate(&c, GATE_AND, and2_in, 2, w_and2);
        comb_add_gate(&c, GATE_OR,  or_in,  2, w_carry[i + 1]);
    }

    /* 测试几个加法 */
    for (int t = 0; t < 4; t++) {
        unsigned char va = test_cases[t][0];
        unsigned char vb = test_cases[t][1];

        /* 设置输入 */
        for (int i = 0; i < 8; i++) {
            c.wires[w_a[i]].value = (va >> i) & 1;
            c.wires[w_b[i]].value = (vb >> i) & 1;
        }
        c.wires[w_cin].value = 0;

        /* 评估 */
        comb_evaluate(&c);

        /* 读取结果 */
        unsigned char sum = 0;
        for (int i = 0; i < 8; i++) {
            sum |= (c.wires[w_s[i]].value ? 1 : 0) << i;
        }
        printf("  0x%02X + 0x%02X = 0x%02X (C=%d)  [gate-level]\n",
               va, vb, sum, c.wires[w_cout].value);
    }

    printf("\nALU demo complete.\n");
    return 0;
}
