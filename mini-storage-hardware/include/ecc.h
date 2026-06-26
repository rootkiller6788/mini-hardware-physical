#ifndef ECC_H
#define ECC_H

#include <stdbool.h>
#include <stdint.h>

#define ECC_HAMMING_DATA_BITS  4
#define ECC_HAMMING_CODE_BITS  7
#define ECC_HAMMING_PARITY_BITS 3

#define ECC_BCH_N             15
#define ECC_BCH_K             7
#define ECC_BCH_T             2

typedef enum {
    ECC_HAMMING,
    ECC_BCH,
    ECC_LDPC,
    ECC_RS
} ECCType;

typedef struct {
    ECCType  type;
    uint32_t data_bits;
    uint32_t parity_bits;
    uint32_t correctable_errors;
} ECCEncoder;

uint8_t ecc_hamming_encode(uint8_t data);
uint8_t ecc_hamming_decode(uint8_t codeword, uint8_t *corrected_data);
uint8_t ecc_hamming_introduce_error(uint8_t codeword, int bit_position);

void ecc_bch_encode(const uint8_t *data, uint8_t *codeword);
int  ecc_bch_decode(const uint8_t *codeword, uint8_t *corrected_data);

#endif
