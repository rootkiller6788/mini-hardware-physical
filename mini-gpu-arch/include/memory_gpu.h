#ifndef GPU_MEMORY_GPU_H
#define GPU_MEMORY_GPU_H

/**
 * mini-gpu-arch: GPU Memory Hierarchy
 *
 * @L1_Definitions: Global, shared, local, constant, texture memory; L1/L2 cache
 * @L2_CoreConcepts: Coalesced access, bank conflicts, memory consistency
 * @L3_EngStructures: Multi-level cache, address translation, coalescer
 * @L4_Standards: Memory consistency models (relaxed, acquire-release, SC)
 * @L5_Algorithms: Address coalescing, padding for bank conflict avoidance
 * @L6_Canonical: GPU memory hierarchy simulator, DRAM scheduler
 *
 * Course mapping:
 *   CMU 18-447: Computer Architecture — memory systems
 *   Stanford CS149: GPU memory model, coalescing
 *   UMich EECS 570: Parallel computer architecture
 */

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * L1: Core Definitions — GPU Memory Spaces
 * ================================================================ */

/** GPU memory address space */
typedef enum {
    MEM_GLOBAL    = 0,   /* HBM/DRAM, all threads */
    MEM_SHARED    = 1,   /* on-chip SRAM, per-block */
    MEM_LOCAL     = 2,   /* thread-private (spills to global) */
    MEM_CONSTANT  = 3,   /* read-only cached */
    MEM_TEXTURE   = 4,   /* read-only, spatially cached */
    MEM_REGISTER  = 5    /* fastest, per-thread */
} GPUMemSpace;

/** Cache level */
typedef enum {
    CACHE_L1 = 0,
    CACHE_L2 = 1
} CacheLevel;

/** Cache line descriptor */
typedef struct {
    uint64_t tag;
    bool     valid;
    bool     dirty;
    uint32_t last_access_cycle;
    uint8_t  data[128];    /* 128-byte cacheline (typical GPU L1/L2) */
} CacheLine;

/** Cache set (associativity) */
#define CACHE_ASSOC 4
#define CACHE_NUM_SETS 64

typedef struct {
    CacheLine lines[CACHE_ASSOC];
    uint32_t  lru_counter[CACHE_ASSOC];  /* for replacement */
} CacheSet;

/** GPU L1/L2 cache */
typedef struct {
    CacheLevel level;
    int        total_size;         /* bytes */
    int        line_size;          /* bytes per cacheline */
    int        num_sets;
    int        assoc;
    CacheSet  *sets;

    /* Statistics */
    uint64_t   accesses;
    uint64_t   hits;
    uint64_t   misses;
    uint64_t   evictions;
    uint64_t   writebacks;
    uint32_t   lru_epoch;          /* global LRU counter */
} GPUCache;

/** Shared memory bank structure */
#define SMEM_NUM_BANKS 32
#define SMEM_BANK_WIDTH 4      /* bytes */

typedef struct {
    int   num_banks;
    int   bank_width;
    int   total_size;          /* bytes */
    float data[49152 / 4];     /* 48KB data, float-typed for convenience */
    int   access_count[SMEM_NUM_BANKS];  /* per-bank access counter */
} SharedMemory;

/** Global memory (HBM) */
typedef struct {
    uint64_t total_size;       /* bytes */
    uint64_t allocated;
    float   *data;             /* backing store */
    int      num_channels;     /* memory channels */
} GlobalMemory;

/* ================================================================
 * L2: Address Coalescing
 * ================================================================ */

/** Coalescing analyzer for warp-level global memory access */
typedef struct {
    uint32_t  addresses[32];   /* one address per warp thread */
    int       num_threads;
    int       cacheline_size;
    int       num_transactions;     /* L2 transactions needed */
    int       min_transactions;     /* ideal minimum */
    double    coalescing_efficiency; /* [0,1] */
    bool      is_coalesced;
    int       misaligned_count;    /* number of misaligned accesses */
} CoalescingAnalysis;

/** Shared memory bank conflict analyzer */
typedef struct {
    uint32_t  addresses[32];   /* per-thread shared mem addresses */
    int       num_threads;
    int       num_banks;
    int       bank_id[32];     /* which bank each thread hits */
    int       max_conflict;    /* max threads per bank */
    int       conflict_cycles; /* serialized cycles for worst bank */
    int       ideal_cycles;    /* 1 if no conflicts */
    double    efficiency;      /* ideal_cycles / conflict_cycles */
} BankConflictAnalysis;

/* ================================================================
 * L3: Address Translation (TLB)
 * ================================================================ */

#define TLB_ENTRIES 64

typedef struct {
    uint64_t  virtual_page;
    uint64_t  physical_page;
    bool      valid;
    uint32_t  last_used;
    GPUMemSpace space;
} TLBEntry;

typedef struct {
    TLBEntry entries[TLB_ENTRIES];
    int      num_entries;
    uint32_t age_counter;
    uint64_t hits;
    uint64_t misses;
} GPUTLB;

/* ================================================================
 * L4: Memory Consistency
 * ================================================================ */

/** GPU memory consistency model */
typedef enum {
    CONS_RELAXED,           /* no ordering guarantees */
    CONS_ACQUIRE_RELEASE,   /* acquire/release semantics */
    CONS_SEQUENTIAL         /* total store order / SC */
} MemConsistencyModel;

/** Fence type */
typedef enum {
    FENCE_CTA,     /* __threadfence_block() */
    FENCE_GPU,     /* __threadfence() */
    FENCE_SYSTEM   /* __threadfence_system() */
} FenceScope;

/** Memory operation with ordering */
typedef struct {
    uint64_t address;
    uint8_t  data[16];
    int      size;
    bool     is_write;
    uint32_t thread_id;
    uint32_t warp_id;
    uint32_t issue_cycle;
    uint32_t complete_cycle;
    MemConsistencyModel order;
    FenceScope fence;
} MemOperation;

/** Memory fence barrier */
typedef struct {
    FenceScope     scope;
    uint32_t       issued_cycle;
    uint32_t       completed_cycle;
    int            pending_ops;  /* ops waiting behind fence */
} MemFence;

/* ================================================================
 * L5: Full Memory Subsystem
 * ================================================================ */

#define MEM_MAX_PENDING 256

typedef struct {
    /* Memory hierarchy */
    GlobalMemory  global_mem;
    SharedMemory  shared_mem;
    GPUCache      l1_cache;
    GPUCache      l2_cache;
    GPUTLB        tlb;
    MemConsistencyModel consistency;

    /* Pending operations */
    MemOperation   pending[MEM_MAX_PENDING];
    int            pending_head;
    int            pending_tail;

    /* Fence tracking */
    MemFence       active_fence;
    bool           fence_active;

    /* Statistics */
    uint64_t       total_reads;
    uint64_t       total_writes;
    uint64_t       coalesced_accesses;
    uint64_t       uncoalesced_accesses;
    uint64_t       dram_cycles;
} GPUMemorySubsystem;

/* ================================================================
 * API Declarations
 * ================================================================ */

/* --- L1: Memory lifecycle --- */
GPUMemorySubsystem* gpu_mem_create(uint64_t global_size, int l1_sz, int l2_sz);
void                gpu_mem_destroy(GPUMemorySubsystem *m);
void                gpu_mem_reset(GPUMemorySubsystem *m);

/* --- L2: Memory access --- */
bool  gpu_mem_read(GPUMemorySubsystem *m, uint64_t addr, float *data, int size,
                   GPUMemSpace space, uint32_t thread_id, uint32_t warp_id);
bool  gpu_mem_write(GPUMemorySubsystem *m, uint64_t addr, const float *data, int size,
                    GPUMemSpace space, uint32_t thread_id, uint32_t warp_id);

/* --- L3: Cache operations --- */
bool    cache_access(GPUCache *c, uint64_t addr, bool is_write);
void    cache_flush(GPUCache *c);
void    cache_invalidate(GPUCache *c);
double  cache_hit_rate(const GPUCache *c);
uint64_t cache_tag(uint64_t addr, int line_size, int num_sets);
uint32_t cache_set_index(uint64_t addr, int line_size, int num_sets);

/* --- L4: Coalescing analysis --- */
CoalescingAnalysis coalesce_analyze(const uint32_t *addresses, int num_threads,
                                     int cacheline_size);
BankConflictAnalysis shared_bank_analyze(const uint32_t *addresses, int num_threads,
                                          int num_banks, int bank_width);
double coalesced_bandwidth_util(const CoalescingAnalysis *ca, double peak_bw);

/* --- L5: Memory consistency --- */
void  mem_fence_issue(GPUMemorySubsystem *m, FenceScope scope);
bool  mem_fence_complete(GPUMemorySubsystem *m, uint32_t current_cycle);
bool  mem_order_check(const GPUMemorySubsystem *m, const MemOperation *a, const MemOperation *b);

/* --- L6: TLB --- */
bool    tlb_lookup(GPUTLB *tlb, uint64_t vpn, uint64_t *ppn, GPUMemSpace *space);
void    tlb_insert(GPUTLB *tlb, uint64_t vpn, uint64_t ppn, GPUMemSpace space);
double  tlb_hit_rate(const GPUTLB *tlb);

/* --- L7: Shared memory --- */
void  smem_store(SharedMemory *sm, int bank, int offset, float val);
float smem_load(const SharedMemory *sm, int bank, int offset);
bool  smem_check_bank_conflict(const SharedMemory *sm);

/* --- L8: Warp-level access --- */
int   warp_coalesced_transactions(const uint32_t *addrs, int warp_size, int cl_size);

/* --- L9: Statistics --- */
void  gpu_mem_print_stats(const GPUMemorySubsystem *m);

#endif /* GPU_MEMORY_GPU_H */
