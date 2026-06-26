#ifndef WARP_H
#define WARP_H

#include <stdbool.h>
#include <stdint.h>
#include "simd.h"

#define MAX_WARPS_PER_SM 64
#define WARP_SIZE        32

typedef enum {
    STALL_NONE,
    STALL_MEMORY,
    STALL_SCOREBOARD,
    STALL_BARRIER
} StallReason;

typedef enum {
    SCHED_ROUND_ROBIN,
    SCHED_GREEDY,
    SCHED_AGE_BASED
} SchedPolicy;

typedef struct {
    int         warp_id;
    SIMDLane    lanes[WARP_SIZE];
    uint32_t    pc;
    uint32_t    active_mask;
    int         stall_cycles;
    StallReason stall_reason;
    uint64_t    issued_count;
    uint64_t    stalled_cycles;
    int         age;
} Warp;

typedef struct {
    int         num_warps;
    int         max_warps;
    Warp        warps[MAX_WARPS_PER_SM];
    int         active_warp_count;
    int         current_warp;
    SchedPolicy scheduling_policy;
} WarpScheduler;

WarpScheduler warp_sched_create(int max_warps);
void          warp_sched_set_policy(WarpScheduler *ws, SchedPolicy policy);
int           warp_sched_add_warp(WarpScheduler *ws, Warp *w);
int           warp_sched_select(WarpScheduler *ws);
void          warp_sched_step(WarpScheduler *ws);
int           warp_sched_latency_hiding_warps(int mem_latency_cycles);
void          warp_print_state(const Warp *w);
void          warp_sched_print(const WarpScheduler *ws);

#endif
