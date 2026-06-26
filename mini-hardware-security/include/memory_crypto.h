#ifndef MINI_HWSEC_MEMORY_CRYPTO_H
#define MINI_HWSEC_MEMORY_CRYPTO_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "hw_crypto.h"

/* ============================================================================
 * L2/L3: Memory Encryption Engine - Hardware-Side Memory Protection
 *
 * Memory encryption protects DRAM contents against physical attacks:
 * - Cold boot attacks (freeze DRAM, read residual charge)
 * - Bus snooping (passive monitoring of memory bus)
 * - DMA attacks (malicious peripheral reads memory)
 *
 * Major implementations:
 * - Intel TME/MKTME: Total/ Multi-Key Memory Encryption
 * - AMD SME/SEV: Secure Memory Encryption / Secure Encrypted Virtualization
 * - ARM CCA: Confidential Compute Architecture (Realm memory encryption)
 *
 * Key concepts:
 * - Encryption granularity: cache line (64 bytes typically)
 * - Tweaked encryption: each cache line uses unique tweak (address-based)
 * - Integrity tree: Merkle tree over all memory to detect tampering
 * - Counters: per-cache-line version counters to prevent replay
 *
 * Reference:
 * - MIT 6.858: Memory encryption & integrity
 * - Gueron 2016: Intel MKTME architecture (AES-XTS for memory)
 * - Rogers 2007: Memory encryption using AES in counter mode
 * - AMI 2020: AMD SEV-SNP: Strengthening VM Isolation
 * ========================================================================== */

/* --- Memory Encryption Constants --------------------------------------- */
#define MINI_HWSEC_MEM_LINE_SIZE       64   /* Cache line = encryption unit */
#define MINI_HWSEC_MEM_TWEAK_SIZE      16   /* Address-based tweak */
#define MINI_HWSEC_MEM_AUTH_TAG_SIZE   16   /* GCM authentication tag */
#define MINI_HWSEC_MEM_MAX_REGIONS     16   /* Memory regions with different keys */

/* --- Encrypted Memory Region ------------------------------------------- */
typedef struct {
    uint64_t base_address;          /* Physical base address */
    uint64_t size;                  /* Region size in bytes */
    uint8_t  encryption_key[MINI_HWSEC_AES_KEY_SIZE]; /* AES-256 key for this region */
    uint8_t  integrity_key[MINI_HWSEC_AES_KEY_SIZE];  /* Key for integrity tree */
    bool     encryption_enabled;
    bool     integrity_enabled;
    uint32_t region_id;
} MiniHwSecMemRegion;

/* --- Cache Line Encryption Context ------------------------------------- */
typedef struct {
    uint64_t line_address;      /* Physical address of this cache line */
    uint8_t  ciphertext[MINI_HWSEC_MEM_LINE_SIZE + MINI_HWSEC_MEM_AUTH_TAG_SIZE];
    uint8_t  tweak[MINI_HWSEC_MEM_TWEAK_SIZE];
    bool     is_encrypted;
    bool     auth_verified;
} MiniHwSecMemEncryptedLine;

/* --- Memory Integrity Tree (Merkle Tree) ------------------------------- */
#define MINI_HWSEC_MEM_MERKLE_ARITY  4   /* 4-ary Merkle tree */
#define MINI_HWSEC_MEM_MERKLE_LEVELS 10  /* Up to 4^10 ≈ 1M cache lines = 64 MB */

typedef struct {
    uint8_t  hash[MINI_HWSEC_SHA256_DIGEST_SIZE];
    bool     verified;
} MiniHwSecMemMerkleNode;

typedef struct {
    MiniHwSecMemMerkleNode *tree;       /* Dynamic array for Merkle tree */
    uint64_t total_lines;               /* Total data lines protected */
    int      levels;                    /* Tree height */
    uint64_t root_verified_at;          /* Timestamp of last root verification */
    bool     root_trusted;              /* Root stored in on-die SRAM */
} MiniHwSecMemIntegrityTree;

/* --- Memory Encryption Engine (MEE) ------------------------------------ */
typedef struct {
    MiniHwSecMemRegion     regions[MINI_HWSEC_MEM_MAX_REGIONS];
    int                    region_count;
    MiniHwSecMemIntegrityTree integrity_tree;
    MiniHwSecAesCtx        crypto_ctx;       /* AES context for memory encryption */
    bool                   initialized;
    uint64_t               encrypted_lines;  /* Performance counter */
    uint64_t               integrity_failures;
    uint64_t               replay_blocked;
} MiniHwSecMemEngine;

/* --- Replay Protection Counters ---------------------------------------- */
#define MINI_HWSEC_MEM_COUNTER_BITS  56
#define MINI_HWSEC_MEM_COUNTER_MASK  ((1ULL << MINI_HWSEC_MEM_COUNTER_BITS) - 1)

typedef struct {
    uint64_t             counters[MINI_HWSEC_MEM_MAX_REGIONS * 1024];
    int                  counter_entries;
    uint64_t             last_write_timestamp;
} MiniHwSecMemReplayCounters;

/* --- API -------------------------------------------------------------- */

/**
 * mini_hwsec_mem_engine_init - Initialize memory encryption engine
 * @engine: Memory engine to initialize
 * @master_key: 256-bit master key for memory encryption
 *
 * Configures the memory encryption engine. In hardware, the MEE sits
 * between the CPU's L2/L3 cache and the memory controller, transparently
 * encrypting/decrypting all off-chip memory traffic.
 *
 * The master key should come from the HRoT (PUF-derived key).
 */
void mini_hwsec_mem_engine_init(MiniHwSecMemEngine *engine,
                                 const uint8_t master_key[MINI_HWSEC_AES_KEY_SIZE]);

/**
 * mini_hwsec_mem_region_add - Register an encrypted memory region
 * @engine:      Memory engine
 * @base_addr:   Physical base address
 * @size:        Region size (must be cache-line aligned)
 * Returns: Region ID or -1 on error
 *
 * Different regions can use different keys (derived from master + region_id).
 * This enables VM-level isolation: each VM gets its own encryption key
 * (AMD SEV model) or process-level isolation.
 */
int mini_hwsec_mem_region_add(MiniHwSecMemEngine *engine,
                               uint64_t base_addr, uint64_t size);

/**
 * mini_hwsec_mem_encrypt_line - Encrypt a cache line before writing to DRAM
 * @engine:       Memory engine
 * @region_id:    Region this line belongs to
 * @phys_addr:    Physical address (used as tweak)
 * @plaintext:    64-byte plaintext (the cache line)
 * @auth_line:    Output: encrypted line + authentication tag
 *
 * Uses AES-XTS mode for memory encryption:
 * T = AES(key2, phys_addr)            ← tweak (address-dependent)
 * C = AES(key1, P XOR T) XOR T       ← encryption with tweak
 *
 * XTS (XEX-based Tweaked-codebook mode with ciphertext Stealing) is
 * the standard for disk/memory encryption (IEEE P1619).
 *
 * The tweak ensures that identical plaintext at different addresses
 * encrypts to different ciphertext, preventing block relocation attacks.
 */
void mini_hwsec_mem_encrypt_line(MiniHwSecMemEngine *engine,
                                  int region_id, uint64_t phys_addr,
                                  const uint8_t plaintext[MINI_HWSEC_MEM_LINE_SIZE],
                                  MiniHwSecMemEncryptedLine *auth_line);

/**
 * mini_hwsec_mem_decrypt_line - Decrypt and verify a cache line from DRAM
 * @engine:    Memory engine
 * @region_id: Region ID
 * @phys_addr: Physical address
 * @auth_line: Encrypted line with auth tag
 * @plaintext: Output 64-byte decrypted line
 * Returns: true if auth tag verified and decryption succeeded
 *
 * Verifies the GCM authentication tag before returning plaintext.
 * If verification fails, the engine signals a security fault and
 * zeros the output (prevents data leakage on corruption).
 */
bool mini_hwsec_mem_decrypt_line(MiniHwSecMemEngine *engine,
                                  int region_id, uint64_t phys_addr,
                                  const MiniHwSecMemEncryptedLine *auth_line,
                                  uint8_t plaintext[MINI_HWSEC_MEM_LINE_SIZE]);

/* --- Integrity Tree API ----------------------------------------------- */

/**
 * mini_hwsec_mem_integrity_init - Build integrity tree for memory region
 * @engine:    Memory engine
 * @base_addr: Start of region to protect
 * @total_lines: Number of cache lines covered
 * Returns: true if tree initialized
 *
 * L5 Algorithm: Merkle Tree Construction
 *
 * For each 64-byte cache line L_i at address A_i:
 *   1. Leaf node h_i = Hash(L_i, counter_i, address_i)
 *   2. Parent node h_parent = Hash(h_child1, ..., h_child4)  (4-ary tree)
 *   3. Continue until root
 *   4. Root stored in on-die SRAM (trusted, cannot be tampered with)
 *
 * On memory read:
 *   1. Fetch data line + siblings + counters from DRAM
 *   2. Recompute hash chain up to root
 *   3. Compare computed root with trusted on-die root
 *   4. If mismatch → tamper detected → security fault
 *
 * Space overhead: ~3% for 4-ary Merkle tree over 64 MB.
 */
bool mini_hwsec_mem_integrity_init(MiniHwSecMemEngine *engine,
                                    uint64_t base_addr, uint64_t total_lines);

/**
 * mini_hwsec_mem_integrity_verify - Verify integrity of a memory line
 * @engine:    Memory engine
 * @phys_addr: Address to verify
 * @data:      64-byte data line to verify
 * Returns: true if verified, false if tampering detected
 *
 * L4 Theorem: Merkle tree verification provides tamper-evidence
 * with probability at least 1 - 2^(-256) per verification,
 * assuming SHA-256 collision resistance.
 *
 * The root of the tree is stored in trusted on-die memory and is
 * updated atomically with memory writes.
 */
bool mini_hwsec_mem_integrity_verify(MiniHwSecMemEngine *engine,
                                      uint64_t phys_addr,
                                      const uint8_t data[MINI_HWSEC_MEM_LINE_SIZE]);

/**
 * mini_hwsec_mem_integrity_update - Update integrity after memory write
 * @engine:    Memory engine
 * @phys_addr: Address written
 * @new_data:  New 64-byte data
 *
 * Updates the Merkle tree from leaf to root. The root update is
 * committed atomically to on-die storage.
 */
void mini_hwsec_mem_integrity_update(MiniHwSecMemEngine *engine,
                                      uint64_t phys_addr,
                                      const uint8_t new_data[MINI_HWSEC_MEM_LINE_SIZE]);

/* --- Replay Protection API -------------------------------------------- */

/**
 * mini_hwsec_mem_replay_protect - Check replay counter
 * @counters: Replay counter array
 * @line_addr: Line address
 * @claimed_counter: Counter value claimed by memory
 * Returns: true if counter is valid (no replay)
 *
 * Each cache line has a 56-bit monotonically incrementing counter.
 * On write: counter = max(counter, global_timestamp++)
 * On read: verify counter hasn't decreased
 *
 * Counters stored in a separate on-die counter cache (reducing DRAM overhead)
 * with periodic write-back to DRAM for persistence across power cycles.
 *
 * Anti-replay: Even if an attacker can read encrypted DRAM contents,
 * replaying old ciphertext will fail because the counter has advanced.
 */
bool mini_hwsec_mem_replay_protect(MiniHwSecMemReplayCounters *counters,
                                    uint64_t line_addr,
                                    uint64_t claimed_counter);

/**
 * mini_hwsec_mem_counter_increment - Increment counter for a line
 */
void mini_hwsec_mem_counter_increment(MiniHwSecMemReplayCounters *counters,
                                       uint64_t line_addr);

/* --- Security Monitoring ---------------------------------------------- */
void     mini_hwsec_mem_get_stats(const MiniHwSecMemEngine *engine,
                                   uint64_t *encrypted, uint64_t *failures,
                                   uint64_t *replays);
void     mini_hwsec_mem_engine_destroy(MiniHwSecMemEngine *engine);

/**
 * mini_hwsec_mem_bulk_encrypt - Encrypt bulk data for secure DMA
 * @engine:    Memory engine
 * @region_id: Region
 * @phys_start: Start physical address
 * @plain:     Plaintext buffer
 * @cipher:    Ciphertext buffer
 * @len:       Length (must be line-aligned)
 *
 * Used for secure DMA transfers where data must be encrypted
 * before being sent to potentially untrusted devices.
 */
bool mini_hwsec_mem_bulk_encrypt(MiniHwSecMemEngine *engine,
                                  int region_id, uint64_t phys_start,
                                  const uint8_t *plain, uint8_t *cipher, size_t len);
bool mini_hwsec_mem_bulk_decrypt(MiniHwSecMemEngine *engine,
                                  int region_id, uint64_t phys_start,
                                  const uint8_t *cipher, uint8_t *plain, size_t len);

#endif /* MINI_HWSEC_MEMORY_CRYPTO_H */
