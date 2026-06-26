#include "branch_pred.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void bp_create(BranchPredictor* bp, PredictorType type) {
    if (!bp) return;
    memset(bp, 0, sizeof(BranchPredictor));
    bp->type = type;
    for (int i = 0; i < BHT_SIZE; i++) {
        bp->bht[i] = WN;
    }
    for (int i = 0; i < PHT_ROWS; i++) {
        for (int j = 0; j < PHT_COLS; j++) {
            bp->pht[i][j] = WN;
        }
    }
    bp->ghr = 0;
    bp->total_predictions = 0;
    bp->correct_predictions = 0;
}

bool bp_predict(BranchPredictor* bp, uint32_t pc) {
    if (!bp) return false;
    bool prediction = false;

    switch (bp->type) {
        case PRED_ALWAYS_TAKEN:
            prediction = true;
            break;
        case PRED_ALWAYS_NOT_TAKEN:
            prediction = false;
            break;
        case PRED_BIMODAL: {
            uint32_t idx = (pc >> 2) & (BHT_SIZE - 1);
            uint8_t state = bp->bht[idx];
            prediction = (state == WT || state == ST);
            break;
        }
        case PRED_TWO_LEVEL: {
            uint32_t bht_idx = (pc >> 2) & (BHT_SIZE - 1);
            uint32_t pht_idx = bht_idx;
            uint8_t state = bp->pht[pht_idx][bp->ghr & (PHT_COLS - 1)];
            prediction = (state == WT || state == ST);
            break;
        }
        case PRED_GSHARE: {
            uint32_t hash = ((pc >> 2) ^ (uint32_t)bp->ghr) & (PHT_ROWS - 1);
            uint8_t state = bp->pht[hash][0];
            prediction = (state == WT || state == ST);
            break;
        }
        default:
            break;
    }
    return prediction;
}

void bp_update(BranchPredictor* bp, uint32_t pc, bool taken) {
    if (!bp) return;

    uint32_t bht_idx = (pc >> 2) & (BHT_SIZE - 1);
    uint8_t* counter = NULL;

    switch (bp->type) {
        case PRED_ALWAYS_TAKEN:
        case PRED_ALWAYS_NOT_TAKEN:
            break;
        case PRED_BIMODAL:
            counter = &bp->bht[bht_idx];
            break;
        case PRED_TWO_LEVEL: {
            uint32_t pht_idx = bht_idx;
            counter = &bp->pht[pht_idx][bp->ghr & (PHT_COLS - 1)];
            bp->bht[bht_idx] = taken ? (bp->bht[bht_idx] < ST ? bp->bht[bht_idx] + 1 : ST)
                                     : (bp->bht[bht_idx] > SN ? bp->bht[bht_idx] - 1 : SN);
            break;
        }
        case PRED_GSHARE: {
            uint32_t hash = ((pc >> 2) ^ (uint32_t)bp->ghr) & (PHT_ROWS - 1);
            counter = &bp->pht[hash][0];
            break;
        }
        default:
            break;
    }

    if (counter) {
        if (taken) {
            if (*counter < ST) (*counter)++;
        } else {
            if (*counter > SN) (*counter)--;
        }
    }

    bp->ghr = (uint8_t)(((uint32_t)bp->ghr << 1) | (taken ? 1 : 0));
}

void bp_print_stats(const BranchPredictor* bp) {
    if (!bp) return;
    const char* type_names[PRED_TYPE_COUNT] = {
        "Always Taken", "Always Not Taken", "Bimodal", "Two-Level", "Gshare"
    };
    double acc = bp_accuracy(bp);
    printf("Predictor: %s\n", type_names[bp->type]);
    printf("  Predictions: %llu\n", (unsigned long long)bp->total_predictions);
    printf("  Correct:     %llu\n", (unsigned long long)bp->correct_predictions);
    printf("  Accuracy:    %.2f%%\n", acc * 100.0);
}

double bp_accuracy(const BranchPredictor* bp) {
    if (!bp || bp->total_predictions == 0) return 0.0;
    return (double)bp->correct_predictions / (double)bp->total_predictions;
}
