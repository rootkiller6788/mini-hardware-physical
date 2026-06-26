#include "spec_exec_atk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t probe_array[SPEC_CACHE_LINES * 64];
static uint8_t secret_data[256];

static void flush_all(SpecExecCPU *cpu) {
    for (int i = 0; i < SPEC_CACHE_LINES; i++) {
        cpu->cache_access_times[i] = 100;
    }
    memset(probe_array, 0, sizeof(probe_array));
}

static void load_cache_line(SpecExecCPU *cpu, int idx) {
    if (idx >= 0 && idx < SPEC_CACHE_LINES) {
        cpu->cache_access_times[idx] = 4;
    }
}

static int probe_cache_line(SpecExecCPU *cpu, int idx) {
    int time_val = 100;
    if (idx >= 0 && idx < SPEC_CACHE_LINES) {
        time_val = cpu->cache_access_times[idx];
        cpu->cache_access_times[idx] = 4;
    }
    return time_val;
}

int main(void) {
    printf("================================================================\n");
    printf("  Spectre v1 (Bounds Check Bypass) Attack Simulation\n");
    printf("================================================================\n\n");

    SpecExecCPU cpu;
    spec_exec_cpu_init(&cpu);

    for (int i = 0; i < 256; i++) {
        secret_data[i] = (uint8_t)((i * 17 + 43) & 0xFF);
    }
    int array1_size = 16;

    printf("[INFO] System Configuration:\n");
    printf("  Registers: %d (arch) + %d (speculative)\n",
           SPEC_NUM_REGS, SPEC_NUM_REGS);
    printf("  BTB entries: %d\n", SPEC_BTB_ENTRIES);
    printf("  ROB entries: %d\n", SPEC_ROB_ENTRIES);
    printf("  Probe array: %d cache lines\n\n", SPEC_CACHE_LINES);

    for (int i = 0; i < array1_size; i++) {
        printf("  secret_data[%2d] = 0x%02X\n", i, secret_data[i]);
    }
    printf("  ... (secret_data[%d..%d] are OUT-OF-BOUNDS)\n\n",
           array1_size, 255);

    printf("[PHASE 1] Training the branch predictor (5 in-bounds accesses)...\n");
    for (int round = 0; round < 5; round++) {
        int valid_x = round % array1_size;
        spec_exec_btb_train(&cpu, 0x1000, 0x1004);
        printf("  Round %d: x=%d (in-bounds), branch TAKEN\n", round + 1, valid_x);

        uint8_t val = secret_data[valid_x];
        printf("    -> Read secret_data[%d] = 0x%02X\n", valid_x, val);
        int cache_line = (int)(val & 0x0F);
        load_cache_line(&cpu, cache_line);
    }
    printf("  -> Branch predictor NOW PREDICTS: TAKEN (in-bounds path)\n\n");

    printf("[PHASE 2] Cache flush: clearing probe array from cache...\n");
    flush_all(&cpu);
    printf("  -> All %d cache lines flushed\n\n", SPEC_CACHE_LINES);

    int malicious_x = array1_size + 8;
    printf("[PHASE 3] Attack: x = %d (OUT-OF-BOUNDS)\n", malicious_x);
    printf("  Step 1: bounds check for x < %d...\n", array1_size);
    printf("  Step 2: branch predictor says TAKEN (in-bounds)\n");
    printf("  Step 3: CPU SPECULATIVELY executes:\n");
    printf("    val = secret_data[%d]  <-- OUT-OF-BOUNDS, but executed speculatively!\n",
           malicious_x);

    uint8_t spec_val = secret_data[malicious_x];
    int spec_cache_line = (int)(spec_val & 0x0F);
    printf("    speculative_val = 0x%02X\n", spec_val);
    printf("    probe_array[%d * 4096] is LOADED into cache (speculatively)\n\n",
           spec_cache_line);

    if (spec_exec_btb_predict(&cpu, 0x1000) == 0x1004) {
        load_cache_line(&cpu, spec_cache_line);
    }

    printf("  Step 4: Bounds check COMPLETES -> out-of-bounds -> EXCEPTION\n");
    printf("  Step 5: Architectural state ROLLED BACK (x reverted, flags cleared)\n");
    printf("  Step 6: BUT cache state REMAINS (microarchitectural side effect!)\n\n");

    printf("[PHASE 4] Covert channel: probe cache to determine leaked nibble...\n");
    printf("  %-5s %-10s %s\n", "Line", "Time(ns)", "Status");
    printf("  %-5s %-10s %s\n", "----", "--------", "------");

    int best_idx = -1;
    int lowest_time = 200;

    for (int i = 0; i < 16; i++) {
        int time_val = probe_cache_line(&cpu, i);
        const char *status = (time_val < 50) ? "HIT" : "MISS";
        printf("  %-5d %-10d %s", i, time_val, status);
        if (time_val < lowest_time) {
            lowest_time = time_val;
            best_idx = i;
        }
        if (time_val < 50) printf(" <-- SECRET NIBBLE LEAKED!");
        printf("\n");
    }

    uint8_t leaked_low_nibble = (uint8_t)best_idx;
    uint8_t actual_val = secret_data[malicious_x];

    printf("\n================================================================\n");
    printf("  SPECTRE v1 ATTACK RESULT\n");
    printf("================================================================\n");
    printf("  Malicious address     : secret_data[%d]\n", malicious_x);
    printf("  Actual secret value   : 0x%02X\n", actual_val);
    printf("  Leaked low nibble     : 0x%X (line %d, time %d ns)\n",
           leaked_low_nibble, best_idx, lowest_time);
    printf("  Diff                  : actual=0x%X, leaked=0x%X\n",
           actual_val & 0x0F, leaked_low_nibble);

    if ((actual_val & 0x0F) == leaked_low_nibble) {
        printf("  STATUS                : *** ATTACK SUCCEEDED ***\n");
    } else {
        printf("  STATUS                : Partial leak (need more iterations)\n");
    }

    printf("\n[MITIGATION] Spectre v1 is mitigated by:\n");
    printf("  - lfence: serialize instruction (stop speculation)\n");
    printf("  - index masking: mask = array_size - 1; x &= mask;\n");
    printf("  - Retpoline: replace indirect branches with return trampolines\n");
    printf("  - ARM: CSDB barrier instruction\n\n");

    return 0;
}
