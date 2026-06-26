#include "ecc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

uint8_t ecc_hamming_encode(uint8_t data) {
    uint8_t d1, d2, d3, d4;
    uint8_t p1, p2, p3;
    uint8_t codeword;

    data &= 0x0F;

    d1 = (data >> 0) & 1;
    d2 = (data >> 1) & 1;
    d3 = (data >> 2) & 1;
    d4 = (data >> 3) & 1;

    p1 = d1 ^ d2 ^ d4;
    p2 = d1 ^ d3 ^ d4;
    p3 = d2 ^ d3 ^ d4;

    codeword = (p1 << 0) | (p2 << 1) | (d1 << 2) | (p3 << 3)
             | (d2 << 4) | (d3 << 5) | (d4 << 6);

    return codeword & 0x7F;
}

uint8_t ecc_hamming_decode(uint8_t codeword, uint8_t *corrected_data) {
    uint8_t p1_recv, p2_recv, d1_recv, p3_recv, d2_recv, d3_recv, d4_recv;
    uint8_t p1_calc, p2_calc, p3_calc;
    uint8_t s1, s2, s3;
    int     error_pos;

    codeword &= 0x7F;

    p1_recv = (codeword >> 0) & 1;
    p2_recv = (codeword >> 1) & 1;
    d1_recv = (codeword >> 2) & 1;
    p3_recv = (codeword >> 3) & 1;
    d2_recv = (codeword >> 4) & 1;
    d3_recv = (codeword >> 5) & 1;
    d4_recv = (codeword >> 6) & 1;

    p1_calc = d1_recv ^ d2_recv ^ d4_recv;
    p2_calc = d1_recv ^ d3_recv ^ d4_recv;
    p3_calc = d2_recv ^ d3_recv ^ d4_recv;

    s1 = p1_recv ^ p1_calc;
    s2 = p2_recv ^ p2_calc;
    s3 = p3_recv ^ p3_calc;

    error_pos = (s3 << 2) | (s2 << 1) | (s1 << 0);

    if (error_pos > 0 && error_pos <= 7) {
        codeword ^= (1 << (error_pos - 1));
    }

    *corrected_data = ((codeword >> 2) & 0x01)
                    | ((codeword >> 3) & 0x0E);

    return (error_pos > 7) ? ECC_BCH_T + 1 : 0;
}

uint8_t ecc_hamming_introduce_error(uint8_t codeword, int bit_position) {
    if (bit_position < 0 || bit_position > 6) return codeword;
    return codeword ^ (uint8_t)(1 << bit_position);
}

#define BCH_GF_SIZE  16
#define BCH_POLY     0x13

static uint8_t gf_mult(uint8_t a, uint8_t b) {
    uint8_t result = 0;
    uint8_t i;
    for (i = 0; i < 4; i++) {
        if (b & 1) result ^= a;
        uint8_t carry = a & 0x08;
        a <<= 1;
        if (carry) a ^= 0x03;
        b >>= 1;
    }
    return result & 0x0F;
}

void ecc_bch_encode(const uint8_t *data, uint8_t *codeword) {
    uint8_t i;
    uint8_t reg[8] = {0};
    uint8_t input;

    for (i = 0; i < 8; i++) {
        codeword[i] = 0;
    }

    for (i = 0; i < ECC_BCH_K; i++) {
        input = (*data >> (6 - i)) & 1;
        uint8_t feedback = (reg[7] ^ input) & 1;
        uint8_t j;
        for (j = 7; j > 0; j--) {
            reg[j] = (reg[j - 1] ^ feedback) & 1;
        }
        reg[0] = feedback;
    }

    for (i = 0; i < 8; i++) {
        codeword[i] = reg[i];
    }
}

int ecc_bch_decode(const uint8_t *codeword, uint8_t *corrected_data) {
    *corrected_data = *codeword;
    return 0;
}
