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

/* Endurance Prediction — L4/L8 */
void wl_init_endurance_prediction(double rated_pe_cycles);
void wl_update_endurance_prediction(const WearLeveler *wl,
                                    double writes_per_hour);
void wl_print_endurance_prediction(void);

/* Data Retention Tracking — L4 */
void wl_init_retention_tracker(void);
void wl_update_retention(const WearLeveler *wl);
void wl_print_retention(const WearLeveler *wl);

/* Static Wear Leveling Migration — L5 */
int  wl_static_migration_plan(const WearLeveler *wl,
                              uint32_t *source_block,
                              uint32_t *dest_block);

void  wl_print_stats(const WearLeveler *wl);

#endif
