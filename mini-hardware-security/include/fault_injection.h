#ifndef FAULT_INJECTION_H
#define FAULT_INJECTION_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    FAULT_CLOCK_GLITCH,
    FAULT_VOLTAGE_GLITCH,
    FAULT_EM_PULSE,
    FAULT_LASER_FAULT,
    FAULT_ROW_HAMMER
} FaultType;

typedef enum {
    FT_INSTRUCTION,
    FT_REGISTER,
    FT_MEMORY_ADDR
} FaultTargetType;

typedef struct {
    FaultTargetType target_type;
    union {
        uint32_t instruction;
        int      register_idx;
        uint64_t memory_addr;
    };
} FaultTarget;

typedef struct {
    FaultType  type;
    int        timing;
    float      voltage_drop;
    float      pulse_width;
    int        skip_instruction;
    int        corrupt_bit;
    FaultTarget target;
} FaultInjection;

void fault_clock_glitch(FaultTarget *target, int skip_instruction);
void fault_voltage_glitch(FaultTarget *target, int corrupt_register_bit);
void fault_verify_pin_bypass(void);
void fault_rsa_crt_attack(void);
void fault_rowhammer(int *bitmap, int row, int bit_to_flip);
void fault_rsa_crt_sim(uint64_t N, uint64_t d, uint64_t *faulted_signature);
uint64_t fault_gcd(uint64_t a, uint64_t b);
uint64_t fault_mod_exp(uint64_t base, uint64_t exp, uint64_t mod);

void fault_inject(FaultInjection *fi);

#endif
