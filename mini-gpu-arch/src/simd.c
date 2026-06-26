#include "simd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

SIMDUnit simd_unit_create(int num_lanes)
{
    SIMDUnit u;
    if (num_lanes > MAX_LANES) num_lanes = MAX_LANES;
    u.num_lanes = num_lanes;
    u.active_mask = 0;
    u.warp_pc = 0;

    for (int i = 0; i < num_lanes; i++) {
        u.lanes[i].lane_id = i;
        u.pc[i] = 0;
        for (int j = 0; j < VEC_REG_COUNT; j++) {
            u.lanes[i].reg[j] = 0;
            u.vector_registers[i][j] = 0;
        }
    }
    /* 默认所有lane活跃 */
    u.active_mask = (1U << num_lanes) - 1;
    return u;
}

/* 按位设置active mask：true = 活跃, false = 非活跃 */
void simd_mask_set(SIMDUnit *u, bool *mask)
{
    u->active_mask = 0;
    for (int i = 0; i < u->num_lanes; i++) {
        if (mask[i]) {
            u->active_mask |= (1U << i);
        }
    }
}

void simd_execute(SIMDUnit *u, SIMDOp op, uint32_t *a, uint32_t *b, uint32_t *result)
{
    for (int i = 0; i < u->num_lanes; i++) {
        /* 检查该lane的active mask位 */
        if (!(u->active_mask & (1U << i))) continue;

        switch (op) {
        case OP_VADD:
            result[i] = a[i] + b[i];
            break;
        case OP_VMUL:
            result[i] = a[i] * b[i];
            break;
        case OP_VFMA:
            /* result = a * b + result (fused multiply-add) */
            result[i] = a[i] * b[i] + result[i];
            break;
        case OP_VLD:
            result[i] = a[i]; /* 模拟从a地址加载 */
            break;
        case OP_VST:
            result[i] = a[i]; /* 模拟存储到a地址 */
            break;
        case OP_VCOMP:
            /* 比较a[i]和b[i]，结果存入result */
            result[i] = (a[i] > b[i]) ? 1 : 0;
            break;
        default:
            break;
        }
    }
}

void simd_print_regs(const SIMDUnit *u)
{
    printf("SIMD Unit: %d lanes, active_mask=0x%08X\n", u->num_lanes, u->active_mask);
    for (int i = 0; i < u->num_lanes; i++) {
        if (u->active_mask & (1U << i)) {
            printf("  Lane %2d [ACTIVE  ]: R0=%10u R1=%10u R2=%10u R3=%10u\n",
                   i, u->lanes[i].reg[0], u->lanes[i].reg[1],
                   u->lanes[i].reg[2], u->lanes[i].reg[3]);
        } else {
            printf("  Lane %2d [INACTIVE]: R0=%10u R1=%10u R2=%10u R3=%10u\n",
                   i, u->lanes[i].reg[0], u->lanes[i].reg[1],
                   u->lanes[i].reg[2], u->lanes[i].reg[3]);
        }
    }
}

void simd_print_lane(const SIMDUnit *u, int lane_id)
{
    if (lane_id < 0 || lane_id >= u->num_lanes) return;
    printf("Lane %d: ", lane_id);
    for (int j = 0; j < 4; j++) {
        printf("R%d=%u ", j, u->lanes[lane_id].reg[j]);
    }
    printf("\n");
}
