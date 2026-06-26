#include "memory_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ================================================================
 * memory_system.c -- Memory System Implementation (L1-L7)
 *
 * L1: struct defs  L2: virtual memory, TLB, page table concepts
 * L3: multi-level page table, TLB hierarchy, FR-FCFS scheduling
 * L4: working set model, Denning's locality principle
 * L5: page table walk algorithm, TLB replacement, FR-FCFS
 * L6: full MMU address translation with TLB miss handling
 * L7: memory-mapped I/O, DMA transfer model
 * ================================================================ */

/* ---- L1/L2: Virtual Address Decomposition ----
 *
 * Given a 32-bit virtual address:
 *   offset_bits = log2(PAGE_SIZE) = 12
 *   vpn_bits    = 32 - offset_bits = 20
 *   vpn         = addr >> 12
 *   offset      = addr & 0xFFF
 *
 * This is the fundamental decomposition that underpins all
 * virtual memory systems since the Atlas Computer (1962). */

VirtualAddress va_decompose(uint32_t addr) {
    VirtualAddress va;
    va.raw    = addr;
    va.offset = addr & (PAGE_SIZE - 1);
    va.vpn    = addr >> 12;
    return va;
}

uint32_t va_compose(uint32_t vpn, uint32_t offset) {
    return (vpn << 12) | (offset & (PAGE_SIZE - 1));
}

PhysicalAddress pa_decompose(uint32_t addr) {
    PhysicalAddress pa;
    pa.raw    = addr;
    pa.offset = addr & (PAGE_SIZE - 1);
    pa.pfn    = addr >> 12;
    return pa;
}

/* ---- L3: Page Table Operations ----
 *
 * Simplified 2-level page table:
 *   Page directory (level 1): index = VPN[19:10], each entry points to a page table
 *   Page table (level 2): index = VPN[9:0], each entry is a PTE
 *
 * Address translation:
 *   pte = page_table[vpn]
 *   paddr = (pte.pfn << 12) | offset
 *
 * Multi-level motivation (L3): most virtual address space is sparse;
 * a flat page table for 32-bit space would have 2^20 entries = 4MB.
 * Hierarchical tables allocate only the levels that are in use. */

int pagetable_init(PageTable* pt, uint32_t num_entries, uint32_t levels) {
    if (!pt) return -1;
    memset(pt, 0, sizeof(PageTable));
    pt->num_entries = num_entries;
    pt->levels      = (levels > 0 && levels <= MAX_PAGE_TABLE_LEVELS) ? levels : 2;
    pt->entries = (PageTableEntry*)calloc(num_entries, sizeof(PageTableEntry));
    if (!pt->entries) return -1;
    return 0;
}

void pagetable_destroy(PageTable* pt) {
    if (!pt) return;
    free(pt->entries);
    memset(pt, 0, sizeof(PageTable));
}

void pagetable_map(PageTable* pt, uint32_t vpn, uint32_t pfn,
                   bool writable, bool executable, bool user) {
    if (!pt || vpn >= pt->num_entries) return;
    PageTableEntry* pte = &pt->entries[vpn];
    pte->valid      = true;
    pte->pfn        = pfn;
    pte->writable   = writable;
    pte->executable = executable;
    pte->readable   = true;
    pte->user_accessible = user;
    pte->accessed   = false;
    pte->dirty      = false;
}

void pagetable_unmap(PageTable* pt, uint32_t vpn) {
    if (!pt || vpn >= pt->num_entries) return;
    memset(&pt->entries[vpn], 0, sizeof(PageTableEntry));
}

const PageTableEntry* pagetable_lookup(const PageTable* pt, uint32_t vpn) {
    if (!pt || vpn >= pt->num_entries) return NULL;
    const PageTableEntry* pte = &pt->entries[vpn];
    if (!pte->valid) return NULL;
    return pte;
}

/* ---- L5: TLB Implementation (Translation Lookaside Buffer) ----
 *
 * TLB is a small, fast cache for page table entries.  It exploits
 * temporal and spatial locality of memory references.
 *
 * Denning's Working Set principle (L4): A process's working set W(t, tau)
 * is the set of pages referenced in the last tau time units.  TLB
 * capacity should exceed the working set size to avoid thrashing.
 *
 * Replacement policies:
 *   LRU: least-recently-used (access_order based)
 *   FIFO: first-in-first-out (insertion order)
 *   Random: random eviction */

void tlb_init(TLB* tlb, TLBReplacePolicy policy) {
    if (!tlb) return;
    memset(tlb, 0, sizeof(TLB));
    tlb->policy = policy;
}

bool tlb_lookup(TLB* tlb, uint32_t vpn, uint32_t asid, uint32_t* pfn) {
    if (!tlb) return false;
    tlb->accesses++;

    for (uint32_t i = 0; i < TLB_ENTRIES; i++) {
        if (!tlb->entries[i].valid) continue;
        /* Match VPN; if global page, ignore ASID */
        bool asid_match = tlb->entries[i].global_page ||
                          (tlb->entries[i].asid == asid);
        if (tlb->entries[i].vpn == vpn && asid_match) {
            tlb->hits++;
            tlb->entries[i].access_order = tlb->clock++;
            tlb->entries[i].access_count++;
            if (pfn) *pfn = tlb->entries[i].pfn;
            return true;
        }
    }

    tlb->misses++;
    return false;
}

void tlb_insert(TLB* tlb, uint32_t vpn, uint32_t pfn, uint32_t asid, bool global) {
    if (!tlb) return;

    /* Check for duplicate first */
    for (uint32_t i = 0; i < TLB_ENTRIES; i++) {
        if (tlb->entries[i].valid && tlb->entries[i].vpn == vpn &&
            (tlb->entries[i].global_page || tlb->entries[i].asid == asid)) {
            /* Update existing entry */
            tlb->entries[i].pfn = pfn;
            tlb->entries[i].access_order = tlb->clock++;
            tlb->entries[i].access_count++;
            return;
        }
    }

    /* Find slot: empty first, then evict */
    int slot = -1;
    for (uint32_t i = 0; i < TLB_ENTRIES; i++) {
        if (!tlb->entries[i].valid) { slot = (int)i; break; }
    }

    if (slot < 0) {
        /* Evict using replacement policy */
        tlb->evictions++;
        switch (tlb->policy) {
            case TLB_REPLACE_FIFO: {
                uint32_t oldest = UINT32_MAX;
                for (uint32_t i = 0; i < TLB_ENTRIES; i++) {
                    if (!tlb->entries[i].global_page &&
                        tlb->entries[i].access_order < oldest) {
                        oldest = tlb->entries[i].access_order;
                        slot = (int)i;
                    }
                }
                if (slot < 0) slot = 0; /* fallback */
                break;
            }
            case TLB_REPLACE_LRU: {
                uint32_t lru = UINT32_MAX;
                for (uint32_t i = 0; i < TLB_ENTRIES; i++) {
                    if (!tlb->entries[i].global_page &&
                        tlb->entries[i].access_order < lru) {
                        lru = tlb->entries[i].access_order;
                        slot = (int)i;
                    }
                }
                if (slot < 0) slot = 0;
                break;
            }
            case TLB_REPLACE_RANDOM:
            default:
                slot = rand() % TLB_ENTRIES;
                break;
        }
    }

    if (slot >= 0) {
        tlb->entries[slot].valid        = true;
        tlb->entries[slot].vpn          = vpn;
        tlb->entries[slot].pfn          = pfn;
        tlb->entries[slot].asid         = asid;
        tlb->entries[slot].global_page  = global;
        tlb->entries[slot].access_order = tlb->clock++;
        tlb->entries[slot].access_count = 1;
    }
}

void tlb_flush(TLB* tlb) {
    if (!tlb) return;
    for (uint32_t i = 0; i < TLB_ENTRIES; i++) {
        if (!tlb->entries[i].global_page) {
            tlb->entries[i].valid = false;
        }
    }
}

double tlb_hit_rate(const TLB* tlb) {
    if (!tlb || tlb->accesses == 0) return 0.0;
    return (double)tlb->hits / (double)tlb->accesses;
}

/* ---- L6: Full MMU Address Translation ----
 *
 * This is the canonical page table walk with TLB.
 *
 * Algorithm (L5):
 *   1. TLB lookup
 *   2. On TLB hit: return physical address
 *   3. On TLB miss: walk page table
 *      a. Look up PTE
 *      b. If PTE not present: page fault (return failure)
 *      c. If PTE present: compute physical address, update TLB
 *   4. Return physical address
 *
 * Complexity: O(1) with TLB hit; O(levels) on TLB miss.
 * TLB hit rate > 99% is typical for well-optimized workloads. */

bool mmu_translate(TLB* tlb, const PageTable* pt, uint32_t vaddr,
                   uint32_t* paddr) {
    if (!tlb || !pt) return false;

    VirtualAddress va = va_decompose(vaddr);

    /* Step 1: Try TLB */
    uint32_t pfn;
    if (tlb_lookup(tlb, va.vpn, 0, &pfn)) {
        if (paddr) *paddr = (pfn << 12) | va.offset;
        return true;
    }

    /* Step 2: TLB miss — walk page table */
    const PageTableEntry* pte = pagetable_lookup(pt, va.vpn);
    if (!pte) {
        /* Page fault: page not present in memory */
        return false;
    }

    /* Step 3: Update TLB with new translation */
    tlb_insert(tlb, va.vpn, pte->pfn, 0, false);

    if (paddr) *paddr = (pte->pfn << 12) | va.offset;
    return true;
}

void tlb_dump(const TLB* tlb) {
    if (!tlb) return;
    printf("=== TLB State ===\n");
    printf("  Entries: %d, Policy: %s\n", TLB_ENTRIES,
           tlb->policy == TLB_REPLACE_LRU ? "LRU" :
           tlb->policy == TLB_REPLACE_FIFO ? "FIFO" : "Random");
    printf("  Hits: %u, Misses: %u, Rate: %.2f%%\n",
           tlb->hits, tlb->misses, tlb_hit_rate(tlb) * 100.0);
    printf("  Evictions: %u\n", tlb->evictions);
    int shown = 0;
    for (uint32_t i = 0; i < TLB_ENTRIES && shown < 8; i++) {
        if (tlb->entries[i].valid) {
            printf("    [%u] VPN=0x%05X -> PFN=0x%05X asid=%u %s (accesses=%u)\n",
                   i, tlb->entries[i].vpn, tlb->entries[i].pfn,
                   tlb->entries[i].asid,
                   tlb->entries[i].global_page ? "G" : " ",
                   tlb->entries[i].access_count);
            shown++;
        }
    }
    printf("==================\n");
}

void pagetable_dump(const PageTable* pt) {
    if (!pt) return;
    printf("=== Page Table ===\n");
    printf("  Entries: %u, Levels: %u\n", pt->num_entries, pt->levels);
    int mapped = 0;
    for (uint32_t i = 0; i < pt->num_entries; i++) {
        if (pt->entries[i].valid) mapped++;
    }
    printf("  Mapped pages: %d / %u\n", mapped, pt->num_entries);
    printf("==================\n");
}

/* ---- L5: FR-FCFS Memory Controller Algorithm ----
 *
 * First-Ready First-Come-First-Served (FR-FCFS):
 * Prioritizes requests that hit an open row buffer to maximize
 * DRAM bandwidth utilization.
 *
 * Algorithm:
 *   1. Find all "row-buffer hit" requests (same bank, same row as last)
 *   2. If any exist, pick the oldest among them
 *   3. Otherwise, pick the oldest request overall
 *
 * FR-FCFS is widely used in DDR3/DDR4 memory controllers because
 * it doubles throughput over naive FCFS for random-access workloads.
 *
 * Complexity: O(N) per scheduling decision, where N = queue size.
 *
 * Reference: Rixner et al., "Memory Access Scheduling," ISCA 2000. */

void memctrl_init(MemController* mc, uint32_t capacity) {
    if (!mc) return;
    memset(mc, 0, sizeof(MemController));
    mc->queue_capacity = capacity > 0 ? capacity : 32;
    mc->pending_queue = (MemRequest*)calloc(mc->queue_capacity, sizeof(MemRequest));
}

void memctrl_destroy(MemController* mc) {
    if (!mc) return;
    free(mc->pending_queue);
    memset(mc, 0, sizeof(MemController));
}

void memctrl_enqueue(MemController* mc, MemReqType type, uint32_t addr,
                     uint32_t data) {
    if (!mc || mc->queue_size >= mc->queue_capacity) return;
    MemRequest* req = &mc->pending_queue[mc->queue_size];
    req->type         = type;
    req->addr         = addr;
    req->data         = data;
    req->arrival_time = mc->total_served;
    req->priority     = 0;
    req->is_prefetch  = (type == REQ_PREFETCH);
    mc->queue_size++;
}

/* Find the row ID for a memory address.
 * Simplified: assume 8 banks, each with row = addr / PAGE_SIZE % 1024.
 * Row-buffer hit = same bank AND same row as previously accessed. */
static uint32_t mem_bank(uint32_t addr) {
    return (addr >> 6) & 0x7; /* 8 banks, interleaved at 64-byte granularity */
}

static uint32_t mem_row(uint32_t addr) {
    return (addr >> 13) & 0x3FF; /* 1024 rows per bank */
}

int memctrl_schedule(MemController* mc) {
    if (!mc || mc->queue_size == 0) return -1;

    /* Find row-buffer hits */
    int best_idx = -1;
    uint32_t best_arrival = UINT32_MAX;
    bool found_hit = false;

    for (uint32_t i = 0; i < mc->queue_size; i++) {
        MemRequest* req = &mc->pending_queue[i];
        uint32_t bank = mem_bank(req->addr);
        uint32_t row  = mem_row(req->addr);

        /* Check if this bank's row-buffer contains this row */
        if (mc->bank_busy[bank] == row) {
            if (req->arrival_time < best_arrival) {
                best_arrival = req->arrival_time;
                best_idx = (int)i;
                found_hit = true;
            }
        }
    }

    /* If no row-buffer hits, use FCFS */
    if (!found_hit) {
        for (uint32_t i = 0; i < mc->queue_size; i++) {
            if (mc->pending_queue[i].arrival_time < best_arrival) {
                best_arrival = mc->pending_queue[i].arrival_time;
                best_idx = (int)i;
            }
        }
    }

    if (best_idx < 0) return -1;

    /* Serve the selected request */
    MemRequest* served = &mc->pending_queue[best_idx];
    uint32_t bank = mem_bank(served->addr);
    uint32_t row  = mem_row(served->addr);

    /* Update row buffer */
    mc->bank_busy[bank] = row;

    /* Latency: row-buffer hit = 10 cycles, miss = 30 cycles */
    uint32_t latency = found_hit ? 10 : 30;
    mc->total_latency += latency;
    mc->total_served++;

    /* Remove from queue by shifting */
    for (uint32_t i = (uint32_t)best_idx; i < mc->queue_size - 1; i++) {
        mc->pending_queue[i] = mc->pending_queue[i + 1];
    }
    mc->queue_size--;

    return best_idx;
}

double memctrl_avg_latency(const MemController* mc) {
    if (!mc || mc->total_served == 0) return 0.0;
    return (double)mc->total_latency / (double)mc->total_served;
}

void memctrl_dump(const MemController* mc) {
    if (!mc) return;
    printf("=== Memory Controller ===\n");
    printf("  Queue: %u/%u, Served: %u\n",
           mc->queue_size, mc->queue_capacity, mc->total_served);
    printf("  Avg Latency: %.1f cycles\n", memctrl_avg_latency(mc));
    printf("  Bank row buffers:\n");
    for (int i = 0; i < 8; i++) {
        printf("    Bank %d: row %u\n", i, mc->bank_busy[i]);
    }
    printf("========================\n");
}

