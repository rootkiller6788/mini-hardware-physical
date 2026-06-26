#ifndef SHADER_CORE_H
#define SHADER_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include "warp.h"

#define REG_FILE_SIZE_KB 64
#define SHARED_MEM_KB    48
#define L1_CACHE_KB      128
#define MAX_IBUFFER      64

typedef enum {
    OP_ADD,
    OP_MUL,
    OP_FMA,
    OP_LD,
    OP_ST,
    OP_TEX,
    OP_BRA,
    OP_BAR,
    OP_RECIP,
    OP_SQRT,
    OP_EXP,
    OP_LOG
} Opcode;

typedef enum {
    PS_FETCH,
    PS_DECODE,
    PS_ISSUE,
    PS_EXECUTE,
    PS_WRITEBACK
} PipelineStage;

typedef struct {
    Opcode   opcode;
    int      dest_reg;
    int      src1_reg;
    int      src2_reg;
    int      src3_reg;
    uint32_t immediate;
    bool     predicate;
    PipelineStage stage;
} Instruction;

typedef struct {
    Instruction entries[MAX_IBUFFER];
    int         head;
    int         tail;
    int         count;
} InstructionBuffer;

typedef struct {
    int              core_id;
    WarpScheduler    warp_scheduler;
    SIMDUnit         simd_unit;
    uint32_t         register_file[1024 * REG_FILE_SIZE_KB];
    uint32_t         shared_memory[256 * SHARED_MEM_KB];
    uint8_t          l1_cache[1024 * L1_CACHE_KB];
    InstructionBuffer ibuffer;
    Instruction       current_instruction;
    uint64_t          total_issued;
    uint64_t          total_cycles;
    int               texture_units;
} ShaderCore;

ShaderCore shader_core_create(int id);
void       shader_core_issue(ShaderCore *sm);
void       shader_core_execute(ShaderCore *sm);
void       shader_core_writeback(ShaderCore *sm);
void       shader_core_cycle(ShaderCore *sm);
void       shader_core_print_stats(const ShaderCore *sm);
void       shader_core_reset_stats(ShaderCore *sm);

#endif
