#ifndef MINI_HWSEC_TEE_ENCLAVE_H
#define MINI_HWSEC_TEE_ENCLAVE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * L3: Engineering Structure - Trusted Execution Environment (TEE) Model
 *
 * A TEE provides an isolated execution environment (enclave) where code
 * and data are protected from the host OS, hypervisor, and other enclaves.
 * Even privileged software (kernel/VMM) cannot inspect or modify enclave
 * memory when the enclave is active.
 *
 * Key TEE implementations:
 * - Intel SGX (Software Guard Extensions) - enclave-based TEE
 * - AMD SEV (Secure Encrypted Virtualization) - VM-based TEE
 * - ARM TrustZone - two-world (secure/non-secure) split
 * - RISC-V PMP + IOPMP - physical memory protection based
 *
 * This module models a generic TEE architecture that abstracts the
 * common primitives: enclave lifecycle, secure memory, sealing,
 * attestation, and secure scheduling.
 *
 * Reference:
 * - MIT 6.858: Trusted Execution lecture / SGX
 * - Stanford CS255: Secure Hardware lecture
 * - CMU 18-732: Enclave programming model
 * - Cambridge Part II: Security - Trusted Execution Environments
 * - Intel SGX Developer Reference (329298-002US)
 * ========================================================================== */

/* --- Enclave Structures ------------------------------------------------- */
#define MINI_HWSEC_TEE_MAX_ENCLAVES    16
#define MINI_HWSEC_TEE_ENCLAVE_SIZE    (64 * 1024)       /* 64 KB (model, not 4MB real) */
#define MINI_HWSEC_TEE_STACK_SIZE      (4 * 1024)        /* 4 KB (model) */
#define MINI_HWSEC_TEE_HEAP_SIZE       (8 * 1024)        /* 8 KB (model) */
#define MINI_HWSEC_TEE_EPC_MIN         (4 * 1024)        /* Min 4KB EPC page */
#define MINI_HWSEC_TEE_MEASUREMENT_SIZE 32
#define MINI_HWSEC_TEE_SEAL_KEY_SIZE   32
#define MINI_HWSEC_TEE_REPORT_SIZE     256

typedef enum {
    MINI_HWSEC_TEE_STATE_UNINITIALIZED = 0,
    MINI_HWSEC_TEE_STATE_CREATED       = 1,
    MINI_HWSEC_TEE_STATE_INITIALIZED   = 2,
    MINI_HWSEC_TEE_STATE_RUNNING       = 3,
    MINI_HWSEC_TEE_STATE_STOPPED       = 4,
    MINI_HWSEC_TEE_STATE_DESTROYED     = 5,
} MiniHwSecTEEState;

typedef struct {
    uint8_t  mr_enclave[MINI_HWSEC_TEE_MEASUREMENT_SIZE];  /* Enclave measurement (identity) */
    uint8_t  mr_signer[MINI_HWSEC_TEE_MEASUREMENT_SIZE];   /* Signer measurement */
    uint32_t enclave_id;
    uint16_t product_id;
    uint16_t security_version;    /* ISVSVN - monotonic counter */
    uint32_t attributes;          /* Debug, KSS, etc. */
    uint64_t base_address;
    uint64_t size;
    bool     debug_allowed;
} MiniHwSecTEEIdentity;

typedef struct {
    uint8_t  code[MINI_HWSEC_TEE_ENCLAVE_SIZE];
    uint8_t  stack[MINI_HWSEC_TEE_STACK_SIZE];
    uint8_t  heap[MINI_HWSEC_TEE_HEAP_SIZE];
    uint64_t base_addr;
    uint64_t stack_top;
    uint64_t heap_base;
    bool     access_controlled;     /* Memory access control active */
    bool     encrypted;             /* Memory encryption active */
} MiniHwSecTEEMemory;

typedef struct {
    MiniHwSecTEEIdentity identity;
    MiniHwSecTEEState    state;
    MiniHwSecTEEMemory   memory;
    uint8_t              seal_key[MINI_HWSEC_TEE_SEAL_KEY_SIZE];
    uint64_t             tsc_start;          /* Time Stamp Counter at creation */
    uint64_t             tsc_last_entry;
    uint64_t             tsc_last_exit;
    uint32_t             thread_id;
    bool                 is_secure;
} MiniHwSecTEEEnclave;

/* --- TEE Manager ------------------------------------------------------- */
typedef struct {
    MiniHwSecTEEEnclave enclaves[MINI_HWSEC_TEE_MAX_ENCLAVES];
    int     enclave_count;
    uint8_t platform_master_key[MINI_HWSEC_TEE_SEAL_KEY_SIZE];
    uint64_t base_epc;          /* Base of Enclave Page Cache in physical memory */
    uint64_t epc_size;
    bool    hardware_tee;       /* True if hardware TEE (SGX/SEV), false if S-mode TEE */
} MiniHwSecTEEManager;

/* --- Attestation Structures -------------------------------------------- */
typedef enum {
    MINI_HWSEC_ATTEST_LOCAL  = 0,   /* Attestation between enclaves on same platform */
    MINI_HWSEC_ATTEST_REMOTE = 1,   /* Remote attestation with third-party verifier */
} MiniHwSecAttestType;

typedef struct {
    MiniHwSecAttestType attest_type;
    uint8_t  report_data[64];          /* User-provided data (e.g., ECDH public key) */
    uint8_t  mr_enclave[MINI_HWSEC_TEE_MEASUREMENT_SIZE];
    uint8_t  mr_signer[MINI_HWSEC_TEE_MEASUREMENT_SIZE];
    uint16_t product_id;
    uint16_t security_version;
    uint32_t attributes;
    uint8_t  report_mac[MINI_HWSEC_TEE_SEAL_KEY_SIZE];  /* MAC over report */
} MiniHwSecTEEAttestReport;

/* --- Sealed Data ------------------------------------------------------- */
typedef struct {
    uint8_t  encrypted_data[1024];
    uint8_t  mac[MINI_HWSEC_TEE_SEAL_KEY_SIZE];
    uint8_t  key_policy[16];          /* MRENCLAVE or MRSIGNER policy */
    uint64_t data_size;
    bool     valid;
} MiniHwSecTEESealedData;

/* --- Secure Scheduler ------------------------------------------------ */
typedef struct {
    int      current_enclave;
    int      next_enclave;
    uint64_t quantum_cycles;          /* Time slice for enclave execution */
    uint64_t total_cycles;
} MiniHwSecTEEScheduler;

/* --- TEE Manager API --------------------------------------------------- */

/**
 * mini_hwsec_tee_init - Initialize TEE platform
 * @tee:      TEE manager to initialize
 * @master_key: Platform master key (from fuses/PUF)
 * @base_epc: Base physical address for Enclave Page Cache
 * @epc_size: Total EPC size
 * @has_hw:   Whether hardware TEE features are available
 *
 * Sets up the trusted execution platform. The master key is the
 * hardware-rooted key used to derive enclave-specific seal keys.
 */
void mini_hwsec_tee_init(MiniHwSecTEEManager *tee,
                          const uint8_t master_key[MINI_HWSEC_TEE_SEAL_KEY_SIZE],
                          uint64_t base_epc, uint64_t epc_size, bool has_hw);

/**
 * mini_hwsec_tee_create_enclave - Create a new enclave
 * @tee:         TEE manager
 * @code:        Enclave code
 * @code_size:   Code size
 * @signer_hash: Hash of the signing key
 * @product_id:  Product identifier
 * @attributes:  Enclave attributes
 * Returns: Enclave ID (>=0) or -1 on failure
 *
 * L3: Enclave Lifecycle - Create step.
 * 1. Allocate EPC page
 * 2. Load enclave code into secure memory
 * 3. Measure (hash) the enclave → mr_enclave
 * 4. Record signer identity → mr_signer
 * 5. Derive seal key from platform master key + mr_enclave
 * 6. Set initial state → CREATED
 */
int mini_hwsec_tee_create_enclave(MiniHwSecTEEManager *tee,
                                   const uint8_t *code, size_t code_size,
                                   const uint8_t signer_hash[MINI_HWSEC_TEE_MEASUREMENT_SIZE],
                                   uint16_t product_id, uint32_t attributes);

/**
 * mini_hwsec_tee_init_enclave - Initialize enclave (EINIT equivalent)
 * @tee:        TEE manager
 * @enclave_id: Enclave to initialize
 * Returns: true if initialization succeeded
 *
 * Finalizes enclave setup. After EINIT, the enclave cannot be modified.
 * Sets state to INITIALIZED. The enclave can now be entered (EENTER).
 */
bool mini_hwsec_tee_init_enclave(MiniHwSecTEEManager *tee, int enclave_id);

/**
 * mini_hwsec_tee_enter - Enter an enclave (EENTER)
 * @tee:        TEE manager
 * @enclave_id: Enclave to enter
 * @data:       Input data for enclave
 * @data_len:   Input data length
 * @output:     Output buffer for result
 * @output_len: Output length
 * Returns: true if entry and exit successful
 *
 * Switches processor to enclave mode:
 * 1. Save host context
 * 2. Enable memory access controls (EPC accessible only by this enclave)
 * 3. Jump to enclave entry point
 * 4. Enclave executes with full isolation
 * 5. On EEXIT, restore host context and clear registers (L1D/LFB flush)
 */
bool mini_hwsec_tee_enter(MiniHwSecTEEManager *tee, int enclave_id,
                           const uint8_t *data, size_t data_len,
                           uint8_t *output, size_t *output_len);

/**
 * mini_hwsec_tee_destroy - Destroy an enclave
 * @tee:        TEE manager
 * @enclave_id: Enclave to destroy
 *
 * Securely deallocates EPC and zeros all memory.
 */
bool mini_hwsec_tee_destroy(MiniHwSecTEEManager *tee, int enclave_id);

/* --- Attestation API --------------------------------------------------- */

/**
 * mini_hwsec_tee_attest_local - Create local attestation report
 * @tee:          TEE manager
 * @enclave_id:   Source enclave
 * @target_id:    Target enclave (on same platform)
 * @report:       Output attestation report
 * Returns: true if report created
 *
 * Local attestation proves to enclave B that enclave A is running
 * the expected code on the same platform. The report is MAC'd with
 * a key only known to the platform.
 */
bool mini_hwsec_tee_attest_local(MiniHwSecTEEManager *tee,
                                  int enclave_id, int target_id,
                                  MiniHwSecTEEAttestReport *report);

/**
 * mini_hwsec_tee_attest_remote - Create remote attestation quote
 * @tee:        TEE manager
 * @enclave_id: Enclave to quote
 * @challenge:  Verifier's challenge (anti-replay)
 * @report:     Output report
 * Returns: true if quote created
 *
 * Remote attestation uses EPID/DCAP to prove to a remote party that
 * the enclave is genuine Intel SGX hardware running specific code.
 * The quote is signed by the platform's attestation key.
 *
 * L7 Application: Used for confidential computing - verifying that
 * cloud VMs are truly running in AMD SEV/TDX protected memory.
 */
bool mini_hwsec_tee_attest_remote(MiniHwSecTEEManager *tee,
                                   int enclave_id,
                                   const uint8_t challenge[32],
                                   MiniHwSecTEEAttestReport *report);

bool mini_hwsec_tee_verify_report(MiniHwSecTEEManager *tee,
                                   const MiniHwSecTEEAttestReport *report);

/* --- Sealing API ------------------------------------------------------- */

/**
 * mini_hwsec_tee_seal - Seal data to an enclave identity
 * @tee:       TEE manager
 * @enclave_id: Enclave requesting seal
 * @data:      Data to seal
 * @data_len:  Data length
 * @sealed:    Output sealed blob
 * @policy:    POLICY_MRENCLAVE (only same enclave) or POLICY_MRSIGNER (same signer)
 *
 * Sealing encrypts data such that it can only be decrypted by the
 * specified enclave (or signer, depending on policy). The seal key
 * is derived from platform_fuse + mr_enclave (or mr_signer).
 *
 * Used for: storing persistent secrets across enclave restarts,
 * secure local storage for crypto wallets, credential caches.
 */
void mini_hwsec_tee_seal(MiniHwSecTEEManager *tee, int enclave_id,
                          const uint8_t *data, size_t data_len,
                          MiniHwSecTEESealedData *sealed,
                          bool policy_mrsigner);

bool mini_hwsec_tee_unseal(MiniHwSecTEEManager *tee, int enclave_id,
                            const MiniHwSecTEESealedData *sealed,
                            uint8_t *data, size_t *data_len);

/* --- Secure Scheduler API ---------------------------------------------- */

void mini_hwsec_tee_scheduler_init(MiniHwSecTEEScheduler *sched);
int  mini_hwsec_tee_scheduler_schedule(MiniHwSecTEEScheduler *sched,
                                        MiniHwSecTEEManager *tee);
void mini_hwsec_tee_scheduler_yield(MiniHwSecTEEScheduler *sched);

/* --- Enclave Queries --------------------------------------------------- */
bool mini_hwsec_tee_is_valid_enclave(const MiniHwSecTEEManager *tee, int id);
int  mini_hwsec_tee_get_running_enclave(const MiniHwSecTEEManager *tee);
int  mini_hwsec_tee_get_free_enclaves(const MiniHwSecTEEManager *tee);

#endif /* MINI_HWSEC_TEE_ENCLAVE_H */
