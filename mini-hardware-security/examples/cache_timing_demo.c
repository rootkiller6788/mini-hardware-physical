#include "cache_attack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void) {
    printf("========================================================\n");
    printf("  Flush+Reload Cache Timing Attack Demonstration\n");
    printf("========================================================\n\n");

    printf("[INFO] Initializing simulated cache...\n");
    printf("  Cache configuration: %d sets x %d ways, line size: %d bytes\n",
           CACHE_NUM_SETS, CACHE_NUM_WAYS, CACHE_LINE_SIZE);

    CacheAttackVictim victim;
    memset(&victim, 0, sizeof(victim));

    for (int i = 0; i < CACHE_SECRET_SIZE; i++) {
        victim.secret_array[i] = (uint8_t)((i * 7 + 13) & 0xFF);
    }

    printf("[INFO] Populated victim's secret array with %d entries\n",
           CACHE_SECRET_SIZE);
    printf("[INFO] Probe array size: %d bytes (%d x %d)\n\n",
           CACHE_PROBE_SIZE, CACHE_SECRET_SIZE, CACHE_LINE_SIZE);

    srand(12345);
    int actual_secret_idx = rand() % CACHE_SECRET_SIZE;
    printf("[STEP 1] Victim randomly accesses secret at index: %d\n",
           actual_secret_idx);
    printf("  -> Secret value at index %d: 0x%02X\n\n",
           actual_secret_idx, victim.secret_array[actual_secret_idx]);

    printf("[STEP 2] FLUSH phase: Flushing all %d cache lines from shared memory...\n",
           CACHE_SECRET_SIZE);
    for (int i = 0; i < CACHE_SECRET_SIZE; i++) {
        cache_sim_flush((uintptr_t)&victim.probe_array[i * CACHE_LINE_SIZE]);
    }
    printf("  -> All cache lines flushed. Access time for any line: ~%d ns\n\n",
           CACHE_DRAM_TIME);

    printf("[STEP 3] VICTIM ACCESS: Victim accesses secret at index %d\n",
           actual_secret_idx);
    cache_attack_victim_access(&victim, actual_secret_idx);
    printf("  -> Cache line for secret index %d is now HOT in cache\n\n",
           actual_secret_idx);

    printf("[STEP 4] RELOAD phase: Attacker measures access times for all %d lines...\n",
           CACHE_SECRET_SIZE);
    printf("  %-12s %-8s %s\n", "Index", "Time(ns)", "Status");
    printf("  %-12s %-8s %s\n", "-----", "--------", "------");

    int best_idx = -1;
    int lowest_time = CACHE_DRAM_TIME + 1;

    for (int i = 0; i < CACHE_SECRET_SIZE; i++) {
        int time_val = cache_sim_reload(
            (uintptr_t)&victim.probe_array[i * CACHE_LINE_SIZE]);
        const char *status = (time_val < 50) ? "HIT" : "MISS";
        if (time_val < lowest_time) {
            lowest_time = time_val;
            best_idx = i;
        }
        if (i < 10 || i == best_idx || i == actual_secret_idx ||
            i >= CACHE_SECRET_SIZE - 5) {
            printf("  %-12d %-8d %s", i, time_val, status);
            if (i == best_idx) printf(" <-- LEAKED");
            if (i == actual_secret_idx) printf(" <-- ACTUAL");
            printf("\n");
        } else if (i == 10) {
            printf("  ... (%d entries omitted) ...\n",
                   CACHE_SECRET_SIZE - 20);
        }
    }

    printf("\n========================================================\n");
    printf("  ATTACK RESULT\n");
    printf("========================================================\n");
    printf("  Leaked secret index : %d\n", best_idx);
    printf("  Actual secret index : %d\n", actual_secret_idx);
    printf("  Attacksucceeded     : %s\n",
           (best_idx == actual_secret_idx) ? "YES" : "NO");
    printf("  Access time (leaked): %d ns (cache HIT)\n", lowest_time);
    printf("  Other lines         : ~%d ns (cache MISS)\n\n",
           CACHE_DRAM_TIME);

    printf("[ANALYSIS] Flush+Reload works because:\n");
    printf("  1. Attacker flushes shared memory from cache (clflush)\n");
    printf("  2. Victim accesses secret, loading the secret's cache line\n");
    printf("  3. Attacker reloads each line and measures timing\n");
    printf("  4. Fast access (L1 hit) = victim accessed that line = secret leaked\n");
    printf("  5. Requires shared memory between attacker and victim\n\n");

    int secret_out;
    cache_attack_flush_reload(&secret_out);
    printf("[VERIFY] Automated Flush+Reload returned: %d\n\n", secret_out);

    return 0;
}
