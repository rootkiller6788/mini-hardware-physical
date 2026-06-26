#ifndef GPU_THREAD_SCHED_H
#define GPU_THREAD_SCHED_H

/**
 * mini-gpu-arch: Thread & Warp Scheduler
 *
 * @L1_Definitions: Grid, block, warp, thread hierarchy; scheduler policy
 * @L2_CoreConcepts: Latency hiding, occupancy, multithreading
 * @L3_EngStructures: Two-level scheduler (block→SM, warp→exec)
 * @L4_Standards: Makespan lower bounds (list scheduling), work stealing bounds
 * @L5_Algorithms: Greedy-then-oldest (GTO), round-robin, priority scheduling
 * @L6_Canonical: CUDA block scheduler, warp scheduler microarchitecture
 *
 * Course mapping:
 *   CMU 15-418: GPU thread scheduling, occupancy tuning
 *   Georgia Tech CS6290: Multithreading and warp scheduling
 *   Stanford CS149: CUDA execution model, block/warp hierarchy
 */

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * L1: Core Definitions — Thread Hierarchy
 * ================================================================ */

#define MAX_GRIDS      4
#define MAX_BLOCKS     16
#define MAX_THREADS_PER_BLOCK 64
#define MAX_WARPS_PER_BLOCK   2     /* 64/32 */

/** Thread dimension (1D/2D/3D) */
typedef struct {
    int x, y, z;
} Dim3;

/** Single CUDA thread */
typedef struct {
    int      thread_id;
    Dim3     thread_idx;     /* threadIdx.{x,y,z} */
    uint32_t pc;
    bool     active;
    uint32_t cycle_active;
} CUDAThread;

/** Thread block */
typedef struct {
    int      block_id;
    Dim3     block_idx;      /* blockIdx.{x,y,z} */
    Dim3     block_dim;      /* blockDim.{x,y,z} */
    int      num_threads;
    int      num_warps;
    CUDAThread threads[MAX_THREADS_PER_BLOCK];
    bool     scheduled;      /* assigned to an SM */
    int      sm_assigned;    /* SM id */
    uint32_t launch_cycle;
} ThreadBlock;

/** Kernel grid */
typedef struct {
    int      grid_id;
    Dim3     grid_dim;       /* gridDim.{x,y,z} */
    Dim3     block_dim;      /* blockDim.{x,y,z} */
    int      total_blocks;
    int      total_threads;
    ThreadBlock blocks[MAX_BLOCKS];
    int      blocks_launched;
    int      blocks_completed;
    uint32_t launch_cycle;
} KernelGrid;

/* ================================================================
 * L2: GigaThread Engine (Global Scheduler)
 * ================================================================ */

/** Block scheduling policy */
typedef enum {
    BLOCK_SCHED_ROUND_ROBIN,     /* distribute blocks across SMs evenly */
    BLOCK_SCHED_FILL_FIRST,      /* fill one SM before next */
    BLOCK_SCHED_MIN_QUEUE,       /* send to SM with fewest blocks */
    BLOCK_SCHED_LOCALITY         /* prefer SM with data locality */
} BlockSchedPolicy;

/** Warp scheduling policy */
typedef enum {
    WARP_SCHED_GTO,             /* Greedy-Then-Oldest */
    WARP_SCHED_LRR,             /* Loose Round-Robin */
    WARP_SCHED_TWO_LEVEL,       /* Two-level: fetch + issue groups */
    WARP_SCHED_ADAPTIVE         /* Dynamic based on warp state */
} WarpSchedPolicy;

/** GigaThread Engine state */
typedef struct {
    int             num_sms;
    KernelGrid      grids[MAX_GRIDS];
    int             num_grids;
    BlockSchedPolicy block_policy;
    WarpSchedPolicy warp_policy;

    /* Per-SM block queues */
    int             blocks_per_sm[128];   /* blocks assigned to each SM */
    int             sm_load[128];         /* warp count per SM */

    /* Statistics */
    uint64_t        total_blocks_scheduled;
    uint64_t        total_warps_issued;
    uint64_t        scheduling_cycles;
    uint64_t        idle_sm_cycles;
} GigaThreadEngine;

/* ================================================================
 * L3: Two-Level Warp Scheduler
 * ================================================================ */

/** Warp scheduler per SM (NVIDIA uses 4 sub-partitions per SM) */
#define WARP_SCHED_SUB_PARTITIONS 4

/** Per-SM warp scheduler */
typedef struct {
    int      sm_id;

    /* Warp queues */
    int      num_warps;
    int      active_warp_ids[MAX_WARPS_PER_BLOCK * 8];  /* warps on this SM */
    int      num_active;

    /* Ready queues per scheduling policy */
    int      ready_queue[MAX_WARPS_PER_BLOCK * 8];
    int      ready_head;
    int      ready_tail;

    /* Sub-partition ready queues (for two-level scheduling) */
    int      sub_queues[WARP_SCHED_SUB_PARTITIONS][32];
    int      sub_head[WARP_SCHED_SUB_PARTITIONS];
    int      sub_tail[WARP_SCHED_SUB_PARTITIONS];

    /* Issue slots per cycle */
    int      issue_width;     /* warps issued per cycle */
    int      issues_this_cycle;

    WarpSchedPolicy policy;
    uint64_t total_issued;
    uint64_t total_stall_cycles;
} WarpScheduler;

/* ================================================================
 * L4: Makespan Analysis
 * ================================================================ */

/** Task for scheduling analysis */
typedef struct {
    int      task_id;
    int      duration;        /* cycles */
    int      sm_id;           /* assigned SM */
    int      start_cycle;
    int      end_cycle;
    bool     scheduled;
} SchedTask;

/** Scheduling result (list scheduling) */
typedef struct {
    int      num_tasks;
    SchedTask *tasks;
    int      num_sms;
    int      makespan;        /* total cycles */
    double   efficiency;      /* sum(duration) / (num_sms * makespan) */
    double   lower_bound;     /* max(avg_load, critical_path) */
} SchedResult;

/* ================================================================
 * L5: Adaptive Scheduling
 * ================================================================ */

/** Warp priority heuristics */
typedef enum {
    HEUR_OLDEST_FIRST,        /* minimum launch cycle */
    HEUR_SHORTEST_JOB,        /* minimum remaining cycles */
    HEUR_CRITICAL_PATH,       /* block on critical path */
    HEUR_STALL_COUNT,         /* most stalled warp first */
    HEUR_OCCUPANCY_WEIGHTED   /* balance occupancy */
} WarpHeuristic;

/** Adaptive scheduler state */
typedef struct {
    WarpHeuristic heuristics[4];  /* multiple heuristics for blend */
    double        weights[4];     /* blend weights */
    uint64_t      stall_history[64];  /* per-warp stall counters */
    uint64_t      progress_history[64]; /* per-warp progress */
} AdaptiveScheduler;

/* ================================================================
 * API Declarations
 * ================================================================ */

/* --- L1: Thread hierarchy --- */
KernelGrid* grid_create(int grid_id, Dim3 grid_dim, Dim3 block_dim);
void        grid_destroy(KernelGrid *g);
int         grid_total_warps(const KernelGrid *g);
int         grid_remaining_blocks(const KernelGrid *g);
void        grid_mark_block_done(KernelGrid *g, int block_id);
bool        grid_is_complete(const KernelGrid *g);

/* --- L2: GigaThread Engine --- */
GigaThreadEngine* gte_create(int num_sms);
void              gte_destroy(GigaThreadEngine *gte);
int               gte_submit_grid(GigaThreadEngine *gte, KernelGrid *g);
int               gte_schedule_blocks(GigaThreadEngine *gte);
void              gte_set_block_policy(GigaThreadEngine *gte, BlockSchedPolicy p);
void              gte_set_warp_policy(GigaThreadEngine *gte, WarpSchedPolicy p);
int*              gte_get_sm_queue(const GigaThreadEngine *gte);

/* --- L3: Warp scheduler --- */
WarpScheduler* ws_create(int sm_id, int issue_width);
void           ws_destroy(WarpScheduler *ws);
int            ws_enqueue_warp(WarpScheduler *ws, int warp_id);
int            ws_dequeue_warp(WarpScheduler *ws);
int            ws_schedule_cycle(WarpScheduler *ws, int *issued, int max_issue);
void           ws_warp_stalled(WarpScheduler *ws, int warp_id);
void           ws_warp_ready(WarpScheduler *ws, int warp_id);
int            ws_available_warps(const WarpScheduler *ws);

/* --- L4: Makespan scheduling --- */
SchedResult* list_schedule(SchedTask *tasks, int num_tasks, int num_sms);
void         sched_result_free(SchedResult *r);
double       makespan_lower_bound(const SchedTask *tasks, int num_tasks, int num_sms);

/* --- L5: Adaptive scheduling --- */
AdaptiveScheduler* adaptive_sched_create(void);
void               adaptive_sched_destroy(AdaptiveScheduler *as);
int                adaptive_sched_next(AdaptiveScheduler *as, const int *candidates,
                                       int num_candidates, int *stall_counts);
void               adaptive_sched_update(AdaptiveScheduler *as, int warp_id,
                                         uint64_t progress, uint64_t stalls);

/* --- L6: Thread block scheduler --- */
int  block_schedule_round_robin(KernelGrid *grid, int num_sms, int *sm_queues);
int  block_schedule_fill_first(KernelGrid *grid, int num_sms, int *sm_queues);
int  block_schedule_min_queue(KernelGrid *grid, int num_sms, int *sm_queues);

/* --- L7: Statistics --- */
void gte_print_stats(const GigaThreadEngine *gte);
void ws_print_stats(const WarpScheduler *ws);
void sched_result_print(const SchedResult *r);

#endif /* GPU_THREAD_SCHED_H */
