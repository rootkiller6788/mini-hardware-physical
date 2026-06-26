#include "fault_injection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

uint64_t fault_gcd(uint64_t a, uint64_t b) {
    while (b != 0) {
        uint64_t t = b;
        b = a % b;
        a = t;
    }
    return a;
}

uint64_t fault_mod_exp(uint64_t base, uint64_t exp, uint64_t mod) {
    uint64_t result = 1;
    base = base % mod;
    while (exp > 0) {
        if (exp & 1) {
            result = (result * base) % mod;
        }
        exp >>= 1;
        base = (base * base) % mod;
    }
    return result;
}

void fault_clock_glitch(FaultTarget *target, int skip_instruction) {
    printf("Clock glitch at cycle %d\n", skip_instruction);
    printf("Target type: %d, address: 0x%llX\n",
           target->target_type,
           (unsigned long long)target->memory_addr);
    printf("Result: instruction at 0x%llX is SKIPPED (becomes NOP)\n",
           (unsigned long long)target->memory_addr);
}

void fault_voltage_glitch(FaultTarget *target, int corrupt_register_bit) {
    printf("Voltage glitch on register %d, bit %d\n",
           target->register_idx, corrupt_register_bit);
    printf("Result: register R%d bit %d is CORRUPTED\n",
           target->register_idx, corrupt_register_bit);
}

void fault_verify_pin_bypass(void) {
    const char correct_pin[] = "1234";
    int authenticated = 0;

    printf("=== Fault-based PIN Bypass Demo ===\n");
    printf("Correct PIN: %s\n", correct_pin);

    if (authenticated) {
        printf("ACCESS GRANTED (authentication bypassed via fault!)\n");
    } else {
        printf("ACCESS DENIED (normal path)\n");
        printf("Simulating voltage glitch on auth flag...\n");
        authenticated = 1;
        printf("ACCESS GRANTED (fault injected - flag flipped!)\n");
    }
}

void fault_rsa_crt_attack(void) {
    printf("=== Bellcore Attack: RSA-CRT Fault Injection ===\n");
    uint64_t p = 61, q = 53;
    uint64_t N = p * q;
    uint64_t phi = (p - 1) * (q - 1);
    uint64_t e_val = 17;
    uint64_t d = 1;
    while (((d * e_val) % phi) != 1) d++;
    uint64_t message = 42;
    uint64_t sig = fault_mod_exp(message, d, N);

    uint64_t dp = d % (p - 1);
    uint64_t dq = d % (q - 1);
    uint64_t sp = fault_mod_exp(message, dp, p);
    uint64_t sq = fault_mod_exp(message, dq, q);
    uint64_t qinv = 0;
    for (uint64_t i = 1; i < p; i++) {
        if ((q * i) % p == 1) { qinv = i; break; }
    }

    uint64_t sp_faulted = sp + 1;
    uint64_t sig_faulted = sq + q * ((qinv * (sp_faulted - sq)) % p);

    if (sig_faulted % q != 0) {
        uint64_t diff = (sig > sig_faulted) ? sig - sig_faulted : sig_faulted - sig;
        uint64_t q_recovered = fault_gcd(diff, N);
        printf("N = %llu, p = %llu, q = %llu\n", (unsigned long long)N,
               (unsigned long long)p, (unsigned long long)q);
        printf("Original signature: %llu\n", (unsigned long long)sig);
        printf("Faulted signature: %llu\n", (unsigned long long)sig_faulted);
        printf("Recovered q = GCD(s - s', N) = %llu\n",
               (unsigned long long)q_recovered);
        printf("Attack SUCCESS: N factored!\n");
    }
}

void fault_rsa_crt_sim(uint64_t N, uint64_t d,
                        uint64_t *faulted_signature) {
    uint64_t message = 42;
    uint64_t q_guess = 2;
    while (N % q_guess != 0) q_guess++;
    uint64_t p_guess = N / q_guess;
    uint64_t dp = d % (p_guess - 1);
    uint64_t dq = d % (q_guess - 1);
    uint64_t sp = fault_mod_exp(message, dp, p_guess);
    uint64_t sq = fault_mod_exp(message, dq, q_guess);
    uint64_t qinv = 0;
    for (uint64_t i = 1; i < p_guess; i++) {
        if ((q_guess * i) % p_guess == 1) { qinv = i; break; }
    }
    uint64_t sp_faulted = sp % (p_guess - 1);
    *faulted_signature = sq + q_guess * ((qinv * (sp_faulted - sq)) % p_guess);
}

void fault_rowhammer(int *bitmap, int row, int bit_to_flip) {
    printf("RowHammer: hammering rows adjacent to row %d\n", row);
    printf("Bit flip simulated at row %d, bit %d\n", row, bit_to_flip);
    if (bit_to_flip >= 0 && bitmap) {
        bitmap[row] ^= (1 << bit_to_flip);
    }
}

void fault_inject(FaultInjection *fi) {
    switch (fi->type) {
    case FAULT_CLOCK_GLITCH:
        fault_clock_glitch(&fi->target, fi->skip_instruction);
        break;
    case FAULT_VOLTAGE_GLITCH:
        fault_voltage_glitch(&fi->target, fi->corrupt_bit);
        break;
    case FAULT_EM_PULSE:
        printf("EM pulse injected at cycle %d\n", fi->timing);
        break;
    case FAULT_LASER_FAULT:
        printf("Laser fault injected at target address\n");
        break;
    case FAULT_ROW_HAMMER: {
        int dummy = 0;
        fault_rowhammer(&dummy, fi->timing, fi->corrupt_bit);
        break;
    }
    }
}
