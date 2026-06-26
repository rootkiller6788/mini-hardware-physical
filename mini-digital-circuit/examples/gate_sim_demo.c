#include <stdio.h>
#include <stdlib.h>
#include "logic_gate.h"
#include "combinational.h"

/* 演示基本门电路评估 */
int main(void) {
    printf("===== Basic Gate Truth Tables =====\n\n");
    logic_print_truth_table(GATE_AND);
    logic_print_truth_table(GATE_OR);
    logic_print_truth_table(GATE_NOT);
    logic_print_truth_table(GATE_NAND);
    logic_print_truth_table(GATE_NOR);
    logic_print_truth_table(GATE_XOR);
    logic_print_truth_table(GATE_XNOR);

    printf("===== Gate Evaluation Demo =====\n\n");

    /* 创建 wires */
    Wire* a = wire_create("A");
    Wire* b = wire_create("B");
    Wire* y_and = wire_create("Y_AND");
    Wire* y_or  = wire_create("Y_OR");
    Wire* y_xor = wire_create("Y_XOR");
    Wire* y_not = wire_create("Y_NOT");

    /* 创建 gates */
    Wire* and_inputs[] = {a, b};
    LogicGate g_and = logic_gate_create(GATE_AND, and_inputs, 2, y_and);

    Wire* or_inputs[] = {a, b};
    LogicGate g_or = logic_gate_create(GATE_OR, or_inputs, 2, y_or);

    Wire* xor_inputs[] = {a, b};
    LogicGate g_xor = logic_gate_create(GATE_XOR, xor_inputs, 2, y_xor);

    Wire* not_inputs[] = {a};
    LogicGate g_not = logic_gate_create(GATE_NOT, not_inputs, 1, y_not);

    /* 遍历所有输入组合 */
    printf("  A B | AND OR XOR NOT(A)\n");
    printf(" -----|-------------------\n");
    for (int av = 0; av <= 1; av++) {
        for (int bv = 0; bv <= 1; bv++) {
            a->value = av;
            b->value = bv;

            logic_eval(&g_and);
            logic_eval(&g_or);
            logic_eval(&g_xor);
            logic_eval(&g_not);

            printf("  %d %d |  %d    %d   %d     %d\n",
                   av, bv,
                   y_and->value, y_or->value,
                   y_xor->value, y_not->value);
        }
    }

    /* Half Adder 演示 */
    printf("\n===== Half Adder Demo =====\n\n");
    HalfAdder ha = half_adder_create();

    Wire* ha_and_inputs[] = {ha.a, ha.b};
    LogicGate ha_and = logic_gate_create(GATE_AND, ha_and_inputs, 2, ha.carry);

    Wire* ha_xor_inputs[] = {ha.a, ha.b};
    LogicGate ha_xor = logic_gate_create(GATE_XOR, ha_xor_inputs, 2, ha.sum);

    printf("  A B | SUM CARRY\n");
    printf(" -----|----------\n");
    for (int av = 0; av <= 1; av++) {
        for (int bv = 0; bv <= 1; bv++) {
            ha.a->value = av;
            ha.b->value = bv;
            logic_eval(&ha_xor);
            logic_eval(&ha_and);
            printf("  %d %d |  %d    %d\n",
                   av, bv, ha.sum->value, ha.carry->value);
        }
    }

    /* 释放分布式 wire */
    free(a); free(b); free(y_and); free(y_or);
    free(y_xor); free(y_not);
    free(ha.a); free(ha.b); free(ha.sum); free(ha.carry);

    printf("\nGate simulation demo complete.\n");
    return 0;
}
