#ifndef NVME_H
#define NVME_H

#include <stdbool.h>
#include <stdint.h>

#define NVME_MAX_IO_QUEUES    8
#define NVME_QUEUE_DEPTH      64
#define NVME_MAX_NAMESPACES   1
#define NVME_SECTOR_SIZE      512

typedef enum {
    NVME_OP_NOP        = 0x00,
    NVME_OP_FLUSH      = 0x00,
    NVME_OP_WRITE      = 0x01,
    NVME_OP_READ       = 0x02,
    NVME_OP_IDENTIFY   = 0x06,
    NVME_OP_WRITE_ZERO = 0x08,
    NVME_OP_DSM        = 0x09
} NVMeOpcode;

typedef enum {
    NVME_SC_SUCCESS      = 0x0000,
    NVME_SC_INVALID_OP   = 0x0001,
    NVME_SC_INVALID_FIELD = 0x0002,
    NVME_SC_LBA_RANGE    = 0x0080,
    NVME_SC_INTERNAL_ERR = 0x0006
} NVMeStatusField;

typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t command_id;
    uint32_t namespace_id;
    uint64_t reserved;
    uint64_t metadata_ptr;
    uint64_t data_ptr;
    uint64_t slba;
    uint16_t nlb;
    uint16_t dsm_attr;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} NVMeCommand;

typedef struct {
    uint32_t cmd_specific;
    uint32_t reserved;
    uint16_t sq_head_ptr;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
} NVMeCompletion;

typedef struct {
    uint32_t       id;
    uint32_t       depth;
    NVMeCommand    entries[NVME_QUEUE_DEPTH];
    uint32_t       head;
    uint32_t       tail;
    volatile uint32_t doorbell_head;
    volatile uint32_t doorbell_tail;
} NVMeQueue;

typedef struct {
    NVMeQueue      admin_sq;
    NVMeQueue      admin_cq;
    NVMeQueue      io_sqs[NVME_MAX_IO_QUEUES];
    NVMeQueue      io_cqs[NVME_MAX_IO_QUEUES];
    uint32_t       sq_tail_doorbell[1 + NVME_MAX_IO_QUEUES];
    uint32_t       cq_head_doorbell[1 + NVME_MAX_IO_QUEUES];
    bool           cq_phase[NVME_MAX_IO_QUEUES + 1];
    uint64_t       ns_size[NVME_MAX_NAMESPACES];
} NVMeController;

void nvme_init(NVMeController *ctrl);
int  nvme_submit_admin_cmd(NVMeController *ctrl, NVMeCommand *cmd);
int  nvme_submit_io_cmd(NVMeController *ctrl, uint32_t qid, NVMeCommand *cmd);
int  nvme_process_cq(NVMeController *ctrl, uint32_t qid,
                     NVMeCompletion *out_comp);
void nvme_print_regs(const NVMeController *ctrl);

#endif
