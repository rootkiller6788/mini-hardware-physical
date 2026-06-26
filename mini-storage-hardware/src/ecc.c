#include "ecc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

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

/* ── Galois Field GF(2^4) arithmetic for BCH codes ──
 *
 * Primitive polynomial: x^4 + x + 1 (0x13).
 * Elements represented as 4-bit values [0..15].
 * alpha = 0x02 is the primitive element.
 */
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

static uint8_t gf_add(uint8_t a, uint8_t b) {
    return a ^ b;
}

static uint8_t gf_pow(uint8_t base, int exp) {
    uint8_t result = 1;
    int i;
    for (i = 0; i < exp; i++) {
        result = gf_mult(result, base);
    }
    return result;
}

/* ── BCH(15,7,2) Encoder ──
 *
 * L5: Systematic encoding using generator polynomial.
 * g(x) = LCM(m_1(x), m_2(x), ..., m_{2t}(x))
 * For t=2: g(x) = x^8 + x^7 + x^6 + x^4 + 1
 *
 * Systematic form: c(x) = m(x)·x^(n-k) + rem(m(x)·x^(n-k), g(x))
 */
void ecc_bch_encode(const uint8_t *data, uint8_t *codeword) {
    uint8_t i;
    uint8_t reg[8] = {0};
    uint8_t input;

    for (i = 0; i < 8; i++) {
        codeword[i] = 0;
    }

    /* g(x) = x^8 + x^7 + x^6 + x^4 + 1, feedback taps at positions 8,7,6,4,0 */
    for (i = 0; i < ECC_BCH_K; i++) {
        input = (*data >> (6 - i)) & 1;
        uint8_t feedback = (reg[7] ^ input) & 1;
        /* Shift register with taps */
        uint8_t new_reg[8];
        new_reg[0] = feedback;
        new_reg[1] = reg[0];                          /* tap at x^1: none */
        new_reg[2] = reg[1];                          /* tap at x^2: none */
        new_reg[3] = reg[2];                          /* tap at x^3: none */
        new_reg[4] = reg[3] ^ feedback;               /* tap at x^4 */
        new_reg[5] = reg[4];                          /* tap at x^5: none */
        new_reg[6] = reg[5] ^ feedback;               /* tap at x^6 */
        new_reg[7] = reg[6] ^ feedback;               /* tap at x^7 */
        {
            uint8_t j;
            for (j = 0; j < 8; j++) reg[j] = new_reg[j];
        }
    }

    for (i = 0; i < 8; i++) {
        codeword[i] = reg[i];
    }
}

/* ── BCH Syndrome Computation ──
 *
 * L5: Syndrome S_i = r(alpha^i) for i = 1, 3, ..., 2t-1.
 * For t=2: compute S1 and S3.
 * If S1 == S3 == 0, no errors detected.
 */
static void bch_compute_syndromes(const uint8_t *received,
                                  uint8_t *s1, uint8_t *s3) {
    uint8_t i;
    uint8_t syn1 = 0, syn3 = 0;

    /* Evaluate received polynomial r(x) at alpha^1 and alpha^3 */
    for (i = 0; i < ECC_BCH_N; i++) {
        uint8_t bit = (received[i / 8] >> (i % 8)) & 1;
        if (bit) {
            syn1 = gf_add(syn1, gf_pow(2, i * 1 % 15));
            syn3 = gf_add(syn3, gf_pow(2, i * 3 % 15));
        }
    }
    *s1 = syn1;
    *s3 = syn3;
}

/* ── BCH Decoder with Berlekamp-Massey Algorithm ──
 *
 * L5: For t=2, the error locator polynomial is:
 *   Lambda(x) = 1 + sigma_1·x + sigma_2·x^2
 * Where sigma_1 = S1, sigma_2 = (S3 + S1^3) / S1
 *
 * Then use Chien search to find error positions.
 * For each i where Lambda(alpha^{-i}) == 0, bit i is in error.
 */
int ecc_bch_decode(const uint8_t *codeword, uint8_t *corrected_data) {
    uint8_t s1, s3;
    uint8_t received[2];
    uint8_t i;

    /* Pack received bits: codeword bytes represent parity bits */
    received[0] = *codeword;
    received[1] = 0;

    bch_compute_syndromes(received, &s1, &s3);

    if (s1 == 0 && s3 == 0) {
        /* No errors */
        *corrected_data = *codeword;
        return 0;
    }

    if (s1 != 0 && s3 == 0) {
        /* Single error in information bits - correctable */
        *corrected_data = *codeword;
        return 1;
    }

    /* Compute error locator coefficients */
    uint8_t sigma1 = s1;
    uint8_t sigma2;

    if (s1 != 0) {
        sigma2 = gf_mult(gf_add(s3, gf_pow(s1, 3)),
                          gf_pow(s1, 14));  /* divide by s1 */
    } else {
        sigma2 = 0;
    }

    /* Chien search: find roots of Lambda(alpha^{-i}) for i in [0, 14] */
    uint8_t corrected[2];
    corrected[0] = received[0];
    corrected[1] = received[1];

    int errors_found = 0;
    for (i = 0; i < ECC_BCH_N && errors_found < 2; i++) {
        /* Lambda(alpha^{-i}) = 1 + sigma1·alpha^{-i} + sigma2·alpha^{-2i} */
        uint8_t inv_i = gf_pow(2, (15 - i) % 15);        /* alpha^{-i} */
        uint8_t inv_2i = gf_pow(2, (15 - 2 * i) % 15);   /* alpha^{-2i} */
        uint8_t eval = gf_add(1, gf_add(gf_mult(sigma1, inv_i),
                                         gf_mult(sigma2, inv_2i)));
        if (eval == 0) {
            /* Error at position i */
            corrected[i / 8] ^= (uint8_t)(1 << (i % 8));
            errors_found++;
        }
    }

    *corrected_data = corrected[0];
    return errors_found;
}

/* ── Reed-Solomon RS(255,239) over GF(2^8) ──
 *
 * L8: Reed-Solomon codes are the foundation of modern storage ECC.
 * RS(n,k) can correct up to (n-k)/2 symbol errors.
 * RS(255,239): 8-bit symbols, corrects 8 symbol errors.
 *
 * Used in: CDs, DVDs, Blu-ray, QR codes, RAID-6, satellite communications.
 *
 * GF(2^8) primitive polynomial: x^8 + x^4 + x^3 + x^2 + 1 (0x11D).
 */

#define RS_N 255
#define RS_K 239
#define RS_T 8

static uint8_t rs_gf_mul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        uint8_t carry = a & 0x80;
        a <<= 1;
        if (carry) a ^= 0x1D;  /* primitive poly: x^8 + x^4 + x^3 + x^2 + 1 */
        b >>= 1;
    }
    return p;
}

/* RS generator polynomial coefficients for t=8 */
void ecc_rs_generator_poly(uint8_t *gen) {
    uint8_t i, j;
    gen[0] = 1;
    for (i = 1; i <= 2 * RS_T; i++) {
        gen[i] = 0;
    }
    for (i = 0; i < 2 * RS_T; i++) {
        /* multiply by (x - alpha^(i+1)) where alpha = 2 */
        uint8_t root = 1;
        uint8_t k;
        for (k = 0; k <= i; k++) {
            root = rs_gf_mul(root, 2);
        }
        for (j = i + 1; j > 0; j--) {
            gen[j] = gen[j - 1] ^ rs_gf_mul(gen[j], root);
        }
        gen[0] = rs_gf_mul(gen[0], root);
    }
}

/* RS systematic encoding */
void ecc_rs_encode(const uint8_t *data, uint8_t *parity) {
    uint8_t gen[2 * RS_T + 1];
    uint8_t i, j;
    uint8_t remainder[2 * RS_T];

    ecc_rs_generator_poly(gen);
    memset(remainder, 0, sizeof(remainder));

    /* Polynomial division: remainder = data(x)·x^{2t} mod g(x) */
    for (i = 0; i < RS_K; i++) {
        uint8_t feedback = data[i] ^ remainder[0];
        if (feedback != 0) {
            for (j = 0; j < 2 * RS_T - 1; j++) {
                remainder[j] = remainder[j + 1] ^
                               rs_gf_mul(feedback, gen[2 * RS_T - 1 - j]);
            }
            remainder[2 * RS_T - 1] = rs_gf_mul(feedback, gen[0]);
        } else {
            for (j = 0; j < 2 * RS_T - 1; j++) {
                remainder[j] = remainder[j + 1];
            }
            remainder[2 * RS_T - 1] = 0;
        }
    }

    for (i = 0; i < 2 * RS_T; i++) {
        parity[i] = remainder[i];
    }
}

/* RS syndrome computation */
void ecc_rs_syndromes(const uint8_t *data, const uint8_t *parity,
                      uint8_t *syndromes) {
    uint8_t i, j;
    for (i = 0; i < 2 * RS_T; i++) {
        uint8_t syn = 0;
        uint8_t alpha_pow = 1;  /* alpha^(i+1) */
        for (j = 0; j <= i; j++) {
            alpha_pow = rs_gf_mul(alpha_pow, 2);
        }

        /* Evaluate at alpha^(i+1) */
        uint8_t eval_pow = 1;
        for (j = 0; j < RS_K; j++) {
            syn ^= rs_gf_mul(data[j], eval_pow);
            eval_pow = rs_gf_mul(eval_pow, alpha_pow);
        }
        for (j = 0; j < 2 * RS_T; j++) {
            syn ^= rs_gf_mul(parity[j], eval_pow);
            eval_pow = rs_gf_mul(eval_pow, alpha_pow);
        }
        syndromes[i] = syn;
    }
}

/* ── Shannon Theorem Connection ──
 *
 * L4: Shannon's channel coding theorem (1948):
 *   For a channel with capacity C, there exists a code of rate R < C
 *   achieving arbitrarily low error probability.
 *
 * For BSC (Binary Symmetric Channel) with crossover p:
 *   C = 1 - H(p) where H(p) = -p·log₂(p) - (1-p)·log₂(1-p)
 *
 * ECC demonstrates Shannon's theorem: as we add more parity (lower rate R),
 * we can correct more errors (higher p), approaching the capacity limit.
 *
 * Example: For p=0.01 (1% BER), C = 1 - H(0.01) = 0.919.
 * Any code with R < 0.919 can theoretically achieve error-free transmission.
 */
double ecc_shannon_capacity_bsc(double crossover_prob) {
    double p = crossover_prob;
    if (p <= 0.0 || p >= 1.0) return (p <= 0.0) ? 1.0 : 0.0;
    double h = -p * log2(p) - (1.0 - p) * log2(1.0 - p);
    return 1.0 - h;
}

/* ── BER vs SNR curve for coded and uncoded BPSK ──
 *
 * Uncoded BPSK: BER = Q(√(2·SNR))
 * Coded (with ECC gain): BER ≈ Q(√(2·SNR·R·G_code))
 *
 * ECC coding gain: typically 3-6 dB at BER=10⁻⁵ for practical codes.
 * Shannon limit for rate-1/2: Eb/N0_min = 0.188 dB (infinite block length).
 */
double ecc_coding_gain_db(double uncoded_snr, double coded_snr) {
    if (uncoded_snr <= 0.0 || coded_snr <= 0.0) return 0.0;
    return 10.0 * log10(uncoded_snr / coded_snr);
}
