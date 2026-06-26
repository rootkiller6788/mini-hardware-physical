#ifndef VIRTUAL_MEMORY_H
#define VIRTUAL_MEMORY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define VM_MAX_PAGES 65536
#define VM_PAGE_SIZE 4096
#define TLB_MAX_ENTRIES 64
#define PHYSICAL_MEMORY_PAGES 16384

typedef enum {
    TLB_LRU,
    TLB_FIFO,
    TLB_RANDOM
} TLBReplacementPolicy;

typedef struct {
    bool valid;
    uint32_t frame_number;
    bool present;
    bool dirty;
    bool accessed;
} PageTableEntry;

typedef struct {
    PageTableEntry *entries;
    uint32_t num_pages;
    uint32_t page_size;
    uint32_t frame_bitmap[PHYSICAL_MEMORY_PAGES / 32];
    uint32_t free_frames;
    uint32_t fifo_queue[PHYSICAL_MEMORY_PAGES];
    uint32_t fifo_head;
    uint32_t fifo_count;
} PageTable;

typedef struct {
    bool valid;
    uint32_t vpn;
    uint32_t pfn;
    uint64_t lru_counter;
} TLBEntry;

typedef struct {
    TLBEntry entries[TLB_MAX_ENTRIES];
    uint32_t size;
    TLBReplacementPolicy policy;
    uint64_t global_counter;
    uint64_t hits;
    uint64_t misses;
} TLB;

void vm_init(PageTable *pt, uint32_t num_pages, uint32_t page_size);
uint32_t vm_translate(PageTable *pt, uint32_t virtual_address);
uint32_t vm_handle_page_fault(PageTable *pt, uint32_t virtual_address);
uint32_t vm_get_vpn(uint32_t virtual_address, uint32_t page_size);
uint32_t vm_get_offset(uint32_t virtual_address, uint32_t page_size);
uint32_t vm_allocate_frame(PageTable *pt);
void vm_free_frame(PageTable *pt, uint32_t frame_number);
void vm_print_page_table(const PageTable *pt);

void tlb_init(TLB *tlb, uint32_t size, TLBReplacementPolicy policy);
bool tlb_lookup(TLB *tlb, uint32_t vpn, uint32_t *pfn);
void tlb_insert(TLB *tlb, uint32_t vpn, uint32_t pfn);
void tlb_flush(TLB *tlb);
void tlb_print_stats(const TLB *tlb);

#endif /* VIRTUAL_MEMORY_H */
