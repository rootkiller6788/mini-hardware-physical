#ifndef SECURE_ENCLAVE_H
#define SECURE_ENCLAVE_H

#include <stdbool.h>
#include <stdint.h>

#define EPC_PAGES       128
#define EPC_PAGE_SIZE   4096
#define SHA256_DIGEST    32
#define SEALED_SIZE_MAX 512

typedef enum {
    ENCLAVE_UNINIT,
    ENCLAVE_BUILDING,
    ENCLAVE_READY,
    ENCLAVE_RUNNING,
    ENCLAVE_ATTESTED
} EnclaveState;

typedef enum {
    SGX_ECREATE,
    SGX_EADD,
    SGX_EEXTEND,
    SGX_EINIT,
    SGX_EENTER,
    SGX_EEXIT,
    SGX_EREPORT,
    SGX_EGETKEY
} SGXInstruction;

typedef struct {
    uint8_t encrypted_pages[EPC_PAGES][EPC_PAGE_SIZE];
    uint8_t mrenclave[SHA256_DIGEST];
} EnclaveMemory;

typedef struct {
    EnclaveState state;
    EnclaveMemory epc;
    int entry_points[8];
    int entry_count;
    uint8_t sealing_key[SHA256_DIGEST];
    uint8_t attestation_report[64];
    uint8_t mrenclave_measured[SHA256_DIGEST];
} Enclave;

void enclave_create(Enclave *e);
void enclave_add_page(Enclave *e, const uint8_t *data, int len, uint64_t addr);
void enclave_measure(Enclave *e, const uint8_t *data, int len);
void enclave_init(Enclave *e);
void enclave_enter(Enclave *e, int entry_func_idx);
void enclave_exit(Enclave *e);
void enclave_attest(Enclave *e, uint8_t report[64]);
void enclave_seal_data(Enclave *e, const uint8_t *data, int len,
                       uint8_t sealed[SEALED_SIZE_MAX]);
void enclave_unseal_data(Enclave *e, const uint8_t *sealed, int len,
                         uint8_t *data);
void enclave_register_entry(Enclave *e, int epc_offset);

#endif
