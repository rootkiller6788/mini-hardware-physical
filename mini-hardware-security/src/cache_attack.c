#include "cache_attack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int cache_state[CACHE_NUM_SETS][CACHE_NUM_WAYS];
static int cache_tags[CACHE_NUM_SETS][CACHE_NUM_WAYS];
static int cache_lru[CACHE_NUM_SETS][CACHE_NUM_WAYS];

static void cache_reset(void) {
    for (int s = 0; s < CACHE_NUM_SETS; s++) {
        for (int w = 0; w < CACHE_NUM_WAYS; w++) {
            cache_state[s][w] = 0;
            cache_tags[s][w] = 0;
            cache_lru[s][w] = 0;
        }
    }
}

static int get_set(uint64_t address) {
    return (int)((address / CACHE_LINE_SIZE) % CACHE_NUM_SETS);
}

static int get_tag(uint64_t address) {
    return (int)(address / (CACHE_LINE_SIZE * CACHE_NUM_SETS));
}

static int cache_lookup(uint64_t address, int *way_out) {
    int set = get_set(address);
    int tag = get_tag(address);
    for (int w = 0; w < CACHE_NUM_WAYS; w++) {
        if (cache_state[set][w] > 0 && cache_tags[set][w] == tag) {
            if (way_out) *way_out = w;
            return CACHE_L1_HIT_TIME;
        }
    }
    return CACHE_DRAM_TIME;
}

static void cache_flush_line(uint64_t address) {
    int set = get_set(address);
    int tag = get_tag(address);
    for (int w = 0; w < CACHE_NUM_WAYS; w++) {
        if (cache_tags[set][w] == tag) {
            cache_state[set][w] = 0;
            cache_tags[set][w] = 0;
        }
    }
}

static void cache_load_line(uint64_t address) {
    int set = get_set(address);
    int tag = get_tag(address);
    for (int w = 0; w < CACHE_NUM_WAYS; w++) {
        if (cache_state[set][w] == 0) {
            cache_state[set][w] = 1;
            cache_tags[set][w] = tag;
            cache_lru[set][w] = 0;
            return;
        }
    }
    int lru_way = 0;
    int lru_max = cache_lru[set][0];
    for (int w = 1; w < CACHE_NUM_WAYS; w++) {
        if (cache_lru[set][w] > lru_max) {
            lru_max = cache_lru[set][w];
            lru_way = w;
        }
    }
    cache_tags[set][lru_way] = tag;
    cache_lru[set][lru_way] = 0;
    for (int w = 0; w < CACHE_NUM_WAYS; w++) {
        if (w != lru_way && cache_state[set][w] >= 0) {
            cache_lru[set][w]++;
        }
    }
}

void cache_sim_flush(uintptr_t address) {
    cache_flush_line((uint64_t)address);
}

int cache_sim_reload(uintptr_t address) {
    int way;
    int time_val = cache_lookup((uint64_t)address, &way);
    cache_load_line((uint64_t)address);
    return time_val;
}

void cache_sim_access(uintptr_t address) {
    cache_load_line((uint64_t)address);
}

void cache_attack_victim_access(CacheAttackVictim *vic, int secret_index) {
    if (secret_index < 0 || secret_index >= CACHE_SECRET_SIZE) return;
    uint64_t addr = (uint64_t)(uintptr_t)&vic->probe_array[secret_index * CACHE_LINE_SIZE];
    cache_load_line(addr);
}

void cache_attack_flush_reload(int *secret_out) {
    cache_reset();
    CacheAttackVictim vic;
    memset(&vic, 0, sizeof(vic));
    for (int i = 0; i < CACHE_SECRET_SIZE; i++) {
        vic.secret_array[i] = (uint8_t)(i + 1);
    }
    int secret_idx = rand() % CACHE_SECRET_SIZE;

    for (int i = 0; i < CACHE_SECRET_SIZE; i++) {
        uint64_t addr = (uint64_t)(uintptr_t)&vic.probe_array[i * CACHE_LINE_SIZE];
        cache_flush_line(addr);
    }

    cache_attack_victim_access(&vic, secret_idx);

    int best_idx = -1;
    int lowest_time = CACHE_DRAM_TIME + 1;
    for (int i = 0; i < CACHE_SECRET_SIZE; i++) {
        uint64_t addr = (uint64_t)(uintptr_t)&vic.probe_array[i * CACHE_LINE_SIZE];
        int time_val = cache_sim_reload((int)addr);
        if (time_val < lowest_time) {
            lowest_time = time_val;
            best_idx = i;
        }
    }
    *secret_out = best_idx;
}

void cache_attack_prime_probe(int eviction_set[], int *secret_out) {
    cache_reset();
    for (int i = 0; i < CACHE_NUM_SETS; i++) {
        cache_attack_flush_reload(secret_out);
        eviction_set[i] = i;
    }
}

void cache_attack_evict_time(int *access_time_out) {
    cache_reset();
    CacheAttackVictim vic;
    memset(&vic, 0, sizeof(vic));
    uint64_t addr = (uint64_t)(uintptr_t)&vic.probe_array[0];
    cache_load_line(addr);
    int time1 = cache_lookup(addr, NULL);
    for (int i = 0; i < CACHE_NUM_SETS * CACHE_NUM_WAYS; i++) {
        cache_load_line((uint64_t)(uintptr_t)&vic.probe_array[
            (i * CACHE_LINE_SIZE) % (CACHE_SECRET_SIZE * CACHE_LINE_SIZE)]);
    }
    int time2 = cache_lookup(addr, NULL);
    *access_time_out = time2;
    (void)time1;
}

void cache_attack_run_flush_reload(CacheAttackVictim *vic,
                                    CacheAttackAttacker *atk,
                                    int *secret_out) {
    cache_reset();
    for (int i = 0; i < CACHE_SECRET_SIZE; i++) {
        uint64_t addr = (uint64_t)(uintptr_t)&vic->probe_array[i * CACHE_LINE_SIZE];
        cache_flush_line(addr);
    }
    if (vic->secret_func) {
        int secret_idx = rand() % CACHE_SECRET_SIZE;
        vic->secret_func(secret_idx);
    }
    int best_idx = -1;
    int lowest_time = CACHE_DRAM_TIME + 1;
    for (int i = 0; i < CACHE_SECRET_SIZE; i++) {
        int time_val = cache_sim_reload(
            (int)(uintptr_t)&vic->probe_array[i * CACHE_LINE_SIZE]);
        if (time_val < lowest_time) {
            lowest_time = time_val;
            best_idx = i;
        }
        atk->observed_timings[i % CACHE_NUM_SETS] = time_val;
    }
    atk->cache_miss_threshold = (CACHE_DRAM_TIME + CACHE_L3_HIT_TIME) / 2;
    *secret_out = best_idx;
}
