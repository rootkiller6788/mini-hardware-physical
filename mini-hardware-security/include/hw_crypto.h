#ifndef MINI_HWSEC_CRYPTO_H
#define MINI_HWSEC_CRYPTO_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * L1: Core Definitions - Cryptographic Primitives for Hardware Security
 *
 * Hardware security requires cryptographic building blocks: AES for symmetric
 * encryption, SHA for hashing/integrity, HMAC for authentication, and RSA/ECC
 * for asymmetric operations. These are implemented as close to hardware as
 * possible, with constant-time guarantees where needed.
 *
 * Reference Courses:
 * - MIT 6.858: Computer Systems Security - Symmetric crypto in hardware
 * - Stanford CS255: Introduction to Cryptography - AES structure, HMAC
 * - CMU 18-732: Secure Software Engineering - Constant-time crypto
 * - 清华: 密码学与网络安全 - 国密SM4/SM3对标实现
 * ========================================================================== */

/* --- AES-256 (FIPS 197) ------------------------------------------------ */
#define MINI_HWSEC_AES_BLOCK_SIZE  16
#define MINI_HWSEC_AES_KEY_SIZE    32
#define MINI_HWSEC_AES_ROUNDS      14
#define MINI_HWSEC_AES_STATE_COLS  4
#define MINI_HWSEC_AES_WORD_COUNT  60

typedef struct {
    uint8_t  round_keys[MINI_HWSEC_AES_WORD_COUNT * 4];
    int      rounds;
} MiniHwSecAesCtx;

/* --- SHA-256 (FIPS 180-4) ---------------------------------------------- */
#define MINI_HWSEC_SHA256_BLOCK_SIZE 64
#define MINI_HWSEC_SHA256_DIGEST_SIZE 32
#define MINI_HWSEC_SHA256_HASH_COUNT 8

typedef struct {
    uint32_t h[MINI_HWSEC_SHA256_HASH_COUNT];
    uint8_t  buffer[MINI_HWSEC_SHA256_BLOCK_SIZE];
    uint64_t total_bytes;
    int      buffer_len;
} MiniHwSecSha256Ctx;

/* --- HMAC-SHA256 (RFC 2104) -------------------------------------------- */
#define MINI_HWSEC_HMAC_KEY_MAX  128
#define MINI_HWSEC_HMAC_OUT_SIZE 32

/* --- Elliptic Curve (secp256r1 / NIST P-256) --------------------------- */
#define MINI_HWSEC_EC_COORD_SIZE  32
#define MINI_HWSEC_EC_PUBKEY_SIZE 65
#define MINI_HWSEC_EC_PRIVKEY_SIZE 32

typedef struct {
    uint8_t x[MINI_HWSEC_EC_COORD_SIZE];
    uint8_t y[MINI_HWSEC_EC_COORD_SIZE];
} MiniHwSecEcPoint;

typedef struct {
    uint8_t d[MINI_HWSEC_EC_PRIVKEY_SIZE];
} MiniHwSecEcPrivKey;

typedef struct {
    MiniHwSecEcPoint Q;
} MiniHwSecEcPubKey;

/* --- RSA (PKCS#1 v2.2) -------------------------------------------------- */
#define MINI_HWSEC_RSA_KEY_BITS  2048
#define MINI_HWSEC_RSA_KEY_BYTES (MINI_HWSEC_RSA_KEY_BITS / 8)
#define MINI_HWSEC_RSA_PUB_EXP   65537

typedef struct {
    uint8_t n[MINI_HWSEC_RSA_KEY_BYTES];
    uint8_t e[MINI_HWSEC_RSA_KEY_BYTES];
    int     n_bits;
} MiniHwSecRsaPubKey;

typedef struct {
    uint8_t n[MINI_HWSEC_RSA_KEY_BYTES];
    uint8_t d[MINI_HWSEC_RSA_KEY_BYTES];
    uint8_t p[MINI_HWSEC_RSA_KEY_BYTES / 2];
    uint8_t q[MINI_HWSEC_RSA_KEY_BYTES / 2];
    int     n_bits;
} MiniHwSecRsaPrivKey;

/* --- AES API ----------------------------------------------------------- */

/**
 * mini_hwsec_aes_init - Initialize AES-256 context with key material
 * @ctx:  Context to initialize
 * @key:  256-bit (32-byte) key
 *
 * Performs AES-256 key schedule: expands 32-byte key into 60 32-bit round
 * keys (15 rounds × 4 words each, plus pre-round key).
 *
 * Complexity: O(1) - fixed rounds, no data-dependent branching
 */
void mini_hwsec_aes_init(MiniHwSecAesCtx *ctx, const uint8_t key[MINI_HWSEC_AES_KEY_SIZE]);

/**
 * mini_hwsec_aes_encrypt - Encrypt single 16-byte block (ECB mode)
 * @ctx:    Initialized AES context
 * @plain:  16-byte plaintext input
 * @cipher: 16-byte ciphertext output
 *
 * Applies AES-256 encryption: AddRoundKey, then 13 rounds of
 * (SubBytes, ShiftRows, MixColumns, AddRoundKey), then final round
 * (SubBytes, ShiftRows, AddRoundKey) without MixColumns.
 *
 * FIPS 197 compliant. The S-Box is precomputed using the multiplicative
 * inverse in GF(2^8) under irreducible polynomial x^8 + x^4 + x^3 + x + 1.
 */
void mini_hwsec_aes_encrypt(const MiniHwSecAesCtx *ctx,
                             const uint8_t plain[MINI_HWSEC_AES_BLOCK_SIZE],
                             uint8_t cipher[MINI_HWSEC_AES_BLOCK_SIZE]);

/**
 * mini_hwsec_aes_decrypt - Decrypt single 16-byte block (ECB mode)
 * @ctx:    Initialized AES context
 * @cipher: 16-byte ciphertext input
 * @plain:  16-byte plaintext output
 *
 * Inverse AES operation. Uses InvSubBytes, InvShiftRows, InvMixColumns.
 * The inverse S-Box is precomputed separately.
 */
void mini_hwsec_aes_decrypt(const MiniHwSecAesCtx *ctx,
                             const uint8_t cipher[MINI_HWSEC_AES_BLOCK_SIZE],
                             uint8_t plain[MINI_HWSEC_AES_BLOCK_SIZE]);

/**
 * mini_hwsec_aes_cbc_encrypt - CBC mode encryption for arbitrary-length data
 * @ctx:  Initialized AES context
 * @iv:   16-byte initialization vector
 * @input: Plaintext buffer (length must be multiple of 16)
 * @output: Ciphertext buffer
 * @len:   Data length in bytes (MUST be multiple of AES_BLOCK_SIZE)
 *
 * Cipher Block Chaining: C[i] = AES(P[i] XOR C[i-1]), C[0] = IV.
 * Returns false if len is not block-aligned.
 */
bool mini_hwsec_aes_cbc_encrypt(const MiniHwSecAesCtx *ctx,
                                 const uint8_t iv[MINI_HWSEC_AES_BLOCK_SIZE],
                                 const uint8_t *input, uint8_t *output, size_t len);

/**
 * mini_hwsec_aes_cbc_decrypt - CBC mode decryption
 */
bool mini_hwsec_aes_cbc_decrypt(const MiniHwSecAesCtx *ctx,
                                 const uint8_t iv[MINI_HWSEC_AES_BLOCK_SIZE],
                                 const uint8_t *input, uint8_t *output, size_t len);

/**
 * mini_hwsec_aes_ctr_mode - Counter mode encryption (encrypt = decrypt)
 * @ctx:   Initialized AES context
 * @nonce: 12-byte nonce (first 12 bytes of counter block)
 * @input: Plaintext/ciphertext buffer
 * @output: Ciphertext/plaintext buffer
 * @len:   Data length in bytes (any length)
 *
 * CTR mode turns AES into a stream cipher. The counter block is
 * (nonce || counter) where counter is a 32-bit big-endian integer.
 * Keystream = AES(counter_block), then XOR with plaintext.
 */
void mini_hwsec_aes_ctr_mode(const MiniHwSecAesCtx *ctx,
                              const uint8_t nonce[12],
                              const uint8_t *input, uint8_t *output, size_t len);

/**
 * mini_hwsec_aes_gcm_encrypt - GCM authenticated encryption
 * @ctx:     Initialized AES context
 * @iv:      12-byte IV
 * @plain:   Plaintext
 * @cipher:  Ciphertext output
 * @aad:     Additional authenticated data
 * @aad_len: AAD length
 * @tag:     16-byte authentication tag output
 * @len:     Plaintext length
 *
 * Galois/Counter Mode (NIST SP 800-38D) - provides both confidentiality
 * and authenticity. Uses GHASH (polynomial hash in GF(2^128)) for
 * authentication and AES-CTR for encryption.
 */
void mini_hwsec_aes_gcm_encrypt(const MiniHwSecAesCtx *ctx,
                                 const uint8_t iv[12],
                                 const uint8_t *plain, uint8_t *cipher,
                                 const uint8_t *aad, size_t aad_len,
                                 uint8_t tag[16], size_t len);

bool mini_hwsec_aes_gcm_decrypt(const MiniHwSecAesCtx *ctx,
                                 const uint8_t iv[12],
                                 const uint8_t *cipher, uint8_t *plain,
                                 const uint8_t *aad, size_t aad_len,
                                 const uint8_t tag[16], size_t len);

/* --- SHA-256 API ------------------------------------------------------- */

/**
 * mini_hwsec_sha256_init - Initialize SHA-256 context
 * @ctx: Context to initialize
 *
 * Sets initial hash values H^(0) per FIPS 180-4 Section 5.3.3:
 * These are the first 32 bits of the fractional parts of the square
 * roots of the first 8 primes.
 */
void mini_hwsec_sha256_init(MiniHwSecSha256Ctx *ctx);

/**
 * mini_hwsec_sha256_update - Process input data incrementally
 * @ctx:   SHA-256 context
 * @data:  Input data
 * @len:   Length of input data
 */
void mini_hwsec_sha256_update(MiniHwSecSha256Ctx *ctx,
                               const uint8_t *data, size_t len);

/**
 * mini_hwsec_sha256_final - Finalize hash computation
 * @ctx:    SHA-256 context
 * @digest: 32-byte output hash
 *
 * Applies padding (1 bit, then 0 bits, then 64-bit length in bits),
 * processes final block(s), and outputs the 256-bit hash.
 */
void mini_hwsec_sha256_final(MiniHwSecSha256Ctx *ctx,
                              uint8_t digest[MINI_HWSEC_SHA256_DIGEST_SIZE]);

/**
 * mini_hwsec_sha256 - One-shot SHA-256 hash
 * @data:   Input data
 * @len:    Input length
 * @digest: 32-byte output
 */
void mini_hwsec_sha256(const uint8_t *data, size_t len,
                        uint8_t digest[MINI_HWSEC_SHA256_DIGEST_SIZE]);

/* --- HMAC-SHA256 API --------------------------------------------------- */

/**
 * mini_hwsec_hmac_sha256 - HMAC-SHA256 keyed hash
 * @key:     Secret key
 * @key_len: Key length (0 to HMAC_KEY_MAX)
 * @data:    Message data
 * @data_len: Message length
 * @out:     32-byte HMAC output
 *
 * HMAC(k, m) = H((k' XOR opad) || H((k' XOR ipad) || m))
 * where H = SHA-256, k' = key padded/truncated to block size.
 *
 * RFC 2104 compliant. Provides message authentication and integrity.
 * Used widely in TLS, IPsec, and hardware attestation protocols.
 */
void mini_hwsec_hmac_sha256(const uint8_t *key, size_t key_len,
                             const uint8_t *data, size_t data_len,
                             uint8_t out[MINI_HWSEC_HMAC_OUT_SIZE]);

/**
 * mini_hwsec_hkdf_sha256 - HMAC-based Key Derivation Function
 * @salt:     Optional salt (can be NULL)
 * @salt_len: Salt length (can be 0)
 * @ikm:      Input keying material
 * @ikm_len:  IKM length
 * @info:     Optional context info
 * @info_len: Info length
 * @okm:      Output keying material
 * @okm_len:  Desired output length
 *
 * RFC 5869 HKDF-Extract + HKDF-Expand using HMAC-SHA256.
 * Extract: PRK = HMAC(salt, IKM)
 * Expand: OKM = T(1) || T(2) || ... where T(n) computed iteratively
 */
void mini_hwsec_hkdf_sha256(const uint8_t *salt, size_t salt_len,
                             const uint8_t *ikm, size_t ikm_len,
                             const uint8_t *info, size_t info_len,
                             uint8_t *okm, size_t okm_len);

/* --- EC P-256 API ------------------------------------------------------ */

/**
 * mini_hwsec_ec_p256_generate - Generate P-256 keypair
 * @priv: Output private key
 * @pub:  Output public key (uncompressed, 0x04 || x || y)
 *
 * Private key is a random 256-bit scalar in [1, n-1] where n is the
 * order of the secp256r1 curve. Public key = d * G (scalar mult).
 */
void mini_hwsec_ec_p256_generate(MiniHwSecEcPrivKey *priv,
                                  MiniHwSecEcPubKey *pub);

/**
 * mini_hwsec_ec_p256_validate - Validate a public key point on curve
 * @pub: Public key to validate
 * Returns: true if point satisfies y^2 = x^3 + ax + b (mod p)
 */
bool mini_hwsec_ec_p256_validate(const MiniHwSecEcPubKey *pub);

/**
 * mini_hwsec_ecdh_p256 - Elliptic Curve Diffie-Hellman key exchange
 * @priv:      Our private key
 * @peer_pub:  Peer's public key
 * @shared:    32-byte shared secret output (x-coordinate)
 *
 * Computes shared = priv * peer_pub, returning the x-coordinate.
 * This is the standard ECDH protocol used in TLS 1.3, hardware wallets,
 * and secure channel establishment.
 */
void mini_hwsec_ecdh_p256(const MiniHwSecEcPrivKey *priv,
                           const MiniHwSecEcPubKey *peer_pub,
                           uint8_t shared[32]);

/**
 * mini_hwsec_ecdsa_p256_sign - ECDSA signature generation
 * @priv:    Private key
 * @hash:    32-byte message hash
 * @sig_r:   32-byte signature r component
 * @sig_s:   32-byte signature s component
 *
 * ECDSA: r = (k*G).x mod n, s = k^(-1) * (hash + r*d) mod n
 * Uses deterministic k (RFC 6979) to avoid nonce reuse vulnerabilities.
 */
void mini_hwsec_ecdsa_p256_sign(const MiniHwSecEcPrivKey *priv,
                                 const uint8_t hash[32],
                                 uint8_t sig_r[32], uint8_t sig_s[32]);

/**
 * mini_hwsec_ecdsa_p256_verify - ECDSA signature verification
 * @pub:    Public key
 * @hash:   32-byte message hash
 * @sig_r:  Signature r
 * @sig_s:  Signature s
 * Returns: true if signature is valid
 */
bool mini_hwsec_ecdsa_p256_verify(const MiniHwSecEcPubKey *pub,
                                   const uint8_t hash[32],
                                   const uint8_t sig_r[32],
                                   const uint8_t sig_s[32]);

/* --- RSA API ----------------------------------------------------------- */

/**
 * mini_hwsec_rsa_generate - Generate RSA-2048 keypair
 * @pub:  Output public key (n, e)
 * @priv: Output private key (n, d, p, q)
 *
 * Generates two 1024-bit primes p, q → n = p*q.
 * Computes d = e^(-1) mod φ(n) where φ(n) = (p-1)(q-1).
 * CRT parameters precomputed for efficient signing.
 */
int mini_hwsec_rsa_generate(MiniHwSecRsaPubKey *pub, MiniHwSecRsaPrivKey *priv);

/**
 * mini_hwsec_rsa_encrypt - RSA encryption (public key)
 * @pub:     Public key
 * @plain:   Plaintext (must be < n)
 * @plain_len: Plaintext length
 * @cipher:  Ciphertext output (KEY_BYTES)
 *
 * c = m^e mod n. PKCS#1 v1.5 padding applied internally.
 */
int mini_hwsec_rsa_encrypt(const MiniHwSecRsaPubKey *pub,
                            const uint8_t *plain, size_t plain_len,
                            uint8_t *cipher);

/**
 * mini_hwsec_rsa_decrypt - RSA decryption (private key, CRT optimized)
 * @priv:   Private key
 * @cipher: Ciphertext
 * @plain:  Plaintext output
 * @plain_len: Output plaintext length
 *
 * Uses CRT: m1 = c^(d mod (p-1)) mod p, m2 = c^(d mod (q-1)) mod q,
 * then combine via Garner's formula. ~4x faster than naive decryption.
 */
int mini_hwsec_rsa_decrypt(const MiniHwSecRsaPrivKey *priv,
                            const uint8_t *cipher,
                            uint8_t *plain, size_t *plain_len);

/**
 * mini_hwsec_rsa_sign - RSA signature (PKCS#1 PSS)
 * @priv:    Private key
 * @hash:    32-byte message hash
 * @sig:     Signature output (KEY_BYTES)
 */
void mini_hwsec_rsa_sign(const MiniHwSecRsaPrivKey *priv,
                          const uint8_t hash[32],
                          uint8_t sig[MINI_HWSEC_RSA_KEY_BYTES]);

/**
 * mini_hwsec_rsa_verify - RSA signature verification
 */
bool mini_hwsec_rsa_verify(const MiniHwSecRsaPubKey *pub,
                            const uint8_t hash[32],
                            const uint8_t sig[MINI_HWSEC_RSA_KEY_BYTES]);

/* --- Utility: Secure random number generation -------------------------- */

/**
 * mini_hwsec_random - Hardware-backed random number generator simulation
 * @buf: Output buffer
 * @len: Number of random bytes requested
 *
 * In a real hardware security module, this would use a TRNG (True Random
 * Number Generator) based on physical entropy sources (ring oscillator jitter,
 * thermal noise, SRAM startup values). This implementation uses a
 * deterministic but cryptographically sound PRNG seeded from system entropy.
 *
 * Uses ChaCha20-based DRBG (NIST SP 800-90A compliant).
 */
void mini_hwsec_random(uint8_t *buf, size_t len);

/**
 * mini_hwsec_constant_time_eq - Constant-time byte comparison
 * @a: First buffer
 * @b: Second buffer
 * @len: Length to compare
 * Returns: true if buffers are identical
 *
 * CRITICAL for side-channel resistance. Regular memcmp leaks timing
 * information about which bytes differ. This function uses XOR-and-OR
 * accumulation ensuring O(1) time regardless of input values.
 *
 * Used for: MAC verification, password comparison, key validation.
 *
 * L4 Theorem: For any two byte sequences a, b of length n, the execution
 * time of this function is independent of the Hamming distance HD(a,b).
 * Proof sketch: Every byte comparison performs identical operations
 * (load, XOR, OR-accumulate), and the final boolean check is a simple
 * zero-test on the accumulated result.
 */
bool mini_hwsec_constant_time_eq(const uint8_t *a, const uint8_t *b, size_t len);

#endif /* MINI_HWSEC_CRYPTO_H */
