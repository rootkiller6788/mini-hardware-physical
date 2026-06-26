#ifndef SPEC_EXEC_ATK_H
#define SPEC_EXEC_ATK_H

#include <stdbool.h>
#include <stdint.h>

#define SPEC_NUM_REGS       16
#define SPEC_BTB_ENTRIES    64
#define SPEC_ROB_ENTRIES    32
#define SPEC_CACHE_LINES   256
#define SPEC_THRESHOLD      50

typedef enum {
    SPECTRE_V1_BTB,
    SPECTRE_V2_PHT,
    SPECTRE_V4_SSB
} SpectreVariant;

typedef struct {
    uint32_t tag;
    uint32_t target;
    bool     valid;
} BTBEntry;

typedef struct {
    int64_t arch_regs[SPEC_NUM_REGS];
    int64_t spec_regs[SPEC_NUM_REGS];
    BTBEntry btb[SPEC_BTB_ENTRIES];
    int rob_entries[SPEC_ROB_ENTRIES];
    int rob_head;
    int rob_tail;
    bool flush_on_mispredict;
    int cache_access_times[SPEC_CACHE_LINES];
} SpecExecCPU;

typedef struct {
    uint32_t kernel_addr;
    uint8_t  probe_array[SPEC_CACHE_LINES * 64];
    bool     fault_suppressed;
} MeltdownCheck;

void spectre_v1_simulate(void);
void spectre_v1_transmit(uint8_t secret);
void meltdown_simulate(SpecExecCPU *cpu, uint32_t kernel_addr);
void meltdown_leak_byte(uint8_t *leaked, int probe_times[256]);

void spec_exec_cpu_init(SpecExecCPU *cpu);
void spec_exec_btb_train(SpecExecCPU *cpu, uint32_t pc, uint32_t target);
uint32_t spec_exec_btb_predict(SpecExecCPU *cpu, uint32_t pc);
void spec_exec_flush_cache(SpecExecCPU *cpu);
int  spec_exec_probe_cache(SpecExecCPU *cpu, int line);

#endif
