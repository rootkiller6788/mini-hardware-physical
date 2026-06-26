#ifndef GC_H
#define GC_H

#include <stdbool.h>
#include <stdint.h>

#include "ftl.h"
#include "wear_leveling.h"

#define GC_DEFAULT_OP_PCT    7
#define GC_DEFAULT_THRESHOLD 5
#define GC_MAX_BLOCKS        WL_MAX_BLOCKS

typedef enum {
    GC_GREEDY,
    GC_COST_BENEFIT,
    GC_AGED_BLOCKS
} GCPolicy;

typedef struct {
    uint64_t total_bgs;
    uint64_t valid_pages_copied;
    uint64_t blocks_erased;
    double   write_amplification;
} GCMetrics;

typedef struct {
    GCPolicy     policy;
    uint32_t     overprovisioning_pct;
    uint32_t     threshold;
    GCMetrics    metrics;
    FTL         *ftl;
} GarbageCollector;

void gc_init(GarbageCollector *gc, FTL *ftl, GCPolicy policy,
             uint32_t op_pct, uint32_t threshold);
int  gc_trigger(GarbageCollector *gc);
int  gc_select_victim(const GarbageCollector *gc);
int  gc_migrate(GarbageCollector *gc, uint32_t victim_block);
int  gc_erase_block(GarbageCollector *gc, uint32_t block_idx);

/* Cost-Benefit GC — L5 (Rosenblum & Ousterhout, 1992) */
void gc_init_block_ages(void);
int  gc_select_cost_benefit(const GarbageCollector *gc);

/* Write Amplification formula — L4 */
double gc_write_amplification_formula(double utilization,
                                      double overprovisioning_pct);
double gc_optimal_op_for_target_wa(double target_wa, double utilization);

/* Multi-Stream GC — L8 (NVMe Streams Directive) */
void gc_streams_init(uint32_t num_streams);
int  gc_assign_stream(uint32_t lba);

/* Wear-Aware GC victim selection — L8 */
int  gc_select_wear_aware(const GarbageCollector *gc,
                          const uint32_t *erase_counts);

void gc_print_metrics(const GarbageCollector *gc);

#endif
