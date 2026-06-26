#ifndef LOGIC_GATE_H
#define LOGIC_GATE_H

#include <stdbool.h>

#define LOGIC_MAX_INPUTS 4
#define LOGIC_MAX_WIRE_NAME 32

typedef enum {
    GATE_AND,
    GATE_OR,
    GATE_NOT,
    GATE_NAND,
    GATE_NOR,
    GATE_XOR,
    GATE_XNOR
} GateType;

typedef struct {
    bool  value;
    char  name[LOGIC_MAX_WIRE_NAME];
} Wire;

typedef struct {
    GateType type;
    Wire*    inputs[LOGIC_MAX_INPUTS];
    int      input_count;
    Wire*    output;
} LogicGate;

Wire*     wire_create(const char* name);
LogicGate logic_gate_create(GateType type, Wire** inputs, int input_count, Wire* output);
bool      logic_eval(const LogicGate* gate);
void      logic_print_truth_table(GateType type);

#endif
