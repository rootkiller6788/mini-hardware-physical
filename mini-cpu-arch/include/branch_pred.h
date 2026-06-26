#ifndef BRANCH_PRED_H
#define BRANCH_PRED_H

#include <stdbool.h>
#include <stdint.h>

#define BHT_SIZE    256
#define PHT_ROWS    256
#define PHT_COLS    64
#define GHR_BITS    6

typedef enum {
    SN = 0,
    WN = 1,
    WT = 2,
    ST = 3
} BPState;

typedef enum {
    PRED_ALWAYS_TAKEN,
    PRED_ALWAYS_NOT_TAKEN,
    PRED_BIMODAL,
    PRED_TWO_LEVEL,
    PRED_GSHARE,
    PRED_TYPE_COUNT
} PredictorType;

typedef struct {
    PredictorType type;
    uint8_t       bht[BHT_SIZE];
    uint8_t       pht[PHT_ROWS][PHT_COLS];
    uint8_t       ghr;
    uint64_t      total_predictions;
    uint64_t      correct_predictions;
} BranchPredictor;

void bp_create(BranchPredictor* bp, PredictorType type);
bool bp_predict(BranchPredictor* bp, uint32_t pc);
void bp_update(BranchPredictor* bp, uint32_t pc, bool taken);
void bp_print_stats(const BranchPredictor* bp);
double bp_accuracy(const BranchPredictor* bp);

#endif
