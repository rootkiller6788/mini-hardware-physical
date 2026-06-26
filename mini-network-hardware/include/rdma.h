#ifndef RDMA_H
#define RDMA_H

#include <stdbool.h>
#include <stdint.h>

#define RDMA_MAX_QP        64
#define RDMA_MAX_MR        128
#define RDMA_CQ_DEPTH      256
#define RDMA_MAX_WQE_SIZE  4096

typedef enum {
    RDMA_QP_RESET = 0,
    RDMA_QP_INIT  = 1,
    RDMA_QP_RTR   = 2,
    RDMA_QP_RTS   = 3,
    RDMA_QP_ERROR = 4
} RDMAQPState;

typedef enum {
    RDMA_OP_SEND   = 0,
    RDMA_OP_WRITE  = 1,
    RDMA_OP_READ   = 2,
    RDMA_OP_ATOMIC = 3
} RDMAOpcode;

typedef struct {
    uint32_t qpn;
    uint32_t send_cq;
    uint32_t recv_cq;
    RDMAQPState state;
} RDMAQP;

typedef struct {
    RDMAOpcode opcode;
    uint64_t   addr;
    uint32_t   length;
    uint32_t   lkey;
    uint32_t   rkey;
    uint8_t    data[RDMA_MAX_WQE_SIZE];
} RDMAWQE;

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint32_t lkey;
    uint32_t rkey;
} RDMAMemoryRegion;

typedef struct {
    uint32_t cq_entries[RDMA_CQ_DEPTH];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} RDMACompletionQueue;

typedef struct {
    uint32_t           protection_domain;
    RDMAMemoryRegion   memory_regions[RDMA_MAX_MR];
    uint32_t           num_mr;
    RDMAQP             queue_pairs[RDMA_MAX_QP];
    uint32_t           num_qp;
    RDMACompletionQueue cq;
    uint8_t            local_buffer[65536];
} RDMAContext;

void rdma_init(RDMAContext *ctx);
int  rdma_reg_mr(RDMAContext *ctx, void *addr, int len);
int  rdma_create_qp(RDMAContext *ctx, RDMAQPState initial_state);
int  rdma_modify_qp(RDMAContext *ctx, int qpn, RDMAQPState new_state);
int  rdma_post_send(RDMAContext *ctx, int qpn, const RDMAWQE *wqe);
int  rdma_poll_cq(RDMAContext *ctx, uint32_t *wqe_result);
int  rdma_remote_write(RDMAContext *ctx, int qpn, void *remote_addr,
                       const void *local_data, int len);
void rdma_dump_qp(const RDMAContext *ctx, int qpn);
void rdma_dump_mr(const RDMAContext *ctx);

#endif
