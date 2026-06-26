#ifndef THREAD_SCHED_H
#define THREAD_SCHED_H

#include <stdbool.h>
#include <stdint.h>
#include "shader_core.h"

#define MAX_BLOCKS_PER_SM   32
#define MAX_GRIDS           16
#define MAX_SMS             8
#define MAX_REGISTERS_SM    65536
#define MAX_SHARED_MEM_SM   49152

typedef struct {
    int       block_id;
    int       dim_x;
    int       dim_y;
    int       dim_z;
    int       shared_mem_alloc;
    int       registers_alloc;
    int       grid_id;
} ThreadBlock;

typedef struct {
    int          grid_id;
    int          num_blocks;
    ThreadBlock *blocks;
    int          registers_per_thread;
    int          shared_mem_per_block;
} Grid;

typedef struct {
    int         max_blocks_per_sm;
    int         active_blocks[MAX_SMS][MAX_BLOCKS_PER_SM];
    int         active_count[MAX_SMS];
    Grid       *pending_queue[MAX_GRIDS];
    int         pending_count;
    int         sm_count;
    uint64_t    total_blocks_launched;
    uint64_t    total_blocks_completed;
} BlockScheduler;

BlockScheduler block_sched_create(int max_blocks, int sm_count);
int            block_sched_launch(BlockScheduler *bs, Grid *g);
void           block_sched_schedule(BlockScheduler *bs, ShaderCore *cores[]);
int            block_sched_complete(BlockScheduler *bs, int grid_id);
int            block_sched_calc_occupancy(int regs_per_thread, int shared_mem_per_block,
                                          int max_regs_sm, int max_smem_sm,
                                          int max_warps_sm);
void           block_sched_print_occupancy_table(void);
void           block_sched_print_status(const BlockScheduler *bs);

#endif
