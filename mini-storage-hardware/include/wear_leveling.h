#ifndef WEAR_LEVELING_H
#define WEAR_LEVELING_H

#include <stdbool.h>
#include <stdint.h>

#include "ftl.h"

#define WL_MAX_BLOCKS (FTL_BLOCKS_PER_PLANE * FTL_PLANES)
#define WL_DEFAULT_THRESHOLD 100

typedef enum {
    WL_DYNAMIC,
    WL_STATIC,
    WL_HYBRID
} WearLevelAlgorithm;

typedef struct {
    uint32_t erase_counts[WL_MAX_BLOCKS];
    uint32_t min;
    uint32_t max;
    double   avg;
    double   stddev;
} WearLevelStats;

typedef struct {
    WearLevelAlgorithm algorithm;
    uint32_t           threshold;
    WearLevelStats     stats;
    FTL               *ftl;
} WearLeveler;

void  wl_init(WearLeveler *wl, FTL *ftl, WearLevelAlgorithm algo,
              uint32_t threshold);
bool  wl_check_and_balance(WearLeveler *wl);
int   wl_select_block(const WearLeveler *wl);
void  wl_update_stats(WearLeveler *wl);
void  wl_print_stats(const WearLeveler *wl);

#endif
