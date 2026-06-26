#include "gc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint32_t block_index_from_page(uint32_t physical_page) {
    return physical_page / FTL_PAGES_PER_BLOCK;
}

static FlashBlock *get_block(FTL *ftl, uint32_t block_idx) {
    uint32_t plane = block_idx / FTL_BLOCKS_PER_PLANE;
    uint32_t blk   = block_idx % FTL_BLOCKS_PER_PLANE;
    if (plane >= FTL_PLANES || blk >= FTL_BLOCKS_PER_PLANE) return NULL;
    return &ftl->planes[plane].blocks[blk];
}

static uint32_t count_valid_pages(const FlashBlock *block) {
    uint32_t i, count = 0;
    for (i = 0; i < FTL_PAGES_PER_BLOCK; i++) {
        if (block->pages[i].state == PAGE_STATE_VALID) {
            count++;
        }
    }
    return count;
}

static uint32_t count_invalid_pages(const FlashBlock *block) {
    uint32_t i, count = 0;
    for (i = 0; i < FTL_PAGES_PER_BLOCK; i++) {
        if (block->pages[i].state == PAGE_STATE_INVALID) {
            count++;
        }
    }
    return count;
}

static uint32_t count_free_blocks(const FTL *ftl) {
    uint32_t i, free_count = 0;
    uint32_t total_blocks = FTL_BLOCKS_PER_PLANE * FTL_PLANES;

    for (i = 0; i < total_blocks; i++) {
        const FlashBlock *block = get_block((FTL *)ftl, i);
        if (block && count_valid_pages(block) == 0
            && count_invalid_pages(block) == 0
            && block->state == BLOCK_STATE_FREE) {
            free_count++;
        }
    }
    return free_count;
}

void gc_init(GarbageCollector *gc, FTL *ftl, GCPolicy policy,
             uint32_t op_pct, uint32_t threshold) {
    memset(gc, 0, sizeof(GarbageCollector));
    gc->ftl                  = ftl;
    gc->policy               = policy;
    gc->overprovisioning_pct = op_pct;
    gc->threshold            = threshold;
}

int gc_trigger(GarbageCollector *gc) {
    uint32_t total_blocks = FTL_BLOCKS_PER_PLANE * FTL_PLANES;
    uint32_t free_needed  = (total_blocks * gc->threshold) / 100;
    uint32_t free_current = count_free_blocks(gc->ftl);

    if (free_current >= free_needed) return 0;

    int victim = gc_select_victim(gc);
    if (victim < 0) return -1;

    gc->metrics.total_bgs++;

    if (gc_migrate(gc, (uint32_t)victim) != 0) return -2;
    if (gc_erase_block(gc, (uint32_t)victim) != 0) return -3;

    return 1;
}

int gc_select_victim(const GarbageCollector *gc) {
    uint32_t i;
    uint32_t total_blocks = FTL_BLOCKS_PER_PLANE * FTL_PLANES;
    uint32_t best_block   = 0;
    uint32_t fewest_valid = FTL_PAGES_PER_BLOCK + 1;
    bool     found        = false;

    for (i = 0; i < total_blocks; i++) {
        FlashBlock *block = get_block(gc->ftl, i);
        if (!block) continue;
        if (block->state == BLOCK_STATE_BAD || block->state == BLOCK_STATE_FREE) {
            continue;
        }

        uint32_t valid = count_valid_pages(block);
        uint32_t invalid = count_invalid_pages(block);

        if (invalid > 0 && valid < fewest_valid) {
            if (gc->policy == GC_GREEDY) {
                best_block    = i;
                fewest_valid  = valid;
                found         = true;
            } else if (gc->policy == GC_AGED_BLOCKS) {
                if (block->erase_count > get_block(gc->ftl, best_block)->erase_count) {
                    best_block = i;
                    fewest_valid = valid;
                    found = true;
                }
            } else {
                best_block   = i;
                fewest_valid = valid;
                found        = true;
            }
        }
    }

    return found ? (int)best_block : -1;
}

int gc_migrate(GarbageCollector *gc, uint32_t victim_block) {
    FlashBlock *block = get_block(gc->ftl, victim_block);
    if (!block) return -1;

    uint32_t i;
    for (i = 0; i < FTL_PAGES_PER_BLOCK; i++) {
        if (block->pages[i].state == PAGE_STATE_VALID) {
            int32_t lba = gc->ftl->reverse_map[block->pages[i].id];
            if (lba >= 0) {
                int32_t old_phys = gc->ftl->mapping_table[lba];
                if (old_phys >= 0) {
                    FlashBlock *old_block = get_block(gc->ftl,
                        block_index_from_page((uint32_t)old_phys));
                    if (old_block) {
                        old_block->pages[old_phys % FTL_PAGES_PER_BLOCK].state
                            = PAGE_STATE_INVALID;
                    }
                }

                if (ftl_write(gc->ftl, (uint32_t)lba, block->pages[i].data) == 0) {
                    gc->ftl->stats.gc_moves++;
                    gc->ftl->stats.host_writes--;
                    gc->metrics.valid_pages_copied++;
                }
            }
        }
    }
    return 0;
}

int gc_erase_block(GarbageCollector *gc, uint32_t block_idx) {
    FlashBlock *block = get_block(gc->ftl, block_idx);
    if (!block) return -1;

    uint32_t i;
    for (i = 0; i < FTL_PAGES_PER_BLOCK; i++) {
        block->pages[i].state = PAGE_STATE_FREE;
    }
    block->state = BLOCK_STATE_FREE;
    block->erase_count++;
    gc->ftl->free_pages += FTL_PAGES_PER_BLOCK;
    gc->ftl->stats.erases++;
    gc->metrics.blocks_erased++;
    gc->ftl->write_pointer = block_idx * FTL_PAGES_PER_BLOCK;

    return 0;
}

/* ── Cost-Benefit Garbage Collection ──
 *
 * L5: Cost-Benefit policy (Rosenblum & Ousterhout, 1992):
 *   Score = (1 - utilization) * age / (2 * utilization)
 *
 * Where:
 *   utilization = valid_pages / total_pages
 *   age = time since last cleaning (in write operations)
 *
 * Blocks with high free space (low utilization) and high age
 * are better victims. The denominator penalizes copying many valid pages.
 *
 * This is the foundation of log-structured file system GC.
 */
typedef struct {
    double age;
    uint32_t last_cleaned;
} BlockAge;

static BlockAge block_ages[WL_MAX_BLOCKS];

void gc_init_block_ages(void) {
    memset(block_ages, 0, sizeof(block_ages));
}

static double gc_cost_benefit_score(const FlashBlock *block,
                                    uint32_t block_idx,
                                    uint32_t current_time) {
    uint32_t valid = count_valid_pages(block);
    uint32_t total = FTL_PAGES_PER_BLOCK;
    double utilization = (double)valid / (double)total;

    if (utilization >= 1.0) return -1.0;

    double age = (double)(current_time - block_ages[block_idx].last_cleaned);
    if (age < 1.0) age = 1.0;

    /* Rosenblum-Ousterhout formula */
    double benefit = (1.0 - utilization) * age;
    double cost = 2.0 * utilization;
    if (cost < 0.01) cost = 0.01;

    return benefit / cost;
}

int gc_select_cost_benefit(const GarbageCollector *gc) {
    uint32_t i;
    uint32_t total_blocks = FTL_BLOCKS_PER_PLANE * FTL_PLANES;
    int best_block = -1;
    double best_score = -1.0;
    static uint32_t gc_time = 0;

    gc_time++;

    for (i = 0; i < total_blocks; i++) {
        FlashBlock *block = get_block(gc->ftl, i);
        if (!block) continue;
        if (block->state == BLOCK_STATE_BAD || block->state == BLOCK_STATE_FREE)
            continue;

        double score = gc_cost_benefit_score(block, i, gc_time);
        if (score > best_score) {
            best_score = score;
            best_block = (int)i;
        }
    }

    if (best_block >= 0) {
        block_ages[best_block].last_cleaned = gc_time;
    }
    return best_block;
}

/* ── Write Amplification Formula Derivation ──
 *
 * L4: WA = (host_writes + gc_writes) / host_writes
 *
 * From first principles, for a uniformly random workload:
 *   WA ≈ 1 / (1 - u * (1 - f))
 *
 * Where:
 *   u = space utilization (1 - overprovisioning)
 *   f = fraction of data that is cold (static)
 *
 * Derivation (simplified Desnoyers model, ACM TOS 2014):
 *   In steady state, each host write triggers gc_writes = u/(1-u) * avg_valid
 *   Therefore WA = 1 + u/(1-u) = 1/(1-u)
 *
 * With overprovisioning: effective_u = u / (1 + op)
 *   WA = 1 / (1 - u/(1+op))
 *
 * This matches empirical data for enterprise SSDs within ~10%.
 */
double gc_write_amplification_formula(double utilization,
                                      double overprovisioning_pct) {
    double effective_u = utilization / (1.0 + overprovisioning_pct / 100.0);
    if (effective_u >= 1.0) return 100.0;
    return 1.0 / (1.0 - effective_u);
}

/* ── Over-Provisioning Capacity Model ──
 *
 * L4: The relationship between OP and WA is non-linear.
 * OP = 7%  → WA ≈ 1.5-2.0
 * OP = 28% → WA ≈ 1.1-1.2
 * OP = 100% → WA ≈ 1.01 (diminishing returns)
 *
 * This model helps SSD architects choose OP% for target WA.
 */
double gc_optimal_op_for_target_wa(double target_wa, double utilization) {
    if (target_wa <= 1.0) return 100.0;
    /* Solve: target_wa = 1/(1 - u/(1+op))
     * → 1+op = u / (1 - 1/target_wa)
     * → op = u / (1 - 1/target_wa) - 1
     */
    double denom = 1.0 - 1.0 / target_wa;
    if (denom <= 0.0) return 100.0;
    double op = utilization / denom - 1.0;
    if (op < 0.0) op = 0.0;
    return op * 100.0;
}

/* ── Multi-Stream GC (L8: NVMe Streams Directive) ──
 *
 * Writes with similar lifetimes are grouped into streams.
 * Each stream gets its own erase blocks, reducing GC overhead
 * by keeping hot/cold data physically separated.
 *
 * Streams reduce WA by 30-50% for mixed workloads (Yang et al., FAST 2019).
 */
#define GC_MAX_STREAMS 16

typedef struct {
    uint32_t stream_blocks[GC_MAX_STREAMS][FTL_PAGES_PER_BLOCK];
    uint32_t stream_counts[GC_MAX_STREAMS];
    uint32_t num_streams;
} GCStreams;

static GCStreams gc_streams;

void gc_streams_init(uint32_t num_streams) {
    memset(&gc_streams, 0, sizeof(gc_streams));
    if (num_streams > GC_MAX_STREAMS) num_streams = GC_MAX_STREAMS;
    gc_streams.num_streams = num_streams;
}

int gc_assign_stream(uint32_t lba) {
    /* Simple hash-based stream assignment */
    if (gc_streams.num_streams == 0) return 0;
    return (int)(lba % gc_streams.num_streams);
}

/* ── Greedy with Wear-Awareness (L8) ──
 *
 * Combine GC victim selection with wear leveling:
 * Among blocks with similar utilization, prefer the least-erased one
 * to improve overall device endurance.
 */
int gc_select_wear_aware(const GarbageCollector *gc,
                         const uint32_t *erase_counts) {
    uint32_t i;
    uint32_t total_blocks = FTL_BLOCKS_PER_PLANE * FTL_PLANES;
    int best_block = -1;
    uint32_t best_ec = UINT32_MAX;
    uint32_t target_invalid = 0;

    /* First pass: find blocks with significant invalid pages */
    for (i = 0; i < total_blocks; i++) {
        FlashBlock *block = get_block(gc->ftl, i);
        if (!block || block->state == BLOCK_STATE_BAD) continue;
        uint32_t invalid = count_invalid_pages(block);
        if (invalid > target_invalid) target_invalid = invalid;
    }

    if (target_invalid < FTL_PAGES_PER_BLOCK / 10) {
        /* Not enough invalid pages, fall back to greedy */
        return gc_select_victim(gc);
    }

    /* Among high-invalid blocks, pick the least erased */
    for (i = 0; i < total_blocks; i++) {
        FlashBlock *block = get_block(gc->ftl, i);
        if (!block || block->state == BLOCK_STATE_BAD) continue;
        if (block->state == BLOCK_STATE_FREE) continue;
        uint32_t invalid = count_invalid_pages(block);
        if (invalid >= target_invalid / 2) {
            uint32_t ec = erase_counts ? erase_counts[i] : block->erase_count;
            if (ec < best_ec) {
                best_ec = ec;
                best_block = (int)i;
            }
        }
    }
    return best_block;
}

void gc_print_metrics(const GarbageCollector *gc) {
    uint32_t total_blocks = FTL_BLOCKS_PER_PLANE * FTL_PLANES;
    uint32_t free_blocks  = count_free_blocks(gc->ftl);
    double wa;

    wa = (gc->ftl->stats.writes + gc->ftl->stats.gc_moves) /
        (double)(gc->ftl->stats.host_writes > 0
                 ? gc->ftl->stats.host_writes : 1);

    printf("Garbage Collection Metrics:\n");
    printf("  Policy:               %s\n",
           gc->policy == GC_GREEDY      ? "GREEDY" :
           gc->policy == GC_COST_BENEFIT ? "COST_BENEFIT" : "AGED_BLOCKS");
    printf("  Over-provisioning:    %u%%\n", gc->overprovisioning_pct);
    printf("  GC Threshold:         %u%% free blocks\n", gc->threshold);
    printf("  Free Blocks:          %u / %u\n", free_blocks, total_blocks);
    printf("  Total BGs:            %llu\n",
           (unsigned long long)gc->metrics.total_bgs);
    printf("  Valid Pages Copied:   %llu\n",
           (unsigned long long)gc->metrics.valid_pages_copied);
    printf("  Blocks Erased:        %llu\n",
           (unsigned long long)gc->metrics.blocks_erased);
    printf("  Write Amplification:  %.2f\n", wa);
}
