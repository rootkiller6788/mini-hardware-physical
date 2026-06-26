#include "memory_gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void)
{
    printf("===== mini-gpu-arch: Memory Coalescing Demo =====\n\n");

    /*
     * 演示1: 创建GPU内存子系统
     */
    GPUMemory gmem = gpu_mem_init(4);  /* 4GB */
    printf("[DEMO 1] GPU Memory Subsystem Created\n");
    gpu_mem_print_bandwidth_stats(&gmem);
    printf("\n");

    /*
     * 演示2: 合并访问 Coalesced Access
     * 16个连续地址: [0, 4, 8, 12, ..., 60] - 全部在同一128B cache line内
     */
    printf("[DEMO 2] Coalesced Access: Sequential addresses\n");
    printf("         Addresses: 0, 4, 8, 12, 16, ..., 60 (16 words)\n");

    uint32_t coalesced_addrs[16];
    for (int i = 0; i < 16; i++) {
        coalesced_addrs[i] = (uint32_t)(i * 4);
    }

    /* 检查是否合并 */
    bool is_coal = mem_is_coalesced(&gmem, coalesced_addrs, 16);
    printf("  Is coalesced? %s\n", is_coal ? "YES" : "NO");

    /* 模拟合并访问 */
    uint64_t old_trans = gmem.total_transactions;
    int trans = mem_coalesced_access(&gmem, coalesced_addrs, 16);
    printf("  DRAM transactions: %d\n", trans);
    printf("  Data transferred:  16 * 4 = 64 bytes in 1 burst\n");
    printf("\n");

    /*
     * 演示3: 非合并访问 Uncoalesced Access
     * 16个跨步地址: [0, 128, 256, 384, ..., 1920] - 每个在不同cache line
     */
    printf("[DEMO 3] Uncoalesced Access: Strided addresses (stride=128)\n");
    printf("         Addresses: 0, 128, 256, 384, ..., 1920 (16 words)\n");

    uint32_t strided_addrs[16];
    for (int i = 0; i < 16; i++) {
        strided_addrs[i] = (uint32_t)(i * 128);
    }

    is_coal = mem_is_coalesced(&gmem, strided_addrs, 16);
    printf("  Is coalesced? %s\n", is_coal ? "YES" : "NO");

    old_trans = gmem.total_transactions;
    trans = mem_uncoalesced_access(&gmem, strided_addrs, 16);
    printf("  DRAM transactions: %d (each word needs separate transaction)\n", trans);
    printf("  Data transferred:  16 * 128 = 2048 bytes (16 full cache lines)\n");
    printf("\n");

    /*
     * 演示4: 性能对比
     */
    printf("[DEMO 4] Performance Comparison: Coalesced vs Uncoalesced\n");
    printf("  +------------------+-------------+----------------+\n");
    printf("  | Metric           | Coalesced   | Uncoalesced    |\n");
    printf("  +------------------+-------------+----------------+\n");
    printf("  | Words accessed   | 16          | 16             |\n");
    printf("  | DRAM transactions| 1           | 16             |\n");
    printf("  | Bus utilization  | 100%%        | %.0f%%           |\n",
           100.0 * 64.0 / (16.0 * 128.0));
    printf("  | Effective BW     | %.0f%%        | %.0f%%            |\n",
           100.0, 100.0 * 64.0 / (16.0 * 128.0));
    printf("  +------------------+-------------+----------------+\n");
    printf("\n");

    /*
     * 演示5: 共享内存Bank Conflict检查
     */
    printf("[DEMO 5] Shared Memory Bank Conflict Analysis\n");
    /* 无冲突: 每个bank只被访问一次 */
    uint32_t no_conflict[32];
    for (int i = 0; i < 32; i++) {
        no_conflict[i] = (uint32_t)(i * 4);
    }
    int max_conf = mem_bank_conflict_count(no_conflict, 32);
    printf("  No-conflict (stride=1): max per bank = %d\n", max_conf);

    /* 2-way冲突: stride=2 到每个bank */
    uint32_t way2_conflict[32];
    for (int i = 0; i < 32; i++) {
        /* stride=2 => 每2个地址映射到同一bank */
        way2_conflict[i] = (uint32_t)((i / 2) * 8);
    }
    max_conf = mem_bank_conflict_count(way2_conflict, 32);
    printf("  2-way conflict (stride=2): max per bank = %d\n", max_conf);

    /* 32-way冲突: 所有地址映射到同一bank */
    uint32_t way32_conflict[32];
    for (int i = 0; i < 32; i++) {
        way32_conflict[i] = (uint32_t)(i * 128); /* 所有到bank 0 */
    }
    max_conf = mem_bank_conflict_count(way32_conflict, 32);
    printf("  32-way conflict (same bank): max per bank = %d\n", max_conf);
    printf("\n");

    /*
     * 演示6: SoA vs AoS 内存布局对合并的影响
     */
    printf("[DEMO 6] Struct-of-Arrays (SoA) vs Array-of-Structs (AoS)\n");
    printf("  SoA layout (good for GPU):\n");
    printf("    x[0..N], y[0..N], z[0..N] -> consecutive reads coalesced\n");
    printf("  AoS layout (bad for GPU):\n");
    printf("    {x0,y0,z0}, {x1,y1,z1}, ... -> strided reads, uncoalesced\n");
    printf("  Rule: Use SoA for GPU to maximize coalescing\n");
    printf("\n");

    /*
     * 演示7: 最终统计
     */
    printf("[DEMO 7] Final Memory Stats\n");
    gpu_mem_print_bandwidth_stats(&gmem);

    /* 清理 */
    gpu_mem_free(&gmem);

    printf("\n===== Demo Complete =====\n");
    return 0;
}
