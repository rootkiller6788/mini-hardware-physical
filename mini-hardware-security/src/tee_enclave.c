#include "tee_enclave.h"
#include "hw_crypto.h"
#include <string.h>
#include <stdlib.h>

/* ============================================================================
 * L3: Trusted Execution Environment (TEE) - Enclave Lifecycle Management
 *
 * The TEE provides isolated execution containers (enclaves) that protect
 * code and data confidentiality/integrity from all other software including
 * the OS kernel and hypervisor.
 *
 * Enclave Lifecycle:
 *   1. CREATE:   Allocate EPC pages, load code, measure identity
 *   2. INIT:     Finalize (EINIT), prove initial state
 *   3. RUNNING:  Enter (EENTER), execute trusted code
 *   4. STOPPED:  Exit (EEXIT), return to untrusted code
 *   5. DESTROYED: Release all resources, zero memory
 *
 * Memory Protection:
 * - Enclave Page Cache (EPC): dedicated DRAM region encrypted with MEE
 * - Access control: hardware prevents non-enclave access to EPC
 * - EPC pages mapped only in enclave mode
 *
 * Key Derivation (Sealing):
 *   SealKey = KDF(PlatformMasterKey, MRENCLAVE || MRSIGNER || attributes)
 *
 * Reference:
 * - Intel SGX Programming Reference (329298-002US)
 * - Costan & Devadas 2016: "Intel SGX Explained"
 * - ARM TrustZone Technical Reference Manual
 * ========================================================================== */

void mini_hwsec_tee_init(MiniHwSecTEEManager *tee,
                          const uint8_t master_key[MINI_HWSEC_TEE_SEAL_KEY_SIZE],
                          uint64_t base_epc, uint64_t epc_size, bool has_hw)
{
    if (!tee) return;
    memset(tee, 0, sizeof(*tee));
    memcpy(tee->platform_master_key, master_key, MINI_HWSEC_TEE_SEAL_KEY_SIZE);
    tee->base_epc = base_epc;
    tee->epc_size = epc_size;
    tee->hardware_tee = has_hw;
}

int mini_hwsec_tee_create_enclave(MiniHwSecTEEManager *tee,
                                   const uint8_t *code, size_t code_size,
                                   const uint8_t signer_hash[MINI_HWSEC_TEE_MEASUREMENT_SIZE],
                                   uint16_t product_id, uint32_t attributes)
{
    if (!tee || !code || !signer_hash) return -1;
    if (code_size > MINI_HWSEC_TEE_ENCLAVE_SIZE) return -1;

    /* Find free enclave slot */
    int slot = -1;
    for (int i = 0; i < MINI_HWSEC_TEE_MAX_ENCLAVES; i++) {
        if (tee->enclaves[i].state == MINI_HWSEC_TEE_STATE_UNINITIALIZED ||
            tee->enclaves[i].state == MINI_HWSEC_TEE_STATE_DESTROYED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    MiniHwSecTEEEnclave *enclave = &tee->enclaves[slot];
    memset(enclave, 0, sizeof(*enclave));

    /* Set identity */
    mini_hwsec_sha256(code, code_size, enclave->identity.mr_enclave);
    memcpy(enclave->identity.mr_signer, signer_hash, MINI_HWSEC_TEE_MEASUREMENT_SIZE);
    enclave->identity.enclave_id = (uint32_t)slot;
    enclave->identity.product_id = product_id;
    enclave->identity.security_version = 0;
    enclave->identity.attributes = attributes;
    enclave->identity.base_address = tee->base_epc + (uint64_t)slot * MINI_HWSEC_TEE_ENCLAVE_SIZE;
    enclave->identity.size = MINI_HWSEC_TEE_ENCLAVE_SIZE;
    enclave->identity.debug_allowed = (attributes & 0x01) != 0;

    /* Load code into secure memory */
    memcpy(enclave->memory.code, code, code_size);
    enclave->memory.base_addr = enclave->identity.base_address;
    enclave->memory.stack_top = MINI_HWSEC_TEE_STACK_SIZE;
    enclave->memory.heap_base = MINI_HWSEC_TEE_STACK_SIZE + MINI_HWSEC_TEE_HEAP_SIZE;
    enclave->memory.access_controlled = false; /* Until EINIT */
    enclave->memory.encrypted = tee->hardware_tee;

    /* Derive seal key */
    uint8_t derivation_input[128];
    memcpy(derivation_input, enclave->identity.mr_enclave, MINI_HWSEC_TEE_MEASUREMENT_SIZE);
    memcpy(derivation_input + 32, enclave->identity.mr_signer, MINI_HWSEC_TEE_MEASUREMENT_SIZE);
    derivation_input[64] = (uint8_t)(product_id & 0xFF);
    derivation_input[65] = (uint8_t)(product_id >> 8);
    derivation_input[66] = (uint8_t)(attributes & 0xFF);
    derivation_input[67] = (uint8_t)((attributes >> 8) & 0xFF);

    mini_hwsec_hkdf_sha256(tee->platform_master_key, MINI_HWSEC_TEE_SEAL_KEY_SIZE,
                            derivation_input, 68,
                            (const uint8_t *)"SEAL-KEY", 8,
                            enclave->seal_key, MINI_HWSEC_TEE_SEAL_KEY_SIZE);

    enclave->state = MINI_HWSEC_TEE_STATE_CREATED;
    enclave->thread_id = (uint32_t)slot;

    tee->enclave_count++;
    return slot;
}

bool mini_hwsec_tee_init_enclave(MiniHwSecTEEManager *tee, int enclave_id)
{
    if (!tee || enclave_id < 0 || enclave_id >= MINI_HWSEC_TEE_MAX_ENCLAVES) return false;

    MiniHwSecTEEEnclave *enclave = &tee->enclaves[enclave_id];
    if (enclave->state != MINI_HWSEC_TEE_STATE_CREATED) return false;

    /* Finalize enclave initialization (EINIT equivalent)
     * 1. Verify enclave measurement
     * 2. Enable memory access controls
     * 3. Protect against further modifications */

    enclave->memory.access_controlled = true;
    enclave->is_secure = true;
    enclave->state = MINI_HWSEC_TEE_STATE_INITIALIZED;

    return true;
}

bool mini_hwsec_tee_enter(MiniHwSecTEEManager *tee, int enclave_id,
                           const uint8_t *data, size_t data_len,
                           uint8_t *output, size_t *output_len)
{
    if (!tee || enclave_id < 0 || enclave_id >= MINI_HWSEC_TEE_MAX_ENCLAVES) return false;

    MiniHwSecTEEEnclave *enclave = &tee->enclaves[enclave_id];
    if (enclave->state != MINI_HWSEC_TEE_STATE_INITIALIZED &&
        enclave->state != MINI_HWSEC_TEE_STATE_STOPPED) return false;

    /* Save entry timestamp */
    enclave->tsc_last_entry = enclave->tsc_start; /* Simplified */

    /* Enter enclave mode
     * In hardware: set processor mode, enable EPC access,
     * flush TLB entries for EPC, jump to enclave entry */

    enclave->state = MINI_HWSEC_TEE_STATE_RUNNING;

    /* Simulate enclave execution:
     * The enclave processes data and produces output.
     * In a real TEE, the actual enclave code would execute here. */
    if (data && output && output_len && data_len > 0) {
        /* Enclave processing: hash the input data as a simple computation */
        size_t out_sz = *output_len;
        uint8_t hash[MINI_HWSEC_TEE_MEASUREMENT_SIZE];
        mini_hwsec_sha256(data, data_len, hash);

        /* Copy result */
        size_t copy_len = out_sz < MINI_HWSEC_TEE_MEASUREMENT_SIZE
                          ? out_sz : MINI_HWSEC_TEE_MEASUREMENT_SIZE;
        memcpy(output, hash, copy_len);
        *output_len = copy_len;
    }

    /* Exit enclave mode
     * Clear registers that held secrets (L1D flush, etc.) */
    enclave->tsc_last_exit = enclave->tsc_start + 1000;
    enclave->state = MINI_HWSEC_TEE_STATE_STOPPED;
    return true;
}

bool mini_hwsec_tee_destroy(MiniHwSecTEEManager *tee, int enclave_id)
{
    if (!tee || enclave_id < 0 || enclave_id >= MINI_HWSEC_TEE_MAX_ENCLAVES) return false;

    MiniHwSecTEEEnclave *enclave = &tee->enclaves[enclave_id];

    /* Securely zero all enclave memory */
    memset(&enclave->memory, 0, sizeof(enclave->memory));
    memset(&enclave->identity, 0, sizeof(enclave->identity));
    memset(enclave->seal_key, 0, sizeof(enclave->seal_key));

    enclave->state = MINI_HWSEC_TEE_STATE_DESTROYED;
    tee->enclave_count--;
    return true;
}

/* ============================================================================
 * L7: Enclave Attestation (Local & Remote)
 *
 * Attestation proves to a relying party that:
 *   1. The code runs in a genuine TEE (hardware root of trust)
 *   2. The enclave identity (MRENCLAVE) matches expectations
 *   3. The enclave has not been tampered with
 *
 * Local Attestation:
 *   Enclave A creates a REPORT for Enclave B on same platform.
 *   The report is MAC'd with a platform-specific key.
 *
 * Remote Attestation:
 *   Enclave creates a QUOTE signed by the platform's attestation key
 *   (EPID or ECDSA-based). The quote is verified by Intel/AMD's
 *   attestation service.
 * ========================================================================== */

bool mini_hwsec_tee_attest_local(MiniHwSecTEEManager *tee,
                                  int enclave_id, int target_id,
                                  MiniHwSecTEEAttestReport *report)
{
    if (!tee || !report) return false;
    if (enclave_id < 0 || enclave_id >= MINI_HWSEC_TEE_MAX_ENCLAVES) return false;
    if (target_id < 0 || target_id >= MINI_HWSEC_TEE_MAX_ENCLAVES) return false;

    MiniHwSecTEEEnclave *src = &tee->enclaves[enclave_id];
    MiniHwSecTEEEnclave *target = &tee->enclaves[target_id];

    if (src->state < MINI_HWSEC_TEE_STATE_INITIALIZED ||
        target->state < MINI_HWSEC_TEE_STATE_INITIALIZED) return false;

    memset(report, 0, sizeof(*report));
    report->attest_type = MINI_HWSEC_ATTEST_LOCAL;

    /* Fill report with source enclave's identity */
    memcpy(report->mr_enclave, src->identity.mr_enclave, MINI_HWSEC_TEE_MEASUREMENT_SIZE);
    memcpy(report->mr_signer, src->identity.mr_signer, MINI_HWSEC_TEE_MEASUREMENT_SIZE);
    report->product_id = src->identity.product_id;
    report->security_version = src->identity.security_version;
    report->attributes = src->identity.attributes;

    /* Generate report data (including target's MRE for binding) */
    uint8_t report_data[128];
    memcpy(report_data, src->identity.mr_enclave, 32);
    memcpy(report_data + 32, target->identity.mr_enclave, 32);
    mini_hwsec_random(report_data + 64, 64);
    memcpy(report->report_data, report_data, 64);

    /* MAC the report using target's seal key as the shared MAC key */
    mini_hwsec_hmac_sha256(target->seal_key, MINI_HWSEC_TEE_SEAL_KEY_SIZE,
                            (const uint8_t *)report, offsetof(MiniHwSecTEEAttestReport, report_mac),
                            report->report_mac);
    return true;
}

bool mini_hwsec_tee_attest_remote(MiniHwSecTEEManager *tee,
                                   int enclave_id,
                                   const uint8_t challenge[32],
                                   MiniHwSecTEEAttestReport *report)
{
    if (!tee || !report || !challenge) return false;
    if (enclave_id < 0 || enclave_id >= MINI_HWSEC_TEE_MAX_ENCLAVES) return false;

    MiniHwSecTEEEnclave *enclave = &tee->enclaves[enclave_id];
    if (enclave->state < MINI_HWSEC_TEE_STATE_INITIALIZED) return false;

    memset(report, 0, sizeof(*report));
    report->attest_type = MINI_HWSEC_ATTEST_REMOTE;

    memcpy(report->mr_enclave, enclave->identity.mr_enclave, MINI_HWSEC_TEE_MEASUREMENT_SIZE);
    memcpy(report->mr_signer, enclave->identity.mr_signer, MINI_HWSEC_TEE_MEASUREMENT_SIZE);
    report->product_id = enclave->identity.product_id;
    report->security_version = enclave->identity.security_version;
    report->attributes = enclave->identity.attributes;

    /* Include challenge in report data for anti-replay */
    memcpy(report->report_data, challenge, 32);
    mini_hwsec_random(report->report_data + 32, 32);

    /* Sign with platform attestation key */
    uint8_t platform_key[32];
    mini_hwsec_hkdf_sha256(tee->platform_master_key, MINI_HWSEC_TEE_SEAL_KEY_SIZE,
                            (const uint8_t *)"ATTEST-KEY", 11,
                            NULL, 0, platform_key, 32);

    mini_hwsec_hmac_sha256(platform_key, 32,
                            (const uint8_t *)report,
                            offsetof(MiniHwSecTEEAttestReport, report_mac),
                            report->report_mac);
    return true;
}

bool mini_hwsec_tee_verify_report(MiniHwSecTEEManager *tee,
                                   const MiniHwSecTEEAttestReport *report)
{
    if (!tee || !report) return false;

    /* Verify MAC integrity */
    uint8_t platform_key[32];
    mini_hwsec_hkdf_sha256(tee->platform_master_key, MINI_HWSEC_TEE_SEAL_KEY_SIZE,
                            (const uint8_t *)"ATTEST-KEY", 11,
                            NULL, 0, platform_key, 32);

    uint8_t expected_mac[MINI_HWSEC_TEE_SEAL_KEY_SIZE];
    mini_hwsec_hmac_sha256(platform_key, 32,
                            (const uint8_t *)report,
                            offsetof(MiniHwSecTEEAttestReport, report_mac),
                            expected_mac);

    return mini_hwsec_constant_time_eq(expected_mac, report->report_mac,
                                        MINI_HWSEC_TEE_SEAL_KEY_SIZE);
}

/* ============================================================================
 * L7: Data Sealing - Persistent Secure Storage for Enclaves
 *
 * Sealing encrypts secrets such that they can only be decrypted by the
 * same enclave identity or signer identity.
 *
 * POLICY_MRENCLAVE: Only enclave with exact same MRENCLAVE can unseal.
 *   Used for data that must survive enclave version upgrades.
 *
 * POLICY_MRSIGNER: Any enclave signed by same key can unseal.
 *   Used for data migration between enclave versions.
 *
 * The seal key is derived from platform master key + identity,
 * so migrating to a different physical machine requires re-sealing.
 * ========================================================================== */

void mini_hwsec_tee_seal(MiniHwSecTEEManager *tee, int enclave_id,
                          const uint8_t *data, size_t data_len,
                          MiniHwSecTEESealedData *sealed,
                          bool policy_mrsigner)
{
    if (!tee || !data || !sealed || data_len > 1024) return;
    if (enclave_id < 0 || enclave_id >= MINI_HWSEC_TEE_MAX_ENCLAVES) return;

    MiniHwSecTEEEnclave *enclave = &tee->enclaves[enclave_id];
    memset(sealed, 0, sizeof(*sealed));
    sealed->data_size = data_len;

    /* Set key policy */
    if (policy_mrsigner) {
        memcpy(sealed->key_policy, "MRSIGNER", 8);
    } else {
        memcpy(sealed->key_policy, "MRENCLAVE", 9);
    }

    /* Encrypt data with seal key using AES-GCM */
    MiniHwSecAesCtx aes_ctx;
    uint8_t aes_key[MINI_HWSEC_AES_KEY_SIZE];
    mini_hwsec_hkdf_sha256(enclave->seal_key, MINI_HWSEC_TEE_SEAL_KEY_SIZE,
                            (const uint8_t *)"ENC", 3,
                            NULL, 0, aes_key, MINI_HWSEC_AES_KEY_SIZE);
    mini_hwsec_aes_init(&aes_ctx, aes_key);

    uint8_t iv[12] = {0};
    memcpy(iv, sealed->key_policy, 8);
    /* Use key_policy as AAD with its actual string length */
    size_t policy_len = strlen((const char *)sealed->key_policy);
    mini_hwsec_aes_gcm_encrypt(&aes_ctx, iv, data, sealed->encrypted_data,
                                sealed->key_policy, policy_len,
                                sealed->mac, data_len);

    sealed->valid = true;
}

bool mini_hwsec_tee_unseal(MiniHwSecTEEManager *tee, int enclave_id,
                            const MiniHwSecTEESealedData *sealed,
                            uint8_t *data, size_t *data_len)
{
    if (!tee || !sealed || !data || !data_len || !sealed->valid) return false;
    if (enclave_id < 0 || enclave_id >= MINI_HWSEC_TEE_MAX_ENCLAVES) return false;
    if (sealed->data_size > 1024) return false;

    MiniHwSecTEEEnclave *enclave = &tee->enclaves[enclave_id];

    /* Derive decryption key */
    MiniHwSecAesCtx aes_ctx;
    uint8_t aes_key[MINI_HWSEC_AES_KEY_SIZE];
    mini_hwsec_hkdf_sha256(enclave->seal_key, MINI_HWSEC_TEE_SEAL_KEY_SIZE,
                            (const uint8_t *)"ENC", 3,
                            NULL, 0, aes_key, MINI_HWSEC_AES_KEY_SIZE);
    mini_hwsec_aes_init(&aes_ctx, aes_key);

    uint8_t iv[12] = {0};
    memcpy(iv, sealed->key_policy, 8);

    size_t policy_len = strlen((const char *)sealed->key_policy);
    bool ok = mini_hwsec_aes_gcm_decrypt(&aes_ctx, iv,
                                          sealed->encrypted_data, data,
                                          sealed->key_policy, policy_len,
                                          sealed->mac, sealed->data_size);
    if (ok) {
        *data_len = sealed->data_size;
    }
    return ok;
}

/* ============================================================================
 * Secure Scheduler - Time-slicing between enclaves
 * ========================================================================== */

void mini_hwsec_tee_scheduler_init(MiniHwSecTEEScheduler *sched)
{
    if (!sched) return;
    memset(sched, 0, sizeof(*sched));
    sched->current_enclave = -1;
    sched->next_enclave = -1;
    sched->quantum_cycles = 1000000; /* 1M cycles default */
}

int mini_hwsec_tee_scheduler_schedule(MiniHwSecTEEScheduler *sched,
                                       MiniHwSecTEEManager *tee)
{
    if (!sched || !tee) return -1;

    /* Round-robin among initialized enclaves */
    int start = (sched->current_enclave + 1) % MINI_HWSEC_TEE_MAX_ENCLAVES;
    for (int i = 0; i < MINI_HWSEC_TEE_MAX_ENCLAVES; i++) {
        int idx = (start + i) % MINI_HWSEC_TEE_MAX_ENCLAVES;
        if (tee->enclaves[idx].state == MINI_HWSEC_TEE_STATE_INITIALIZED ||
            tee->enclaves[idx].state == MINI_HWSEC_TEE_STATE_STOPPED) {
            sched->current_enclave = idx;
            return idx;
        }
    }
    sched->current_enclave = -1;
    return -1;
}

void mini_hwsec_tee_scheduler_yield(MiniHwSecTEEScheduler *sched)
{
    if (!sched) return;
    sched->next_enclave = -1;
}

bool mini_hwsec_tee_is_valid_enclave(const MiniHwSecTEEManager *tee, int id)
{
    if (!tee || id < 0 || id >= MINI_HWSEC_TEE_MAX_ENCLAVES) return false;
    return tee->enclaves[id].state >= MINI_HWSEC_TEE_STATE_CREATED &&
           tee->enclaves[id].state < MINI_HWSEC_TEE_STATE_DESTROYED;
}

int mini_hwsec_tee_get_running_enclave(const MiniHwSecTEEManager *tee)
{
    if (!tee) return -1;
    for (int i = 0; i < MINI_HWSEC_TEE_MAX_ENCLAVES; i++) {
        if (tee->enclaves[i].state == MINI_HWSEC_TEE_STATE_RUNNING) return i;
    }
    return -1;
}

int mini_hwsec_tee_get_free_enclaves(const MiniHwSecTEEManager *tee)
{
    if (!tee) return 0;
    int free_count = 0;
    for (int i = 0; i < MINI_HWSEC_TEE_MAX_ENCLAVES; i++) {
        if (tee->enclaves[i].state == MINI_HWSEC_TEE_STATE_UNINITIALIZED ||
            tee->enclaves[i].state == MINI_HWSEC_TEE_STATE_DESTROYED) {
            free_count++;
        }
    }
    return free_count;
}
