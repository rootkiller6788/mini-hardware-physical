#ifndef MEMORY_SYSTEM_H
#define MEMORY_SYSTEM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ================================================================
 * memory_system.h -- Memory System (TLB, Page Table, Memory Controller)
 *
 * L1: struct definitions (TLB entry, page table entry, memory request)
 * L2: Virtual memory concepts (page table walk, TLB, address translation)
 * L3: Engineering: multi-level page table, TLB hierarchy, FR-FCFS scheduling
 * L4: Theorem: working set model, Denning's principle of locality
 * L5: Algorithm: page table walk, TLB replacement (LRU/FIFO), FR-FCFS
 * L6: Canonical problem: address translation with TLB miss handling
 * L7: Applications: memory-mapped I/O simulation, DMA transfer model
 *
 * Reference: MIT 6.004 Ch 15-16, Berkeley CS 162 Lec 13-16,
 * CMU 15-410 / 15-418, Stanford CS 140.
 * ================================================================ */

#define TLB_ENTRIES        64
#define PAGE_SIZE          4096
#define VPN_BITS           20
#define PFN_BITS           12
#define MAX_PAGE_TABLE_LEVELS 4

/* ---- L1: Core Type Definitions ---- */

/* Page Table Entry (PTE) — maps VPN to PFN with protection bits.
 * Format inspired by RISC-V Sv39 PTE. */
typedef struct {
    bool     valid;
    bool     readable;
    bool     writable;
    bool     executable;
    bool     user_accessible;
    bool     accessed;
    bool     dirty;
    uint32_t pfn;              /* physical frame number */
} PageTableEntry;

/* Virtual address decomposition */
typedef struct {
    uint32_t raw;
    uint32_t vpn;             /* virtual page number */
    uint32_t offset;          /* byte offset within page */
} VirtualAddress;

/* Physical address */
typedef struct {
    uint32_t raw;
    uint32_t pfn;             /* physical frame number */
    uint32_t offset;
} PhysicalAddress;

/* TLB Entry (Translation Lookaside Buffer) */
typedef enum {
    TLB_REPLACE_LRU,
    TLB_REPLACE_FIFO,
    TLB_REPLACE_RANDOM
} TLBReplacePolicy;

typedef struct {
    bool     valid;
    uint32_t vpn;             /* virtual page number tag */
    uint32_t pfn;             /* physical frame number data */
    uint32_t asid;            /* address space ID (multi-process) */
    bool     global_page;     /* true if mapping is global (OS kernel) */
    uint32_t access_order;    /* for LRU replacement tracking */
    uint32_t access_count;    /* total number of accesses to this entry */
} TLBEntry;

/* TLB structure */
typedef struct {
    TLBEntry        entries[TLB_ENTRIES];
    TLBReplacePolicy policy;
    uint32_t        clock;
    uint32_t        hits;
    uint32_t        misses;
    uint32_t        accesses;
    uint32_t        evictions;
} TLB;

/* Page Table: simplified 2-level scheme */
typedef struct {
    PageTableEntry* entries;   /* flat array of entries */
    uint32_t        num_entries;
    uint32_t        levels;    /* 1 = single-level, 2 = 2-level, etc. */
} PageTable;

/* Memory Controller Request */
typedef enum {
    REQ_READ,
    REQ_WRITE,
    REQ_REFRESH,
    REQ_PREFETCH
} MemReqType;

typedef struct {
    MemReqType type;
    uint32_t   addr;
    uint32_t   data;
    uint32_t   arrival_time;
    uint32_t   priority;
    bool       is_prefetch;
} MemRequest;

/* Memory controller with scheduling */
typedef struct {
    MemRequest* pending_queue;
    uint32_t    queue_size;
    uint32_t    queue_capacity;
    uint32_t    total_served;
    uint32_t    total_latency;
    uint32_t    bank_busy[8]; /* 8 memory banks */
} MemController;

/* ---- API: Virtual Address Translation ---- */

/* Decompose virtual address into VPN and offset. */
VirtualAddress va_decompose(uint32_t addr);

/* Compose virtual address from VPN and offset. */
uint32_t va_compose(uint32_t vpn, uint32_t offset);

/* Decompose physical address. */
PhysicalAddress pa_decompose(uint32_t addr);

/* ---- API: Page Table Operations ---- */

/* Initialize page table with specified number of entries. */
int  pagetable_init(PageTable* pt, uint32_t num_entries, uint32_t levels);

/* Free page table resources. */
void pagetable_destroy(PageTable* pt);

/* Map virtual page to physical frame with protection bits. */
void pagetable_map(PageTable* pt, uint32_t vpn, uint32_t pfn,
                   bool writable, bool executable, bool user);

/* Unmap a virtual page. */
void pagetable_unmap(PageTable* pt, uint32_t vpn);

/* Look up virtual page: returns pointer to PTE or NULL if not present. */
const PageTableEntry* pagetable_lookup(const PageTable* pt, uint32_t vpn);

/* ---- API: TLB Operations ---- */

/* Initialize TLB with given replacement policy. */
void tlb_init(TLB* tlb, TLBReplacePolicy policy);

/* TLB lookup: returns true if hit, and fills *pfn.
 * On miss, returns false. */
bool tlb_lookup(TLB* tlb, uint32_t vpn, uint32_t asid, uint32_t* pfn);

/* TLB insert: add a new translation entry. Evict if needed. */
void tlb_insert(TLB* tlb, uint32_t vpn, uint32_t pfn, uint32_t asid, bool global);

/* TLB flush: invalidate all non-global entries (context switch). */
void tlb_flush(TLB* tlb);

/* TLB hit rate. */
double tlb_hit_rate(const TLB* tlb);

/* ---- API: Full Address Translation ---- */

/* Full address translation: TLB -> Page Table walk -> Physical address.
 * Updates TLB on miss.  Returns true if translation succeeds.
 * This is the canonical MMU path (L6). */
bool mmu_translate(TLB* tlb, const PageTable* pt, uint32_t vaddr,
                   uint32_t* paddr);

/* ---- API: Memory Controller (L5: FR-FCFS Algorithm) ---- */

/* Initialize memory controller with queue capacity. */
void memctrl_init(MemController* mc, uint32_t capacity);

/* Free memory controller resources. */
void memctrl_destroy(MemController* mc);

/* Enqueue a memory request. */
void memctrl_enqueue(MemController* mc, MemReqType type, uint32_t addr,
                     uint32_t data);

/* Process one request: First-Ready First-Come-First-Served.
 * FR-FCFS prioritizes row-buffer hits over older requests.
 * Returns request index that was served, or -1 if queue empty. */
int  memctrl_schedule(MemController* mc);

/* Average latency of served requests. */
double memctrl_avg_latency(const MemController* mc);

/* ---- Utility: Dump ---- */

void tlb_dump(const TLB* tlb);
void pagetable_dump(const PageTable* pt);
void memctrl_dump(const MemController* mc);

#endif /* MEMORY_SYSTEM_H */

