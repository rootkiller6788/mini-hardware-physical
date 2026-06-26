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
