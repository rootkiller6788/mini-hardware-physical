#include "predictor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ===== L2: Bimodal Predictor (Smith 1981) =====
 *
 * Uses a simple table of 2-bit saturating counters indexed by
 * the low bits of the PC. This is the foundational dynamic
 * branch predictor, achieving ~85-90% accuracy for integer code.
 *
 * State machine (Smith, ISCA 1981):
 *   SNT(00) --taken--> WNT(01) --taken--> WT(10) --taken--> ST(11)
 *     ^                   |                   |                   |
 *     +---not taken-------+---not taken------+---not taken-------+
 */

void bimodal_init(BimodalPredictor *b)
{
    memset(b, 0, sizeof(*b));
    for (int i = 0; i < PRED_BHT_SIZE; i++)
        b->counters[i] = WT;  /* Start weakly taken (neutral) */
}

static uint32_t bimodal_index(uint32_t pc)
{
    return (pc >> 2) & (PRED_BHT_SIZE - 1);
}

bool bimodal_predict(const BimodalPredictor *b, uint32_t pc)
{
    TwoBitState state = b->counters[bimodal_index(pc)];
    return (state == WT || state == ST);
}

void bimodal_update(BimodalPredictor *b, uint32_t pc, bool taken)
{
    uint32_t idx = bimodal_index(pc);
    b->predictions++;
    if (bimodal_predict(b, pc) == taken) b->correct++;

    /* 2-bit saturating counter update */
    switch (b->counters[idx]) {
    case SNT: b->counters[idx] = taken ? WNT : SNT; break;
    case WNT: b->counters[idx] = taken ? WT  : SNT; break;
    case WT:  b->counters[idx] = taken ? ST  : WNT; break;
    case ST:  b->counters[idx] = taken ? ST  : WT;  break;
    }
}

/* ===== L2: Two-Level Adaptive Predictor (Yeh & Patt 1991) =====
 *
 * Uses a Branch History Register (BHR) to index into a Pattern
 * History Table (PHT). This captures branch correlation patterns.
 * PAs (Pattern History Table with global history) scheme.
 * Reference: Yeh & Patt, "Two-Level Adaptive Training Branch Prediction",
 * MICRO-24, 1991.
 */

void twolevel_init(TwoLevelPredictor *tl)
{
    memset(tl, 0, sizeof(*tl));
    for (int i = 0; i < PRED_PHT_SIZE; i++)
        tl->pht[i] = 1; /* Weakly taken */
}

static uint32_t twolevel_index(const TwoLevelPredictor *tl, uint32_t pc)
{
    return ((pc >> 2) ^ (tl->bhr << 4)) & (PRED_PHT_SIZE - 1);
}

bool twolevel_predict(const TwoLevelPredictor *tl, uint32_t pc)
{
    return tl->pht[twolevel_index(tl, pc)] >= 2;
}

void twolevel_update(TwoLevelPredictor *tl, uint32_t pc, bool taken)
{
    uint32_t idx = twolevel_index(tl, pc);
    tl->predictions++;

    if (twolevel_predict(tl, pc) == taken) tl->correct++;

    /* 2-bit counter update in PHT */
    uint8_t ctr = tl->pht[idx];
    if (taken && ctr < 3) tl->pht[idx] = ctr + 1;
    else if (!taken && ctr > 0) tl->pht[idx] = ctr - 1;

    /* Shift BHR */
    tl->bhr = (uint16_t)(((tl->bhr << 1) | (taken ? 1u : 0u)) & 0xFFFu);
}

/* ===== L2: Gshare Predictor (McFarling 1993) =====
 *
 * XORs the PC with the Global History Register to index into
 * a single PHT. This reduces aliasing compared to GAg and
 * provides better accuracy for most workloads.
 * Reference: McFarling, "Combining Branch Predictors", WRL TN-36, 1993.
 */

void gshare_init(GsharePredictor *g)
{
    memset(g, 0, sizeof(*g));
    for (int i = 0; i < PRED_PHT_SIZE; i++)
        g->pht[i] = 2; /* Weakly taken starting state */
}

static uint32_t gshare_index(const GsharePredictor *g, uint32_t pc)
{
    return ((pc >> 2) ^ g->ghr) & (PRED_PHT_SIZE - 1);
}

bool gshare_predict(const GsharePredictor *g, uint32_t pc)
{
    return g->pht[gshare_index(g, pc)] >= 2;
}

void gshare_update(GsharePredictor *g, uint32_t pc, bool taken)
{
    uint32_t idx = gshare_index(g, pc);
    g->predictions++;

    if (gshare_predict(g, pc) == taken) g->correct++;

    uint8_t ctr = g->pht[idx];
    if (taken && ctr < 3) g->pht[idx] = ctr + 1;
    else if (!taken && ctr > 0) g->pht[idx] = ctr - 1;

    g->ghr = (uint16_t)(((g->ghr << 1) | (taken ? 1u : 0u)) & 0xFFFu);
}

/* ===== L2: Tournament/Hybrid Predictor (Alpha 21264) =====
 *
 * Combines a local (bimodal) predictor and a global (gshare) predictor
 * with a choice predictor that selects between them.
 * Kessler, "The Alpha 21264 Microprocessor", IEEE Micro 1999.
 */

void tournament_init(TournamentPredictor *t)
{
    memset(t, 0, sizeof(*t));
    bimodal_init(&t->local);
    gshare_init(&t->global);
    for (int i = 0; i < PRED_BHT_SIZE; i++)
        t->choice_table[i] = WT;
}

bool tournament_predict(TournamentPredictor *t, uint32_t pc)
{
    uint32_t idx = bimodal_index(pc);
    TwoBitState choice = t->choice_table[idx];

    if (choice == WT || choice == ST) {
        return gshare_predict(&t->global, pc);
    } else {
        return bimodal_predict(&t->local, pc);
    }
}

void tournament_update(TournamentPredictor *t, uint32_t pc, bool taken)
{
    uint32_t idx = bimodal_index(pc);
    t->predictions++;

    bool local_pred  = bimodal_predict(&t->local, pc);
    bool global_pred = gshare_predict(&t->global, pc);

    if (tournament_predict(t, pc) == taken) t->correct++;

    bimodal_update(&t->local, pc, taken);
    gshare_update(&t->global, pc, taken);

    /* Update choice table: promote predictor that was correct */
    if (local_pred == taken && global_pred != taken) {
        if (t->choice_table[idx] > SNT) t->choice_table[idx]--;
    } else if (global_pred == taken && local_pred != taken) {
        if (t->choice_table[idx] < ST) t->choice_table[idx]++;
    }
}

/* ===== L3: Branch Target Buffer (BTB) =====
 *
 * Caches branch target addresses to enable zero-cycle branch prediction.
 * Associative lookup by PC tag. Used in conjunction with direction
 * predictor for full branch prediction.
 */

void btb_init(BranchTargetBuffer *btb)
{
    memset(btb, 0, sizeof(*btb));
}

static uint32_t btb_hash(uint32_t pc)
{
    return (pc >> 2) & (PRED_BTB_SIZE - 1);
}

bool btb_lookup(BranchTargetBuffer *btb, uint32_t pc,
                uint32_t *target, uint8_t *type)
{
    uint32_t idx = btb_hash(pc);
    BTBEntry *e = &btb->entries[idx];
    btb->accesses++;

    if (e->valid && e->tag == (pc >> 2)) {
        btb->hits++;
        if (target) *target = e->target;
        if (type)   *type   = e->type;
        return true;
    }
    btb->misses++;
    return false;
}

void btb_update(BranchTargetBuffer *btb, uint32_t pc,
                uint32_t target, uint8_t type)
{
    uint32_t idx = btb_hash(pc);
    BTBEntry *e = &btb->entries[idx];
    e->tag    = pc >> 2;
    e->target = target;
    e->type   = type;
    e->valid  = true;
}

/* ===== L3: Return Address Stack (RAS) =====
 *
 * Predicts return addresses by caching the return address
 * pushed on a call. RAS provides near-perfect prediction
 * for function returns.
 * Kaeli & Emma, "Branch History Table Prediction of Moving
 * Target Branches", ISCA 1991.
 */

void ras_init(ReturnAddressStack *ras)
{
    memset(ras, 0, sizeof(*ras));
    ras->tos = -1;
}

void ras_push(ReturnAddressStack *ras, uint32_t ret_addr)
{
    if (ras->tos < (int32_t)(PRED_RAS_SIZE - 1)) {
        ras->tos++;
        ras->stack[ras->tos] = ret_addr;
        ras->pushes++;
    }
}

uint32_t ras_pop(ReturnAddressStack *ras)
{
    ras->pops++;
    if (ras->tos >= 0) {
        uint32_t addr = ras->stack[ras->tos];
        ras->tos--;
        return addr;
    }
    ras->mispredictions++;
    return 0;
}

/* ===== L8: TAGE Predictor (Seznec 2006) =====
 *
 * TAgged GEometric length predictor: uses multiple PHTs with
 * different history lengths. The prediction comes from the table
 * with the longest matching history. Champion of CBP-2.
 * Seznec & Michaud, "A case for (partially) tagged Geometric
 * History Length Branch Prediction", JILP 2006.
 */

void tage_table_init(TAGETable *t, uint32_t entries,
                     uint32_t history_len, uint8_t tag_bits)
{
    t->num_entries = entries;
    t->history_length = history_len;
    t->tag_bits = tag_bits;
    t->predictions = 0;
    t->correct = 0;

    t->entries = (TAGEEntry *)calloc(entries, sizeof(TAGEEntry));
    if (!t->entries) {
        fprintf(stderr, "tage_table_init: allocation failed\n");
        exit(1);
    }
}

bool tage_predict(TAGETable *t, uint32_t pc, uint16_t folded_hist)
{
    uint32_t idx = ((pc >> 2) ^ folded_hist) & (t->num_entries - 1);
    TAGEEntry *e = &t->entries[idx];
    uint16_t expected_tag = (uint16_t)((pc >> 2) & ((1u << t->tag_bits) - 1));

    t->predictions++;

    if (e->tag == expected_tag) {
        return (e->ctr >= 0);  /* 3-bit signed counter: >= 0 = predict taken */
    }
    return false;  /* No tag match, predict not-taken */
}

void tage_update(TAGETable *t, uint32_t pc, bool taken,
                 uint16_t folded_hist)
{
    uint32_t idx = ((pc >> 2) ^ folded_hist) & (t->num_entries - 1);
    TAGEEntry *e = &t->entries[idx];

    if (tage_predict(t, pc, folded_hist) == taken) t->correct++;

    /* Update 3-bit counter (-4 to +3 range) */
    if (taken) {
        if (e->ctr < 3) e->ctr++;
    } else {
        if (e->ctr > -4) e->ctr--;
    }

    /* Update useful counter on misprediction */
    if (tage_predict(t, pc, folded_hist) != taken) {
        if (taken) {
            if (e->u < 3) e->u++;
        } else {
            if (e->u > 0) e->u--;
        }
    }

    /* Set tag */
    e->tag = (uint16_t)((pc >> 2) & ((1u << t->tag_bits) - 1));
}

/* ===== L8: Perceptron Predictor (Jimenez & Lin 2001) =====
 *
 * Uses a simple neural network (perceptron) to predict branches.
 * Each perceptron learns weights for each bit of the global
 * history. Can achieve higher accuracy than table-based predictors
 * for workloads with long history dependencies.
 * Jimenez & Lin, "Dynamic Branch Prediction with Perceptrons", HPCA-7, 2001.
 */

void perceptron_init(PerceptronPredictor *pp)
{
    memset(pp, 0, sizeof(*pp));
    pp->bias = 0;
    pp->threshold = (int32_t)(1.93 * PRED_PERCEPTRON_WEIGHTS + 14);
}

bool perceptron_predict(PerceptronPredictor *pp, const int8_t *global_history)
{
    int32_t sum = pp->bias;
    for (int i = 0; i < PRED_PERCEPTRON_WEIGHTS; i++) {
        sum += pp->weights[i] * global_history[i];
    }
    pp->predictions++;
    return (sum >= 0);
}

void perceptron_train(PerceptronPredictor *pp,
                       const int8_t *global_history, bool outcome)
{
    int32_t sum = pp->bias;
    for (int i = 0; i < PRED_PERCEPTRON_WEIGHTS; i++) {
        sum += pp->weights[i] * global_history[i];
    }
    bool pred = (sum >= 0);
    if (pred == outcome) pp->correct++;

    int8_t t = outcome ? 1 : -1;

    /* Only update on misprediction or when sum is below threshold */
    if (pred != outcome || (sum >= -pp->threshold && sum <= pp->threshold)) {
        pp->bias += t;
        for (int i = 0; i < PRED_PERCEPTRON_WEIGHTS; i++) {
            pp->weights[i] += t * global_history[i];
        }
        pp->training_iterations++;
    }
}

/* ===== L1: Unified Branch Predictor ===== */

void bp_init(BranchPredictor *bp, PredictorType type)
{
    memset(bp, 0, sizeof(*bp));
    bp->type = type;

    switch (type) {
    case PRED_BIMODAL:    bimodal_init(&bp->bimodal); break;
    case PRED_TWO_LEVEL:  twolevel_init(&bp->twolevel); break;
    case PRED_GSHARE:     gshare_init(&bp->gshare); break;
    case PRED_TOURNAMENT: tournament_init(&bp->tournament); break;
    default: break;
    }

    btb_init(&bp->btb);
    ras_init(&bp->ras);
}

bool bp_predict(BranchPredictor *bp, uint32_t pc)
{
    bool pred = false;
    switch (bp->type) {
    case PRED_SIMPLE:     pred = true; break; /* Always predict taken */
    case PRED_BIMODAL:    pred = bimodal_predict(&bp->bimodal, pc); break;
    case PRED_TWO_LEVEL:  pred = twolevel_predict(&bp->twolevel, pc); break;
    case PRED_GSHARE:     pred = gshare_predict(&bp->gshare, pc); break;
    case PRED_TOURNAMENT: pred = tournament_predict(&bp->tournament, pc); break;
    default: break;
    }
    bp->total_branches++;
    return pred;
}

void bp_update(BranchPredictor *bp, uint32_t pc, bool taken, uint32_t target)
{
    if (bp_predict(bp, pc) == taken) bp->total_correct++;

    switch (bp->type) {
    case PRED_BIMODAL:    bimodal_update(&bp->bimodal, pc, taken); break;
    case PRED_TWO_LEVEL:  twolevel_update(&bp->twolevel, pc, taken); break;
    case PRED_GSHARE:     gshare_update(&bp->gshare, pc, taken); break;
    case PRED_TOURNAMENT: tournament_update(&bp->tournament, pc, taken); break;
    default: break;
    }

    /* Update BTB with target */
    if (taken) {
        uint8_t btype = 3; /* generic branch type */
        btb_update(&bp->btb, pc, target, btype);
    }

    /* 2-cycle misprediction penalty savings */
    if (taken) {
        bp->cycles_saved += 2;
    }
}

/* L7: Compute autocorrelation of branch outcomes (Pan et al. 1992) */
void bp_compute_correlation(const bool *history, size_t len, double *corr_coeff)
{
    if (len < 2) {
        *corr_coeff = 0.0;
        return;
    }

    double mean = 0.0;
    for (size_t i = 0; i < len; i++)
        mean += history[i] ? 1.0 : 0.0;
    mean /= (double)len;

    double num = 0.0, den = 0.0;
    for (size_t i = 1; i < len; i++) {
        double a = (history[i-1] ? 1.0 : 0.0) - mean;
        double b = (history[i]   ? 1.0 : 0.0) - mean;
        num += a * b;
        den += a * a;
    }

    *corr_coeff = (fabs(den) > 1e-10) ? (num / den) : 0.0;
}

double bp_accuracy(const BranchPredictor *bp)
{
    if (bp->total_branches == 0) return 0.0;
    return (double)bp->total_correct / (double)bp->total_branches * 100.0;
}

double bp_f2g_metric(const BranchPredictor *bp)
{
    /* Forward-to-goal: fraction of remaining potential improvement */
    double acc = bp_accuracy(bp);
    double remaining = 100.0 - acc;
    return remaining / 100.0;
}

void bp_print_stats(const BranchPredictor *bp)
{
    printf("=== Branch Predictor Statistics ===\n");
    printf("  Type: ");
    switch (bp->type) {
    case PRED_SIMPLE:     printf("Always-Taken\n"); break;
    case PRED_BIMODAL:    printf("Bimodal (Smith 1981)\n"); break;
    case PRED_TWO_LEVEL:  printf("Two-Level (Yeh&Patt 1991)\n"); break;
    case PRED_GSHARE:     printf("Gshare (McFarling 1993)\n"); break;
    case PRED_TOURNAMENT: printf("Tournament (Alpha 21264)\n"); break;
    case PRED_TAGE:       printf("TAGE (Seznec 2006)\n"); break;
    case PRED_PERCEPTRON: printf("Perceptron (Jimenez&Lin 2001)\n"); break;
    }
    printf("  Total branches:    %llu\n", (unsigned long long)bp->total_branches);
    printf("  Correct:           %llu\n", (unsigned long long)bp->total_correct);
    printf("  Accuracy:          %.2f%%\n", bp_accuracy(bp));
    printf("  Cycles saved:      %llu\n", (unsigned long long)bp->cycles_saved);
    printf("  F2G metric:        %.4f\n", bp_f2g_metric(bp));
    printf("  BTB hits:          %llu/%llu\n",
           (unsigned long long)bp->btb.hits, (unsigned long long)bp->btb.accesses);
    printf("  RAS pushes/pops:   %llu/%llu\n",
           (unsigned long long)bp->ras.pushes, (unsigned long long)bp->ras.pops);
    printf("========================================\n");
}

/* L7: Compare multiple predictors on the same trace */
void bp_compare_predictors(uint32_t *trace, bool *outcomes, size_t len)
{
    BranchPredictor b1, b2, b3, b4;
    bp_init(&b1, PRED_BIMODAL);
    bp_init(&b2, PRED_TWO_LEVEL);
    bp_init(&b3, PRED_GSHARE);
    bp_init(&b4, PRED_TOURNAMENT);

    for (size_t i = 0; i < len; i++) {
        (void)bp_predict(&b1, trace[i]);
        bp_update(&b1, trace[i], outcomes[i], trace[i] + 4);
        (void)bp_predict(&b2, trace[i]);
        bp_update(&b2, trace[i], outcomes[i], trace[i] + 4);
        (void)bp_predict(&b3, trace[i]);
        bp_update(&b3, trace[i], outcomes[i], trace[i] + 4);
        (void)bp_predict(&b4, trace[i]);
        bp_update(&b4, trace[i], outcomes[i], trace[i] + 4);
    }

    printf("=== Predictor Comparison (%zu branches) ===\n", len);
    printf("  Bimodal:    %.2f%%\n", bp_accuracy(&b1));
    printf("  Two-Level:  %.2f%%\n", bp_accuracy(&b2));
    printf("  Gshare:     %.2f%%\n", bp_accuracy(&b3));
    printf("  Tournament: %.2f%%\n", bp_accuracy(&b4));
    printf("========================================\n");
}
