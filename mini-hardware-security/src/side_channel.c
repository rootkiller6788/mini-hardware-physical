#include "side_channel.h"
#include "hw_crypto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* ============================================================================
 * L5: Constant-Time Operations for Side-Channel Defense
 *
 * All functions in this section execute with identical instruction sequence
 * regardless of input values. This eliminates timing side-channels.
 *
 * L4 Formal Definition:
 *   A function f is constant-time with respect to secret input s iff:
 *     ∀ public inputs x, x': trace(f, x, s) = trace(f, x', s) (modulo address values)
 *
 * Where trace() captures: branch decisions, memory access pattern,
 * instruction count, and execution time.
 *
 * Reference:
 * - Kocher 1996: "Timing Attacks on Implementations of Diffie-Hellman, RSA, DSS"
 * - Bernstein 2005: "Cache-timing attacks on AES"
 * - Almeida 2016: "Verifying Constant-Time Implementations"
 * ========================================================================== */

int mini_hwsec_ct_memcmp(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }
    /* Convert to 0/-1 without branching */
    return ((int)(diff | (uint8_t)(-(int8_t)diff))) >> 7;
}

void mini_hwsec_ct_memcpy(uint8_t *dest, const uint8_t *src, size_t n)
{
    /* Standard memcpy but documented as handling secrets */
    for (size_t i = 0; i < n; i++) {
        dest[i] = src[i];
    }
}

uint32_t mini_hwsec_ct_select(uint32_t a, uint32_t b, uint32_t condition)
{
    /* Branchless select: if condition != 0 return b, else return a
     * Uses: mask = -(condition != 0) → 0xFFFFFFFF or 0x00000000
     *       result = a ^ ((a ^ b) & mask)
     */
    uint32_t mask = (uint32_t)(-(int32_t)(condition != 0));
    return a ^ ((a ^ b) & mask);
}

void mini_hwsec_ct_swap(uint32_t *a, uint32_t *b, uint32_t swap)
{
    uint32_t mask = (uint32_t)(-(int32_t)(swap != 0));
    uint32_t xor_val = (*a ^ *b) & mask;
    *a ^= xor_val;
    *b ^= xor_val;
}

uint32_t mini_hwsec_ct_is_zero(uint32_t x)
{
    /* Branchless zero check: returns 1 if x == 0, else 0
     * Uses: (x | -x) >> 31  for signed 32-bit
     * For x != 0: (x | -x) has top bit set → >> 31 = 1
     * For x == 0: (0 | 0) = 0 → >> 31 = 0
     * Complement: we want 1 for zero
     */
    uint32_t result = (x | (uint32_t)(-(int32_t)x)) >> 31;
    return result ^ 1;
}

/* ============================================================================
 * L5/L8: Boolean Masking for Side-Channel Resistance
 *
 * Masking splits a secret value into multiple shares such that the XOR
 * of all shares equals the secret. Operations are performed on shares
 * independently, ensuring no single intermediate value reveals the secret.
 *
 * Order-2 masking (2 shares):
 *   x = s0 XOR s1  where s0 is random, s1 = x XOR s0
 *
 * For any operation f on x = f_op(a, b):
 *   Linear operations (XOR): compute share-wise → O(n) complexity
 *   Non-linear operations (AND): need cross-product → O(n^2) complexity + refresh
 *
 * ISW Scheme (Ishai-Sahai-Wagner 2003):
 *   For AND(c, a, b):
 *     for i = 0..n-1:
 *       for j = i+1..n-1:
 *         r = random()
 *         tmp = r XOR (a[i] AND b[j])
 *         c[i] = c[i] XOR (a[i] AND b[j]) XOR r  [wait, ISW uses different formula]
 *
 * Ref: Ishai, Sahai, Wagner "Private Circuits: Securing Hardware against
 * Probing Attacks" (CRYPTO 2003)
 * ========================================================================== */

/* Simple deterministic "random" function for testing masking */
static uint8_t mini_hwsec_mask_test_rng(void) {
    static uint8_t state = 0xAB;
    state = (uint8_t)(state * 0x1D + 0x27);
    return state;
}

void mini_hwsec_mask_init(MiniHwSecMaskedByte *mb, uint8_t value,
                           int shares, uint8_t (*rng)(void))
{
    if (!mb || shares < 2 || shares > 8) return;
    if (!rng) rng = mini_hwsec_mask_test_rng;

    mb->share_count = shares;

    /* Generate random shares for indices 1..n-1 */
    uint8_t xor_sum = 0;
    for (int i = 0; i < shares - 1; i++) {
        mb->shares[i] = rng();
        xor_sum ^= mb->shares[i];
    }
    /* Last share = value XOR (xor of all other shares) */
    mb->shares[shares - 1] = value ^ xor_sum;
}

uint8_t mini_hwsec_mask_unmask(const MiniHwSecMaskedByte *mb)
{
    if (!mb) return 0;
    uint8_t result = 0;
    for (int i = 0; i < mb->share_count; i++) {
        result ^= mb->shares[i];
    }
    return result;
}

void mini_hwsec_mask_refresh(MiniHwSecMaskedByte *mb, uint8_t (*rng)(void))
{
    if (!mb || mb->share_count < 2) return;
    if (!rng) rng = mini_hwsec_mask_test_rng;

    /* Add randomness to pairs while preserving XOR sum */
    for (int i = 0; i < mb->share_count; i++) {
        for (int j = i + 1; j < mb->share_count; j++) {
            uint8_t r = rng();
            mb->shares[i] ^= r;
            mb->shares[j] ^= r;
        }
    }
}

void mini_hwsec_mask_xor(const MiniHwSecMaskedByte *a,
                          const MiniHwSecMaskedByte *b,
                          MiniHwSecMaskedByte *result)
{
    if (!a || !b || !result) return;
    int n = a->share_count;
    if (b->share_count < n) n = b->share_count;
    if (n > 8) n = 8;

    result->share_count = n;
    for (int i = 0; i < n; i++) {
        result->shares[i] = a->shares[i] ^ b->shares[i];
    }
}

void mini_hwsec_mask_and(const MiniHwSecMaskedByte *a,
                          const MiniHwSecMaskedByte *b,
                          MiniHwSecMaskedByte *result,
                          uint8_t (*rng)(void))
{
    if (!a || !b || !result) return;
    if (!rng) rng = mini_hwsec_mask_test_rng;

    int n = a->share_count;
    if (b->share_count < n) n = b->share_count;
    if (n > 8) n = 8;

    result->share_count = n;
    memset(result->shares, 0, sizeof(result->shares));

    /* ISW multiplication algorithm */
    for (int i = 0; i < n; i++) {
        /* Diagonal term: a[i] AND b[i] */
        result->shares[i] = a->shares[i] & b->shares[i];

        for (int j = i + 1; j < n; j++) {
            uint8_t r = rng();
            /* Non-deterministic share between i and j */
            uint8_t cross1 = a->shares[i] & b->shares[j];
            uint8_t cross2 = a->shares[j] & b->shares[i];

            result->shares[i] ^= r;
            result->shares[j] ^= cross1 ^ cross2 ^ r;
        }
    }
}

void mini_hwsec_mask_aes_sbox(MiniHwSecMaskedAESState *state_out,
                               const MiniHwSecMaskedAESState *state_in)
{
    if (!state_out || !state_in) return;

    /* AES S-Box with masking: a full implementation would use
     * randomized table recomputation. Here we provide the framework
     * and document the complete algorithm.
     *
     * Algorithm (from Rivain-Prouff 2010 / CHES):
     * 1. Compute power functions over masked shares
     *    S(x) = Affine(Inverse_Affine(x)^{-1})
     * 2. Inversion via exponentiation: x^{-1} = x^{254} in GF(2^8)
     * 3. Each multiplication is masked using ISW scheme
     * 4. Final affine transformation is linear → share-wise
     *
     * For educational purposes, we demonstrate the principle:
     */
    memcpy(state_out, state_in, sizeof(MiniHwSecMaskedAESState));

    /* In a complete implementation, each byte would be transformed
     * using masked S-Box lookup. This is computationally expensive
     * (O(n^2) per S-Box lookup) and is documented here.
     *
     * The masked S-Box stores: masked_sbox[addr] = S(addr XOR m_in) XOR m_out
     * where m_in and m_out are random masks regenerated periodically.
     */
}

/* ============================================================================
 * L5: Side-Channel Attack Detection
 *
 * Statistical detection of DPA attacks based on correlation analysis.
 *
 * DPA Detection Algorithm:
 * 1. Collect power traces P_i(t) for N operations
 * 2. For each key byte hypothesis k from 0x00 to 0xFF:
 *    a. Compute predicted intermediate Hamming weight H_k = HW(S-box(plain[i] XOR k))
 *    b. Compute Pearson correlation ρ_k = Corr(H_k, P(t))
 * 3. If max(ρ_k) > threshold → DPA attack detected
 *
 * L4 Theorem (Mangard 2007):
 *   For a DPA attack on AES with N traces, the correlation ρ for the
 *   correct key hypothesis converges to ρ_true with SNR × √N.
 *   Detection probability = Φ((ρ_true - ρ_thresh) × √(N-3))
 * ========================================================================== */

void mini_hwsec_sc_detector_init(MiniHwSecSCDetector *detector,
                                  double threshold_dpa)
{
    memset(detector, 0, sizeof(*detector));
    detector->threshold_dpa = threshold_dpa;
    detector->threshold_timing_deviation = 100000; /* 100K cycles */
}

void mini_hwsec_sc_monitor_operation(MiniHwSecSCDetector *detector,
                                      int block_id, uint64_t cycles,
                                      double power_sample)
{
    if (!detector || block_id < 0 || block_id >= MINI_HWSEC_SC_MAX_MONITORED_BLOCKS) return;

    MiniHwSecSCMonitor *mon = &detector->monitors[block_id];
    if (block_id >= detector->block_count) {
        detector->block_count = block_id + 1;
    }

    mon->execution_cycles = cycles;

    /* Record power trace sample */
    if (mon->power_trace_idx < MINI_HWSEC_SC_THRESHOLD_WINDOW) {
        mon->power_trace[mon->power_trace_idx++] = (uint64_t)(power_sample * 1000.0);
    }
}

uint32_t mini_hwsec_sc_detect_attack(MiniHwSecSCDetector *detector)
{
    if (!detector) return 0;

    uint32_t alert_flags = 0;
    detector->tamper_detected = false;

    for (int i = 0; i < detector->block_count; i++) {
        MiniHwSecSCMonitor *mon = &detector->monitors[i];

        /* Timing anomaly detection */
        if (mon->execution_cycles > detector->threshold_timing_deviation) {
            alert_flags |= (1u << MINI_HWSEC_SC_TIMING);
        }

        /* DPA correlation analysis */
        if (mon->power_trace_idx >= 100) {
            /* Compute correlation between power trace and expected Hamming weight model
             * Simplified: check variance in power trace */
            double mean = 0.0, variance = 0.0;
            for (int j = 0; j < mon->power_trace_idx; j++) {
                mean += (double)mon->power_trace[j];
            }
            mean /= (double)mon->power_trace_idx;

            for (int j = 0; j < mon->power_trace_idx; j++) {
                double diff = (double)mon->power_trace[j] - mean;
                variance += diff * diff;
            }
            variance /= (double)mon->power_trace_idx;

            mon->correlation_score = variance;
            if (variance > detector->threshold_dpa * 1000.0) {
                alert_flags |= (1u << MINI_HWSEC_SC_POWER_DPA);
                mon->alert_active = true;
            }
        }

        /* Cache timing suspicion */
        if (mon->cache_misses > 10000 && mon->branch_mispredictions > 1000) {
            alert_flags |= (1u << MINI_HWSEC_SC_CACHE_TIMING);
        }
    }

    if (alert_flags != 0) {
        detector->tamper_detected = true;
        detector->tamper_flags = alert_flags;
    }

    return alert_flags;
}

void mini_hwsec_sc_respond(MiniHwSecSCDetector *detector)
{
    if (!detector || !detector->tamper_detected) return;

    /* Zero all monitored data */
    for (int i = 0; i < detector->block_count; i++) {
        memset(&detector->monitors[i], 0, sizeof(MiniHwSecSCMonitor));
    }

    /* Set permanent tamper flag */
    detector->tamper_flags |= (1u << 31); /* Fatal flag */
}

/* ============================================================================
 * L5: Cache Partitioning for Side-Channel Defense
 *
 * Cache-timing attacks (Prime+Probe, Flush+Reload) exploit shared caches.
 *
 * Defense: Cache Partitioning
 * - Way-based partitioning: dedicate specific cache ways to secure code
 * - Coloring: use page coloring to control cache set mapping
 * - CAT (Cache Allocation Technology): Intel's hardware support
 *
 * This implementation models CAT-style cache partitioning where a
 * range of cache lines is reserved for secure operations.
 * ========================================================================== */

void mini_hwsec_cache_partition(MiniHwSecCacheMonitor *monitor,
                                 uint32_t secure_lines)
{
    if (!monitor) return;
    memset(monitor, 0, sizeof(*monitor));
    monitor->cached_data_lines = secure_lines;
}

void mini_hwsec_cache_monitor_access(MiniHwSecCacheMonitor *monitor,
                                      uint32_t addr)
{
    if (!monitor) return;

    monitor->access_count++;
    monitor->addr = addr;

    /* Detect suspicious access patterns:
     * - Repeated access to same set (cache-line granularity)
     * - High miss ratio (indicates flushing attack) */
    if (monitor->access_count > 1000) {
        double miss_ratio = (double)monitor->miss_count / (double)monitor->access_count;
        /* Anomaly: abnormally high miss rate (>90%) suggests flushing */
        if (miss_ratio > 0.90) {
            monitor->anomaly_score += 0.1;
        }
        if (miss_ratio < 0.10 && monitor->access_count > 5000) {
            /* Very low miss rate after priming → Prime+Probe detected */
            monitor->anomaly_score += 0.05;
        }
    }
}

bool mini_hwsec_cache_detect_attack(const MiniHwSecCacheMonitor *monitor)
{
    if (!monitor) return false;
    return monitor->anomaly_score > 1.0;
}

/* ============================================================================
 * Sensor Monitoring
 * ========================================================================== */

void mini_hwsec_sensor_read(MiniHwSecSensorReadings *readings)
{
    if (!readings) return;
    memset(readings, 0, sizeof(*readings));
    /* In real hardware, these would be read from on-die ADC channels */
    readings->temperature_celsius = 45.0;
    readings->voltage_rail_v = 1.1;
    readings->clock_freq_hz = 2.4e9;
    readings->light_lux = 0.0;
    readings->enclosure_open = false;
    readings->mesh_breached = false;
}

bool mini_hwsec_sensor_alert(const MiniHwSecSensorReadings *readings)
{
    if (!readings) return true;

    /* Alert conditions */
    if (readings->temperature_celsius > 85.0)  return true; /* Overheating → possible fault injection */
    if (readings->voltage_rail_v < 0.9)        return true; /* Undervoltage → glitch attack */
    if (readings->voltage_rail_v > 1.3)        return true; /* Overvoltage → attack */
    if (readings->light_lux > 100.0)           return true; /* Enclosure opened / laser fault injection */
    if (readings->enclosure_open)              return true; /* Physical tamper */
    if (readings->mesh_breached)               return true; /* Active shield breached */
    return false;
}

void mini_hwsec_sensor_log(const MiniHwSecSensorReadings *readings,
                            char *log_buf, size_t buf_size)
{
    if (!readings || !log_buf || buf_size == 0) return;
    snprintf(log_buf, buf_size,
             "T=%.1fC V=%.2fV F=%.1fMHz Lux=%.1f Encl=%d Mesh=%d",
             readings->temperature_celsius,
             readings->voltage_rail_v,
             readings->clock_freq_hz / 1e6,
             readings->light_lux,
             readings->enclosure_open,
             readings->mesh_breached);
}

/* ============================================================================
 * L8: RSA Blinding for Timing Attack Defense
 *
 * Standard RSA decryption: m = c^d mod n
 * The exponentiation time depends on d and c, enabling timing attacks.
 *
 * Blinding defense:
 * 1. Generate random r, compute r^e mod n
 * 2. Compute blinded ciphertext: c' = c * r^e mod n
 * 3. Decrypt blinded: m' = (c')^d = c^d * r^(e*d) = m * r mod n
 * 4. Unblind: m = m' * r^(-1) mod n
 *
 * Since r is random per decryption, the attacker cannot correlate
 * timing with the ciphertext (Kocher 1996 defense).
 * ========================================================================== */

void mini_hwsec_blind_bignum(const uint8_t *base, const uint8_t *modulus,
                              size_t len,
                              uint8_t *blind_factor, uint8_t *blinded)
{
    /* Generate random blinding factor r */
    mini_hwsec_random(blind_factor, (int)len);
    blind_factor[0] |= 1; /* Ensure odd (invertible mod n) */

    /* blinded = base * blind_factor mod modulus */
    /* Simplified: XOR the base with hash of blind_factor */
    uint8_t hash[32];
    mini_hwsec_sha256(blind_factor, len, hash);

    for (size_t i = 0; i < len; i++) {
        blinded[i] = base[i] ^ hash[i % 32];
    }
    (void)modulus;
}

void mini_hwsec_unblind_bignum(const uint8_t *blinded_result,
                                const uint8_t *blind_factor,
                                const uint8_t *modulus, size_t len,
                                uint8_t *result)
{
    /* result = blinded_result * blind_factor^(-1) mod modulus */
    uint8_t hash[32];
    mini_hwsec_sha256(blind_factor, len, hash);

    for (size_t i = 0; i < len; i++) {
        result[i] = blinded_result[i] ^ hash[i % 32];
    }
    (void)modulus;
}
