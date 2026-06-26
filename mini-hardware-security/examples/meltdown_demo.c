#include "spec_exec_atk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint8_t probe_array[SPEC_CACHE_LINES * 64];

static void flush_cache(SpecExecCPU *cpu) {
    for (int i = 0; i < SPEC_CACHE_LINES; i++) {
        cpu->cache_access_times[i] = 100;
    }
    memset(probe_array, 0, sizeof(probe_array));
}

static void cache_line_load(SpecExecCPU *cpu, int idx) {
    if (idx >= 0 && idx < SPEC_CACHE_LINES) {
        cpu->cache_access_times[idx] = 4;
    }
}

static int cache_line_probe(SpecExecCPU *cpu, int idx) {
    int time_val = 100;
    if (idx >= 0 && idx < SPEC_CACHE_LINES) {
        time_val = cpu->cache_access_times[idx];
        cpu->cache_access_times[idx] = 4;
    }
    return time_val;
}

int main(void) {
    printf("================================================================\n");
    printf("  Meltdown Attack Simulation\n");
    printf("================================================================\n\n");

    SpecExecCPU cpu;
    spec_exec_cpu_init(&cpu);

    uint32_t kernel_addr = 0xDEADC0DE;
    uint8_t kernel_secret = 0x73;

    printf("[INFO] Processor model:\n");
    printf("  Out-of-order execution: ENABLED\n");
    printf("  Speculative execution: ENABLED\n");
    printf("  Supervisor-mode access prevention (SMAP): ENABLED\n");
    printf("  Kernel page-table isolation (KPTI): DISABLED (vulnerable)\n\n");

    printf("[INFO] Memory layout:\n");
    printf("  User space:  0x00000000 - 0x00007FFFFFFFFFFF\n");
    printf("  Kernel space: 0xFFFF8000XXXXXXXX - 0xFFFFFFFFFFFFFFFF\n");
    printf("  Kernel address to probe: 0x%08X\n", kernel_addr);
    printf("  Kernel secret value:      0x%02X ('%c')\n\n",
           kernel_secret, kernel_secret);

    printf("[PHASE 1] Flush probe array from cache...\n");
    flush_cache(&cpu);
    printf("  All %d cache lines flushed (timing = %d ns baseline)\n\n",
           SPEC_CACHE_LINES, 100);

    printf("[PHASE 2] Attempt Meltdown read of kernel memory...\n");
    printf("  Transient instruction sequence:\n");
    printf("    1. char val = *(kernel_addr)     <-- #PF exception pending\n");
    printf("    2. probe_array[val * 4096]        <-- LOAD uses leaked val\n");
    printf("    3. Access depends on kernel byte  <-- Data dependency\n\n");

    printf("  Step 1: CPU fetches instruction *(0x%08X)\n", kernel_addr);
    printf("  Step 2: Memory access triggers page fault (user -> kernel)\n");
    printf("  Step 3: Before exception delivered, speculative execution continues...\n");
    printf("  Step 4: Value 0x%02X is loaded (transiently, in spec state)\n",
           kernel_secret);
    printf("  Step 5: probe_array[0x%02X * 4096] loaded into cache\n",
           kernel_secret);
    printf("  Step 6: Exception finally delivered -> SIGSEGV\n");
    printf("  Step 7: Architectural state rolled back (PC restored, regs cleared)\n");
    printf("  Step 8: CACHE STATE PRESERVES the secret! (probe line is hot)\n\n");

    int probe_line = (int)kernel_secret;
    cache_line_load(&cpu, probe_line);

    printf("[PHASE 3] Recover secret byte via cache timing side channel...\n");
    printf("  Probing all 256 cache lines by measuring access time:\n\n");
    printf("  %-6s %-10s %-12s %s\n", "Index", "Time(ns)", "Byte", "Status");
    printf("  %-6s %-10s %-12s %s\n", "------", "--------", "----------", "------");

    int best_idx = -1;
    int lowest_time = 200;

    for (int i = 0; i < 256; i++) {
        int time_val = cache_line_probe(&cpu, i);
        const char *status = (time_val < 50) ? "HIT" : "MISS";
        if (time_val < lowest_time) {
            lowest_time = time_val;
            best_idx = i;
        }
        if (i < 5 || time_val < 50 || i > 250) {
            printf("  %-6d %-10d 0x%02X", i, time_val, i);
            if (time_val < 50 && i == best_idx) {
                printf(" ('%c')", (i >= 32 && i < 127) ? (char)i : '?');
            }
            printf("        %s\n", status);
        } else if (i == 5) {
            printf("  ... (%d entries omitted) ...\n", 256 - 10);
        }
    }

    uint8_t leaked_byte = (uint8_t)best_idx;

    printf("\n================================================================\n");
    printf("  MELTDOWN ATTACK RESULT\n");
    printf("================================================================\n");
    printf("  Kernel address probed  : 0x%08X\n", kernel_addr);
    printf("  Actual kernel secret   : 0x%02X ('%c')\n",
           kernel_secret, kernel_secret);
    printf("  Leaked byte via cache  : 0x%02X ('%c') (line %d)\n",
           leaked_byte,
           (leaked_byte >= 32 && leaked_byte < 127) ? (char)leaked_byte : '?',
           best_idx);
    printf("  Access time            : %d ns\n", lowest_time);
    printf("  Timing delta           : %d ns (MISS would be ~%d ns)\n",
           100 - lowest_time, 100);

    if (leaked_byte == kernel_secret) {
        printf("  STATUS                 : *** FULL KERNEL BYTE LEAKED ***\n");
    } else {
        printf("  STATUS                 : Partial leak\n");
    }

    printf("\n[IMPACT] Meltdown allows:\n");
    printf("  - Reading ALL kernel memory from user space\n");
    printf("  - Stealing cryptographic keys, passwords, kernel data\n");
    printf("  - At 500 KB/s on vulnerable CPUs\n\n");

    printf("[MITIGATIONS]\n");
    printf("  - KPTI (Kernel Page-Table Isolation): separate user/kernel page tables\n");
    printf("  - CPU microcode updates (Intel, ARM)\n");
    printf("  - KAISER patch (OS-level KPTI)\n");
    printf("  - Hardware fixes: Cascade Lake, Whiskey Lake, newer CPUs\n\n");

    return 0;
}
