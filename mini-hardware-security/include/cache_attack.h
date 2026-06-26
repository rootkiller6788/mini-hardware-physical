#ifndef CACHE_ATTACK_H
#define CACHE_ATTACK_H

#include <stdbool.h>
#include <stdint.h>

#define CACHE_LINE_SIZE      64
#define CACHE_NUM_SETS       64
#define CACHE_NUM_WAYS        8
#define CACHE_SECRET_SIZE   256
#define CACHE_PROBE_SIZE     (CACHE_SECRET_SIZE * CACHE_LINE_SIZE)

#define CACHE_L1_HIT_TIME     4
#define CACHE_L2_HIT_TIME    12
#define CACHE_L3_HIT_TIME    40
#define CACHE_DRAM_TIME     100

typedef void (*CacheVictimFunc)(int secret_index);

typedef struct {
    uint8_t secret_array[CACHE_SECRET_SIZE];
    uint8_t probe_array[CACHE_PROBE_SIZE];
    CacheVictimFunc secret_func;
} CacheAttackVictim;

typedef struct {
    int probe_set[CACHE_NUM_SETS];
    int observed_timings[CACHE_NUM_SETS];
    int cache_miss_threshold;
} CacheAttackAttacker;

void cache_attack_flush_reload(int *secret_out);
void cache_attack_prime_probe(int eviction_set[], int *secret_out);
void cache_attack_evict_time(int *access_time_out);

void cache_sim_flush(uintptr_t address);
int  cache_sim_reload(uintptr_t address);
void cache_sim_access(uintptr_t address);

void cache_attack_victim_access(CacheAttackVictim *vic, int secret_index);
void cache_attack_run_flush_reload(CacheAttackVictim *vic,
                                   CacheAttackAttacker *atk,
                                   int *secret_out);

#endif
