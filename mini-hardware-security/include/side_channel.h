#ifndef MINI_HWSEC_SIDE_CHANNEL_H
#define MINI_HWSEC_SIDE_CHANNEL_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * L2/L5: Core Concepts - Side-Channel Attack Defenses for Hardware Security
 *
 * Side-channel attacks exploit physical leakages (timing, power consumption,
 * EM emissions, acoustic) to extract secrets. Hardware security modules must
 * implement defenses against these attacks.
 *
 * Key attack classes:
 * - Timing attacks: exploit variable-time operations
 * - SPA (Simple Power Analysis): observe single trace to guess operations
 * - DPA (Differential Power Analysis): statistical analysis over many traces
 * - Cache-timing: Prime+Probe, Flush+Reload attacks on shared caches
 * - EM leakage: electromagnetic emanations correlated with computations
 * - Fault injection: glitching voltage/clock to cause computation errors
 *
 * Defense strategies:
 * - Constant-time programming (eliminate timing variation)
 * - Masking (randomize intermediate values)
 * - Blinding (randomize inputs to operations)
 * - Balanced circuits (dual-rail logic, constant power draw)
 * - Shielded enclosures (EM containment)
 * - Sensors (voltage, temperature, light monitoring)
 *
 * Reference:
 * - Kocher 1996: Timing Attacks on Implementations of Diffie-Hellman, RSA, DSS
 * - Kocher 1999: Differential Power Analysis
 * - Mangard 2007: Power Analysis Attacks (textbook)
 * - MIT 6.858: Side-channel attacks lecture
 * - CMU 18-732: Constant-time programming discipline
 * - Cambridge Part II: Side-channel analysis in embedded systems
 */

/* --- L4 Standards ------------------------------------------------------ */

/**
 * L4: The Lebesgue differentiation of a side-channel trace
 *
 * For a power trace P(t) over operation O with key k, the DPA attack
 * computes the correlation:
 *
 *   ρ(k_guess) = Corr(H(W, k_guess), P(t))
 *
 * where H is a leakage model (e.g., Hamming weight) and W is known data.
 * The correct key hypothesis maximizes ρ.
 *
 * The defense goal: ensure var(P(t) | k) = 0, i.e., power consumption
 * is statistically independent of the secret key.
 */

/**
 * L4: Constant-Time Execution Formal Model
 *
 * An algorithm A is constant-time with respect to secret s iff:
 *   ∀ inputs x, x', the execution trace T(A, x, s) and T(A, x', s)
 *   have the same sequence of:
 *   1. Instructions executed (same branches taken)
 *   2. Memory addresses accessed (no secret-dependent indexing)
 *   3. Execution time (same instruction count)
 *
 * Consequence: var(execution_time | s) = 0
 */

/* --- Side-Channel Attack Detection Structures -------------------------- */

#define MINI_HWSEC_SC_THRESHOLD_WINDOW 1000
#define MINI_HWSEC_SC_MAX_MONITORED_BLOCKS 64

typedef enum {
    MINI_HWSEC_SC_TIMING       = 0,  /* Timing side-channel */
    MINI_HWSEC_SC_POWER_SPA    = 1,  /* Simple Power Analysis */
    MINI_HWSEC_SC_POWER_DPA    = 2,  /* Differential Power Analysis */
    MINI_HWSEC_SC_EM_LEAKAGE   = 3,  /* EM emission analysis */
    MINI_HWSEC_SC_CACHE_TIMING = 4,  /* Cache-timing attack */
    MINI_HWSEC_SC_FAULT_GLITCH = 5,  /* Fault injection via glitching */
    MINI_HWSEC_SC_TEMP_SENSOR  = 6,  /* Thermal tampering */
    MINI_HWSEC_SC_ACOUSTIC     = 7,  /* Acoustic cryptanalysis */
    MINI_HWSEC_SC_COUNT        = 8,
} MiniHwSecSCType;

typedef struct {
    uint64_t execution_cycles;
    uint64_t cache_misses;
    uint64_t branch_mispredictions;
    uint64_t power_trace[MINI_HWSEC_SC_THRESHOLD_WINDOW];
    int      power_trace_idx;
    double   correlation_score;     /* DPA correlation metric */
    bool     alert_active;
} MiniHwSecSCMonitor;

typedef struct {
    MiniHwSecSCMonitor monitors[MINI_HWSEC_SC_MAX_MONITORED_BLOCKS];
    int       block_count;
    bool      tamper_detected;
    uint32_t  tamper_flags;         /* Bitmap of attack types detected */
    double    threshold_dpa;        /* DPA correlation threshold */
    uint64_t  threshold_timing_deviation; /* Max allowed timing deviation in cycles */
} MiniHwSecSCDetector;

typedef struct {
    uint32_t addr;
    uint32_t cached_data_lines;
    uint64_t access_count;
    uint64_t miss_count;
    double   anomaly_score;    /* Statistical anomaly detection */
} MiniHwSecCacheMonitor;

/* --- Masking & Blinding Structures ------------------------------------- */

/**
 * Boolean masking: represent secret x as (x XOR m) and m, where m is random.
 * All operations performed on shares independently, then un-masked.
 * This ensures that intermediate values have uniform distribution.
 */
typedef struct {
    uint8_t shares[8];   /* Up to 8 shares for high-order masking */
    int     share_count; /* Number of shares (≥ 2) */
} MiniHwSecMaskedByte;

typedef struct {
    uint8_t masked[16];  /* 16 bytes, each masked independently */
    uint8_t masks[16];   /* Corresponding masks */
} MiniHwSecMaskedAESState;

/* --- Sensor Structures ------------------------------------------------- */

typedef struct {
    double temperature_celsius;
    double voltage_rail_v;
    double clock_freq_hz;
    double light_lux;
    bool   enclosure_open;
    bool   mesh_breached;
} MiniHwSecSensorReadings;

/* --- Constant-Time Operations API -------------------------------------- */

/**
 * mini_hwsec_ct_memcmp - Constant-time memory comparison
 * @a, @b: Buffers to compare
 * @len:   Comparison length
 * Returns: 0 if equal, non-zero if different
 *
 * L5 Algorithm: XOR accumulation with constant-time memory access pattern.
 * Every byte is loaded exactly once, regardless of comparison result.
 * No early exit on mismatch.
 *
 * Time complexity: Θ(len)
 * Space complexity: Θ(1)
 */
int mini_hwsec_ct_memcmp(const uint8_t *a, const uint8_t *b, size_t len);

/**
 * mini_hwsec_ct_memcpy - Constant-time memory copy with masking
 * @dest: Destination
 * @src:  Source containing secret
 * @n:    Bytes to copy
 *
 * Ensures no timing variation based on source content.
 */
void mini_hwsec_ct_memcpy(uint8_t *dest, const uint8_t *src, size_t n);

/**
 * mini_hwsec_ct_select - Constant-time conditional select
 * @a, @b: Input values
 * @condition: 0 selects a, non-zero selects b
 * Returns: Selected value
 *
 * Uses bitwise operations (not branching) to avoid timing leaks.
 * val = a ^ ((a ^ b) & -condition)
 * This is the basis for constant-time conditional swaps and moves.
 *
 * L4 Theorem: For any a, b, the execution of ct_select is
 * data-independent (branchless, fixed instruction sequence).
 */
uint32_t mini_hwsec_ct_select(uint32_t a, uint32_t b, uint32_t condition);

/**
 * mini_hwsec_ct_swap - Constant-time conditional swap
 * @a, @b: Values to potentially swap
 * @swap: If non-zero, swap the values
 *
 * Swaps a and b without branching. Used in constant-time sorting
 * and key-dependent permutations.
 */
void mini_hwsec_ct_swap(uint32_t *a, uint32_t *b, uint32_t swap);

/**
 * mini_hwsec_ct_is_zero - Constant-time zero check
 * @x: Value to check
 * Returns: 1 if x == 0, 0 otherwise (in constant time)
 *
 * Uses bitwise reduction: (-x | x) >> (bits-1) & 1 for signed,
 * or a series of OR operations for unsigned.
 */
uint32_t mini_hwsec_ct_is_zero(uint32_t x);

/* --- Masking API ------------------------------------------------------- */

/**
 * mini_hwsec_mask_init - Initialize masking for a byte
 * @mb:      Masked byte structure
 * @value:   Secret value to protect
 * @shares:  Number of shares (2-8)
 * @rng:     Random byte generator function
 *
 * Creates shares such that: value = XOR(shares[0], ..., shares[n-1])
 * Each share individually provides no information about value.
 *
 * L5 Algorithm: Boolean masking with n shares.
 * Statistical security: (n-1)-th order DPA resistant.
 *
 * Reference: Ishai-Sahai-Wagner 2003: Private Circuits
 */
void mini_hwsec_mask_init(MiniHwSecMaskedByte *mb, uint8_t value,
                           int shares, uint8_t (*rng)(void));

/**
 * mini_hwsec_mask_unmask - Reveal masked value
 * @mb: Masked byte
 * Returns: Original secret value
 *
 * Recombines shares: value = XOR(shares[0], ..., shares[n-1])
 */
uint8_t mini_hwsec_mask_unmask(const MiniHwSecMaskedByte *mb);

/**
 * mini_hwsec_mask_refresh - Refresh mask shares (re-randomization)
 * @mb: Masked byte to refresh
 * @rng: RNG function
 *
 * Adds fresh randomness to shares without changing the underlying value.
 * Prevents side-channel leakage accumulation across operations.
 *
 * Algorithm: For each pair (i,j), add r to share[i], subtract r from share[j].
 * Since shares are XOR-based: new_share[i] = old_share[i] XOR r,
 * new_share[j] = old_share[j] XOR r. Other shares unchanged.
 * The XOR of all shares remains = original value.
 */
void mini_hwsec_mask_refresh(MiniHwSecMaskedByte *mb, uint8_t (*rng)(void));

/**
 * mini_hwsec_mask_xor - XOR two masked values (share-wise)
 * @a, @b: Masked inputs
 * @result: Masked output = a XOR b
 *
 * Computes XOR on shares independently: result.share[i] = a.share[i] XOR b.share[i]
 * This is linear and does not require unmasking.
 */
void mini_hwsec_mask_xor(const MiniHwSecMaskedByte *a,
                          const MiniHwSecMaskedByte *b,
                          MiniHwSecMaskedByte *result);

/**
 * mini_hwsec_mask_and - AND two masked values (requires refresh)
 * @a, @b: Masked inputs
 * @result: Masked output = a AND b
 * @rng: RNG for refresh
 *
 * For the non-linear AND operation, we compute:
 * For each pair i≠j (i<j): result gets a[i]&b[j] XOR randomness
 * Then result[i] gets a[i]&b[i].
 * Requires O(n^2) operations and O(n^2) random bits.
 *
 * This implements the ISW (Ishai-Sahai-Wagner) multiplication scheme.
 */
void mini_hwsec_mask_and(const MiniHwSecMaskedByte *a,
                          const MiniHwSecMaskedByte *b,
                          MiniHwSecMaskedByte *result,
                          uint8_t (*rng)(void));

/**
 * mini_hwsec_mask_aes_sbox - Masked AES S-Box lookup
 * @state_out: Output masked state
 * @state_in:  Input masked state
 *
 * Performs SubBytes transformation with masking protection.
 * Input: (x XOR m_in)  → S-Box → (S(x) XOR m_out)
 *
 * Uses randomized S-Box recomputation technique:
 * For each S-Box entry value s, store masked s' = s XOR m_out XOR (S(addr XOR m_in)).
 * This ensures no unmasked intermediate appears.
 */
void mini_hwsec_mask_aes_sbox(MiniHwSecMaskedAESState *state_out,
                               const MiniHwSecMaskedAESState *state_in);

/* --- Side-Channel Detection API ---------------------------------------- */

/**
 * mini_hwsec_sc_detector_init - Initialize side-channel attack detector
 * @detector: Detector to initialize
 * @threshold_dpa: DPA correlation threshold for alert
 *
 * Sets up monitoring infrastructure. In real hardware, this would connect
 * to on-die sensors (ring oscillators for voltage, temperature diodes,
 * EM probes, etc.).
 */
void mini_hwsec_sc_detector_init(MiniHwSecSCDetector *detector,
                                  double threshold_dpa);

/**
 * mini_hwsec_sc_monitor_operation - Monitor a cryptographic operation
 * @detector:  Detector instance
 * @block_id:  Monitored block identifier
 * @cycles:    Execution cycles for this operation
 * @power_sample: Instantaneous power measurement
 *
 * Updates the DPA correlation tracker for the monitored block.
 * Accumulates power traces and execution timing statistics.
 */
void mini_hwsec_sc_monitor_operation(MiniHwSecSCDetector *detector,
                                      int block_id, uint64_t cycles,
                                      double power_sample);

/**
 * mini_hwsec_sc_detect_attack - Run attack detection algorithm
 * @detector: Detector to analyze
 * Returns: Bitmap of detected attack types (0 = no attack)
 *
 * L5 Algorithm: Multi-dimensional anomaly detection.
 * 1. Timing: compare cycle variance against threshold
 * 2. DPA: compute correlation coefficient between power traces and Hamming weight
 * 3. Cache: monitor access pattern anomalies
 *
 * Tamper flags are set on detection. The system should respond by
 * zeroing secrets and raising a security interrupt.
 */
uint32_t mini_hwsec_sc_detect_attack(MiniHwSecSCDetector *detector);

/**
 * mini_hwsec_sc_respond - Execute tamper response
 * @detector: Detector with active alerts
 *
 * Standard tamper response:
 * 1. Zero-fill all secret key material
 * 2. Set tamper flag in secure storage
 * 3. Signal security interrupt to system monitor
 * 4. Optionally trigger physical self-destruct
 */
void mini_hwsec_sc_respond(MiniHwSecSCDetector *detector);

/* --- Cache Timing Defense ---------------------------------------------- */

/**
 * mini_hwsec_cache_partition - Partition cache to prevent side leaks
 * @monitor: Cache monitor structure
 * @secure_lines: Number of cache lines to reserve for secure operations
 *
 * Creates a dedicated cache partition that cannot be evicted by
 * untrusted code. Implements cache coloring or way-based partitioning.
 */
void mini_hwsec_cache_partition(MiniHwSecCacheMonitor *monitor,
                                 uint32_t secure_lines);

/**
 * mini_hwsec_cache_monitor_access - Monitor cache access pattern
 * @monitor: Cache monitor
 * @addr:    Memory address accessed
 *
 * Updates access statistics. Detects suspicious patterns (repeated
 * access to same set, unusual miss patterns) that may indicate
 * Prime+Probe or Flush+Reload attacks.
 */
void mini_hwsec_cache_monitor_access(MiniHwSecCacheMonitor *monitor,
                                      uint32_t addr);

/**
 * mini_hwsec_cache_detect_attack - Detect cache-timing attack
 * @monitor: Cache monitor
 * Returns: true if attack pattern detected
 */
bool mini_hwsec_cache_detect_attack(const MiniHwSecCacheMonitor *monitor);

/* --- Sensor API -------------------------------------------------------- */

void     mini_hwsec_sensor_read(MiniHwSecSensorReadings *readings);
bool     mini_hwsec_sensor_alert(const MiniHwSecSensorReadings *readings);
void     mini_hwsec_sensor_log(const MiniHwSecSensorReadings *readings,
                                char *log_buf, size_t buf_size);

/* --- Blinding API ------------------------------------------------------ */

/**
 * mini_hwsec_blind_bignum - Blinding for modular exponentiation
 * @base:    Base value
 * @modulus: Modulus
 * @blind_factor: Output blinding factor
 * @blinded: Output blinded base
 *
 * RSA blinding: b' = b * r^e mod n where r is random.
 * After decryption: m' = (b')^d = b^d * r mod n.
 * Then unblind: m = m' * r^(-1) mod n.
 *
 * This prevents the attacker from choosing ciphertext values and
 * observing their decryption timing (Kocher timing attack defense).
 */
void mini_hwsec_blind_bignum(const uint8_t *base, const uint8_t *modulus,
                              size_t len,
                              uint8_t *blind_factor, uint8_t *blinded);

/**
 * mini_hwsec_unblind_bignum - Remove RSA blinding
 */
void mini_hwsec_unblind_bignum(const uint8_t *blinded_result,
                                const uint8_t *blind_factor,
                                const uint8_t *modulus, size_t len,
                                uint8_t *result);

#endif /* MINI_HWSEC_SIDE_CHANNEL_H */
