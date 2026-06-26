#include "thread_sched.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

BlockScheduler block_sched_create(int max_blocks, int sm_count)
{
    BlockScheduler bs;
    bs.max_blocks_per_sm = (max_blocks > MAX_BLOCKS_PER_SM) ? MAX_BLOCKS_PER_SM : max_blocks;
    bs.sm_count = (sm_count > MAX_SMS) ? MAX_SMS : sm_count;
    bs.pending_count = 0;
    bs.total_blocks_launched = 0;
    bs.total_blocks_completed = 0;

    for (int sm = 0; sm < bs.sm_count; sm++) {
        bs.active_count[sm] = 0;
        memset(bs.active_blocks[sm], -1, sizeof(bs.active_blocks[sm]));
    }

    memset(bs.pending_queue, 0, sizeof(bs.pending_queue));
    return bs;
}

int block_sched_launch(BlockScheduler *bs, Grid *g)
{
    if (bs->pending_count >= MAX_GRIDS) return -1;

    bs->pending_queue[bs->pending_count] = g;
    bs->total_blocks_launched += g->num_blocks;
    bs->pending_count++;

    return bs->pending_count - 1;
}

/* 将pending blocks分发到各个SM */
void block_sched_schedule(BlockScheduler *bs, ShaderCore *cores[])
{
    for (int g_idx = 0; g_idx < bs->pending_count; g_idx++) {
        Grid *grid = bs->pending_queue[g_idx];
        if (grid == NULL) continue;

        for (int b = 0; b < grid->num_blocks; b++) {
            ThreadBlock *block = &grid->blocks[b];

            /* 找一个有空位的SM */
            int target_sm = -1;
            int min_blocks = bs->max_blocks_per_sm;

            for (int sm = 0; sm < bs->sm_count; sm++) {
                if (bs->active_count[sm] < min_blocks) {
                    min_blocks = bs->active_count[sm];
                    target_sm = sm;
                }
            }

            if (target_sm >= 0 && bs->active_count[target_sm] < bs->max_blocks_per_sm) {
                bs->active_blocks[target_sm][bs->active_count[target_sm]] = block->block_id;
                bs->active_count[target_sm]++;
            }
        }
    }
}

int block_sched_complete(BlockScheduler *bs, int grid_id)
{
    bs->total_blocks_completed++;
    /* 从pending队列移除已完成的grid */
    if (grid_id >= 0 && grid_id < bs->pending_count) {
        bs->pending_queue[grid_id] = NULL;
    }
    return 0;
}

/* 计算occupancy: 给定资源约束，返回可调度的最大active warp数 */
int block_sched_calc_occupancy(int regs_per_thread, int shared_mem_per_block,
                               int max_regs_sm, int max_smem_sm,
                               int max_warps_sm)
{
    int threads_per_warp = 32;
    int warps_per_block = 1; /* 简化：假设每个block 1个warp */

    /* 受寄存器约束 */
    int regs_per_block = regs_per_thread * threads_per_warp * warps_per_block;
    int max_blocks_by_regs = max_regs_sm / regs_per_block;

    /* 受共享内存约束 */
    int max_blocks_by_smem = max_smem_sm / shared_mem_per_block;

    /* 取两者较小值，再受SM最大warp数限制 */
    int max_blocks = (max_blocks_by_regs < max_blocks_by_smem)
                     ? max_blocks_by_regs : max_blocks_by_smem;

    int active_warps = max_blocks * warps_per_block;
    if (active_warps > max_warps_sm) active_warps = max_warps_sm;

    return active_warps;
}

/* 打印occupancy表格（各种寄存器用量下的最大active warp数） */
void block_sched_print_occupancy_table(void)
{
    int reg_configs[] = {32, 48, 64, 96, 128, 192, 255};
    int smem_sizes[] = {0, 8192, 16384, 24576, 32768, 49152};
    int n_regs = sizeof(reg_configs) / sizeof(reg_configs[0]);
    int n_smem = sizeof(smem_sizes) / sizeof(smem_sizes[0]);

    printf("======================================================================\n");
    printf("Occupancy Table (MAX_REG_SM=65536, MAX_SMEM_SM=49152, MAX_WARPS=64)\n");
    printf("======================================================================\n");
    printf("%-10s", "Regs\\SMEM");
    for (int j = 0; j < n_smem; j++) {
        printf("%8d", smem_sizes[j]);
    }
    printf("\n");
    printf("----------");
    for (int j = 0; j < n_smem; j++) {
        printf("--------");
    }
    printf("\n");

    for (int i = 0; i < n_regs; i++) {
        printf("%-10d", reg_configs[i]);
        for (int j = 0; j < n_smem; j++) {
            int occ = block_sched_calc_occupancy(
                reg_configs[i], smem_sizes[j],
                MAX_REGISTERS_SM, MAX_SHARED_MEM_SM, 64);
            printf("%8d", occ);
        }
        printf("\n");
    }
    printf("======================================================================\n");
}

void block_sched_print_status(const BlockScheduler *bs)
{
    printf("Block Scheduler Status:\n");
    printf("  Max blocks per SM: %d\n", bs->max_blocks_per_sm);
    printf("  SM count:          %d\n", bs->sm_count);
    printf("  Pending grids:     %d\n", bs->pending_count);
    printf("  Total launched:    %llu\n", (unsigned long long)bs->total_blocks_launched);
    printf("  Total completed:   %llu\n", (unsigned long long)bs->total_blocks_completed);

    for (int sm = 0; sm < bs->sm_count; sm++) {
        printf("  SM %d: %d active blocks\n", sm, bs->active_count[sm]);
    }
}
