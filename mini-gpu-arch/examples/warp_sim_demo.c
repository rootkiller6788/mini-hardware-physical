#include "warp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define NUM_WARPS       8
#define SIM_CYCLES      20
#define MEM_LATENCY     8

int main(void)
{
    printf("===== mini-gpu-arch: Warp Scheduling Simulation Demo =====\n\n");
    printf("[Setup] Creating warp scheduler with %d warps\n", NUM_WARPS);
    printf("[Setup] Memory latency: %d cycles\n", MEM_LATENCY);
    printf("[Setup] Warps needed to hide latency: %d\n\n",
           warp_sched_latency_hiding_warps(MEM_LATENCY));

    /*
     * 演示1: 创建warp调度器并添加warps
     */
    WarpScheduler ws = warp_sched_create(NUM_WARPS);
    warp_sched_set_policy(&ws, SCHED_ROUND_ROBIN);

    for (int i = 0; i < NUM_WARPS; i++) {
        Warp w;
        w.warp_id = i;
        w.pc = 0x1000 + i * 0x10;
        w.active_mask = 0xFFFFFFFF;
        w.stall_cycles = 0;
        w.stall_reason = STALL_NONE;
        w.issued_count = 0;
        w.stalled_cycles = 0;
        w.age = 0;
        warp_sched_add_warp(&ws, &w);
    }

    printf("[DEMO 1] Initial state - all %d warps ready\n", NUM_WARPS);
    warp_sched_print(&ws);
    printf("\n");

    /*
     * 演示2: 模拟warp调度 - 部分warp因内存访问而stall
     * GPU通过切换到其他ready warp来隐藏内存延迟
     */
    printf("[DEMO 2] Scheduling Trace (%d cycles)\n", SIM_CYCLES);
    printf("         Warps 0,2,4,6 will stall for memory every 4th cycle\n");
    printf("         Demonstrating latency hiding: scheduler picks next ready warp\n\n");

    printf("Cycle | Selected Warp | Warp States (R=Ready, S=Stalled)\n");
    printf("------+---------------+------------------------------------\n");

    for (int cycle = 0; cycle < SIM_CYCLES; cycle++) {
        /* 模拟: 每4个周期, 偶数warp触发内存stall */
        if (cycle % 4 == 0 && cycle > 0) {
            for (int i = 0; i < NUM_WARPS; i += 2) {
                /* 每个偶数warp交替stall */
                int wid = (cycle / 4 + i / 2) % NUM_WARPS;
                wid = (wid % 2 == 0) ? wid : wid;
                if (ws.warps[wid].stall_cycles == 0) {
                    ws.warps[wid].stall_cycles = MEM_LATENCY;
                    ws.warps[wid].stall_reason = STALL_MEMORY;
                    ws.active_warp_count--;
                }
            }
        }

        /* 选择一个warp并推进 */
        int selected = warp_sched_select(&ws);
        warp_sched_step(&ws);

        /* 打印该周期的状态 */
        printf("  %3d  |      %3d       | ",
               cycle, selected);

        for (int i = 0; i < NUM_WARPS; i++) {
            printf("W%d:%c ", i,
                   ws.warps[i].stall_cycles > 0 ? 'S' : 'R');
        }
        printf("\n");
    }

    printf("\n");

    /*
     * 演示3: 最终统计
     */
    printf("[DEMO 3] Final Statistics\n");
    for (int i = 0; i < NUM_WARPS; i++) {
        printf("  Warp %d: issued=%llu stalled=%llu cycles\n",
               ws.warps[i].warp_id,
               (unsigned long long)ws.warps[i].issued_count,
               (unsigned long long)ws.warps[i].stalled_cycles);
    }

    /*
     * 演示4: 三种调度策略对比
     */
    printf("\n[DEMO 4] Scheduling Policy Comparison\n");
    const char *policies[] = {"RoundRobin", "Greedy", "AgeBased"};
    SchedPolicy sp[] = {SCHED_ROUND_ROBIN, SCHED_GREEDY, SCHED_AGE_BASED};

    for (int p = 0; p < 3; p++) {
        WarpScheduler ws2 = warp_sched_create(4);
        warp_sched_set_policy(&ws2, sp[p]);

        for (int i = 0; i < 4; i++) {
            Warp w;
            w.warp_id = i;
            w.pc = 0x1000;
            w.active_mask = 0xFFFFFFFF;
            w.stall_cycles = (i == 0) ? 3 : 0;
            w.stall_reason = (i == 0) ? STALL_MEMORY : STALL_NONE;
            w.issued_count = 0;
            w.stalled_cycles = 0;
            w.age = 0;
            warp_sched_add_warp(&ws2, &w);
        }

        printf("  Policy: %-12s | Selection order: ", policies[p]);
        for (int c = 0; c < 5; c++) {
            int sel = warp_sched_select(&ws2);
            printf("W%d ", sel);
            warp_sched_step(&ws2);
        }
        printf("\n");
    }

    /*
     * 演示5: 延迟隐藏理论
     */
    printf("\n[DEMO 5] Latency Hiding Theory\n");
    printf("  Memory latency:          %d cycles\n", MEM_LATENCY);
    printf("  Warps needed to hide:    %d (one ready each cycle)\n", MEM_LATENCY);
    printf("  With %d warps and %d%% stall rate, effective throughput maintained\n",
           NUM_WARPS, 25);
    printf("  Key insight: GPU hides memory latency through massive\n");
    printf("  multithreading, not through caches (though caches help too).\n");

    printf("\n===== Demo Complete =====\n");
    return 0;
}
