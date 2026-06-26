#include "sequential.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ----- D Flip-Flop ----- */
DFlipFlop dff_create(const char* name) {
    DFlipFlop dff;
    dff.q     = false;
    dff.q_bar = true;
    dff.clk   = false;
    dff.d     = false;
    strncpy(dff.name, name, SEQ_MAX_DFF_NAME - 1);
    dff.name[SEQ_MAX_DFF_NAME - 1] = '\0';
    return dff;
}

void dff_clock(DFlipFlop* dff) {
    /* 上升沿触发：clk 从 0 → 1 时采样 d */
    dff->clk   = true;
    dff->q     = dff->d;
    dff->q_bar = !dff->d;
    /* 保持后 clk 归 0（模拟脉冲） */
    dff->clk = false;
}

/* ----- Register ----- */
Register reg_create(int width) {
    Register r;
    r.width = (width < REG_MAX_WIDTH) ? width : REG_MAX_WIDTH;
    for (int i = 0; i < r.width; i++) {
        char name[SEQ_MAX_DFF_NAME];
        snprintf(name, sizeof(name), "reg_bit_%d", i);
        r.dffs[i] = dff_create(name);
    }
    return r;
}

void reg_write(Register* reg, unsigned long long value) {
    for (int i = 0; i < reg->width; i++) {
        reg->dffs[i].d = (value >> i) & 1ULL;
        dff_clock(&reg->dffs[i]);
    }
}

unsigned long long reg_read(const Register* reg) {
    unsigned long long value = 0;
    for (int i = 0; i < reg->width; i++) {
        if (reg->dffs[i].q) {
            value |= (1ULL << i);
        }
    }
    return value;
}

/* ----- SR Latch ----- */
SRLatch sr_latch_create(void) {
    SRLatch l;
    l.q     = false;
    l.q_bar = true;
    l.s     = false;
    l.r     = false;
    return l;
}

void sr_latch_set_s(SRLatch* latch) {
    latch->s = true;
    latch->r = false;
    sr_latch_eval(latch);
}

void sr_latch_set_r(SRLatch* latch) {
    latch->s = false;
    latch->r = true;
    sr_latch_eval(latch);
}

void sr_latch_eval(SRLatch* latch) {
    /* SR 锁存器行为表 */
    if (latch->s && latch->r) {
        /* 无效状态：保持之前值 */
        return;
    } else if (latch->s) {
        latch->q     = true;
        latch->q_bar = false;
    } else if (latch->r) {
        latch->q     = false;
        latch->q_bar = true;
    }
    /* s=0, r=0: 保持 */
}

/* ----- D Latch ----- */
DLatch d_latch_create(void) {
    DLatch l;
    l.q      = false;
    l.q_bar  = true;
    l.d      = false;
    l.enable = false;
    return l;
}

void d_latch_set(DLatch* latch, bool val) {
    latch->d = val;
    if (latch->enable) {
        latch->q     = val;
        latch->q_bar = !val;
    }
}

void d_latch_enable(DLatch* latch, bool en) {
    latch->enable = en;
    if (en) {
        latch->q     = latch->d;
        latch->q_bar = !latch->d;
    }
}

bool d_latch_output(const DLatch* latch) {
    return latch->q;
}
