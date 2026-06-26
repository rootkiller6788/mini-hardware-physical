#include "nvme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static bool sq_is_full(const NVMeQueue *sq) {
    uint32_t count;
    if (sq->tail >= sq->head) {
        count = sq->tail - sq->head;
    } else {
        count = sq->depth - sq->head + sq->tail;
    }
    return count >= sq->depth - 1;
}

static bool sq_is_empty(const NVMeQueue *sq) {
    return sq->head == sq->tail;
}

void nvme_init(NVMeController *ctrl) {
    uint32_t i;

    memset(ctrl, 0, sizeof(NVMeController));

    ctrl->admin_sq.id    = 0;
    ctrl->admin_sq.depth = NVME_QUEUE_DEPTH;
    ctrl->admin_sq.head  = 0;
    ctrl->admin_sq.tail  = 0;

    ctrl->admin_cq.id    = 0;
    ctrl->admin_cq.depth = NVME_QUEUE_DEPTH;
    ctrl->admin_cq.head  = 0;
    ctrl->admin_cq.tail  = 0;

    for (i = 0; i < NVME_MAX_IO_QUEUES; i++) {
        ctrl->io_sqs[i].id    = i + 1;
        ctrl->io_sqs[i].depth = NVME_QUEUE_DEPTH;
        ctrl->io_sqs[i].head  = 0;
        ctrl->io_sqs[i].tail  = 0;

        ctrl->io_cqs[i].id    = i + 1;
        ctrl->io_cqs[i].depth = NVME_QUEUE_DEPTH;
        ctrl->io_cqs[i].head  = 0;
        ctrl->io_cqs[i].tail  = 0;
    }

    for (i = 0; i < NVME_MAX_NAMESPACES; i++) {
        ctrl->ns_size[i] = 1024 * 1024 * 1024;
    }
}

static int sq_enqueue(NVMeQueue *sq, const NVMeCommand *cmd) {
    if (sq_is_full(sq)) return -1;
    sq->entries[sq->tail] = *cmd;
    sq->tail = (sq->tail + 1) % sq->depth;
    return 0;
}

int nvme_submit_admin_cmd(NVMeController *ctrl, NVMeCommand *cmd) {
    if (sq_enqueue(&ctrl->admin_sq, cmd) != 0) return -1;
    ctrl->sq_tail_doorbell[0] = ctrl->admin_sq.tail;
    return 0;
}

int nvme_submit_io_cmd(NVMeController *ctrl, uint32_t qid, NVMeCommand *cmd) {
    if (qid >= NVME_MAX_IO_QUEUES) return -1;
    if (sq_enqueue(&ctrl->io_sqs[qid], cmd) != 0) return -1;
    ctrl->sq_tail_doorbell[qid + 1] = ctrl->io_sqs[qid].tail;
    return 0;
}

int nvme_process_cq(NVMeController *ctrl, uint32_t qid,
                    NVMeCompletion *out_comp) {
    NVMeQueue *cq;

    if (qid == 0) {
        cq = &ctrl->admin_cq;
    } else if (qid <= NVME_MAX_IO_QUEUES) {
        cq = &ctrl->io_cqs[qid - 1];
    } else {
        return -1;
    }

    if (sq_is_empty(cq)) return -1;

    memset(out_comp, 0, sizeof(NVMeCompletion));
    out_comp->sq_head_ptr = cq->head;
    out_comp->sq_id       = (uint16_t)qid;
    out_comp->status      = (uint16_t)NVME_SC_SUCCESS;

    cq->head = (cq->head + 1) % cq->depth;
    ctrl->cq_head_doorbell[qid] = cq->head;

    return 0;
}

void nvme_print_regs(const NVMeController *ctrl) {
    uint32_t i;

    printf("NVMe Controller Registers:\n");
    printf("  Admin SQ: head=%u tail=%u depth=%u\n",
           ctrl->admin_sq.head, ctrl->admin_sq.tail, ctrl->admin_sq.depth);
    printf("  Admin CQ: head=%u tail=%u depth=%u\n",
           ctrl->admin_cq.head, ctrl->admin_cq.tail, ctrl->admin_cq.depth);
    printf("  Doorbell - Admin SQ Tail: %u\n", ctrl->sq_tail_doorbell[0]);
    printf("  Doorbell - Admin CQ Head: %u\n", ctrl->cq_head_doorbell[0]);

    for (i = 0; i < NVME_MAX_IO_QUEUES; i++) {
        printf("  I/O SQ[%u]: head=%u tail=%u depth=%u\n",
               i, ctrl->io_sqs[i].head, ctrl->io_sqs[i].tail,
               ctrl->io_sqs[i].depth);
        printf("  I/O CQ[%u]: head=%u tail=%u depth=%u\n",
               i, ctrl->io_cqs[i].head, ctrl->io_cqs[i].tail,
               ctrl->io_cqs[i].depth);
        printf("  Doorbell - I/O SQ[%u] Tail: %u\n",
               i, ctrl->sq_tail_doorbell[i + 1]);
        printf("  Doorbell - I/O CQ[%u] Head: %u\n",
               i, ctrl->cq_head_doorbell[i + 1]);
    }

    for (i = 0; i < NVME_MAX_NAMESPACES; i++) {
        printf("  Namespace[%u] Size: %llu bytes\n",
               i, (unsigned long long)ctrl->ns_size[i]);
    }
}
