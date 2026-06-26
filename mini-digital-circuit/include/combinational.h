/**
 * combinational.h — L2: Combinational Logic Building Blocks
 *
 * Knowledge coverage:
 *   L1: HalfAdder, FullAdder, RippleAdder, CarryLookaheadAdder,
 *       Comparator, Decoder, Encoder, Mux, BarrelShifter structs
 *   L2: Arithmetic circuits, data-routing circuits
 *   L3: Multi-bit bus architectures, parameterized widths
 *   L5: Carry-lookahead algorithm for fast addition
 *
 * References:
 *   Patterson & Hennessy — Computer Organization and Design
 *   Morris Mano — Digital Design, Ch4 (Combinational Logic)
 */
#ifndef COMBINATIONAL_H
#define COMBINATIONAL_H

#include <stdbool.h>
#include <stdint.h>
#include "logic_gate.h"

#define COMB_MAX_GATES    256
#define COMB_MAX_WIRES    512
#define COMB_MAX_BITWIDTH  64
#define ADDER_MAX_WIDTH    64

/* ---- L1: Arithmetic Units ---- */

/** Half Adder: adds two 1-bit inputs, producing sum and carry */
typedef struct {
    Wire* a;
    Wire* b;
    Wire* sum;
    Wire* carry;
} HalfAdder;

/** Full Adder: adds three 1-bit inputs (a, b, carry-in) */
typedef struct {
    Wire* a;
    Wire* b;
    Wire* cin;
    Wire* sum;
    Wire* cout;
} FullAdder;

/** Ripple-Carry Adder: N-bit adder with O(N) delay.
 *  Simple but slow for large N due to carry propagation chain. */
typedef struct {
    Wire*   a[ADDER_MAX_WIDTH];
    Wire*   b[ADDER_MAX_WIDTH];
    Wire*   sum[ADDER_MAX_WIDTH];
    Wire*   carry_out;
    int     width;
} RippleAdder;

/** Carry-Lookahead Adder (CLA): N-bit adder with O(log N) delay.
 *  Uses generate/propagate signals for parallel carry computation.
 *  L5: Implements the CLA algorithm for fast addition. */
typedef struct {
    Wire*   a[ADDER_MAX_WIDTH];
    Wire*   b[ADDER_MAX_WIDTH];
    Wire*   sum[ADDER_MAX_WIDTH];
    Wire*   carry_in;
    Wire*   carry_out;
    int     width;
    /* Internal propagate/generate signals */
    bool    p[ADDER_MAX_WIDTH];
    bool    g[ADDER_MAX_WIDTH];
    bool    c[ADDER_MAX_WIDTH + 1];
} CarryLookaheadAdder;

/** Array Multiplier: N×N combinational multiplier.
 *  Implements shift-and-add multiplication using AND gates + adders. */
#define MUL_MAX_WIDTH 16

typedef struct {
    int     a_width;
    int     b_width;
    uint32_t a_val;
    uint32_t b_val;
    uint64_t product;
} ArrayMultiplier;

/* ---- L1: Comparison & Routing ---- */

/** Magnitude Comparator: compares two N-bit numbers.
 *  Produces EQ, GT, LT flags. */
#define COMP_MAX_WIDTH 64

typedef struct {
    int      width;
    uint64_t a_val;
    uint64_t b_val;
    bool     eq;     /* a == b */
    bool     gt;     /* a > b */
    bool     lt;     /* a < b */
} MagnitudeComparator;

/** Multiplexer: N-to-1 selector with sel_width select lines. */
#define MUX_MAX_INPUTS 32

typedef struct {
    int     num_inputs;
    int     sel_width;
    uint64_t inputs[MUX_MAX_INPUTS];
    int     sel;
    uint64_t output;
} Multiplexer;

/** Decoder: N-to-2^N decoder. Activates exactly one output. */
#define DECODER_MAX_N 8

typedef struct {
    int     n_inputs;
    int     input_val;
    uint8_t output_mask[256]; /* output_mask[i] = 1 if output i active */
} Decoder;

/** Priority Encoder: 2^N-to-N encoder. Outputs index of highest-priority
 *  active input. If no input active, valid flag is false. */
typedef struct {
    int     n_inputs;
    int     n_outputs;
    uint64_t input_mask;
    int     output_val;
    bool    valid;
} PriorityEncoder;

/** Barrel Shifter: shifts/rotates N-bit input by k positions.
 *  Supports: logical left/right, arithmetic right, rotate left/right. */
#define BARREL_MAX_WIDTH 64

typedef enum {
    SHIFT_LL,   /* logical left */
    SHIFT_LR,   /* logical right */
    SHIFT_AR,   /* arithmetic right (sign-extend) */
    SHIFT_ROL,  /* rotate left */
    SHIFT_ROR   /* rotate right */
} ShiftType;

typedef struct {
    int        width;
    ShiftType  type;
    uint64_t   input_val;
    int        shift_amount;
    uint64_t   output_val;
} BarrelShifter;

/* ---- Combinational Circuit Container ---- */

/** Combinational: a netlist-based combinational circuit.
 *  Similar to GateNetwork but with higher-level building blocks. */
typedef struct {
    LogicGate   gates[COMB_MAX_GATES];
    int         gate_count;
    Wire        wires[COMB_MAX_WIRES];
    int         wire_count;
} Combinational;

/* ---- L1: API ---- */

/* Half Adder */
HalfAdder   half_adder_create(void);
void        half_adder_set_inputs(HalfAdder* ha, bool a, bool b);
void        half_adder_evaluate(HalfAdder* ha);

/* Full Adder */
FullAdder   full_adder_create(void);
void        full_adder_set_inputs(FullAdder* fa, bool a, bool b, bool cin);
void        full_adder_evaluate(FullAdder* fa);

/* Ripple Adder */
RippleAdder ripple_adder_create(int width);
void        ripple_adder_set_inputs(RippleAdder* ra, uint64_t a, uint64_t b);
uint64_t    ripple_adder_compute(RippleAdder* ra);

/* Carry-Lookahead Adder (L5 algorithm) */
CarryLookaheadAdder cla_create(int width);
void        cla_set_inputs(CarryLookaheadAdder* cla, uint64_t a, uint64_t b, bool cin);
uint64_t    cla_compute(CarryLookaheadAdder* cla);
bool        cla_get_carry_out(const CarryLookaheadAdder* cla);
void        cla_print_signals(const CarryLookaheadAdder* cla);

/* Array Multiplier (L5 algorithm) */
ArrayMultiplier array_mul_create(int a_w, int b_w);
void        array_mul_set_inputs(ArrayMultiplier* am, uint32_t a, uint32_t b);
uint64_t    array_mul_compute(ArrayMultiplier* am);

/* Magnitude Comparator */
MagnitudeComparator mag_comp_create(int width);
void        mag_comp_set_inputs(MagnitudeComparator* mc, uint64_t a, uint64_t b);
void        mag_comp_evaluate(MagnitudeComparator* mc);

/* Multiplexer */
Multiplexer mux_create(int num_inputs);
void        mux_select(Multiplexer* m, int sel);
uint64_t    mux_get_output(const Multiplexer* m);
void        mux_set_input(Multiplexer* m, int index, uint64_t val);

/* Decoder */
Decoder     decoder_create(int n_inputs);
void        decoder_set_input(Decoder* d, int val);
bool        decoder_get_output(const Decoder* d, int index);
void        decoder_print(const Decoder* d);

/* Priority Encoder */
PriorityEncoder pr_encoder_create(int n_inputs);
void        pr_encoder_set_inputs(PriorityEncoder* pe, uint64_t mask);
int         pr_encoder_get_output(const PriorityEncoder* pe);
bool        pr_encoder_is_valid(const PriorityEncoder* pe);

/* Barrel Shifter */
BarrelShifter barrel_create(int width);
void        barrel_set_input(BarrelShifter* bs, uint64_t val);
void        barrel_set_shift(BarrelShifter* bs, ShiftType type, int amount);
uint64_t    barrel_compute(BarrelShifter* bs);

/* Combinational container */
Combinational comb_create(void);
int         comb_add_wire(Combinational* c, const char* name);
int         comb_add_gate(Combinational* c, GateType type,
                          int wire_ids[], int in_count, int out_id);
void        comb_evaluate(Combinational* c);
void        comb_print(const Combinational* c);

#endif /* COMBINATIONAL_H */
