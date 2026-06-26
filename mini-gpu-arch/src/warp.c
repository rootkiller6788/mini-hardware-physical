#include "warp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

WarpScheduler warp_sched_create(int max_warps)
{
    WarpScheduler ws;
    if (max_warps > MAX_WARPS_PER_SM) max_warps = MAX_WARPS_PER_SM;
    ws.max_warps = max_warps;
    ws.num_warps = 0;
    ws.active_warp_count = 0;
    ws.current_warp = 0;
    ws.scheduling_policy = SCHED_ROUND_ROBIN;
    memset(ws.warps, 0, sizeof(ws.warps));
    return ws;
}

void warp_sched_set_policy(WarpScheduler *ws, SchedPolicy policy)
{
    ws->scheduling_policy = policy;
}

int warp_sched_add_warp(WarpScheduler *ws, Warp *w)
{
    if (ws->num_warps >= ws->max_warps) return -1;
    ws->warps[ws->num_warps] = *w;
    ws->warps[ws->num_warps].age = 0;
    ws->warps[ws->num_warps].issued_count = 0;
    ws->warps[ws->num_warps].stalled_cycles = 0;
    if (ws->warps[ws->num_warps].stall_cycles == 0) {
        ws->active_warp_count++;
    }
    ws->num_warps++;
    return ws->num_warps - 1;
}

/* 选择下一个可发射的warp，返回warp_id，全stall返回-1 */
int warp_sched_select(WarpScheduler *ws)
{
    if (ws->num_warps == 0) return -1;

    switch (ws->scheduling_policy) {
    case SCHED_ROUND_ROBIN: {
        /* 轮询调度：从current_warp开始找下一个ready的warp */
        for (int i = 0; i < ws->num_warps; i++) {
            int idx = (ws->current_warp + i) % ws->num_warps;
            if (ws->warps[idx].stall_cycles == 0) {
                ws->current_warp = (idx + 1) % ws->num_warps;
                return idx;
            }
        }
        return -1;
    }
    case SCHED_GREEDY: {
        /* 贪心调度：总是选择第一个ready的warp */
        for (int i = 0; i < ws->num_warps; i++) {
            if (ws->warps[i].stall_cycles == 0) {
                return i;
            }
        }
        return -1;
    }
    case SCHED_AGE_BASED: {
        /* 基于年龄：选age最大且ready的warp */
        int best = -1;
        int best_age = -1;
        for (int i = 0; i < ws->num_warps; i++) {
            if (ws->warps[i].stall_cycles == 0 && ws->warps[i].age > best_age) {
                best_age = ws->warps[i].age;
                best = i;
            }
        }
        if (best >= 0) {
            ws->warps[best].age = 0; /* 发射后重置年龄 */
            /* 其他warp年龄+1 */
            for (int i = 0; i < ws->num_warps; i++) {
                if (i != best) ws->warps[i].age++;
            }
        }
        return best;
    }
    default:
        return -1;
    }
}

void warp_sched_step(WarpScheduler *ws)
{
    /* 每个周期：递减stall计数器 */
    for (int i = 0; i < ws->num_warps; i++) {
        if (ws->warps[i].stall_cycles > 0) {
            ws->warps[i].stall_cycles--;
            ws->warps[i].stalled_cycles++;
            if (ws->warps[i].stall_cycles == 0) {
                ws->warps[i].stall_reason = STALL_NONE;
                ws->active_warp_count++;
            }
        }
        ws->warps[i].age++;
    }

    /* 选择一个warp发射指令 */
    int selected = warp_sched_select(ws);
    if (selected >= 0) {
        ws->warps[selected].issued_count++;
        ws->warps[selected].pc++;
    }
}

/* 计算隐藏给定内存延迟所需的最少warps数量 */
int warp_sched_latency_hiding_warps(int mem_latency_cycles)
{
    /* 假设每cycle发射1条指令，需要enough warps来填充latency */
    return mem_latency_cycles;
}

void warp_print_state(const Warp *w)
{
    const char *reason_str;
    switch (w->stall_reason) {
    case STALL_NONE:       reason_str = "NONE";       break;
    case STALL_MEMORY:     reason_str = "MEMORY";     break;
    case STALL_SCOREBOARD: reason_str = "SCOREBOARD"; break;
    case STALL_BARRIER:    reason_str = "BARRIER";    break;
    default:               reason_str = "UNKNOWN";    break;
    }

    printf("Warp %2d | PC=0x%04X | mask=0x%08X | stall=%d(%s) | issued=%llu | age=%d\n",
           w->warp_id, w->pc, w->active_mask,
           w->stall_cycles, reason_str,
           (unsigned long long)w->issued_count, w->age);
}

void warp_sched_print(const WarpScheduler *ws)
{
    printf("==== Warp Scheduler (%d warps, %d active, policy=%d) ====\n",
           ws->num_warps, ws->active_warp_count, ws->scheduling_policy);
    for (int i = 0; i < ws->num_warps; i++) {
        warp_print_state(&ws->warps[i]);
    }
}
