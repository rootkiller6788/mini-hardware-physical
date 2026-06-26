#ifndef GPU_WARP_H
#define GPU_WARP_H

/**
 * mini-gpu-arch: Warp Execution Engine (SIMT)
 *
 * @L1_Definitions: Warp, thread, lane, warp scheduler, barrier
 * @L2_CoreConcepts: SIMT divergence/reconvergence, warp-level primitives
 * @L3_EngStructures: Warp state machine, instruction issue, active mask stack
 * @L4_Standards: SIMT efficiency formula, divergence overhead bounds
 * @L5_Algorithms: Branch divergence tracking, reconvergence IPDOM, ballot/shuffle
 *
 * Course mapping:
 *   CMU 15-418: Parallel Architecture — SIMT execution model
 *   Stanford CS149: GPU warp-level programming
 *   UT Austin CS395T: GPU system software
 */

#include <stdint.h>
#include <stdbool.h>

/* ================================================================
 * L1: Core Definitions
 * ================================================================ */

#define WARP_SIZE  32        /* NVIDIA warp size */
#define MAX_WARPS  64        /* max warps per SM */
#define MAX_INSTR  256       /* max instructions in kernel */

/** Thread state within a warp */
typedef enum {
    T_READY   = 0,  /* instruction fetched, ready to issue */
    T_ACTIVE  = 1,  /* currently executing */
    T_BLOCKED = 2,  /* waiting on memory / barrier */
    T_DONE    = 3
} ThreadState;

/** Warp scheduling priority class */
typedef enum {
    PRIO_NORMAL = 0,
    PRIO_HIGH   = 1,
    PRIO_CRIT   = 2
} WarpPrio;

/** Single warp (32 threads) */
typedef struct {
    int         warp_id;
    uint32_t    active_mask;        /* 32-bit: which threads are active */
    uint32_t    thread_state[WARP_SIZE];  /* per-thread state */
    uint32_t    prog_counter[WARP_SIZE];  /* per-thread PC for divergence */
    uint32_t    unified_pc;         /* PC when all threads converge */
    int         num_active;         /* count of active threads */
    WarpPrio    priority;
    uint64_t    stall_cycles;       /* cycles spent stalled */
    uint64_t    issue_count;        /* instructions issued */
    bool        at_barrier;         /* waiting at __syncthreads() */
    bool        diverged;           /* warp has divergent paths */
    /* divergence tracking */
    uint32_t    reconverge_pc;      /* immediate post-dominator PC */
    int         div_depth;          /* stack depth of divergent branches */
    uint32_t    div_stack[8];       /* saved masks for nested branches */
    int         div_pc_stack[8];    /* reconverge PCs for nested branches */
} Warp;

/** Mask for warp shuffle operations */
typedef enum {
    SHFL_UP, SHFL_DOWN, SHFL_XOR, SHFL_IDX
} ShuffleOp;

/** Warp vote type */
typedef enum {
    VOTE_ALL, VOTE_ANY, VOTE_BALLOT
} VoteOp;

/* ================================================================
 * L3: Warp Scheduler
 * ================================================================ */

/** Scheduling policy */
typedef enum {
    SCHED_GTO,      /* Greedy-Then-Oldest */
    SCHED_LRR,      /* Loose Round-Robin */
    SCHED_PRIO      /* Priority-based */
} SchedPolicy;

/** Instruction issue slot */
typedef struct {
    int    warp_id;
    bool   valid;
    uint32_t pc;
    uint32_t active_mask;  /* which lanes execute */
} IssueSlot;

/* ================================================================
 * L4: Divergence Analysis
 * ================================================================ */

/** SIMT efficiency metrics */
typedef struct {
    double   simt_efficiency;    /* avg active_threads/WARP_SIZE */
    double   branch_divergence;  /* fraction of divergent branches */
    double   reconvergence_cost; /* avg cycles to reconverge */
    int      max_div_depth;      /* maximum nesting depth */
} SIMTEfficiency;

/** Branch pattern type for divergence classification */
typedef enum {
    BRANCH_UNIFORM,       /* all threads same direction */
    BRANCH_TWO_WAY,       /* standard if-else */
    BRANCH_MULTI_WAY,     /* switch-case */
    BRANCH_LOOP_TAIL      /* loop exit with varying trip counts */
} BranchPattern;

/* ================================================================
 * API Declarations
 * ================================================================ */

/* --- L1: Warp lifecycle --- */
Warp* warp_create(int warp_id);
void  warp_destroy(Warp *w);
void  warp_reset(Warp *w);
void  warp_activate_all(Warp *w);
bool  warp_any_active(const Warp *w);

/* --- L2: SIMT execution --- */
void  warp_set_active_mask(Warp *w, uint32_t mask);
void  warp_push_divergence(Warp *w, uint32_t taken_mask, uint32_t reconverge_pc);
void  warp_pop_divergence(Warp *w);
bool  warp_is_diverged(const Warp *w);
int   warp_active_count(const Warp *w);

/* --- L3: Warp-level primitives (ballot, shuffle) --- */
uint32_t warp_vote(const Warp *w, VoteOp op, bool lane_pred);
float    warp_shuffle_up(const Warp *w, float val, int delta);
float    warp_shuffle_down(const Warp *w, float val, int delta);
float    warp_shuffle_xor(const Warp *w, float val, int mask);

/* --- L4: Divergence analysis --- */
SIMTEfficiency   warp_simt_efficiency(const Warp *w);
BranchPattern    warp_classify_branch(const Warp *w, uint32_t taken_mask);
double           warp_idle_ratio(const Warp *w);

/* --- L5: Warp reduction --- */
float  warp_reduce_sum(Warp *w, float val);
float  warp_reduce_min(Warp *w, float val);
float  warp_reduce_max(Warp *w, float val);

/* --- L6: Instruction issue --- */
int    warp_issue_instr(Warp *w);
void   warp_complete_instr(Warp *w);

/* --- L7: Stall handling --- */
void   warp_stall_memory(Warp *w);
void   warp_unstall_memory(Warp *w);
bool   warp_is_stalled(const Warp *w);

/* --- L8: Debug & stats --- */
void   warp_print_state(const Warp *w);
void   warp_print_divergence_analysis(const Warp *w);

#endif /* GPU_WARP_H */
