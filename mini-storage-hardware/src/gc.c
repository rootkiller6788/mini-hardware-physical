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

void gc_print_metrics(const GarbageCollector *gc) {
    uint32_t total_blocks = FTL_BLOCKS_PER_PLANE * FTL_PLANES;
    uint32_t free_blocks  = count_free_blocks(gc->ftl);

    gc->metrics.write_amplification =
        (gc->ftl->stats.writes + gc->ftl->stats.gc_moves) /
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
    printf("  Write Amplification:  %.2f\n",
           gc->metrics.write_amplification);
}
