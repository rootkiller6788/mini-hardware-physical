#include <stdio.h>
#include <string.h>
#include "secure_boot.h"
#include "hw_crypto.h"

/* ============================================================================
 * L6 Canonical Problem: Secure Boot Implementation
 *
 * Demonstrates a complete measured boot flow:
 * ROM → BL1 → BL2 → Kernel with TPM measurements and attestation.
 *
 * This pattern implements the Trusted Computing Group (TCG) PC Client
 * specification with PCR measurements and event logging.
 * ========================================================================== */
int main(void)
{
    printf("=== Mini Secure Boot Demonstration ===\n\n");

    /* Initialize security infrastructure */
    MiniHwSecBootConfig config;
    MiniHwSecTPM tpm;
    MiniHwSecEventLog log;
    mini_hwsec_sb_init(&config, &tpm, &log);

    printf("TPM initialized with %d PCR registers\n", tpm.pcr_count);
    printf("Secure boot policy: ENFORCE\n\n");

    /* Setup a 3-stage boot chain */
    MiniHwSecBootChain chain;
    memset(&chain, 0, sizeof(chain));
    chain.stage_count = 3;

    /* Stage 0: ROM (implicitly trusted) */
    memcpy(chain.stages[0].stage_name, "ROM", 3);
    chain.stages[0].stage = MINI_HWSEC_SB_STAGE_ROM;
    chain.stages[0].version = 1;
    chain.stages[0].load_address = 0xFFFF0000;
    chain.stages[0].signature_valid = true; /* ROM is immutable */
    mini_hwsec_random(chain.stages[0].binary_hash, 32);

    /* Stage 1: Bootloader Stage 1 (BL1) */
    memcpy(chain.stages[1].stage_name, "BL1", 3);
    chain.stages[1].stage = MINI_HWSEC_SB_STAGE_BL1;
    chain.stages[1].version = 2;
    chain.stages[1].load_address = 0x80000000;
    chain.stages[1].signature_valid = true;
    mini_hwsec_random(chain.stages[1].binary_hash, 32);

    /* Stage 2: Kernel */
    memcpy(chain.stages[2].stage_name, "Linux-6.1", 9);
    chain.stages[2].stage = MINI_HWSEC_SB_STAGE_KERNEL;
    chain.stages[2].version = 3;
    chain.stages[2].load_address = 0x80200000;
    chain.stages[2].signature_valid = true;
    mini_hwsec_random(chain.stages[2].binary_hash, 32);

    printf("Boot chain:\n");
    printf("  [0] ROM          (immutable, implicit trust)\n");
    printf("  [1] BL1 v2       @0x80000000\n");
    printf("  [2] Linux-6.1 v3 @0x80200000\n\n");

    /* Execute secure boot chain */
    printf("Executing secure boot chain...\n");
    bool ok = mini_hwsec_sb_execute_chain(&chain, &config, &tpm, &log);

    if (ok && chain.boot_successful) {
        printf("Secure boot: SUCCESS ✓\n\n");
    } else {
        printf("Secure boot: FAILED at stage %u ✗\n", chain.failed_stage);
        return 1;
    }

    /* Display PCR values */
    printf("PCR values after boot:\n");
    for (int i = 0; i < 8; i++) {
        uint8_t pcr_val[32];
        if (mini_hwsec_tpm_pcr_read(&tpm, (uint32_t)i, pcr_val)) {
            printf("  PCR[%d]: ", i);
            for (int j = 0; j < 8; j++) printf("%02x", pcr_val[j]);
            printf("...\n");
        }
    }

    /* Generate TPM Quote for remote attestation */
    printf("\nGenerating TPM Quote for remote attestation...\n");
    uint8_t nonce[32] = "RemoteVerifierChallenge12345XX";
    MiniHwSecTPMQuote quote;
    if (mini_hwsec_tpm_quote(&tpm, 0x000F, nonce, &quote)) {
        printf("Quote generated successfully\n");
        printf("  PCR selection: 0x%02x%02x%02x\n",
               quote.pcr_select[0], quote.pcr_select[1], quote.pcr_select[2]);
        printf("  Composite hash: ");
        for (int j = 0; j < 8; j++) printf("%02x", quote.pcr_composite_hash[j]);
        printf("...\n");
        printf("  Quote valid: %s\n", quote.valid ? "YES" : "NO");
    }

    /* Print event log */
    mini_hwsec_event_log_print(&log);

    /* Demonstrate rollback detection */
    printf("\nRollback detection test:\n");
    bool rollback = mini_hwsec_sb_rollback_detect(&chain.stages[1], 3);
    printf("  BL1 v2 vs required v3: %s\n",
           rollback ? "ROLLBACK DETECTED" : "OK");

    return 0;
}
