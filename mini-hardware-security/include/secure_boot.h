#ifndef MINI_HWSEC_SECURE_BOOT_H
#define MINI_HWSEC_SECURE_BOOT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * L2: Core Concepts - Secure Boot & Trusted Execution Chain
 *
 * Secure boot establishes a chain of trust from an immutable hardware root
 * (typically ROM / eFuse) through each stage of the boot process. Each stage
 * verifies the cryptographic signature of the next before executing it.
 *
 * Key concepts:
 * - Root of Trust (RoT): immutable first-stage code in ROM
 * - Chain of Trust: RoT → Bootloader → Kernel → Userspace
 * - Measured Boot: each stage's hash recorded in PCR for attestation
 * - TPM: Trusted Platform Module for secure storage and attestation
 *
 * Reference:
 * - UEFI Secure Boot Specification 2.10
 * - TCG PC Client Platform Firmware Profile 1.05
 * - NIST SP 800-155: BIOS Integrity Measurement Guidelines
 * - MIT 6.858: Trusted Boot lecture
 * - Cambridge Part II: Security - Secure Boot Chains
 */

/* --- Secure Boot Constants --------------------------------------------- */
#define MINI_HWSEC_SB_MAX_STAGES       6
#define MINI_HWSEC_SB_HASH_SIZE        32
#define MINI_HWSEC_SB_SIG_SIZE         256
#define MINI_HWSEC_SB_PUBKEY_SIZE      256
#define MINI_HWSEC_SB_STAGE_NAME_MAX   32
#define MINI_HWSEC_SB_MAX_PCR_REGISTERS 24

/* --- TPM PCR (Platform Configuration Register) ------------------------- */
typedef struct {
    uint8_t  value[MINI_HWSEC_SB_HASH_SIZE];
    uint32_t index;
    bool     resettable;
    bool     locked;
} MiniHwSecPCR;

typedef enum {
    MINI_HWSEC_PCR_LOCALITY_0 = 0,
    MINI_HWSEC_PCR_LOCALITY_1 = 1,
    MINI_HWSEC_PCR_LOCALITY_2 = 2,
    MINI_HWSEC_PCR_LOCALITY_3 = 3,
    MINI_HWSEC_PCR_LOCALITY_4 = 4,
} MiniHwSecPcrLocality;

/* --- Boot Stage Types -------------------------------------------------- */
typedef enum {
    MINI_HWSEC_SB_STAGE_ROM      = 0,  /* HW ROM (immutable, implicit trust) */
    MINI_HWSEC_SB_STAGE_BL1      = 1,  /* Bootloader Stage 1 */
    MINI_HWSEC_SB_STAGE_BL2      = 2,  /* Bootloader Stage 2 */
    MINI_HWSEC_SB_STAGE_KERNEL   = 3,  /* OS Kernel */
    MINI_HWSEC_SB_STAGE_INITRD   = 4,  /* Initial RAM Disk */
    MINI_HWSEC_SB_STAGE_USER     = 5,  /* User-space init */
} MiniHwSecBootStage;

typedef enum {
    MINI_HWSEC_SB_STATE_UNKNOWN   = 0,
    MINI_HWSEC_SB_STATE_VERIFIED  = 1,
    MINI_HWSEC_SB_STATE_FAILED    = 2,
    MINI_HWSEC_SB_STATE_LAUNCHED  = 3,
    MINI_HWSEC_SB_STATE_COMPROMISED = 4,
} MiniHwSecBootState;

/* --- Certificate / Key Structures -------------------------------------- */
typedef struct {
    uint8_t  modulus[MINI_HWSEC_SB_PUBKEY_SIZE];
    uint8_t  exponent[4];
    uint32_t key_id;
    bool     revoked;
} MiniHwSecPubKey;

typedef struct {
    uint8_t  issuer[64];
    uint8_t  subject[64];
    MiniHwSecPubKey public_key;
    uint8_t  signature[MINI_HWSEC_SB_SIG_SIZE];
    uint32_t serial;
    uint64_t not_before;
    uint64_t not_after;
    bool     valid;
} MiniHwSecCertificate;

typedef struct {
    uint8_t  fingerprint[MINI_HWSEC_SB_HASH_SIZE];
    uint32_t key_id;
    bool     trusted;
} MiniHwSecTrustAnchor;

/* --- Boot Image Manifest ----------------------------------------------- */
typedef struct {
    uint8_t  stage_name[MINI_HWSEC_SB_STAGE_NAME_MAX];
    MiniHwSecBootStage stage;
    uint8_t  binary_hash[MINI_HWSEC_SB_HASH_SIZE];
    uint8_t  signature[MINI_HWSEC_SB_SIG_SIZE];
    uint32_t version;
    uint32_t image_size;
    uint64_t load_address;
    uint64_t entry_point;
    uint8_t  signer_key_id;
    bool     signature_valid;
} MiniHwSecBootImage;

typedef struct {
    MiniHwSecBootImage stages[MINI_HWSEC_SB_MAX_STAGES];
    int    stage_count;
    bool   boot_successful;
    uint32_t failed_stage;
} MiniHwSecBootChain;

/* --- TPM Simulation ---------------------------------------------------- */
typedef struct {
    MiniHwSecPCR    pcrs[MINI_HWSEC_SB_MAX_PCR_REGISTERS];
    int             pcr_count;
    MiniHwSecPubKey endorsement_key;
    MiniHwSecPubKey attestation_key;
    uint8_t         storage_root_key[MINI_HWSEC_SB_HASH_SIZE];
    bool            initialized;
    bool            in_use;
} MiniHwSecTPM;

typedef struct {
    uint8_t  pcr_select[3];          /* PCR selection bitmap (24 bits) */
    uint8_t  pcr_composite_hash[MINI_HWSEC_SB_HASH_SIZE];  /* Hash of selected PCR values */
    uint8_t  nonce[MINI_HWSEC_SB_HASH_SIZE];               /* Anti-replay nonce */
    uint8_t  signature[MINI_HWSEC_SB_SIG_SIZE];            /* Quote signature */
    bool     valid;
} MiniHwSecTPMQuote;

/* --- Secure Boot Event Log --------------------------------------------- */
typedef enum {
    MINI_HWSEC_EVENT_PCR_EXTEND = 0,
    MINI_HWSEC_EVENT_STAGE_VERIFY = 1,
    MINI_HWSEC_EVENT_STAGE_LAUNCH = 2,
    MINI_HWSEC_EVENT_KEY_REVOKE = 3,
    MINI_HWSEC_EVENT_POLICY_UPDATE = 4,
    MINI_HWSEC_EVENT_SECURITY_ALERT = 5,
} MiniHwSecEventType;

typedef struct {
    MiniHwSecEventType type;
    uint32_t pcr_index;
    uint8_t  digest[MINI_HWSEC_SB_HASH_SIZE];
    uint32_t stage_id;
    uint64_t timestamp;
    char     description[128];
} MiniHwSecBootEvent;

#define MINI_HWSEC_MAX_EVENTS 256
typedef struct {
    MiniHwSecBootEvent events[MINI_HWSEC_MAX_EVENTS];
    int event_count;
} MiniHwSecEventLog;

/* --- Secure Boot Policy ------------------------------------------------ */
typedef enum {
    MINI_HWSEC_SB_POLICY_NONE       = 0,  /* No verification */
    MINI_HWSEC_SB_POLICY_VERIFY     = 1,  /* Verify but continue on fail */
    MINI_HWSEC_SB_POLICY_ENFORCE    = 2,  /* Verify and halt on fail */
    MINI_HWSEC_SB_POLICY_MEASURE    = 3,  /* Measure only (no verification) */
} MiniHwSecBootPolicy;

typedef struct {
    MiniHwSecTrustAnchor anchors[8];
    int    anchor_count;
    MiniHwSecBootPolicy policy;
    bool   require_attestation;
    bool   allow_debug;
    bool   lockdown_on_failure;
    uint32_t min_version[MINI_HWSEC_SB_MAX_STAGES];
} MiniHwSecBootConfig;

/* --- Measured Boot API ------------------------------------------------- */

/**
 * mini_hwsec_sb_init - Initialize secure boot subsystem
 * @config: Boot configuration (trust anchors, policy)
 * @tpm:    TPM instance to use for measurements
 * @log:    Event log for recording boot events
 *
 * Sets up the trusted computing base (TCB). This must be called before
 * any other secure boot function. The TPM must be separately initialized.
 *
 * L4 Standard: TCG PC Client Platform Firmware Profile 1.05
 * - PCR 0: SRTM (Static Root of Trust Measurement) - BIOS
 */
void mini_hwsec_sb_init(MiniHwSecBootConfig *config,
                         MiniHwSecTPM *tpm,
                         MiniHwSecEventLog *log);

/**
 * mini_hwsec_sb_measure - Record a measurement (hash) into PCR
 * @pcr_index: PCR register index (0-23)
 * @data:      Data to measure
 * @len:       Data length
 * @tpm:       TPM instance
 * @log:       Event log
 *
 * PCR extend operation: PCR_new = SHA-256(PCR_old || data_hash)
 * This is a one-way operation - PCR cannot be "reset" except at TPM reset.
 * The event log provides the reconstruction path for verification.
 */
void mini_hwsec_sb_measure(uint32_t pcr_index, const uint8_t *data, size_t len,
                            MiniHwSecTPM *tpm, MiniHwSecEventLog *log);

/**
 * mini_hwsec_sb_verify_image - Verify a boot image's signature
 * @image:     Boot image to verify
 * @pub_key:   Public key for signature verification
 * @trust_anchor: Trusted CA certificate
 * Returns: true if signature is valid and certificate chain is trusted
 *
 * Implements the verification step of the boot chain.
 * 1. Hash the image content → hash
 * 2. Verify signature(hash) using pub_key
 * 3. Verify pub_key certificate chain to trust anchor
 * 4. Check version >= minimum required
 * 5. Check key not revoked
 */
bool mini_hwsec_sb_verify_image(MiniHwSecBootImage *image,
                                  const MiniHwSecPubKey *pub_key,
                                  const MiniHwSecTrustAnchor *trust_anchor);

/**
 * mini_hwsec_sb_execute_chain - Execute the full secure boot chain
 * @chain:  Boot chain with images and metadata
 * @config: Boot configuration
 * @tpm:    TPM instance
 * @log:    Event log
 * Returns: true if all stages verified and booted successfully
 *
 * L3 Engineering Structure:
 * For each stage in chain:
 *   1. Measure stage image → PCR (ensures auditable boot log)
 *   2. Verify signature against trusted keys
 *   3. If policy == ENFORCE and verification fails → halt (return false)
 *   4. If policy == VERIFY and verification fails → log warning, continue
 *   5. Launch stage (record as launched)
 *   6. Repeat for next stage
 */
bool mini_hwsec_sb_execute_chain(MiniHwSecBootChain *chain,
                                  MiniHwSecBootConfig *config,
                                  MiniHwSecTPM *tpm,
                                  MiniHwSecEventLog *log);

/**
 * mini_hwsec_sb_rollback_detect - Detect firmware rollback attempt
 * @image:   Image to check
 * @current_version: Currently installed version
 * Returns: true if image version < current (rollback detected)
 *
 * Prevents downgrade attacks where an attacker tries to install
 * older firmware with known vulnerabilities.
 */
bool mini_hwsec_sb_rollback_detect(const MiniHwSecBootImage *image,
                                    uint32_t current_version);

/* --- TPM Operations ---------------------------------------------------- */

/**
 * mini_hwsec_tpm_init - Initialize TPM simulator
 * @tpm: TPM instance to initialize
 *
 * Creates endorsement key (EK), sets up PCR banks, initializes
 * storage root key (SRK) hierarchy.
 */
void mini_hwsec_tpm_init(MiniHwSecTPM *tpm);

/**
 * mini_hwsec_tpm_extend - Extend a PCR with measurement
 * @tpm:     TPM instance
 * @pcr_idx: PCR index to extend
 * @digest:  32-byte hash to extend with
 * @locality: Command locality (0-4)
 * Returns: true if extension succeeded
 *
 * PCR[i] = SHA256(PCR[i] || digest)
 *
 * Locality controls which software layers can access which PCRs.
 * Typically: locality 0-3 = trusted OS components, locality 4 = SMM/TrustZone
 */
bool mini_hwsec_tpm_extend(MiniHwSecTPM *tpm, uint32_t pcr_idx,
                            const uint8_t digest[MINI_HWSEC_SB_HASH_SIZE],
                            MiniHwSecPcrLocality locality);

/**
 * mini_hwsec_tpm_pcr_read - Read PCR value
 * @tpm:     TPM instance
 * @pcr_idx: PCR index
 * @value:   Output buffer (32 bytes)
 * Returns: true if PCR exists
 */
bool mini_hwsec_tpm_pcr_read(MiniHwSecTPM *tpm, uint32_t pcr_idx,
                              uint8_t value[MINI_HWSEC_SB_HASH_SIZE]);

/**
 * mini_hwsec_tpm_quote - Generate a TPM quote (remote attestation)
 * @tpm:     TPM instance
 * @pcr_mask: Bitmap of PCRs to include (bit i → PCR[i])
 * @nonce:   Anti-replay nonce
 * @quote:   Output quote structure
 * Returns: true if quote generated successfully
 *
 * A TPM Quote proves to a remote verifier that the PCR values are authentic.
 * The quote contains PCR composite hash signed by the AIK (Attestation
 * Identity Key), which in turn is certified by the EK (Endorsement Key).
 *
 * This is the foundation of Remote Attestation (L7 Application).
 *
 * L4 Standard: TCG TPM v2.0 Part 1: Architecture, Section 18
 */
bool mini_hwsec_tpm_quote(MiniHwSecTPM *tpm,
                           uint32_t pcr_mask,
                           const uint8_t nonce[MINI_HWSEC_SB_HASH_SIZE],
                           MiniHwSecTPMQuote *quote);

/**
 * mini_hwsec_tpm_quote_verify - Verify a TPM quote
 * @quote:      Quote to verify
 * @expected_pcrs: Expected PCR values (array of 24 × 32 bytes)
 * @pcr_mask:   Which PCRs are included
 * @nonce:      Expected nonce
 * Returns: true if quote is authentic and PCRs match
 */
bool mini_hwsec_tpm_quote_verify(const MiniHwSecTPMQuote *quote,
                                  const uint8_t expected_pcrs[24][MINI_HWSEC_SB_HASH_SIZE],
                                  uint32_t pcr_mask,
                                  const uint8_t nonce[MINI_HWSEC_SB_HASH_SIZE]);

/**
 * mini_hwsec_tpm_seal - Seal data to a PCR state
 * @tpm:       TPM instance
 * @data:      Data to seal
 * @data_len:  Data length
 * @pcr_mask:  PCR bitmask to seal to
 * @sealed:    Output sealed blob
 * @sealed_len: Output blob length
 * Returns: true if sealed successfully
 *
 * Sealed data can only be unsealed when PCRs match the sealing state.
 * Used for: disk encryption keys (BitLocker), credential protection.
 */
bool mini_hwsec_tpm_seal(MiniHwSecTPM *tpm,
                          const uint8_t *data, size_t data_len,
                          uint32_t pcr_mask,
                          uint8_t *sealed, size_t *sealed_len);

bool mini_hwsec_tpm_unseal(MiniHwSecTPM *tpm,
                            const uint8_t *sealed, size_t sealed_len,
                            uint8_t *data, size_t *data_len);

/**
 * mini_hwsec_tpm_clear - Clear TPM (back to factory state)
 * @tpm: TPM instance
 *
 * Requires physical presence assertion. Resets all PCRs, keys, and
 * authorization values. Used before recycling hardware.
 */
void mini_hwsec_tpm_clear(MiniHwSecTPM *tpm);

/* --- Certificate Operations -------------------------------------------- */

/**
 * mini_hwsec_cert_verify_chain - Verify certificate chain to trust anchor
 * @cert:        Certificate to verify
 * @intermediate: Optional intermediate CA (NULL if direct)
 * @anchor:      Trust anchor (root CA)
 * Returns: true if chain is valid
 *
 * L4 Standard: X.509 v3 certificate path validation (RFC 5280)
 *
 * Checks:
 * 1. Certificate not expired
 * 2. Certificate not revoked
 * 3. Issuer signature valid
 * 4. Chain terminates at trusted anchor
 */
bool mini_hwsec_cert_verify_chain(const MiniHwSecCertificate *cert,
                                   const MiniHwSecCertificate *intermediate,
                                   const MiniHwSecTrustAnchor *anchor);

/**
 * mini_hwsec_key_revoke - Revoke a key by key_id
 * @key: Key to revoke
 */
void mini_hwsec_key_revoke(MiniHwSecPubKey *key);

/* --- Event Log Operations ---------------------------------------------- */
void mini_hwsec_event_log_init(MiniHwSecEventLog *log);
void mini_hwsec_event_log_record(MiniHwSecEventLog *log,
                                  MiniHwSecEventType type,
                                  uint32_t pcr_index,
                                  const uint8_t digest[MINI_HWSEC_SB_HASH_SIZE],
                                  uint32_t stage_id,
                                  const char *description);
int  mini_hwsec_event_log_query(const MiniHwSecEventLog *log,
                                 MiniHwSecEventType type,
                                 MiniHwSecBootEvent *results, int max_results);
void mini_hwsec_event_log_print(const MiniHwSecEventLog *log);

#endif /* MINI_HWSEC_SECURE_BOOT_H */
