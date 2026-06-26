#include "alu.h"
#include <stdio.h>
#include <string.h>

ALU alu_create(void) { ALU a; memset(&a, 0, sizeof(a)); a.latency = 1; return a; }
void alu_set_inputs(ALU* alu, uint32_t a, uint32_t b, AluOp op) { if (alu) { alu->a = a; alu->b = b; alu->op = op; } }

uint32_t alu_compute(ALU* alu) {
    if (!alu) return 0;
    uint32_t res = 0; alu->flags = 0;
    switch (alu->op) {
    case ALU_ADD: res = alu->a + alu->b;
        if (res < alu->a) alu->flags |= ALU_FLAG_CARRY;
        if ((~(alu->a ^ alu->b) & (alu->a ^ res)) >> 31) alu->flags |= ALU_FLAG_OVERFLOW;
        break;
    case ALU_SUB: res = alu->a - alu->b;
        if (alu->a >= alu->b) alu->flags |= ALU_FLAG_CARRY;
        if (((alu->a ^ alu->b) & (alu->a ^ res)) >> 31) alu->flags |= ALU_FLAG_OVERFLOW;
        break;
    case ALU_AND: res = alu->a & alu->b; break;
    case ALU_OR:  res = alu->a | alu->b; break;
    case ALU_XOR: res = alu->a ^ alu->b; break;
    case ALU_NOT: res = ~alu->a; break;
    case ALU_SHL: res = alu->a << (alu->b & 0x1F); break;
    case ALU_SHR: res = alu->a >> (alu->b & 0x1F); break;
    case ALU_SAR: res = (int32_t)alu->a >> (alu->b & 0x1F); break;
    case ALU_MUL: res = alu->a * alu->b;
        if ((uint64_t)alu->a * alu->b > 0xFFFFFFFF) alu->flags |= ALU_FLAG_OVERFLOW;
        break;
    case ALU_DIV:
        if (alu->b == 0) { res = 0; alu->flags |= ALU_FLAG_OVERFLOW; }
        else res = alu->a / alu->b;
        break;
    case ALU_MOD:
        if (alu->b == 0) { res = 0; alu->flags |= ALU_FLAG_OVERFLOW; }
        else res = alu->a % alu->b;
        break;
    case ALU_CMP:
        if (alu->a == alu->b) alu->flags |= ALU_FLAG_ZERO;
        else if ((int32_t)alu->a < (int32_t)alu->b) alu->flags |= ALU_FLAG_NEGATIVE;
        res = alu->a; break;
    case ALU_MOV: res = alu->b; break;
    case ALU_NOP: res = 0; break;
    }
    alu->result = res;
    if (res == 0) alu->flags |= ALU_FLAG_ZERO;
    if (res >> 31) alu->flags |= ALU_FLAG_NEGATIVE;
    int parity = 0;
    for (int i = 0; i < 32; i++) parity ^= (res >> i) & 1;
    if (parity) alu->flags |= ALU_FLAG_PARITY;
    return res;
}
uint8_t alu_get_flags(const ALU* alu) { return alu ? alu->flags : 0; }
const char* alu_op_name(AluOp op) {
    switch (op) {
    case ALU_ADD: return "ADD"; case ALU_SUB: return "SUB";
    case ALU_AND: return "AND"; case ALU_OR:  return "OR";
    case ALU_XOR: return "XOR"; case ALU_NOT: return "NOT";
    case ALU_SHL: return "SHL"; case ALU_SHR: return "SHR";
    case ALU_SAR: return "SAR"; case ALU_MUL: return "MUL";
    case ALU_DIV: return "DIV"; case ALU_MOD: return "MOD";
    case ALU_CMP: return "CMP"; case ALU_MOV: return "MOV";
    default: return "NOP";
    }
}
bool alu_flag_zero(const ALU* alu)     { return alu ? (alu->flags & ALU_FLAG_ZERO) != 0 : false; }
bool alu_flag_negative(const ALU* alu) { return alu ? (alu->flags & ALU_FLAG_NEGATIVE) != 0 : false; }
bool alu_flag_carry(const ALU* alu)    { return alu ? (alu->flags & ALU_FLAG_CARRY) != 0 : false; }
bool alu_flag_overflow(const ALU* alu) { return alu ? (alu->flags & ALU_FLAG_OVERFLOW) != 0 : false; }

MultiplyUnit mul_unit_create(void) { MultiplyUnit mu; memset(&mu, 0, sizeof(mu)); return mu; }
void mul_unit_set(MultiplyUnit* mu, uint32_t a, uint32_t b) { if (mu) { mu->a = a; mu->b = b; } }
uint64_t mul_unit_compute(MultiplyUnit* mu) {
    if (!mu) return 0;
    uint64_t product = (uint64_t)mu->a * mu->b;
    mu->result_hi = product >> 32; mu->result_lo = product & 0xFFFFFFFF;
    mu->flags = (product > 0xFFFFFFFF) ? ALU_FLAG_OVERFLOW : 0;
    return product;
}
bool mul_unit_overflow(const MultiplyUnit* mu) { return mu ? (mu->flags & ALU_FLAG_OVERFLOW) != 0 : false; }

DivideUnit div_unit_create(void) { DivideUnit du; memset(&du, 0, sizeof(du)); return du; }
void div_unit_set(DivideUnit* du, uint32_t dividend, uint32_t divisor) { if (du) { du->dividend = dividend; du->divisor = divisor; } }
bool div_unit_compute(DivideUnit* du) {
    if (!du) return false;
    du->div_by_zero = (du->divisor == 0);
    if (du->div_by_zero) { du->quotient = 0; du->remainder = 0; return false; }
    du->quotient = du->dividend / du->divisor;
    du->remainder = du->dividend % du->divisor;
    return true;
}
uint32_t div_unit_quotient(const DivideUnit* du) { return du ? du->quotient : 0; }
uint32_t div_unit_remainder(const DivideUnit* du) { return du ? du->remainder : 0; }
