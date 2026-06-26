#ifndef SIDE_CHANNEL_H
#define SIDE_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>

#define SIDE_CHANNEL_MAX_TRACES    256
#define SIDE_CHANNEL_MAX_SAMPLES   1024
#define SIDE_CHANNEL_L1_HIT         4
#define SIDE_CHANNEL_L2_HIT        12
#define SIDE_CHANNEL_L3_HIT        40
#define SIDE_CHANNEL_DRAM          100
#define SIDE_CHANNEL_NUM_ADDRESSES 256

typedef enum {
    SC_CHANNEL_TIMING,
    SC_CHANNEL_POWER,
    SC_CHANNEL_EM,
    SC_CHANNEL_ACOUSTIC
} SideChannelType;

typedef struct {
    int access_times[SIDE_CHANNEL_NUM_ADDRESSES];
    int iterations;
    int threshold;
} TimingChannel;

typedef struct {
    uint8_t samples[SIDE_CHANNEL_MAX_SAMPLES];
    int sample_count;
    int sample_rate;
} PowerTrace;

typedef struct {
    uint8_t victim_secret;
    int attacker_observations[SIDE_CHANNEL_NUM_ADDRESSES];
    SideChannelType channel_type;
} SideChannel;

void sc_timing_init(TimingChannel *tc);
int  sc_timing_measure(TimingChannel *tc, int address);
void sc_timing_attack(TimingChannel *tc, int *secret_out);

void sc_power_simple(PowerTrace *pt, uint8_t key_byte, int iteration);
int  sc_correlation_power_analysis(PowerTrace *pt, uint8_t key_guess);
void sc_differential_power_analysis(PowerTrace traces[], int n_traces, uint8_t *key_out);

void sc_channel_init(SideChannel *sc, SideChannelType type, uint8_t secret);

#endif
