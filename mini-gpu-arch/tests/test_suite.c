/**
 * mini-gpu-arch: Comprehensive Test Suite
 *
 * Tests all core APIs across all six submodules:
 *   simd, warp, shader_core, tensor_core, memory_gpu, thread_sched
 *
 * Compile: gcc -Wall -Wextra -I include tests/test_suite.c src/ all .c -lm -o test_suite
 * Run:     ./test_suite
 * Bench:   ./test_suite bench
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <stdbool.h>
#include <inttypes.h>

#include "simd.h"
#include "warp.h"
#include "shader_core.h"
#include "tensor_core.h"
#include "memory_gpu.h"
#include "thread_sched.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    printf("  TEST: %-50s ", name); \
    fflush(stdout); \
} while(0)

#define PASS() do { \
    printf("PASS\n"); \
    tests_passed++; \
} while(0)

#define FAIL(msg, ...) do { \
    printf("FAIL: " msg "\n", ##__VA_ARGS__); \
    tests_failed++; \
} while(0)

#define ASSERT_EQ(a, b, fmt) do { \
    if ((a) != (b)) { FAIL("%s != %s (" fmt " vs " fmt ")", #a, #b, (a), (b)); return; } \
} while(0)

#define ASSERT_FLOAT_EQ(a, b) do { \
    if (fabsf((float)(a) - (float)(b)) > 1e-4f) { \
        FAIL("%s != %s (%.6f vs %.6f)", #a, #b, (double)(a), (double)(b)); return; \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { FAIL("%s is false", #cond); return; } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { FAIL("%s is NULL", #ptr); return; } \
} while(0)

/* ===================================================================
 * SIMD Tests
 * =================================================================== */
static void test_simd_create(void) {
    TEST("simd_create(32)");
    SIMDUnit *u = simd_create(32);
    ASSERT_NOT_NULL(u);
    ASSERT_EQ(u->num_lanes, 32, "%d");
    simd_destroy(u);
    PASS();
}

static void test_simd_create_invalid(void) {
    TEST("simd_create(-1) returns NULL");
    SIMDUnit *u = simd_create(-1);
    ASSERT_EQ(u == NULL, true, "%d");
    PASS();
}

static void test_simd_vop_add(void) {
    TEST("simd_vop(VOP_ADD, ...)");
    SIMDUnit *u = simd_create(32);
    ASSERT_NOT_NULL(u);

    float a[32], b[32], r[32];
    for (int i = 0; i < 32; i++) { a[i] = (float)i; b[i] = (float)(i * 2); }
    simd_vop(u, VOP_ADD, a, b, r, 32);
    for (int i = 0; i < 32; i++) ASSERT_FLOAT_EQ(r[i], i * 3.0f);

    simd_destroy(u);
    PASS();
}

static void test_simd_vfma(void) {
    TEST("simd_vfma(A, B, C)");
    SIMDUnit *u = simd_create(32);
    ASSERT_NOT_NULL(u);

    float a[32], b[32], c[32], r[32];
    for (int i = 0; i < 32; i++) { a[i] = 2.0f; b[i] = 3.0f; c[i] = 1.0f; }
    simd_vfma(u, a, b, c, r, 32);
    for (int i = 0; i < 32; i++) ASSERT_FLOAT_EQ(r[i], 7.0f);

    simd_destroy(u);
    PASS();
}

static void test_simd_vreduce_sum(void) {
    TEST("simd_vreduce(REDUCE_SUM)");
    SIMDUnit *u = simd_create(32);
    ASSERT_NOT_NULL(u);

    float data[32], result;
    for (int i = 0; i < 32; i++) data[i] = 1.0f;
    simd_vreduce(u, REDUCE_SUM, data, 32, &result);
    ASSERT_FLOAT_EQ(result, 32.0f);

    simd_destroy(u);
    PASS();
}

static void test_simd_predication(void) {
    TEST("simd_mask_push/pop");
    SIMDUnit *u = simd_create(32);
    ASSERT_NOT_NULL(u);

    uint32_t orig = u->exec_mask;
    simd_mask_push(u, 0x0000FFFF);  /* only lower 16 active */
    ASSERT_EQ(u->exec_mask, 0x0000FFFF, "0x%08X");
    simd_mask_pop(u);
    ASSERT_EQ(u->exec_mask, orig, "0x%08X");

    simd_destroy(u);
    PASS();
}

static void test_amdahl_law(void) {
    TEST("amdahl_compute(0.1, 32)");
    AmdahlModel m = amdahl_compute(0.1, 32);
    /* speedup = 1/(0.1 + 0.9/32) = 1/(0.1 + 0.028125) = 1/0.128125 ≈ 7.80 */
    ASSERT_TRUE(m.speedup > 7.7 && m.speedup < 7.9);
    PASS();
}

static void test_simd_coalesce(void) {
    TEST("simd_coalesce_analyze(stride=1)");
    MemAccessPattern pat = {0};
    pat.stride = 1;
    pat.is_scatter = false;
    pat.num_indices = 32;
    for (int i = 0; i < 32; i++) pat.indices[i] = i * 4;

    CoalesceResult cr = simd_coalesce_analyze(&pat, 128);
    ASSERT_FLOAT_EQ(cr.efficiency, 1.0);

    PASS();
}

/* ===================================================================
 * Warp Tests
 * =================================================================== */
static void test_warp_create(void) {
    TEST("warp_create(0)");
    Warp *w = warp_create(0);
    ASSERT_NOT_NULL(w);
    ASSERT_EQ(w->warp_id, 0, "%d");
    ASSERT_EQ(w->num_active, 32, "%d");
    warp_destroy(w);
    PASS();
}

static void test_warp_active_mask(void) {
    TEST("warp_set_active_mask(0x0000FFFF)");
    Warp *w = warp_create(0);
    ASSERT_NOT_NULL(w);

    warp_set_active_mask(w, 0x0000FFFF);
    ASSERT_EQ(w->num_active, 16, "%d");

    warp_destroy(w);
    PASS();
}

static void test_warp_divergence(void) {
    TEST("warp_push/pop_divergence");
    Warp *w = warp_create(0);
    ASSERT_NOT_NULL(w);

    warp_push_divergence(w, 0x000000FF, 10);
    ASSERT_TRUE(w->diverged);
    ASSERT_EQ(w->div_depth, 1, "%d");

    warp_pop_divergence(w);
    ASSERT_EQ(w->div_depth, 0, "%d");
    ASSERT_TRUE(!w->diverged);

    warp_destroy(w);
    PASS();
}

static void test_warp_vote(void) {
    TEST("warp_vote(VOTE_BALLOT)");
    Warp *w = warp_create(0);
    ASSERT_NOT_NULL(w);

    uint32_t ballot = warp_vote(w, VOTE_BALLOT, true);
    ASSERT_EQ(ballot, 0xFFFFFFFF, "0x%08X");

    warp_destroy(w);
    PASS();
}

static void test_warp_issue(void) {
    TEST("warp_issue_instr");
    Warp *w = warp_create(0);
    ASSERT_NOT_NULL(w);

    int issued = warp_issue_instr(w);
    ASSERT_EQ(issued, 32, "%d");
    ASSERT_EQ((unsigned long long)w->issue_count, 1ULL, "%llu");

    warp_destroy(w);
    PASS();
}

static void test_warp_stall(void) {
    TEST("warp_stall/warp_unstall");
    Warp *w = warp_create(0);
    ASSERT_NOT_NULL(w);

    warp_stall_memory(w);
    ASSERT_TRUE(warp_is_stalled(w));
    warp_unstall_memory(w);
    ASSERT_TRUE(!warp_is_stalled(w));

    warp_destroy(w);
    PASS();
}

/* ===================================================================
 * Shader Core Tests
 * =================================================================== */
static void test_sm_create(void) {
    TEST("sm_create(0, SM_CC_80)");
    ShaderCore *sm = sm_create(0, SM_CC_80);
    ASSERT_NOT_NULL(sm);
    ASSERT_EQ(sm->sm_id, 0, "%d");
    ASSERT_EQ(sm->compute_cap, SM_CC_80, "%d");
    sm_destroy(sm);
    PASS();
}

static void test_sm_alloc_block(void) {
    TEST("sm_allocate_block(simple)");
    ShaderCore *sm = sm_create(0, SM_CC_80);
    ASSERT_NOT_NULL(sm);

    int bid = sm_allocate_block(sm, 32, 1, 1, 1024, 32);
    ASSERT_TRUE(bid >= 0);
    ASSERT_EQ(sm->num_blocks, 1, "%d");
    ASSERT_TRUE(sm->used_registers > 0);

    sm_deallocate_block(sm, bid);
    ASSERT_EQ(sm->num_blocks, 0, "%d");

    sm_destroy(sm);
    PASS();
}

static void test_sm_occupancy(void) {
    TEST("sm_calc_occupancy(128 threads, 32 regs)");
    OccupancyConfig cfg = sm_default_config(SM_CC_80);
    cfg.threads_per_block = 128;
    cfg.registers_per_thread = 32;
    cfg.shared_mem_per_block_bytes = 8192;

    OccupancyResult r = sm_calc_occupancy(&cfg);
    ASSERT_TRUE(r.active_warps > 0);
    ASSERT_TRUE(r.occupancy > 0.0 && r.occupancy <= 1.0);

    PASS();
}

static void test_sm_pipeline_cycle(void) {
    TEST("sm_cycle advances pipeline");
    ShaderCore *sm = sm_create(0, SM_CC_80);
    ASSERT_NOT_NULL(sm);

    /* Run 10 cycles */
    for (int i = 0; i < 10; i++) {
        sm_cycle(sm);
    }
    ASSERT_EQ((unsigned long long)sm->cycles, 10ULL, "%llu");

    sm_destroy(sm);
    PASS();
}

static void test_bank_analyze(void) {
    TEST("bank_analyze(no conflict)");
    int addrs[32];
    for (int i = 0; i < 32; i++) addrs[i] = i * 4; /* each thread hits different bank */

    BankAnalyzer ba = bank_analyze(addrs, 32, 32, 4);
    ASSERT_EQ(ba.conflict, BANK_NO_CONFLICT, "%d");

    /* All threads access same bank */
    for (int i = 0; i < 32; i++) addrs[i] = 128; /* same bank */
    ba = bank_analyze(addrs, 32, 32, 4);
    ASSERT_EQ(ba.conflict, BANK_FULL, "%d");

    PASS();
}

static void test_littles_law(void) {
    TEST("littles_law_sm_model(0.25, 6)");
    LittlesLawModel m = littles_law_sm_model(0.25, 6);
    ASSERT_FLOAT_EQ(m.occupancy_law, 1.5);
    PASS();
}

static void test_roofline(void) {
    TEST("roofline_evaluate(10, 100, 900)");
    RooflinePoint rp = roofline_evaluate(10.0, 100.0, 900.0);
    /* OI * BW = 10 * 900 = 9000 > 100 compute peak → compute_bound */
    ASSERT_TRUE(rp.compute_bound);
    ASSERT_FLOAT_EQ(rp.achievable_perf, 100.0);
    PASS();
}

/* ===================================================================
 * Tensor Core Tests
 * =================================================================== */
static void test_tc_create(void) {
    TEST("tc_create(0, MMA_M8N8K4, MMA_FP16)");
    TensorCore *tc = tc_create(0, MMA_M8N8K4, MMA_FP16);
    ASSERT_NOT_NULL(tc);
    ASSERT_EQ(tc->m, (uint8_t)8, "%u");
    ASSERT_EQ(tc->n, (uint8_t)8, "%u");
    ASSERT_EQ(tc->k, (uint8_t)4, "%u");
    tc_destroy(tc);
    PASS();
}

static void test_tc_mma(void) {
    TEST("tc_mma_compute(4x4 GEMM)");
    TensorCore *tc = tc_create(0, MMA_M8N8K4, MMA_FP16);
    ASSERT_NOT_NULL(tc);

    float A[4*4] = {1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16};
    float B[4*4] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float C[4*4] = {0};
    float D[4*4] = {0};

    bool ok = tc_mma_compute(tc, A, B, C, D, 4, 4, 4);
    ASSERT_TRUE(ok);
    /* A * I = A */
    ASSERT_FLOAT_EQ(D[0], 1.0f);
    ASSERT_FLOAT_EQ(D[5], 6.0f);   /* row 1, col 1 */
    ASSERT_FLOAT_EQ(D[15], 16.0f); /* last element */

    tc_destroy(tc);
    PASS();
}

static void test_tc_ops_count(void) {
    TEST("tc_mma_compute counts FLOPs");
    TensorCore *tc = tc_create(0, MMA_M8N8K4, MMA_FP16);
    ASSERT_NOT_NULL(tc);

    float A[64] = {0}, B[64] = {0}, C[64] = {0}, D[64] = {0};
    tc_mma_compute(tc, A, B, C, D, 8, 8, 4);

    /* 2 * 8 * 8 * 4 = 512 FLOPs */
    ASSERT_EQ((unsigned long long)tc->ops_completed, 512ULL, "%llu");

    tc_destroy(tc);
    PASS();
}

static void test_gemm_block_config(void) {
    TEST("gemm_block_config(64, 64, 64, MMA_M8N8K4)");
    GEMMBlockConfig cfg = gemm_block_config(64, 64, 64, MMA_M8N8K4);
    ASSERT_EQ(cfg.M_blocks, 8, "%d");
    ASSERT_EQ(cfg.N_blocks, 8, "%d");
    ASSERT_EQ(cfg.K_blocks, 16, "%d");
    PASS();
}

static void test_fp8_conversion(void) {
    TEST("fp8_to_float / float_to_fp8 roundtrip");
    /* FP8 E4M3: 1.0 = 0b0_0111_000 = 0x38 */
    uint8_t fp8_one = 0x38;
    float f = fp8_to_float(fp8_one, false);
    ASSERT_TRUE(f > 0.9f && f < 1.1f);

    uint8_t back = float_to_fp8(f, false);
    ASSERT_EQ(back, fp8_one, "0x%02X");

    PASS();
}

static void test_bf16_conversion(void) {
    TEST("bf16_to_float(1.0)");
    /* BF16 1.0 = 0x3F80 */
    uint16_t bf16_one = 0x3F80;
    float f = bf16_to_float(bf16_one);
    ASSERT_FLOAT_EQ(f, 1.0f);

    uint16_t back = float_to_bf16(1.0f);
    ASSERT_EQ(back, bf16_one, "0x%04X");

    PASS();
}

static void test_tensor_roofline(void) {
    TEST("tensor_roofline_eval(MM_FP16, oi=50, bw=1555, SM_CC_80)");
    TensorRoofline tr = tensor_roofline_eval(MMA_FP16, 50.0, 1555.0, SM_CC_80);
    ASSERT_TRUE(tr.tensor_peak_tflops > 0.0);
    ASSERT_TRUE(tr.speedup_vs_cuda > 10.0); /* tensor cores >> CUDA cores */
    PASS();
}

/* ===================================================================
 * Memory GPU Tests
 * =================================================================== */
static void test_gpu_mem_create(void) {
    TEST("gpu_mem_create(1GB, 32KB, 2MB)");
    GPUMemorySubsystem *m = gpu_mem_create(1024ULL*1024*1024, 32768, 2*1024*1024);
    ASSERT_NOT_NULL(m);
    ASSERT_NOT_NULL(m->global_mem.data);
    gpu_mem_destroy(m);
    PASS();
}

static void test_gpu_mem_read_write(void) {
    TEST("gpu_mem_read/write global memory");
    GPUMemorySubsystem *m = gpu_mem_create(1024*1024, 32768, 65536);
    ASSERT_NOT_NULL(m);

    float wdata[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    bool ok = gpu_mem_write(m, 0, wdata, 16, MEM_GLOBAL, 0, 0);
    ASSERT_TRUE(ok);

    float rdata[4] = {0};
    ok = gpu_mem_read(m, 0, rdata, 16, MEM_GLOBAL, 0, 0);
    ASSERT_TRUE(ok);
    ASSERT_FLOAT_EQ(rdata[0], 1.0f);
    ASSERT_FLOAT_EQ(rdata[3], 4.0f);

    gpu_mem_destroy(m);
    PASS();
}

static void test_cache_ops(void) {
    TEST("cache_access and hit rate");
    GPUMemorySubsystem *m = gpu_mem_create(1024*1024, 32768, 65536);
    ASSERT_NOT_NULL(m);

    /* Access same address multiple times */
    for (int i = 0; i < 10; i++) {
        float val;
        gpu_mem_read(m, 64, &val, 4, MEM_GLOBAL, 0, 0);
    }

    double hr = cache_hit_rate(&m->l1_cache);
    ASSERT_TRUE(hr > 0.5); /* most should be hits after first miss */

    gpu_mem_destroy(m);
    PASS();
}

static void test_coalesce_analyze_contiguous(void) {
    TEST("coalesce_analyze contiguous 32 threads");
    uint32_t addrs[32];
    for (int i = 0; i < 32; i++) addrs[i] = (uint32_t)(i * 4); /* 0,4,8,... */

    CoalescingAnalysis ca = coalesce_analyze(addrs, 32, 128);
    ASSERT_EQ(ca.is_coalesced, true, "%d");
    ASSERT_FLOAT_EQ(ca.coalescing_efficiency, 1.0f);

    PASS();
}

static void test_coalesce_analyze_strided(void) {
    TEST("coalesce_analyze strided access");
    uint32_t addrs[32];
    for (int i = 0; i < 32; i++) addrs[i] = (uint32_t)(i * 1024); /* 1KB stride */

    CoalescingAnalysis ca = coalesce_analyze(addrs, 32, 128);
    ASSERT_TRUE(ca.coalescing_efficiency < 0.5); /* very poor */

    PASS();
}

static void test_shared_bank_analyze_conflict(void) {
    TEST("shared_bank_analyze full conflict");
    uint32_t addrs[32];
    for (int i = 0; i < 32; i++) addrs[i] = 128; /* all same bank */

    BankConflictAnalysis bca = shared_bank_analyze(addrs, 32, 32, 4);
    ASSERT_EQ(bca.max_conflict, 32, "%d");
    ASSERT_TRUE(bca.efficiency < 0.1);

    PASS();
}

static void test_tlb(void) {
    TEST("tlb_lookup/tlb_insert");
    GPUMemorySubsystem *m = gpu_mem_create(1024*1024, 32768, 65536);
    ASSERT_NOT_NULL(m);

    tlb_insert(&m->tlb, 0x1000, 0x2000, MEM_GLOBAL);
    uint64_t ppn;
    GPUMemSpace sp;
    bool hit = tlb_lookup(&m->tlb, 0x1000, &ppn, &sp);
    ASSERT_TRUE(hit);
    ASSERT_EQ(ppn, (uint64_t)0x2000, "0x%llX");

    gpu_mem_destroy(m);
    PASS();
}

static void test_smem(void) {
    TEST("smem_store/smem_load");
    GPUMemorySubsystem *m = gpu_mem_create(1024*1024, 32768, 65536);
    ASSERT_NOT_NULL(m);

    smem_store(&m->shared_mem, 0, 0, 42.0f);
    float v = smem_load(&m->shared_mem, 0, 0);
    ASSERT_FLOAT_EQ(v, 42.0f);

    gpu_mem_destroy(m);
    PASS();
}

/* ===================================================================
 * Thread Scheduler Tests
 * =================================================================== */
static void test_grid_create(void) {
    TEST("grid_create(0, {2,2,1}, {32,1,1})");
    Dim3 gd = {2, 2, 1};
    Dim3 bd = {32, 1, 1};
    KernelGrid *g = grid_create(0, gd, bd);
    ASSERT_NOT_NULL(g);
    ASSERT_EQ(g->total_blocks, 4, "%d");
    ASSERT_EQ(g->blocks[0].num_threads, 32, "%d");
    grid_destroy(g);
    PASS();
}

static void test_gte_create(void) {
    TEST("gte_create(8)");
    GigaThreadEngine *gte = gte_create(8);
    ASSERT_NOT_NULL(gte);
    ASSERT_EQ(gte->num_sms, 8, "%d");
    gte_destroy(gte);
    PASS();
}

static void test_gte_schedule_blocks(void) {
    TEST("gte_schedule_blocks round-robin");
    GigaThreadEngine *gte = gte_create(4);
    ASSERT_NOT_NULL(gte);

    Dim3 gd = {4, 1, 1};
    Dim3 bd = {64, 1, 1};
    KernelGrid *g = grid_create(0, gd, bd);
    ASSERT_NOT_NULL(g);

    int gid = gte_submit_grid(gte, g);
    ASSERT_TRUE(gid >= 0);

    /* Need to copy grid data before scheduling since g is on stack */
    int sched = gte_schedule_blocks(gte);
    ASSERT_EQ(sched, 4, "%d");

    gte_destroy(gte);
    grid_destroy(g);
    PASS();
}

static void test_ws_create(void) {
    TEST("ws_create(0, 4)");
    WarpScheduler *ws = ws_create(0, 4);
    ASSERT_NOT_NULL(ws);
    ASSERT_EQ(ws->issue_width, 4, "%d");
    ws_destroy(ws);
    PASS();
}

static void test_ws_enqueue_dequeue(void) {
    TEST("ws_enqueue_warp / ws_dequeue_warp");
    WarpScheduler *ws = ws_create(0, 4);
    ASSERT_NOT_NULL(ws);

    ws_enqueue_warp(ws, 5);
    ws_enqueue_warp(ws, 10);
    ASSERT_EQ(ws_available_warps(ws), 2, "%d");

    int w0 = ws_dequeue_warp(ws);
    int w1 = ws_dequeue_warp(ws);
    ASSERT_EQ(w0, 5, "%d");
    ASSERT_EQ(w1, 10, "%d");

    ws_destroy(ws);
    PASS();
}

static void test_ws_schedule(void) {
    TEST("ws_schedule_cycle(GTO)");
    WarpScheduler *ws = ws_create(0, 4);
    ASSERT_NOT_NULL(ws);

    ws_enqueue_warp(ws, 0);
    ws_enqueue_warp(ws, 1);

    int issued[4] = {0};
    int count = ws_schedule_cycle(ws, issued, 2);
    ASSERT_TRUE(count > 0);

    ws_destroy(ws);
    PASS();
}

static void test_list_schedule(void) {
    TEST("list_schedule(4 tasks, 2 SMs)");
    SchedTask tasks[4];
    for (int i = 0; i < 4; i++) {
        tasks[i].task_id = i;
        tasks[i].duration = 10;
    }

    SchedResult *r = list_schedule(tasks, 4, 2);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r->makespan <= 20); /* 4*10 = 40 work / 2 SMs = 20 */
    ASSERT_TRUE(r->efficiency >= 0.8);

    sched_result_free(r);
    PASS();
}

static void test_makespan_bound(void) {
    TEST("makespan_lower_bound");
    SchedTask tasks[3];
    tasks[0].duration = 5;
    tasks[1].duration = 5;
    tasks[2].duration = 20; /* critical path */

    double lb = makespan_lower_bound(tasks, 3, 2);
    ASSERT_FLOAT_EQ(lb, 20.0); /* max(avg=10, critical=20) = 20 */

    PASS();
}

static void test_block_schedule_rr(void) {
    TEST("block_schedule_round_robin");
    Dim3 gd = {4, 1, 1};
    Dim3 bd = {32, 1, 1};
    KernelGrid *g = grid_create(0, gd, bd);
    ASSERT_NOT_NULL(g);

    int smq[4] = {0};
    int sched = block_schedule_round_robin(g, 4, smq);
    ASSERT_EQ(sched, 4, "%d");
    /* Each SM should get 1 block */
    for (int i = 0; i < 4; i++) ASSERT_EQ(smq[i], 1, "%d");

    grid_destroy(g);
    PASS();
}

/* ===================================================================
 * Main Test Runner
 * =================================================================== */
int main(int argc, char **argv) {
    (void)argc; (void)argv;

    printf("========================================\n");
    printf("  mini-gpu-arch Test Suite\n");
    printf("  CMU 15-418 · Stanford CS149 · UMich EECS 570\n");
    printf("========================================\n\n");

    /* SIMD Tests */
    printf("[1] SIMD Unit Tests\n");
    test_simd_create();
    test_simd_create_invalid();
    test_simd_vop_add();
    test_simd_vfma();
    test_simd_vreduce_sum();
    test_simd_predication();
    test_amdahl_law();
    test_simd_coalesce();

    /* Warp Tests */
    printf("\n[2] Warp Tests\n");
    test_warp_create();
    test_warp_active_mask();
    test_warp_divergence();
    test_warp_vote();
    test_warp_issue();
    test_warp_stall();

    /* Shader Core Tests */
    printf("\n[3] Shader Core Tests\n");
    test_sm_create();
    test_sm_alloc_block();
    test_sm_occupancy();
    test_sm_pipeline_cycle();
    test_bank_analyze();
    test_littles_law();
    test_roofline();

    /* Tensor Core Tests */
    printf("\n[4] Tensor Core Tests\n");
    test_tc_create();
    test_tc_mma();
    test_tc_ops_count();
    test_gemm_block_config();
    test_fp8_conversion();
    test_bf16_conversion();
    test_tensor_roofline();

    /* Memory GPU Tests */
    printf("\n[5] GPU Memory Tests\n");
    test_gpu_mem_create();
    test_gpu_mem_read_write();
    test_cache_ops();
    test_coalesce_analyze_contiguous();
    test_coalesce_analyze_strided();
    test_shared_bank_analyze_conflict();
    test_tlb();
    test_smem();

    /* Thread Scheduler Tests */
    printf("\n[6] Thread Scheduler Tests\n");
    test_grid_create();
    test_gte_create();
    test_gte_schedule_blocks();
    test_ws_create();
    test_ws_enqueue_dequeue();
    test_ws_schedule();
    test_list_schedule();
    test_makespan_bound();
    test_block_schedule_rr();

    /* Summary */
    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    if (tests_failed > 0) {
        printf("SOME TESTS FAILED!\n");
        return 1;
    }

    printf("ALL TESTS PASSED!\n");
    return 0;
}
