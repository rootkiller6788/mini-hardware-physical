#include "logic_gate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Wire* wire_create(const char* name) {
    Wire* w = (Wire*)malloc(sizeof(Wire));
    if (!w) return NULL;
    w->value = false; w->delay_ps = 0.0; w->is_glitching = false;
    w->tri_value = SIG_LOW;
    if (name) { strncpy(w->name, name, LOGIC_MAX_WIRE_NAME - 1); w->name[LOGIC_MAX_WIRE_NAME - 1] = '\0'; }
    else { w->name[0] = '\0'; }
    return w;
}

void wire_free(Wire* w) { free(w); }

Wire* wire_clone(const Wire* src, const char* new_name) {
    if (!src) return NULL;
    Wire* c = wire_create(new_name ? new_name : src->name);
    if (!c) return NULL;
    c->value = src->value; c->delay_ps = src->delay_ps;
    c->is_glitching = src->is_glitching; c->tri_value = src->tri_value;
    return c;
}

LogicGate logic_gate_create(GateType type, Wire** inputs, int input_count, Wire* output) {
    LogicGate g; g.type = type; g.output = output;
    g.tplh_ps = 100.0; g.tphl_ps = 100.0; g.fan_out_count = 0;
    if (input_count > LOGIC_MAX_INPUTS) input_count = LOGIC_MAX_INPUTS;
    g.input_count = input_count;
    for (int i = 0; i < input_count; i++) g.inputs[i] = inputs[i];
    return g;
}

bool logic_eval(LogicGate* gate) {
    if (!gate || !gate->output) return false;
    bool r = false;
    switch (gate->type) {
    case GATE_AND: r = true; for (int i = 0; i < gate->input_count && r; i++) r = r && gate->inputs[i]->value; break;
    case GATE_OR: r = false; for (int i = 0; i < gate->input_count && !r; i++) r = r || gate->inputs[i]->value; break;
    case GATE_NOT: r = !gate->inputs[0]->value; break;
    case GATE_NAND: r = true; for (int i = 0; i < gate->input_count && r; i++) r = r && gate->inputs[i]->value; r = !r; break;
    case GATE_NOR: r = false; for (int i = 0; i < gate->input_count && !r; i++) r = r || gate->inputs[i]->value; r = !r; break;
    case GATE_XOR: r = false; for (int i = 0; i < gate->input_count; i++) r = (r != gate->inputs[i]->value); break;
    case GATE_XNOR: r = false; for (int i = 0; i < gate->input_count; i++) r = (r != gate->inputs[i]->value); r = !r; break;
    case GATE_BUF: r = gate->inputs[0]->value; break;
    }
    gate->output->value = r;
    return r;
}

SignalValue logic_eval_tri(const LogicGate* gate) {
    if (!gate) return SIG_X;
    if (gate->fan_out_count == 0) return SIG_Z;
    bool r = gate->inputs[0]->value;
    if (gate->type == GATE_NOT) r = !r;
    return r ? SIG_HIGH : SIG_LOW;
}

const char* gate_type_name(GateType type) {
    switch (type) {
    case GATE_AND: return "AND"; case GATE_OR: return "OR";
    case GATE_NOT: return "NOT"; case GATE_NAND: return "NAND";
    case GATE_NOR: return "NOR"; case GATE_XOR: return "XOR";
    case GATE_XNOR: return "XNOR"; case GATE_BUF: return "BUF";
    default: return "UNKNOWN";
    }
}

GateType gate_complement(GateType type) {
    switch (type) {
    case GATE_AND: return GATE_OR; case GATE_OR: return GATE_AND;
    case GATE_NAND: return GATE_NOR; case GATE_NOR: return GATE_NAND;
    case GATE_XOR: return GATE_XNOR; case GATE_XNOR: return GATE_XOR;
    default: return type;
    }
}

int gate_precedence(GateType type) {
    switch (type) {
    case GATE_NOT: case GATE_BUF: return 3;
    case GATE_AND: case GATE_NAND: return 2;
    case GATE_XOR: case GATE_XNOR: return 1;
    default: return 0;
    }
}

TruthTable truth_table_create(GateType type, int num_inputs) {
    TruthTable tt; tt.type = type;
    if (num_inputs > 8) num_inputs = 8;
    if (num_inputs < 1) num_inputs = 1;
    tt.num_inputs = num_inputs;
    int rows = 1 << num_inputs;
    if (rows > LOGIC_MAX_TRUTH_ROWS) rows = LOGIC_MAX_TRUTH_ROWS;
    for (int r = 0; r < rows; r++) {
        bool in[8] = {false};
        for (int b = 0; b < num_inputs; b++) in[b] = (r >> b) & 1;
        switch (type) {
        case GATE_AND: { bool v = true; for (int i = 0; i < num_inputs; i++) v = v && in[i]; tt.table[r] = v; break; }
        case GATE_OR:  { bool v = false; for (int i = 0; i < num_inputs; i++) v = v || in[i]; tt.table[r] = v; break; }
        case GATE_NOT: tt.table[r] = !in[0]; break;
        case GATE_NAND:{ bool v = true; for (int i = 0; i < num_inputs; i++) v = v && in[i]; tt.table[r] = !v; break; }
        case GATE_NOR: { bool v = false; for (int i = 0; i < num_inputs; i++) v = v || in[i]; tt.table[r] = !v; break; }
        case GATE_XOR: { bool v = false; for (int i = 0; i < num_inputs; i++) v = (v != in[i]); tt.table[r] = v; break; }
        case GATE_XNOR:{ bool v = false; for (int i = 0; i < num_inputs; i++) v = (v != in[i]); tt.table[r] = !v; break; }
        case GATE_BUF: tt.table[r] = in[0]; break;
        }
    }
    return tt;
}

bool truth_table_lookup(const TruthTable* tt, int row) {
    if (!tt || row < 0 || row >= (1 << tt->num_inputs)) return false;
    return tt->table[row];
}

void truth_table_print(const TruthTable* tt) {
    if (!tt) return;
    printf("%s gate truth table (%d inputs):\n", gate_type_name(tt->type), tt->num_inputs);
    for (int i = tt->num_inputs - 1; i >= 0; i--) printf(" %c ", 'A' + i);
    printf("| Q\n");
    for (int i = 0; i < tt->num_inputs; i++) printf("---");
    printf("+---\n");
    int rows = 1 << tt->num_inputs;
    for (int r = 0; r < rows; r++) {
        for (int i = tt->num_inputs - 1; i >= 0; i--) printf(" %d ", (r >> i) & 1);
        printf("| %d\n", tt->table[r]);
    }
}

bool verify_de_morgan(int num_inputs) {
    if (num_inputs < 1 || num_inputs > 6) num_inputs = 2;
    int rows = 1 << num_inputs;
    bool ok1 = true, ok2 = true;
    for (int r = 0; r < rows; r++) {
        bool and_r = true, nor_r = false;
        for (int i = 0; i < num_inputs; i++) { bool b = (r >> i) & 1; and_r = and_r && b; nor_r = nor_r || !b; }
        if (!and_r != nor_r) ok1 = false;
        bool or_r = false, nand_r = true;
        for (int i = 0; i < num_inputs; i++) { bool b = (r >> i) & 1; or_r = or_r || b; nand_r = nand_r && !b; }
        if (!or_r != nand_r) ok2 = false;
    }
    return ok1 && ok2;
}

bool verify_absorption(void) {
    for (int a = 0; a <= 1; a++) for (int b = 0; b <= 1; b++)
        if ((a || (a && b)) != a) return false;
    return true;
}

bool verify_consensus(void) {
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++)
            for (int c = 0; c <= 1; c++)
                if (((a && b) || (!a && c) || (b && c)) != ((a && b) || (!a && c)))
                    return false;
    return true;
}

bool is_functionally_complete(GateType type) { return (type == GATE_NAND || type == GATE_NOR); }

int nand_synthesize_count(bool (*func)(bool, bool)) {
    if (!func) return 0;
    int code = (func(0,0)?1:0)|(func(0,1)?2:0)|(func(1,0)?4:0)|(func(1,1)?8:0);
    switch (code) { case 0: case 15: return 0; case 1: case 3: case 5: case 10: case 12: return 1; case 2: case 4: case 7: case 8: return 2; case 11: case 13: case 14: return 3; case 6: case 9: return 4; default: return 0; }
}

int nor_synthesize_count(bool (*func)(bool, bool)) {
    if (!func) return 0;
    int code = (func(0,0)?1:0)|(func(0,1)?2:0)|(func(1,0)?4:0)|(func(1,1)?8:0);
    switch (code) { case 0: case 15: return 0; case 1: case 5: case 10: return 1; case 3: case 7: case 12: case 14: return 2; case 2: case 4: case 8: case 11: case 13: return 3; case 6: case 9: return 5; default: return 0; }
}

/* ===== Gate Network (L3) ===== */

GateNetwork gate_net_create(void) {
    GateNetwork net; net.gate_count = 0; net.wire_count = 0; net.sorted = false; return net;
}
int gate_net_add_wire(GateNetwork* net, const char* name) {
    if (net->wire_count >= GATE_NET_MAX_WIRES) return -1;
    Wire* w = &net->wires[net->wire_count];
    w->value = false; w->delay_ps = 0; w->is_glitching = false; w->tri_value = SIG_LOW;
    if (name) { strncpy(w->name, name, LOGIC_MAX_WIRE_NAME - 1); w->name[LOGIC_MAX_WIRE_NAME - 1] = '\0'; }
    else snprintf(w->name, LOGIC_MAX_WIRE_NAME, "w%d", net->wire_count);
    return net->wire_count++;
}
int gate_net_add_gate(GateNetwork* net, GateType type, int input_ids[], int in_count, int out_id) {
    if (net->gate_count >= GATE_NET_MAX_GATES) return -1;
    if (in_count > LOGIC_MAX_INPUTS) in_count = LOGIC_MAX_INPUTS;
    LogicGate* g = &net->gates[net->gate_count];
    g->type = type; g->input_count = in_count; g->output = &net->wires[out_id];
    g->tplh_ps = 100.0; g->tphl_ps = 100.0; g->fan_out_count = 0;
    for (int i = 0; i < in_count; i++)
        g->inputs[i] = (input_ids[i] >= 0 && input_ids[i] < net->wire_count) ? &net->wires[input_ids[i]] : NULL;
    net->sorted = false;
    return net->gate_count++;
}
void gate_net_evaluate(GateNetwork* net) {
    if (!net->sorted) gate_net_topo_sort(net);
    for (int i = 0; i < net->gate_count; i++) {
        int gi = net->order[i];
        if (gi >= 0 && gi < net->gate_count) logic_eval(&net->gates[gi]);
    }
}
bool gate_net_topo_sort(GateNetwork* net) {
    if (net->gate_count == 0) { net->sorted = true; return true; }
    int indegree[GATE_NET_MAX_GATES]; memset(indegree, 0, sizeof(indegree));
    for (int i = 0; i < net->gate_count; i++) {
        Wire* out = net->gates[i].output; if (!out) continue;
        for (int j = 0; j < net->gate_count; j++) {
            if (i == j) continue;
            for (int k = 0; k < net->gates[j].input_count; k++)
                if (net->gates[j].inputs[k] == out) indegree[j]++;
        }
    }
    int queue[GATE_NET_MAX_GATES], qh = 0, qt = 0;
    for (int i = 0; i < net->gate_count; i++)
        if (indegree[i] == 0) queue[qt++] = i;
    int cnt = 0;
    while (qh < qt) {
        int u = queue[qh++]; net->order[cnt++] = u;
        Wire* out = net->gates[u].output; if (!out) continue;
        for (int j = 0; j < net->gate_count; j++) {
            if (u == j) continue;
            for (int k = 0; k < net->gates[j].input_count; k++)
                if (net->gates[j].inputs[k] == out && --indegree[j] == 0) queue[qt++] = j;
        }
    }
    net->sorted = (cnt == net->gate_count);
    for (int i = cnt; i < net->gate_count; i++) net->order[i] = -1;
    return net->sorted;
}
void gate_net_print(const GateNetwork* net) {
    if (!net) return;
    printf("Gate Network: %d gates, %d wires\n", net->gate_count, net->wire_count);
    for (int i = 0; i < net->wire_count; i++)
        printf("  [%d] %s = %d\n", i, net->wires[i].name, net->wires[i].value);
    for (int i = 0; i < net->gate_count; i++) {
        printf("  [%d] %s:", i, gate_type_name(net->gates[i].type));
        for (int j = 0; j < net->gates[i].input_count; j++)
            if (net->gates[i].inputs[j]) printf(" %s", net->gates[i].inputs[j]->name);
        printf(" -> %s\n", net->gates[i].output ? net->gates[i].output->name : "?");
    }
}
static bool dfs_cycle_fn(GateNetwork* net, int idx, int* vis, int* rec) {
    vis[idx] = 1; rec[idx] = 1;
    Wire* out = net->gates[idx].output;
    if (out) {
        for (int j = 0; j < net->gate_count; j++) {
            if (idx == j) continue;
            for (int k = 0; k < net->gates[j].input_count; k++) {
                if (net->gates[j].inputs[k] == out) {
                    if (!vis[j]) { if (dfs_cycle_fn(net, j, vis, rec)) return true; }
                    else if (rec[j]) return true;
                }
            }
        }
    }
    rec[idx] = 0; return false;
}
bool gate_net_is_cyclic(const GateNetwork* net) {
    if (!net) return false;
    int vis[GATE_NET_MAX_GATES] = {0}, rec[GATE_NET_MAX_GATES] = {0};
    for (int i = 0; i < net->gate_count; i++)
        if (!vis[i] && dfs_cycle_fn((GateNetwork*)net, i, vis, rec)) return true;
    return false;
}
int gate_net_logic_depth(GateNetwork* net) {
    if (!net->sorted && !gate_net_topo_sort(net)) return -1;
    int depth[GATE_NET_MAX_GATES]; memset(depth, 0, sizeof(depth));
    int maxd = 0;
    for (int i = 0; i < net->gate_count; i++) {
        int gi = net->order[i]; if (gi < 0) continue;
        for (int j = 0; j < net->gate_count; j++) {
            if (gi == j) continue;
            Wire* out = net->gates[gi].output; if (!out) continue;
            for (int k = 0; k < net->gates[j].input_count; k++)
                if (net->gates[j].inputs[k] == out && depth[j] < depth[gi] + 1) {
                    depth[j] = depth[gi] + 1;
                    if (depth[j] > maxd) maxd = depth[j];
                }
        }
    }
    return maxd;
}
void gate_net_reset(GateNetwork* net) {
    if (!net) return;
    for (int i = 0; i < net->wire_count; i++) net->wires[i].value = false;
}
int gate_net_detect_hazards(GateNetwork* net) {
    if (!net) return 0;
    if (!net->sorted) gate_net_topo_sort(net);
    int hcount = 0;
    for (int i = 0; i < net->gate_count; i++) {
        Wire* out = net->gates[i].output; if (!out) continue;
        int parity[GATE_NET_MAX_GATES];
        for (int p = 0; p < net->gate_count; p++) parity[p] = -1;
        for (int j = 0; j < net->gates[i].input_count; j++) {
            Wire* in = net->gates[i].inputs[j]; if (!in) continue;
            for (int k = 0; k < net->gate_count; k++)
                if (net->gates[k].output == in)
                    parity[k] = (net->gates[k].type == GATE_NOT) ? 1 : 0;
        }
        for (int a = 0; a < net->gate_count; a++) {
            if (parity[a] < 0) continue;
            for (int b = a + 1; b < net->gate_count; b++)
                if (parity[b] >= 0 && parity[a] != parity[b]) {
                    out->is_glitching = true; hcount++;
                }
        }
    }
    return hcount;
}

void logic_print_truth_table(GateType type) {
    TruthTable tt = truth_table_create(type, 2);
    truth_table_print(&tt);
}

/* BoolExpr (L5) */

BoolExpr* bool_expr_const(bool val) {
    BoolExpr* e = (BoolExpr*)malloc(sizeof(BoolExpr));
    if (!e) return NULL;
    e->type = BOOL_CONST; e->const_val = val; e->var_id = -1;
    e->left = e->right = NULL; return e;
}
BoolExpr* bool_expr_var(int var_id) {
    BoolExpr* e = (BoolExpr*)malloc(sizeof(BoolExpr));
    if (!e) return NULL;
    e->type = BOOL_VAR; e->const_val = false; e->var_id = var_id;
    e->left = e->right = NULL; return e;
}
BoolExpr* bool_expr_create(BoolNodeType type, BoolExpr* left, BoolExpr* right) {
    BoolExpr* e = (BoolExpr*)malloc(sizeof(BoolExpr));
    if (!e) return NULL;
    e->type = type; e->const_val = false; e->var_id = -1;
    e->left = left; e->right = right; return e;
}
bool bool_expr_eval(const BoolExpr* expr, const bool* var_values) {
    if (!expr) return false;
    switch (expr->type) {
    case BOOL_CONST: return expr->const_val;
    case BOOL_VAR: return var_values ? var_values[expr->var_id] : false;
    case BOOL_AND: return bool_expr_eval(expr->left, var_values) && bool_expr_eval(expr->right, var_values);
    case BOOL_OR:  return bool_expr_eval(expr->left, var_values) || bool_expr_eval(expr->right, var_values);
    case BOOL_NOT: return !bool_expr_eval(expr->left, var_values);
    case BOOL_XOR: return bool_expr_eval(expr->left, var_values) != bool_expr_eval(expr->right, var_values);
    default: return false;
    }
}
void bool_expr_free(BoolExpr* expr) {
    if (!expr) return;
    bool_expr_free(expr->left); bool_expr_free(expr->right); free(expr);
}
BoolExpr* bool_expr_clone(const BoolExpr* src) {
    if (!src) return NULL;
    BoolExpr* c = (BoolExpr*)malloc(sizeof(BoolExpr));
    if (!c) return NULL;
    c->type = src->type; c->const_val = src->const_val; c->var_id = src->var_id;
    c->left = bool_expr_clone(src->left); c->right = bool_expr_clone(src->right);
    return c;
}
BoolExpr* bool_expr_simplify(BoolExpr* expr) {
    if (!expr) return NULL;
    if (expr->left)  expr->left  = bool_expr_simplify(expr->left);
    if (expr->right) expr->right = bool_expr_simplify(expr->right);
    switch (expr->type) {
    case BOOL_AND:
        if (expr->right && expr->right->type == BOOL_CONST) {
            if (expr->right->const_val) { BoolExpr* r = expr->left; expr->left = NULL; bool_expr_free(expr); return r; }
            else { bool_expr_free(expr->left); bool_expr_free(expr->right); expr->type = BOOL_CONST; expr->const_val = false; expr->left = expr->right = NULL; return expr; }
        }
        if (expr->left && expr->left->type == BOOL_CONST) {
            if (expr->left->const_val) { BoolExpr* r = expr->right; expr->right = NULL; bool_expr_free(expr); return r; }
            else { bool_expr_free(expr->left); bool_expr_free(expr->right); expr->type = BOOL_CONST; expr->const_val = false; expr->left = expr->right = NULL; return expr; }
        }
        break;
    case BOOL_OR:
        if (expr->right && expr->right->type == BOOL_CONST) {
            if (expr->right->const_val) { bool_expr_free(expr->left); bool_expr_free(expr->right); expr->type = BOOL_CONST; expr->const_val = true; expr->left = expr->right = NULL; return expr; }
            else { BoolExpr* r = expr->left; expr->left = NULL; bool_expr_free(expr); return r; }
        }
        if (expr->left && expr->left->type == BOOL_CONST) {
            if (expr->left->const_val) { bool_expr_free(expr->left); bool_expr_free(expr->right); expr->type = BOOL_CONST; expr->const_val = true; expr->left = expr->right = NULL; return expr; }
            else { BoolExpr* r = expr->right; expr->right = NULL; bool_expr_free(expr); return r; }
        }
        break;
    case BOOL_NOT:
        if (expr->left && expr->left->type == BOOL_NOT) {
            BoolExpr* r = expr->left->left; expr->left->left = NULL; bool_expr_free(expr->left); expr->left = NULL; bool_expr_free(expr); return r;
        }
        if (expr->left && expr->left->type == BOOL_CONST) {
            expr->type = BOOL_CONST; expr->const_val = !expr->left->const_val; bool_expr_free(expr->left); expr->left = NULL; return expr;
        }
        break;
    default: break;
    }
    return expr;
}
void bool_expr_print(const BoolExpr* expr) {
    if (!expr) { printf("NULL"); return; }
    switch (expr->type) {
    case BOOL_CONST: printf("%d", expr->const_val ? 1 : 0); break;
    case BOOL_VAR: printf("%c", 'A' + (int)(expr->var_id)); break;
    case BOOL_NOT: printf("!"); bool_expr_print(expr->left); break;
    case BOOL_AND: bool_expr_print(expr->left); printf(" & "); bool_expr_print(expr->right); break;
    case BOOL_OR:  bool_expr_print(expr->left); printf(" | "); bool_expr_print(expr->right); break;
    case BOOL_XOR: bool_expr_print(expr->left); printf(" ^ "); bool_expr_print(expr->right); break;
    }
}
int bool_expr_node_count(const BoolExpr* expr) {
    if (!expr) return 0;
    return 1 + bool_expr_node_count(expr->left) + bool_expr_node_count(expr->right);
}
int bool_expr_depth(const BoolExpr* expr) {
    if (!expr) return 0;
    int ld = bool_expr_depth(expr->left), rd = bool_expr_depth(expr->right);
    return 1 + (ld > rd ? ld : rd);
}
