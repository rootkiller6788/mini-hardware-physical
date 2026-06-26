/**
 * sequential.h — L2: Sequential Logic Elements
 *
 * Knowledge coverage:
 *   L1: DFlipFlop, JKFlipFlop, TFlipFlop, Register, SRLatch, DLatch
 *   L2: Clocked vs level-sensitive storage elements
 *   L3: Multi-bit registers, shift registers, counters
 *   L4: Setup/hold time constraints, metastability
 *   L5: Binary/BCD/Gray/Johnson/Ring counter implementations
 *
 * References:
 *   MIT 6.004 — Sequential Logic (L10-L12)
 *   Morris Mano — Digital Design, Ch5 (Synchronous Sequential Logic)
 */
#ifndef SEQUENTIAL_H
#define SEQUENTIAL_H

#include <stdbool.h>
#include <stdint.h>

#define SEQ_MAX_DFF_NAME  64
#define REG_MAX_WIDTH     64
#define SHIFT_REG_MAX     128
#define COUNTER_MAX_BITS  64

/* ---- L1: Basic Flip-Flops & Latches ---- */

/** D Flip-Flop: positive-edge-triggered.
 *  On clk 0→1 transition, Q ← D, Q_bar ← !D. */
typedef struct {
    bool    q;
    bool    q_bar;
    bool    clk;
    bool    d;
    bool    reset;
    bool    preset;
    double  tsetup_ps;  /* setup time constraint (L4) */
    double  thold_ps;   /* hold time constraint (L4) */
    char    name[SEQ_MAX_DFF_NAME];
} DFlipFlop;

/** JK Flip-Flop: resolves the "forbidden" SR=11 state by toggling.
 *  J=1,K=0 → SET; J=0,K=1 → RESET; J=1,K=1 → TOGGLE; J=0,K=0 → HOLD */
typedef struct {
    bool    q;
    bool    q_bar;
    bool    j;
    bool    k;
    bool    clk;
    char    name[SEQ_MAX_DFF_NAME];
} JKFlipFlop;

/** T (Toggle) Flip-Flop: T=1 toggles output on clock edge.
 *  Built from JK-FF with J=K=T. */
typedef struct {
    bool    q;
    bool    q_bar;
    bool    t;
    bool    clk;
    char    name[SEQ_MAX_DFF_NAME];
} TFlipFlop;

/** Multi-bit Register: N DFFs sharing a clock.
 *  Parallel load, parallel read. */
typedef struct {
    int         width;
    DFlipFlop   dffs[REG_MAX_WIDTH];
} Register;

/* ---- L1: Latches (Level-Sensitive) ---- */

/** SR Latch: Set-Reset latch built from cross-coupled NOR gates.
 *  S=1,R=0 → SET; S=0,R=1 → RESET; S=0,R=0 → HOLD; S=1,R=1 → invalid */
typedef struct {
    bool    q;
    bool    q_bar;
    bool    s;
    bool    r;
} SRLatch;

/** D Latch: transparent when enable=1, holds when enable=0 */
typedef struct {
    bool    q;
    bool    q_bar;
    bool    d;
    bool    enable;
} DLatch;

/* ---- L5: Shift Registers ---- */

typedef enum {
    SHIFT_LEFT,
    SHIFT_RIGHT,
    SHIFT_BIDIR
} ShiftRegDir;

/** Universal Shift Register: supports parallel load, serial in/out,
 *  left/right shift, and hold operations. */
typedef struct {
    int         width;
    bool        bits[SHIFT_REG_MAX];
    bool        serial_in_left;
    bool        serial_in_right;
    ShiftRegDir direction;
} ShiftRegister;

/* ---- L5: Counters ---- */

typedef enum {
    COUNTER_BINARY,     /* standard binary up-counter */
    COUNTER_BCD,        /* binary-coded decimal (0-9) */
    COUNTER_GRAY,       /* Gray code (adjacent values differ by 1 bit) */
    COUNTER_JOHNSON,    /* Johnson/twisted-ring counter */
    COUNTER_RING,       /* Ring counter (one-hot) */
    COUNTER_UP_DOWN     /* bidirectional binary counter */
} CounterType;

typedef struct {
    CounterType type;
    int         num_bits;
    uint64_t    value;
    uint64_t    max_value;
    bool        up;     /* direction for up/down */
} Counter;

/* ---- L1: API — D Flip-Flop ---- */
DFlipFlop  dff_create(const char* name);
void       dff_set_d(DFlipFlop* dff, bool val);
void       dff_clock(DFlipFlop* dff);
void       dff_reset(DFlipFlop* dff, bool async);
void       dff_preset(DFlipFlop* dff);
void       dff_clock_with_enable(DFlipFlop* dff, bool enable);

/* ---- L1: API — JK Flip-Flop ---- */
JKFlipFlop jkff_create(const char* name);
void       jkff_set_inputs(JKFlipFlop* jk, bool j, bool k);
void       jkff_clock(JKFlipFlop* jk);
void       jkff_reset(JKFlipFlop* jk);

/* ---- L1: API — T Flip-Flop ---- */
TFlipFlop  tff_create(const char* name);
void       tff_set_t(TFlipFlop* tf, bool t);
void       tff_clock(TFlipFlop* tf);

/* ---- L1: API — Register ---- */
Register   reg_create(int width);
void       reg_set_d(Register* r, int bit_index, bool val);
void       reg_set_value(Register* r, uint64_t value);
void       reg_clock(Register* r);
uint64_t   reg_read(const Register* r);
int        reg_get_width(const Register* r);

/* ---- L1: API — SR Latch ---- */
SRLatch    sr_latch_create(void);
void       sr_latch_set_inputs(SRLatch* latch, bool s, bool r);
void       sr_latch_eval(SRLatch* latch);

/* ---- L1: API — D Latch ---- */
DLatch     d_latch_create(void);
void       d_latch_set_inputs(DLatch* latch, bool d, bool enable);
void       d_latch_eval(DLatch* latch);
bool       d_latch_output(const DLatch* latch);

/* ---- L5: API — Shift Register ---- */
ShiftRegister shift_reg_create(int width, ShiftRegDir dir);
void       shift_reg_load(ShiftRegister* sr, const bool* data);
void       shift_reg_serial_in(ShiftRegister* sr, bool left_bit, bool right_bit);
void       shift_reg_shift(ShiftRegister* sr);
void       shift_reg_read(const ShiftRegister* sr, bool* out);
bool       shift_reg_serial_out_left(const ShiftRegister* sr);
bool       shift_reg_serial_out_right(const ShiftRegister* sr);

/* ---- L5: API — Counter ---- */
Counter    counter_create(CounterType type, int num_bits);
void       counter_reset(Counter* c);
void       counter_tick(Counter* c);
uint64_t   counter_read(const Counter* c);
void       counter_set_direction(Counter* c, bool up);

#endif /* SEQUENTIAL_H */
