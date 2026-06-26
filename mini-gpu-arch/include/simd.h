#ifndef SIMD_H
#define SIMD_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_LANES      32
#define VEC_REG_COUNT  16

typedef enum {
    OP_VADD,
    OP_VMUL,
    OP_VFMA,
    OP_VLD,
    OP_VST,
    OP_VCOMP
} SIMDOp;

typedef struct {
    int      lane_id;
    uint32_t reg[VEC_REG_COUNT];
} SIMDLane;

typedef struct {
    int       num_lanes;
    SIMDLane  lanes[MAX_LANES];
    uint32_t  vector_registers[MAX_LANES][VEC_REG_COUNT];
    uint32_t  active_mask;
    uint32_t  pc[MAX_LANES];
    uint32_t  warp_pc;
} SIMDUnit;

SIMDUnit  simd_unit_create(int num_lanes);
void      simd_execute(SIMDUnit *u, SIMDOp op, uint32_t *a, uint32_t *b, uint32_t *result);
void      simd_mask_set(SIMDUnit *u, bool *mask);
void      simd_print_regs(const SIMDUnit *u);
void      simd_print_lane(const SIMDUnit *u, int lane_id);

#endif
