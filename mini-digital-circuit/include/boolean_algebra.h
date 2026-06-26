#ifndef BOOLEAN_ALGEBRA_H
#define BOOLEAN_ALGEBRA_H
#include <stdbool.h>
#include <stdint.h>
#define BA_MAX_VARS 8
#define BA_MAX_TERMS 256
#define BA_MAX_IMPLICANTS 128

typedef struct { int num_vars; uint8_t mask; uint8_t values; bool is_dont_care; } Minterm;
typedef struct { int num_vars; uint8_t mask; uint8_t values; } Maxterm;

typedef struct { int num_vars; Minterm terms[BA_MAX_TERMS]; int term_count; } SOP;
typedef struct { int num_vars; Maxterm terms[BA_MAX_TERMS]; int term_count; } POS;

typedef struct { int num_vars; bool cells[16]; } KMap;
typedef struct { int num_vars; int rows; int cols; bool cells[64]; int row_order[8]; int col_order[8]; } KMap6;

typedef struct { uint8_t mask; uint8_t values; int covered_count; int covered[BA_MAX_TERMS]; bool is_prime; } Implicant;

typedef struct { int num_vars; Implicant primes[BA_MAX_IMPLICANTS]; int prime_count; Implicant essentials[BA_MAX_IMPLICANTS]; int essential_count; int minterm_indices[BA_MAX_TERMS]; int minterm_count; } QMCResult;

SOP sop_create(int num_vars);
void sop_add_term(SOP* sop, uint8_t mask, uint8_t values);
bool sop_eval(const SOP* sop, uint8_t input_values);
void sop_print(const SOP* sop);
POS sop_to_pos(const SOP* sop);

KMap kmap_create(int num_vars);
void kmap_set_cell(KMap* km, int minterm_idx, bool val);
bool kmap_get_cell(const KMap* km, int minterm_idx);
void kmap_print(const KMap* km);
SOP kmap_simplify(const KMap* km);
void kmap_from_sop(KMap* km, const SOP* sop);

KMap6 kmap6_create(int num_vars);
void kmap6_set(KMap6* km, int idx, bool val);
SOP kmap6_simplify(const KMap6* km);
void kmap6_print(const KMap6* km);

QMCResult qmc_minimize(const int* minterms, int count, int num_vars);
void qmc_print(const QMCResult* result);
SOP qmc_to_sop(const QMCResult* result);
void qmc_free(QMCResult* result);

SOP shannon_cofactor(const SOP* sop, int var, bool value);
bool shannon_verify(const SOP* sop, int var);

int espresso_reduce(SOP* sop);
bool minterm_covers(const Minterm* a, const Minterm* b);
bool minterm_combine(const Minterm* a, const Minterm* b, Minterm* result);

#endif
