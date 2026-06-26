#include "puf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint32_t puf_random_seed = 0xDEADBEEF;

static uint32_t puf_rand(void) {
    puf_random_seed = puf_random_seed * 1103515245 + 12345;
    return puf_random_seed;
}

static float puf_randf(void) {
    return (float)(puf_rand() % 10000) / 10000.0f;
}

void puf_sram_cell_sim(uint8_t *response_bits, int num_cells) {
    int byte_count = (num_cells + 7) / 8;
    for (int i = 0; i < byte_count; i++) {
        response_bits[i] = (uint8_t)(puf_rand() & 0xFF);
    }
}

void puf_sram_init(PUF *puf, int num_cells) {
    memset(puf, 0, sizeof(PUF));
    puf->type = PUF_SRAM;
    puf->intra_hd = 0.05f;
    puf->inter_hd = 0.48f;
    puf->bit_error_rate = 0.03f;
    int cells = num_cells > 0 ? num_cells : PUF_BITS;
    puf->enrollment_count = 0;
    (void)cells;
}

void puf_arbiter_challenge(PUF *puf, const PUFChallenge *challenge,
                            PUFResponse *response) {
    memset(response, 0, sizeof(PUFResponse));
    uint32_t state = 0;
    for (int i = 0; i < PUF_CHALLENGE_BITS / 8; i++) {
        state ^= ((uint32_t)challenge->bits[i]) << (i % 4) * 8;
    }
    state = state * 1103515245 + 12345;
    for (int i = 0; i < PUF_BITS / 8; i++) {
        response->bits[i] = (uint8_t)((state >> ((i % 4) * 8)) & 0xFF);
        state = state * 1103515245 + 12345;
    }
    response->reliability = 0.95f;
    response->noise_level = 0.03f;
    (void)puf;
}

void puf_get_response(PUF *puf, const PUFChallenge *c, PUFResponse *r) {
    if (puf->type == PUF_SRAM) {
        memset(r, 0, sizeof(PUFResponse));
        uint32_t seed = 0;
        for (int i = 0; i < PUF_CHALLENGE_BITS; i++) {
            seed ^= ((uint32_t)c->bits[i]) << ((i % 4) * 8);
        }
        puf_random_seed = seed + 0xABCD;
        puf_sram_cell_sim(r->bits, PUF_BITS);
        r->reliability = 0.97f;
        r->noise_level = 0.03f;
    } else if (puf->type == PUF_ARBITER) {
        puf_arbiter_challenge(puf, c, r);
    } else {
        memset(r, 0, sizeof(PUFResponse));
        r->reliability = 0.90f;
    }
}

void puf_noise_simulate(const PUFResponse *ideal, float noise_prob,
                         PUFResponse *noisy) {
    memcpy(noisy, ideal, sizeof(PUFResponse));
    for (int i = 0; i < PUF_BITS / 8; i++) {
        for (int bit = 0; bit < 8; bit++) {
            if (puf_randf() < noise_prob) {
                noisy->bits[i] ^= (uint8_t)(1 << bit);
            }
        }
    }
    noisy->noise_level = noise_prob;
    noisy->reliability = 1.0f - noise_prob;
}

float puf_hamming_compare(const uint8_t *a, const uint8_t *b, int len) {
    int diff = 0;
    int total_bits = len * 8;
    for (int i = 0; i < len; i++) {
        uint8_t x = a[i] ^ b[i];
        while (x) {
            diff += (x & 1);
            x >>= 1;
        }
    }
    return (float)diff / (float)total_bits;
}

bool puf_authenticate(PUF *puf, const PUFChallenge *c,
                       const PUFResponse *claimed) {
    PUFResponse expected;
    puf_get_response(puf, c, &expected);
    float hd = puf_hamming_compare(expected.bits, claimed->bits, PUF_BITS / 8);
    return hd < PUF_THRESHOLD;
}

void puf_key_generate(PUF *puf, int key_len, uint8_t *key) {
    PUFChallenge challenge;
    memset(&challenge, 0, sizeof(challenge));
    for (int i = 0; i < PUF_CHALLENGE_BITS; i++) {
        challenge.bits[i] = (uint8_t)(puf_rand() & 0xFF);
    }
    PUFResponse response;
    puf_get_response(puf, &challenge, &response);
    int bytes = key_len < (PUF_BITS / 8) ? key_len : (PUF_BITS / 8);
    memcpy(key, response.bits, bytes);
}

void puf_enroll(PUF *puf, uint8_t helper_data[PUF_HELPER_BITS]) {
    memset(helper_data, 0, PUF_HELPER_BITS);
    PUFChallenge challenge;
    memset(&challenge, 0, sizeof(challenge));
    for (int i = 0; i < PUF_CHALLENGE_BITS; i++) {
        challenge.bits[i] = (uint8_t)(puf_rand() & 0xFF);
    }
    PUFResponse ideal;
    puf_get_response(puf, &challenge, &ideal);

    int byte_count = PUF_BITS / 8;
    for (int i = 0; i < byte_count; i++) {
        helper_data[i] = ideal.bits[i] ^ 0xAA;
    }

    if (puf->enrollment_count < 32) {
        memcpy(&puf->enrollment_challenge[puf->enrollment_count],
               &challenge, sizeof(PUFChallenge));
        memcpy(&puf->enrollment_response[puf->enrollment_count],
               &ideal, sizeof(PUFResponse));
        puf->enrollment_count++;
    }
}

void puf_reconstruct(PUF *puf, const PUFResponse *noisy,
                      const uint8_t helper_data[PUF_HELPER_BITS],
                      uint8_t key[32]) {
    int byte_count = PUF_BITS / 8;
    memcpy(key, helper_data, byte_count);
    for (int i = 0; i < byte_count; i++) {
        key[i] = noisy->bits[i] ^ (key[i] ^ 0xAA);
        key[i] = noisy->bits[i];
    }
    (void)puf;
}
