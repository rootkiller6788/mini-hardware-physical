#include "virtual_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void vm_init(PageTable *pt, uint32_t num_pages, uint32_t page_size)
{
    if (num_pages > VM_MAX_PAGES) num_pages = VM_MAX_PAGES;

    pt->num_pages = num_pages;
    pt->page_size = page_size;
    pt->free_frames = PHYSICAL_MEMORY_PAGES;
    pt->fifo_head = 0;
    pt->fifo_count = 0;

    pt->entries = (PageTableEntry *)calloc(num_pages, sizeof(PageTableEntry));
    if (!pt->entries) {
        fprintf(stderr, "vm_init: failed to allocate page table\n");
        exit(1);
    }

    memset(pt->frame_bitmap, 0, sizeof(pt->frame_bitmap));
    memset(pt->fifo_queue, 0, sizeof(pt->fifo_queue));
}

uint32_t vm_get_vpn(uint32_t virtual_address, uint32_t page_size)
{
    return virtual_address / page_size;
}

uint32_t vm_get_offset(uint32_t virtual_address, uint32_t page_size)
{
    return virtual_address % page_size;
}

uint32_t vm_allocate_frame(PageTable *pt)
{
    if (pt->free_frames == 0) {
        return vm_handle_page_fault(pt, 0xFFFFFFFFu);
    }

    for (uint32_t i = 0; i < PHYSICAL_MEMORY_PAGES; i++) {
        uint32_t word = i / 32;
        uint32_t bit = i % 32;
        if (!(pt->frame_bitmap[word] & (1u << bit))) {
            pt->frame_bitmap[word] |= (1u << bit);
            pt->free_frames--;
            return i;
        }
    }

    return 0;
}

void vm_free_frame(PageTable *pt, uint32_t frame_number)
{
    if (frame_number >= PHYSICAL_MEMORY_PAGES) return;

    uint32_t word = frame_number / 32;
    uint32_t bit = frame_number % 32;

    if (pt->frame_bitmap[word] & (1u << bit)) {
        pt->frame_bitmap[word] &= ~(1u << bit);
        pt->free_frames++;
    }
}

uint32_t vm_handle_page_fault(PageTable *pt, uint32_t virtual_address)
{
    if (pt->free_frames > 0) {
        return vm_allocate_frame(pt);
    }

    uint32_t victim = pt->fifo_queue[pt->fifo_head];
    pt->fifo_head = (pt->fifo_head + 1) % PHYSICAL_MEMORY_PAGES;
    pt->fifo_count--;

    for (uint32_t i = 0; i < pt->num_pages; i++) {
        if (pt->entries[i].present &&
            pt->entries[i].frame_number == victim) {
            if (pt->entries[i].dirty) {
            }
            pt->entries[i].present = false;
            pt->entries[i].valid = false;
            break;
        }
    }

    if (pt->fifo_count < PHYSICAL_MEMORY_PAGES) {
        pt->fifo_queue[(pt->fifo_head + pt->fifo_count) % PHYSICAL_MEMORY_PAGES] = victim;
        pt->fifo_count++;
    }

    return victim;
}

uint32_t vm_translate(PageTable *pt, uint32_t virtual_address)
{
    uint32_t vpn = vm_get_vpn(virtual_address, pt->page_size);
    uint32_t offset = vm_get_offset(virtual_address, pt->page_size);

    if (vpn >= pt->num_pages) return 0xFFFFFFFFu;

    if (!pt->entries[vpn].present) {
        uint32_t frame = vm_allocate_frame(pt);
        if (frame >= PHYSICAL_MEMORY_PAGES) {
            frame = vm_handle_page_fault(pt, virtual_address);
        }

        pt->entries[vpn].frame_number = frame;
        pt->entries[vpn].present = true;
        pt->entries[vpn].valid = true;
        pt->entries[vpn].accessed = true;
        pt->entries[vpn].dirty = false;

        if (pt->fifo_count < PHYSICAL_MEMORY_PAGES) {
            pt->fifo_queue[(pt->fifo_head + pt->fifo_count) % PHYSICAL_MEMORY_PAGES] = frame;
            pt->fifo_count++;
        }
    }

    pt->entries[vpn].accessed = true;
    return (pt->entries[vpn].frame_number * pt->page_size) + offset;
}

void vm_print_page_table(const PageTable *pt)
{
    printf("========================================\n");
    printf("  Page Table (size=%u pages, page=%u bytes)\n",
           pt->num_pages, pt->page_size);
    printf("========================================\n");
    printf("Free frames: %u / %u\n", pt->free_frames, PHYSICAL_MEMORY_PAGES);

    uint32_t mapped = 0;
    for (uint32_t i = 0; i < pt->num_pages; i++) {
        if (pt->entries[i].present) mapped++;
    }

    printf("Mapped pages: %u / %u\n", mapped, pt->num_pages);
    printf("========================================\n");
}

void tlb_init(TLB *tlb, uint32_t size, TLBReplacementPolicy policy)
{
    if (size > TLB_MAX_ENTRIES) size = TLB_MAX_ENTRIES;

    tlb->size = size;
    tlb->policy = policy;
    tlb->global_counter = 0;
    tlb->hits = 0;
    tlb->misses = 0;

    memset(tlb->entries, 0, sizeof(tlb->entries));

    srand((unsigned int)time(NULL));
}

bool tlb_lookup(TLB *tlb, uint32_t vpn, uint32_t *pfn)
{
    tlb->global_counter++;

    for (uint32_t i = 0; i < tlb->size; i++) {
        if (tlb->entries[i].valid && tlb->entries[i].vpn == vpn) {
            tlb->entries[i].lru_counter = tlb->global_counter;
            tlb->hits++;
            if (pfn) *pfn = tlb->entries[i].pfn;
            return true;
        }
    }

    tlb->misses++;
    return false;
}

void tlb_insert(TLB *tlb, uint32_t vpn, uint32_t pfn)
{
    uint32_t victim = 0;

    for (uint32_t i = 0; i < tlb->size; i++) {
        if (!tlb->entries[i].valid) {
            victim = i;
            break;
        }

        switch (tlb->policy) {
        case TLB_LRU: {
            uint64_t min_lru = tlb->entries[0].lru_counter;
            uint32_t min_idx = 0;
            for (uint32_t j = 1; j < tlb->size; j++) {
                if (tlb->entries[j].lru_counter < min_lru) {
                    min_lru = tlb->entries[j].lru_counter;
                    min_idx = j;
                }
            }
            victim = min_idx;
            break;
        }
        case TLB_FIFO:
            victim = tlb->global_counter % tlb->size;
            break;
        case TLB_RANDOM:
            victim = (uint32_t)rand() % tlb->size;
            break;
        }
    }

    tlb->entries[victim].valid = true;
    tlb->entries[victim].vpn = vpn;
    tlb->entries[victim].pfn = pfn;
    tlb->entries[victim].lru_counter = tlb->global_counter;
}

void tlb_flush(TLB *tlb)
{
    tlb->hits = 0;
    tlb->misses = 0;

    for (uint32_t i = 0; i < tlb->size; i++) {
        tlb->entries[i].valid = false;
        tlb->entries[i].vpn = 0;
        tlb->entries[i].pfn = 0;
        tlb->entries[i].lru_counter = 0;
    }
}

void tlb_print_stats(const TLB *tlb)
{
    uint64_t total = tlb->hits + tlb->misses;
    double hit_rate = total > 0 ? (double)tlb->hits / (double)total * 100.0 : 0.0;
    double miss_rate = total > 0 ? (double)tlb->misses / (double)total * 100.0 : 0.0;

    printf("========================================\n");
    printf("  TLB Statistics\n");
    printf("========================================\n");
    printf("  Size:     %u entries\n", tlb->size);
    printf("  Policy:   ");
    switch (tlb->policy) {
    case TLB_LRU:    printf("LRU\n"); break;
    case TLB_FIFO:   printf("FIFO\n"); break;
    case TLB_RANDOM: printf("Random\n"); break;
    }
    printf("----------------------------------------\n");
    printf("  Lookups:  %llu\n", (unsigned long long)total);
    printf("  Hits:     %llu\n", (unsigned long long)tlb->hits);
    printf("  Misses:   %llu\n", (unsigned long long)tlb->misses);
    printf("  Hit Rate: %.2f%%\n", hit_rate);
    printf("  Miss Rate: %.2f%%\n", miss_rate);
    printf("========================================\n");
}
