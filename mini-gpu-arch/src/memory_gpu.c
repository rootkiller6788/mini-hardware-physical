/**
 * mini-gpu-arch: GPU Memory Hierarchy Implementation
 *
 * Knowledge layers:
 *   L1: GPU memory spaces (global, shared, local, constant, texture, register)
 *   L2: Cache hierarchy (L1/L2 with LRU replacement, write-back)
 *   L3: TLB with LRU page translation
 *   L4: Memory consistency models (relaxed, acquire-release, sequential)
 *   L5: Address coalescing analyzer, shared memory bank conflict analyzer
 *   L6: Cacheline-aligned access counting for warp-level transactions
 *   L7: Memory fence semantics (CTA/GPU/system scope)
 *   L8: Warp coalesced transaction estimator
 *
 * GPU Memory Hierarchy (NVIDIA):
 *   Registers (fastest) → L1/Smem → L2 → HBM2e/HBM3 → System Memory (slowest)
 *
 * References:
 *   - NVIDIA CUDA C Programming Guide §5.3 (Memory Hierarchy)
 *   - NVIDIA CUDA Best Practices Guide §9 (Memory Optimizations)
 *   - NVIDIA PTX ISA §7 (Memory Consistency Model)
 */

#include "memory_gpu.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ===================================================================
 * L1: GPU Memory Subsystem Lifecycle
 * =================================================================== */

GPUMemorySubsystem* gpu_mem_create(uint64_t global_size, int l1_sz, int l2_sz) {
    GPUMemorySubsystem *m = (GPUMemorySubsystem*)calloc(1, sizeof(GPUMemorySubsystem));
    if (!m) return NULL;

    /* Global memory (HBM) */
    m->global_mem.total_size = global_size;
    m->global_mem.allocated = 0;
    m->global_mem.num_channels = 8;
    m->global_mem.data = (float*)calloc((size_t)(global_size / sizeof(float) + 1),
                                         sizeof(float));
    if (!m->global_mem.data) {
        free(m);
        return NULL;
    }

    /* Shared memory */
    m->shared_mem.num_banks = SMEM_NUM_BANKS;
    m->shared_mem.bank_width = SMEM_BANK_WIDTH;
    m->shared_mem.total_size = 49152;
    memset(m->shared_mem.access_count, 0, sizeof(m->shared_mem.access_count));

    /* L1 cache setup */
    m->l1_cache.level = CACHE_L1;
    m->l1_cache.total_size = l1_sz;
    m->l1_cache.line_size = 128;  /* 128B cacheline (NVIDIA L1) */
    m->l1_cache.num_sets = CACHE_NUM_SETS;
    m->l1_cache.assoc = CACHE_ASSOC;
    m->l1_cache.sets = (CacheSet*)calloc(CACHE_NUM_SETS, sizeof(CacheSet));
    m->l1_cache.lru_epoch = 0;

    /* L2 cache setup */
    m->l2_cache.level = CACHE_L2;
    m->l2_cache.total_size = l2_sz;
    m->l2_cache.line_size = 128;
    m->l2_cache.num_sets = CACHE_NUM_SETS;
    m->l2_cache.assoc = CACHE_ASSOC;
    m->l2_cache.sets = (CacheSet*)calloc(CACHE_NUM_SETS, sizeof(CacheSet));
    m->l2_cache.lru_epoch = 0;

    /* TLB */
    m->tlb.num_entries = TLB_ENTRIES;
    m->tlb.age_counter = 0;

    /* Consistency model: default relaxed (CUDA default) */
    m->consistency = CONS_RELAXED;

    /* Pending ops queue */
    m->pending_head = 0;
    m->pending_tail = 0;
    m->fence_active = false;

    return m;
}

void gpu_mem_destroy(GPUMemorySubsystem *m) {
    if (!m) return;
    free(m->global_mem.data);
    free(m->l1_cache.sets);
    free(m->l2_cache.sets);
    free(m);
}

void gpu_mem_reset(GPUMemorySubsystem *m) {
    if (!m) return;

    /* Clear global memory */
    if (m->global_mem.data) {
        memset(m->global_mem.data, 0, (size_t)m->global_mem.total_size);
    }
    m->global_mem.allocated = 0;

    /* Clear shared memory */
    memset(m->shared_mem.data, 0, sizeof(m->shared_mem.data));
    memset(m->shared_mem.access_count, 0, sizeof(m->shared_mem.access_count));

    /* Reset caches */
    if (m->l1_cache.sets) {
        memset(m->l1_cache.sets, 0, CACHE_NUM_SETS * sizeof(CacheSet));
    }
    m->l1_cache.accesses = 0;
    m->l1_cache.hits = 0;
    m->l1_cache.misses = 0;
    m->l1_cache.evictions = 0;
    m->l1_cache.writebacks = 0;
    m->l1_cache.lru_epoch = 0;

    if (m->l2_cache.sets) {
        memset(m->l2_cache.sets, 0, CACHE_NUM_SETS * sizeof(CacheSet));
    }
    m->l2_cache.accesses = 0;
    m->l2_cache.hits = 0;
    m->l2_cache.misses = 0;
    m->l2_cache.evictions = 0;
    m->l2_cache.writebacks = 0;
    m->l2_cache.lru_epoch = 0;

    /* Reset TLB */
    memset(m->tlb.entries, 0, TLB_ENTRIES * sizeof(TLBEntry));
    m->tlb.hits = 0;
    m->tlb.misses = 0;
    m->tlb.age_counter = 0;

    /* Reset pending ops */
    m->pending_head = 0;
    m->pending_tail = 0;
    m->fence_active = false;
    m->total_reads = 0;
    m->total_writes = 0;
    m->coalesced_accesses = 0;
    m->uncoalesced_accesses = 0;
    m->dram_cycles = 0;
}

/* ===================================================================
 * L2: Memory Access API
 * =================================================================== */

/**
 * Read from GPU memory hierarchy.
 *
 * Access path depends on memory space:
 *   GLOBAL:   L1 → L2 → HBM (DRAM)
 *   SHARED:   Shared memory SRAM (on-chip)
 *   CONSTANT: L1 constant cache → L2 → HBM
 *   LOCAL:    L1 (thread-private) → L2 → HBM (thread stack spill)
 *   REGISTER: direct register access (not modeled here)
 *
 * Returns true on success.
 */
bool gpu_mem_read(GPUMemorySubsystem *m, uint64_t addr, float *data, int size,
                  GPUMemSpace space, uint32_t thread_id, uint32_t warp_id) {
    if (!m || !data || size <= 0) return false;

    m->total_reads++;

    switch (space) {
        case MEM_SHARED: {
            /* Shared memory: direct SRAM access (no cache) */
            uint64_t smem_addr = addr;
            if (smem_addr + (uint64_t)size <= (uint64_t)m->shared_mem.total_size) {
                memcpy(data, &m->shared_mem.data[smem_addr / sizeof(float)], (size_t)size);
                /* Check bank conflicts */
                int bank = (int)((smem_addr / m->shared_mem.bank_width) % m->shared_mem.num_banks);
                m->shared_mem.access_count[bank]++;
                return true;
            }
            return false;
        }

        case MEM_GLOBAL:
        case MEM_LOCAL:
        case MEM_CONSTANT:
        case MEM_TEXTURE: {
            /* Cache hierarchy access: track hits/misses */
            bool l1_hit = cache_access(&m->l1_cache, addr, false);
            if (!l1_hit) {
                bool l2_hit = cache_access(&m->l2_cache, addr, false);
                if (!l2_hit) {
                    /* DRAM access latency penalty */
                    m->dram_cycles += 200;
                    /* Fill caches on miss */
                    cache_access(&m->l2_cache, addr, false);
                    (void)cache_access(&m->l1_cache, addr, false);
                }
            }
            /* Read data from backing store regardless of cache outcome */
            if (addr + (uint64_t)size <= m->global_mem.total_size) {
                memcpy(data, &m->global_mem.data[addr / sizeof(float)], (size_t)size);
            }
            return true;
        }

        case MEM_REGISTER:
            /* Not modeled (handled by register file in SM) */
            return false;

        default:
            return false;
    }

    (void)thread_id; (void)warp_id;
    return true;
}

/**
 * Write to GPU memory hierarchy.
 *
 * Write policy: write-back for L1/L2 caches
 * Shared memory: write-through (immediately visible to all threads in block)
 */
bool gpu_mem_write(GPUMemorySubsystem *m, uint64_t addr, const float *data, int size,
                   GPUMemSpace space, uint32_t thread_id, uint32_t warp_id) {
    if (!m || !data || size <= 0) return false;

    m->total_writes++;

    switch (space) {
        case MEM_SHARED: {
            uint64_t smem_addr = addr;
            if (smem_addr + (uint64_t)size <= (uint64_t)m->shared_mem.total_size) {
                memcpy(&m->shared_mem.data[smem_addr / sizeof(float)], data, (size_t)size);
                int bank = (int)((smem_addr / m->shared_mem.bank_width) % m->shared_mem.num_banks);
                m->shared_mem.access_count[bank]++;
                return true;
            }
            return false;
        }

        case MEM_GLOBAL:
        case MEM_LOCAL: {
            /* Write-back: write to cache, mark dirty */
            cache_access(&m->l1_cache, addr, true);
            m->l2_cache.accesses++;
            if (addr + size <= m->global_mem.total_size) {
                m->global_mem.allocated += size;
                memcpy(&m->global_mem.data[addr / sizeof(float)], data, (size_t)size);
            }
            return true;
        }

        default:
            return false;
    }

    (void)thread_id; (void)warp_id;
    return true;
}

/* ===================================================================
 * L3: Cache Operations
 * =================================================================== */

/**
 * CPU-style set-associative cache access with LRU replacement.
 *
 * Tag = address / (line_size * num_sets)
 * Set index = (address / line_size) % num_sets
 *
 * On miss: LRU replacement, write-back dirty line if needed.
 *
 * Complexity: O(assoc) per access (scan associativity set)
 */
bool cache_access(GPUCache *c, uint64_t addr, bool is_write) {
    if (!c || !c->sets) return false;

    uint64_t tag = cache_tag(addr, c->line_size, c->num_sets);
    uint32_t set_idx = cache_set_index(addr, c->line_size, c->num_sets);

    c->accesses++;
    CacheSet *set = &c->sets[set_idx];

    /* Look for hit */
    for (int way = 0; way < c->assoc; way++) {
        if (set->lines[way].valid && set->lines[way].tag == tag) {
            /* Cache hit */
            c->hits++;
            set->lines[way].last_access_cycle = c->lru_epoch++;
            set->lru_counter[way] = c->lru_epoch;

            if (is_write) {
                set->lines[way].dirty = true;
            }
            return true;
        }
    }

    /* Cache miss: find LRU victim */
    c->misses++;

    int lru_way = 0;
    uint32_t min_lru = set->lru_counter[0];
    for (int way = 1; way < c->assoc; way++) {
        if (!set->lines[way].valid) {
            lru_way = way;
            break;
        }
        if (set->lru_counter[way] < min_lru) {
            min_lru = set->lru_counter[way];
            lru_way = way;
        }
    }

    /* Evict if dirty */
    if (set->lines[lru_way].valid && set->lines[lru_way].dirty) {
        c->writebacks++;
        c->evictions++;
    } else if (set->lines[lru_way].valid) {
        c->evictions++;
    }

    /* Install new line */
    set->lines[lru_way].valid = true;
    set->lines[lru_way].dirty = is_write;
    set->lines[lru_way].tag = tag;
    set->lines[lru_way].last_access_cycle = c->lru_epoch++;
    set->lru_counter[lru_way] = c->lru_epoch;

    return false;
}

void cache_flush(GPUCache *c) {
    if (!c || !c->sets) return;
    for (int s = 0; s < c->num_sets; s++) {
        for (int w = 0; w < c->assoc; w++) {
            if (c->sets[s].lines[w].dirty) {
                c->writebacks++;
                c->sets[s].lines[w].dirty = false;
            }
        }
    }
}

void cache_invalidate(GPUCache *c) {
    if (!c || !c->sets) return;
    for (int s = 0; s < c->num_sets; s++) {
        for (int w = 0; w < c->assoc; w++) {
            c->sets[s].lines[w].valid = false;
            c->sets[s].lines[w].dirty = false;
        }
    }
}

double cache_hit_rate(const GPUCache *c) {
    if (!c || c->accesses == 0) return 0.0;
    return (double)c->hits / (double)c->accesses;
}

uint64_t cache_tag(uint64_t addr, int line_size, int num_sets) {
    uint64_t line_addr = addr / line_size;
    return line_addr / num_sets;
}

uint32_t cache_set_index(uint64_t addr, int line_size, int num_sets) {
    uint64_t line_addr = addr / line_size;
    return (uint32_t)(line_addr % num_sets);
}

/* ===================================================================
 * L4: Coalescing Analysis
 * =================================================================== */

/**
 * Warp-level memory coalescing analyzer.
 *
 * Analyzes W addresses (one per thread) and determines:
 *   - Number of L2 cache line transactions required
 *   - Coalescing efficiency (actual / ideal)
 *   - Misalignment status
 *
 * Perfect coalescing: all threads access within aligned 128-byte segment.
 * Misaligned: segment crosses 128-byte boundary → 2 transactions.
 *
 * Reference: NVIDIA CUDA Best Practices Guide §9.2.1
 *
 * Complexity: O(W) where W = warp_size
 */
CoalescingAnalysis coalesce_analyze(const uint32_t *addresses, int num_threads,
                                     int cacheline_size) {
    CoalescingAnalysis ca = {0};
    ca.cacheline_size = cacheline_size;
    ca.num_threads = num_threads;

    if (!addresses || num_threads <= 0 || cacheline_size <= 0) {
        ca.coalescing_efficiency = 0.0;
        return ca;
    }

    /* Record unique cacheline segments accessed */
    int cachelines_touched[32] = {0};
    int num_touched = 0;

    for (int i = 0; i < num_threads && i < 32; i++) {
        uint32_t addr = addresses[i];
        ca.addresses[i] = addr;

        int cl = addr / cacheline_size;
        bool found = false;
        for (int j = 0; j < num_touched; j++) {
            if (cachelines_touched[j] == cl) { found = true; break; }
        }
        if (!found && num_touched < 32) {
            cachelines_touched[num_touched++] = cl;
        }

        /* Check alignment */
        if (addr % 4 != 0) {
            ca.misaligned_count++;
        }
    }

    ca.num_transactions = num_touched;

    /* Ideal: all addresses within same aligned 128-byte segment */
    int bytes_needed = num_threads * 4;
    ca.min_transactions = (bytes_needed + cacheline_size - 1) / cacheline_size;

    if (ca.num_transactions > 0) {
        ca.coalescing_efficiency = (double)ca.min_transactions / (double)ca.num_transactions;
        if (ca.coalescing_efficiency > 1.0) ca.coalescing_efficiency = 1.0;
    } else {
        ca.coalescing_efficiency = 0.0;
    }

    ca.is_coalesced = (ca.num_transactions == ca.min_transactions);

    return ca;
}

/**
 * Shared memory bank conflict analyzer for a warp.
 *
 * Bank ID = (byte_address / bank_width) % num_banks
 *
 * If k threads access different addresses in the same bank,
 * k serialized accesses are required, degrading performance by k×.
 *
 * Padding technique: add padding to avoid bank conflicts
 * (e.g., 32 banks × 4 bytes → add 4 bytes padding to arrays).
 */
BankConflictAnalysis shared_bank_analyze(const uint32_t *addresses, int num_threads,
                                          int num_banks, int bank_width) {
    BankConflictAnalysis bca = {0};
    bca.num_threads = num_threads;
    bca.num_banks = num_banks;
    bca.ideal_cycles = 1;

    if (!addresses || num_threads <= 0 || num_banks <= 0) {
        bca.efficiency = 0.0;
        return bca;
    }

    int bank_counts[64] = {0};
    int max_c = 0;

    for (int i = 0; i < num_threads && i < 32; i++) {
        bca.addresses[i] = addresses[i];
        int bank = (addresses[i] / bank_width) % num_banks;
        bca.bank_id[i] = bank;

        if (bank >= 0 && bank < 64) {
            bank_counts[bank]++;
            if (bank_counts[bank] > max_c) {
                max_c = bank_counts[bank];
            }
        }
    }

    bca.max_conflict = max_c;
    bca.conflict_cycles = (max_c > 0) ? max_c : 1;
    bca.efficiency = (bca.conflict_cycles > 0)
                     ? (double)bca.ideal_cycles / (double)bca.conflict_cycles : 0.0;

    return bca;
}

double coalesced_bandwidth_util(const CoalescingAnalysis *ca, double peak_bw) {
    if (!ca || peak_bw <= 0.0) return 0.0;
    return ca->coalescing_efficiency * peak_bw;
}

/* ===================================================================
 * L5: Memory Consistency / Fence
 * =================================================================== */

void mem_fence_issue(GPUMemorySubsystem *m, FenceScope scope) {
    if (!m) return;
    m->fence_active = true;
    m->active_fence.scope = scope;
    m->active_fence.pending_ops = (m->pending_tail >= m->pending_head)
        ? (m->pending_tail - m->pending_head) : 0;
}

bool mem_fence_complete(GPUMemorySubsystem *m, uint32_t current_cycle) {
    if (!m || !m->fence_active) return true;

    /* Fence completes when all prior memory operations complete */
    if (m->pending_head == m->pending_tail) {
        /* All ops drained */
        m->active_fence.completed_cycle = current_cycle;
        m->fence_active = false;
        return true;
    }

    /* Check if pending ops are complete */
    bool all_done = true;
    for (int i = m->pending_head; i != m->pending_tail;
         i = (i + 1) % MEM_MAX_PENDING) {
        if (m->pending[i].complete_cycle > current_cycle) {
            all_done = false;
            break;
        }
    }

    if (all_done) {
        m->active_fence.completed_cycle = current_cycle;
        m->fence_active = false;
        return true;
    }

    return false;
}

/**
 * Check memory ordering between two operations.
 *
 * For relaxed consistency: no ordering guarantees.
 * For acquire-release: load-acquire sees all prior store-releases.
 * For sequential: all operations appear in program order (total store order).
 */
bool mem_order_check(const GPUMemorySubsystem *m, const MemOperation *a,
                     const MemOperation *b) {
    if (!m || !a || !b) return true;

    switch (m->consistency) {
        case CONS_RELAXED:
            /* No ordering guarantee — operations can be reordered arbitrarily */
            return true;

        case CONS_ACQUIRE_RELEASE:
            /* Release (store) → Acquire (load): store must complete before load */
            if (a->is_write && !b->is_write) {
                return a->issue_cycle <= b->complete_cycle;
            }
            return true;

        case CONS_SEQUENTIAL:
            /* Total store order: all ops must complete in program order */
            return a->complete_cycle <= b->issue_cycle;

        default:
            return true;
    }
}

/* ===================================================================
 * L6: TLB Operations
 * =================================================================== */

bool tlb_lookup(GPUTLB *tlb, uint64_t vpn, uint64_t *ppn, GPUMemSpace *space) {
    if (!tlb || !ppn) return false;

    for (int i = 0; i < tlb->num_entries; i++) {
        if (tlb->entries[i].valid && tlb->entries[i].virtual_page == vpn) {
            tlb->entries[i].last_used = tlb->age_counter++;
            *ppn = tlb->entries[i].physical_page;
            if (space) *space = tlb->entries[i].space;
            tlb->hits++;
            return true;
        }
    }

    tlb->misses++;
    return false;
}

void tlb_insert(GPUTLB *tlb, uint64_t vpn, uint64_t ppn, GPUMemSpace space) {
    if (!tlb) return;

    /* Find empty entry or LRU victim */
    int victim = -1;
    uint32_t min_age = UINT32_MAX;

    for (int i = 0; i < tlb->num_entries; i++) {
        if (!tlb->entries[i].valid) {
            victim = i;
            break;
        }
        if (tlb->entries[i].last_used < min_age) {
            min_age = tlb->entries[i].last_used;
            victim = i;
        }
    }

    if (victim >= 0) {
        tlb->entries[victim].virtual_page = vpn;
        tlb->entries[victim].physical_page = ppn;
        tlb->entries[victim].space = space;
        tlb->entries[victim].valid = true;
        tlb->entries[victim].last_used = tlb->age_counter++;
    }
}

double tlb_hit_rate(const GPUTLB *tlb) {
    if (!tlb) return 0.0;
    uint64_t total = tlb->hits + tlb->misses;
    if (total == 0) return 0.0;
    return (double)tlb->hits / (double)total;
}

/* ===================================================================
 * L7: Shared Memory Operations
 * =================================================================== */

void smem_store(SharedMemory *sm, int bank, int offset, float val) {
    if (!sm || bank < 0 || bank >= SMEM_NUM_BANKS || offset < 0) return;
    int idx = bank + offset * SMEM_NUM_BANKS;
    if (idx >= 0 && (size_t)idx < sizeof(sm->data) / sizeof(sm->data[0])) {
        sm->data[idx] = val;
    }
}

float smem_load(const SharedMemory *sm, int bank, int offset) {
    if (!sm || bank < 0 || bank >= SMEM_NUM_BANKS || offset < 0) return 0.0f;
    int idx = bank + offset * SMEM_NUM_BANKS;
    if (idx >= 0 && (size_t)idx < sizeof(sm->data) / sizeof(sm->data[0])) {
        return sm->data[idx];
    }
    return 0.0f;
}

/** Check if any bank has >1 access (indicating a bank conflict) */
bool smem_check_bank_conflict(const SharedMemory *sm) {
    if (!sm) return false;
    for (int i = 0; i < SMEM_NUM_BANKS; i++) {
        if (sm->access_count[i] > 1) return true;
    }
    return false;
}

/* ===================================================================
 * L8: Warp-Level Transaction Counter
 * =================================================================== */

/**
 * Compute number of coalesced memory transactions for a warp access.
 *
 * Given W addresses (one per warp thread), counts the number of aligned
 * 128-byte segments touched. This is the actual number of L2 transactions.
 *
 * Complexity: O(W) using segment tracking.
 */
int warp_coalesced_transactions(const uint32_t *addrs, int warp_size, int cl_size) {
    if (!addrs || warp_size <= 0 || cl_size <= 0) return 0;

    /* Track which cacheline segments are touched */
    int segments[32] = {0};
    int num_segments = 0;

    for (int i = 0; i < warp_size && i < 32; i++) {
        int segment = addrs[i] / cl_size;
        bool found = false;
        for (int j = 0; j < num_segments; j++) {
            if (segments[j] == segment) { found = true; break; }
        }
        if (!found && num_segments < 32) {
            segments[num_segments++] = segment;
        }
    }

    return num_segments;
}

/* ===================================================================
 * L9: Statistics
 * =================================================================== */

void gpu_mem_print_stats(const GPUMemorySubsystem *m) {
    if (!m) { printf("GPUMemorySubsystem: NULL\n"); return; }

    printf("--- GPU Memory Subsystem Stats ---\n");
    printf("Global memory:  %lu MB\n",
           (unsigned long)(m->global_mem.total_size / (1024 * 1024)));
    printf("Channels:       %d\n", m->global_mem.num_channels);
    printf("\n");

    printf("L1 Cache:       %d KB\n", m->l1_cache.total_size / 1024);
    printf("  Hits:         %lu\n", (unsigned long)m->l1_cache.hits);
    printf("  Misses:       %lu\n", (unsigned long)m->l1_cache.misses);
    printf("  Hit rate:     %.2f%%\n", cache_hit_rate(&m->l1_cache) * 100.0);
    printf("  Evictions:    %lu\n", (unsigned long)m->l1_cache.evictions);
    printf("\n");

    printf("L2 Cache:       %d KB\n", m->l2_cache.total_size / 1024);
    printf("  Hits:         %lu\n", (unsigned long)m->l2_cache.hits);
    printf("  Misses:       %lu\n", (unsigned long)m->l2_cache.misses);
    printf("  Hit rate:     %.2f%%\n", cache_hit_rate(&m->l2_cache) * 100.0);
    printf("\n");

    printf("TLB:            %d entries\n", m->tlb.num_entries);
    printf("  Hit rate:     %.2f%%\n", tlb_hit_rate(&m->tlb) * 100.0);
    printf("\n");

    printf("Shared memory:  %d KB\n", m->shared_mem.total_size / 1024);
    printf("  Banks:        %d\n", m->shared_mem.num_banks);
    printf("  Conflicts:    %s\n", smem_check_bank_conflict(&m->shared_mem) ? "yes" : "no");
    printf("\n");

    printf("Consistency:    %d\n", m->consistency);
    printf("Total reads:    %lu\n", (unsigned long)m->total_reads);
    printf("Total writes:   %lu\n", (unsigned long)m->total_writes);
    printf("Coalesced:      %lu\n", (unsigned long)m->coalesced_accesses);
    printf("Uncoalesced:    %lu\n", (unsigned long)m->uncoalesced_accesses);
    printf("DRAM cycles:    %lu\n", (unsigned long)m->dram_cycles);
}
