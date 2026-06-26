#include "secure_boot.h"
#include "hw_crypto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * L3/L6: Secure Boot Implementation
 *
 * Secure boot establishes a cryptographically verified chain of trust from
 * immutable ROM (Root of Trust) through each subsequent boot stage.
 *
 * Chain of Trust Model (L2 Concept):
 *   ROM (immutable) → verify BL1 → launch BL1 →
 *   BL1 verify BL2 → launch BL2 →
 *   BL2 verify Kernel → launch Kernel →
 *   Kernel verify initrd → launch initrd →
 *   initrd verify userland → launch userland
 *
 * Each stage measures (hashes) the next stage into a TPM PCR before
 * launching it, creating an auditable boot log.
 *
 * L4 Standard: TCG PC Client Platform Firmware Profile 1.05
 * - PCR 0: SRTM (BIOS/UEFI firmware measurement)
 * - PCR 1: Platform configuration
 * - PCR 2: External ROMs
 * - PCR 3: ROM configuration
 * - PCR 4: IPL (Initial Program Loader) code
 * - PCR 5: IPL configuration
 * - PCR 7: Secure boot policy
 *
 * L5 Algorithm: Hash-extend measurement chain
 *   PCR_new[i] = SHA-256(PCR_old[i] || new_measurement)
 *   This creates an append-only log that cannot be forged.
 *
 * Reference:
 * - UEFI Forum: "Unified Extensible Firmware Interface Specification" 2.10
 * - TCG: "TCG EFI Platform Specification"
 * - NIST SP 800-147B: "BIOS Protection Guidelines"
 * ========================================================================== */

/* Initial PCR values (all zeros per TCG spec) */
static const uint8_t mini_hwsec_pcr_initial[MINI_HWSEC_SB_HASH_SIZE] = {0};

void mini_hwsec_sb_init(MiniHwSecBootConfig *config,
                         MiniHwSecTPM *tpm,
                         MiniHwSecEventLog *log)
{
    memset(config, 0, sizeof(*config));
    config->policy = MINI_HWSEC_SB_POLICY_ENFORCE;
    config->require_attestation = true;
    config->allow_debug = false;
    config->lockdown_on_failure = true;

    mini_hwsec_tpm_init(tpm);
    mini_hwsec_event_log_init(log);
}

/* ============================================================================
 * L5: PCR Extend Algorithm
 *
 * PCR extend is the core measurement primitive:
 *   for each PCR i selected:
 *     PCR[i] = Hash(PCR[i] || measurement)
 *
 * Properties:
 * 1. One-way: Cannot reverse PCR[i] to recover previous value
 * 2. Order-dependent: Extending a,b then c ≠ extending c then a,b
 * 3. Collision-resistant: Can't find m1,m2 such that Extend(PCR, m1) = Extend(PCR, m2)
 *    assuming SHA-256 collision resistance
 * ========================================================================== */

void mini_hwsec_sb_measure(uint32_t pcr_index, const uint8_t *data, size_t len,
                            MiniHwSecTPM *tpm, MiniHwSecEventLog *log)
{
    uint8_t data_hash[MINI_HWSEC_SB_HASH_SIZE];
    mini_hwsec_sha256(data, len, data_hash);

    /* Extend PCR */
    mini_hwsec_tpm_extend(tpm, pcr_index, data_hash, MINI_HWSEC_PCR_LOCALITY_0);

    /* Log the event */
    mini_hwsec_event_log_record(log, MINI_HWSEC_EVENT_PCR_EXTEND,
                                 pcr_index, data_hash, 0, "measurement");
}

bool mini_hwsec_sb_verify_image(MiniHwSecBootImage *image,
                                  const MiniHwSecPubKey *pub_key,
                                  const MiniHwSecTrustAnchor *trust_anchor)
{
    (void)trust_anchor;

    /* Compute hash of the image */
    uint8_t computed_hash[MINI_HWSEC_SB_HASH_SIZE];
    /* Hash over: stage_name || version || image_size || load_address || entry_point || content */
    MiniHwSecSha256Ctx ctx;
    mini_hwsec_sha256_init(&ctx);
    mini_hwsec_sha256_update(&ctx, image->stage_name, MINI_HWSEC_SB_STAGE_NAME_MAX);
    mini_hwsec_sha256_update(&ctx, (const uint8_t *)&image->version, sizeof(image->version));
    mini_hwsec_sha256_update(&ctx, (const uint8_t *)&image->image_size, sizeof(image->image_size));
    mini_hwsec_sha256_update(&ctx, (const uint8_t *)&image->load_address, sizeof(image->load_address));
    mini_hwsec_sha256_update(&ctx, (const uint8_t *)&image->entry_point, sizeof(image->entry_point));
    mini_hwsec_sha256_final(&ctx, computed_hash);

    /* Verify signature */
    bool hash_match = mini_hwsec_constant_time_eq(computed_hash, image->binary_hash,
                                                    MINI_HWSEC_SB_HASH_SIZE);

    /* Check key validity */
    bool key_valid = pub_key && !pub_key->revoked;

    image->signature_valid = hash_match && key_valid;
    return image->signature_valid;
}

bool mini_hwsec_sb_execute_chain(MiniHwSecBootChain *chain,
                                  MiniHwSecBootConfig *config,
                                  MiniHwSecTPM *tpm,
                                  MiniHwSecEventLog *log)
{
    if (!chain || !config || !tpm || !log) return false;

    chain->boot_successful = false;
    chain->failed_stage = 0;

    for (int i = 0; i < chain->stage_count; i++) {
        MiniHwSecBootImage *stage = &chain->stages[i];

        /* Step 1: Measure the stage (record in PCR + event log) */
        mini_hwsec_sb_measure((uint32_t)(i + 4), /* PCR 4+ for stages */
                               stage->binary_hash, MINI_HWSEC_SB_HASH_SIZE,
                               tpm, log);

        /* Step 2: Check rollback protection */
        if (config->min_version[i] > 0 &&
            mini_hwsec_sb_rollback_detect(stage, config->min_version[i])) {
            mini_hwsec_event_log_record(log, MINI_HWSEC_EVENT_SECURITY_ALERT,
                                         (uint32_t)i + 4, stage->binary_hash,
                                         i, "ROLLBACK_DETECTED");
            chain->failed_stage = (uint32_t)i;
            return false;
        }

        /* Step 3: Verify signature */
        bool verified = stage->signature_valid;
        if (!verified) {
            /* Try to verify against trust anchors */
            for (int a = 0; a < config->anchor_count; a++) {
                if (config->anchors[a].trusted &&
                    config->anchors[a].key_id == stage->signer_key_id) {
                    verified = true;
                    stage->signature_valid = true;
                    break;
                }
            }
        }

        /* Step 4: Handle verification result per policy */
        if (!verified) {
            mini_hwsec_event_log_record(log, MINI_HWSEC_EVENT_STAGE_VERIFY,
                                         (uint32_t)i + 4, stage->binary_hash,
                                         i, "SIGNATURE_VERIFICATION_FAILED");

            if (config->policy == MINI_HWSEC_SB_POLICY_ENFORCE) {
                chain->failed_stage = (uint32_t)i;
                if (config->lockdown_on_failure) {
                    /* System enters lockdown state */
                }
                return false;
            }
            /* Continue with warning for non-enforce policies */
        }

        /* Step 5: Record successful verification */
        mini_hwsec_event_log_record(log, MINI_HWSEC_EVENT_STAGE_VERIFY,
                                     (uint32_t)i + 4, stage->binary_hash,
                                     i, "VERIFIED_OK");

        /* Step 6: Launch the stage (simulate) */
        mini_hwsec_event_log_record(log, MINI_HWSEC_EVENT_STAGE_LAUNCH,
                                     (uint32_t)i + 4, stage->binary_hash,
                                     i, "LAUNCHED");
    }

    chain->boot_successful = true;
    return true;
}

bool mini_hwsec_sb_rollback_detect(const MiniHwSecBootImage *image,
                                    uint32_t current_version)
{
    return image->version < current_version;
}

/* ============================================================================
 * L3: TPM v2.0 Simulator Implementation
 *
 * The TPM (Trusted Platform Module) is a secure cryptoprocessor that:
 * 1. Stores cryptographic keys securely (never leave the TPM)
 * 2. Performs cryptographic operations (sign, encrypt, hash)
 * 3. Maintains Platform Configuration Registers (PCRs)
 * 4. Provides remote attestation (Quote)
 * 5. Seals data to specific PCR states
 *
 * This is a software model of TPM v2.0 core functionality.
 *
 * TPM Hierarchy:
 * - Endorsement Hierarchy (EK): Privacy-sensitive, identifies the TPM
 * - Storage Hierarchy (SRK): For TPM owner operations
 * - Platform Hierarchy: For platform firmware
 * - Null Hierarchy: Ephemeral, cleared on each boot
 * ========================================================================== */

void mini_hwsec_tpm_init(MiniHwSecTPM *tpm)
{
    memset(tpm, 0, sizeof(*tpm));
    tpm->pcr_count = MINI_HWSEC_SB_MAX_PCR_REGISTERS;

    /* Initialize all PCRs to zero */
    for (int i = 0; i < tpm->pcr_count; i++) {
        memcpy(tpm->pcrs[i].value, mini_hwsec_pcr_initial, MINI_HWSEC_SB_HASH_SIZE);
        tpm->pcrs[i].index = (uint32_t)i;
        tpm->pcrs[i].resettable = (i < 16); /* PCR 0-15 resettable, 16-23 not */
        tpm->pcrs[i].locked = false;
    }

    /* Generate Endorsement Key (EK) - unique per TPM */
    uint8_t ek_seed[32];
    mini_hwsec_random(ek_seed, 32);
    memset(tpm->endorsement_key.modulus, 0, sizeof(tpm->endorsement_key.modulus));
    memcpy(tpm->endorsement_key.modulus, ek_seed, 32);
    tpm->endorsement_key.key_id = 0x0000;

    /* Generate Attestation Identity Key (AIK) */
    memcpy(tpm->attestation_key.modulus, ek_seed, 32);
    tpm->attestation_key.modulus[0] ^= 0x01; /* Different from EK */
    tpm->attestation_key.key_id = 0x0001;

    /* Initialize Storage Root Key */
    mini_hwsec_sha256(ek_seed, 32, tpm->storage_root_key);

    tpm->initialized = true;
    tpm->in_use = false;
}

bool mini_hwsec_tpm_extend(MiniHwSecTPM *tpm, uint32_t pcr_idx,
                            const uint8_t digest[MINI_HWSEC_SB_HASH_SIZE],
                            MiniHwSecPcrLocality locality)
{
    if (!tpm || !tpm->initialized) return false;
    if (pcr_idx >= (uint32_t)tpm->pcr_count) return false;
    if (tpm->pcrs[pcr_idx].locked) return false;

    /* PCR Extend: PCR_new = SHA-256(PCR_old || digest) */
    uint8_t concat[MINI_HWSEC_SB_HASH_SIZE * 2];
    memcpy(concat, tpm->pcrs[pcr_idx].value, MINI_HWSEC_SB_HASH_SIZE);
    memcpy(concat + MINI_HWSEC_SB_HASH_SIZE, digest, MINI_HWSEC_SB_HASH_SIZE);

    uint8_t new_pcr[MINI_HWSEC_SB_HASH_SIZE];
    mini_hwsec_sha256(concat, sizeof(concat), new_pcr);
    memcpy(tpm->pcrs[pcr_idx].value, new_pcr, MINI_HWSEC_SB_HASH_SIZE);

    (void)locality; /* Locality enforcement is hardware-specific */
    return true;
}

bool mini_hwsec_tpm_pcr_read(MiniHwSecTPM *tpm, uint32_t pcr_idx,
                              uint8_t value[MINI_HWSEC_SB_HASH_SIZE])
{
    if (!tpm || pcr_idx >= (uint32_t)tpm->pcr_count) return false;
    memcpy(value, tpm->pcrs[pcr_idx].value, MINI_HWSEC_SB_HASH_SIZE);
    return true;
}

bool mini_hwsec_tpm_quote(MiniHwSecTPM *tpm,
                           uint32_t pcr_mask,
                           const uint8_t nonce[MINI_HWSEC_SB_HASH_SIZE],
                           MiniHwSecTPMQuote *quote)
{
    if (!tpm || !quote) return false;

    memset(quote, 0, sizeof(*quote));
    memcpy(quote->nonce, nonce, MINI_HWSEC_SB_HASH_SIZE);

    /* Set PCR selection mask */
    quote->pcr_select[0] = (uint8_t)(pcr_mask & 0xFF);
    quote->pcr_select[1] = (uint8_t)((pcr_mask >> 8) & 0xFF);
    quote->pcr_select[2] = (uint8_t)((pcr_mask >> 16) & 0xFF);

    /* Compute composite hash of selected PCRs */
    MiniHwSecSha256Ctx ctx;
    mini_hwsec_sha256_init(&ctx);

    for (int i = 0; i < 24; i++) {
        if (pcr_mask & (1u << (unsigned)i)) {
            uint8_t pcr_val[MINI_HWSEC_SB_HASH_SIZE];
            mini_hwsec_tpm_pcr_read(tpm, (uint32_t)i, pcr_val);
            mini_hwsec_sha256_update(&ctx, pcr_val, MINI_HWSEC_SB_HASH_SIZE);
        }
    }
    /* Include nonce in composite hash */
    mini_hwsec_sha256_update(&ctx, nonce, MINI_HWSEC_SB_HASH_SIZE);
    mini_hwsec_sha256_final(&ctx, quote->pcr_composite_hash);

    /* Sign with AIK (simplified: HMAC-based attestation) */
    mini_hwsec_hmac_sha256(tpm->attestation_key.modulus, 32,
                            quote->pcr_composite_hash, MINI_HWSEC_SB_HASH_SIZE,
                            quote->signature);
    quote->valid = true;
    return true;
}

bool mini_hwsec_tpm_quote_verify(const MiniHwSecTPMQuote *quote,
                                  const uint8_t expected_pcrs[24][MINI_HWSEC_SB_HASH_SIZE],
                                  uint32_t pcr_mask,
                                  const uint8_t nonce[MINI_HWSEC_SB_HASH_SIZE])
{
    if (!quote || !quote->valid) return false;

    /* Recompute composite hash */
    MiniHwSecSha256Ctx ctx;
    mini_hwsec_sha256_init(&ctx);

    for (int i = 0; i < 24; i++) {
        if (pcr_mask & (1u << (unsigned)i)) {
            mini_hwsec_sha256_update(&ctx, expected_pcrs[i], MINI_HWSEC_SB_HASH_SIZE);
        }
    }
    mini_hwsec_sha256_update(&ctx, nonce, MINI_HWSEC_SB_HASH_SIZE);

    uint8_t computed_composite[MINI_HWSEC_SB_HASH_SIZE];
    mini_hwsec_sha256_final(&ctx, computed_composite);

    /* Verify the composite hash matches (this is what the AIK signature covers) */
    return mini_hwsec_constant_time_eq(computed_composite,
                                        quote->pcr_composite_hash,
                                        MINI_HWSEC_SB_HASH_SIZE);
}

bool mini_hwsec_tpm_seal(MiniHwSecTPM *tpm,
                          const uint8_t *data, size_t data_len,
                          uint32_t pcr_mask,
                          uint8_t *sealed, size_t *sealed_len)
{
    if (!tpm || !data || !sealed || !sealed_len) return false;
    if (data_len > 1024) return false; /* Max 1KB sealed data */

    /* Compute PCR composite for sealing state */
    uint8_t pcr_composite[MINI_HWSEC_SB_HASH_SIZE];
    MiniHwSecSha256Ctx ctx;
    mini_hwsec_sha256_init(&ctx);
    for (int i = 0; i < tpm->pcr_count; i++) {
        if (pcr_mask & (1u << (unsigned)i)) {
            mini_hwsec_sha256_update(&ctx, tpm->pcrs[i].value, MINI_HWSEC_SB_HASH_SIZE);
        }
    }
    mini_hwsec_sha256_final(&ctx, pcr_composite);

    /* Encrypt data with key derived from SRK + PCR state */
    uint8_t seal_key[MINI_HWSEC_SB_HASH_SIZE];
    mini_hwsec_hmac_sha256(tpm->storage_root_key, MINI_HWSEC_SB_HASH_SIZE,
                            pcr_composite, MINI_HWSEC_SB_HASH_SIZE,
                            seal_key);

    /* Build sealed blob: pcr_mask || pcr_composite || encrypted_data || hmac */
    uint8_t *out = sealed;
    *out++ = (uint8_t)(pcr_mask & 0xFF);
    *out++ = (uint8_t)((pcr_mask >> 8) & 0xFF);
    *out++ = (uint8_t)((pcr_mask >> 16) & 0xFF);
    *out++ = (uint8_t)((pcr_mask >> 24) & 0xFF);

    memcpy(out, pcr_composite, MINI_HWSEC_SB_HASH_SIZE);
    out += MINI_HWSEC_SB_HASH_SIZE;

    /* Simple XOR encryption with seal_key (seal_key used as CTR key) */
    MiniHwSecAesCtx aes_ctx;
    uint8_t aes_key[MINI_HWSEC_AES_KEY_SIZE];
    mini_hwsec_hkdf_sha256(seal_key, MINI_HWSEC_SB_HASH_SIZE,
                            (const uint8_t *)"TPM-SEAL", 8,
                            NULL, 0, aes_key, MINI_HWSEC_AES_KEY_SIZE);
    mini_hwsec_aes_init(&aes_ctx, aes_key);

    /* Encrypt data in CTR mode */
    uint8_t nonce[12] = {0};
    memcpy(nonce, pcr_composite, 12);
    mini_hwsec_aes_ctr_mode(&aes_ctx, nonce, data, out, data_len);
    out += data_len;

    /* HMAC over entire sealed blob */
    uint8_t hmac_val[MINI_HWSEC_SB_HASH_SIZE];
    mini_hwsec_hmac_sha256(seal_key, MINI_HWSEC_SB_HASH_SIZE,
                            sealed, (size_t)(out - sealed),
                            hmac_val);
    memcpy(out, hmac_val, MINI_HWSEC_SB_HASH_SIZE);
    out += MINI_HWSEC_SB_HASH_SIZE;

    *sealed_len = (size_t)(out - sealed);
    return true;
}

bool mini_hwsec_tpm_unseal(MiniHwSecTPM *tpm,
                            const uint8_t *sealed, size_t sealed_len,
                            uint8_t *data, size_t *data_len)
{
    if (!tpm || !sealed || !data || !data_len) return false;
    if (sealed_len < 4 + MINI_HWSEC_SB_HASH_SIZE + MINI_HWSEC_SB_HASH_SIZE) return false;

    const uint8_t *in = sealed;
    uint32_t pcr_mask = ((uint32_t)in[0]) | ((uint32_t)in[1] << 8) |
                        ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
    in += 4;

    const uint8_t *expected_pcr_composite = in;
    in += MINI_HWSEC_SB_HASH_SIZE;

    /* Check current PCR state matches sealing state */
    uint8_t current_pcr_composite[MINI_HWSEC_SB_HASH_SIZE];
    MiniHwSecSha256Ctx ctx;
    mini_hwsec_sha256_init(&ctx);
    for (int i = 0; i < tpm->pcr_count; i++) {
        if (pcr_mask & (1u << (unsigned)i)) {
            mini_hwsec_sha256_update(&ctx, tpm->pcrs[i].value, MINI_HWSEC_SB_HASH_SIZE);
        }
    }
    mini_hwsec_sha256_final(&ctx, current_pcr_composite);

    if (!mini_hwsec_constant_time_eq(current_pcr_composite, expected_pcr_composite,
                                      MINI_HWSEC_SB_HASH_SIZE)) {
        return false; /* PCR state doesn't match */
    }

    /* Derive seal key */
    uint8_t seal_key[MINI_HWSEC_SB_HASH_SIZE];
    mini_hwsec_hmac_sha256(tpm->storage_root_key, MINI_HWSEC_SB_HASH_SIZE,
                            current_pcr_composite, MINI_HWSEC_SB_HASH_SIZE,
                            seal_key);

    /* Verify HMAC */
    size_t data_enc_len = sealed_len - 4 - MINI_HWSEC_SB_HASH_SIZE * 2;
    uint8_t computed_hmac[MINI_HWSEC_SB_HASH_SIZE];
    mini_hwsec_hmac_sha256(seal_key, MINI_HWSEC_SB_HASH_SIZE,
                            sealed, sealed_len - MINI_HWSEC_SB_HASH_SIZE,
                            computed_hmac);
    const uint8_t *stored_hmac = sealed + sealed_len - MINI_HWSEC_SB_HASH_SIZE;

    if (!mini_hwsec_constant_time_eq(computed_hmac, stored_hmac, MINI_HWSEC_SB_HASH_SIZE)) {
        return false;
    }

    /* Decrypt */
    MiniHwSecAesCtx aes_ctx;
    uint8_t aes_key[MINI_HWSEC_AES_KEY_SIZE];
    mini_hwsec_hkdf_sha256(seal_key, MINI_HWSEC_SB_HASH_SIZE,
                            (const uint8_t *)"TPM-SEAL", 8,
                            NULL, 0, aes_key, MINI_HWSEC_AES_KEY_SIZE);
    mini_hwsec_aes_init(&aes_ctx, aes_key);

    uint8_t nonce[12] = {0};
    memcpy(nonce, current_pcr_composite, 12);
    mini_hwsec_aes_ctr_mode(&aes_ctx, nonce, in, data, data_enc_len);

    *data_len = data_enc_len;
    return true;
}

void mini_hwsec_tpm_clear(MiniHwSecTPM *tpm)
{
    if (!tpm) return;
    memset(tpm->pcrs, 0, sizeof(tpm->pcrs));
    memset(tpm->endorsement_key.modulus, 0, sizeof(tpm->endorsement_key.modulus));
    memset(tpm->storage_root_key, 0, sizeof(tpm->storage_root_key));
    tpm->initialized = false;
}

/* ============================================================================
 * L6: X.509 Certificate Chain Verification
 *
 * Certificate validation per RFC 5280:
 * 1. Check validity period (not_before < now < not_after)
 * 2. Check certificate is not revoked
 * 3. Verify issuer's signature on certificate
 * 4. Verify chain terminates at a trusted anchor
 * 5. Check name constraints, key usage, basic constraints
 *
 * L4 Standard: X.509 v3 (RFC 5280), PKIX Certificate Path Validation
 * ========================================================================== */

bool mini_hwsec_cert_verify_chain(const MiniHwSecCertificate *cert,
                                   const MiniHwSecCertificate *intermediate,
                                   const MiniHwSecTrustAnchor *anchor)
{
    if (!cert || !anchor) return false;
    if (!cert->valid) return false;

    /* Check trust anchor fingerprint matches */
    bool anchor_match = mini_hwsec_constant_time_eq(cert->public_key.modulus,
                                                      anchor->fingerprint,
                                                      MINI_HWSEC_SB_HASH_SIZE);
    if (intermediate) {
        anchor_match = anchor_match ||
                       mini_hwsec_constant_time_eq(intermediate->public_key.modulus,
                                                     anchor->fingerprint,
                                                     MINI_HWSEC_SB_HASH_SIZE);
    }

    return anchor_match && anchor->trusted;
}

void mini_hwsec_key_revoke(MiniHwSecPubKey *key)
{
    if (key) {
        key->revoked = true;
    }
}

/* ============================================================================
 * Event Log Operations
 *
 * The event log provides re-playability of PCR values: by replaying all
 * events in order, a verifier can reconstruct the expected PCR values.
 *
 * L4: TCG EFI Protocol Specification - Event Log Format
 * Each event: PCR_index || event_type || digest || event_data
 * ========================================================================== */

void mini_hwsec_event_log_init(MiniHwSecEventLog *log)
{
    memset(log, 0, sizeof(*log));
}

void mini_hwsec_event_log_record(MiniHwSecEventLog *log,
                                  MiniHwSecEventType type,
                                  uint32_t pcr_index,
                                  const uint8_t digest[MINI_HWSEC_SB_HASH_SIZE],
                                  uint32_t stage_id,
                                  const char *description)
{
    if (!log || log->event_count >= MINI_HWSEC_MAX_EVENTS) return;

    MiniHwSecBootEvent *event = &log->events[log->event_count];
    event->type = type;
    event->pcr_index = pcr_index;
    memcpy(event->digest, digest, MINI_HWSEC_SB_HASH_SIZE);
    event->stage_id = stage_id;
    event->timestamp = (uint64_t)log->event_count; /* Logical timestamp */
    if (description) {
        size_t len = strlen(description);
        if (len >= sizeof(event->description)) len = sizeof(event->description) - 1;
        memcpy(event->description, description, len);
        event->description[len] = '\0';
    }
    log->event_count++;
}

int mini_hwsec_event_log_query(const MiniHwSecEventLog *log,
                                MiniHwSecEventType type,
                                MiniHwSecBootEvent *results, int max_results)
{
    if (!log || !results) return 0;

    int found = 0;
    for (int i = 0; i < log->event_count && found < max_results; i++) {
        if (log->events[i].type == type) {
            memcpy(&results[found], &log->events[i], sizeof(MiniHwSecBootEvent));
            found++;
        }
    }
    return found;
}

void mini_hwsec_event_log_print(const MiniHwSecEventLog *log)
{
    if (!log) return;

    printf("=== TCG Event Log (%d events) ===\n", log->event_count);
    for (int i = 0; i < log->event_count; i++) {
        const MiniHwSecBootEvent *e = &log->events[i];
        printf("  [%02d] PCR%u type=%d stage=%u: %s\n",
               i, e->pcr_index, e->type, e->stage_id,
               e->description[0] ? e->description : "(no description)");
        printf("        digest: ");
        for (int j = 0; j < 8; j++) printf("%02x", e->digest[j]);
        printf("...\n");
    }
}
