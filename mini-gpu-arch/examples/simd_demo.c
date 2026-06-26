#include "simd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void)
{
    printf("===== mini-gpu-arch: SIMD Execution Demo =====\n\n");

    /*
     * 演示1: 创建16-lane SIMD单元
     * 每个lane有16个向量寄存器(VEC_REG_COUNT=16)
     */
    SIMDUnit unit = simd_unit_create(16);
    printf("[DEMO 1] Created SIMD unit with %d lanes\n\n", unit.num_lanes);

    /*
     * 演示2: 向量加法 VADD
     * 将所有lane的R0 = lane_id, R1 = lane_id * 2, 结果R2 = R0 + R1
     */
    printf("[DEMO 2] Vector ADD (VADD)\n");
    uint32_t a[MAX_LANES], b[MAX_LANES], result[MAX_LANES];
    for (int i = 0; i < unit.num_lanes; i++) {
        a[i] = (uint32_t)i;
        b[i] = (uint32_t)(i * 2);
        result[i] = 0;
    }
    simd_execute(&unit, OP_VADD, a, b, result);
    for (int i = 0; i < unit.num_lanes; i++) {
        printf("  Lane %2d: %3u + %3u = %3u\n", i, a[i], b[i], result[i]);
    }
    printf("\n");

    /*
     * 演示3: 向量乘法 VMUL
     */
    printf("[DEMO 3] Vector MUL (VMUL)\n");
    for (int i = 0; i < unit.num_lanes; i++) {
        a[i] = (uint32_t)(i + 1);
        b[i] = (uint32_t)(i + 1);
        result[i] = 0;
    }
    simd_execute(&unit, OP_VMUL, a, b, result);
    for (int i = 0; i < unit.num_lanes; i++) {
        printf("  Lane %2d: %3u * %3u = %3u\n", i, a[i], b[i], result[i]);
    }
    printf("\n");

    /*
     * 演示4: 向量FMA (result = a * b + result)
     */
    printf("[DEMO 4] Vector FMA (VFMA) - Fused Multiply-Add\n");
    for (int i = 0; i < unit.num_lanes; i++) {
        a[i] = 2;
        b[i] = 3;
        result[i] = (uint32_t)(i * 10);  /* 初始累加值 */
    }
    simd_execute(&unit, OP_VFMA, a, b, result);
    for (int i = 0; i < unit.num_lanes; i++) {
        printf("  Lane %2d: %u * %u + initial(%u) = %u\n",
               i, a[i], b[i], (uint32_t)(i * 10), result[i]);
    }
    printf("\n");

    /*
     * 演示5: Predicated执行 - 只有前8个lane活跃
     * 在GPU中divergent branch通过active mask实现条件执行
     */
    printf("[DEMO 5] Predicated Execution (Mask: lanes 0-7 only)\n");
    bool mask[MAX_LANES];
    for (int i = 0; i < unit.num_lanes; i++) {
        mask[i] = (i < 8);  /* 只有lane 0-7活跃 */
    }
    simd_mask_set(&unit, mask);

    for (int i = 0; i < unit.num_lanes; i++) {
        a[i] = 100;
        b[i] = (uint32_t)i;
        result[i] = 0;
    }
    simd_execute(&unit, OP_VADD, a, b, result);
    printf("  Active mask: 0x%04X (lanes 0-7)\n", unit.active_mask);
    for (int i = 0; i < unit.num_lanes; i++) {
        printf("  Lane %2d [%s]: %u + %u = %u\n",
               i, mask[i] ? "ON " : "OFF",
               a[i], b[i], result[i]);
    }
    printf("\n");

    /*
     * 演示6: 向量比较 VCOMP
     */
    printf("[DEMO 6] Vector Compare (VCOMP: a > b ?)\n");
    simd_mask_set(&unit, NULL); /* 重置为全活跃 */
    /* 需要重新做: 默认所有活跃 */
    unit.active_mask = (1U << unit.num_lanes) - 1;

    for (int i = 0; i < unit.num_lanes; i++) {
        a[i] = (uint32_t)i;
        b[i] = (uint32_t)(unit.num_lanes / 2);  /* 与8比较 */
        result[i] = 0;
    }
    simd_execute(&unit, OP_VCOMP, a, b, result);
    for (int i = 0; i < unit.num_lanes; i++) {
        printf("  Lane %2d: %u > %u ? %u\n", i, a[i], b[i], result[i]);
    }
    printf("\n");

    /*
     * 演示7: 打印SIMD单元全部寄存器状态
     */
    printf("[DEMO 7] SIMD Unit Register Dump (showing first 8 lanes)\n");
    for (int i = 0; i < 8; i++) {
        simd_print_lane(&unit, i);
    }

    printf("\n===== Demo Complete =====\n");
    return 0;
}
