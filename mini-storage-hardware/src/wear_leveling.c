#include "wear_leveling.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

static uint32_t get_block_erase_count(const FTL *ftl, uint32_t block_idx) {
    uint32_t plane = block_idx / FTL_BLOCKS_PER_PLANE;
    uint32_t blk   = block_idx % FTL_BLOCKS_PER_PLANE;
    if (plane >= FTL_PLANES || blk >= FTL_BLOCKS_PER_PLANE) return UINT32_MAX;
    return ftl->planes[plane].blocks[blk].erase_count;
}

static void set_block_erase_count(FTL *ftl, uint32_t block_idx, uint32_t count) {
    uint32_t plane = block_idx / FTL_BLOCKS_PER_PLANE;
    uint32_t blk   = block_idx % FTL_BLOCKS_PER_PLANE;
    if (plane >= FTL_PLANES || blk >= FTL_BLOCKS_PER_PLANE) return;
    ftl->planes[plane].blocks[blk].erase_count = count;
}

void wl_init(WearLeveler *wl, FTL *ftl, WearLevelAlgorithm algo,
             uint32_t threshold) {
    memset(wl, 0, sizeof(WearLeveler));
    wl->ftl       = ftl;
    wl->algorithm = algo;
    wl->threshold = threshold;
    wl_update_stats(wl);
}

void wl_update_stats(WearLeveler *wl) {
    uint32_t i, count = 0;
    uint32_t min_val = UINT32_MAX, max_val = 0;
    double sum = 0.0, sum_sq = 0.0;

    for (i = 0; i < WL_MAX_BLOCKS; i++) {
        uint32_t ec = get_block_erase_count(wl->ftl, i);
        wl->stats.erase_counts[i] = ec;
        if (ec < min_val) min_val = ec;
        if (ec > max_val) max_val = ec;
        sum    += (double)ec;
        sum_sq += (double)ec * (double)ec;
        count++;
    }

    wl->stats.min = min_val;
    wl->stats.max = max_val;
    wl->stats.avg = (count > 0) ? sum / (double)count : 0.0;
    double variance = (count > 0) ? (sum_sq / (double)count)
                     - (wl->stats.avg * wl->stats.avg) : 0.0;
    if (variance < 0.0) variance = 0.0;
    wl->stats.stddev = sqrt(variance);
}

bool wl_check_and_balance(WearLeveler *wl) {
    wl_update_stats(wl);

    if (wl->stats.max - wl->stats.min < wl->threshold) {
        return false;
    }

    uint32_t i;
    uint32_t max_block = 0, min_block = 0;

    for (i = 0; i < WL_MAX_BLOCKS; i++) {
        uint32_t ec = wl->stats.erase_counts[i];
        if (ec == wl->stats.max) max_block = i;
        if (ec == wl->stats.min) min_block = i;
    }

    if (wl->algorithm == WL_STATIC || wl->algorithm == WL_HYBRID) {
        set_block_erase_count(wl->ftl, max_block,
                              get_block_erase_count(wl->ftl, max_block) / 2);
        set_block_erase_count(wl->ftl, min_block,
                              get_block_erase_count(wl->ftl, min_block) * 2);
    }

    wl->ftl->stats.gc_moves += FTL_PAGES_PER_BLOCK;
    wl->ftl->stats.erases++;
    return true;
}

int wl_select_block(const WearLeveler *wl) {
    uint32_t i;
    uint32_t best_block   = 0;
    uint32_t lowest_ec    = UINT32_MAX;

    for (i = 0; i < WL_MAX_BLOCKS; i++) {
        uint32_t ec = wl->stats.erase_counts[i];
        if (ec < lowest_ec) {
            lowest_ec  = ec;
            best_block = i;
        }
    }
    return (int)best_block;
}

/* ── Endurance Prediction Model ──
 *
 * L4: Given erase count distribution, predict remaining endurance.
 * Uses linear projection with safety margin based on standard deviation.
 *
 * Remaining PE cycles = rated_pe - max_observed_pe * safety_factor
 * safety_factor = 1 + (stddev / avg)  -- accounts for variability
 *
 * L8: Write cliff detection: when PE spread exceeds threshold,
 *     the worst block will fail first, limiting overall device life.
 */
typedef struct {
    double   rated_pe_cycles;
    double   remaining_pe;
    double   safety_margin;
    uint32_t worst_block;
    double   projected_life_hours;
    bool     write_cliff_detected;
} EndurancePrediction;

static EndurancePrediction endurance_pred;

void wl_init_endurance_prediction(double rated_pe_cycles) {
    memset(&endurance_pred, 0, sizeof(endurance_pred));
    endurance_pred.rated_pe_cycles = rated_pe_cycles;
}

void wl_update_endurance_prediction(const WearLeveler *wl,
                                    double writes_per_hour) {
    double max_ec = (double)wl->stats.max;
    double sigma = wl->stats.stddev;
    double avg = wl->stats.avg;

    /* Safety margin based on variation */
    if (avg > 0.0) {
        endurance_pred.safety_margin = 1.0 + (sigma / avg);
    } else {
        endurance_pred.safety_margin = 1.0;
    }

    /* Effective worst-case PE */
    double effective_max = max_ec * endurance_pred.safety_margin;

    if (effective_max < endurance_pred.rated_pe_cycles) {
        endurance_pred.remaining_pe = endurance_pred.rated_pe_cycles
                                      - effective_max;
    } else {
        endurance_pred.remaining_pe = 0.0;
    }

    /* Project remaining life */
    if (writes_per_hour > 0.0 && endurance_pred.remaining_pe > 0.0) {
        double pe_per_hour = writes_per_hour / (WL_MAX_BLOCKS * FTL_PAGES_PER_BLOCK);
        if (pe_per_hour > 0.0) {
            endurance_pred.projected_life_hours =
                endurance_pred.remaining_pe / pe_per_hour;
        }
    }

    /* Write cliff detection */
    endurance_pred.write_cliff_detected =
        (sigma > avg * 0.5) && (max_ec > endurance_pred.rated_pe_cycles * 0.8);
}

void wl_print_endurance_prediction(void) {
    printf("Endurance Prediction:\n");
    printf("  Rated P/E Cycles:    %.0f\n", endurance_pred.rated_pe_cycles);
    printf("  Remaining P/E:       %.0f\n", endurance_pred.remaining_pe);
    printf("  Safety Margin:       %.2fx\n", endurance_pred.safety_margin);
    printf("  Projected Life:      %.0f hours (%.1f years)\n",
           endurance_pred.projected_life_hours,
           endurance_pred.projected_life_hours / 8760.0);
    printf("  Write Cliff Warning: %s\n",
           endurance_pred.write_cliff_detected ? "YES" : "no");
}

/* ── Data Retention Tracking ──
 *
 * L4: Data retention time decreases as P/E cycles increase
 * (Arrhenius model). Tracks the "worst case" retention based
 * on the most-erased block.
 */
typedef struct {
    double   retention_hours;     /* estimated retention at current state */
    double   min_retention_hours; /* worst-case across all blocks */
    uint32_t worst_retention_block;
} RetentionTracker;

static RetentionTracker ret_tracker;

void wl_init_retention_tracker(void) {
    memset(&ret_tracker, 0, sizeof(ret_tracker));
    ret_tracker.retention_hours = 87600.0;  /* 10 years at fresh state */
    ret_tracker.min_retention_hours = 87600.0;
}

/* Retention degradation: approximately halves every 10K P/E cycles */
void wl_update_retention(const WearLeveler *wl) {
    uint32_t max_ec = wl->stats.max;
    /* Simplified retention model: R(PE) = R0 * 2^(-PE/PE_half) */
    double pe_half = 10000.0;  /* PE cycles to halve retention */
    double r0 = 87600.0;       /* base retention at fresh state (hours) */

    ret_tracker.retention_hours = r0 * pow(2.0, -(double)max_ec / pe_half);
    if (ret_tracker.retention_hours < 1.0) ret_tracker.retention_hours = 1.0;

    if (ret_tracker.retention_hours < ret_tracker.min_retention_hours) {
        ret_tracker.min_retention_hours = ret_tracker.retention_hours;
    }
}

void wl_print_retention(const WearLeveler *wl) {
    printf("Data Retention:\n");
    printf("  Retention (current):  %.0f hours (%.1f days)\n",
           ret_tracker.retention_hours,
           ret_tracker.retention_hours / 24.0);
    printf("  Retention (worst):    %.0f hours (%.1f days)\n",
           ret_tracker.min_retention_hours,
           ret_tracker.min_retention_hours / 24.0);
    printf("  Based on max PE:      %u\n", wl->stats.max);
}

/* ── Static Wear Leveling Data Migration ──
 *
 * L5: Static wear leveling moves cold data from low-erase blocks
 * to high-erase blocks, and hot data vice versa. This equalizes
 * wear by ensuring all blocks participate in wear-leveling,
 * not just free blocks (dynamic WL).
 *
 * Cost: Write amplification from migration copies.
 * Benefit: Extended device lifetime (up to 2x for some workloads).
 */
int wl_static_migration_plan(const WearLeveler *wl,
                             uint32_t *source_block,
                             uint32_t *dest_block) {
    uint32_t i;
    uint32_t max_ec_block = 0, min_ec_block = 0;

    for (i = 0; i < WL_MAX_BLOCKS; i++) {
        uint32_t ec = wl->stats.erase_counts[i];
        if (ec == wl->stats.max) max_ec_block = i;
        if (ec == wl->stats.min) min_ec_block = i;
    }

    /* Only migrate if benefit exceeds cost */
    uint32_t ec_diff = wl->stats.max - wl->stats.min;
    if (ec_diff < wl->threshold) return 0;

    *source_block = min_ec_block;  /* move cold data FROM least-erased */
    *dest_block   = max_ec_block;  /* move hot data TO most-erased */
    return (int)ec_diff;
}

void wl_print_stats(const WearLeveler *wl) {
    printf("Wear Leveling Statistics:\n");
    printf("  Algorithm:       %s\n",
           wl->algorithm == WL_DYNAMIC ? "DYNAMIC" :
           wl->algorithm == WL_STATIC  ? "STATIC"  : "HYBRID");
    printf("  Threshold:       %u\n", wl->threshold);
    printf("  Min Erase Count: %u\n", wl->stats.min);
    printf("  Max Erase Count: %u\n", wl->stats.max);
    printf("  Avg Erase Count: %.2f\n", wl->stats.avg);
    printf("  Std Dev:         %.2f\n", wl->stats.stddev);
    printf("  Spread (max-min): %u\n",
           wl->stats.max - wl->stats.min);
}
