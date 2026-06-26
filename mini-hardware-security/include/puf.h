#ifndef PUF_H
#define PUF_H

#include <stdbool.h>
#include <stdint.h>

#define PUF_BITS       256
#define PUF_CHALLENGE_BITS 128
#define PUF_HELPER_BITS PUF_BITS
#define PUF_THRESHOLD   0.10f

typedef enum {
    PUF_SRAM,
    PUF_ARBITER,
    PUF_RING_OSCILLATOR
} PUFType;

typedef struct {
    uint8_t bits[PUF_BITS];
    float   noise_level;
    float   reliability;
} PUFResponse;

typedef struct {
    uint8_t bits[PUF_CHALLENGE_BITS];
} PUFChallenge;

typedef struct {
    PUFType type;
    float   intra_hd;
    float   inter_hd;
    float   bit_error_rate;
    PUFResponse enrollment_response[32];
    PUFChallenge enrollment_challenge[32];
    int     enrollment_count;
} PUF;

void puf_sram_init(PUF *puf, int num_cells);
void puf_arbiter_challenge(PUF *puf, const PUFChallenge *challenge,
                           PUFResponse *response);
void puf_get_response(PUF *puf, const PUFChallenge *c, PUFResponse *r);
void puf_noise_simulate(const PUFResponse *ideal, float noise_prob,
                        PUFResponse *noisy);
bool puf_authenticate(PUF *puf, const PUFChallenge *c,
                      const PUFResponse *claimed);
void puf_key_generate(PUF *puf, int key_len, uint8_t *key);
void puf_enroll(PUF *puf, uint8_t helper_data[PUF_HELPER_BITS]);
void puf_reconstruct(PUF *puf, const PUFResponse *noisy,
                     const uint8_t helper_data[PUF_HELPER_BITS],
                     uint8_t key[32]);
float puf_hamming_compare(const uint8_t *a, const uint8_t *b, int len);
void puf_sram_cell_sim(uint8_t *response_bits, int num_cells);

#endif
