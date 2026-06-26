#include "logic_gate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Wire* wire_create(const char* name) {
    Wire* w = malloc(sizeof(Wire));
    if (!w) return NULL;
    w->value = false;
    strncpy(w->name, name, LOGIC_MAX_WIRE_NAME - 1);
    w->name[LOGIC_MAX_WIRE_NAME - 1] = '\0';
    return w;
}

LogicGate logic_gate_create(GateType type, Wire** inputs, int input_count, Wire* output) {
    LogicGate g;
    g.type = type;
    g.output = output;
    if (input_count > LOGIC_MAX_INPUTS) input_count = LOGIC_MAX_INPUTS;
    g.input_count = input_count;
    for (int i = 0; i < input_count; i++) {
        g.inputs[i] = inputs[i];
    }
    return g;
}

bool logic_eval(const LogicGate* gate) {
    if (!gate || !gate->output) return false;

    bool result = false;
    switch (gate->type) {
    case GATE_AND:
        result = true;
        for (int i = 0; i < gate->input_count; i++) {
            result = result && gate->inputs[i]->value;
        }
        break;
    case GATE_OR:
        result = false;
        for (int i = 0; i < gate->input_count; i++) {
            result = result || gate->inputs[i]->value;
        }
        break;
    case GATE_NOT:
        result = !gate->inputs[0]->value;
        break;
    case GATE_NAND:
        result = true;
        for (int i = 0; i < gate->input_count; i++) {
            result = result && gate->inputs[i]->value;
        }
        result = !result;
        break;
    case GATE_NOR:
        result = false;
        for (int i = 0; i < gate->input_count; i++) {
            result = result || gate->inputs[i]->value;
        }
        result = !result;
        break;
    case GATE_XOR:
        result = false;
        for (int i = 0; i < gate->input_count; i++) {
            result = result != gate->inputs[i]->value;
        }
        break;
    case GATE_XNOR:
        result = false;
        for (int i = 0; i < gate->input_count; i++) {
            result = result != gate->inputs[i]->value;
        }
        result = !result;
        break;
    }
    gate->output->value = result;
    return result;
}

static void print_gate_type_name(GateType type) {
    switch (type) {
    case GATE_AND:  printf("AND");  break;
    case GATE_OR:   printf("OR");   break;
    case GATE_NOT:  printf("NOT");  break;
    case GATE_NAND: printf("NAND"); break;
    case GATE_NOR:  printf("NOR");  break;
    case GATE_XOR:  printf("XOR");  break;
    case GATE_XNOR: printf("XNOR"); break;
    }
}

static bool eval_gate_type(GateType type, bool a, bool b) {
    switch (type) {
    case GATE_AND:  return a && b;
    case GATE_OR:   return a || b;
    case GATE_NAND: return !(a && b);
    case GATE_NOR:  return !(a || b);
    case GATE_XOR:  return a != b;
    case GATE_XNOR: return a == b;
    default: return false;
    }
}

void logic_print_truth_table(GateType type) {
    print_gate_type_name(type);
    printf(" gate truth table:\n");

    if (type == GATE_NOT) {
        printf("  A | Q\n");
        printf(" ---|---\n");
        for (int a = 0; a <= 1; a++) {
            printf("  %d | %d\n", a, a ? 0 : 1);
        }
    } else {
        printf("  A B | Q\n");
        printf(" -----|---\n");
        for (int a = 0; a <= 1; a++) {
            for (int b = 0; b <= 1; b++) {
                printf("  %d %d | %d\n", a, b, eval_gate_type(type, a, b));
            }
        }
    }
    printf("\n");
}
