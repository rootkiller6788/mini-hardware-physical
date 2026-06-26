#include "combinational.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

HalfAdder half_adder_create(void) {
    HalfAdder ha;
    ha.a = wire_create("A"); ha.b = wire_create("B");
    ha.sum = wire_create("SUM"); ha.carry = wire_create("CARRY");
    return ha;
}
void half_adder_set_inputs(HalfAdder* ha, bool a, bool b) {
    if (!ha) return;
    ha->a->value = a;
    ha->b->value = b;
}
void half_adder_evaluate(HalfAdder* ha) {
    if (!ha) return;
    ha->sum->value = (ha->a->value != ha->b->value);
    ha->carry->value = ha->a->value && ha->b->value;
}

FullAdder full_adder_create(void) {
    FullAdder fa;
    fa.a = wire_create("A"); fa.b = wire_create("B");
    fa.cin = wire_create("CIN");
    fa.sum = wire_create("SUM"); fa.cout = wire_create("COUT");
    return fa;
}
void full_adder_set_inputs(FullAdder* fa, bool a, bool b, bool cin) {
    if (!fa) return;
    fa->a->value = a;
    fa->b->value = b;
    fa->cin->value = cin;
}
void full_adder_evaluate(FullAdder* fa) {
    if (!fa) return;
    bool axb = fa->a->value != fa->b->value;
    fa->sum->value = axb != fa->cin->value;
    fa->cout->value = (fa->a->value && fa->b->value) || (axb && fa->cin->value);
}

RippleAdder ripple_adder_create(int width) {
    RippleAdder ra;
    if (width > ADDER_MAX_WIDTH) width = ADDER_MAX_WIDTH;
    if (width < 1) width = 1;
    ra.width = width;
    for (int i = 0; i < width; i++) {
        char name[16];
        snprintf(name, sizeof(name), "A%d", i); ra.a[i] = wire_create(name);
        snprintf(name, sizeof(name), "B%d", i); ra.b[i] = wire_create(name);
        snprintf(name, sizeof(name), "S%d", i); ra.sum[i] = wire_create(name);
    }
    ra.carry_out = wire_create("COUT");
    return ra;
}
void ripple_adder_set_inputs(RippleAdder* ra, uint64_t a, uint64_t b) {
    if (!ra) return;
    for (int i = 0; i < ra->width; i++) {
        ra->a[i]->value = (a >> i) & 1;
        ra->b[i]->value = (b >> i) & 1;
    }
}
uint64_t ripple_adder_compute(RippleAdder* ra) {
    if (!ra) return 0;
    bool carry = false;
    uint64_t sum = 0;
    for (int i = 0; i < ra->width; i++) {
        bool a = ra->a[i]->value, b = ra->b[i]->value;
        bool s = a ^ b ^ carry;
        carry = (a && b) || (carry && (a ^ b));
        sum |= ((uint64_t)s) << i;
    }
    ra->carry_out->value = carry;
    return sum;
}

CarryLookaheadAdder cla_create(int width) {
    CarryLookaheadAdder cla;
    if (width > ADDER_MAX_WIDTH) width = ADDER_MAX_WIDTH;
    if (width < 1) width = 1;
    cla.width = width;
    for (int i = 0; i < width; i++) {
        char name[16];
        snprintf(name, sizeof(name), "A%d", i); cla.a[i] = wire_create(name);
        snprintf(name, sizeof(name), "B%d", i); cla.b[i] = wire_create(name);
        snprintf(name, sizeof(name), "S%d", i); cla.sum[i] = wire_create(name);
        cla.p[i] = false; cla.g[i] = false; cla.c[i] = false;
    }
    cla.carry_in = wire_create("CIN");
    cla.carry_out = wire_create("COUT");
    cla.c[width] = false;
    return cla;
}
void cla_set_inputs(CarryLookaheadAdder* cla, uint64_t a, uint64_t b, bool cin) {
    if (!cla) return;
    for (int i = 0; i < cla->width; i++) {
        bool ai = (a >> i) & 1, bi = (b >> i) & 1;
        cla->a[i]->value = ai; cla->b[i]->value = bi;
        cla->p[i] = ai ^ bi; cla->g[i] = ai && bi;
    }
    cla->c[0] = cin;
}
uint64_t cla_compute(CarryLookaheadAdder* cla) {
    if (!cla) return 0;
    for (int i = 0; i < cla->width; i++)
        cla->c[i + 1] = cla->g[i] || (cla->p[i] && cla->c[i]);
    uint64_t sum = 0;
    for (int i = 0; i < cla->width; i++) {
        bool si = cla->p[i] ^ cla->c[i];
        cla->sum[i]->value = si;
        sum |= ((uint64_t)si) << i;
    }
    cla->carry_out->value = cla->c[cla->width];
    return sum;
}
bool cla_get_carry_out(const CarryLookaheadAdder* cla) { return cla ? cla->carry_out->value : false; }
void cla_print_signals(const CarryLookaheadAdder* cla) {
    if (!cla) return;
    printf("CLA (w=%d) P=", cla->width);
    for (int i=0;i<cla->width;i++) printf("%d",cla->p[i]);
    printf(" G=");
    for (int i=0;i<cla->width;i++) printf("%d",cla->g[i]);
    printf(" Cout=%d\n", cla->carry_out->value);
}

ArrayMultiplier array_mul_create(int a_w, int b_w) {
    ArrayMultiplier am;
    am.a_width = (a_w > MUL_MAX_WIDTH) ? MUL_MAX_WIDTH : a_w;
    am.b_width = (b_w > MUL_MAX_WIDTH) ? MUL_MAX_WIDTH : b_w;
    am.a_val = 0; am.b_val = 0; am.product = 0;
    return am;
}
void array_mul_set_inputs(ArrayMultiplier* am, uint32_t a, uint32_t b) {
    if (!am) return;
    am->a_val = a;
    am->b_val = b;
}
uint64_t array_mul_compute(ArrayMultiplier* am) {
    if (!am) return 0;
    uint64_t result = 0;
    for (int i = 0; i < am->b_width; i++)
        if ((am->b_val >> i) & 1) result += ((uint64_t)am->a_val) << i;
    am->product = result;
    return result;
}

MagnitudeComparator mag_comp_create(int width) {
    MagnitudeComparator mc;
    mc.width = (width > COMP_MAX_WIDTH) ? COMP_MAX_WIDTH : width;
    if (mc.width < 1) mc.width = 1;
    mc.a_val = 0; mc.b_val = 0; mc.eq = false; mc.gt = false; mc.lt = false;
    return mc;
}
void mag_comp_set_inputs(MagnitudeComparator* mc, uint64_t a, uint64_t b) {
    if (!mc) return;
    mc->a_val = a;
    mc->b_val = b;
}
void mag_comp_evaluate(MagnitudeComparator* mc) {
    if (!mc) return;
    mc->gt = mc->a_val > mc->b_val;
    mc->lt = mc->a_val < mc->b_val;
    mc->eq = mc->a_val == mc->b_val;
}

Multiplexer mux_create(int num_inputs) {
    Multiplexer m;
    m.num_inputs = (num_inputs > MUX_MAX_INPUTS) ? MUX_MAX_INPUTS : num_inputs;
    if (m.num_inputs < 2) m.num_inputs = 2;
    m.sel_width = 0; int n = m.num_inputs - 1;
    while (n > 0) { m.sel_width++; n >>= 1; }
    m.sel = 0; m.output = 0;
    for (int i = 0; i < MUX_MAX_INPUTS; i++) m.inputs[i] = 0;
    return m;
}
void mux_select(Multiplexer* m, int sel) {
    if (!m) return;
    m->sel = sel;
    if (sel >= 0 && sel < m->num_inputs) m->output = m->inputs[sel];
}
uint64_t mux_get_output(const Multiplexer* m) { return m ? m->output : 0; }
void mux_set_input(Multiplexer* m, int index, uint64_t val) {
    if (m && index >= 0 && index < m->num_inputs) m->inputs[index] = val;
}

Decoder decoder_create(int n_inputs) {
    Decoder d;
    d.n_inputs = (n_inputs > DECODER_MAX_N) ? DECODER_MAX_N : n_inputs;
    if (d.n_inputs < 1) d.n_inputs = 1;
    d.input_val = 0; memset(d.output_mask, 0, sizeof(d.output_mask));
    return d;
}
void decoder_set_input(Decoder* d, int val) {
    if (!d) return;
    d->input_val = val; int n_out = 1 << d->n_inputs;
    memset(d->output_mask, 0, sizeof(d->output_mask));
    if (val >= 0 && val < n_out) d->output_mask[val] = 1;
}
bool decoder_get_output(const Decoder* d, int index) {
    if (!d || index < 0 || index >= 256) return false;
    return d->output_mask[index] != 0;
}
void decoder_print(const Decoder* d) {
    if (!d) return;
    printf("Decoder(n=%d,in=%d): ", d->n_inputs, d->input_val);
    int n_out = 1 << d->n_inputs;
    for (int i = 0; i < n_out; i++) printf("%d", d->output_mask[i]);
    printf("\n");
}

PriorityEncoder pr_encoder_create(int n_inputs) {
    PriorityEncoder pe;
    pe.n_inputs = (n_inputs > 64) ? 64 : (n_inputs < 2 ? 2 : n_inputs);
    pe.n_outputs = 0; int n = pe.n_inputs - 1;
    while (n > 0) { pe.n_outputs++; n >>= 1; }
    pe.input_mask = 0; pe.output_val = 0; pe.valid = false;
    return pe;
}
void pr_encoder_set_inputs(PriorityEncoder* pe, uint64_t mask) {
    if (!pe) return;
    pe->input_mask = mask; pe->valid = false; pe->output_val = 0;
    for (int i = pe->n_inputs - 1; i >= 0; i--)
        if ((mask >> i) & 1) { pe->output_val = i; pe->valid = true; break; }
}
int pr_encoder_get_output(const PriorityEncoder* pe) { return pe ? pe->output_val : 0; }
bool pr_encoder_is_valid(const PriorityEncoder* pe) { return pe ? pe->valid : false; }

BarrelShifter barrel_create(int width) {
    BarrelShifter bs;
    bs.width = (width > BARREL_MAX_WIDTH) ? BARREL_MAX_WIDTH : width;
    if (bs.width < 1) bs.width = 1;
    bs.type = SHIFT_LL; bs.input_val = 0; bs.shift_amount = 0; bs.output_val = 0;
    return bs;
}
void barrel_set_input(BarrelShifter* bs, uint64_t val) { if (bs) bs->input_val = val; }
void barrel_set_shift(BarrelShifter* bs, ShiftType type, int amount) {
    if (!bs) return;
    bs->type = type;
    bs->shift_amount = amount % bs->width;
}
uint64_t barrel_compute(BarrelShifter* bs) {
    if (!bs) return 0;
    uint64_t in = bs->input_val;
    int sh = bs->shift_amount, w = bs->width;
    uint64_t mask = (w == 64) ? ~0ULL : ((1ULL << w) - 1);
    in &= mask;
    uint64_t result = 0;
    switch (bs->type) {
    case SHIFT_LL: result = (in << sh) & mask; break;
    case SHIFT_LR: result = (in >> sh) & mask; break;
    case SHIFT_AR:
        if (in & (1ULL << (w - 1)))
            result = (in >> sh) | (((1ULL << sh) - 1) << (w - sh));
        else result = (in >> sh);
        result &= mask; break;
    case SHIFT_ROL: result = ((in << sh) | (in >> (w - sh))) & mask; break;
    case SHIFT_ROR: result = ((in >> sh) | (in << (w - sh))) & mask; break;
    }
    bs->output_val = result;
    return result;
}

Combinational comb_create(void) { Combinational c; c.gate_count = 0; c.wire_count = 0; return c; }
int comb_add_wire(Combinational* c, const char* name) {
    if (c->wire_count >= COMB_MAX_WIRES) return -1;
    Wire* w = &c->wires[c->wire_count];
    w->value = false; w->delay_ps = 0; w->is_glitching = false; w->tri_value = SIG_LOW;
    if (name) { strncpy(w->name, name, LOGIC_MAX_WIRE_NAME - 1); w->name[LOGIC_MAX_WIRE_NAME - 1] = '\0'; }
    else snprintf(w->name, LOGIC_MAX_WIRE_NAME, "w%d", c->wire_count);
    return c->wire_count++;
}
int comb_add_gate(Combinational* c, GateType type, int wire_ids[], int in_count, int out_id) {
    if (c->gate_count >= COMB_MAX_GATES) return -1;
    if (in_count > LOGIC_MAX_INPUTS) in_count = LOGIC_MAX_INPUTS;
    LogicGate* g = &c->gates[c->gate_count];
    g->type = type; g->input_count = in_count; g->output = &c->wires[out_id];
    g->tplh_ps = 100.0; g->tphl_ps = 100.0; g->fan_out_count = 0;
    for (int i = 0; i < in_count; i++)
        g->inputs[i] = (wire_ids[i] >= 0 && wire_ids[i] < c->wire_count) ? &c->wires[wire_ids[i]] : NULL;
    return c->gate_count++;
}
void comb_evaluate(Combinational* c) { for (int i = 0; i < c->gate_count; i++) logic_eval(&c->gates[i]); }
void comb_print(const Combinational* c) {
    printf("Combinational: %d gates, %d wires\n", c->gate_count, c->wire_count);
    for (int i = 0; i < c->wire_count; i++)
        printf("  %s = %d\n", c->wires[i].name, c->wires[i].value);
}
