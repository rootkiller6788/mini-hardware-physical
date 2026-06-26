#ifndef MINI_HWSEC_HROT_PUF_H
#define MINI_HWSEC_HROT_PUF_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * L1/L3: Hardware Root of Trust (HRoT) & Physically Unclonable Function (PUF)
 *
 * The Hardware Root of Trust is the foundational security anchor in a
 * computing system. It includes:
 * - Immutable boot ROM (first instruction executed)
 * - Unique Device Identity (from PUF or fuses)
 * - Secure key storage (never leaves the RoT)
 * - Cryptographic acceleration (AES, SHA, ECC)
 * - Secure counters (monotonic, anti-rollback)
 *
 * PUF (Physically Unclonable Function) exploits manufacturing variations
 * in silicon to generate device-unique fingerprints/keys:
 * - SRAM PUF: power-up state of SRAM cells (random due to threshold mismatch)
 * - Ring Oscillator PUF: frequency differences between identical oscillators
 * - Arbiter PUF: race condition in identically-laid-out paths
 * - Butterfly PUF: cross-coupled latches in FPGA
 *
 * Key properties of a good PUF:
 * 1. Uniqueness: PUF responses differ between devices (inter-HD ~50%)
 * 2. Reliability: Same device gives same response (intra-HD < 15%)
 * 3. Unpredictability: Response cannot be modeled from challenges
 * 4. Unclonability: Cannot physically reproduce the PUF
 *
 * Reference:
 * - Pappu 2002: Physical One-Way Functions (optical PUF)
 * - Guajardo 2007: FPGA Intrinsic PUFs (SRAM PUF)
 * - Suh 2007: Physical Unclonable Functions for Device Authentication
 * - NIST SP 800-193: Platform Firmware Resiliency Guidelines
 * - 清华: PUF-based hardware security (SRAM PUF characterization)
 * - MIT 6.858: Hardware security primitives
 * ========================================================================== */

/* --- PUF Types and Parameters ------------------------------------------ */
#define MINI_HWSEC_PUF_SRAM_SIZE       2048    /* SRAM PUF: 2048-bit response */
#define MINI_HWSEC_PUF_RING_OSC_COUNT  128     /* RO PUF: 128 ring oscillators */
#define MINI_HWSEC_PUF_HELPER_DATA_SIZE 512    /* Helper data for error correction */
#define MINI_HWSEC_PUF_KEY_SIZE        32

typedef enum {
    MINI_HWSEC_PUF_TYPE_SRAM         = 0,  /* SRAM startup value PUF */
    MINI_HWSEC_PUF_TYPE_RING_OSC     = 1,  /* Ring oscillator PUF */
    MINI_HWSEC_PUF_TYPE_ARBITER      = 2,  /* Arbiter PUF (delay-based) */
    MINI_HWSEC_PUF_TYPE_BUTTERFLY    = 3,  /* Butterfly PUF (FPGA) */
} MiniHwSecPufType;

/* --- PUF Structures ---------------------------------------------------- */
typedef struct {
    uint8_t  raw_response[MINI_HWSEC_PUF_SRAM_SIZE / 8];
    uint8_t  stable_mask[MINI_HWSEC_PUF_SRAM_SIZE / 8];    /* Bit stability mask */
    uint8_t  helper_data[MINI_HWSEC_PUF_HELPER_DATA_SIZE];  /* Syndrome/fuzzy extractor data */
    double   reliability;            /* Fraction of stable bits (0.0-1.0) */
    double   uniqueness;             /* Inter-device variation */
    int      unstable_bit_count;
    bool     enrolled;               /* Has enrollment been performed? */
} MiniHwSecSRAMPuf;

typedef struct {
    uint64_t frequencies[MINI_HWSEC_PUF_RING_OSC_COUNT];
    int      frequency_count;
    uint8_t  bit_string[MINI_HWSEC_PUF_RING_OSC_COUNT / 8];
    double   temperature_coeff;      /* ppm/C frequency drift */
} MiniHwSecRingOscPuf;

/* --- Hardware Root of Trust (HRoT) Structure --------------------------- */
#define MINI_HWSEC_HROT_ROM_SIZE      4096
#define MINI_HWSEC_HROT_SECURE_RAM    1024
#define MINI_HWSEC_HROT_KEY_SLOTS     8
#define MINI_HWSEC_HROT_COUNTER_COUNT 4

typedef struct {
    uint8_t  uid[32];                       /* Unique Device ID */
    uint8_t  device_key[MINI_HWSEC_PUF_KEY_SIZE];  /* PUF-derived device key */
    bool     key_provisioned;
    bool     debug_locked;
} MiniHwSecDeviceIdentity;

typedef struct {
    uint8_t  key[MINI_HWSEC_PUF_KEY_SIZE];
    int      slot;
    bool     occupied;
    bool     locked;           /* Locked keys cannot be modified */
    bool     exportable;       /* Exportable keys can be wrapped for export */
    char     label[32];
} MiniHwSecKeySlot;

typedef struct {
    uint64_t value;
    uint64_t min_value;        /* Monotonic: value >= min_value always */
    bool     locked;           /* Locked counters cannot be decremented */
    char     label[16];
} MiniHwSecSecureCounter;

typedef struct {
    uint8_t  rom[MINI_HWSEC_HROT_ROM_SIZE];          /* Immutable boot ROM */
    MiniHwSecSRAMPuf       sram_puf;                  /* Device-unique PUF */
    MiniHwSecRingOscPuf    ring_puf;                  /* For temperature sensing */
    MiniHwSecDeviceIdentity identity;                  /* Device identity block */
    MiniHwSecKeySlot        key_slots[MINI_HWSEC_HROT_KEY_SLOTS];
    MiniHwSecSecureCounter  counters[MINI_HWSEC_HROT_COUNTER_COUNT];
    uint8_t  secure_ram[MINI_HWSEC_HROT_SECURE_RAM]; /* Tamper-resistant RAM */
    bool     tamper_detected;
    bool     initialized;
    uint32_t lifecycle_state;   /* 0=manufacturing, 1=provisioned, 2=secure, 3=RMA */
} MiniHwSecHRoT;

/* --- PUF Enrollment Structures ---------------------------------------- */
typedef struct {
    uint8_t  responses[32][MINI_HWSEC_PUF_SRAM_SIZE / 8];  /* 32 enrollment measurements */
    uint8_t  helper_data[MINI_HWSEC_PUF_HELPER_DATA_SIZE];
    uint8_t  golden_key[MINI_HWSEC_PUF_KEY_SIZE];          /* Stable key derived from PUF */
    int      measurement_count;
    double   bit_stability[MINI_HWSEC_PUF_SRAM_SIZE];      /* Per-bit stability */
    bool     enrollment_complete;
} MiniHwSecPufEnrollment;

/* --- L4 Standards ----------------------------------------------------- */

/**
 * L4: Fuzzy Extractor for PUF Key Generation
 *
 * PUF responses are noisy (intra-device variation). To derive a stable key:
 *
 * 1. Enrollment:
 *    - Measure PUF response R multiple times
 *    - Generate helper data P (syndrome of ECC) and key K
 *    - Store P publicly; K is the stable key
 *
 * 2. Reconstruction:
 *    - Measure noisy response R' ≈ R
 *    - Use helper data P to correct errors: K = Decode(R', P)
 *    - If Hamming(R, R') < correction_capacity, then K' = K
 *
 * Formal property (Dodis 2004):
 *   Given helper data P = Gen(R),
 *   H_infty(R | P) ≥ m - |P| - 2 log(1/ε)
 *   where m is the min-entropy of R and ε is the security parameter.
 *
 * This module implements a BCH-based fuzzy extractor with error
 * correction capacity t = 15 bits per 256-bit block.
 */

/**
 * L4: Monotonic Counter Security
 *
 * A monotonic counter guarantees:
 *   ∀ time points t1 < t2: counter(t1) < counter(t2)
 *
 * This prevents rollback attacks (e.g., restoring old firmware with
 * known vulnerabilities). Implemented using:
 * - One-time programmable (OTP) fuses for permanence
 * - Voltage/temperature-hardened storage cells
 * - Redundant encoding with majority voting
 *
 * NIST SP 800-193 defines the Platform Firmware Resiliency (PFR) guidelines
 * requiring monotonic counters for firmware anti-rollback protection.
 */

/* --- SRAM PUF API ------------------------------------------------------ */

/**
 * mini_hwsec_sram_puf_enroll - Enroll SRAM PUF (create helper data)
 * @puf:        SRAM PUF to enroll
 * @enrollment: Enrollment data structure
 * @read_puf:   Hardware function to read SRAM startup values
 * Returns: true if enrollment successful
 *
 * Enrollment process:
 * 1. Read SRAM startup values 32 times at different temperatures/voltages
 * 2. Calculate per-bit stability across measurements
 * 3. Mark stable bits (same in >= 28/32 measurements)
 * 4. Generate helper data using BCH error correction code
 * 5. Derive golden key: K = Hash(stable_bits)
 *
 * Requires: Physical access to the device during manufacturing.
 * After enrollment, only the helper data is stored (not the key).
 */
bool mini_hwsec_sram_puf_enroll(MiniHwSecSRAMPuf *puf,
                                 MiniHwSecPufEnrollment *enrollment,
                                 void (*read_puf)(uint8_t *response, int bitlen));

/**
 * mini_hwsec_sram_puf_reconstruct - Reconstruct stable key from PUF
 * @puf:        Enrolled SRAM PUF (with helper data)
 * @key:        Output 32-byte stable key
 * @read_puf:   Hardware function to read SRAM startup values
 * Returns: true if key reconstructed successfully
 *
 * Run-time reconstruction:
 * 1. Read SRAM startup values
 * 2. Apply helper data (error correction decode)
 * 3. Hash corrected bit string → stable key
 *
 * The key is ONLY present in volatile memory and can be zeroed on tamper.
 */
bool mini_hwsec_sram_puf_reconstruct(MiniHwSecSRAMPuf *puf,
                                      uint8_t key[MINI_HWSEC_PUF_KEY_SIZE],
                                      void (*read_puf)(uint8_t *response, int bitlen));

/**
 * mini_hwsec_sram_puf_authenticate - Challenge-response authentication
 * @puf:    PUF instance
 * @challenge: Challenge data
 * @response:  Output response (PUF(challenge))
 * Returns: true if authenticated
 */
bool mini_hwsec_sram_puf_authenticate(MiniHwSecSRAMPuf *puf,
                                       const uint8_t *challenge,
                                       uint8_t *response);

/* --- Ring Oscillator PUF API ------------------------------------------- */

void mini_hwsec_ring_puf_init(MiniHwSecRingOscPuf *puf);
void mini_hwsec_ring_puf_measure(MiniHwSecRingOscPuf *puf,
                                  void (*measure_freq)(int osc_id, uint64_t *freq));
void mini_hwsec_ring_puf_derive_key(MiniHwSecRingOscPuf *puf,
                                     uint8_t key[MINI_HWSEC_PUF_KEY_SIZE]);

/* --- HRoT API ---------------------------------------------------------- */

/**
 * mini_hwsec_hrot_init - Initialize Hardware Root of Trust
 * @hrot:          HRoT to initialize
 * @device_uid:    Unique device identifier (from fuses or PUF)
 * @read_sram_puf: Hardware function to read SRAM PUF
 *
 * Initialization steps:
 * 1. Read device UID from eFuses
 * 2. Enroll/reconstruct SRAM PUF → device_key
 * 3. Initialize monotonic counters from secure storage
 * 4. Set lifecycle state to PROVISIONED (if first boot) or SECURE
 * 5. Lock debug port if in SECURE state
 *
 * L3 Engineering Structure: HRoT is the first thing that executes.
 * All subsequent security depends on HRoT integrity.
 */
void mini_hwsec_hrot_init(MiniHwSecHRoT *hrot,
                           const uint8_t device_uid[32],
                           void (*read_sram_puf)(uint8_t *response, int bitlen));

/**
 * mini_hwsec_hrot_derive_key - Derive a key from the master device key
 * @hrot:    HRoT instance
 * @label:   Key derivation label (e.g., "AES-STORAGE", "HMAC-ATTEST")
 * @context: Optional context (NULL for default)
 * @key:     Output derived key (32 bytes)
 *
 * Uses HKDF(HROT_Device_Key, label || context) to derive purpose-specific
 * keys. This ensures key separation: compromising one derived key does not
 * compromise others or the master key.
 *
 * Key derivation follows NIST SP 800-108 (KDF in Counter Mode).
 */
void mini_hwsec_hrot_derive_key(MiniHwSecHRoT *hrot,
                                 const char *label,
                                 const uint8_t *context,
                                 uint8_t key[MINI_HWSEC_PUF_KEY_SIZE]);

/**
 * mini_hwsec_hrot_provision_key - Store a key in the HRoT key slot
 * @hrot:   HRoT instance
 * @slot:   Key slot index (0-7)
 * @key:    Key material (32 bytes)
 * @label:  Human-readable label
 * Returns: true if provisioning succeeded
 */
bool mini_hwsec_hrot_provision_key(MiniHwSecHRoT *hrot, int slot,
                                    const uint8_t key[MINI_HWSEC_PUF_KEY_SIZE],
                                    const char *label);

/**
 * mini_hwsec_hrot_get_key - Retrieve key from HRoT slot (internal use only)
 * @hrot: HRoT instance
 * @slot: Key slot index
 * @key:  Output key buffer
 * Returns: true if key retrieved
 *
 * CRITICAL: In real hardware, keys never leave the HRoT. This API exists
 * only for secure operations within the HRoT boundary. External callers
 * should use the crypto operations that use keys by reference, not by value.
 */
bool mini_hwsec_hrot_get_key(MiniHwSecHRoT *hrot, int slot,
                              uint8_t key[MINI_HWSEC_PUF_KEY_SIZE]);

/* --- Secure Counter API ------------------------------------------------ */

uint64_t mini_hwsec_counter_read(const MiniHwSecHRoT *hrot, int counter_id);
bool     mini_hwsec_counter_increment(MiniHwSecHRoT *hrot, int counter_id);
bool     mini_hwsec_counter_verify(const MiniHwSecHRoT *hrot,
                                    int counter_id, uint64_t min_expected);

/**
 * mini_hwsec_counter_lock - Permanently lock a counter
 * @hrot:       HRoT instance
 * @counter_id: Counter to lock
 *
 * Locked counters cannot be modified even by the HRoT itself.
 * Used for anti-rollback protection in production devices.
 */
bool mini_hwsec_counter_lock(MiniHwSecHRoT *hrot, int counter_id);

/* --- Lifecycle State API ----------------------------------------------- */

int  mini_hwsec_hrot_get_lifecycle(const MiniHwSecHRoT *hrot);
bool mini_hwsec_hrot_advance_lifecycle(MiniHwSecHRoT *hrot, int new_state);
bool mini_hwsec_hrot_lock_debug(MiniHwSecHRoT *hrot);

/* --- BCH Error Correction for Fuzzy Extractor -------------------------- */

/**
 * mini_hwsec_bch_encode - BCH(255, 131, 18) encode for PUF helper data
 * @data:     131-bit data input (7 × 255 = 1785 bits total for 256 bytes)
 * @parity:   124-bit parity output per block
 *
 * BCH code parameters: n=255, k=131, t=18.
 * Can correct up to 18 bit errors per 255-bit block.
 * Used as the error-correcting code in the fuzzy extractor.
 */
void mini_hwsec_bch_encode(const uint8_t *data, int data_bits,
                            uint8_t *parity, int *parity_bits);

/**
 * mini_hwsec_bch_decode - BCH decode with error correction
 * @data:     Received data with parity appended
 * @n_bits:   Total bits (data + parity)
 * @corrected: Output corrected data
 * Returns: Number of errors corrected, or -1 if uncorrectable
 */
int mini_hwsec_bch_decode(const uint8_t *data, int n_bits,
                           uint8_t *corrected);

/**
 * mini_hwsec_fuzzy_extract - Full fuzzy extractor pipeline
 * @noisy:     Noisy PUF response
 * @helper:    Helper data (from enrollment)
 * @key:       Output stable key
 * Returns: true if extraction successful
 *
 * L5 Algorithm: Fuzzy Extractor using BCH ECC
 *
 * Enrollment (done once):
 *   1. Read PUF → R (256 bytes)
 *   2. Choose random K (32 bytes)
 *   3. Compute helper P = R XOR C where C = BCH_Encode(K)
 *   4. Store P (public), discard K and R
 *
 * Reconstruction (every boot):
 *   1. Read PUF → R' (noisy, R' ≈ R)
 *   2. Compute C' = R' XOR P
 *   3. K = BCH_Decode(C')
 *   4. If R' is close enough to R, K' = K
 */
bool mini_hwsec_fuzzy_extract(const uint8_t *noisy, const uint8_t *helper,
                               uint8_t key[MINI_HWSEC_PUF_KEY_SIZE]);

#endif /* MINI_HWSEC_HROT_PUF_H */
