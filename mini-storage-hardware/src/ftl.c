#include "ftl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint32_t block_index_from_page(uint32_t physical_page) {
    return physical_page / FTL_PAGES_PER_BLOCK;
}

static uint32_t plane_index_from_page(uint32_t physical_page) {
    uint32_t block = block_index_from_page(physical_page);
    return block / FTL_BLOCKS_PER_PLANE;
}

static uint32_t block_in_plane(uint32_t physical_page) {
    return block_index_from_page(physical_page) % FTL_BLOCKS_PER_PLANE;
}

static uint32_t page_in_block(uint32_t physical_page) {
    return physical_page % FTL_PAGES_PER_BLOCK;
}

void ftl_init(FTL *ftl, FTLMappingMode mode) {
    uint32_t i;

    memset(ftl, 0, sizeof(FTL));
    ftl->mapping_mode = mode;

    for (i = 0; i < FTL_MAX_LBAS; i++) {
        ftl->mapping_table[i] = -1;
    }
    for (i = 0; i < FTL_MAX_PHYSICAL_PAGES; i++) {
        ftl->reverse_map[i] = -1;
    }

    ftl->write_pointer = 0;
    ftl->free_pages = FTL_MAX_PHYSICAL_PAGES;
}

static FlashPage *get_physical_page(FTL *ftl, uint32_t physical_page) {
    uint32_t plane = plane_index_from_page(physical_page);
    uint32_t blk   = block_in_plane(physical_page);
    uint32_t pg    = page_in_block(physical_page);

    if (plane >= FTL_PLANES || blk >= FTL_BLOCKS_PER_PLANE
        || pg >= FTL_PAGES_PER_BLOCK) {
        return NULL;
    }
    return &ftl->planes[plane].blocks[blk].pages[pg];
}

static FlashBlock *get_physical_block(FTL *ftl, uint32_t physical_page) {
    uint32_t plane = plane_index_from_page(physical_page);
    uint32_t blk   = block_in_plane(physical_page);

    if (plane >= FTL_PLANES || blk >= FTL_BLOCKS_PER_PLANE) {
        return NULL;
    }
    return &ftl->planes[plane].blocks[blk];
}

int ftl_read(const FTL *ftl, uint32_t lba, uint8_t *out_data) {
    if (lba >= FTL_MAX_LBAS) return -1;

    int32_t phys = ftl->mapping_table[lba];
    if (phys < 0) {
        memset(out_data, 0, 4096);
        return 0;
    }

    const FlashPage *page = get_physical_page((FTL *)ftl, (uint32_t)phys);
    if (!page || page->state != PAGE_STATE_VALID) return -1;

    memcpy(out_data, page->data, 4096);
    ((FTL *)ftl)->stats.reads++;
    return 0;
}

int ftl_write(FTL *ftl, uint32_t lba, const uint8_t *data) {
    if (lba >= FTL_MAX_LBAS) return -1;

    int32_t old_phys = ftl->mapping_table[lba];
    if (old_phys >= 0) {
        FlashPage *old_page = get_physical_page(ftl, (uint32_t)old_phys);
        if (old_page) {
            old_page->state = PAGE_STATE_INVALID;
        }
        ftl->reverse_map[old_phys] = -1;
    }

    if (ftl->free_pages == 0) return -2;

    FlashPage *page = get_physical_page(ftl, ftl->write_pointer);
    while (page && page->state != PAGE_STATE_FREE) {
        ftl->write_pointer = (ftl->write_pointer + 1) % FTL_MAX_PHYSICAL_PAGES;
        page = get_physical_page(ftl, ftl->write_pointer);
    }
    if (!page) return -1;

    page->id    = ftl->write_pointer;
    page->type  = PAGE_TYPE_LOWER;
    page->state = PAGE_STATE_VALID;
    memcpy(page->data, data, 4096);

    ftl->mapping_table[lba] = (int32_t)ftl->write_pointer;
    ftl->reverse_map[ftl->write_pointer] = (int32_t)lba;

    FlashBlock *block = get_physical_block(ftl, ftl->write_pointer);
    if (block) {
        if (block->state == BLOCK_STATE_FREE) {
            block->state = BLOCK_STATE_ACTIVE;
        }
    }

    ftl->free_pages--;
    ftl->write_pointer = (ftl->write_pointer + 1) % FTL_MAX_PHYSICAL_PAGES;
    ftl->stats.writes++;
    ftl->stats.host_writes++;
    return 0;
}

int ftl_trim(FTL *ftl, uint32_t lba) {
    if (lba >= FTL_MAX_LBAS) return -1;

    int32_t phys = ftl->mapping_table[lba];
    if (phys >= 0) {
        FlashPage *page = get_physical_page(ftl, (uint32_t)phys);
        if (page) {
            page->state = PAGE_STATE_INVALID;
        }
        ftl->reverse_map[phys] = -1;
        ftl->mapping_table[lba] = -1;
    }
    return 0;
}

/* ── Block-Level Mapping Operations ──
 *
 * L3: Block-level mapping maps LBA ranges to physical blocks.
 * Trade-off: smaller mapping table (1 entry per block of pages)
 * vs page-level (1 entry per page). Block-level suffers from
 * write amplification for small writes.
 *
 * L5: Log-block FTL (BAST/FAST): Use log blocks for updates,
 * merge log blocks with data blocks via switch/partial/full merge.
 */

/* Allocate a contiguous block for sequential data */
int ftl_allocate_block(FTL *ftl, uint32_t start_lba, uint32_t num_pages) {
    uint32_t i;
    if (start_lba + num_pages > FTL_MAX_LBAS) return -1;
    if (ftl->free_pages < num_pages) return -2;

    /* Find a contiguous free block */
    uint32_t block_start = ftl->write_pointer;
    uint32_t contiguous = 0;

    while (contiguous < num_pages) {
        FlashPage *page = get_physical_page(ftl, block_start);
        if (!page) return -3;
        if (page->state != PAGE_STATE_FREE) {
            block_start = (block_start + FTL_PAGES_PER_BLOCK) % FTL_MAX_PHYSICAL_PAGES;
            contiguous = 0;
            continue;
        }
        contiguous++;
        block_start = (block_start + 1) % FTL_MAX_PHYSICAL_PAGES;
    }

    /* Map the block */
    uint32_t phys = (block_start - num_pages + FTL_MAX_PHYSICAL_PAGES) % FTL_MAX_PHYSICAL_PAGES;
    for (i = 0; i < num_pages; i++) {
        uint32_t pp = (phys + i) % FTL_MAX_PHYSICAL_PAGES;
        FlashPage *page = get_physical_page(ftl, pp);
        if (!page) continue;
        page->state = PAGE_STATE_VALID;
        page->type  = PAGE_TYPE_LOWER;
        ftl->mapping_table[start_lba + i] = (int32_t)pp;
        ftl->reverse_map[pp] = (int32_t)(start_lba + i);
        ftl->free_pages--;
    }
    return 0;
}

/* ── SLC Write Buffer (Turbo Write) ──
 *
 * L8: Modern SSDs use a portion of NAND in SLC mode as a write buffer.
 * SLC programming is ~3x faster and ~10x more durable than TLC.
 * Data is later compacted from SLC to TLC during idle time or GC.
 *
 * This simulates an SLC buffer: pages written here are flagged for
 * later compaction to high-density storage.
 */
#define FTL_SLC_BUFFER_PAGES 1024

typedef struct {
    uint32_t slc_pages[FTL_SLC_BUFFER_PAGES];
    uint32_t slc_count;
    bool     slc_enabled;
} FTLExtended;

static FTLExtended ftl_ext;

void ftl_slc_buffer_init(void) {
    memset(&ftl_ext, 0, sizeof(ftl_ext));
    ftl_ext.slc_enabled = true;
}

int ftl_slc_buffer_write(FTL *ftl, uint32_t lba, const uint8_t *data) {
    if (!ftl_ext.slc_enabled || ftl_ext.slc_count >= FTL_SLC_BUFFER_PAGES) {
        return ftl_write(ftl, lba, data);
    }
    /* Write to SLC buffer region (first FTL_SLC_BUFFER_PAGES) */
    FlashPage *page = get_physical_page(ftl, ftl_ext.slc_count);
    if (!page) return -1;
    memcpy(page->data, data, 4096);
    page->state = PAGE_STATE_VALID;
    page->type  = PAGE_TYPE_LOWER;
    ftl->mapping_table[lba] = (int32_t)ftl_ext.slc_count;
    ftl_ext.slc_pages[ftl_ext.slc_count] = lba;
    ftl_ext.slc_count++;
    ftl->free_pages--;
    ftl->stats.writes++;
    ftl->stats.host_writes++;
    return 0;
}

/* Flush SLC buffer to main storage (compaction) */
int ftl_slc_buffer_flush(FTL *ftl) {
    uint32_t i;
    uint32_t flushed = 0;
    for (i = 0; i < ftl_ext.slc_count; i++) {
        uint32_t lba = ftl_ext.slc_pages[i];
        FlashPage *slc_page = get_physical_page(ftl, i);
        if (!slc_page || slc_page->state != PAGE_STATE_VALID) continue;
        /* Re-write via normal path to TLC region */
        ftl_write(ftl, lba, slc_page->data);
        slc_page->state = PAGE_STATE_INVALID;
        flushed++;
    }
    ftl_ext.slc_count = 0;
    return (int)flushed;
}

/* ── Hot/Cold Data Separator ──
 *
 * L5: Separating hot (frequently updated) from cold (static) data
 * reduces write amplification during GC. Hot data ages out quickly
 * (pages become invalid), while cold data stays valid.
 *
 * Simple frequency-based classifier using update count per LBA.
 * LBAs updated > threshold times are "hot" and placed in a dedicated
 * hot partition to improve GC efficiency.
 */
typedef struct {
    uint32_t update_count[FTL_MAX_LBAS];
    uint32_t total_updates;
} HotColdTracker;

static HotColdTracker hc_tracker;

#define HOT_THRESHOLD 5

void ftl_hotcold_init(void) {
    memset(&hc_tracker, 0, sizeof(hc_tracker));
}

bool ftl_is_hot_lba(uint32_t lba) {
    if (lba >= FTL_MAX_LBAS) return false;
    return hc_tracker.update_count[lba] >= HOT_THRESHOLD;
}

void ftl_track_update(uint32_t lba) {
    if (lba >= FTL_MAX_LBAS) return;
    hc_tracker.update_count[lba]++;
    hc_tracker.total_updates++;
}

double ftl_hot_data_ratio(void) {
    uint32_t i, hot = 0;
    for (i = 0; i < FTL_MAX_LBAS; i++) {
        if (hc_tracker.update_count[i] >= HOT_THRESHOLD) hot++;
    }
    return (double)hot / (double)FTL_MAX_LBAS;
}

/* ── Merge operations for log-block FTL ──
 *
 * L5: Three types of merge in BAST (Block Associative Sector Translation):
 *   1. Switch merge: All pages sequential, just swap mapping (cost: 0 copies)
 *   2. Partial merge: Some pages valid in data block, copy them (cost: < N copies)
 *   3. Full merge: Most pages valid, copy all valid pages (cost: N copies)
 *
 * Merge cost directly impacts write amplification:
 *   WA = (host_writes + merge_copies) / host_writes
 */
double ftl_merge_cost_estimate(uint32_t valid_pages_in_data,
                               uint32_t valid_pages_in_log,
                               uint32_t pages_per_block) {
    if (valid_pages_in_data == 0 && valid_pages_in_log == pages_per_block) {
        return 0.0;  /* switch merge */
    }
    if (valid_pages_in_data > pages_per_block / 2) {
        return (double)valid_pages_in_data;  /* full merge dominant */
    }
    return (double)valid_pages_in_data;  /* partial merge */
}

void ftl_print_stats(const FTL *ftl) {
    uint32_t i, valid_count = 0, free_count = 0, invalid_count = 0;

    for (i = 0; i < FTL_MAX_PHYSICAL_PAGES; i++) {
        const FlashPage *page = get_physical_page((FTL *)ftl, i);
        if (!page) continue;
        switch (page->state) {
        case PAGE_STATE_VALID:   valid_count++;   break;
        case PAGE_STATE_FREE:    free_count++;    break;
        case PAGE_STATE_INVALID: invalid_count++; break;
        default: break;
        }
    }

    printf("FTL Statistics:\n");
    printf("  Mode:             %s\n",
           ftl->mapping_mode == FTL_MAPPING_PAGE_LEVEL  ? "PAGE_LEVEL" :
           ftl->mapping_mode == FTL_MAPPING_BLOCK_LEVEL ? "BLOCK_LEVEL" : "HYBRID");
    printf("  Host Reads:       %llu\n",
           (unsigned long long)ftl->stats.reads);
    printf("  Host Writes:      %llu\n",
           (unsigned long long)ftl->stats.host_writes);
    printf("  Total Writes:     %llu\n",
           (unsigned long long)ftl->stats.writes);
    printf("  Erases:           %llu\n",
           (unsigned long long)ftl->stats.erases);
    printf("  GC Moves:         %llu\n",
           (unsigned long long)ftl->stats.gc_moves);
    printf("  Valid Pages:      %u\n", valid_count);
    printf("  Free Pages:       %u\n", free_count);
    printf("  Invalid Pages:    %u\n", invalid_count);
    if (ftl->stats.host_writes > 0) {
        printf("  Write Amplification: %.2f\n",
               (double)(ftl->stats.writes + ftl->stats.gc_moves)
               / (double)ftl->stats.host_writes);
    }
}
