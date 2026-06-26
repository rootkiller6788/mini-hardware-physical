/**
 * mini-gpu-arch: Shader Core / Streaming Multiprocessor Implementation
 *
 * Knowledge layers:
 *   L1: SM architecture (warps, registers, shared memory, L1, pipeline stages)
 *   L2: Thread block scheduling, occupancy model
 *   L3: Pipeline cycle simulation (fetch→decode→issue→execute→memory→writeback)
 *   L4: Little's Law for pipeline occupancy, Roofline model
 *   L5: Register allocation with spill awareness, shared memory bank conflict
 *   L6: CUDA occupancy calculator implementation (matching CUDA toolkit)
 *   L7: SM throughput/IPC calculator
 *   L8: Compute capability-based resource limits
 *
 * References:
 *   - NVIDIA CUDA C Programming Guide §5.4 (Compute Capability)
 *   - NVIDIA Tuning Guide: Occupancy
 *   - Little, J.D.C. "A Proof for the Queuing Formula L=λW" (1961)
 *   - Williams et al. "Roofline: An Insightful Visual Performance Model" (2009)
 */

#include "shader_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ===================================================================
 * Helper: Compute Capability → resource limits
 * =================================================================== */

/** Returns SM limits for given compute capability */
static void sm_limits(SMComputeCap cc, int *max_warps, int *max_regs,
                       int *max_shmem, int *max_blocks) {
    switch (cc) {
        case SM_CC_70:
            *max_warps = 64;  *max_regs = 65536;
            *max_shmem = 49152; *max_blocks = 32;
            break;
        case SM_CC_75:
            *max_warps = 64;  *max_regs = 65536;
            *max_shmem = 65536; *max_blocks = 32;
            break;
        case SM_CC_80:
            *max_warps = 64;  *max_regs = 65536;
            *max_shmem = 102400; *max_blocks = 32;
            break;
        case SM_CC_90:
            *max_warps = 64;  *max_regs = 65536;
            *max_shmem = 131072; *max_blocks = 32;
            break;
        default:
            *max_warps = 64;  *max_regs = 65536;
            *max_shmem = 49152; *max_blocks = 32;
            break;
    }
}

/* ===================================================================
 * L1: SM Lifecycle
 * =================================================================== */

ShaderCore* sm_create(int sm_id, SMComputeCap cc) {
    ShaderCore *sm = (ShaderCore*)calloc(1, sizeof(ShaderCore));
    if (!sm) return NULL;

    sm->sm_id = sm_id;
    sm->compute_cap = cc;

    int max_w, max_r, max_s, max_b;
    sm_limits(cc, &max_w, &max_r, &max_s, &max_b);

    sm->total_registers = max_r;
    sm->total_shared_mem = max_s;
    sm->sched_policy = SCHED_GTO;

    /* Default L1/smem split: 50/50 for cc<80, configurable for cc>=80 */
    if (cc >= SM_CC_80) {
        sm->l1_cache_size = 32768;
        sm->shared_mem_carveout = 131072; /* Ampere: up to 100KB smem */
    } else {
        sm->l1_cache_size = 32768;
        sm->shared_mem_carveout = 49152;
    }

    /* Initialize warps */
    sm->num_warps = max_w;
    for (int i = 0; i < sm->num_warps; i++) {
        Warp *w = warp_create(i);
        if (!w) {
            /* Cleanup previous warps */
            for (int j = 0; j < i; j++) warp_destroy(&sm->warps[j]);
            free(sm);
            return NULL;
        }
        memcpy(&sm->warps[i], w, sizeof(Warp));
        /* Don't leak the temporary Warp copy fields; the Warp struct is
         * embedded and warp_create allocates memory. We need to manage this. */
        free(w);  /* discard the allocated warp, use embedded copy instead */
    }

    sm->mem_subsys = NULL;

    return sm;
}

void sm_destroy(ShaderCore *sm) {
    if (!sm) return;
    /* Warps are embedded, no additional free needed since we stored them inline */
    free(sm);
}

void sm_reset(ShaderCore *sm) {
    if (!sm) return;
    for (int i = 0; i < sm->num_warps; i++) {
        warp_reset(&sm->warps[i]);
    }
    sm->num_active_warps = 0;
    sm->num_blocks = 0;
    sm->used_registers = 0;
    sm->used_shared_mem = 0;
    sm->cycles = 0;
    sm->instr_issued = 0;
    sm->instr_retired = 0;
    sm->stall_memory_cycles = 0;
    sm->stall_sync_cycles = 0;

    memset(sm->pipeline, 0, sizeof(sm->pipeline));
    memset(sm->blocks, 0, sizeof(sm->blocks));
}

/* ===================================================================
 * L2: Thread Block Management
 * =================================================================== */

/**
 * Allocate a thread block to this SM.
 *
 * Checks resource availability:
 *   1. Free warp slots
 *   2. Register file capacity
 *   3. Shared memory capacity
 *
 * Returns block_id or -1 on failure.
 *
 * Complexity: O(blocks_on_sm) for finding a free block slot
 */
int sm_allocate_block(ShaderCore *sm, int block_dim_x, int block_dim_y,
                      int block_dim_z, int shmem_bytes, int regs_per_thread) {
    if (!sm) return -1;

    int total_threads = block_dim_x * block_dim_y * block_dim_z;
    if (total_threads <= 0 || total_threads > SM_MAX_THREADS_PER_BLOCK) return -1;

    int warps_needed = (total_threads + WARP_SIZE - 1) / WARP_SIZE;
    int regs_needed = warps_needed * WARP_SIZE * regs_per_thread;

    /* Check resources */
    int free_warps = sm->num_warps - sm->num_active_warps;
    if (warps_needed > free_warps) return -1;

    if (sm->used_registers + regs_needed > sm->total_registers) return -1;
    if (sm->used_shared_mem + shmem_bytes > sm->total_shared_mem) return -1;

    /* Find free block slot */
    int block_id = -1;
    for (int i = 0; i < sm->num_warps; i++) {
        if (!sm->blocks[i].active) {
            block_id = i;
            break;
        }
    }
    if (block_id == -1) return -1;

    /* Allocate */
    SMBlock *b = &sm->blocks[block_id];
    b->block_id = block_id;
    b->block_dim_x = block_dim_x;
    b->block_dim_y = block_dim_y;
    b->block_dim_z = block_dim_z;
    b->num_warps = warps_needed;
    b->warp_base = sm->num_active_warps;  /* allocate from consecutive warp pool */
    b->shared_mem_alloc = shmem_bytes;
    b->registers_per_thread = regs_per_thread;
    b->active = true;

    sm->num_active_warps += warps_needed;
    sm->used_registers += regs_needed;
    sm->used_shared_mem += shmem_bytes;
    sm->num_blocks++;

    /* Initialize warps for this block */
    for (int w = 0; w < warps_needed; w++) {
        int warp_id = b->warp_base + w;
        warp_reset(&sm->warps[warp_id]);
        sm->warps[warp_id].warp_id = warp_id;
    }

    return block_id;
}

void sm_deallocate_block(ShaderCore *sm, int block_id) {
    if (!sm || block_id < 0 || block_id >= sm->num_warps) return;

    SMBlock *b = &sm->blocks[block_id];
    if (!b->active) return;

    sm->num_active_warps -= b->num_warps;
    sm->used_registers -= b->num_warps * WARP_SIZE * b->registers_per_thread;
    sm->used_shared_mem -= b->shared_mem_alloc;
    sm->num_blocks--;

    b->active = false;
}

int sm_free_warps(const ShaderCore *sm) {
    if (!sm) return 0;
    return sm->num_warps - sm->num_active_warps;
}

int sm_free_register_count(const ShaderCore *sm) {
    if (!sm) return 0;
    return sm->total_registers - sm->used_registers;
}

/* ===================================================================
 * L3: Occupancy Calculation
 * =================================================================== */

/**
 * CUDA Occupancy Calculator.
 *
 * Determines how many warps/blocks can be resident on an SM given
 * resource constraints (registers, shared memory, max blocks).
 *
 * The occupancy is the ratio of active warps to maximum warps.
 *
 * Reference: CUDA Occupancy Calculator (CUDA Toolkit)
 *
 * Constraint checking order (matches CUDA):
 *   1. Per-block warp limit (regs per thread)
 *   2. Per-SM warp limit (total regs)
 *   3. Per-SM shared memory limit
 *   4. Per-SM block limit
 *
 * The limiting factor determines occupancy.
 */
OccupancyResult sm_calc_occupancy(const OccupancyConfig *cfg) {
    OccupancyResult r = {0};
    if (!cfg || cfg->warp_size <= 0) return r;

    r.warps_per_block = (cfg->threads_per_block + cfg->warp_size - 1) / cfg->warp_size;

    /* Constraint 1: Registers per block */
    int regs_per_block = cfg->registers_per_thread * cfg->threads_per_block;
    /* Round up to allocation granularity (256 registers) */
    int alloc_gran = 256;
    regs_per_block = ((regs_per_block + alloc_gran - 1) / alloc_gran) * alloc_gran;

    int blocks_by_regs = (regs_per_block > 0)
        ? cfg->max_registers_per_sm / regs_per_block : cfg->max_blocks_per_sm;

    /* Constraint 2: Shared memory per block */
    int smem_per_block = cfg->shared_mem_per_block_bytes;
    int blocks_by_smem = (smem_per_block > 0)
        ? cfg->max_shared_mem_per_sm / smem_per_block : cfg->max_blocks_per_sm;

    /* Constraint 3: Threads per SM */
    int threads_per_block_val = cfg->threads_per_block;
    int blocks_by_threads = (threads_per_block_val > 0)
        ? cfg->max_threads_per_sm / threads_per_block_val : 0;

    /* Constraint 4: Max blocks per SM */
    int blocks_by_limit = cfg->max_blocks_per_sm;

    /* Minimum of all constraints */
    int blocks_per_sm = blocks_by_regs;
    if (blocks_by_smem < blocks_per_sm) blocks_per_sm = blocks_by_smem;
    if (blocks_by_threads < blocks_per_sm) blocks_per_sm = blocks_by_threads;
    if (blocks_by_limit < blocks_per_sm) blocks_per_sm = blocks_by_limit;
    if (blocks_per_sm < 1) blocks_per_sm = 1;

    /* Check which constraint limits */
    r.reg_limited   = (blocks_per_sm == blocks_by_regs);
    r.smem_limited  = (blocks_per_sm == blocks_by_smem && !r.reg_limited);
    r.block_limited = (blocks_per_sm == blocks_by_limit && !r.reg_limited && !r.smem_limited);

    r.blocks_per_sm = blocks_per_sm;
    r.active_warps = blocks_per_sm * r.warps_per_block;
    r.active_threads = blocks_per_sm * cfg->threads_per_block;

    int max_warps_per_sm = cfg->max_threads_per_sm / cfg->warp_size;
    r.occupancy = (double)r.active_warps / (double)max_warps_per_sm;

    return r;
}

OccupancyConfig sm_default_config(SMComputeCap cc) {
    OccupancyConfig cfg = {0};
    cfg.warp_size = WARP_SIZE;
    cfg.max_blocks_per_sm = 32;

    switch (cc) {
        case SM_CC_70:
            cfg.max_threads_per_sm = 2048;
            cfg.max_registers_per_sm = 65536;
            cfg.max_shared_mem_per_sm = 49152;
            break;
        case SM_CC_75:
            cfg.max_threads_per_sm = 2048;
            cfg.max_registers_per_sm = 65536;
            cfg.max_shared_mem_per_sm = 65536;
            break;
        case SM_CC_80:
            cfg.max_threads_per_sm = 2048;
            cfg.max_registers_per_sm = 65536;
            cfg.max_shared_mem_per_sm = 102400;
            break;
        case SM_CC_90:
            cfg.max_threads_per_sm = 2048;
            cfg.max_registers_per_sm = 65536;
            cfg.max_shared_mem_per_sm = 131072;
            break;
        default:
            cfg.max_threads_per_sm = 2048;
            cfg.max_registers_per_sm = 65536;
            cfg.max_shared_mem_per_sm = 49152;
            break;
    }
    return cfg;
}

void sm_print_occupancy(const OccupancyResult *r) {
    if (!r) { printf("Occupancy: NULL\n"); return; }
    printf("--- SM Occupancy ---\n");
    printf("Active warps:     %d\n", r->active_warps);
    printf("Active threads:   %d\n", r->active_threads);
    printf("Warps per block:  %d\n", r->warps_per_block);
    printf("Blocks per SM:    %d\n", r->blocks_per_sm);
    printf("Occupancy:        %.1f%%\n", r->occupancy * 100.0);
    printf("Limited by:       %s%s%s\n",
           r->reg_limited ? "REGISTERS " : "",
           r->smem_limited ? "SHARED_MEM " : "",
           r->block_limited ? "MAX_BLOCKS " : "");
}

/* ===================================================================
 * L4: Register Allocation
 * =================================================================== */

RegAllocResult sm_allocate_registers(ShaderCore *sm, int requested_per_thread,
                                      int num_threads) {
    RegAllocResult r = {0};
    if (!sm || requested_per_thread <= 0 || num_threads <= 0) {
        r.success = false;
        return r;
    }

    /* Clamp to per-thread max */
    if (requested_per_thread > SM_REGISTERS_PER_THREAD_MAX) {
        requested_per_thread = SM_REGISTERS_PER_THREAD_MAX;
    }

    /* Round up to 256-register allocation granularity (NVIDIA convention) */
    int regs_rounded = ((num_threads * requested_per_thread + 255) / 256) * 256;
    int remaining = sm->total_registers - sm->used_registers;

    if (regs_rounded <= remaining) {
        r.success = true;
        r.registers_assigned = requested_per_thread;
        r.registers_remaining = remaining - regs_rounded;
        r.spill_registers = 0;
        sm->used_registers += regs_rounded;
    } else {
        /* Can't allocate requested registers — how many can we allocate? */
        int max_possible = remaining / num_threads;
        if (max_possible > 0) {
            r.success = true;
            r.registers_assigned = max_possible;
            r.spill_registers = requested_per_thread - max_possible;
            int adjusted = ((num_threads * max_possible + 255) / 256) * 256;
            r.registers_remaining = remaining - adjusted;
            sm->used_registers += adjusted;
        } else {
            r.success = false;
            r.spill_registers = requested_per_thread;
        }
    }

    return r;
}

void sm_free_registers(ShaderCore *sm, int regs_per_thread, int num_threads) {
    if (!sm || regs_per_thread <= 0 || num_threads <= 0) return;
    int freed = ((num_threads * regs_per_thread + 255) / 256) * 256;
    sm->used_registers = (sm->used_registers > freed) ? sm->used_registers - freed : 0;
}

/* ===================================================================
 * L5: Shared Memory Bank Conflict Analysis
 * =================================================================== */

/**
 * Analyze shared memory bank conflicts.
 *
 * A bank conflict occurs when multiple threads in the same warp access
 * different addresses within the same bank in the same cycle.
 * The access is serialized (n-way conflict = n serialized accesses).
 *
 * num_banks: typically 32 (one per warp thread)
 * bank_width: typically 4 bytes (32-bit word)
 *
 * bank_id = (byte_address / bank_width) % num_banks
 *
 * Complexity: O(num_threads) for counting, O(num_banks) for max.
 */
BankAnalyzer bank_analyze(const int *addresses, int num_threads,
                          int num_banks, int bank_width) {
    BankAnalyzer ba = {0};
    ba.num_banks = num_banks;
    ba.bank_width_bytes = bank_width;

    if (!addresses || num_threads <= 0 || num_banks <= 0) {
        ba.conflict = BANK_NO_CONFLICT;
        return ba;
    }

    memset(ba.bank_hits, 0, sizeof(ba.bank_hits));

    int max_hits = 0;
    for (int i = 0; i < num_threads && i < WARP_SIZE; i++) {
        int bank = (addresses[i] / bank_width) % num_banks;
        ba.bank_ids[i] = bank;
        ba.access_addresses[i] = addresses[i];

        if (bank >= 0 && bank < 64) {
            ba.bank_hits[bank]++;
            if (ba.bank_hits[bank] > max_hits) {
                max_hits = ba.bank_hits[bank];
            }
        }
    }

    ba.max_way_conflict = max_hits;

    if (max_hits <= 1) {
        ba.conflict = BANK_NO_CONFLICT;
    } else if (max_hits <= 2) {
        ba.conflict = BANK_2WAY;
    } else if (max_hits <= 4) {
        ba.conflict = BANK_4WAY;
    } else if (max_hits <= 8) {
        ba.conflict = BANK_8WAY;
    } else {
        ba.conflict = BANK_FULL;
    }

    return ba;
}

const char* bank_conflict_str(BankConflictLevel lvl) {
    switch (lvl) {
        case BANK_NO_CONFLICT: return "no conflict";
        case BANK_2WAY:        return "2-way";
        case BANK_4WAY:        return "4-way";
        case BANK_8WAY:        return "8-way";
        case BANK_FULL:        return "full (all threads)";
        default:               return "unknown";
    }
}

/* ===================================================================
 * L6: Pipeline Execution
 * =================================================================== */

/**
 * Execute one cycle of the SM pipeline.
 *
 * Pipeline stages cycle in lockstep:
 *   WRITEBACK ← MEMORY ← EXECUTE ← ISSUE ← DECODE ← FETCH
 *
 * Each stage advances by one slot per cycle.
 * Stalls propagate backward when a stage cannot accept new work.
 *
 * This models a single-issue in-order pipeline per warp.
 * Real SM pipelines issue multiple warps per cycle from different sub-partitions.
 */
void sm_cycle(ShaderCore *sm) {
    if (!sm) return;

    sm->cycles++;

    /* Pipeline progression (reverse order for dependency) */
    /* Stage 5: Writeback */
    if (sm->pipeline[STAGE_WRITEBACK].valid) {
        sm->instr_retired++;
        sm->pipeline[STAGE_WRITEBACK].valid = false;
    }

    /* Stage 4: Memory */
    if (sm->pipeline[STAGE_MEMORY].valid) {
        sm->pipeline[STAGE_WRITEBACK] = sm->pipeline[STAGE_MEMORY];
        sm->pipeline[STAGE_MEMORY].valid = false;
    }

    /* Stage 3: Execute */
    if (sm->pipeline[STAGE_EXECUTE].valid) {
        sm->pipeline[STAGE_MEMORY] = sm->pipeline[STAGE_EXECUTE];
        sm->pipeline[STAGE_EXECUTE].valid = false;
    }

    /* Stage 2: Issue */
    if (sm->pipeline[STAGE_ISSUE].valid) {
        sm->pipeline[STAGE_EXECUTE] = sm->pipeline[STAGE_ISSUE];
        sm->pipeline[STAGE_ISSUE].valid = false;
    }

    /* Stage 1: Decode */
    if (sm->pipeline[STAGE_DECODE].valid) {
        sm->pipeline[STAGE_ISSUE] = sm->pipeline[STAGE_DECODE];
        sm->pipeline[STAGE_DECODE].valid = false;
        sm->instr_issued++;
    }

    /* Stage 0: Fetch — fill from warp scheduler */
    if (!sm->pipeline[STAGE_FETCH].valid) {
        /* Select a ready warp (simple round-robin) */
        for (int i = 0; i < sm->num_active_warps; i++) {
            int wid = (i) % sm->num_warps;
            if (sm->warps[wid].num_active > 0 && !warp_is_stalled(&sm->warps[wid])) {
                sm->pipeline[STAGE_FETCH].valid = true;
                sm->pipeline[STAGE_FETCH].warp_id = wid;
                sm->pipeline[STAGE_FETCH].pc = sm->warps[wid].unified_pc;
                sm->pipeline[STAGE_FETCH].active_mask = sm->warps[wid].active_mask;
                break;
            }
        }
    }

    if (sm->pipeline[STAGE_FETCH].valid) {
        sm->pipeline[STAGE_DECODE] = sm->pipeline[STAGE_FETCH];
        sm->pipeline[STAGE_FETCH].valid = false;
    }
}

/** Issue instructions from ready warps */
void sm_issue_instructions(ShaderCore *sm) {
    if (!sm) return;
    /* Simple: issue one warp per cycle */
    for (int i = 0; i < sm->num_warps; i++) {
        Warp *w = &sm->warps[i];
        if (w->num_active > 0 && !warp_is_stalled(w)) {
            int issued = warp_issue_instr(w);
            if (issued > 0) {
                sm->instr_issued++;
                break;
            }
        }
    }
}

/** Execute instruction in execution stage */
void sm_execute_stage(ShaderCore *sm) {
    if (!sm) return;

    /* Pipeline already handles this in sm_cycle().
     * Here we just advance the pipeline.
     * In a cycle-accurate model, different instruction types have
     * different latencies (FP32=4, FP64=8, tensor=1, memory=var). */
    (void)sm;
    /* The main cycle logic is in sm_cycle() */
}

/* ===================================================================
 * L7: Performance Models
 * =================================================================== */

/**
 * Little's Law for pipeline occupancy:
 *   L = λ * W
 *
 * where:
 *   L = average number of warps in flight (pipeline occupancy)
 *   λ = arrival rate (warps per cycle)
 *   W = average time in system (pipeline depth, cycles)
 *
 * For an SM with WARP_SIZE=32, pipeline_stages=6:
 *   If we issue 1 warp every 4 cycles (λ=0.25), then L ≈ 0.25*6 = 1.5 warps.
 *   If we have 4 warp schedulers (λ=1.0), then L = 6 (fully utilized).
 *
 * Reference: Little, J.D.C. (1961)
 */
LittlesLawModel littles_law_sm_model(double arrival_rate, int pipeline_stages) {
    LittlesLawModel m;
    m.arrival_rate = (arrival_rate > 0.0) ? arrival_rate : 0.0;
    m.service_time = (pipeline_stages > 0) ? (double)pipeline_stages : 1.0;
    m.occupancy_law = m.arrival_rate * m.service_time;
    m.throughput = (m.service_time > 0.0) ? 1.0 / m.service_time : 0.0;
    return m;
}

/**
 * Roofline Model for GPU kernels.
 *
 * Computes the achievable performance as:
 *   Attainable GFLOPS = min(Peak GFLOPS, Operational Intensity * Peak Bandwidth)
 *
 * If the kernel's arithmetic intensity is above the ridge point
 * (peak_compute / peak_bandwidth), it is compute-bound.
 * Otherwise, it is memory-bandwidth-bound.
 *
 * Reference: Williams, Waterman, Patterson. "Roofline" (CACM 2009)
 */
RooflinePoint roofline_evaluate(double op_intensity, double peak_compute,
                                 double peak_bw) {
    RooflinePoint rp;
    rp.operational_intensity = (op_intensity > 0.0) ? op_intensity : 0.0;
    rp.peak_compute = peak_compute;
    rp.peak_bandwidth = peak_bw;

    double memory_bound_perf = rp.operational_intensity * peak_bw;
    rp.achievable_perf = (memory_bound_perf < peak_compute)
        ? memory_bound_perf : peak_compute;

    rp.compute_bound = (memory_bound_perf >= peak_compute);

    return rp;
}

/* ===================================================================
 * L8: Statistics
 * =================================================================== */

void sm_print_stats(const ShaderCore *sm) {
    if (!sm) { printf("SM: NULL\n"); return; }

    printf("--- SM %d Stats ---\n", sm->sm_id);
    printf("Compute cap:    SM %d.%d\n", sm->compute_cap / 10, sm->compute_cap % 10);
    printf("Cycles:         %lu\n", (unsigned long)sm->cycles);
    printf("IPC:            %.2f\n", sm_ipc(sm));
    printf("Active warps:   %d / %d\n", sm->num_active_warps, sm->num_warps);
    printf("Active blocks:  %d\n", sm->num_blocks);
    printf("Registers:      %d / %d used\n", sm->used_registers, sm->total_registers);
    printf("Shared mem:     %d / %d bytes\n", sm->used_shared_mem, sm->total_shared_mem);
    printf("Occupancy:      %.1f%%\n", sm_occupancy_ratio(sm) * 100.0);
    printf("Stall ratio:    %.1f%%\n", sm_stall_ratio(sm) * 100.0);
    printf("Issued:         %lu\n", (unsigned long)sm->instr_issued);
    printf("Retired:        %lu\n", (unsigned long)sm->instr_retired);
}

double sm_ipc(const ShaderCore *sm) {
    if (!sm || sm->cycles == 0) return 0.0;
    return (double)sm->instr_retired / (double)sm->cycles;
}

double sm_occupancy_ratio(const ShaderCore *sm) {
    if (!sm || sm->num_warps == 0) return 0.0;
    return (double)sm->num_active_warps / (double)sm->num_warps;
}

double sm_stall_ratio(const ShaderCore *sm) {
    if (!sm || sm->cycles == 0) return 0.0;
    uint64_t total_stalls = sm->stall_memory_cycles + sm->stall_sync_cycles;
    return (double)total_stalls / (double)sm->cycles;
}
