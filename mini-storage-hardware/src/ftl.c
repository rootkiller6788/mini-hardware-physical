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
