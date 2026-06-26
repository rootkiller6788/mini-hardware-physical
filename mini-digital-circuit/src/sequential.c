#include "sequential.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DFlipFlop dff_create(const char* name) {
    DFlipFlop dff;
    dff.q = false; dff.q_bar = true; dff.clk = false; dff.d = false;
    dff.reset = false; dff.preset = false;
    dff.tsetup_ps = 50.0; dff.thold_ps = 50.0;
    if (name) { strncpy(dff.name, name, SEQ_MAX_DFF_NAME - 1); dff.name[SEQ_MAX_DFF_NAME - 1] = '\0'; }
    else dff.name[0] = '\0';
    return dff;
}
void dff_set_d(DFlipFlop* dff, bool val) { if (dff) dff->d = val; }
void dff_clock(DFlipFlop* dff) {
    if (!dff) return;
    if (!dff->clk) {
        dff->clk = true;
        if (dff->reset) { dff->q = false; dff->q_bar = true; }
        else if (dff->preset) { dff->q = true; dff->q_bar = false; }
        else { dff->q = dff->d; dff->q_bar = !dff->d; }
        dff->clk = false;
    }
}
void dff_reset(DFlipFlop* dff, bool async) {
    if (!dff) return;
    if (async) { dff->q = false; dff->q_bar = true; }
    else dff->reset = true;
}
void dff_preset(DFlipFlop* dff) {
    if (!dff) return;
    dff->preset = true; dff->q = true; dff->q_bar = false;
}
void dff_clock_with_enable(DFlipFlop* dff, bool enable) {
    if (!dff) return;
    if (enable) dff_clock(dff);
}

JKFlipFlop jkff_create(const char* name) {
    JKFlipFlop jk;
    jk.q = false; jk.q_bar = true; jk.j = false; jk.k = false; jk.clk = false;
    if (name) { strncpy(jk.name, name, SEQ_MAX_DFF_NAME - 1); jk.name[SEQ_MAX_DFF_NAME - 1] = '\0'; }
    else jk.name[0] = '\0';
    return jk;
}
void jkff_set_inputs(JKFlipFlop* jk, bool j, bool k) { if (jk) { jk->j = j; jk->k = k; } }
void jkff_clock(JKFlipFlop* jk) {
    if (!jk) return;
    if (!jk->clk) {
        jk->clk = true;
        if (jk->j && jk->k) { bool t = jk->q; jk->q = jk->q_bar; jk->q_bar = t; }
        else if (jk->j && !jk->k) { jk->q = true;  jk->q_bar = false; }
        else if (!jk->j && jk->k) { jk->q = false; jk->q_bar = true; }
        jk->clk = false;
    }
}
void jkff_reset(JKFlipFlop* jk) { if (jk) { jk->q = false; jk->q_bar = true; } }

TFlipFlop tff_create(const char* name) {
    TFlipFlop tf;
    tf.q = false; tf.q_bar = true; tf.t = false; tf.clk = false;
    if (name) { strncpy(tf.name, name, SEQ_MAX_DFF_NAME - 1); tf.name[SEQ_MAX_DFF_NAME - 1] = '\0'; }
    else tf.name[0] = '\0';
    return tf;
}
void tff_set_t(TFlipFlop* tf, bool t) { if (tf) tf->t = t; }
void tff_clock(TFlipFlop* tf) {
    if (!tf) return;
    if (!tf->clk) {
        tf->clk = true;
        if (tf->t) { bool tmp = tf->q; tf->q = tf->q_bar; tf->q_bar = tmp; }
        tf->clk = false;
    }
}

Register reg_create(int width) {
    Register r;
    r.width = (width > REG_MAX_WIDTH) ? REG_MAX_WIDTH : width;
    if (r.width < 1) r.width = 1;
    for (int i = 0; i < r.width; i++) {
        char name[SEQ_MAX_DFF_NAME];
        snprintf(name, sizeof(name), "r%d", i);
        r.dffs[i] = dff_create(name);
    }
    return r;
}
void reg_set_d(Register* r, int bit_index, bool val) {
    if (r && bit_index >= 0 && bit_index < r->width) dff_set_d(&r->dffs[bit_index], val);
}
void reg_set_value(Register* r, uint64_t value) {
    if (!r) return;
    for (int i = 0; i < r->width; i++) {
        r->dffs[i].d = (value >> i) & 1;
        dff_clock(&r->dffs[i]);
    }
}
void reg_clock(Register* r) {
    if (!r) return;
    for (int i = 0; i < r->width; i++) dff_clock(&r->dffs[i]);
}
uint64_t reg_read(const Register* r) {
    if (!r) return 0;
    uint64_t val = 0;
    for (int i = 0; i < r->width; i++)
        if (r->dffs[i].q) val |= (1ULL << i);
    return val;
}
int reg_get_width(const Register* r) { return r ? r->width : 0; }

SRLatch sr_latch_create(void) {
    SRLatch l; l.q = false; l.q_bar = true; l.s = false; l.r = false; return l;
}
void sr_latch_set_inputs(SRLatch* latch, bool s, bool r) { if (latch) { latch->s = s; latch->r = r; } }
void sr_latch_eval(SRLatch* latch) {
    if (!latch) return;
    if (latch->s && latch->r) return;
    else if (latch->s) { latch->q = true; latch->q_bar = false; }
    else if (latch->r) { latch->q = false; latch->q_bar = true; }
}
DLatch d_latch_create(void) {
    DLatch l; l.q = false; l.q_bar = true; l.d = false; l.enable = false; return l;
}
void d_latch_set_inputs(DLatch* latch, bool d, bool enable) { if (latch) { latch->d = d; latch->enable = enable; } }
void d_latch_eval(DLatch* latch) {
    if (!latch) return;
    if (latch->enable) { latch->q = latch->d; latch->q_bar = !latch->d; }
}
bool d_latch_output(const DLatch* latch) { return latch ? latch->q : false; }

ShiftRegister shift_reg_create(int width, ShiftRegDir dir) {
    ShiftRegister sr;
    sr.width = (width > SHIFT_REG_MAX) ? SHIFT_REG_MAX : width;
    if (sr.width < 1) sr.width = 1;
    sr.direction = dir; sr.serial_in_left = false; sr.serial_in_right = false;
    for (int i = 0; i < sr.width; i++) sr.bits[i] = false;
    return sr;
}
void shift_reg_load(ShiftRegister* sr, const bool* data) {
    if (!sr || !data) return;
    for (int i = 0; i < sr->width; i++) sr->bits[i] = data[i];
}
void shift_reg_serial_in(ShiftRegister* sr, bool left_bit, bool right_bit) {
    if (!sr) return;
    sr->serial_in_left = left_bit; sr->serial_in_right = right_bit;
}
void shift_reg_shift(ShiftRegister* sr) {
    if (!sr) return;
    switch (sr->direction) {
    case SHIFT_LEFT:
        for (int i = sr->width - 1; i > 0; i--) sr->bits[i] = sr->bits[i - 1];
        sr->bits[0] = sr->serial_in_left; break;
    case SHIFT_RIGHT:
        for (int i = 0; i < sr->width - 1; i++) sr->bits[i] = sr->bits[i + 1];
        sr->bits[sr->width - 1] = sr->serial_in_right; break;
    case SHIFT_BIDIR:
        for (int i = sr->width - 1; i > 0; i--) sr->bits[i] = sr->bits[i - 1];
        sr->bits[0] = sr->serial_in_left; break;
    }
}
void shift_reg_read(const ShiftRegister* sr, bool* out) {
    if (!sr || !out) return;
    for (int i = 0; i < sr->width; i++) out[i] = sr->bits[i];
}
bool shift_reg_serial_out_left(const ShiftRegister* sr) { return sr ? sr->bits[sr->width - 1] : false; }
bool shift_reg_serial_out_right(const ShiftRegister* sr) { return sr ? sr->bits[0] : false; }

static uint64_t bin2gray(uint64_t n) { return n ^ (n >> 1); }
static uint64_t gray2bin(uint64_t n) { uint64_t m = n; while (m) { m >>= 1; n ^= m; } return n; }

Counter counter_create(CounterType type, int num_bits) {
    Counter c; c.type = type;
    c.num_bits = (num_bits > COUNTER_MAX_BITS) ? COUNTER_MAX_BITS : num_bits;
    if (c.num_bits < 1) c.num_bits = 1;
    c.value = 0; c.up = true;
    c.max_value = (1ULL << c.num_bits) - 1;
    if (type == COUNTER_BCD) c.max_value = 9;
    return c;
}
void counter_reset(Counter* c) { if (c) c->value = 0; }
void counter_tick(Counter* c) {
    if (!c) return;
    switch (c->type) {
    case COUNTER_BINARY: case COUNTER_UP_DOWN:
        c->value = c->up ? (c->value + 1) & c->max_value : (c->value - 1) & c->max_value;
        break;
    case COUNTER_BCD:
        c->value = c->up ? (c->value + 1) % 10 : (c->value == 0 ? 9 : c->value - 1);
        break;
    case COUNTER_GRAY: {
        uint64_t bin = gray2bin(c->value);
        bin = c->up ? (bin + 1) & c->max_value : (bin - 1) & c->max_value;
        c->value = bin2gray(bin); break;
    }
    case COUNTER_JOHNSON: case COUNTER_RING:
        c->value = c->up ? (c->value + 1) & c->max_value : (c->value - 1) & c->max_value;
        break;
    }
}
uint64_t counter_read(const Counter* c) { return c ? c->value : 0; }
void counter_set_direction(Counter* c, bool up) { if (c) c->up = up; }
