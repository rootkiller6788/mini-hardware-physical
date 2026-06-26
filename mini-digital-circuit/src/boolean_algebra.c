#include "boolean_algebra.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SOP sop_create(int num_vars) {
    SOP s; s.num_vars = (num_vars > BA_MAX_VARS) ? BA_MAX_VARS : num_vars;
    if (s.num_vars < 1) s.num_vars = 1;
    s.term_count = 0; return s;
}
void sop_add_term(SOP* sop, uint8_t mask, uint8_t values) {
    if (!sop || sop->term_count >= BA_MAX_TERMS) return;
    sop->terms[sop->term_count].num_vars = sop->num_vars;
    sop->terms[sop->term_count].mask = mask;
    sop->terms[sop->term_count].values = values;
    sop->terms[sop->term_count].is_dont_care = false;
    sop->term_count++;
}
bool sop_eval(const SOP* sop, uint8_t input_values) {
    if (!sop || sop->term_count == 0) return false;
    for (int t = 0; t < sop->term_count; t++) {
        bool term_true = true;
        for (int v = 0; v < sop->num_vars && term_true; v++) {
            if (sop->terms[t].mask & (1 << v)) {
                bool var_val = (input_values >> v) & 1;
                bool expected = (sop->terms[t].values >> v) & 1;
                if (var_val != expected) term_true = false;
            }
        }
        if (term_true) return true;
    }
    return false;
}
void sop_print(const SOP* sop) {
    if (!sop) return;
    printf("SOP(%d vars, %d terms):\n", sop->num_vars, sop->term_count);
    for (int t = 0; t < sop->term_count; t++) {
        printf("  ");
        bool first = true;
        for (int v = sop->num_vars - 1; v >= 0; v--) {
            if (sop->terms[t].mask & (1 << v)) {
                if (!first) printf(" & ");
                if (!((sop->terms[t].values >> v) & 1)) printf("!");
                printf("%c", 'A' + v);
                first = false;
            }
        }
        printf("\n");
    }
}
POS sop_to_pos(const SOP* sop) {
    POS p; p.num_vars = sop ? sop->num_vars : 0; p.term_count = 0;
    if (!sop) return p;
    int rows = 1 << sop->num_vars;
    for (int r = 0; r < rows && p.term_count < BA_MAX_TERMS; r++) {
        if (!sop_eval(sop, r)) {
            p.terms[p.term_count].num_vars = p.num_vars;
            p.terms[p.term_count].mask = (1 << p.num_vars) - 1;
            p.terms[p.term_count].values = r;
            p.term_count++;
        }
    }
    return p;
}

KMap kmap_create(int num_vars) {
    KMap k; k.num_vars = (num_vars > 4) ? 4 : (num_vars < 2 ? 2 : num_vars);
    for (int i = 0; i < 16; i++) k.cells[i] = false;
    return k;
}
void kmap_set_cell(KMap* km, int minterm_idx, bool val) {
    if (km && minterm_idx >= 0 && minterm_idx < (1 << km->num_vars)) {
        int gray = minterm_idx ^ (minterm_idx >> 1); km->cells[gray] = val;
    }
}
bool kmap_get_cell(const KMap* km, int minterm_idx) {
    if (!km || minterm_idx < 0 || minterm_idx >= (1 << km->num_vars)) return false;
    int gray = minterm_idx ^ (minterm_idx >> 1); return km->cells[gray];
}
void kmap_print(const KMap* km) {
    if (!km) return;
    printf("K-Map (%d vars):\n", km->num_vars);
    if (km->num_vars == 2) {
        printf("  B\\A 0 1\n");
        for (int b = 0; b <= 1; b++) {
            printf("  %d  ", b);
            for (int a = 0; a <= 1; a++) printf(" %d", km->cells[(b << 1) | a]);
            printf("\n");
        }
    } else if (km->num_vars == 3) {
        printf("  BC\\A 0 1\n");
        for (int bc = 0; bc <= 3; bc++) {
            int gray = bc ^ (bc >> 1);
            printf("  %d%d  ", (gray >> 1) & 1, gray & 1);
            for (int a = 0; a <= 1; a++) printf(" %d", km->cells[(gray << 1) | a]);
            printf("\n");
        }
    } else if (km->num_vars == 4) {
        printf("  CD\\AB 00 01 11 10\n");
        for (int cd = 0; cd <= 3; cd++) {
            int gray_cd = cd ^ (cd >> 1);
            printf("  %d%d    ", (gray_cd >> 1) & 1, gray_cd & 1);
            for (int ab = 0; ab <= 3; ab++) {
                int gray_ab = ab ^ (ab >> 1);
                printf(" %d ", km->cells[(gray_cd << 2) | gray_ab]);
            }
            printf("\n");
        }
    }
}
SOP kmap_simplify(const KMap* km) {
    SOP result = sop_create(km ? km->num_vars : 0);
    if (!km) return result;
    int rows = 1 << km->num_vars;
    for (int i = 0; i < rows; i++) {
        if (km->cells[i]) {
            int bin = i ^ (i >> 1);
            sop_add_term(&result, (1 << km->num_vars) - 1, bin);
        }
    }
    return result;
}
void kmap_from_sop(KMap* km, const SOP* sop) {
    if (!km || !sop) return;
    int rows = 1 << km->num_vars;
    for (int r = 0; r < rows; r++)
        if (sop_eval(sop, r)) kmap_set_cell(km, r, true);
}

KMap6 kmap6_create(int num_vars) {
    KMap6 k; k.num_vars = (num_vars > 6) ? 6 : (num_vars < 2 ? 2 : num_vars);
    k.rows = 1 << (k.num_vars / 2); k.cols = 1 << ((k.num_vars + 1) / 2);
    for (int i = 0; i < 64; i++) k.cells[i] = false;
    for (int i = 0; i < 8; i++) { k.row_order[i] = i ^ (i >> 1); k.col_order[i] = i ^ (i >> 1); }
    return k;
}
void kmap6_set(KMap6* km, int idx, bool val) {
    if (km && idx >= 0 && idx < (1 << km->num_vars)) {
        int r = (idx >> (km->num_vars / 2)) & ((1 << (km->num_vars / 2)) - 1);
        int c = idx & ((1 << ((km->num_vars + 1) / 2)) - 1);
        km->cells[r * km->cols + c] = val;
    }
}
SOP kmap6_simplify(const KMap6* km) {
    SOP r = sop_create(km ? km->num_vars : 0);
    if (!km) return r;
    int rows = 1 << km->num_vars;
    for (int i = 0; i < rows; i++)
        if (km->cells[i]) sop_add_term(&r, (1 << km->num_vars) - 1, i);
    return r;
}
void kmap6_print(const KMap6* km) {
    if (!km) return;
    printf("KMap6 (%d vars):\n", km->num_vars);
    for (int r = 0; r < km->rows; r++) {
        for (int c = 0; c < km->cols; c++) printf("%d ", km->cells[r * km->cols + c]);
        printf("\n");
    }
}

QMCResult qmc_minimize(const int* minterms, int count, int num_vars) {
    QMCResult res; memset(&res, 0, sizeof(res));
    res.num_vars = (num_vars > BA_MAX_VARS) ? BA_MAX_VARS : num_vars;
    if (res.num_vars < 1) res.num_vars = 1;
    if (!minterms || count <= 0) return res;
    for (int i = 0; i < count && i < BA_MAX_TERMS; i++) {
        res.minterm_indices[i] = minterms[i];
        res.primes[res.prime_count].mask = (1 << res.num_vars) - 1;
        res.primes[res.prime_count].values = minterms[i];
        res.primes[res.prime_count].covered[0] = minterms[i];
        res.primes[res.prime_count].covered_count = 1;
        res.primes[res.prime_count].is_prime = true;
        res.prime_count++;
    }
    res.minterm_count = count;
    return res;
}
void qmc_print(const QMCResult* result) {
    if (!result) return;
    printf("QMC Result: %d primes, %d essentials, %d minterms\n",
           result->prime_count, result->essential_count, result->minterm_count);
}
SOP qmc_to_sop(const QMCResult* result) {
    SOP s = sop_create(result ? result->num_vars : 0);
    if (!result) return s;
    for (int i = 0; i < result->prime_count && s.term_count < BA_MAX_TERMS; i++)
        sop_add_term(&s, result->primes[i].mask, result->primes[i].values);
    return s;
}
void qmc_free(QMCResult* result) { (void)result; }

SOP shannon_cofactor(const SOP* sop, int var, bool value) {
    SOP co = sop_create(sop ? sop->num_vars : 0);
    if (!sop || var < 0 || var >= sop->num_vars) return co;
    for (int t = 0; t < sop->term_count; t++) {
        if (sop->terms[t].mask & (1 << var)) {
            bool expected = (sop->terms[t].values >> var) & 1;
            if (expected != value) continue;
        }
        Minterm m = sop->terms[t];
        m.mask &= ~(1 << var);
        m.values &= ~(1 << var);
        if (co.term_count < BA_MAX_TERMS) { co.terms[co.term_count++] = m; }
    }
    return co;
}
bool shannon_verify(const SOP* sop, int var) {
    if (!sop || var < 0 || var >= sop->num_vars) return false;
    int rows = 1 << sop->num_vars;
    SOP co1 = shannon_cofactor(sop, var, true);
    SOP co0 = shannon_cofactor(sop, var, false);
    for (int r = 0; r < rows; r++) {
        bool orig = sop_eval(sop, r);
        bool var_val = (r >> var) & 1;
        bool cof_val = var_val ? sop_eval(&co1, r) : sop_eval(&co0, r);
        if (orig != cof_val) return false;
    }
    return true;
}

int espresso_reduce(SOP* sop) {
    if (!sop) return 0;
    int removed = 0;
    for (int i = 0; i < sop->term_count; i++) {
        for (int j = i + 1; j < sop->term_count; j++) {
            if (minterm_covers(&sop->terms[i], &sop->terms[j])) {
                memmove(&sop->terms[j], &sop->terms[j + 1], (sop->term_count - j - 1) * sizeof(Minterm));
                sop->term_count--; j--; removed++;
            }
        }
    }
    return removed;
}
bool minterm_covers(const Minterm* a, const Minterm* b) {
    if (!a || !b) return false;
    if ((a->mask & b->mask) != b->mask) return false;
    return (a->values & b->mask) == (b->values & b->mask);
}
bool minterm_combine(const Minterm* a, const Minterm* b, Minterm* result) {
    if (!a || !b || !result) return false;
    if (a->mask != b->mask) return false;
    uint8_t diff = a->values ^ b->values;
    if (diff == 0 || (diff & (diff - 1)) != 0) return false;
    result->num_vars = a->num_vars;
    result->mask = a->mask & ~diff;
    result->values = a->values & ~diff;
    return true;
}
