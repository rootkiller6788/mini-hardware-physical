#ifndef SEQUENTIAL_H
#define SEQUENTIAL_H

#include <stdbool.h>

#define SEQ_MAX_DFF_NAME 32
#define REG_MAX_WIDTH    64

typedef struct {
    bool  q;
    bool  q_bar;
    bool  clk;
    bool  d;
    char  name[SEQ_MAX_DFF_NAME];
} DFlipFlop;

typedef struct {
    int         width;
    DFlipFlop   dffs[REG_MAX_WIDTH];
} Register;

typedef enum {
    LATCH_SR,
    LATCH_D
} LatchType;

typedef struct {
    bool  q;
    bool  q_bar;
    bool  s;
    bool  r;
} SRLatch;

typedef struct {
    bool  q;
    bool  q_bar;
    bool  d;
    bool  enable;
} DLatch;

/* D flip-flop */
DFlipFlop dff_create(const char* name);
void      dff_clock(DFlipFlop* dff);

/* Register */
Register  reg_create(int width);
void      reg_write(Register* reg, unsigned long long value);
unsigned long long reg_read(const Register* reg);

/* SR Latch */
SRLatch   sr_latch_create(void);
void      sr_latch_set_s(SRLatch* latch);
void      sr_latch_set_r(SRLatch* latch);
void      sr_latch_eval(SRLatch* latch);

/* D Latch */
DLatch    d_latch_create(void);
void      d_latch_set(DLatch* latch, bool val);
void      d_latch_enable(DLatch* latch, bool en);
bool      d_latch_output(const DLatch* latch);

#endif
