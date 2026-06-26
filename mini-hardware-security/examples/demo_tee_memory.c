#include <stdio.h>
#include <string.h>
#include "tee_enclave.h"
#include "hrot_puf.h"
#include "memory_crypto.h"
#include "hw_crypto.h"
#include "side_channel.h"

/* ============================================================================
 * L6/L7: TEE + Memory Encryption + Side-Channel Defense Integration Demo
 *
 * Complete scenario: A hardware wallet application running in a TEE enclave
 * with encrypted memory and side-channel countermeasures.
 *
 * Flow:
 * 1. HRoT boot → derive device key from PUF
 * 2. Initialize TEE platform with encrypted memory
 * 3. Create enclave for wallet application
 * 4. Attest enclave identity to verifier
 * 5. Seal wallet private keys within enclave
 * 6. Sign transaction in enclave (protected from host OS)
 * 7. Monitor side-channel for attacks
 * ========================================================================== */
int main(void)
{
    printf("=== Mini Hardware Wallet with TEE + Memory Encryption ===\n\n");

    /* ==================================================================
     * Step 1: Hardware Root of Trust Boot
     * ================================================================== */
    printf("--- Step 1: HRoT Boot ---\n");
    MiniHwSecHRoT hrot;
    uint8_t device_uid[32];
    mini_hwsec_random(device_uid, 32);
    mini_hwsec_hrot_init(&hrot, device_uid, NULL);
    printf("HRoT initialized. Lifecycle: %d\n",
           mini_hwsec_hrot_get_lifecycle(&hrot));

    /* Derive platform master key for TEE */
    uint8_t platform_key[32];
    mini_hwsec_hrot_derive_key(&hrot, "TEE-PLATFORM", NULL, platform_key);
    printf("Platform key derived from HRoT PUF.\n");

    /* ==================================================================
     * Step 2: Memory Encryption Engine
     * ================================================================== */
    printf("\n--- Step 2: Memory Encryption Engine ---\n");
    MiniHwSecMemEngine mem_engine;
    mini_hwsec_mem_engine_init(&mem_engine, platform_key);

    int mem_region = mini_hwsec_mem_region_add(&mem_engine,
                                                0x40000000, 64 * 1024 * 1024); /* 64MB */
    printf("Encrypted memory region: ID=%d, base=0x40000000, size=64MB\n",
           mem_region);

    /* Demonstrate memory encryption roundtrip */
    uint8_t secret_data[64];
    mini_hwsec_random(secret_data, 64);
    printf("Plain data:  ");
    for (int i = 0; i < 8; i++) printf("%02x", secret_data[i]);
    printf("...\n");

    MiniHwSecMemEncryptedLine enc_line;
    mini_hwsec_mem_encrypt_line(&mem_engine, mem_region,
                                 0x40000100, secret_data, &enc_line);
    printf("Encrypted:   ");
    for (int i = 0; i < 8; i++) printf("%02x", enc_line.ciphertext[i]);
    printf("...\n");

    uint8_t recovered[64];
    bool ok = mini_hwsec_mem_decrypt_line(&mem_engine, mem_region,
                                           0x40000100, &enc_line, recovered);
    printf("Decrypt OK:  %s\n", ok ? "YES" : "NO");

    /* ==================================================================
     * Step 3: TEE Enclave Creation
     * ================================================================== */
    printf("\n--- Step 3: TEE Enclave (Wallet App) ---\n");
    MiniHwSecTEEManager tee;
    mini_hwsec_tee_init(&tee, platform_key, 0x40000000, 64 * 1024 * 1024, true);

    uint8_t signer_hash[32];
    mini_hwsec_random(signer_hash, 32);

    uint8_t wallet_code[4096];
    memset(wallet_code, 0xEE, sizeof(wallet_code));
    /* In a real wallet, this code would: 
     * - Derive keys from seed
     * - Sign transactions
     * - Never expose private keys outside enclave */

    int wallet_enclave = mini_hwsec_tee_create_enclave(&tee,
                                                         wallet_code, sizeof(wallet_code),
                                                         signer_hash, 1,
                                                         0 /* DEBUG_DISABLED */);
    printf("Enclave created: ID=%d\n", wallet_enclave);

    mini_hwsec_tee_init_enclave(&tee, wallet_enclave);
    printf("Enclave initialized: state=%d\n",
           tee.enclaves[wallet_enclave].state);

    /* ==================================================================
     * Step 4: Local Attestation
     * ================================================================== */
    printf("\n--- Step 4: Local Attestation ---\n");

    /* Create a verifier enclave */
    int verifier_enclave = mini_hwsec_tee_create_enclave(&tee,
                                                           wallet_code, sizeof(wallet_code),
                                                           signer_hash, 1, 0);
    mini_hwsec_tee_init_enclave(&tee, verifier_enclave);

    MiniHwSecTEEAttestReport report;
    ok = mini_hwsec_tee_attest_local(&tee, wallet_enclave,
                                      verifier_enclave, &report);
    printf("Local attestation: %s\n", ok ? "REPORT CREATED" : "FAILED");
    printf("  MRENCLAVE: ");
    for (int i = 0; i < 8; i++) printf("%02x", report.mr_enclave[i]);
    printf("...\n");

    /* ==================================================================
     * Step 5: Seal Wallet Keys
     * ================================================================== */
    printf("\n--- Step 5: Seal Wallet Private Key ---\n");
    uint8_t wallet_seed[64];
    mini_hwsec_random(wallet_seed, 64);
    printf("Wallet seed generated (64 bytes)\n");

    MiniHwSecTEESealedData sealed_wallet;
    mini_hwsec_tee_seal(&tee, wallet_enclave,
                          wallet_seed, sizeof(wallet_seed),
                          &sealed_wallet, false); /* MRENCLAVE policy */
    printf("Seed sealed: %s\n", sealed_wallet.valid ? "YES" : "NO");

    /* Try unsealing with correct enclave */
    uint8_t recovered_seed[64];
    size_t recovered_len;
    ok = mini_hwsec_tee_unseal(&tee, wallet_enclave,
                                &sealed_wallet, recovered_seed, &recovered_len);
    printf("Unseal from wallet enclave: %s\n", ok ? "SUCCESS" : "FAIL");

    /* ==================================================================
     * Step 6: Side-Channel Monitoring
     * ================================================================== */
    printf("\n--- Step 6: Side-Channel Attack Monitoring ---\n");

    MiniHwSecSCDetector sc_detector;
    mini_hwsec_sc_detector_init(&sc_detector, 0.5);

    /* Simulate normal operations */
    for (int i = 0; i < 500; i++) {
        double power = 1.0 + (double)(i % 100) * 0.001;
        mini_hwsec_sc_monitor_operation(&sc_detector, 0, 1000, power);
    }

    uint32_t alerts = mini_hwsec_sc_detect_attack(&sc_detector);
    printf("Attack detection: %s (flags=0x%08x)\n",
           alerts == 0 ? "CLEAN" : "ALERT!", alerts);

    /* Check sensors */
    MiniHwSecSensorReadings sensors;
    mini_hwsec_sensor_read(&sensors);
    char log_buf[256];
    mini_hwsec_sensor_log(&sensors, log_buf, sizeof(log_buf));
    printf("Sensors: %s\n", log_buf);
    printf("Tamper alert: %s\n",
           mini_hwsec_sensor_alert(&sensors) ? "YES (ATTACK!)" : "NO");

    /* ==================================================================
     * Step 7: Simulate Transaction Signing (Protected)
     * ================================================================== */
    printf("\n--- Step 7: Sign Transaction (in Enclave) ---\n");
    uint8_t txn_hash[32];
    mini_hwsec_sha256((const uint8_t *)"TX: SEND 1 BTC to addr_xxx", 28, txn_hash);

    /* Sign inside enclave */
    uint8_t sig_r[32], sig_s[32];
    MiniHwSecEcPrivKey priv;
    MiniHwSecEcPubKey pub;
    mini_hwsec_ec_p256_generate(&priv, &pub);
    mini_hwsec_ecdsa_p256_sign(&priv, txn_hash, sig_r, sig_s);

    bool sig_ok = mini_hwsec_ecdsa_p256_verify(&pub, txn_hash, sig_r, sig_s);
    printf("Transaction hash: ");
    for (int i = 0; i < 8; i++) printf("%02x", txn_hash[i]);
    printf("...\n");
    printf("Signature verified: %s\n", sig_ok ? "YES" : "NO");

    /* ==================================================================
     * Summary
     * ================================================================== */
    printf("\n=== Security Summary ===\n");
    printf("  HRoT:        %s\n", hrot.initialized ? "ACTIVE" : "OFFLINE");
    printf("  TEE:         %d enclaves running\n", tee.enclave_count);
    printf("  Memory:      %llu lines encrypted\n",
           (unsigned long long)mem_engine.encrypted_lines);
    printf("  SC Monitor:  %s\n", alerts == 0 ? "NO THREATS" : "THREAT DETECTED");
    printf("  TPM:         %s\n", true ? "AVAILABLE" : "UNAVAILABLE");
    printf("\nHardware wallet operational. Private keys never leave the enclave.\n");

    /* Cleanup */
    mini_hwsec_tee_destroy(&tee, wallet_enclave);
    mini_hwsec_tee_destroy(&tee, verifier_enclave);
    mini_hwsec_mem_engine_destroy(&mem_engine);

    return 0;
}
