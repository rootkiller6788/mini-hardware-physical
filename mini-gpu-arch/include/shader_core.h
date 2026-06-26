#ifndef GPU_SHADER_CORE_H
#define GPU_SHADER_CORE_H

/**
 * mini-gpu-arch: Shader Core / Streaming Multiprocessor (SM)
 *
 * @L1_Definitions: SM architecture, register file, shared memory, L1 cache
 * @L2_CoreConcepts: Thread block scheduling, occupancy, latency hiding
 * @L3_EngStructures: Pipeline stages (fetch/decode/issue/execute/writeback)
 * @L4_Standards: Little's Law for pipeline occupancy, Roofline model
 * @L5_Algorithms: Register allocation, shared memory bank conflict resolution
 * @L6_Canonical: CUDA occupancy calculator, SM pipeline throughput model
 *
 * Course mapping:
 *   CMU 15-418: Streaming Multiprocessor microarchitecture
 *   Stanford CS149: GPU SM programming model
 *   ETH 263-3501: GPU parallel programming
 */

#include <stdint.h>
#include <stdbool.h>
#include "warp.h"
#include "memory_gpu.h"

/* ================================================================
 * L1: Core Definitions — SM Architecture
 * ================================================================ */

#define SM_MAX_WARPS         64
#define SM_MAX_THREAD_BLOCKS 32
#define SM_MAX_THREADS_PER_BLOCK 1024
#define SM_MAX_REGISTERS     65536   /* 64K registers per SM */
#define SM_MAX_SHARED_MEM    49152   /* 48KB default shared mem */
#define SM_REGISTERS_PER_THREAD_MAX 255
#define SM_PIPELINE_STAGES   6

/** SM compute capability (approximates GPU generation) */
typedef enum {
    SM_CC_70 = 70,   /* Volta: tensor cores, independent thread scheduling */
    SM_CC_75 = 75,   /* Turing: RT cores, INT4/INT8 */
    SM_CC_80 = 80,   /* Ampere: 3rd-gen tensor, sparse, MIG */
    SM_CC_90 = 90    /* Hopper: 4th-gen tensor, DPX, TMA */
} SMComputeCap;

/** Pipeline stage */
typedef enum {
    STAGE_FETCH    = 0,
    STAGE_DECODE   = 1,
    STAGE_ISSUE    = 2,
    STAGE_EXECUTE  = 3,
    STAGE_MEMORY   = 4,
    STAGE_WRITEBACK = 5
} PipelineStage;

/** Execution pipeline within SM */
typedef struct {
    PipelineStage stage;
    uint32_t      warp_id;
    uint32_t      pc;
    bool          valid;
    uint32_t      active_mask;
    uint8_t       delay_left;       /* remaining pipeline cycles */
} PipelineSlot;

/** Thread block allocated to SM */
typedef struct {
    int    block_id;
    int    block_dim_x, block_dim_y, block_dim_z;
    int    num_warps;              /* warps in this block */
    int    warp_base;              /* first warp index in SM */
    int    shared_mem_alloc;       /* bytes of shared mem allocated */
    int    registers_per_thread;   /* regs per thread */
    bool   active;
} SMBlock;

/** Full Streaming Multiprocessor model */
typedef struct {
    int    sm_id;
    SMComputeCap compute_cap;

    /* Warp slots */
    int    num_warps;
    Warp   warps[SM_MAX_WARPS];
    int    num_active_warps;

    /* Thread blocks */
    int    num_blocks;
    SMBlock blocks[SM_MAX_THREAD_BLOCKS];

    /* Resources */
    int    total_registers;
    int    used_registers;       /* currently allocated */
    int    total_shared_mem;
    int    used_shared_mem;

    /* Pipeline */
    PipelineSlot pipeline[SM_PIPELINE_STAGES];
    SchedPolicy sched_policy;

    /* L1 cache (co-located with shared memory on many GPUs) */
    int    l1_cache_size;        /* bytes */
    int    shared_mem_carveout;  /* bytes carved out for shared mem from L1 */

    /* Memory subsystem reference */
    GPUMemorySubsystem *mem_subsys;

    /* Statistics */
    uint64_t cycles;
    uint64_t instr_issued;
    uint64_t instr_retired;
    uint64_t stall_memory_cycles;
    uint64_t stall_sync_cycles;
} ShaderCore;

/* ================================================================
 * L2: Occupancy Model
 * ================================================================ */

/** Occupancy configuration (matches CUDA Occupancy Calculator) */
typedef struct {
    int   threads_per_block;
    int   registers_per_thread;
    int   shared_mem_per_block_bytes;
    int   max_threads_per_sm;
    int   max_registers_per_sm;
    int   max_shared_mem_per_sm;
    int   max_blocks_per_sm;
    int   warp_size;
} OccupancyConfig;

/** Occupancy result */
typedef struct {
    int    active_warps;
    int    active_threads;
    int    warps_per_block;
    int    blocks_per_sm;
    double occupancy;            /* active_warps / max_warps_per_sm */
    bool   reg_limited;
    bool   smem_limited;
    bool   block_limited;
} OccupancyResult;

/* ================================================================
 * L3: SM Resource Allocation
 * ================================================================ */

/** Register allocation result */
typedef struct {
    int    registers_assigned;
    int    registers_remaining;
    bool   success;
    int    spill_registers;       /* regs that must spill to memory */
} RegAllocResult;

/** Shared memory bank conflict descriptor */
typedef enum {
    BANK_NO_CONFLICT,
    BANK_2WAY,
    BANK_4WAY,
    BANK_8WAY,
    BANK_FULL       /* all threads hit same bank */
} BankConflictLevel;

/** Shared memory access analyzer */
typedef struct {
    int    num_banks;           /* typically 32 */
    int    bank_width_bytes;    /* typically 4 */
    int    access_addresses[WARP_SIZE];
    int    bank_ids[WARP_SIZE];
    int    bank_hits[64];       /* count per bank */
    BankConflictLevel conflict;
    int    max_way_conflict;    /* max threads hitting one bank */
} BankAnalyzer;

/* ================================================================
 * L4: Performance Models
 * ================================================================ */

/** Little's Law pipeline model */
typedef struct {
    double   arrival_rate;       /* λ: warps per cycle */
    double   service_time;       /* W: pipeline depth cycles */
    double   occupancy_law;      /* L = λ * W */
    double   throughput;         /* 1/W warps per cycle */
} LittlesLawModel;

/** Roofline model point */
typedef struct {
    double   operational_intensity;  /* FLOP/byte */
    double   peak_compute;           /* GFLOPS */
    double   peak_bandwidth;         /* GB/s */
    double   achievable_perf;        /* min(peak_compute, OI * peak_bandwidth) */
    bool     compute_bound;
} RooflinePoint;

/* ================================================================
 * API Declarations
 * ================================================================ */

/* --- L1: SM lifecycle --- */
ShaderCore* sm_create(int sm_id, SMComputeCap cc);
void        sm_destroy(ShaderCore *sm);
void        sm_reset(ShaderCore *sm);

/* --- L2: Thread Block management --- */
int  sm_allocate_block(ShaderCore *sm, int block_dim_x, int block_dim_y,
                       int block_dim_z, int shmem_bytes, int regs_per_thread);
void sm_deallocate_block(ShaderCore *sm, int block_id);
int  sm_free_warps(const ShaderCore *sm);
int  sm_free_register_count(const ShaderCore *sm);

/* --- L3: Occupancy calculation --- */
OccupancyResult sm_calc_occupancy(const OccupancyConfig *cfg);
void            sm_print_occupancy(const OccupancyResult *r);
OccupancyConfig sm_default_config(SMComputeCap cc);

/* --- L4: Register allocation --- */
RegAllocResult   sm_allocate_registers(ShaderCore *sm, int requested_per_thread, int num_threads);
void             sm_free_registers(ShaderCore *sm, int regs_per_thread, int num_threads);

/* --- L5: Shared memory bank analysis --- */
BankAnalyzer bank_analyze(const int *addresses, int num_threads, int num_banks, int bank_width);
const char*  bank_conflict_str(BankConflictLevel lvl);

/* --- L6: Pipeline execution --- */
void sm_cycle(ShaderCore *sm);
void sm_issue_instructions(ShaderCore *sm);
void sm_execute_stage(ShaderCore *sm);

/* --- L7: Pipeline modeling --- */
LittlesLawModel littles_law_sm_model(double arrival_rate, int pipeline_stages);
RooflinePoint   roofline_evaluate(double op_intensity, double peak_compute, double peak_bw);

/* --- L8: Statistics --- */
void   sm_print_stats(const ShaderCore *sm);
double sm_ipc(const ShaderCore *sm);           /* instructions per cycle */
double sm_occupancy_ratio(const ShaderCore *sm);
double sm_stall_ratio(const ShaderCore *sm);

#endif /* GPU_SHADER_CORE_H */
