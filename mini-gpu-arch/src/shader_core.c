#include "shader_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

ShaderCore shader_core_create(int id)
{
    ShaderCore sm;
    sm.core_id = id;
    sm.warp_scheduler = warp_sched_create(MAX_WARPS_PER_SM);
    sm.simd_unit = simd_unit_create(MAX_LANES);
    sm.ibuffer.head = 0;
    sm.ibuffer.tail = 0;
    sm.ibuffer.count = 0;
    sm.total_issued = 0;
    sm.total_cycles = 0;
    sm.texture_units = 4;
    sm.current_instruction.stage = PS_FETCH;

    memset(sm.register_file, 0, sizeof(sm.register_file));
    memset(sm.shared_memory, 0, sizeof(sm.shared_memory));
    memset(sm.l1_cache, 0, sizeof(sm.l1_cache));

    return sm;
}

/* 从选中的warp发射一条指令到流水线 */
void shader_core_issue(ShaderCore *sm)
{
    int warp_id = warp_sched_select(&sm->warp_scheduler);
    if (warp_id < 0) {
        /* 所有warp都stall了，无法发射 */
        return;
    }

    /* 模拟获取指令：从I-Buffer顶部取 */
    sm->current_instruction.opcode = OP_ADD;
    sm->current_instruction.dest_reg = 0;
    sm->current_instruction.src1_reg = 1;
    sm->current_instruction.src2_reg = 2;
    sm->current_instruction.src3_reg = 3;
    sm->current_instruction.immediate = 0;
    sm->current_instruction.predicate = (sm->warp_scheduler.warps[warp_id].active_mask != 0);
    sm->current_instruction.stage = PS_ISSUE;

    sm->warp_scheduler.warps[warp_id].issued_count++;
    sm->total_issued++;
}

/* 执行当前指令的SIMD操作 */
void shader_core_execute(ShaderCore *sm)
{
    Instruction *inst = &sm->current_instruction;
    uint32_t src1[MAX_LANES];
    uint32_t src2[MAX_LANES];
    uint32_t dst[MAX_LANES];

    /* 收集每个lane的源操作数 */
    for (int i = 0; i < sm->simd_unit.num_lanes; i++) {
        if (!(sm->simd_unit.active_mask & (1U << i))) continue;
        src1[i] = sm->simd_unit.lanes[i].reg[inst->src1_reg];
        src2[i] = sm->simd_unit.lanes[i].reg[inst->src2_reg];
        dst[i]  = sm->simd_unit.lanes[i].reg[inst->dest_reg];
    }

    /* 根据opcode执行 */
    switch (inst->opcode) {
    case OP_ADD:
        sm->current_instruction.stage = PS_EXECUTE;
        for (int i = 0; i < sm->simd_unit.num_lanes; i++) {
            if (sm->simd_unit.active_mask & (1U << i)) {
                dst[i] = src1[i] + src2[i];
            }
        }
        break;
    case OP_MUL:
        sm->current_instruction.stage = PS_EXECUTE;
        for (int i = 0; i < sm->simd_unit.num_lanes; i++) {
            if (sm->simd_unit.active_mask & (1U << i)) {
                dst[i] = src1[i] * src2[i];
            }
        }
        break;
    case OP_FMA:
        sm->current_instruction.stage = PS_EXECUTE;
        for (int i = 0; i < sm->simd_unit.num_lanes; i++) {
            if (sm->simd_unit.active_mask & (1U << i)) {
                dst[i] = src1[i] * src2[i] + dst[i];
            }
        }
        break;
    default:
        sm->current_instruction.stage = PS_EXECUTE;
        break;
    }

    /* 写回结果 */
    for (int i = 0; i < sm->simd_unit.num_lanes; i++) {
        if (sm->simd_unit.active_mask & (1U << i)) {
            sm->simd_unit.lanes[i].reg[inst->dest_reg] = dst[i];
        }
    }
}

void shader_core_writeback(ShaderCore *sm)
{
    sm->current_instruction.stage = PS_WRITEBACK;
}

/* 完整的一个周期：发射 + 执行 + 写回 */
void shader_core_cycle(ShaderCore *sm)
{
    sm->total_cycles++;

    /* 写回阶段 */
    shader_core_writeback(sm);

    /* 发射新指令 */
    shader_core_issue(sm);

    /* 执行当前指令 */
    shader_core_execute(sm);

    /* 推进warp调度器 */
    warp_sched_step(&sm->warp_scheduler);
}

void shader_core_print_stats(const ShaderCore *sm)
{
    printf("Shader Core %d Stats:\n", sm->core_id);
    printf("  Total cycles:      %llu\n", (unsigned long long)sm->total_cycles);
    printf("  Total instructions: %llu\n", (unsigned long long)sm->total_issued);
    printf("  IPC:                %.2f\n",
           sm->total_cycles > 0 ? (double)sm->total_issued / sm->total_cycles : 0.0);
    printf("  Active warps:       %d\n", sm->warp_scheduler.active_warp_count);
    printf("  Texture units:      %d\n", sm->texture_units);
}

void shader_core_reset_stats(ShaderCore *sm)
{
    sm->total_issued = 0;
    sm->total_cycles = 0;
}
