#ifndef PREDICTOR_H
#define PREDICTOR_H

#include <stdbool.h>
#include <stdint.h>

/* L1: Core Definitions — Branch Prediction */

#define PRED_BHT_SIZE      1024    /* Branch History Table entries */
#define PRED_PHT_SIZE      4096    /* Pattern History Table entries */
#define PRED_BTB_SIZE       512    /* Branch Target Buffer entries */
#define PRED_RAS_SIZE        32    /* Return Address Stack depth */
#define PRED_GSHARE_BITS     12    /* Gshare global history register bits */
#define PRED_TOURNAMENT_TABLES 3   /* Alpha 21264 style: local, global, choice */
#define PRED_TAGE_TABLES      8    /* TAGE: partially tagged geometric tables */
#define PRED_PERCEPTRON_WEIGHTS 64 /* Perceptron branch predictor */

/* L1: Prediction state machines */
typedef enum {
    SNT,   /* Strongly Not Taken  (00) */
    WNT,   /* Weakly Not Taken    (01) */
    WT,    /* Weakly Taken        (10) */
    ST     /* Strongly Taken      (11) */
} TwoBitState;

typedef enum {
    PRED_SIMPLE,       /* Always-taken / always-not-taken */
    PRED_BIMODAL,      /* 2-bit saturating counter (Smith 1981) */
    PRED_TWO_LEVEL,    /* Two-level adaptive (Yeh & Patt 1991) */
    PRED_GSHARE,       /* Gshare: global history XOR PC (McFarling 1993) */
    PRED_TOURNAMENT,   /* Hybrid tournament (Alpha 21264) */
    PRED_TAGE,         /* TAgged GEometric length (Seznec 2006) */
    PRED_PERCEPTRON    /* Perceptron-based (Jimenez & Lin 2001) */
} PredictorType;

/* L2: Core Concepts — Bimodal predictor (Smith 1981) */
typedef struct {
    TwoBitState  counters[PRED_BHT_SIZE];
    uint64_t     predictions;
    uint64_t     correct;
    uint64_t     mispredictions;
} BimodalPredictor;

/* L2: Two-level adaptive predictor (Yeh & Patt 1991) */
typedef struct {
    uint16_t     bhr;       /* Branch History Register */
    uint8_t      pht[PRED_PHT_SIZE];  /* Pattern History Table: 2-bit counters */
    uint64_t     predictions;
    uint64_t     correct;
} TwoLevelPredictor;

/* L2: Gshare predictor (McFarling 1993) */
typedef struct {
    uint16_t     ghr;       /* Global History Register */
    uint8_t      pht[PRED_PHT_SIZE];  /* XORed with PC */
    uint64_t     predictions;
    uint64_t     correct;
} GsharePredictor;

/* L2: Tournament/Hybrid predictor (Alpha 21264) */
typedef struct {
    BimodalPredictor  local;
    GsharePredictor   global;
    TwoBitState       choice_table[PRED_BHT_SIZE];  /* Meta-predictor */
    uint64_t          predictions;
    uint64_t          correct;
} TournamentPredictor;

/* L3: Engineering Structure — Branch Target Buffer */
typedef struct {
    uint32_t     tag;
    uint32_t     target;
    bool         valid;
    uint8_t      type;   /* 0=call, 1=ret, 2=jump, 3=branch */
    uint64_t     lru;
} BTBEntry;

typedef struct {
    BTBEntry     entries[PRED_BTB_SIZE];
    uint64_t     accesses;
    uint64_t     hits;
    uint64_t     misses;
} BranchTargetBuffer;

/* L3: Return Address Stack */
typedef struct {
    uint32_t     stack[PRED_RAS_SIZE];
    int32_t      tos;         /* top of stack index */
    uint64_t     pushes;
    uint64_t     pops;
    uint64_t     mispredictions;
} ReturnAddressStack;

/* L8: TAGE predictor structure (Seznec 2006) */
typedef struct {
    uint16_t     tag;
    int8_t       ctr;        /* 3-bit signed counter (-4 to +3) */
    uint8_t      u;          /* 2-bit useful counter */
} TAGEEntry;

typedef struct {
    TAGEEntry   *entries;
    uint32_t     num_entries;
    uint32_t     history_length;
    uint8_t      tag_bits;
    uint64_t     predictions;
    uint64_t     correct;
} TAGETable;

/* L8: Perceptron predictor (Jimenez & Lin 2001) */
typedef struct {
    int8_t       weights[PRED_PERCEPTRON_WEIGHTS];
    int32_t      bias;
    int32_t      threshold;
    uint64_t     predictions;
    uint64_t     correct;
    uint64_t     training_iterations;
} PerceptronPredictor;

/* L3: Unified branch predictor */
typedef struct {
    PredictorType       type;
    BimodalPredictor    bimodal;
    TwoLevelPredictor   twolevel;
    GsharePredictor     gshare;
    TournamentPredictor tournament;
    BranchTargetBuffer  btb;
    ReturnAddressStack  ras;

    /* Global stats */
    uint64_t            total_branches;
    uint64_t            total_correct;
    double              accuracy;
    uint64_t            cycles_saved;   /* Assuming 2-cycle misprediction penalty */
} BranchPredictor;

/* L1 API */
void bp_init(BranchPredictor *bp, PredictorType type);
bool bp_predict(BranchPredictor *bp, uint32_t pc);
void bp_update(BranchPredictor *bp, uint32_t pc, bool taken, uint32_t target);

/* L2: Bimodal */
void bimodal_init(BimodalPredictor *b);
bool bimodal_predict(const BimodalPredictor *b, uint32_t pc);
void bimodal_update(BimodalPredictor *b, uint32_t pc, bool taken);

/* L2: Two-level */
void twolevel_init(TwoLevelPredictor *tl);
bool twolevel_predict(const TwoLevelPredictor *tl, uint32_t pc);
void twolevel_update(TwoLevelPredictor *tl, uint32_t pc, bool taken);

/* L2: Gshare */
void gshare_init(GsharePredictor *g);
bool gshare_predict(const GsharePredictor *g, uint32_t pc);
void gshare_update(GsharePredictor *g, uint32_t pc, bool taken);

/* L2: Tournament */
void tournament_init(TournamentPredictor *t);
bool tournament_predict(TournamentPredictor *t, uint32_t pc);
void tournament_update(TournamentPredictor *t, uint32_t pc, bool taken);

/* L3: BTB */
void btb_init(BranchTargetBuffer *btb);
bool btb_lookup(BranchTargetBuffer *btb, uint32_t pc, uint32_t *target, uint8_t *type);
void btb_update(BranchTargetBuffer *btb, uint32_t pc, uint32_t target, uint8_t type);

/* L3: RAS */
void ras_push(ReturnAddressStack *ras, uint32_t ret_addr);
uint32_t ras_pop(ReturnAddressStack *ras);
void ras_init(ReturnAddressStack *ras);

/* L8: TAGE */
void tage_table_init(TAGETable *t, uint32_t entries, uint32_t history_len, uint8_t tag_bits);
bool tage_predict(TAGETable *t, uint32_t pc, uint16_t folded_hist);
void tage_update(TAGETable *t, uint32_t pc, bool taken, uint16_t folded_hist);

/* L8: Perceptron */
void perceptron_init(PerceptronPredictor *pp);
bool perceptron_predict(PerceptronPredictor *pp, const int8_t *global_history);
void perceptron_train(PerceptronPredictor *pp, const int8_t *global_history, bool outcome);

/* Stats and print */
void bp_print_stats(const BranchPredictor *bp);
double bp_accuracy(const BranchPredictor *bp);
void bp_compare_predictors(uint32_t *trace, bool *outcomes, size_t len);

/* L7: Correlating branch analysis (Pan & So 1992) */
void bp_compute_correlation(const bool *history, size_t len, double *corr_coeff);
double bp_f2g_metric(const BranchPredictor *bp); /* Forward-to-goal metric */

#endif
