#include "side_channel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define THRESHOLD_CACHE_HIT 50

static int simulated_cache[SIDE_CHANNEL_NUM_ADDRESSES];

void sc_channel_init(SideChannel *sc, SideChannelType type, uint8_t secret) {
    sc->victim_secret = secret;
    sc->channel_type = type;
    memset(sc->attacker_observations, 0, sizeof(sc->attacker_observations));
}

void sc_timing_init(TimingChannel *tc) {
    tc->iterations = 0;
    tc->threshold = THRESHOLD_CACHE_HIT;
    for (int i = 0; i < SIDE_CHANNEL_NUM_ADDRESSES; i++) {
        tc->access_times[i] = SIDE_CHANNEL_DRAM;
        simulated_cache[i] = 0;
    }
}

static int get_access_latency(int address) {
    int idx = address % SIDE_CHANNEL_NUM_ADDRESSES;
    if (simulated_cache[idx] == 1) {
        return SIDE_CHANNEL_L1_HIT;
    }
    return SIDE_CHANNEL_DRAM;
}

int sc_timing_measure(TimingChannel *tc, int address) {
    int idx = address % SIDE_CHANNEL_NUM_ADDRESSES;
    int time_val = get_access_latency(address);
    if (tc->iterations < SIDE_CHANNEL_MAX_TRACES) {
        tc->access_times[idx] = time_val;
    }
    tc->iterations++;
    return time_val;
}

static void cache_line_flush(int address) {
    int idx = address % SIDE_CHANNEL_NUM_ADDRESSES;
    simulated_cache[idx] = 0;
}

static void cache_line_load(int address) {
    int idx = address % SIDE_CHANNEL_NUM_ADDRESSES;
    simulated_cache[idx] = 1;
}

static void victim_access(int secret_index) {
    cache_line_load(secret_index);
}

void sc_timing_attack(TimingChannel *tc, int *secret_out) {
    int best_idx = -1;
    int lowest_time = SIDE_CHANNEL_DRAM + 1;

    for (int i = 0; i < SIDE_CHANNEL_NUM_ADDRESSES; i++) {
        cache_line_flush(i);
    }

    int secret_idx = 42;
    victim_access(secret_idx);

    for (int i = 0; i < SIDE_CHANNEL_NUM_ADDRESSES; i++) {
        int time_val = sc_timing_measure(tc, i);
        if (time_val < lowest_time) {
            lowest_time = time_val;
            best_idx = i;
        }
    }

    *secret_out = best_idx;
}

void sc_power_simple(PowerTrace *pt, uint8_t key_byte, int iteration) {
    if (iteration >= SIDE_CHANNEL_MAX_SAMPLES) return;
    pt->sample_rate = 1000000;
    int hw = 0;
    uint8_t val = key_byte;
    while (val) {
        hw += (val & 1);
        val >>= 1;
    }
    pt->samples[iteration] = (uint8_t)(hw * 20 + 10 + (rand() % 5));
    pt->sample_count++;
}

int sc_correlation_power_analysis(PowerTrace *pt, uint8_t key_guess) {
    int hw_guess = 0;
    uint8_t val = key_guess;
    while (val) {
        hw_guess += (val & 1);
        val >>= 1;
    }
    int expected = hw_guess * 20 + 10;
    int correlation = 0;
    int n = pt->sample_count;
    if (n == 0) return 0;
    for (int i = 0; i < n; i++) {
        correlation += abs((int)pt->samples[i] - expected);
    }
    return -correlation / n;
}

void sc_differential_power_analysis(PowerTrace traces[], int n_traces,
                                     uint8_t *key_out) {
    int best_key = 0;
    int best_corr = __INT_MAX__;
    for (int guess = 0; guess < 256; guess++) {
        int total_corr = 0;
        for (int t = 0; t < n_traces; t++) {
            total_corr += sc_correlation_power_analysis(&traces[t],
                                                         (uint8_t)guess);
        }
        if (total_corr < best_corr) {
            best_corr = total_corr;
            best_key = guess;
        }
    }
    *key_out = (uint8_t)best_key;
}
