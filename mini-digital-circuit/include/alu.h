#ifndef ALU_H
#define ALU_H
#include <stdbool.h>
#include <stdint.h>

#define ALU_FLAG_ZERO     (1u << 0)
#define ALU_FLAG_NEGATIVE (1u << 1)
#define ALU_FLAG_CARRY    (1u << 2)
#define ALU_FLAG_OVERFLOW (1u << 3)
#define ALU_FLAG_PARITY   (1u << 4)

typedef enum {
    ALU_ADD, ALU_SUB, ALU_AND, ALU_OR, ALU_XOR,
    ALU_NOT, ALU_SHL, ALU_SHR, ALU_SAR,
    ALU_MUL, ALU_DIV, ALU_MOD,
    ALU_CMP, ALU_MOV, ALU_NOP
} AluOp;

typedef struct {
    uint32_t a;
    uint32_t b;
    AluOp op;
    uint32_t result;
    uint8_t flags;
    bool busy;
    int latency;
} ALU;

typedef struct {
    uint32_t a;
    uint32_t b;
    uint32_t result_hi;
    uint32_t result_lo;
    uint8_t flags;
} MultiplyUnit;

typedef struct {
    uint32_t dividend;
    uint32_t divisor;
    uint32_t quotient;
    uint32_t remainder;
    bool div_by_zero;
} DivideUnit;

ALU alu_create(void);
void alu_set_inputs(ALU* alu, uint32_t a, uint32_t b, AluOp op);
uint32_t alu_compute(ALU* alu);
uint8_t alu_get_flags(const ALU* alu);
const char* alu_op_name(AluOp op);
bool alu_flag_zero(const ALU* alu);
bool alu_flag_negative(const ALU* alu);
bool alu_flag_carry(const ALU* alu);
bool alu_flag_overflow(const ALU* alu);

MultiplyUnit mul_unit_create(void);
void mul_unit_set(MultiplyUnit* mu, uint32_t a, uint32_t b);
uint64_t mul_unit_compute(MultiplyUnit* mu);
bool mul_unit_overflow(const MultiplyUnit* mu);

DivideUnit div_unit_create(void);
void div_unit_set(DivideUnit* du, uint32_t dividend, uint32_t divisor);
bool div_unit_compute(DivideUnit* du);
uint32_t div_unit_quotient(const DivideUnit* du);
uint32_t div_unit_remainder(const DivideUnit* du);

#endif
