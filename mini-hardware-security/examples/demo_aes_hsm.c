#include <stdio.h>
#include <string.h>
#include "hw_crypto.h"

/* ============================================================================
 * L7 Application: Hardware Security Module (HSM) Demo
 *
 * Demonstrates AES-256 GCM authenticated encryption as used in hardware
 * security modules for protecting data-at-rest. This pattern is used in:
 * - Self-encrypting drives (SED) with TCG Opal
 * - HSM key wrapping (PKCS#11 C_WrapKey)
 * - Payment HSM for PIN encryption
 * ========================================================================== */
int main(void)
{
    printf("=== Mini HSM: AES-256-GCM Crypto Operations ===\n\n");

    /* Master key derivation from PUF (simulated) */
    uint8_t puf_seed[32];
    mini_hwsec_random(puf_seed, 32);
    printf("PUF seed: ");
    for (int i = 0; i < 8; i++) printf("%02x", puf_seed[i]);
    printf("...\n");

    uint8_t master_key[32];
    mini_hwsec_hkdf_sha256(puf_seed, 32,
                            (const uint8_t *)"MASTER-KEY", 10,
                            NULL, 0, master_key, 32);

    MiniHwSecAesCtx ctx;
    mini_hwsec_aes_init(&ctx, master_key);
    printf("Master key derived and AES context initialized.\n\n");

    /* Encrypt a payment record */
    const char *payment = "CARD=4111111111111111|EXP=1228|CVV=123|AMT=99.99";
    uint8_t plain[128], cipher[128], decrypted[128];
    uint8_t aad[32] = "TransactionID=0xDEADBEEF";
    uint8_t iv[12];
    uint8_t tag[16];

    mini_hwsec_random(iv, 12);

    memset(plain, 0, sizeof(plain));
    memcpy(plain, payment, strlen(payment));

    printf("Plaintext:  %s\n", plain);
    printf("AAD:        %s\n", aad);

    mini_hwsec_aes_gcm_encrypt(&ctx, iv, plain, cipher, aad, 16, tag, 128);
    printf("IV:         ");
    for (int i = 0; i < 12; i++) printf("%02x", iv[i]);
    printf("\nTag:        ");
    for (int i = 0; i < 16; i++) printf("%02x", tag[i]);
    printf("\nCiphertext: ");
    for (int i = 0; i < 16; i++) printf("%02x", cipher[i]);
    printf("...\n\n");

    /* Decrypt with wrong AAD (should fail) */
    uint8_t wrong_aad[32] = "TransactionID=0xBADC0DE";
    bool ok = mini_hwsec_aes_gcm_decrypt(&ctx, iv, cipher, decrypted,
                                          wrong_aad, 16, tag, 128);
    printf("Decrypt with wrong AAD: %s (expected: FAIL)\n", ok ? "SUCCESS" : "FAIL");

    /* Decrypt correctly */
    ok = mini_hwsec_aes_gcm_decrypt(&ctx, iv, cipher, decrypted,
                                     aad, 16, tag, 128);
    printf("Decrypt with correct AAD: %s\n", ok ? "SUCCESS" : "FAIL");
    printf("Decrypted:  %s\n", decrypted);

    /* Demonstrate AES-CTR for bulk storage encryption */
    printf("\n=== AES-CTR Bulk Encryption ===\n");
    uint8_t disk_data[256];
    mini_hwsec_random(disk_data, sizeof(disk_data));
    uint8_t disk_encrypted[256], disk_decrypted[256];
    uint8_t ctr_nonce[12];
    mini_hwsec_random(ctr_nonce, 12);

    mini_hwsec_aes_ctr_mode(&ctx, ctr_nonce, disk_data, disk_encrypted, 256);
    mini_hwsec_aes_ctr_mode(&ctx, ctr_nonce, disk_encrypted, disk_decrypted, 256);

    printf("CTR roundtrip: %s\n",
           memcmp(disk_data, disk_decrypted, 256) == 0 ? "PASS" : "FAIL");

    return 0;
}
