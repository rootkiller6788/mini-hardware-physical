#include "secure_enclave.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint32_t sha256_rotate_right(uint32_t val, int n) {
    return (val >> n) | (val << (32 - n));
}

static void sha256_init(uint32_t state[8]) {
    state[0] = 0x6a09e667; state[1] = 0xbb67ae85;
    state[2] = 0x3c6ef372; state[3] = 0xa54ff53a;
    state[4] = 0x510e527f; state[5] = 0x9b05688c;
    state[6] = 0x1f83d9ab; state[7] = 0x5be0cd19;
}

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = sha256_rotate_right(w[i-15], 7) ^
                      sha256_rotate_right(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = sha256_rotate_right(w[i-2], 17) ^
                      sha256_rotate_right(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = sha256_rotate_right(e, 6) ^
                      sha256_rotate_right(e, 11) ^ sha256_rotate_right(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + sha256_k[i] + w[i];
        uint32_t S0 = sha256_rotate_right(a, 2) ^
                      sha256_rotate_right(a, 13) ^ sha256_rotate_right(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256_hash(const uint8_t *data, int len, uint8_t digest[32]) {
    uint32_t state[8];
    sha256_init(state);
    uint8_t block[64];
    int block_len = 0;
    uint64_t bit_len = (uint64_t)len * 8;
    for (int i = 0; i < len; i++) {
        block[block_len++] = data[i];
        if (block_len == 64) {
            sha256_transform(state, block);
            block_len = 0;
        }
    }
    block[block_len++] = 0x80;
    if (block_len > 56) {
        while (block_len < 64) block[block_len++] = 0;
        sha256_transform(state, block);
        block_len = 0;
    }
    while (block_len < 56) block[block_len++] = 0;
    for (int i = 0; i < 8; i++) {
        block[56 + i] = (uint8_t)(bit_len >> (56 - i * 8));
    }
    sha256_transform(state, block);
    for (int i = 0; i < 8; i++) {
        digest[i*4]   = (uint8_t)(state[i] >> 24);
        digest[i*4+1] = (uint8_t)(state[i] >> 16);
        digest[i*4+2] = (uint8_t)(state[i] >> 8);
        digest[i*4+3] = (uint8_t)(state[i]);
    }
}

static void xor_encrypt(const uint8_t *key, const uint8_t *in,
                         uint8_t *out, int len) {
    for (int i = 0; i < len; i++) {
        out[i] = in[i] ^ key[i % SHA256_DIGEST];
    }
}

void enclave_create(Enclave *e) {
    memset(e, 0, sizeof(Enclave));
    e->state = ENCLAVE_UNINIT;
    for (int i = 0; i < SHA256_DIGEST; i++) {
        e->sealing_key[i] = (uint8_t)(0xAA + i * 7);
    }
    sha256_hash(e->sealing_key, SHA256_DIGEST, e->sealing_key);
}

void enclave_add_page(Enclave *e, const uint8_t *data, int len, uint64_t addr) {
    if (e->state != ENCLAVE_UNINIT && e->state != ENCLAVE_BUILDING) return;
    e->state = ENCLAVE_BUILDING;
    int page_idx = (int)(addr / EPC_PAGE_SIZE);
    if (page_idx < 0 || page_idx >= EPC_PAGES) return;
    if (len > EPC_PAGE_SIZE) len = EPC_PAGE_SIZE;
    xor_encrypt(e->sealing_key, data,
                e->epc.encrypted_pages[page_idx], len);
    enclave_measure(e, data, len);
}

void enclave_measure(Enclave *e, const uint8_t *data, int len) {
    uint8_t temp[SHA256_DIGEST * 2];
    memcpy(temp, e->mrenclave_measured, SHA256_DIGEST);
    memcpy(temp + SHA256_DIGEST, data, len < SHA256_DIGEST ? len : SHA256_DIGEST);
    sha256_hash(temp, SHA256_DIGEST + (len < SHA256_DIGEST ? len : SHA256_DIGEST),
                e->mrenclave_measured);
}

void enclave_init(Enclave *e) {
    if (e->state != ENCLAVE_BUILDING) return;
    e->state = ENCLAVE_READY;
    memcpy(e->epc.mrenclave, e->mrenclave_measured, SHA256_DIGEST);
}

void enclave_enter(Enclave *e, int entry_func_idx) {
    if (e->state != ENCLAVE_READY && e->state != ENCLAVE_ATTESTED) return;
    if (entry_func_idx < 0 || entry_func_idx >= e->entry_count) return;
    e->state = ENCLAVE_RUNNING;
}

void enclave_exit(Enclave *e) {
    if (e->state == ENCLAVE_RUNNING) {
        e->state = ENCLAVE_READY;
    }
}

void enclave_attest(Enclave *e, uint8_t report[64]) {
    memset(report, 0, 64);
    memcpy(report, e->mrenclave_measured, SHA256_DIGEST);
    for (int i = 0; i < 32; i++) {
        report[32 + i] = (uint8_t)(e->mrenclave_measured[i] ^ 0xFF);
    }
    memcpy(e->attestation_report, report, 64);
    if (e->state == ENCLAVE_READY) {
        e->state = ENCLAVE_ATTESTED;
    }
}

void enclave_seal_data(Enclave *e, const uint8_t *data, int len,
                        uint8_t sealed[SEALED_SIZE_MAX]) {
    if (len + 8 > SEALED_SIZE_MAX) len = SEALED_SIZE_MAX - 8;
    sealed[0] = (uint8_t)(len & 0xFF);
    sealed[1] = (uint8_t)((len >> 8) & 0xFF);
    sealed[2] = (uint8_t)((len >> 16) & 0xFF);
    sealed[3] = (uint8_t)((len >> 24) & 0xFF);
    memcpy(sealed + 4, "SEAL", 4);
    xor_encrypt(e->sealing_key, data, sealed + 8, len);
}

void enclave_unseal_data(Enclave *e, const uint8_t *sealed, int len,
                          uint8_t *data) {
    int data_len = (int)(sealed[0] | (sealed[1] << 8) |
                         (sealed[2] << 16) | (sealed[3] << 24));
    if (data_len < 0 || data_len > SEALED_SIZE_MAX) return;
    xor_encrypt(e->sealing_key, sealed + 8, data, data_len);
    (void)len;
}

void enclave_register_entry(Enclave *e, int epc_offset) {
    if (e->entry_count < 8) {
        e->entry_points[e->entry_count++] = epc_offset;
    }
}
