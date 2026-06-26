#ifndef COMBINATIONAL_H
#define COMBINATIONAL_H

#include <stdbool.h>
#include "logic_gate.h"

#define COMB_MAX_GATES 128
#define COMB_MAX_WIRES 128

typedef struct {
    LogicGate gates[COMB_MAX_GATES];
    int       gate_count;
    Wire      wires[COMB_MAX_WIRES];
    int       wire_count;
} Combinational;

typedef struct {
    Wire* a;
    Wire* b;
    Wire* sum;
    Wire* carry;
} HalfAdder;

typedef struct {
    Wire* a;
    Wire* b;
    Wire* cin;
    Wire* sum;
    Wire* cout;
} FullAdder;

typedef struct {
    Wire*  a[8];
    Wire*  b[8];
    Wire*  sum[8];
    Wire*  carry_out;
    int    width;
} RippleAdder;

Combinational comb_create(void);
int           comb_add_wire(Combinational* c, const char* name);
int           comb_add_gate(Combinational* c, GateType type, int wire_ids[], int in_count, int out_id);
void          comb_evaluate(Combinational* c);
void          comb_print(const Combinational* c);

HalfAdder   half_adder_create(void);
FullAdder   full_adder_create(void);
RippleAdder ripple_adder_create(int width);

#endif
