#include "rdma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void rdma_init(RDMAContext *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->protection_domain = 1;
    ctx->num_mr = 0;
    ctx->num_qp = 0;
    ctx->cq.head = 0;
    ctx->cq.tail = 0;
    ctx->cq.count = 0;
    memset(ctx->cq.cq_entries, 0, sizeof(ctx->cq.cq_entries));
}

int rdma_reg_mr(RDMAContext *ctx, void *addr, int len)
{
    if (ctx->num_mr >= RDMA_MAX_MR) return -1;
    if (!addr || len <= 0) return -1;

    uint32_t idx = ctx->num_mr;
    ctx->memory_regions[idx].addr = (uint64_t)(uintptr_t)addr;
    ctx->memory_regions[idx].len  = (uint32_t)len;
    ctx->memory_regions[idx].lkey = 0x1000 + idx;
    ctx->memory_regions[idx].rkey = 0x2000 + idx;
    ctx->num_mr++;
    return 0;
}

int rdma_create_qp(RDMAContext *ctx, RDMAQPState initial_state)
{
    if (ctx->num_qp >= RDMA_MAX_QP) return -1;

    uint32_t idx = ctx->num_qp;
    ctx->queue_pairs[idx].qpn     = idx;
    ctx->queue_pairs[idx].send_cq = 0;
    ctx->queue_pairs[idx].recv_cq = 0;
    ctx->queue_pairs[idx].state   = initial_state;
    ctx->num_qp++;
    return (int)idx;
}

int rdma_modify_qp(RDMAContext *ctx, int qpn, RDMAQPState new_state)
{
    if (qpn < 0 || qpn >= (int)ctx->num_qp) return -1;
    ctx->queue_pairs[qpn].state = new_state;
    return 0;
}

int rdma_post_send(RDMAContext *ctx, int qpn, const RDMAWQE *wqe)
{
    if (qpn < 0 || qpn >= (int)ctx->num_qp) return -1;
    if (!wqe) return -1;

    RDMAQP *qp = &ctx->queue_pairs[qpn];
    if (qp->state != RDMA_QP_RTS) return -1;

    if (ctx->cq.count >= RDMA_CQ_DEPTH) return -1;

    uint32_t result = (wqe->opcode == RDMA_OP_WRITE) ? wqe->length : 1;
    ctx->cq.cq_entries[ctx->cq.tail] = result;
    ctx->cq.tail = (ctx->cq.tail + 1) % RDMA_CQ_DEPTH;
    ctx->cq.count++;

    return 0;
}

int rdma_poll_cq(RDMAContext *ctx, uint32_t *wqe_result)
{
    if (ctx->cq.count == 0) return -1;

    *wqe_result = ctx->cq.cq_entries[ctx->cq.head];
    ctx->cq.head = (ctx->cq.head + 1) % RDMA_CQ_DEPTH;
    ctx->cq.count--;
    return 0;
}

int rdma_remote_write(RDMAContext *ctx, int qpn, void *remote_addr,
                      const void *local_data, int len)
{
    if (qpn < 0 || qpn >= (int)ctx->num_qp) return -1;
    if (!remote_addr || !local_data || len <= 0) return -1;

    RDMAQP *qp = &ctx->queue_pairs[qpn];
    if (qp->state != RDMA_QP_RTS) return -1;

    memcpy(remote_addr, local_data, (size_t)len);

    if (ctx->cq.count < RDMA_CQ_DEPTH) {
        ctx->cq.cq_entries[ctx->cq.tail] = (uint32_t)len;
        ctx->cq.tail = (ctx->cq.tail + 1) % RDMA_CQ_DEPTH;
        ctx->cq.count++;
    }

    return len;
}

void rdma_dump_qp(const RDMAContext *ctx, int qpn)
{
    if (qpn < 0 || qpn >= (int)ctx->num_qp) {
        printf("QP %d: invalid\n", qpn);
        return;
    }
    const RDMAQP *qp = &ctx->queue_pairs[qpn];
    const char *state_str;
    switch (qp->state) {
        case RDMA_QP_RESET: state_str = "RESET"; break;
        case RDMA_QP_INIT:  state_str = "INIT";  break;
        case RDMA_QP_RTR:   state_str = "RTR";   break;
        case RDMA_QP_RTS:   state_str = "RTS";   break;
        case RDMA_QP_ERROR: state_str = "ERROR"; break;
        default:            state_str = "UNKNOWN"; break;
    }
    printf("QP %u: state=%s send_cq=%u recv_cq=%u\n",
           qp->qpn, state_str, qp->send_cq, qp->recv_cq);
}

void rdma_dump_mr(const RDMAContext *ctx)
{
    printf("=== RDMA Memory Regions (PD=%u) ===\n", ctx->protection_domain);
    for (uint32_t i = 0; i < ctx->num_mr; i++) {
        const RDMAMemoryRegion *mr = &ctx->memory_regions[i];
        printf("MR[%u]: addr=0x%llX len=%u lkey=0x%X rkey=0x%X\n",
               i, (unsigned long long)mr->addr, mr->len, mr->lkey, mr->rkey);
    }
}
