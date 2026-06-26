#include "ecc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void) {
    uint8_t test_data, encoded, corrupted, corrected;
    uint8_t decoded_data;
    uint32_t i;

    printf("=== ECC Demo: Hamming(7,4) and BCH Codes ===\n\n");

    printf("--- Hamming(7,4) ---\n");
    printf("Data bits: 4, Code bits: 7, Correctable errors: 1\n\n");

    printf("[1] Encoding test data:\n");
    for (test_data = 0; test_data < 16; test_data++) {
        encoded = ecc_hamming_encode(test_data);
        printf("    Data=0x%01X (%c%c%c%c) -> Code=0x%02X (%c%c%c%c%c%c%c)\n",
               test_data,
               (test_data & 0x08) ? '1' : '0',
               (test_data & 0x04) ? '1' : '0',
               (test_data & 0x02) ? '1' : '0',
               (test_data & 0x01) ? '1' : '0',
               encoded,
               (encoded & 0x40) ? '1' : '0',
               (encoded & 0x20) ? '1' : '0',
               (encoded & 0x10) ? '1' : '0',
               (encoded & 0x08) ? '1' : '0',
               (encoded & 0x04) ? '1' : '0',
               (encoded & 0x02) ? '1' : '0',
               (encoded & 0x01) ? '1' : '0');
    }

    printf("\n[2] Single-bit error injection and correction:\n");
    test_data = 0x05;
    encoded   = ecc_hamming_encode(test_data);
    printf("    Original data=0x%X, Encoded=0x%02X\n", test_data, encoded);

    for (i = 0; i < 7; i++) {
        corrupted  = ecc_hamming_introduce_error(encoded, (int)i);
        uint8_t err_count = ecc_hamming_decode(corrupted, &decoded_data);
        printf("    Error at bit %u: codeword 0x%02X -> decoded 0x%X (errors=%u) %s\n",
               i, corrupted, decoded_data, err_count,
               (decoded_data == test_data && err_count == i + 1) ? "OK" : "FAIL");
    }

    printf("\n[3] Double-bit error detection (uncorrectable):\n");
    encoded = ecc_hamming_encode(0x09);
    corrupted = ecc_hamming_introduce_error(encoded, 1);
    corrupted = ecc_hamming_introduce_error(corrupted, 4);
    uint8_t err_count = ecc_hamming_decode(corrupted, &decoded_data);
    printf("    Data=0x9, 2-bit error -> decoded=0x%X, error_count=%u (should detect)\n",
           decoded_data, err_count);

    printf("\n[4] Error correction verification for all 16 data values:\n");
    uint32_t correct = 0;
    for (test_data = 0; test_data < 16; test_data++) {
        encoded = ecc_hamming_encode(test_data);
        int all_ok = 1;
        for (i = 0; i < 7; i++) {
            corrupted = ecc_hamming_introduce_error(encoded, (int)i);
            ecc_hamming_decode(corrupted, &decoded_data);
            if (decoded_data != test_data) { all_ok = 0; break; }
        }
        if (all_ok) correct++;
    }
    printf("    All 7 single-bit errors corrected for %u/16 data values\n", correct);

    printf("\n--- BCH(15,7,2) ---\n");
    printf("Data bits: 7, Code bits: 15, Correctable errors: 2\n");
    uint8_t bch_data = 0x55;
    uint8_t bch_codeword[8];
    uint8_t bch_decoded;

    ecc_bch_encode(&bch_data, bch_codeword);
    printf("\n[5] BCH Encode: data=0x%02X -> parity=", bch_data);
    for (i = 0; i < 8; i++) {
        printf("%02X ", bch_codeword[i]);
    }
    printf("\n");

    ecc_bch_decode(bch_codeword, &bch_decoded);
    printf("    BCH Decode: decoded=0x%02X\n", bch_decoded);

    printf("\n=== ECC Demo Complete ===\n");
    return 0;
}
