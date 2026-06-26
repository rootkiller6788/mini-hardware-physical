#include <stdio.h>
#include <stdlib.h>
#include "logic_gate.h"
#include "combinational.h"

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
    Wire* a = wire_create("A");
    Wire* b = wire_create("B");
    Wire* y_and = wire_create("Y_AND");
    Wire* y_or  = wire_create("Y_OR");
    Wire* y_xor = wire_create("Y_XOR");
    Wire* y_not = wire_create("Y_NOT");
    Wire* and_in[] = {a, b};
    LogicGate g_and = logic_gate_create(GATE_AND, and_in, 2, y_and);
    Wire* or_in[] = {a, b};
    LogicGate g_or = logic_gate_create(GATE_OR, or_in, 2, y_or);
    Wire* xor_in[] = {a, b};
    LogicGate g_xor = logic_gate_create(GATE_XOR, xor_in, 2, y_xor);
    Wire* not_in[] = {a};
    LogicGate g_not = logic_gate_create(GATE_NOT, not_in, 1, y_not);

    printf("  A B | AND OR XOR NOT(A)\n");
    printf(" -----|-------------------\n");
    for (int av = 0; av <= 1; av++) {
        for (int bv = 0; bv <= 1; bv++) {
            a->value = av; b->value = bv;
            logic_eval(&g_and); logic_eval(&g_or); logic_eval(&g_xor); logic_eval(&g_not);
            printf("  %d %d |  %d    %d   %d     %d\n", av, bv, y_and->value, y_or->value, y_xor->value, y_not->value);
        }
    }

    printf("\n===== Half Adder Demo =====\n\n");
    HalfAdder ha = half_adder_create();
    printf("  A B | SUM CARRY\n  -----|----------\n");
    for (int av = 0; av <= 1; av++) {
        for (int bv = 0; bv <= 1; bv++) {
            half_adder_set_inputs(&ha, av, bv);
            half_adder_evaluate(&ha);
            printf("  %d %d |  %d    %d\n", av, bv, ha.sum->value, ha.carry->value);
        }
    }

    printf("\n===== De Morgan Verification =====\n");
    verify_de_morgan(2);
    printf("Absorption: %s\n", verify_absorption() ? "PASS" : "FAIL");
    printf("Consensus:  %s\n", verify_consensus() ? "PASS" : "FAIL");

    wire_free(a); wire_free(b); wire_free(y_and); wire_free(y_or);
    wire_free(y_xor); wire_free(y_not);
    wire_free(ha.a); wire_free(ha.b); wire_free(ha.sum); wire_free(ha.carry);

    printf("\nGate simulation demo complete.\n");
    return 0;
}
