/**
 * logic_gate.h — L1: Digital Logic Gate Definitions
 *
 * Knowledge coverage:
 *   L1: GateType, Wire, LogicGate, TruthTable, BoolExpr structs
 *   L2: 8 primitive gates + tri-state logic
 *   L3: Multi-level GateNetwork with topological ordering
 *   L4: De Morgan's Laws, functional completeness, absorption, consensus
 *   L5: Boolean expression AST, algebraic simplification
 *   L8: Hazard/glitch detection
 *
 * References:
 *   MIT 6.004 — Computation Structures
 *   Berkeley CS 61C — Great Ideas in Computer Architecture
 *   Morris Mano — Digital Design
 */
#ifndef LOGIC_GATE_H
#define LOGIC_GATE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define LOGIC_MAX_INPUTS      8
#define LOGIC_MAX_WIRE_NAME   64
#define LOGIC_MAX_TRUTH_ROWS  256

/* L1: 8 primitive gate types. NAND/NOR are functionally complete (L4) */
typedef enum {
    GATE_AND,
    GATE_OR,
    GATE_NOT,
    GATE_NAND,
    GATE_NOR,
    GATE_XOR,
    GATE_XNOR,
    GATE_BUF
} GateType;

/* L1: Tri-state values for bus contention modeling (L3) */
typedef enum {
    SIG_LOW  = 0,
    SIG_HIGH = 1,
    SIG_Z    = 2,   /* high-impedance */
    SIG_X    = 3    /* unknown / contention */
} SignalValue;

/* L1: Wire — digital signal carrier with timing annotations */
typedef struct {
    bool        value;
    char        name[LOGIC_MAX_WIRE_NAME];
    double      delay_ps;
    bool        is_glitching;
    SignalValue tri_value;
} Wire;

/* L1: LogicGate — one gate with fan-in and fan-out */
typedef struct {
    GateType    type;
    Wire*       inputs[LOGIC_MAX_INPUTS];
    int         input_count;
    Wire*       output;
    double      tplh_ps;
    double      tphl_ps;
    int         fan_out_count;
} LogicGate;

/* L1: TruthTable — complete truth table for an n-input function */
typedef struct {
    GateType    type;
    int         num_inputs;
    bool        table[LOGIC_MAX_TRUTH_ROWS];
} TruthTable;

/* L2: Boolean expression AST node type */
typedef enum {
    BOOL_CONST,
    BOOL_VAR,
    BOOL_AND,
    BOOL_OR,
    BOOL_NOT,
    BOOL_XOR
} BoolNodeType;

/* L2: Boolean expression tree node */
typedef struct BoolExpr {
    BoolNodeType     type;
    bool             const_val;
    int              var_id;
    struct BoolExpr* left;
    struct BoolExpr* right;
} BoolExpr;

/* L3: Multi-level gate network (netlist) */
#define GATE_NET_MAX_GATES 256
#define GATE_NET_MAX_WIRES 512

typedef struct {
    LogicGate   gates[GATE_NET_MAX_GATES];
    int         gate_count;
    Wire        wires[GATE_NET_MAX_WIRES];
    int         wire_count;
    bool        sorted;
    int         order[GATE_NET_MAX_GATES];
} GateNetwork;

/* ---- L1: Wire API ---- */
Wire*        wire_create(const char* name);
void         wire_free(Wire* w);
Wire*        wire_clone(const Wire* src, const char* new_name);

/* ---- L1: Gate API ---- */
LogicGate    logic_gate_create(GateType type, Wire** inputs, int input_count,
                               Wire* output);
bool         logic_eval(LogicGate* gate);
SignalValue  logic_eval_tri(const LogicGate* gate);
const char*  gate_type_name(GateType type);
GateType     gate_complement(GateType type);
int          gate_precedence(GateType type);

/* ---- L4: Truth Table & Theorem Verification ---- */
TruthTable   truth_table_create(GateType type, int num_inputs);
bool         truth_table_lookup(const TruthTable* tt, int row);
void         truth_table_print(const TruthTable* tt);
bool         verify_de_morgan(int num_inputs);
bool         verify_absorption(void);
bool         verify_consensus(void);
bool         is_functionally_complete(GateType type);
int          nand_synthesize_count(bool (*func)(bool, bool));
int          nor_synthesize_count(bool (*func)(bool, bool));

/* ---- L5: Boolean Expression API ---- */
BoolExpr*    bool_expr_const(bool val);
BoolExpr*    bool_expr_var(int var_id);
BoolExpr*    bool_expr_create(BoolNodeType type, BoolExpr* left, BoolExpr* right);
bool         bool_expr_eval(const BoolExpr* expr, const bool* var_values);
void         bool_expr_free(BoolExpr* expr);
BoolExpr*    bool_expr_clone(const BoolExpr* src);
BoolExpr*    bool_expr_simplify(BoolExpr* expr);
void         bool_expr_print(const BoolExpr* expr);
int          bool_expr_node_count(const BoolExpr* expr);
int          bool_expr_depth(const BoolExpr* expr);

/* ---- L3: Gate Network API ---- */
GateNetwork  gate_net_create(void);
int          gate_net_add_wire(GateNetwork* net, const char* name);
int          gate_net_add_gate(GateNetwork* net, GateType type,
                               int input_ids[], int in_count, int out_id);
void         gate_net_evaluate(GateNetwork* net);
bool         gate_net_topo_sort(GateNetwork* net);
void         gate_net_print(const GateNetwork* net);
bool         gate_net_is_cyclic(const GateNetwork* net);
int          gate_net_logic_depth(GateNetwork* net);
void         gate_net_reset(GateNetwork* net);

/* ---- L8: Hazard Detection ---- */
int          gate_net_detect_hazards(GateNetwork* net);

void         logic_print_truth_table(GateType type);

#endif /* LOGIC_GATE_H */

