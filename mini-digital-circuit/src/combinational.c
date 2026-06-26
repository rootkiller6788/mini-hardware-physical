#include "combinational.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Combinational comb_create(void) {
    Combinational c;
    c.gate_count = 0;
    c.wire_count = 0;
    return c;
}

int comb_add_wire(Combinational* c, const char* name) {
    if (c->wire_count >= COMB_MAX_WIRES) return -1;
    strncpy(c->wires[c->wire_count].name, name, LOGIC_MAX_WIRE_NAME - 1);
    c->wires[c->wire_count].name[LOGIC_MAX_WIRE_NAME - 1] = '\0';
    c->wires[c->wire_count].value = false;
    return c->wire_count++;
}

int comb_add_gate(Combinational* c, GateType type, int wire_ids[], int in_count, int out_id) {
    if (c->gate_count >= COMB_MAX_GATES) return -1;
    if (in_count > LOGIC_MAX_INPUTS) in_count = LOGIC_MAX_INPUTS;

    /* 通过索引引用组合逻辑中的 wire */
    LogicGate* g = &c->gates[c->gate_count];
    g->type = type;
    g->input_count = in_count;
    g->output = &c->wires[out_id];
    for (int i = 0; i < in_count; i++) {
        g->inputs[i] = &c->wires[wire_ids[i]];
    }
    return c->gate_count++;
}

void comb_evaluate(Combinational* c) {
    /* 拓扑顺序评估：按门添加顺序依次计算 */
    for (int i = 0; i < c->gate_count; i++) {
        logic_eval(&c->gates[i]);
    }
}

void comb_print(const Combinational* c) {
    printf("Combinational circuit: %d gates, %d wires\n", c->gate_count, c->wire_count);
    printf("Wire values:\n");
    for (int i = 0; i < c->wire_count; i++) {
        printf("  %s = %d\n", c->wires[i].name, c->wires[i].value);
    }
}

/* ---- Half Adder ---- */
HalfAdder half_adder_create(void) {
    HalfAdder ha;
    ha.a     = wire_create("A");
    ha.b     = wire_create("B");
    ha.sum   = wire_create("SUM");
    ha.carry = wire_create("CARRY");
    return ha;
}

/* ---- Full Adder ---- */
FullAdder full_adder_create(void) {
    FullAdder fa;
    fa.a    = wire_create("A");
    fa.b    = wire_create("B");
    fa.cin  = wire_create("CIN");
    fa.sum  = wire_create("SUM");
    fa.cout = wire_create("COUT");
    return fa;
}

/* ---- Ripple Adder ---- */
RippleAdder ripple_adder_create(int width) {
    RippleAdder ra;
    ra.width = width;
    for (int i = 0; i < width; i++) {
        char name[16];
        snprintf(name, sizeof(name), "A%d", i);
        ra.a[i] = wire_create(name);
        snprintf(name, sizeof(name), "B%d", i);
        ra.b[i] = wire_create(name);
        snprintf(name, sizeof(name), "S%d", i);
        ra.sum[i] = wire_create(name);
    }
    ra.carry_out = wire_create("COUT");
    return ra;
}
