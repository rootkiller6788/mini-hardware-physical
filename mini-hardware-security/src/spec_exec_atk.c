#include "spec_exec_atk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SPEC_L1_HIT 4

static uint8_t probe_array[SPEC_CACHE_LINES * 64];

void spec_exec_cpu_init(SpecExecCPU *cpu) {
    memset(cpu->arch_regs, 0, sizeof(cpu->arch_regs));
    memset(cpu->spec_regs, 0, sizeof(cpu->spec_regs));
    memset(cpu->btb, 0, sizeof(cpu->btb));
    memset(cpu->rob_entries, 0, sizeof(cpu->rob_entries));
    cpu->rob_head = 0;
    cpu->rob_tail = 0;
    cpu->flush_on_mispredict = true;
    for (int i = 0; i < SPEC_CACHE_LINES; i++) {
        cpu->cache_access_times[i] = SPEC_THRESHOLD + 50;
    }
}

void spec_exec_btb_train(SpecExecCPU *cpu, uint32_t pc, uint32_t target) {
    int idx = pc % SPEC_BTB_ENTRIES;
    cpu->btb[idx].tag = pc;
    cpu->btb[idx].target = target;
    cpu->btb[idx].valid = true;
}

uint32_t spec_exec_btb_predict(SpecExecCPU *cpu, uint32_t pc) {
    int idx = pc % SPEC_BTB_ENTRIES;
    if (cpu->btb[idx].valid && cpu->btb[idx].tag == pc) {
        return cpu->btb[idx].target;
    }
    return pc + 4;
}

void spec_exec_flush_cache(SpecExecCPU *cpu) {
    for (int i = 0; i < SPEC_CACHE_LINES; i++) {
        cpu->cache_access_times[i] = 100;
    }
}

int spec_exec_probe_cache(SpecExecCPU *cpu, int line) {
    if (line < 0 || line >= SPEC_CACHE_LINES) return 100;
    return cpu->cache_access_times[line];
}

static void cache_flush_all(SpecExecCPU *cpu) {
    for (int i = 0; i < SPEC_CACHE_LINES; i++) {
        cpu->cache_access_times[i] = 100;
        memset(&probe_array[i * 64], 0, 64);
    }
}

static void cache_line_load(SpecExecCPU *cpu, int line) {
    if (line >= 0 && line < SPEC_CACHE_LINES) {
        cpu->cache_access_times[line] = SPEC_L1_HIT;
    }
}

static int cache_line_probe(SpecExecCPU *cpu, int line) {
    int time_val = cpu->cache_access_times[line];
    cache_line_load(cpu, line);
    return time_val;
}

void spectre_v1_attack_detailed(SpecExecCPU *cpu, int *secret_out);

void spectre_v1_simulate(void) {
    SpecExecCPU cpu;
    spec_exec_cpu_init(&cpu);

    uint8_t secret_data[16] = {0x53, 0x45, 0x43, 0x52, 0x45, 0x54, 0x58, 0x59,
                                0x5a, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36};
    int array1_size = 8;
    int malicious_x;

    printf("=== Spectre v1 Simulation ===\n");
    printf("Secret data at address beyond valid bounds.\n");
    printf("array1_size = %d\n", array1_size);

    for (int training_round = 0; training_round < 5; training_round++) {
        int valid_x = training_round % array1_size;
        if ((uint32_t)valid_x < (uint32_t)array1_size) {
            uint8_t val = secret_data[valid_x];
            int line = (int)(val & 0x0F);
            cache_line_load(&cpu, line);
        }
    }

    cache_flush_all(&cpu);

    malicious_x = array1_size + 5;
    if (spec_exec_btb_predict(&cpu, 0x1000) == 0x1004) {
        uint8_t spec_val = secret_data[malicious_x];
        int spec_line = (int)(spec_val & 0x0F);
        cache_line_load(&cpu, spec_line);
    }

    int leaked_secret = -1;
    int lowest_time = 200;
    for (int i = 0; i < 16; i++) {
        int time_val = cache_line_probe(&cpu, i);
        if (time_val < lowest_time) {
            lowest_time = time_val;
            leaked_secret = i;
        }
    }

    printf("Leaked secret nibble (low 4 bits): %d (0x%X)\n",
           leaked_secret, leaked_secret);
    printf("Expected: %d (0x%X)\n", secret_data[malicious_x] & 0x0F,
           secret_data[malicious_x] & 0x0F);
}

void spectre_v1_transmit(uint8_t secret) {
    SpecExecCPU dummy;
    spec_exec_cpu_init(&dummy);
    printf("covert channel transmission of byte: 0x%02X\n", secret);
    for (int i = 0; i < 8; i++) {
        if (secret & (1 << i)) {
            cache_line_load(&dummy, i * 32);
        }
    }
}

void meltdown_simulate(SpecExecCPU *cpu, uint32_t kernel_addr) {
    printf("=== Meltdown Simulation ===\n");
    printf("Attempting to read kernel address: 0x%08X\n", kernel_addr);

    cache_flush_all(cpu);

    uint8_t kernel_byte = (uint8_t)(kernel_addr & 0xFF);
    int probe_line = kernel_byte & 0x0F;
    cache_line_load(cpu, probe_line);

    int leaked = -1;
    int lowest_time = 200;
    for (int i = 0; i < 16; i++) {
        int time_val = cache_line_probe(cpu, i);
        if (time_val < lowest_time) {
            lowest_time = time_val;
            leaked = i;
        }
    }

    printf("Leaked kernel byte: 0x%02X (low nibble: %d)\n",
           kernel_byte, leaked);
    printf("Exception handling simulated - transient instructions\n");
    printf("left cache state even though architectural state rolled back.\n");
}

void meltdown_leak_byte(uint8_t *leaked, int probe_times[256]) {
    SpecExecCPU cpu;
    spec_exec_cpu_init(&cpu);
    cache_flush_all(&cpu);

    for (int guess = 0; guess < 256; guess++) {
        probe_times[guess] = cache_line_probe(&cpu, guess % SPEC_CACHE_LINES);
    }

    int best_idx = -1;
    int lowest_time = 200;
    for (int i = 0; i < 256; i++) {
        if (probe_times[i] < lowest_time) {
            lowest_time = probe_times[i];
            best_idx = i;
        }
    }
    *leaked = (uint8_t)best_idx;
}
