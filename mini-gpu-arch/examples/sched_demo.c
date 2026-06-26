/**
 * sched_demo.c — Thread & Warp Scheduling Demo
 *
 * Demonstrates:
 *   - Grid/Block/Warp hierarchy creation
 *   - GigaThread Engine block distribution to SMs
 *   - Warp scheduling policies (GTO, LRR, Two-Level)
 *   - List scheduling for makespan optimization
 *   - Adaptive heuristic scheduling
 *   - Block scheduling policies comparison
 *
 * L6: Canonical problem — CUDA block scheduler, warp scheduler
 * L7: Application — parallel task scheduling on GPU SMs
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "thread_sched.h"

int main(void) {
    printf("=== Thread & Warp Scheduler Demo ===\n\n");

    /* --- Demo 1: Create a kernel grid --- */
    printf("--- Kernel Grid ---\n");
    Dim3 grid_dims = {4, 2, 1};   /* 4×2 grid = 8 blocks */
    Dim3 block_dims = {64, 1, 1}; /* 64 threads per block */
    KernelGrid *g = grid_create(0, grid_dims, block_dims);
    if (!g) { fprintf(stderr, "Failed to create grid\n"); return 1; }

    printf("  Grid: %d×%d×%d = %d blocks\n",
           grid_dims.x, grid_dims.y, grid_dims.z, g->total_blocks);
    printf("  Block: %d threads = %d warps\n",
           g->blocks[0].num_threads, g->blocks[0].num_warps);
    printf("  Total threads: %d\n", g->total_threads);
    printf("  Total warps:   %d\n", grid_total_warps(g));

    /* --- Demo 2: GigaThread Engine --- */
    printf("\n--- GigaThread Engine (8 SMs) ---\n");
    GigaThreadEngine *gte = gte_create(8);
    if (!gte) { fprintf(stderr, "Failed to create GTE\n"); grid_destroy(g); return 1; }

    /* Use round-robin for blocks */
    gte_set_block_policy(gte, BLOCK_SCHED_ROUND_ROBIN);
    gte_set_warp_policy(gte, WARP_SCHED_GTO);

    int gid = gte_submit_grid(gte, g);
    printf("  Submitted grid %d\n", gid);

    /* Schedule blocks to SMs */
    int scheduled = gte_schedule_blocks(gte);
    printf("  Scheduled %d blocks across %d SMs (round-robin)\n", scheduled, gte->num_sms);

    /* Show block distribution */
    printf("  Per-SM block distribution:\n");
    for (int s = 0; s < 8; s++) {
        printf("    SM[%d]: %d blocks, %d warps\n",
               s, gte->blocks_per_sm[s], gte->sm_load[s]);
    }

    /* --- Demo 3: Compare Block Scheduling Policies --- */
    printf("\n--- Block Scheduling Policy Comparison ---\n");

    GigaThreadEngine *gte2 = gte_create(4);
    gte2->block_policy = BLOCK_SCHED_FILL_FIRST;
    Dim3 gd2 = {8, 1, 1};
    Dim3 bd2 = {32, 1, 1};
    KernelGrid *g2 = grid_create(1, gd2, bd2);
    gte_submit_grid(gte2, g2);
    int sched_ff = gte_schedule_blocks(gte2);
    printf("  FILL_FIRST:       %d blocks ", sched_ff);
    for (int s = 0; s < 4; s++) printf("SM%d:%d ", s, gte2->blocks_per_sm[s]);
    printf("\n");

    GigaThreadEngine *gte3 = gte_create(4);
    gte3->block_policy = BLOCK_SCHED_MIN_QUEUE;
    Dim3 gd3 = {8, 1, 1};
    Dim3 bd3 = {32, 1, 1};
    KernelGrid *g3 = grid_create(2, gd3, bd3);
    gte_submit_grid(gte3, g3);
    int sched_mq = gte_schedule_blocks(gte3);
    printf("  MIN_QUEUE:        %d blocks ", sched_mq);
    for (int s = 0; s < 4; s++) printf("SM%d:%d ", s, gte3->blocks_per_sm[s]);
    printf("\n");

    /* --- Demo 4: Warp Scheduler --- */
    printf("\n--- Warp Scheduler (per-SM) ---\n");
    WarpScheduler *ws = ws_create(0, 4);
    if (!ws) { fprintf(stderr, "Failed to create warp scheduler\n"); return 1; }

    /* Enqueue 8 warps */
    for (int i = 0; i < 8; i++) {
        ws_enqueue_warp(ws, i);
    }
    printf("  Enqueued 8 warps, %d ready\n", ws_available_warps(ws));

    /* Schedule for 3 cycles */
    for (int cycle = 0; cycle < 3; cycle++) {
        int issued[4] = {0};
        int count = ws_schedule_cycle(ws, issued, 4);
        printf("  Cycle %d: issued %d warps", cycle + 1, count);
        if (count > 0) {
            printf(" [");
            for (int c = 0; c < count; c++) printf("W%d ", issued[c]);
            printf("\b]");
        }
        printf("\n");
    }
    ws_print_stats(ws);

    /* --- Demo 5: Stalled Warp Handling --- */
    printf("\n--- Stalled Warp Handling ---\n");
    ws_warp_stalled(ws, 3);
    ws_warp_stalled(ws, 4);
    printf("  Stalled W3 and W4: %d ready, %lu stall cycles\n",
           ws_available_warps(ws), (unsigned long)ws->total_stall_cycles);

    ws_warp_ready(ws, 3);
    printf("  Unstalled W3: %d ready\n", ws_available_warps(ws));

    /* --- Demo 6: List Scheduling (Makespan) --- */
    printf("\n--- List Scheduling (Makespan Optimization) ---\n");
    SchedTask tasks[6];
    int durations[6] = {10, 20, 15, 5, 25, 10};
    for (int i = 0; i < 6; i++) {
        tasks[i].task_id = i;
        tasks[i].duration = durations[i];
        printf("  Task %d: %d cycles\n", i, tasks[i].duration);
    }

    SchedResult *r = list_schedule(tasks, 6, 3);
    if (r) {
        printf("  Lower bound: %.1f\n", r->lower_bound);
        printf("  Makespan:    %d cycles\n", r->makespan);
        printf("  Efficiency:  %.1f%%\n", r->efficiency * 100.0);
        printf("  Assignment:\n");
        for (int i = 0; i < r->num_tasks; i++) {
            printf("    Task %d → SM%d: [%d, %d]\n",
                   r->tasks[i].task_id, r->tasks[i].sm_id,
                   r->tasks[i].start_cycle, r->tasks[i].end_cycle);
        }
        sched_result_free(r);
    }

    /* Cleanup */
    ws_destroy(ws);
    gte_destroy(gte);
    gte_destroy(gte2);
    gte_destroy(gte3);
    grid_destroy(g);
    grid_destroy(g2);
    grid_destroy(g3);

    printf("\n=== Scheduler Demo Complete ===\n");
    return 0;
}
