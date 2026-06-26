#include "puf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int main(void) {
    printf("========================================================\n");
    printf("  Physical Unclonable Function (PUF) Demonstration\n");
    printf("========================================================\n\n");

    PUF my_device;
    PUF another_device;

    printf("[STEP 1] Initialize SRAM PUF for device A...\n");
    puf_sram_init(&my_device, PUF_BITS);
    printf("  PUF type: SRAM PUF\n");
    printf("  Cells: %d\n", PUF_BITS);
    printf("  Intra-HD (expected): %.2f%%\n", my_device.intra_hd * 100);
    printf("  Inter-HD (expected): %.2f%%\n\n", my_device.inter_hd * 100);

    printf("[STEP 2] Initialize SRAM PUF for device B (different chip)...\n");
    puf_sram_init(&another_device, PUF_BITS);
    printf("  Both devices have unique, unclonable identities\n\n");

    printf("[STEP 3] Generate challenge-response pairs for device A...\n");
    PUFChallenge challenges[5];
    PUFResponse responses_a[5];
    PUFResponse responses_b[5];

    for (int i = 0; i < 5; i++) {
        memset(&challenges[i], 0, sizeof(PUFChallenge));
        for (int j = 0; j < PUF_CHALLENGE_BITS; j++) {
            challenges[i].bits[j] = (uint8_t)((i * 37 + j * 13) & 0xFF);
        }

        puf_get_response(&my_device, &challenges[i], &responses_a[i]);
        puf_get_response(&another_device, &challenges[i], &responses_b[i]);
    }

    printf("  %-6s %-36s %-36s %s\n", "CRP#", "Response A", "Response B", "HD");
    printf("  %-6s %-36s %-36s %s\n", "----", "-----------------------------",
           "-----------------------------", "---");
    for (int i = 0; i < 5; i++) {
        char ra[37] = {0}, rb[37] = {0};
        for (int j = 0; j < 16; j++) {
            sprintf(ra + j * 2, "%02X", responses_a[i].bits[j]);
            sprintf(rb + j * 2, "%02X", responses_b[i].bits[j]);
        }
        float hd = puf_hamming_compare(responses_a[i].bits,
                                        responses_b[i].bits, 32);
        printf("  %-6d %s  %s  %.3f\n", i + 1, ra, rb, hd);
    }

    printf("\n[STEP 4] Demonstrate PUF noise (same device, same challenge)...\n");
    PUFResponse noisy_resp;
    puf_noise_simulate(&responses_a[0], 0.03f, &noisy_resp);

    float noise_hd = puf_hamming_compare(responses_a[0].bits,
                                          noisy_resp.bits, 32);
    printf("  Original response:   ");
    for (int i = 0; i < 8; i++) {
        printf("%02X", responses_a[0].bits[i]);
    }
    printf("...\n");
    printf("  Noisy response:      ");
    for (int i = 0; i < 8; i++) {
        printf("%02X", noisy_resp.bits[i]);
    }
    printf("...\n");
    printf("  Noise probability:   3%%\n");
    printf("  Hamming distance:    %.3f (%.1f%% bits flipped)\n\n",
           noise_hd, noise_hd * 100);

    printf("[STEP 5] Enroll PUF - generate helper data...\n");
    uint8_t helper_data[PUF_HELPER_BITS];
    puf_enroll(&my_device, helper_data);
    printf("  Helper data generated (%d bytes)\n", PUF_HELPER_BITS);
    printf("  Helper data (first 16 bytes): ");
    for (int i = 0; i < 16; i++) {
        printf("%02X", helper_data[i]);
    }
    printf("\n\n");

    printf("[STEP 6] Reconstruct key from noisy response + helper data...\n");
    uint8_t reconstructed_key[32];
    puf_reconstruct(&my_device, &noisy_resp, helper_data, reconstructed_key);
    printf("  Reconstructed key: ");
    for (int i = 0; i < 16; i++) {
        printf("%02X", reconstructed_key[i]);
    }
    printf("...\n\n");

    printf("[STEP 7] Generate cryptographic key from PUF...\n");
    uint8_t crypto_key[32];
    puf_key_generate(&my_device, 32, crypto_key);
    printf("  Generated key (%d bytes): ", 32);
    for (int i = 0; i < 16; i++) {
        printf("%02X", crypto_key[i]);
    }
    printf("...\n\n");

    printf("[STEP 8] Authentication test...\n");
    printf("  Challenge:    ");
    for (int i = 0; i < 8; i++) {
        printf("%02X", challenges[0].bits[i]);
    }
    printf("...\n");

    bool auth_ok = puf_authenticate(&my_device, &challenges[0], &responses_a[0]);
    printf("  Auth (correct device + correct response): %s\n",
           auth_ok ? "SUCCESS" : "FAIL");

    auth_ok = puf_authenticate(&another_device, &challenges[0], &responses_a[0]);
    printf("  Auth (wrong device + correct response):   %s\n",
           auth_ok ? "SUCCESS" : "FAIL");

    auth_ok = puf_authenticate(&my_device, &challenges[0], &noisy_resp);
    printf("  Auth (correct device + noisy response):   %s\n",
           auth_ok ? "SUCCESS" : "FAIL");

    auth_ok = puf_authenticate(&my_device, &challenges[0], &responses_a[1]);
    printf("  Auth (correct device + wrong response):   %s\n\n",
           auth_ok ? "SUCCESS" : "FAIL");

    printf("========================================================\n");
    printf("  PUF Key Properties\n");
    printf("========================================================\n");
    printf("  Uniqueness:  Each chip has different response (Inter-HD ~50%%)\n");
    printf("  Reliability: Same challenge gives same response (Intra-HD <5%%)\n");
    printf("  Unclonability: Cannot physically clone due to process variation\n");
    printf("  Tamper-evidence: Physical probing destroys PUF response\n\n");

    return 0;
}
