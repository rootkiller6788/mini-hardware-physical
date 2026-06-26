#include "thread_sched.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void)
{
    printf("===== mini-gpu-arch: Occupancy Calculator Demo =====\n\n");

    /*
     * 演示1: 单一点计算
     * 给定寄存器/共享内存用量，计算最大active warp数
     */
    printf("[DEMO 1] Single Point Occupancy Calculation\n");
    printf("  Assumptions:\n");
    printf("    Max registers per SM:  %d\n", MAX_REGISTERS_SM);
    printf("    Max shared mem per SM: %d bytes\n", MAX_SHARED_MEM_SM);
    printf("    Max warps per SM:      64\n");
    printf("    Threads per warp:      32\n");
    printf("    Warps per block:       1 (simplified)\n");
    printf("\n");

    /* 测试几种配置 */
    int configs[][2] = {
        {32,  0},      /* 轻量线程 */
        {48,  8192},   /* 中等配置 */
        {64,  16384},  /* 典型配置 */
        {96,  24576},  /* 重寄存器 */
        {128, 32768},  /* 较重配置 */
        {255, 49152},  /* 极限配置 */
    };

    printf("  %-12s %-12s %-15s %-20s %-12s\n",
           "Regs/Thread", "Smem/Block", "Blocks(SMEM)",
           "Blocks(REG)", "Active Warps");
    printf("  %s\n",
           "----------------------------------------------------------"
           "-----------------");

    int n_configs = sizeof(configs) / sizeof(configs[0]);
    for (int i = 0; i < n_configs; i++) {
        int occ = block_sched_calc_occupancy(
            configs[i][0], configs[i][1],
            MAX_REGISTERS_SM, MAX_SHARED_MEM_SM, 64);

        int regs_per_block = configs[i][0] * 32 * 1;
        int blocks_by_regs = MAX_REGISTERS_SM / regs_per_block;
        int blocks_by_smem = (configs[i][1] > 0)
                             ? MAX_SHARED_MEM_SM / configs[i][1]
                             : MAX_BLOCKS_PER_SM;

        printf("  %-12d %-12d %-15d %-20d %-12d\n",
               configs[i][0], configs[i][1],
               blocks_by_smem, blocks_by_regs, occ);
    }
    printf("\n");

    /*
     * 演示2: 完整Occupancy表
     */
    printf("[DEMO 2] Full Occupancy Table\n");
    block_sched_print_occupancy_table();
    printf("\n");

    /*
     * 演示3: 如何提高Occupancy
     */
    printf("[DEMO 3] How to Improve Occupancy\n");
    printf("  Strategies:\n");
    printf("  1. Reduce register usage (launch bounds, __launch_bounds__)\n");
    printf("     - Use fewer local variables\n");
    printf("     - Compiler flag: -maxrregcount=N\n");
    printf("  2. Reduce shared memory per block\n");
    printf("     - Use smaller tiles\n");
    printf("     - Spill to global memory if necessary\n");
    printf("  3. Use smaller block sizes\n");
    printf("     - Fewer threads = fewer registers per block\n");
    printf("  4. Trade occupancy for other resources:\n");
    printf("     - Sometimes lower occupancy + larger tiles = better perf\n");
    printf("\n");

    /*
     * 演示4: NVIDIA Occupancy API模拟
     */
    printf("[DEMO 4] Simulated NVIDIA Occupancy API\n");
    printf("  CUDA API: cudaOccupancyMaxActiveBlocksPerMultiprocessor()\n");
    printf("  Our API:  block_sched_calc_occupancy()\n");
    printf("\n");

    printf("  Scenario: Matrix Multiply kernel\n");
    printf("  - Registers per thread:  32\n");
    printf("  - Shared memory per block: 8192 bytes (16x16 tile of floats)\n");
    printf("  - Block size:             256 threads\n");
    printf("  - Warps per block:        8\n");
    printf("  => Estimated occupancy: 64 warps (100%%)\n");
    printf("\n");

    printf("  Scenario: Reduction kernel (heavy register use)\n");
    printf("  - Registers per thread:  64\n");
    printf("  - Shared memory per block: 1024 bytes\n");
    printf("  - Block size:             256 threads\n");
    printf("  - Warps per block:        8\n");
    printf("  => Regs per block: 64*256 = 16384\n");
    printf("  => Max blocks by regs: 65536/16384 = 4\n");
    printf("  => Active warps: 4*8 = 32 warps (50%% occupancy)\n");

    printf("\n===== Demo Complete =====\n");
    return 0;
}
